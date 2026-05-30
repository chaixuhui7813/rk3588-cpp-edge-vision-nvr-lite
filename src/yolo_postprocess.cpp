#include "yolo_postprocess.hpp"

#include <algorithm>
#include <cmath>

namespace edge::yolo {
namespace {

float iou(const Detection& a, const Detection& b) {
    const float xx1 = std::max(a.x1, b.x1);
    const float yy1 = std::max(a.y1, b.y1);
    const float xx2 = std::min(a.x2, b.x2);
    const float yy2 = std::min(a.y2, b.y2);
    const float w = std::max(0.0f, xx2 - xx1);
    const float h = std::max(0.0f, yy2 - yy1);
    const float inter = w * h;
    const float area_a = std::max(0.0f, a.x2 - a.x1) * std::max(0.0f, a.y2 - a.y1);
    const float area_b = std::max(0.0f, b.x2 - b.x1) * std::max(0.0f, b.y2 - b.y1);
    return inter / std::max(1e-6f, area_a + area_b - inter);
}

float clampf(float value, float lo, float hi) {
    return std::max(lo, std::min(value, hi));
}

Detection mapXYXY(float x1, float y1, float x2, float y2, int class_id, float conf,
                  const cv::Size& original, const LetterboxInfo& lb,
                  const std::vector<std::string>& names) {
    Detection d;
    d.class_id = class_id;
    d.class_name = (class_id >= 0 && class_id < static_cast<int>(names.size())) ? names[class_id] : "class_" + std::to_string(class_id);
    d.confidence = conf;
    d.x1 = clampf((x1 - lb.pad_x) / lb.scale, 0.0f, static_cast<float>(original.width - 1));
    d.y1 = clampf((y1 - lb.pad_y) / lb.scale, 0.0f, static_cast<float>(original.height - 1));
    d.x2 = clampf((x2 - lb.pad_x) / lb.scale, 0.0f, static_cast<float>(original.width - 1));
    d.y2 = clampf((y2 - lb.pad_y) / lb.scale, 0.0f, static_cast<float>(original.height - 1));
    return d;
}

Detection mapBox(float cx, float cy, float w, float h, int class_id, float conf,
                 const cv::Size& original, const LetterboxInfo& lb,
                 const std::vector<std::string>& names) {
    Detection d;
    d.class_id = class_id;
    d.class_name = (class_id >= 0 && class_id < static_cast<int>(names.size())) ? names[class_id] : "class_" + std::to_string(class_id);
    d.confidence = conf;
    const float x1 = (cx - w * 0.5f - lb.pad_x) / lb.scale;
    const float y1 = (cy - h * 0.5f - lb.pad_y) / lb.scale;
    const float x2 = (cx + w * 0.5f - lb.pad_x) / lb.scale;
    const float y2 = (cy + h * 0.5f - lb.pad_y) / lb.scale;
    d.x1 = clampf(x1, 0.0f, static_cast<float>(original.width - 1));
    d.y1 = clampf(y1, 0.0f, static_cast<float>(original.height - 1));
    d.x2 = clampf(x2, 0.0f, static_cast<float>(original.width - 1));
    d.y2 = clampf(y2, 0.0f, static_cast<float>(original.height - 1));
    return d;
}

void decodeRows(const float* data, int rows, int cols, const cv::Size& original,
                const LetterboxInfo& lb, const std::vector<std::string>& names,
                float conf_threshold, std::vector<Detection>& out) {
    if (cols < 6) {
        return;
    }
    const bool has_objectness = cols >= static_cast<int>(names.size()) + 5;
    const int class_offset = has_objectness ? 5 : 4;
    const int class_count = cols - class_offset;
    for (int i = 0; i < rows; ++i) {
        const float* row = data + i * cols;
        int best_id = 0;
        float best_score = 0.0f;
        for (int c = 0; c < class_count; ++c) {
            if (row[class_offset + c] > best_score) {
                best_score = row[class_offset + c];
                best_id = c;
            }
        }
        const float objectness = has_objectness ? row[4] : 1.0f;
        const float conf = objectness * best_score;
        if (conf < conf_threshold) {
            continue;
        }
        out.push_back(mapBox(row[0], row[1], row[2], row[3], best_id, conf, original, lb, names));
    }
}

void decodeChannelsFirst(const float* data, int channels, int anchors, const cv::Size& original,
                         const LetterboxInfo& lb, const std::vector<std::string>& names,
                         float conf_threshold, std::vector<Detection>& out) {
    if (channels < 6) {
        return;
    }
    const bool has_objectness = channels >= static_cast<int>(names.size()) + 5;
    const int class_offset = has_objectness ? 5 : 4;
    const int class_count = channels - class_offset;
    for (int i = 0; i < anchors; ++i) {
        int best_id = 0;
        float best_score = 0.0f;
        for (int c = 0; c < class_count; ++c) {
            const float score = data[(class_offset + c) * anchors + i];
            if (score > best_score) {
                best_score = score;
                best_id = c;
            }
        }
        const float objectness = has_objectness ? data[4 * anchors + i] : 1.0f;
        const float conf = objectness * best_score;
        if (conf < conf_threshold) {
            continue;
        }
        out.push_back(mapBox(data[i], data[anchors + i], data[2 * anchors + i], data[3 * anchors + i],
                             best_id, conf, original, lb, names));
    }
}

bool shape4NCHW(const TensorView& t, int& c, int& h, int& w) {
    if (t.dims.size() != 4) {
        return false;
    }
    c = t.dims[1];
    h = t.dims[2];
    w = t.dims[3];
    return c > 0 && h > 0 && w > 0 && t.count >= static_cast<size_t>(c) * h * w;
}

float nchwAt(const TensorView& t, int c, int y, int x, int h, int w) {
    (void)h;
    return t.data[(static_cast<size_t>(c) * static_cast<size_t>(h) + static_cast<size_t>(y)) *
                  static_cast<size_t>(w) + static_cast<size_t>(x)];
}

float dflExpected(const TensorView& box, int side, int y, int x, int h, int w, int bins) {
    const int base_c = side * bins;
    float max_v = -1.0e30f;
    for (int i = 0; i < bins; ++i) {
        max_v = std::max(max_v, nchwAt(box, base_c + i, y, x, h, w));
    }
    float sum = 0.0f;
    float weighted = 0.0f;
    for (int i = 0; i < bins; ++i) {
        const float e = std::exp(nchwAt(box, base_c + i, y, x, h, w) - max_v);
        sum += e;
        weighted += e * static_cast<float>(i);
    }
    return sum > 0.0f ? weighted / sum : 0.0f;
}

void decodeRockchipYolov8(const std::vector<TensorView>& outputs,
                          int input_w,
                          int input_h,
                          const cv::Size& original,
                          const LetterboxInfo& lb,
                          const std::vector<std::string>& names,
                          float conf_threshold,
                          std::vector<Detection>& out) {
    struct Branch {
        const TensorView* box = nullptr;
        const TensorView* cls = nullptr;
        const TensorView* score = nullptr;
        int h = 0;
        int w = 0;
    };
    std::vector<Branch> branches;
    for (const auto& box : outputs) {
        int bc = 0, bh = 0, bw = 0;
        if (!shape4NCHW(box, bc, bh, bw) || bc % 4 != 0 || bc < 16) {
            continue;
        }
        const TensorView* cls = nullptr;
        const TensorView* score = nullptr;
        for (const auto& cand : outputs) {
            int cc = 0, ch = 0, cw = 0;
            if (shape4NCHW(cand, cc, ch, cw) && ch == bh && cw == bw &&
                cc >= static_cast<int>(names.size()) && cc <= 512 && cc != bc) {
                cls = &cand;
                break;
            }
        }
        for (const auto& cand : outputs) {
            int sc = 0, sh = 0, sw = 0;
            if (shape4NCHW(cand, sc, sh, sw) && sc == 1 && sh == bh && sw == bw) {
                score = &cand;
                break;
            }
        }
        if (cls) {
            branches.push_back(Branch{&box, cls, score, bh, bw});
        }
    }
    std::sort(branches.begin(), branches.end(), [](const Branch& a, const Branch& b) {
        return (a.h * a.w) > (b.h * b.w);
    });
    if (branches.empty()) {
        return;
    }

    const int class_count = static_cast<int>(names.size());
    for (const auto& br : branches) {
        int box_c = 0, box_h = 0, box_w = 0;
        int cls_c = 0, cls_h = 0, cls_w = 0;
        shape4NCHW(*br.box, box_c, box_h, box_w);
        shape4NCHW(*br.cls, cls_c, cls_h, cls_w);
        const int bins = box_c / 4;
        const float stride_x = static_cast<float>(input_w) / static_cast<float>(box_w);
        const float stride_y = static_cast<float>(input_h) / static_cast<float>(box_h);
        const int classes_to_scan = std::min(class_count, cls_c);
        for (int y = 0; y < box_h; ++y) {
            for (int x = 0; x < box_w; ++x) {
                if (br.score && nchwAt(*br.score, 0, y, x, box_h, box_w) < conf_threshold) {
                    continue;
                }
                int best_id = 0;
                float best_score = 0.0f;
                for (int c = 0; c < classes_to_scan; ++c) {
                    const float score = nchwAt(*br.cls, c, y, x, cls_h, cls_w);
                    if (score > best_score) {
                        best_score = score;
                        best_id = c;
                    }
                }
                if (best_score < conf_threshold) {
                    continue;
                }
                const float l = dflExpected(*br.box, 0, y, x, box_h, box_w, bins);
                const float t = dflExpected(*br.box, 1, y, x, box_h, box_w, bins);
                const float r = dflExpected(*br.box, 2, y, x, box_h, box_w, bins);
                const float b = dflExpected(*br.box, 3, y, x, box_h, box_w, bins);
                const float cx = (static_cast<float>(x) + 0.5f);
                const float cy = (static_cast<float>(y) + 0.5f);
                out.push_back(mapXYXY((cx - l) * stride_x, (cy - t) * stride_y,
                                      (cx + r) * stride_x, (cy + b) * stride_y,
                                      best_id, best_score, original, lb, names));
            }
        }
    }
}

}  // namespace

cv::Mat letterbox(const cv::Mat& src, int input_w, int input_h, LetterboxInfo& info, int pad_value) {
    const float scale = std::min(input_w / static_cast<float>(src.cols), input_h / static_cast<float>(src.rows));
    info.scale = scale;
    info.new_w = std::max(1, static_cast<int>(std::round(src.cols * scale)));
    info.new_h = std::max(1, static_cast<int>(std::round(src.rows * scale)));
    info.pad_x = (input_w - info.new_w) / 2;
    info.pad_y = (input_h - info.new_h) / 2;

    cv::Mat resized;
    cv::resize(src, resized, cv::Size(info.new_w, info.new_h));
    cv::Mat out(input_h, input_w, src.type(), cv::Scalar(pad_value, pad_value, pad_value));
    resized.copyTo(out(cv::Rect(info.pad_x, info.pad_y, info.new_w, info.new_h)));
    return out;
}

std::vector<Detection> postprocessYoloViews(
    const std::vector<TensorView>& outputs,
    int input_w,
    int input_h,
    const cv::Size& original_size,
    const LetterboxInfo& lb,
    const std::vector<std::string>& class_names,
    float conf_threshold,
    float nms_threshold) {
    (void)input_w;
    (void)input_h;
    std::vector<Detection> detections;
    if (outputs.size() >= 6) {
        decodeRockchipYolov8(outputs, input_w, input_h, original_size, lb, class_names, conf_threshold, detections);
        if (!detections.empty()) {
            return nmsDetections(detections, nms_threshold);
        }
    }

    for (const auto& out : outputs) {
        if (!out.data || out.count == 0 || out.dims.empty()) {
            continue;
        }

        // This block supports common YOLOv8 export shapes:
        //   [1, 84, 8400] / [84, 8400] channels-first
        //   [1, 8400, 84] / [8400, 84] rows-first
        // If your RKNN model exports decoded boxes, anchors, masks, or a custom head,
        // adapt this shape switch and keep the rest of the pipeline unchanged.
        if (out.dims.size() >= 3) {
            const int a = out.dims[out.dims.size() - 2];
            const int b = out.dims[out.dims.size() - 1];
            if (a <= 256 && b > a) {
                decodeChannelsFirst(out.data, a, b, original_size, lb, class_names, conf_threshold, detections);
            } else {
                decodeRows(out.data, a, b, original_size, lb, class_names, conf_threshold, detections);
            }
        } else if (out.dims.size() == 2) {
            const int a = out.dims[0];
            const int b = out.dims[1];
            if (a <= 256 && b > a) {
                decodeChannelsFirst(out.data, a, b, original_size, lb, class_names, conf_threshold, detections);
            } else {
                decodeRows(out.data, a, b, original_size, lb, class_names, conf_threshold, detections);
            }
        }
    }
    return nmsDetections(detections, nms_threshold);
}

std::vector<Detection> postprocessYolo(
    const std::vector<TensorOutput>& outputs,
    int input_w,
    int input_h,
    const cv::Size& original_size,
    const LetterboxInfo& lb,
    const std::vector<std::string>& class_names,
    float conf_threshold,
    float nms_threshold) {
    std::vector<TensorView> views;
    views.reserve(outputs.size());
    for (const auto& out : outputs) {
        views.push_back(TensorView{out.data.data(), out.data.size(), out.dims});
    }
    return postprocessYoloViews(views, input_w, input_h, original_size, lb, class_names,
                                conf_threshold, nms_threshold);
}

std::vector<Detection> nmsDetections(const std::vector<Detection>& detections, float nms_threshold) {
    std::vector<Detection> sorted = detections;
    std::sort(sorted.begin(), sorted.end(), [](const Detection& a, const Detection& b) {
        return a.confidence > b.confidence;
    });
    std::vector<Detection> kept;
    std::vector<bool> removed(sorted.size(), false);
    for (size_t i = 0; i < sorted.size(); ++i) {
        if (removed[i]) {
            continue;
        }
        kept.push_back(sorted[i]);
        for (size_t j = i + 1; j < sorted.size(); ++j) {
            if (!removed[j] && sorted[i].class_id == sorted[j].class_id && iou(sorted[i], sorted[j]) > nms_threshold) {
                removed[j] = true;
            }
        }
    }
    return kept;
}

}  // namespace edge::yolo
