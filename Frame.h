
#ifndef FRAME_H
#define FRAME_H

#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <vector>
#include "Exceptions.h"

typedef unsigned char uint8_t;

// Function template for clamping a value into [lo, hi]
template <typename T>
inline T clampValue(T v, T lo, T hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

// Region of interest rectangle in a camera frame
class Rect {
public:
    int x;
    int y;
    int w;
    int h;

    Rect() : x(0), y(0), w(0), h(0) {}
    Rect(int x_, int y_, int w_, int h_) : x(x_), y(y_), w(w_), h(h_) {}
    Rect(const Rect& other) : x(other.x), y(other.y), w(other.w), h(other.h) {}

    Rect& operator=(const Rect& other) {
        if (this != &other) {
            this->x = other.x;
            this->y = other.y;
            this->w = other.w;
            this->h = other.h;
        }
        return *this;
    }

    ~Rect() {}

    bool operator==(const Rect& other) const {
        return this->x == other.x && this->y == other.y &&
               this->w == other.w && this->h == other.h;
    }

    friend std::istream& operator>>(std::istream& in, Rect& r);
    friend std::ostream& operator<<(std::ostream& out, const Rect& r);
};

inline std::istream& operator>>(std::istream& in, Rect& r) {
    return in >> r.x >> r.y >> r.w >> r.h;
}

inline std::ostream& operator<<(std::ostream& out, const Rect& r) {
    return out << r.x << ' ' << r.y << ' ' << r.w << ' ' << r.h;
}

// A grayscale camera frame
class Frame {
private:
    int w_;
    int h_;
    std::vector<uint8_t> px_;

public:
    Frame() : w_(0), h_(0), px_() {}

    Frame(int w, int h, uint8_t fill = 0)
        : w_(w), h_(h), px_(static_cast<std::size_t>(w) * static_cast<std::size_t>(h), fill) {}

    Frame(const Frame& o) : w_(o.w_), h_(o.h_), px_(o.px_) {}

    Frame& operator=(const Frame& o) {
        if (this != &o) {
            this->w_ = o.w_;
            this->h_ = o.h_;
            this->px_ = o.px_;
        }
        return *this;
    }

    ~Frame() {}

    inline int width() const { return this->w_; }
    inline int height() const { return this->h_; }
    inline bool empty() const { return this->px_.empty(); }

    inline uint8_t& at(int x, int y) {
        return this->px_[static_cast<std::size_t>(y) * static_cast<std::size_t>(this->w_) + static_cast<std::size_t>(x)];
    }

    inline uint8_t at(int x, int y) const {
        return this->px_[static_cast<std::size_t>(y) * static_cast<std::size_t>(this->w_) + static_cast<std::size_t>(x)];
    }

    inline std::vector<uint8_t>& data() { return this->px_; }
    inline const std::vector<uint8_t>& data() const { return this->px_; }

    // Overloaded binary operator- : per-pixel absolute difference of two frames
    Frame operator-(const Frame& o) const;

    // Overloaded functions: mean pixel value inside a rectangular region
    double meanIn(const Rect& r) const;
    double meanIn(int x, int y, int w, int h) const;

    // Median of (this - other) over every pixel to measure global brightness shift
    int medianShiftFrom(const Frame& o) const;

    // Return a copy of the frame with every pixel shifted by delta (clamped to 0..255)
    Frame brightened(int delta) const;
};

// Member functions defined outside the class using scope resolution operator (Frame::)
inline Frame Frame::operator-(const Frame& o) const {
    if (this->w_ != o.w_ || this->h_ != o.h_) {
        throw CameraError("Frame size mismatch");
    }
    Frame d(this->w_, this->h_);
    for (std::size_t i = 0; i < this->px_.size(); ++i) {
        int diff = static_cast<int>(this->px_[i]) - static_cast<int>(o.px_[i]);
        d.px_[i] = static_cast<uint8_t>(std::abs(diff));
    }
    return d;
}

inline double Frame::meanIn(const Rect& r) const {
    int x0 = clampValue(r.x, 0, this->w_);
    int y0 = clampValue(r.y, 0, this->h_);
    int x1 = clampValue(r.x + r.w, 0, this->w_);
    int y1 = clampValue(r.y + r.h, 0, this->h_);
    if (x1 <= x0 || y1 <= y0) {
        return 0.0;
    }
    long sum = 0;
    for (int y = y0; y < y1; ++y) {
        for (int x = x0; x < x1; ++x) {
            sum += static_cast<long>(this->at(x, y));
        }
    }
    double count = static_cast<double>(x1 - x0) * static_cast<double>(y1 - y0);
    return static_cast<double>(sum) / count;
}

inline double Frame::meanIn(int x, int y, int w, int h) const {
    return this->meanIn(Rect(x, y, w, h));
}

inline int Frame::medianShiftFrom(const Frame& o) const {
    if (this->w_ != o.w_ || this->h_ != o.h_) {
        throw CameraError("Frame size mismatch");
    }
    if (this->px_.empty()) {
        return 0;
    }
    std::vector<std::size_t> hist(511, 0);
    for (std::size_t i = 0; i < this->px_.size(); ++i) {
        int bin = static_cast<int>(this->px_[i]) - static_cast<int>(o.px_[i]) + 255;
        ++hist[static_cast<std::size_t>(bin)];
    }
    std::size_t half = this->px_.size() / 2;
    std::size_t acc = 0;
    for (int b = 0; b < 511; ++b) {
        acc += hist[static_cast<std::size_t>(b)];
        if (acc > half) {
            return b - 255;
        }
    }
    return 0;
}

inline Frame Frame::brightened(int delta) const {
    Frame r(this->w_, this->h_);
    for (std::size_t i = 0; i < this->px_.size(); ++i) {
        int val = static_cast<int>(this->px_[i]) + delta;
        r.px_[i] = static_cast<uint8_t>(clampValue(val, 0, 255));
    }
    return r;
}

#endif
