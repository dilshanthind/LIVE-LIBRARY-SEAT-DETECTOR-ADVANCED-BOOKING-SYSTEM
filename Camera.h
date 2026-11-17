
#ifndef CAMERA_H
#define CAMERA_H

#include <algorithm>
#include <ctime>
#include <string>
#include <vector>
#include "Frame.h"

#ifdef USE_OPENCV
#include <opencv2/opencv.hpp>
#endif


class Camera {
protected:
    int w_;
    int h_;

public:
    Camera(int w = 640, int h = 480) : w_(w), h_(h) {}
    Camera(const Camera& other) : w_(other.w_), h_(other.h_) {}
    virtual ~Camera() {}

    inline int width() const { return this->w_; }
    inline int height() const { return this->h_; }

    // Pure virtual functions overridden by concrete camera classes
    virtual Frame grab() = 0;
    virtual std::string kind() const = 0;
};

// Simulated camera that synthesizes a library room image with sensor noise and seated people
class SimulatedCamera : public Camera {
private:
    std::vector<Rect> rois_;
    std::vector<char> present_;
    unsigned int seed_;

    int nextNoise() {
        this->seed_ = this->seed_ * 1103515245u + 12345u;
        return static_cast<int>((this->seed_ / 65536u) % 9u) - 4;
    }

public:
    SimulatedCamera() : Camera(640, 480), rois_(), present_(), seed_(12345u) {}

    SimulatedCamera(int w, int h, const std::vector<Rect>& rois)
        : Camera(w, h), rois_(rois), present_(rois.size(), 0), seed_(12345u) {}

    SimulatedCamera(const SimulatedCamera& other)
        : Camera(other), rois_(other.rois_), present_(other.present_), seed_(other.seed_) {}

    virtual ~SimulatedCamera() {}

    void setPresent(int index, bool on) {
        if (index < 0 || index >= static_cast<int>(this->present_.size())) {
            throw ValidationError("No such seat in the simulation");
        }
        this->present_[static_cast<std::size_t>(index)] = on ? 1 : 0;
    }

    virtual Frame grab() {
        Frame f(this->w_, this->h_);
        for (int y = 0; y < this->h_; ++y) {
            for (int x = 0; x < this->w_; ++x) {
                int v = 112 + (((x / 16) + (y / 16)) % 2) * 6 + this->nextNoise();
                f.at(x, y) = static_cast<uint8_t>(clampValue(v, 0, 255));
            }
        }
        for (std::size_t i = 0; i < this->rois_.size(); ++i) {
            if (!this->present_[i]) {
                continue;
            }
            const Rect& r = this->rois_[i];
            int bx = r.x + (r.w * 2) / 10;
            int by = r.y + (r.h * 40) / 100;
            int bw = (r.w * 6) / 10;
            int bh = (r.h * 60) / 100;
            for (int y = by; y < by + bh && y < this->h_; ++y) {
                for (int x = bx; x < bx + bw && x < this->w_; ++x) {
                    f.at(x, y) = static_cast<uint8_t>(clampValue(40 + this->nextNoise(), 0, 255));
                }
            }
            int cx = r.x + r.w / 2;
            int cy = r.y + (r.h * 25) / 100;
            int rad = (r.h * 18) / 100;
            for (int y = cy - rad; y <= cy + rad; ++y) {
                for (int x = cx - rad; x <= cx + rad; ++x) {
                    if (x >= 0 && y >= 0 && x < this->w_ && y < this->h_ &&
                        (x - cx) * (x - cx) + (y - cy) * (y - cy) <= rad * rad) {
                        f.at(x, y) = static_cast<uint8_t>(clampValue(70 + this->nextNoise(), 0, 255));
                    }
                }
            }
        }
        return f;
    }

    virtual std::string kind() const {
        return "simulated";
    }
};


class BrowserCamera : public Camera {
private:
    Frame latest_;
    bool have_;
    std::time_t lastPush_;

public:
    BrowserCamera(int w = 640, int h = 480)
        : Camera(w, h), latest_(), have_(false), lastPush_(0) {}

    BrowserCamera(const BrowserCamera& other)
        : Camera(other), latest_(other.latest_), have_(other.have_), lastPush_(other.lastPush_) {}

    virtual ~BrowserCamera() {}

    void push(const std::string& bytes) {
        std::size_t expected = static_cast<std::size_t>(this->w_) * static_cast<std::size_t>(this->h_);
        if (bytes.size() != expected) {
            throw ValidationError("Expected " + std::to_string(expected) + " bytes (" +
                                  std::to_string(this->w_) + "x" + std::to_string(this->h_) +
                                  " grayscale) but got " + std::to_string(bytes.size()));
        }
        Frame f(this->w_, this->h_);
        std::copy(bytes.begin(), bytes.end(), f.data().begin());
        this->latest_ = f;
        this->lastPush_ = std::time(0);
        this->have_ = true;
    }

    virtual Frame grab() {
        if (!this->have_) {
            throw CameraError("Waiting for the browser camera: open the page and press 'Start camera'");
        }
        if (std::difftime(std::time(0), this->lastPush_) > 3.0) {
            throw CameraError("The browser camera stopped sending pictures (is the page still open?)");
        }
        return this->latest_;
    }

    virtual std::string kind() const {
        return "browser camera";
    }
};

#ifdef USE_OPENCV
// Real camera (USB webcam / IP camera) or recorded video file via OpenCV
class OpenCVCamera : public Camera {
private:
    cv::VideoCapture cap_;
    bool isFile_;
    std::string label_;

public:
    // Constructor overload 1: open webcam by index
    OpenCVCamera(int index, int w = 640, int h = 480)
        : Camera(w, h), cap_(index), isFile_(false), label_("webcam " + std::to_string(index)) {
        if (!this->cap_.isOpened()) {
            throw CameraError("Cannot open camera " + std::to_string(index));
        }
    }

    // Constructor overload 2: open video file by path
    OpenCVCamera(const std::string& path, int w = 640, int h = 480)
        : Camera(w, h), cap_(path), isFile_(true), label_("video " + path) {
        if (!this->cap_.isOpened()) {
            throw CameraError("Cannot open video file " + path);
        }
    }

    virtual ~OpenCVCamera() {
        this->cap_.release();
    }

    virtual Frame grab() {
        cv::Mat bgr, gray, small;
        if (!this->cap_.read(bgr) || bgr.empty()) {
            if (this->isFile_) {
                this->cap_.set(cv::CAP_PROP_POS_FRAMES, 0);
                this->cap_.read(bgr);
            }
            if (bgr.empty()) {
                throw CameraError("Camera returned an empty frame");
            }
        }
        cv::cvtColor(bgr, gray, cv::COLOR_BGR2GRAY);
        cv::resize(gray, small, cv::Size(this->w_, this->h_));
        cv::GaussianBlur(small, small, cv::Size(5, 5), 0);
        Frame f(this->w_, this->h_);
        for (int y = 0; y < this->h_; ++y) {
            for (int x = 0; x < this->w_; ++x) {
                f.at(x, y) = small.at<unsigned char>(y, x);
            }
        }
        return f;
    }

    virtual std::string kind() const {
        return this->label_;
    }
};
#endif

#endif
