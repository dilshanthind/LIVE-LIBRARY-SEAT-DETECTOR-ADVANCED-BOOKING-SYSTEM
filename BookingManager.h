
#ifndef BOOKING_MANAGER_H
#define BOOKING_MANAGER_H

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include "Entities.h"
#include "Registry.h"


class SpinLock {
private:
    volatile int locked_;

public:
    SpinLock() : locked_(0) {}
    ~SpinLock() {}

    void acquire() {
        while (__sync_lock_test_and_set(&this->locked_, 1)) {
            // spin until lock is released
        }
    }

    void release() {
        __sync_lock_release(&this->locked_);
    }
};

class ScopedLock {
private:
    SpinLock& lock_;

public:
    ScopedLock(SpinLock& l) : lock_(l) {
        this->lock_.acquire();
    }
    ~ScopedLock() {
        this->lock_.release();
    }
};

class BookingManager {
private:
    mutable SpinLock lock_;
    Registry<Seat> seats_;
    std::vector<Booking*> bookings_;
    int open_;
    int close_;
    int nextId_;
    long seq_;
    int cols_;
    bool simulated_;
    std::string cameraMode_;
    std::string savePath_;

    // Prevent copying of BookingManager (owns heap-allocated Booking* objects)
    BookingManager(const BookingManager& other);
    BookingManager& operator=(const BookingManager& other);

    static std::string lower(const std::string& s) {
        std::string result = s;
        for (std::size_t i = 0; i < result.size(); ++i) {
            unsigned char c = static_cast<unsigned char>(result[i]);
            result[i] = static_cast<char>(std::tolower(c));
        }
        return result;
    }

    static bool sameUser(const std::string& a, const std::string& b) {
        return lower(a) == lower(b);
    }

    static bool compareBookings(const Booking* a, const Booking* b) {
        if (a->slot().start != b->slot().start) {
            return a->slot().start < b->slot().start;
        }
        return a->seatId() < b->seatId();
    }

    static std::string today() {
        std::time_t t = std::time(0);
        std::tm lt;
        localtime_r(&t, &lt);
        char buf[16];
        std::strftime(buf, sizeof(buf), "%Y-%m-%d", &lt);
        return std::string(buf);
    }

    static std::string cleanUser(const std::string& raw) {
        std::size_t a = raw.find_first_not_of(" \t");
        if (a == std::string::npos) {
            throw ValidationError("Please enter your name");
        }
        std::size_t b = raw.find_last_not_of(" \t");
        std::string u = raw.substr(a, b - a + 1);
        if (u.size() > 30) {
            throw ValidationError("Name must be 30 characters or fewer");
        }
        for (std::size_t i = 0; i < u.size(); ++i) {
            unsigned char c = static_cast<unsigned char>(u[i]);
            if (!(std::isalnum(c) || c == ' ' || c == '.' || c == '_' || c == '-')) {
                throw ValidationError("Name may only contain letters, digits, space, . _ -");
            }
        }
        return u;
    }

    void validateSlot(const TimeSlot& s, int now) const {
        if (s.start % 30 != 0 || s.end % 30 != 0) {
            throw ValidationError("Times must be on 30-minute steps");
        }
        if (s.start >= s.end) {
            throw ValidationError("End time must be after start time");
        }
        if (s.start < this->open_ || s.end > this->close_) {
            throw ValidationError("Library is open " + formatTime(this->open_) + " to " + formatTime(this->close_));
        }
        if (s.end <= now) {
            throw ValidationError("That time interval has already ended");
        }
    }

    std::string seatProblem(const Seat& s, const TimeSlot& slot, int now) const {
        if (!s.isFree(slot)) {
            return s.name() + " is already booked during part of that interval";
        }
        if (slot.contains(now) && s.occupied() && s.isFreeAt(now)) {
            return s.name() + " is currently occupied (detected by camera)";
        }
        return "";
    }

    std::string userProblem(const std::string& user, const TimeSlot& slot) const {
        std::vector<Booking*>::const_iterator it;
        for (it = this->bookings_.begin(); it != this->bookings_.end(); ++it) {
            const Booking* b = *it;
            if (sameUser(b->user(), user) && b->slot().overlaps(slot)) {
                std::ostringstream o;
                o << "You already have a booking (Seat " << b->seatId() << ", " << b->slot()
                  << ") that overlaps this time";
                return o.str();
            }
        }
        return "";
    }

    Seat* findSeat(int id) const {
        return this->seats_.find(id);
    }

    Booking commit(const std::string& user, Seat& seat, const TimeSlot& slot) {
        Booking* b = new Booking(this->nextId_++, seat.id(), user, slot, this->seq_++);
        seat.reserve(slot);
        this->bookings_.push_back(b);
        this->saveUnlocked();
        return *b;
    }

    void saveUnlocked() const {
        if (this->savePath_.empty()) {
            return;
        }
        try {
            std::string tmp = this->savePath_ + ".tmp";
            {
                std::ofstream out(tmp.c_str(), std::ios::out | std::ios::trunc);
                if (!out) {
                    throw LibraryError("cannot open " + tmp);
                }
                out << "# date " << today() << '\n';
                std::vector<Booking*>::const_iterator it;
                for (it = this->bookings_.begin(); it != this->bookings_.end(); ++it) {
                    out << **it << '\n';
                }
                out.close();
            }
            if (std::rename(tmp.c_str(), this->savePath_.c_str()) != 0) {
                throw LibraryError("rename failed");
            }
        } catch (const std::exception& e) {
            std::cerr << "[warn] could not save bookings: " << e.what() << '\n';
        }
    }

public:
    static std::string jsonEscape(const std::string& s) {
        std::string o;
        for (std::size_t i = 0; i < s.size(); ++i) {
            unsigned char c = static_cast<unsigned char>(s[i]);
            if (c == '"') {
                o += "\\\"";
            } else if (c == '\\') {
                o += "\\\\";
            } else if (c < 0x20) {
                o += ' ';
            } else {
                o += static_cast<char>(c);
            }
        }
        return o;
    }

    static std::string bookingJson(const Booking& b) {
        std::ostringstream o;
        o << "{\"id\":" << b.id() << ",\"seat\":" << b.seatId()
          << ",\"start\":" << b.slot().start << ",\"end\":" << b.slot().end
          << ",\"user\":\"" << jsonEscape(b.user()) << "\"}";
        return o.str();
    }

    BookingManager(int openHour = 8, int closeHour = 22, const std::string& savePath = "")
        : lock_(),
          seats_(),
          bookings_(),
          open_(openHour * 60),
          close_(closeHour * 60),
          nextId_(1),
          seq_(0),
          cols_(4),
          simulated_(false),
          cameraMode_("simulated"),
          savePath_(savePath) {
        if (openHour < 0 || closeHour > 24 || openHour >= closeHour) {
            throw ValidationError("Invalid opening hours");
        }
    }

    ~BookingManager() {
        std::vector<Booking*>::iterator it;
        for (it = this->bookings_.begin(); it != this->bookings_.end(); ++it) {
            delete *it;
        }
        this->bookings_.clear();
    }

    // Add a dynamically allocated Seat pointer (takes ownership)
    void addSeat(Seat* s) {
        ScopedLock guard(this->lock_);
        this->seats_.add(s);
    }

    // Overload for derived Seat pointers (e.g. StudySeat*, ComputerSeat*)
    template <class U>
    void addSeat(U* s) {
        Seat* basePtr = s;
        this->addSeat(basePtr);
    }

    // Template overload for compatibility with tests passing smart-pointer wrappers
    template <class Ptr>
    void addSeat(Ptr s) {
        Seat* rawPtr = s.release();
        this->addSeat(rawPtr);
    }

    void setColumns(int c) { this->cols_ = c; }

    void setCameraMode(const std::string& mode) {
        this->cameraMode_ = mode;
        this->simulated_ = (mode == "simulated");
    }

    Registry<Seat>& seats() { return this->seats_; }
    const Registry<Seat>& seats() const { return this->seats_; }
    SpinLock& lock() const { return this->lock_; }

    template <typename Fn>
    void withSeats(Fn fn) {
        ScopedLock guard(this->lock_);
        fn(this->seats_);
    }

    std::size_t bookingCount() const {
        ScopedLock guard(this->lock_);
        return this->bookings_.size();
    }

    // Overload 1: book a specific seat by ID
    Booking book(const std::string& rawUser, int seatId, const TimeSlot& slot, int now) {
        ScopedLock guard(this->lock_);
        std::string user = cleanUser(rawUser);
        this->validateSlot(slot, now);
        Seat* seat = this->findSeat(seatId);
        if (!seat) {
            throw NotFoundError("Seat " + std::to_string(seatId) + " does not exist");
        }
        if (slot.length() > seat->maxMinutes()) {
            throw ValidationError(seat->name() + " allows at most " +
                                  std::to_string(seat->maxMinutes()) + " minutes per booking");
        }
        std::string problem = this->seatProblem(*seat, slot, now);
        if (!problem.empty()) {
            throw SlotConflictError(problem);
        }
        problem = this->userProblem(user, slot);
        if (!problem.empty()) {
            throw SlotConflictError(problem);
        }
        return this->commit(user, *seat, slot);
    }

    // Overload 2: auto-assign the lowest-numbered free seat
    Booking book(const std::string& rawUser, const TimeSlot& slot, int now) {
        ScopedLock guard(this->lock_);
        std::string user = cleanUser(rawUser);
        this->validateSlot(slot, now);
        int longest = 0;
        for (std::size_t i = 0; i < this->seats_.size(); ++i) {
            const Seat* s = this->seats_.at(i);
            longest = std::max(longest, s->maxMinutes());
        }
        if (slot.length() > longest) {
            throw ValidationError("Bookings can be at most " + std::to_string(longest) + " minutes");
        }
        std::string problem = this->userProblem(user, slot);
        if (!problem.empty()) {
            throw SlotConflictError(problem);
        }
        Seat* chosen = 0;
        for (std::size_t i = 0; i < this->seats_.size(); ++i) {
            Seat* s = this->seats_.at(i);
            if (!chosen && slot.length() <= s->maxMinutes() && this->seatProblem(*s, slot, now).empty()) {
                chosen = s;
            }
        }
        if (!chosen) {
            throw SlotConflictError("No seat is free for that time interval");
        }
        return this->commit(user, *chosen, slot);
    }

    void cancel(int bookingId, const std::string& rawUser) {
        ScopedLock guard(this->lock_);
        std::string user = cleanUser(rawUser);
        std::vector<Booking*>::iterator it = this->bookings_.end();
        for (std::vector<Booking*>::iterator cur = this->bookings_.begin(); cur != this->bookings_.end(); ++cur) {
            if ((*cur)->id() == bookingId) {
                it = cur;
                break;
            }
        }
        if (it == this->bookings_.end()) {
            throw NotFoundError("Booking not found");
        }
        Booking* b = *it;
        if (!sameUser(b->user(), user)) {
            throw ValidationError("You can only cancel your own bookings");
        }
        Seat* s = this->findSeat(b->seatId());
        if (s) {
            s->release(b->slot());
        }
        this->bookings_.erase(it);
        delete b;
        this->saveUnlocked();
    }

    // Overloaded seatsJson: with and without an explicit TimeSlot interval
    std::string seatsJson(int now, const TimeSlot& interval) const {
        ScopedLock guard(this->lock_);
        std::string slotError;
        try {
            this->validateSlot(interval, now);
        } catch (const LibraryError& e) {
            slotError = e.what();
        }
        std::ostringstream o;
        o << std::fixed << std::setprecision(1);
        o << "{\"ok\":true,\"now\":" << now << ",\"open\":" << this->open_ << ",\"close\":" << this->close_
          << ",\"cols\":" << this->cols_ << ",\"simulated\":" << (this->simulated_ ? "true" : "false")
          << ",\"camera\":\"" << this->cameraMode_ << "\""
          << ",\"slotError\":\"" << jsonEscape(slotError) << "\",\"seats\":[";
        for (std::size_t i = 0; i < this->seats_.size(); ++i) {
            const Seat* s = this->seats_.at(i);
            std::string reason;
            if (!slotError.empty()) {
                reason = "Choose a valid time interval";
            } else if (interval.length() > s->maxMinutes()) {
                reason = "Max " + std::to_string(s->maxMinutes() / 60) + "h here";
            } else {
                reason = this->seatProblem(*s, interval, now);
            }
            if (!reason.empty() && reason.find(" is ") != std::string::npos) {
                reason = (reason.find("occupied") != std::string::npos) ? "Occupied now" : "Taken in slot";
            }
            if (i > 0) {
                o << ',';
            }
            o << "{\"id\":" << s->id() << ",\"name\":\"" << jsonEscape(s->name()) << "\",\"type\":\""
              << jsonEscape(s->type()) << "\",\"maxMinutes\":" << s->maxMinutes()
              << ",\"inUse\":" << (s->occupied() ? "true" : "false") << ",\"score\":" << s->score()
              << ",\"bookedNow\":" << (s->isFreeAt(now) ? "false" : "true")
              << ",\"available\":" << (reason.empty() ? "true" : "false") << ",\"reason\":\""
              << jsonEscape(reason) << "\"}";
        }
        o << "],\"bookings\":[";
        std::vector<const Booking*> sorted(this->bookings_.begin(), this->bookings_.end());
        std::sort(sorted.begin(), sorted.end(), BookingManager::compareBookings);
        for (std::size_t i = 0; i < sorted.size(); ++i) {
            if (i > 0) {
                o << ',';
            }
            o << bookingJson(*(sorted[i]));
        }
        o << "]}";
        return o.str();
    }

    std::string seatsJson(int now) const {
        return this->seatsJson(now, TimeSlot(now - (now % 30), now - (now % 30) + 60));
    }

    void save() const {
        ScopedLock guard(this->lock_);
        this->saveUnlocked();
    }

    int load() {
        ScopedLock guard(this->lock_);
        if (this->savePath_.empty()) {
            return 0;
        }
        std::ifstream in(this->savePath_.c_str(), std::ios::in);
        if (!in) {
            return 0;
        }
        std::string line;
        if (!std::getline(in, line) || line != "# date " + today()) {
            in.close();
            return 0;
        }
        int loaded = 0;
        while (std::getline(in, line)) {
            if (line.empty()) {
                continue;
            }
            std::istringstream ls(line);
            Booking b;
            if (!(ls >> b)) {
                std::cerr << "[warn] skipping malformed line in " << this->savePath_ << '\n';
                continue;
            }
            Seat* seat = this->findSeat(b.seatId());
            if (!seat || !seat->isFree(b.slot())) {
                continue;
            }
            seat->reserve(b.slot());
            this->bookings_.push_back(new Booking(b));
            this->nextId_ = std::max(this->nextId_, b.id() + 1);
            this->seq_ = std::max(this->seq_, b.seq() + 1);
            ++loaded;
        }
        in.close();
        return loaded;
    }
};

#endif
