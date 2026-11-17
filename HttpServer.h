
#pragma once
#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <thread>
#include "Exceptions.h"

struct HttpRequest {
    std::string method, path, query, body;
    std::map<std::string, std::string> params;  // query string + form body, URL-decoded

    std::string param(const std::string& key, const std::string& def = "") const {
        auto it = params.find(key);
        return it == params.end() ? def : it->second;
    }
    int intParam(const std::string& key, int def) const {  // overload-style helper
        auto it = params.find(key);
        if (it == params.end()) return def;
        try {
            return std::stoi(it->second);
        } catch (const std::exception&) {
            return def;
        }
    }
};

struct HttpResponse {
    int status = 200;
    std::string contentType = "application/json";
    std::string body;
};

class HttpServer {
public:
    using Handler = std::function<HttpResponse(const HttpRequest&)>;

    HttpServer(int port, Handler handler) : port_(port), handler_(std::move(handler)) {}
    ~HttpServer() {
        stop();
        if (listenFd_ >= 0) ::close(listenFd_);
    }

    // Blocks until stop() is called (e.g. from a Ctrl+C handler)
    void run() {
        std::signal(SIGPIPE, SIG_IGN);
        listenFd_ = ::socket(AF_INET, SOCK_STREAM, 0);
        if (listenFd_ < 0) throw LibraryError("socket() failed");
        int yes = 1;
        ::setsockopt(listenFd_, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes);
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_ANY);  // reachable from phones on the same Wi-Fi
        addr.sin_port = htons(static_cast<uint16_t>(port_));
        if (::bind(listenFd_, reinterpret_cast<sockaddr*>(&addr), sizeof addr) < 0)
            throw LibraryError("Cannot bind to port " + std::to_string(port_) + " (already in use?)");
        if (::listen(listenFd_, 64) < 0) throw LibraryError("listen() failed");
        running_ = true;
        while (running_) {
            pollfd p{listenFd_, POLLIN, 0};
            if (::poll(&p, 1, 300) <= 0) continue;  // wake up regularly to notice stop()
            int fd = ::accept(listenFd_, nullptr, nullptr);
            if (fd < 0) continue;
            ++active_;
            std::thread(&HttpServer::serveClient, this, fd).detach();
        }
        for (int i = 0; i < 20 && active_ > 0; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(100));
        ::close(listenFd_);
        listenFd_ = -1;
    }
    void stop() { running_ = false; }  // async-signal-safe: just flips a flag

private:
    int port_;
    Handler handler_;
    std::atomic<bool> running_{false};
    std::atomic<int> active_{0};
    int listenFd_ = -1;

    static std::string urlDecode(const std::string& s) {
        std::string o;
        for (std::size_t i = 0; i < s.size(); ++i) {
            if (s[i] == '+') o += ' ';
            else if (s[i] == '%' && i + 2 < s.size() + 0 && std::isxdigit((unsigned char)s[i + 1]) &&
                     std::isxdigit((unsigned char)s[i + 2])) {
                o += static_cast<char>(std::strtol(s.substr(i + 1, 2).c_str(), nullptr, 16));
                i += 2;
            } else o += s[i];
        }
        return o;
    }

    static void parseParams(const std::string& s, std::map<std::string, std::string>& out) {
        std::size_t pos = 0;
        while (pos <= s.size()) {
            std::size_t amp = s.find('&', pos);
            std::string pair = s.substr(pos, amp == std::string::npos ? std::string::npos : amp - pos);
            if (!pair.empty()) {
                std::size_t eq = pair.find('=');
                if (eq == std::string::npos) out[urlDecode(pair)] = "";
                else out[urlDecode(pair.substr(0, eq))] = urlDecode(pair.substr(eq + 1));
            }
            if (amp == std::string::npos) break;
            pos = amp + 1;
        }
    }

    static const char* statusText(int s) {
        switch (s) {
            case 200: return "OK";
            case 400: return "Bad Request";
            case 404: return "Not Found";
            case 409: return "Conflict";
            case 503: return "Service Unavailable";
            default: return "Internal Server Error";
        }
    }

    void serveClient(int fd) {
        struct Guard {
            int fd;
            std::atomic<int>& counter;
            ~Guard() {
                ::close(fd);
                --counter;
            }
        } guard{fd, active_};
        try {
            timeval tv{5, 0};
            ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
            std::string data;
            char buf[4096];
            std::size_t headerEnd;
            while ((headerEnd = data.find("\r\n\r\n")) == std::string::npos) {
                ssize_t n = ::recv(fd, buf, sizeof buf, 0);
                if (n <= 0 || data.size() > (1u << 20)) return;
                data.append(buf, static_cast<std::size_t>(n));
            }
            std::string head = data.substr(0, headerEnd);
            std::string body = data.substr(headerEnd + 4);

            std::istringstream hs(head);
            HttpRequest req;
            std::string target, version;
            hs >> req.method >> target >> version;

            std::string low = head;
            for (char& c : low) c = static_cast<char>(std::tolower((unsigned char)c));
            std::size_t clen = 0;
            std::size_t cp = low.find("content-length:");
            if (cp != std::string::npos) clen = std::strtoul(low.c_str() + cp + 15, nullptr, 10);
            if (clen > (1u << 20)) return;
            while (body.size() < clen) {
                ssize_t n = ::recv(fd, buf, sizeof buf, 0);
                if (n <= 0) break;
                body.append(buf, static_cast<std::size_t>(n));
            }

            std::size_t q = target.find('?');
            req.path = target.substr(0, q);
            if (q != std::string::npos) req.query = target.substr(q + 1);
            req.body = body;
            parseParams(req.query, req.params);
            // Form bodies are parsed into params; raw binary uploads (camera pictures) are left alone
            bool binaryBody = low.find("content-type: application/octet-stream") != std::string::npos;
            if (req.method == "POST" && !binaryBody) parseParams(req.body, req.params);

            HttpResponse res;
            try {
                res = handler_(req);
            } catch (const std::exception& e) {
                res.status = 500;
                res.contentType = "application/json";
                res.body = std::string("{\"ok\":false,\"error\":\"internal error\"}");
                std::cerr << "[error] " << e.what() << '\n';
            }
            std::ostringstream out;
            out << "HTTP/1.1 " << res.status << ' ' << statusText(res.status) << "\r\n"
                << "Content-Type: " << res.contentType << "\r\n"
                << "Content-Length: " << res.body.size() << "\r\n"
                << "Cache-Control: no-store\r\n"
                << "Connection: close\r\n\r\n";
            std::string wire = out.str() + res.body;
            std::size_t sent = 0;
            while (sent < wire.size()) {
                ssize_t n = ::send(fd, wire.data() + sent, wire.size() - sent, 0);
                if (n <= 0) break;
                sent += static_cast<std::size_t>(n);
            }
        } catch (...) {
            // never let one bad client take the server down
        }
    }
};
