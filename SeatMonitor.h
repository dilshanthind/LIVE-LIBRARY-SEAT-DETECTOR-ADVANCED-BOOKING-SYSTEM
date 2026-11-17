
#ifndef SEAT_MONITOR_H
#define SEAT_MONITOR_H

#include <cstddef>
#include <string>
#include <vector>
#include "BookingManager.h"
#include "Camera.h"
#include "SeatDetector.h"

class SeatMonitor {
private:
    Camera* cam_;
    BookingManager& mgr_;
    SeatDetector detector_;
    int intervalMs_;
    bool running_;
    mutable SpinLock monitorLock_;
    Frame last_;
    std::string status_;

    // Prevent copying of SeatMonitor (owns Camera* pointer)
    SeatMonitor(const SeatMonitor& other);
    SeatMonitor& operator=(const SeatMonitor& other);

    static void appendUint16(std::string& out, unsigned short v) {
        out.push_back(static_cast<char>(v & 255));
        out.push_back(static_cast<char>((v >> 8) & 255));
    }

    static void appendUint32(std::string& out, unsigned int v) {
        appendUint16(out, static_cast<unsigned short>(v & 0xFFFFu));
        appendUint16(out, static_cast<unsigned short>((v >> 16) & 0xFFFFu));
    }

    Frame averageFrames(int n) {
        Frame first = this->cam_->grab();
        std::vector<int> acc(first.data().begin(), first.data().end());
        for (int i = 1; i < n; ++i) {
            Frame f = this->cam_->grab();
            if (f.width() != first.width() || f.height() != first.height()) {
                throw CameraError("Camera changed resolution during calibration");
            }
            for (std::size_t k = 0; k < acc.size(); ++k) {
                acc[k] += static_cast<int>(f.data()[k]);
            }
        }
        Frame avg(first.width(), first.height());
        for (std::size_t k = 0; k < acc.size(); ++k) {
            avg.data()[k] = static_cast<uint8_t>(acc[k] / n);
        }
        return avg;
    }

public:
    SeatMonitor(Camera* cam, BookingManager& mgr, double threshold = 18.0, int intervalMs = 500)
        : cam_(cam),
          mgr_(mgr),
          detector_(threshold),
          intervalMs_(intervalMs),
          running_(false),
          monitorLock_(),
          last_(),
          status_("starting") {}

    // Template constructor overload for compatibility with callers passing smart-pointer wrappers
    template <class CamPtr>
    SeatMonitor(CamPtr cam, BookingManager& mgr, double threshold = 18.0, int intervalMs = 500)
        : cam_(cam.release()),
          mgr_(mgr),
          detector_(threshold),
          intervalMs_(intervalMs),
          running_(false),
          monitorLock_(),
          last_(),
          status_("starting") {}

    ~SeatMonitor() {
        this->stop();
        delete this->cam_;
        this->cam_ = 0;
    }

    void calibrate() {
        ScopedLock guard(this->monitorLock_);
        this->detector_.calibrate(this->averageFrames(5));
        Frame f = this->cam_->grab();
        {
            ScopedLock mgrGuard(this->mgr_.lock());
            Detector* detPtr = &(this->detector_);
            detPtr->update(f, this->mgr_.seats());
        }
        this->last_ = f;
        this->status_ = "ok";
    }

    // Poll one frame from the camera and update seat occupancy through the Detector interface
    void step() {
        if (!this->running_) {
            return;
        }
        ScopedLock guard(this->monitorLock_);
        try {
            Frame f = this->cam_->grab();
            Detector* detPtr = &(this->detector_);
            if (!detPtr->calibrated()) {
                detPtr->calibrate(f);
            } else {
                ScopedLock mgrGuard(this->mgr_.lock());
                detPtr->update(f, this->mgr_.seats());
            }
            this->last_ = f;
            this->status_ = "ok";
        } catch (const std::exception& e) {
            this->status_ = e.what();
        }
    }

    void start() {
        this->running_ = true;
        try {
            this->calibrate();
        } catch (const LibraryError& e) {
            this->status_ = e.what();
        }
        this->step();
    }

    void stop() {
        this->running_ = false;
    }

    std::string status() {
        this->step();
        ScopedLock guard(this->monitorLock_);
        return this->status_;
    }

    std::string cameraKind() const {
        return this->cam_->kind();
    }

    // Generate 24-bit BMP snapshot with green/red seat bounding boxes
    std::string snapshotBmp() {
        this->step();
        Frame f;
        {
            ScopedLock guard(this->monitorLock_);
            f = this->last_;
        }
        if (f.empty()) {
            throw CameraError("No camera frame available yet");
        }
        const int W = f.width() / 2;
        const int H = f.height() / 2;
        std::vector<uint8_t> rgb(static_cast<std::size_t>(W) * static_cast<std::size_t>(H) * 3u);
        for (int y = 0; y < H; ++y) {
            for (int x = 0; x < W; ++x) {
                uint8_t v = f.at(x * 2, y * 2);
                std::size_t i = (static_cast<std::size_t>(y) * static_cast<std::size_t>(W) + static_cast<std::size_t>(x)) * 3u;
                rgb[i] = v;
                rgb[i + 1] = v;
                rgb[i + 2] = v;
            }
        }
        {
            ScopedLock mgrGuard(this->mgr_.lock());
            const Registry<Seat>& seats = this->mgr_.seats();
            for (std::size_t idx = 0; idx < seats.size(); ++idx) {
                const Seat* s = seats.at(idx);
                const Rect& r = s->roi();
                int x0 = r.x / 2;
                int y0 = r.y / 2;
                int x1 = (r.x + r.w) / 2;
                int y1 = (r.y + r.h) / 2;
                uint8_t blue = 60;
                uint8_t green = s->occupied() ? 60 : 210;
                uint8_t red = s->occupied() ? 230 : 60;
                for (int y = y0; y < y1; ++y) {
                    for (int x = x0; x < x1; ++x) {
                        bool border = (x < x0 + 2 || x >= x1 - 2 || y < y0 + 2 || y >= y1 - 2);
                        if (!border || x < 0 || y < 0 || x >= W || y >= H) {
                            continue;
                        }
                        std::size_t i = (static_cast<std::size_t>(y) * static_cast<std::size_t>(W) + static_cast<std::size_t>(x)) * 3u;
                        rgb[i] = blue;
                        rgb[i + 1] = green;
                        rgb[i + 2] = red;
                    }
                }
            }
        }
        const int rowBytes = W * 3;
        const int pad = (4 - (rowBytes % 4)) % 4;
        const unsigned int imgSize = static_cast<unsigned int>((rowBytes + pad) * H);
        std::string out;
        out += "BM";
        appendUint32(out, 54u + imgSize);
        appendUint32(out, 0u);
        appendUint32(out, 54u);
        appendUint32(out, 40u);
        appendUint32(out, static_cast<unsigned int>(W));
        appendUint32(out, static_cast<unsigned int>(H));
        appendUint16(out, 1u);
        appendUint16(out, 24u);
        appendUint32(out, 0u);
        appendUint32(out, imgSize);
        appendUint32(out, 2835u);
        appendUint32(out, 2835u);
        appendUint32(out, 0u);
        appendUint32(out, 0u);
        for (int y = H - 1; y >= 0; --y) {
            out.append(reinterpret_cast<const char*>(&rgb[static_cast<std::size_t>(y) * static_cast<std::size_t>(W) * 3u]),
                       static_cast<std::size_t>(rowBytes));
            out.append(static_cast<std::size_t>(pad), '\0');
        }
        return out;
    }
};

#endif
