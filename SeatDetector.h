
#ifndef SEAT_DETECTOR_H
#define SEAT_DETECTOR_H

#include <cstddef>
#include <map>
#include "Entities.h"
#include "Frame.h"
#include "Registry.h"

// Abstract base class for occupancy detectors
class Detector {
protected:
    double threshold_;

public:
    Detector(double threshold = 18.0) : threshold_(threshold) {}
    Detector(const Detector& other) : threshold_(other.threshold_) {}
    virtual ~Detector() {}

    inline double threshold() const { return this->threshold_; }

    virtual void calibrate(const Frame& emptyRoom) = 0;
    virtual bool calibrated() const = 0;
    virtual void update(const Frame& live, const Registry<Seat>& seats) = 0;
};

// Concrete seat occupancy detector using reference-frame subtraction and hysteresis
class SeatDetector : public Detector {
private:
    Frame reference_;
    int confirmFrames_;
    std::map<int, int> streak_;

public:
    SeatDetector(double threshold = 18.0, int confirmFrames = 2)
        : Detector(threshold), reference_(), confirmFrames_(confirmFrames), streak_() {}

    SeatDetector(const SeatDetector& other)
        : Detector(other),
          reference_(other.reference_),
          confirmFrames_(other.confirmFrames_),
          streak_(other.streak_) {}

    virtual ~SeatDetector() {}

    virtual void calibrate(const Frame& emptyRoom) {
        this->reference_ = emptyRoom;
        this->streak_.clear();
    }

    virtual bool calibrated() const {
        return !this->reference_.empty();
    }

    virtual void update(const Frame& live, const Registry<Seat>& seats);
};

inline void SeatDetector::update(const Frame& live, const Registry<Seat>& seats) {
    if (!this->calibrated()) {
        throw CameraError("Detector has not been calibrated yet");
    }
    int shift = live.medianShiftFrom(this->reference_);
    Frame aligned = live.brightened(-shift);
    Frame diff = aligned - this->reference_;

    for (std::size_t i = 0; i < seats.size(); ++i) {
        Seat* s = seats.at(i);
        if (!s) {
            continue;
        }
        double score = diff.meanIn(s->roi());
        bool seen = score > this->threshold_;
        int seatId = s->id();
        if (seen == s->occupied()) {
            this->streak_[seatId] = 0;
            s->setDetection(s->occupied(), score);
        } else {
            int currentStreak = ++(this->streak_[seatId]);
            if (currentStreak >= this->confirmFrames_) {
                this->streak_[seatId] = 0;
                s->setDetection(seen, score);
            } else {
                s->setDetection(s->occupied(), score);
            }
        }
    }
}

#endif
