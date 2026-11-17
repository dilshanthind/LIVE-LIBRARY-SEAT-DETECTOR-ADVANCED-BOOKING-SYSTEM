
#include <csignal>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include "BookingManager.h"
#include "Camera.h"
#include "HttpServer.h"
#include "SeatDetector.h"
#include "SeatMonitor.h"

static HttpServer* g_server = 0;

static void onSignal(int) {
    if (g_server) {
        g_server->stop();
    }
}

class Options {
public:
    int port;
    int openHour;
    int closeHour;
    int cameraIndex;
    bool browserCamera;
    std::string video;
    std::string dataFile;
    std::string webDir;
    std::string seatCfg;
    double threshold;

    Options()
        : port(8080),
          openHour(6),
          closeHour(24),
          cameraIndex(-1),
          browserCamera(false),
          video(""),
          dataFile("bookings.txt"),
          webDir("web"),
          seatCfg("seats.cfg"),
          threshold(18.0) {}
};

static void printHelp() {
    std::cout << "Usage: seatserver [options]\n"
                 "  --port N         HTTP port (default 8080)\n"
                 "  --open H         library opening hour, 0-23 (default 6)\n"
                 "  --close H        library closing hour, 1-24 (default 24)\n"
                 "  --browser-camera use this computer's camera through the web page (no install needed)\n"
                 "  --camera N       use webcam number N        (needs `make opencv`)\n"
                 "  --video FILE     use a recorded video file  (needs `make opencv`)\n"
                 "  --threshold X    detection sensitivity, lower = more sensitive (default 18)\n"
                 "  --seats FILE     seat rectangles file (default seats.cfg)\n"
                 "  --data FILE      bookings file (default bookings.txt)\n"
                 "  --web DIR        folder containing index.html (default web)\n"
                 "Without --camera/--video a simulated camera is used.\n";
}

static std::string nextArg(int& i, int argc, char** argv, const std::string& flag) {
    if (i + 1 >= argc) {
        throw ValidationError("Missing value after " + flag);
    }
    ++i;
    return std::string(argv[i]);
}

static Options parseArgs(int argc, char** argv) {
    Options o;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        try {
            if (a == "--port") {
                o.port = std::stoi(nextArg(i, argc, argv, a));
            } else if (a == "--open") {
                o.openHour = std::stoi(nextArg(i, argc, argv, a));
            } else if (a == "--close") {
                o.closeHour = std::stoi(nextArg(i, argc, argv, a));
            } else if (a == "--browser-camera") {
                o.browserCamera = true;
            } else if (a == "--camera") {
                o.cameraIndex = std::stoi(nextArg(i, argc, argv, a));
            } else if (a == "--video") {
                o.video = nextArg(i, argc, argv, a);
            } else if (a == "--threshold") {
                o.threshold = std::stod(nextArg(i, argc, argv, a));
            } else if (a == "--seats") {
                o.seatCfg = nextArg(i, argc, argv, a);
            } else if (a == "--data") {
                o.dataFile = nextArg(i, argc, argv, a);
            } else if (a == "--web") {
                o.webDir = nextArg(i, argc, argv, a);
            } else if (a == "--help" || a == "-h") {
                printHelp();
                std::exit(0);
            } else {
                throw ValidationError("Unknown option " + a);
            }
        } catch (const ValidationError&) {
            throw;
        } catch (const std::exception&) {
            throw ValidationError("Bad number for " + a);
        }
    }
    return o;
}

static int nowMinutes() {
    std::time_t t = std::time(0);
    std::tm lt;
    localtime_r(&t, &lt);
    return lt.tm_hour * 60 + lt.tm_min;
}

static std::vector<Rect> defaultLayout() {
    std::vector<Rect> r;
    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 4; ++col) {
            r.push_back(Rect(30 + col * 150, 30 + row * 150, 120, 100));
        }
    }
    return r;
}

static std::vector<Rect> loadOrCreateLayout(const std::string& path) {
    std::vector<Rect> rois;
    std::ifstream in(path.c_str(), std::ios::in);
    std::string line;
    while (in && std::getline(in, line)) {
        if (line.empty() || line[0] == '#') {
            continue;
        }
        std::istringstream ls(line);
        Rect r;
        if (ls >> r) {
            rois.push_back(r);
        }
    }
    in.close();
    if (rois.empty()) {
        rois = defaultLayout();
        std::ofstream out(path.c_str(), std::ios::out | std::ios::trunc);
        out << "# One line per seat: x y w h   (pixels in a 640x480 camera frame; seat 1 = first line)\n";
        for (std::size_t i = 0; i < rois.size(); ++i) {
            out << rois[i] << '\n';
        }
        out.close();
    }
    return rois;
}

static std::string readFile(const std::string& path) {
    std::ifstream f(path.c_str(), std::ios::in | std::ios::binary);
    if (!f) {
        throw NotFoundError("File not found: " + path + " (run the program from the project folder)");
    }
    std::ostringstream ss;
    ss << f.rdbuf();
    f.close();
    return ss.str();
}

static HttpResponse jsonResponse(int status, const std::string& body) {
    HttpResponse res;
    res.status = status;
    res.contentType = "application/json";
    res.body = body;
    return res;
}

static HttpResponse errorJson(const LibraryError& e) {
    return jsonResponse(e.httpStatus(),
                        std::string("{\"ok\":false,\"code\":\"") + e.code() +
                            "\",\"error\":\"" + BookingManager::jsonEscape(e.what()) + "\"}");
}

// Application router class overloading operator() (replaces lambda callback)
class RouteHandler {
private:
    const Options* opt_;
    BookingManager* mgr_;
    SeatMonitor* monitor_;
    SimulatedCamera* sim_;
    BrowserCamera* bcam_;

public:
    RouteHandler(const Options* opt,
                 BookingManager* mgr,
                 SeatMonitor* monitor,
                 SimulatedCamera* sim,
                 BrowserCamera* bcam)
        : opt_(opt), mgr_(mgr), monitor_(monitor), sim_(sim), bcam_(bcam) {}

    HttpResponse operator()(const HttpRequest& req) const {
        try {
            if (req.method == "GET" && (req.path == "/" || req.path == "/index.html")) {
                HttpResponse res;
                res.status = 200;
                res.contentType = "text/html; charset=utf-8";
                res.body = readFile(this->opt_->webDir + "/index.html");
                return res;
            }

            if (req.method == "GET" && req.path == "/api/seats") {
                this->monitor_->step();
                TimeSlot slot(req.intParam("start", -1), req.intParam("end", -1));
                int now = nowMinutes();
                if (slot.start < 0 || slot.end < 0) {
                    return jsonResponse(200, this->mgr_->seatsJson(now));
                }
                return jsonResponse(200, this->mgr_->seatsJson(now, slot));
            }

            if (req.method == "GET" && req.path == "/api/frame.bmp") {
                HttpResponse res;
                res.status = 200;
                res.contentType = "image/bmp";
                res.body = this->monitor_->snapshotBmp();
                return res;
            }

            if (req.method == "GET" && req.path == "/api/status") {
                return jsonResponse(200,
                                    "{\"ok\":true,\"camera\":\"" +
                                        BookingManager::jsonEscape(this->monitor_->cameraKind()) +
                                        "\",\"status\":\"" +
                                        BookingManager::jsonEscape(this->monitor_->status()) + "\"}");
            }

            if (req.method == "POST" && req.path == "/api/book") {
                this->monitor_->step();
                std::string name = req.param("name");
                int seat = req.intParam("seat", 0);
                TimeSlot slot(req.intParam("start", -1), req.intParam("end", -1));
                int now = nowMinutes();
                Booking b = (seat > 0) ? this->mgr_->book(name, seat, slot, now)
                                       : this->mgr_->book(name, slot, now);
                std::ostringstream msg;
                msg << "Seat " << b.seatId() << " booked for " << b.slot();
                return jsonResponse(200,
                                    "{\"ok\":true,\"message\":\"" + msg.str() +
                                        "\",\"booking\":" + BookingManager::bookingJson(b) + "}");
            }

            if (req.method == "POST" && req.path == "/api/cancel") {
                this->mgr_->cancel(req.intParam("id", -1), req.param("name"));
                return jsonResponse(200, "{\"ok\":true,\"message\":\"Booking cancelled\"}");
            }

            if (req.method == "POST" && req.path == "/api/upload") {
                if (!this->bcam_) {
                    throw ValidationError("Browser camera is off. Start the server with --browser-camera");
                }
                this->bcam_->push(req.body);
                this->monitor_->step();
                return jsonResponse(200, "{\"ok\":true}");
            }

            if (req.method == "POST" && req.path == "/api/calibrate") {
                this->monitor_->calibrate();
                return jsonResponse(200, "{\"ok\":true,\"message\":\"Camera recalibrated (assumed empty library)\"}");
            }

            if (req.method == "POST" && req.path == "/api/simulate") {
                if (!this->sim_) {
                    throw ValidationError("Simulation is only available with the simulated camera");
                }
                this->sim_->setPresent(req.intParam("seat", 0) - 1, req.intParam("present", 0) != 0);
                // Run two detector steps so hysteresis immediately reflects the simulated toggle
                this->monitor_->step();
                this->monitor_->step();
                return jsonResponse(200, "{\"ok\":true}");
            }

            return jsonResponse(404, "{\"ok\":false,\"code\":\"NOT_FOUND\",\"error\":\"Unknown route\"}");
        } catch (const LibraryError& e) {
            return errorJson(e);
        }
    }
};

int main(int argc, char** argv) {
    try {
        Options opt = parseArgs(argc, argv);

        std::vector<Rect> rois = loadOrCreateLayout(opt.seatCfg);
        BookingManager mgr(opt.openHour, opt.closeHour, opt.dataFile);
        for (std::size_t i = 0; i < rois.size(); ++i) {
            int id = static_cast<int>(i) + 1;
            if (rois.size() > 4 && i >= rois.size() - 4) {
                mgr.addSeat(new ComputerSeat(id, rois[i]));
            } else {
                mgr.addSeat(new StudySeat(id, rois[i]));
            }
        }
        int restored = mgr.load();

        Camera* cam = 0;
        SimulatedCamera* sim = 0;
        BrowserCamera* bcam = 0;
        if (opt.browserCamera) {
            bcam = new BrowserCamera(640, 480);
            cam = bcam;
        } else if (opt.cameraIndex >= 0 || !opt.video.empty()) {
#ifdef USE_OPENCV
            if (opt.video.empty()) {
                cam = new OpenCVCamera(opt.cameraIndex);
            } else {
                cam = new OpenCVCamera(opt.video);
            }
#else
            throw CameraError("This build has no OpenCV. Rebuild with `make opencv` to use a real camera.");
#endif
        } else {
            sim = new SimulatedCamera(640, 480, rois);
            cam = sim;
        }
        mgr.setCameraMode(sim ? "simulated" : (bcam ? "browser" : "hardware"));

        SeatMonitor monitor(cam, mgr, opt.threshold);
        monitor.start();

        RouteHandler handler(&opt, &mgr, &monitor, sim, bcam);
        HttpServer server(opt.port, handler);

        g_server = &server;
        std::signal(SIGINT, onSignal);
        std::signal(SIGTERM, onSignal);

        std::cout << "Library Seat Booking\n"
                  << "  camera : " << monitor.cameraKind() << "\n"
                  << "  seats  : " << rois.size() << " (layout in " << opt.seatCfg << ")\n"
                  << "  hours  : " << formatTime(opt.openHour * 60) << " - " << formatTime(opt.closeHour * 60) << "\n"
                  << "  restored " << restored << " booking(s) from " << opt.dataFile << "\n"
                  << "  open   : http://localhost:" << opt.port << "   (Ctrl+C to stop)\n";
        if (bcam) {
            std::cout << "  NEXT   : open that address in THIS computer's browser and press \"Start camera\"\n";
        }
        std::cout << std::flush;
        server.run();
        std::cout << "\nShutting down...\n";
        g_server = 0;
        monitor.stop();
        return 0;
    } catch (const LibraryError& e) {
        std::cerr << "Error [" << e.code() << "]: " << e.what() << '\n';
        return 1;
    } catch (const std::exception& e) {
        std::cerr << "Unexpected error: " << e.what() << '\n';
        return 2;
    }
}
