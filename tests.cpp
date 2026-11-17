
#include <cstdio>
#include <iostream>
#include <memory>
#include <sstream>
#include <thread>
#include "BookingManager.h"
#include "Camera.h"
#include "SeatDetector.h"

static int total = 0, failures = 0;
#define CHECK(cond)                                                                   \
    do {                                                                              \
        ++total;                                                                      \
        if (!(cond)) {                                                                \
            ++failures;                                                               \
            std::cerr << "  FAIL line " << __LINE__ << ": " #cond "\n";               \
        }                                                                             \
    } while (0)
#define CHECK_THROWS(expr, ExType)                                                    \
    do {                                                                              \
        ++total;                                                                      \
        try {                                                                         \
            expr;                                                                     \
            ++failures;                                                               \
            std::cerr << "  FAIL (no exception) line " << __LINE__ << "\n";           \
        } catch (const ExType&) {                                                     \
        } catch (...) {                                                               \
            ++failures;                                                               \
            std::cerr << "  FAIL (wrong exception type) line " << __LINE__ << "\n";   \
        }                                                                             \
    } while (0)

static const int NOW = 10 * 60;  // pretend it is 10:00

static std::unique_ptr<BookingManager> makeManager(const std::string& path = "") {
    auto m = std::make_unique<BookingManager>(8, 22, path);
    for (int i = 1; i <= 4; ++i) m->addSeat(std::make_unique<StudySeat>(i, Rect(i * 20, 0, 10, 10)));
    m->addSeat(std::make_unique<ComputerSeat>(5, Rect(120, 0, 10, 10)));
    return m;
}
static TimeSlot slot(int h1, int m1, int h2, int m2) { return TimeSlot(h1 * 60 + m1, h2 * 60 + m2); }

static void testBookingBasics() {
    std::cout << "booking basics\n";
    auto m = makeManager();
    Booking b = m->book("Asha", 1, slot(10, 0, 12, 0), NOW);
    CHECK(b.seatId() == 1 && b.user() == "Asha" && b.slot() == slot(10, 0, 12, 0));
    CHECK(m->bookingCount() == 1);
}

static void testFirstComeFirstServed() {
    std::cout << "first come first served\n";
    auto m = makeManager();
    m->book("Asha", 1, slot(10, 0, 12, 0), NOW);
    CHECK_THROWS(m->book("Ben", 1, slot(11, 0, 13, 0), NOW), SlotConflictError);  // overlaps the end
    CHECK_THROWS(m->book("Ben", 1, slot(9, 0, 10, 30), NOW), SlotConflictError);  // overlaps the start
    CHECK_THROWS(m->book("Ben", 1, slot(10, 30, 11, 0), NOW), SlotConflictError); // inside
    m->book("Ben", 1, slot(12, 0, 13, 0), NOW);  // back-to-back is allowed
    m->book("Ben", 2, slot(10, 0, 11, 0), NOW);  // other seat is free
    CHECK(m->bookingCount() == 3);
}

static void testUserCannotDoubleBook() {
    std::cout << "one person, one seat at a time\n";
    auto m = makeManager();
    m->book("Asha", 1, slot(10, 0, 11, 0), NOW);
    CHECK_THROWS(m->book("asha", 2, slot(10, 30, 11, 30), NOW), SlotConflictError);  // case-insensitive
    m->book("Asha", 2, slot(11, 0, 12, 0), NOW);
}

static void testValidation() {
    std::cout << "input validation\n";
    auto m = makeManager();
    CHECK_THROWS(m->book("", 1, slot(10, 0, 11, 0), NOW), ValidationError);
    CHECK_THROWS(m->book("<script>", 1, slot(10, 0, 11, 0), NOW), ValidationError);
    CHECK_THROWS(m->book("Asha", 1, slot(10, 15, 11, 0), NOW), ValidationError);   // not 30-min step
    CHECK_THROWS(m->book("Asha", 1, slot(11, 0, 10, 0), NOW), ValidationError);    // end before start
    CHECK_THROWS(m->book("Asha", 1, slot(7, 0, 9, 0), NOW), ValidationError);      // before opening
    CHECK_THROWS(m->book("Asha", 1, slot(21, 0, 23, 0), NOW), ValidationError);    // after closing
    CHECK_THROWS(m->book("Asha", 1, slot(8, 0, 9, 30), NOW), ValidationError);     // already over
    CHECK_THROWS(m->book("Asha", 1, slot(10, 0, 15, 0), NOW), ValidationError);    // > 4h on study desk
    CHECK_THROWS(m->book("Asha", 5, slot(10, 0, 12, 30), NOW), ValidationError);   // > 2h on computer desk
    CHECK_THROWS(m->book("Asha", 99, slot(10, 0, 11, 0), NOW), NotFoundError);
    CHECK(m->bookingCount() == 0);
}

static void testAutoAssign() {
    std::cout << "auto-assign\n";
    auto m = makeManager();
    CHECK(m->book("Asha", slot(10, 0, 11, 0), NOW).seatId() == 1);
    CHECK(m->book("Ben", slot(10, 0, 11, 0), NOW).seatId() == 2);
    CHECK(m->book("Chen", slot(10, 0, 11, 0), NOW).seatId() == 3);
    CHECK(m->book("Dev", slot(10, 0, 11, 0), NOW).seatId() == 4);
    CHECK(m->book("Esha", slot(10, 0, 11, 0), NOW).seatId() == 5);
    CHECK_THROWS(m->book("Farah", slot(10, 0, 11, 0), NOW), SlotConflictError);  // library full
    CHECK(m->book("Farah", slot(11, 0, 12, 0), NOW).seatId() == 1);              // free again later
}

static void testCancel() {
    std::cout << "cancel\n";
    auto m = makeManager();
    Booking b = m->book("Asha", 1, slot(10, 0, 11, 0), NOW);
    CHECK_THROWS(m->cancel(b.id(), "Ben"), ValidationError);  // not yours
    CHECK_THROWS(m->cancel(9999, "Asha"), NotFoundError);
    m->cancel(b.id(), "Asha");
    CHECK(m->bookingCount() == 0);
    m->book("Ben", 1, slot(10, 0, 11, 0), NOW);  // seat is free again
}

static void testWalkInOccupied() {
    std::cout << "camera: person already sitting there\n";
    auto m = makeManager();
    m->withSeats([](Registry<Seat>& seats) { seats.at(0)->setDetection(true, 50.0); });  // seat 1 occupied
    CHECK_THROWS(m->book("Asha", 1, slot(10, 0, 11, 0), NOW), SlotConflictError);  // covers "now"
    m->book("Asha", 1, slot(11, 0, 12, 0), NOW);  // later is fine
    CHECK(m->book("Ben", slot(10, 0, 11, 0), NOW).seatId() == 2);  // auto-assign skips seat 1
}

static void testPersistence() {
    std::cout << "file persistence\n";
    const std::string path = "test_bookings.txt";
    std::remove(path.c_str());
    {
        auto m = makeManager(path);
        m->book("Asha Singh", 1, slot(10, 0, 11, 0), NOW);
        m->book("Ben", 2, slot(12, 0, 13, 0), NOW);
    }
    auto m2 = makeManager(path);
    CHECK(m2->load() == 2);
    CHECK(m2->bookingCount() == 2);
    CHECK_THROWS(m2->book("Chen", 1, slot(10, 0, 11, 0), NOW), SlotConflictError);  // still reserved
    Booking b3 = m2->book("Chen", 3, slot(10, 0, 11, 0), NOW);
    CHECK(b3.id() == 3);  // ids continue after the restored ones
    std::remove(path.c_str());
}

static void testStreamOperators() {
    std::cout << "operator << and >>\n";
    Booking a(7, 3, "Asha Singh", slot(10, 0, 11, 30), 4);
    std::ostringstream out;
    out << a;
    CHECK(out.str() == "7|3|600|690|4|Asha Singh");
    std::istringstream in(out.str());
    Booking b;
    in >> b;
    CHECK(in && a == b);
    std::istringstream bad("not|a|booking");
    Booking c;
    CHECK(!(bad >> c));
    std::ostringstream t;
    t << slot(9, 5, 10, 30);
    CHECK(t.str() == "09:05-10:30");
}

static void testNewDeleteOverload() {
    std::cout << "overloaded new/delete\n";
    int before = Booking::liveCount;
    {
        auto m = makeManager();
        m->book("Asha", 1, slot(10, 0, 11, 0), NOW);
        m->book("Ben", 2, slot(10, 0, 11, 0), NOW);
        CHECK(Booking::liveCount == before + 2);
        m->cancel(1, "Asha");
        CHECK(Booking::liveCount == before + 1);
    }
    CHECK(Booking::liveCount == before);  // destructor freed the rest
}

static void testFrameOps() {
    std::cout << "Frame operators\n";
    Frame a(4, 4, 100), b(4, 4, 90);
    Frame d = a - b;
    CHECK(d.at(0, 0) == 10 && d.meanIn(0, 0, 4, 4) == 10.0);
    Frame copy(a);
    copy.at(0, 0) = 1;
    CHECK(a.at(0, 0) == 100);  // deep copy
    Frame wrong(5, 5, 0);
    CHECK_THROWS(a - wrong, CameraError);
}

static void testDetector() {
    std::cout << "seat detector with simulated camera\n";
    std::vector<Rect> rois = {Rect(30, 30, 120, 100), Rect(180, 30, 120, 100), Rect(330, 30, 120, 100)};
    SimulatedCamera cam(640, 480, rois);
    Registry<Seat> seats;
    for (int i = 0; i < 3; ++i) seats.add(std::make_unique<StudySeat>(i + 1, rois[i]));
    SeatDetector det(18.0, 2);
    CHECK_THROWS(det.update(cam.grab(), seats), CameraError);  // not calibrated
    det.calibrate(cam.grab());
    for (int i = 0; i < 3; ++i) det.update(cam.grab(), seats);
    CHECK(!seats.at(0)->occupied() && !seats.at(1)->occupied() && !seats.at(2)->occupied());

    cam.setPresent(1, true);
    det.update(cam.grab(), seats);
    CHECK(!seats.at(1)->occupied());  // one frame is not enough (hysteresis)
    det.update(cam.grab(), seats);
    CHECK(seats.at(1)->occupied() && !seats.at(0)->occupied() && !seats.at(2)->occupied());

    cam.setPresent(1, false);
    det.update(cam.grab(), seats);
    det.update(cam.grab(), seats);
    CHECK(!seats.at(1)->occupied());
}

static void testBrowserCamera() {
    std::cout << "browser camera (pictures pushed over HTTP)\n";
    BrowserCamera cam(8, 4);
    CHECK_THROWS(cam.grab(), CameraError);                   // nothing received yet
    CHECK_THROWS(cam.push(std::string(10, 'x')), ValidationError);  // wrong size rejected
    cam.push(std::string(32, static_cast<char>(100)));
    Frame f = cam.grab();
    CHECK(f.width() == 8 && f.height() == 4 && f.at(3, 2) == 100);
    std::unique_ptr<Camera> base = std::make_unique<BrowserCamera>(2, 2);  // used through base pointer
    CHECK(base->kind() == "browser camera");
}

static void testBrightnessShift() {
    std::cout << "global brightness change must not look like people\n";
    std::vector<Rect> rois = {Rect(30, 30, 120, 100), Rect(180, 30, 120, 100)};
    SimulatedCamera cam(640, 480, rois);
    Registry<Seat> seats;
    for (int i = 0; i < 2; ++i) seats.add(std::make_unique<StudySeat>(i + 1, rois[i]));
    SeatDetector det(18.0, 2);
    Frame empty = cam.grab();
    det.calibrate(empty);
    CHECK(empty.brightened(20).medianShiftFrom(empty) == 20);
    for (int i = 0; i < 4; ++i) det.update(cam.grab().brightened(25), seats);  // lights got brighter
    CHECK(!seats.at(0)->occupied() && !seats.at(1)->occupied());
    for (int i = 0; i < 4; ++i) det.update(cam.grab().brightened(-25), seats);  // and darker
    CHECK(!seats.at(0)->occupied() && !seats.at(1)->occupied());
    cam.setPresent(0, true);  // a real person is still detected under the brightness change
    for (int i = 0; i < 4; ++i) det.update(cam.grab().brightened(25), seats);
    CHECK(seats.at(0)->occupied() && !seats.at(1)->occupied());
}

static void testFirstComeFirstServedThreads() {
    std::cout << "concurrency: 30 people race for one seat\n";
    auto m = makeManager();
    std::atomic<int> wins{0}, losses{0};
    std::vector<std::thread> ts;
    for (int i = 0; i < 30; ++i)
        ts.emplace_back([&, i] {
            try {
                m->book("User" + std::to_string(i), 1, slot(10, 0, 11, 0), NOW);
                ++wins;
            } catch (const SlotConflictError&) {
                ++losses;
            }
        });
    for (auto& t : ts) t.join();
    CHECK(wins == 1 && losses == 29);
}

static void testPolymorphism() {
    std::cout << "inheritance & virtual functions\n";
    std::unique_ptr<Seat> a = std::make_unique<StudySeat>(1, Rect());
    std::unique_ptr<Seat> b = std::make_unique<ComputerSeat>(2, Rect());
    CHECK(a->maxMinutes() == 240 && b->maxMinutes() == 120);
    CHECK(a->describe() == "Seat 1 [Study desk]" && b->describe() == "Seat 2 [Computer desk]");
    std::unique_ptr<LibraryError> e = std::make_unique<SlotConflictError>("x");
    CHECK(std::string(e->code()) == "SLOT_CONFLICT" && e->httpStatus() == 409);
}

int main() {
    testBookingBasics();
    testFirstComeFirstServed();
    testUserCannotDoubleBook();
    testValidation();
    testAutoAssign();
    testCancel();
    testWalkInOccupied();
    testPersistence();
    testStreamOperators();
    testNewDeleteOverload();
    testFrameOps();
    testDetector();
    testBrowserCamera();
    testBrightnessShift();
    testFirstComeFirstServedThreads();
    testPolymorphism();
    std::cout << "\n" << (total - failures) << "/" << total << " checks passed\n";
    return failures == 0 ? 0 : 1;
}
