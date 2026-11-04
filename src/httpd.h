#pragma once
#include "common.h"
#include <map>
#include <chrono>
// Minimal non-blocking HTTP/1.1 server (loopback) used by the browser front-end.
// poll() must be called regularly from the emulation loop; it never blocks (browsers open
// speculative connections that may never send a request).
class Httpd {
public:
    struct Request { std::string method, path, body; };
    struct Response { int status = 200; std::string type = "text/plain"; std::string body; };
    bool start(int port, const std::string& doc_root);
    void stop();
    void poll();
    std::function<Response(const Request&)> handler;
private:
    struct Conn { int fd; std::string in, out; size_t out_off = 0; std::chrono::steady_clock::time_point t0; bool responded = false; };
    void process(Conn& c);
    Response route(const Request& r);
    int lfd_ = -1; std::string root_;
    std::vector<Conn> conns_;
};
