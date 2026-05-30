#include "rknn_detector.hpp"

#include "utils.hpp"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>

#ifdef HAVE_RKNN
#include "rknn_api.h"
#endif

namespace edge {

RknnDetector::RknnDetector(ModelConfig model, RuntimeConfig runtime, std::vector<std::string> classes)
    : model_(std::move(model)), runtime_(std::move(runtime)), classes_(std::move(classes)) {}

#ifdef HAVE_RKNN

struct RknnDetector::Impl {
    struct Context {
        rknn_context ctx = 0;
        std::vector<rknn_tensor_attr> input_attrs;
        std::vector<rknn_tensor_attr> output_attrs;
        rknn_input_output_num io_num{};
        std::vector<unsigned char> nchw_data;
    };
    std::vector<unsigned char> model_data;
    std::vector<Context> contexts;
};

namespace {

std::vector<unsigned char> readBinary(const std::string& path) {
    std::ifstream ifs(path, std::ios::binary);
    if (!ifs) {
        return {};
    }
    return std::vector<unsigned char>((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
}

int workerCoreMask(int worker_id, const RuntimeConfig& runtime) {
    if (runtime.use_all_cores) {
#ifdef RKNN_NPU_CORE_0_1_2
        return RKNN_NPU_CORE_0_1_2;
#else
        return runtime.npu_core_mask;
#endif
    }
    if (worker_id == 0) {
#ifdef RKNN_NPU_CORE_0
        return runtime.worker0_core_mask > 0 ? runtime.worker0_core_mask : RKNN_NPU_CORE_0;
#else
        return runtime.worker0_core_mask > 0 ? runtime.worker0_core_mask : 1;
#endif
    }
    if (worker_id == 1) {
#ifdef RKNN_NPU_CORE_1
        return runtime.worker1_core_mask > 0 ? runtime.worker1_core_mask : RKNN_NPU_CORE_1;
#else
        return runtime.worker1_core_mask > 0 ? runtime.worker1_core_mask : 2;
#endif
    }
#ifdef RKNN_NPU_CORE_2
    return runtime.worker2_core_mask > 0 ? runtime.worker2_core_mask : RKNN_NPU_CORE_2;
#else
    return runtime.worker2_core_mask > 0 ? runtime.worker2_core_mask : 4;
#endif
}

std::vector<int> attrDims(const rknn_tensor_attr& attr) {
    std::vector<int> dims;
    for (uint32_t i = 0; i < attr.n_dims; ++i) {
        dims.push_back(static_cast<int>(attr.dims[i]));
    }
    return dims;
}

}  // namespace

RknnDetector::~RknnDetector() {
    if (!impl_) {
        return;
    }
    for (auto& c : impl_->contexts) {
        if (c.ctx) {
            rknn_destroy(c.ctx);
            c.ctx = 0;
        }
    }
    delete impl_;
    impl_ = nullptr;
}

bool RknnDetector::init() {
    impl_ = new Impl();
    impl_->model_data = readBinary(model_.path);
    if (impl_->model_data.empty()) {
        std::cerr << "[rknn] cannot read model: " << model_.path << std::endl;
        std::cerr << "[rknn] run with --mock until models/yolov8n.rknn is copied to the board" << std::endl;
        return false;
    }
    const int workers = std::max(1, std::min(runtime_.num_workers, 3));
    impl_->contexts.resize(static_cast<size_t>(workers));
    for (int i = 0; i < workers; ++i) {
        auto& c = impl_->contexts[static_cast<size_t>(i)];
        int ret = rknn_init(&c.ctx, impl_->model_data.data(), impl_->model_data.size(), 0, nullptr);
        if (ret != RKNN_SUCC) {
            std::cerr << "[rknn] rknn_init failed for worker " << i << ", ret=" << ret << std::endl;
            return false;
        }
        const int mask = workerCoreMask(i, runtime_);
        ret = rknn_set_core_mask(c.ctx, static_cast<rknn_core_mask>(mask));
        if (ret != RKNN_SUCC) {
            std::cerr << "[rknn] warning: rknn_set_core_mask(" << mask << ") failed for worker "
                      << i << ", ret=" << ret << std::endl;
        }
        ret = rknn_query(c.ctx, RKNN_QUERY_IN_OUT_NUM, &c.io_num, sizeof(c.io_num));
        if (ret != RKNN_SUCC) {
            std::cerr << "[rknn] RKNN_QUERY_IN_OUT_NUM failed, ret=" << ret << std::endl;
            return false;
        }
        c.input_attrs.resize(c.io_num.n_input);
        c.output_attrs.resize(c.io_num.n_output);
        for (uint32_t k = 0; k < c.io_num.n_input; ++k) {
            std::memset(&c.input_attrs[k], 0, sizeof(rknn_tensor_attr));
            c.input_attrs[k].index = k;
            rknn_query(c.ctx, RKNN_QUERY_INPUT_ATTR, &c.input_attrs[k], sizeof(rknn_tensor_attr));
        }
        for (uint32_t k = 0; k < c.io_num.n_output; ++k) {
            std::memset(&c.output_attrs[k], 0, sizeof(rknn_tensor_attr));
            c.output_attrs[k].index = k;
            rknn_query(c.ctx, RKNN_QUERY_OUTPUT_ATTR, &c.output_attrs[k], sizeof(rknn_tensor_attr));
        }
        std::cout << "[rknn] worker " << i << " initialized, core_mask=" << mask
                  << ", outputs=" << c.io_num.n_output << std::endl;
    }
    available_ = true;
    return true;
}

std::vector<Detection> RknnDetector::detect(const cv::Mat& frame, int worker_id, StageTimings& timings) {
    if (!available_ || !impl_ || impl_->contexts.empty() || frame.empty()) {
        return {};
    }
    auto& c = impl_->contexts[static_cast<size_t>(worker_id) % impl_->contexts.size()];

    const auto preprocess_start = Clock::now();
    yolo::LetterboxInfo lb;
    cv::Mat input_bgr = yolo::letterbox(frame, model_.input_width, model_.input_height, lb, model_.letterbox_value);
    cv::Mat input_rgb;
    cv::cvtColor(input_bgr, input_rgb, cv::COLOR_BGR2RGB);
    int input_fmt = RKNN_TENSOR_NHWC;
    if (!c.input_attrs.empty()) {
        input_fmt = c.input_attrs[0].fmt;
    }
    if (input_fmt == RKNN_TENSOR_NCHW) {
        c.nchw_data.resize(static_cast<size_t>(model_.input_width * model_.input_height * 3));
        const size_t plane = static_cast<size_t>(model_.input_width * model_.input_height);
        for (int ch = 0; ch < 3; ++ch) {
            cv::Mat dst(model_.input_height, model_.input_width, CV_8UC1,
                        c.nchw_data.data() + plane * static_cast<size_t>(ch));
            cv::extractChannel(input_rgb, dst, ch);
        }
    }
    timings.preprocess_ms = msSince(preprocess_start);

    rknn_input input{};
    input.index = 0;
    input.type = RKNN_TENSOR_UINT8;
    input.fmt = static_cast<rknn_tensor_format>(input_fmt);
    input.size = static_cast<uint32_t>(model_.input_width * model_.input_height * 3);
    input.buf = (input_fmt == RKNN_TENSOR_NCHW) ? static_cast<void*>(c.nchw_data.data())
                                                : static_cast<void*>(input_rgb.data);

    const auto infer_start = Clock::now();
    int ret = rknn_inputs_set(c.ctx, 1, &input);
    if (ret != RKNN_SUCC) {
        std::cerr << "[rknn] rknn_inputs_set failed, ret=" << ret << std::endl;
        return {};
    }
    ret = rknn_run(c.ctx, nullptr);
    if (ret != RKNN_SUCC) {
        std::cerr << "[rknn] rknn_run failed, ret=" << ret << std::endl;
        return {};
    }
    std::vector<rknn_output> outputs(c.io_num.n_output);
    for (auto& o : outputs) {
        std::memset(&o, 0, sizeof(rknn_output));
        o.want_float = 1;
    }
    ret = rknn_outputs_get(c.ctx, c.io_num.n_output, outputs.data(), nullptr);
    timings.inference_ms = msSince(infer_start);
    if (ret != RKNN_SUCC) {
        std::cerr << "[rknn] rknn_outputs_get failed, ret=" << ret << std::endl;
        return {};
    }

    const auto post_start = Clock::now();
    std::vector<yolo::TensorView> tensors;
    tensors.reserve(outputs.size());
    for (size_t i = 0; i < outputs.size(); ++i) {
        yolo::TensorView t;
        t.dims = attrDims(c.output_attrs[i]);
        t.count = c.output_attrs[i].n_elems;
        t.data = static_cast<const float*>(outputs[i].buf);
        if (t.data && t.count > 0) {
            tensors.push_back(std::move(t));
        }
    }
    static bool printed_tensor_debug = false;
    if (!printed_tensor_debug && std::getenv("EDGE_DEBUG_TENSORS")) {
        printed_tensor_debug = true;
        for (size_t i = 0; i < tensors.size(); ++i) {
            const auto& t = tensors[i];
            auto mm = std::minmax_element(t.data, t.data + t.count);
            float best_score = -std::numeric_limits<float>::infinity();
            if (t.dims.size() >= 2) {
                const int a = t.dims[t.dims.size() - 2];
                const int b = t.dims[t.dims.size() - 1];
                if (a <= 256 && b > a) {
                    for (int anchor = 0; anchor < b; ++anchor) {
                        for (int cidx = 4; cidx < a; ++cidx) {
                            best_score = std::max(best_score, t.data[static_cast<size_t>(cidx) * b + anchor]);
                        }
                    }
                } else {
                    for (int row = 0; row < a; ++row) {
                        for (int cidx = 4; cidx < b; ++cidx) {
                            best_score = std::max(best_score, t.data[static_cast<size_t>(row) * b + cidx]);
                        }
                    }
                }
            }
            std::cerr << "[rknn-debug] output " << i << " dims=";
            for (int d : t.dims) std::cerr << d << " ";
            std::cerr << "min=" << (t.count == 0 ? 0.0f : *mm.first)
                      << " max=" << (t.count == 0 ? 0.0f : *mm.second)
                      << " best_class_score=" << best_score << std::endl;
        }
    }
    auto detections = yolo::postprocessYoloViews(tensors, model_.input_width, model_.input_height,
                                                 frame.size(), lb, classes_,
                                                 model_.conf_threshold, model_.nms_threshold);
    rknn_outputs_release(c.ctx, c.io_num.n_output, outputs.data());
    timings.postprocess_ms = msSince(post_start);
    return detections;
}

#else

struct RknnDetector::Impl {};

RknnDetector::~RknnDetector() {
    delete impl_;
}

bool RknnDetector::init() {
    std::cerr << "[rknn] RKNN Runtime was not found at CMake configure time." << std::endl;
    std::cerr << "[rknn] Install/copy rknn_api.h and librknnrt.so, then rebuild; or run with --mock." << std::endl;
    return false;
}

std::vector<Detection> RknnDetector::detect(const cv::Mat&, int, StageTimings&) {
    return {};
}

#endif

}  // namespace edge
