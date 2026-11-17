
#ifndef ENTITIES_H
#define ENTITIES_H

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <new>
#include <sstream>
#include <string>
#include <vector>
#include "Exceptions.h"
#include "Frame.h"


inline std::string formatTime(int minutes) {
    std::ostringstream o;
    o << std::setfill('0') << std::setw(2) << (minutes / 60)
      << ':' << std::setw(2) << (minutes % 60);
    return o.str();
}


class TimeSlot {
public:
    int start;
    int end;

    TimeSlot(int s = 0, int e = 0) : start(s), end(e) {}
    TimeSlot(const TimeSlot& other) : start(other.start), end(other.end) {}

    TimeSlot& operator=(const TimeSlot& other) {
        if (this != &other) {
            this->start = other.start;
            this->end = other.end;
        }
        return *this;
    }

    ~TimeSlot() {}

    inline int length() const { return this->end - this->start; }

    inline bool overlaps(const TimeSlot& o) const {
        return this->start < o.end && o.start < this->end;
    }

    inline bool contains(int minute) const {
        return minute >= this->start && minute < this->end;
    }

    bool operator==(const TimeSlot& o) const {
        return this->start == o.start && this->end == o.end;
    }

    bool operator!=(const TimeSlot& o) const {
        return !(*this == o);
    }

    friend std::ostream& operator<<(std::ostream& os, const TimeSlot& s);
};

inline std::ostream& operator<<(std::ostream& os, const TimeSlot& s) {
    return os << formatTime(s.start) << '-' << formatTime(s.end);
}

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic ignored "-Wmismatched-new-delete"
#endif


class Booking {
private:
    int id_;
    int seatId_;
    std::string user_;
    TimeSlot slot_;
    long seq_;

public:
    // Static data members for tracking active heap allocations
    static int liveCount;
    static std::size_t liveBytes;

    static int getLiveCount() { return liveCount; }
    static std::size_t getLiveBytes() { return liveBytes; }

    // Overloaded new and delete operators for dynamic memory tracking
    static void* operator new(std::size_t size) {
        void* p = std::malloc(size);
        if (!p) {
            throw std::bad_alloc();
        }
        ++liveCount;
        liveBytes += size;
        return p;
    }

    static void operator delete(void* p, std::size_t size) {
        if (!p) {
            return;
        }
        std::free(p);
        --liveCount;
        liveBytes -= size;
    }

    // Default, parameterized, and copy constructors + destructor
    Booking() : id_(0), seatId_(0), user_(""), slot_(), seq_(0) {}

    Booking(int id, int seatId, const std::string& user, const TimeSlot& slot, long seq)
        : id_(id), seatId_(seatId), user_(user), slot_(slot), seq_(seq) {}

    Booking(const Booking& o)
        : id_(o.id_), seatId_(o.seatId_), user_(o.user_), slot_(o.slot_), seq_(o.seq_) {}

    Booking& operator=(const Booking& o) {
        if (this != &o) {
            this->id_ = o.id_;
            this->seatId_ = o.seatId_;
            this->user_ = o.user_;
            this->slot_ = o.slot_;
            this->seq_ = o.seq_;
        }
        return *this;
    }

    ~Booking() {}

    inline int id() const { return this->id_; }
    inline int seatId() const { return this->seatId_; }
    inline const std::string& user() const { return this->user_; }
    inline const TimeSlot& slot() const { return this->slot_; }
    inline long seq() const { return this->seq_; }

    bool operator==(const Booking& o) const {
        return this->id_ == o.id_ && this->seatId_ == o.seatId_ &&
               this->user_ == o.user_ && this->slot_ == o.slot_;
    }

    bool operator!=(const Booking& o) const {
        return !(*this == o);
    }

    // Friend stream operators for file serialization: id|seat|start|end|seq|user
    friend std::ostream& operator<<(std::ostream& os, const Booking& b);
    friend std::istream& operator>>(std::istream& is, Booking& b);
};

// Static member definitions
int Booking::liveCount = 0;
std::size_t Booking::liveBytes = 0;

inline std::ostream& operator<<(std::ostream& os, const Booking& b) {
    return os << b.id_ << '|' << b.seatId_ << '|' << b.slot_.start << '|'
              << b.slot_.end << '|' << b.seq_ << '|' << b.user_;
}

inline std::istream& operator>>(std::istream& is, Booking& b) {
    std::string line;
    if (!std::getline(is, line)) {
        return is;
    }
    std::vector<std::string> fields;
    std::size_t pos = 0;
    for (int i = 0; i < 5; ++i) {
        std::size_t bar = line.find('|', pos);
        if (bar == std::string::npos) {
            is.setstate(std::ios::failbit);
            return is;
        }
        fields.push_back(line.substr(pos, bar - pos));
        pos = bar + 1;
    }
    fields.push_back(line.substr(pos));
    try {
        int parsedId = std::stoi(fields[0]);
        int parsedSeat = std::stoi(fields[1]);
        int parsedStart = std::stoi(fields[2]);
        int parsedEnd = std::stoi(fields[3]);
        long parsedSeq = std::stol(fields[4]);
        b = Booking(parsedId, parsedSeat, fields[5], TimeSlot(parsedStart, parsedEnd), parsedSeq);
    } catch (const std::exception&) {
        is.setstate(std::ios::failbit);
    }
    return is;
}

class Entity {
protected:
    int id_;
    std::string name_;
    static int totalEntities_;

public:
    Entity(int id = 0, const std::string& name = "") : id_(id), name_(name) {
        ++totalEntities_;
    }

    Entity(const Entity& other) : id_(other.id_), name_(other.name_) {
        ++totalEntities_;
    }

    Entity& operator=(const Entity& other) {
        if (this != &other) {
            this->id_ = other.id_;
            this->name_ = other.name_;
        }
        return *this;
    }

    virtual ~Entity() {
        --totalEntities_;
    }

    inline int id() const { return this->id_; }
    inline const std::string& name() const { return this->name_; }
    static int getTotalEntities() { return totalEntities_; }

    bool operator==(const Entity& other) const {
        return this->id_ == other.id_;
    }

    virtual std::string describe() const {
        return this->name_;
    }
};

int Entity::totalEntities_ = 0;

// Camera-detectable aspect of a seat
class Detectable : virtual public Entity {
protected:
    Rect roi_;
    bool occupied_;
    double score_;

public:
    Detectable(const Rect& r = Rect()) : Entity(), roi_(r), occupied_(false), score_(0.0) {}
    Detectable(const Detectable& other)
        : Entity(other), roi_(other.roi_), occupied_(other.occupied_), score_(other.score_) {}
    virtual ~Detectable() {}

    inline const Rect& roi() const { return this->roi_; }
    inline bool occupied() const { return this->occupied_; }
    inline double score() const { return this->score_; }

    void setDetection(bool occ, double score) {
        this->occupied_ = occ;
        this->score_ = score;
    }
};

// Bookable aspect of a seat (abstract class with pure virtual maxMinutes())
class Bookable : virtual public Entity {
protected:
    std::vector<TimeSlot> reserved_;

public:
    Bookable() : Entity(), reserved_() {}
    Bookable(const Bookable& other) : Entity(other), reserved_(other.reserved_) {}
    virtual ~Bookable() {}

    // Pure virtual function: each seat type defines its own maximum booking duration
    virtual int maxMinutes() const = 0;

    // Function overloading for checking seat availability
    bool isFree(const TimeSlot& s) const {
        std::vector<TimeSlot>::const_iterator it;
        for (it = this->reserved_.begin(); it != this->reserved_.end(); ++it) {
            if (it->overlaps(s)) {
                return false;
            }
        }
        return true;
    }

    bool isFree(int start, int end) const {
        return this->isFree(TimeSlot(start, end));
    }

    bool isFreeAt(int minute) const {
        std::vector<TimeSlot>::const_iterator it;
        for (it = this->reserved_.begin(); it != this->reserved_.end(); ++it) {
            if (it->contains(minute)) {
                return false;
            }
        }
        return true;
    }

    void reserve(const TimeSlot& s) {
        if (!this->isFree(s)) {
            throw SlotConflictError(this->name_ + " is already reserved for that time");
        }
        this->reserved_.push_back(s);
    }

    void release(const TimeSlot& s) {
        std::vector<TimeSlot>::iterator it = std::find(this->reserved_.begin(), this->reserved_.end(), s);
        if (it != this->reserved_.end()) {
            this->reserved_.erase(it);
        }
    }

    const std::vector<TimeSlot>& reservations() const {
        return this->reserved_;
    }
};

// Abstract Seat class combining Detectable and Bookable via multiple inheritance
class Seat : public Detectable, public Bookable {
public:
    Seat(int id = 0, const Rect& roi = Rect())
        : Entity(id, "Seat " + std::to_string(id)), Detectable(roi), Bookable() {}

    Seat(const Seat& other)
        : Entity(other), Detectable(other), Bookable(other) {}

    virtual ~Seat() {}

    virtual std::string type() const = 0;

    virtual std::string describe() const {
        return this->name_ + " [" + this->type() + "]";
    }
};

class StudySeat : public Seat {
public:
    StudySeat(int id = 0, const Rect& roi = Rect())
        : Entity(id, "Seat " + std::to_string(id)), Seat(id, roi) {}

    StudySeat(const StudySeat& other)
        : Entity(other), Seat(other) {}

    virtual ~StudySeat() {}

    virtual int maxMinutes() const {
        return 240;
    }

    virtual std::string type() const {
        return "Study desk";
    }
};

class ComputerSeat : public Seat {
public:
    ComputerSeat(int id = 0, const Rect& roi = Rect())
        : Entity(id, "Seat " + std::to_string(id)), Seat(id, roi) {}

    ComputerSeat(const ComputerSeat& other)
        : Entity(other), Seat(other) {}

    virtual ~ComputerSeat() {}

    virtual int maxMinutes() const {
        return 120;
    }

    virtual std::string type() const {
        return "Computer desk";
    }
};

#endif
