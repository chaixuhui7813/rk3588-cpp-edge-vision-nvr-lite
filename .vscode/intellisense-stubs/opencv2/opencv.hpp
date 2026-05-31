#pragma once

// Editor-only OpenCV stub for Windows IntelliSense.
// The real project still requires libopencv-dev/OpenCV on Linux/RK3588 and
// CMake does not include this directory during actual builds.

#include <cstddef>
#include <string>
#include <vector>

#define CV_8UC1 0
#define CV_8UC3 16

namespace cv {

struct Size {
    int width = 0;
    int height = 0;
    Size() = default;
    Size(int w, int h) : width(w), height(h) {}
};

struct Point {
    int x = 0;
    int y = 0;
    Point() = default;
    Point(int px, int py) : x(px), y(py) {}
};

struct Rect {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
    Rect() = default;
    Rect(int rx, int ry, int rw, int rh) : x(rx), y(ry), width(rw), height(rh) {}
};

struct Scalar {
    double val[4] = {0.0, 0.0, 0.0, 0.0};
    Scalar() = default;
    Scalar(double v0, double v1, double v2, double v3 = 0.0) : val{v0, v1, v2, v3} {}
};

class Mat {
public:
    int rows = 0;
    int cols = 0;
    unsigned char* data = nullptr;

    Mat() = default;
    Mat(int h, int w, int, Scalar = {}) : rows(h), cols(w) {}
    Mat(int h, int w, int, void*) : rows(h), cols(w) {}

    bool empty() const { return false; }
    int type() const { return 0; }
    Size size() const { return Size(cols, rows); }
    Mat clone() const { return Mat(*this); }
    Mat operator()(const Rect&) const { return Mat(); }
    void copyTo(Mat) const {}
};

class VideoCapture {
public:
    VideoCapture() = default;
    explicit VideoCapture(const std::string&) {}
    explicit VideoCapture(int) {}
    bool open(const std::string&) { return true; }
    bool open(int) { return true; }
    bool isOpened() const { return true; }
    bool read(Mat&) { return true; }
    bool set(int, double) { return true; }
    double get(int) const { return 0.0; }
    void release() {}
};

class VideoWriter {
public:
    static int fourcc(char, char, char, char) { return 0; }
    bool open(const std::string&, int, double, Size, bool = true) { return true; }
    bool isOpened() const { return true; }
    void write(const Mat&) {}
};

enum {
    COLOR_BGR2RGB = 0,
    FONT_HERSHEY_SIMPLEX = 0,
    FILLED = -1,
    CAP_PROP_FRAME_WIDTH = 3,
    CAP_PROP_FRAME_HEIGHT = 4,
    CAP_PROP_POS_FRAMES = 1,
    CAP_PROP_FRAME_COUNT = 7
};

enum ImwriteFlags {
    IMWRITE_JPEG_QUALITY = 1
};

using uchar = unsigned char;

inline void resize(const Mat&, Mat&, Size) {}
inline void cvtColor(const Mat&, Mat&, int) {}
inline void split(const Mat&, std::vector<Mat>&) {}
inline void extractChannel(const Mat&, Mat&, int) {}
inline void circle(Mat&, Point, int, Scalar, int) {}
inline void rectangle(Mat&, Point, Point, Scalar, int) {}
inline void rectangle(Mat&, Rect, Scalar, int) {}
inline void putText(Mat&, const std::string&, Point, int, double, Scalar, int) {}
inline Size getTextSize(const std::string&, int, double, int, int* baseline) {
    if (baseline) {
        *baseline = 0;
    }
    return Size(80, 20);
}
inline bool imencode(const std::string&, const Mat&, std::vector<uchar>&, const std::vector<int>& = {}) { return true; }
inline bool imwrite(const std::string&, const Mat&) { return true; }

}  // namespace cv
