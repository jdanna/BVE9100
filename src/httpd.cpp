#include "httpd.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <fstream>
#include <sstream>
#include <cstring>
#include <algorithm>

bool Httpd::start(int port, const std::string& doc_root) {
    root_ = doc_root;
    lfd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (lfd_ < 0) return false;
    int one = 1; setsockopt(lfd_, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    sockaddr_in a{}; a.sin_family = AF_INET; a.sin_port = htons((uint16_t)port); a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(lfd_, (sockaddr*)&a, sizeof a) < 0 || listen(lfd_, 16) < 0) { close(lfd_); lfd_ = -1; return false; }
    fcntl(lfd_, F_SETFL, O_NONBLOCK);
    return true;
}
void Httpd::stop() { for (auto& c : conns_) close(c.fd); conns_.clear(); if (lfd_ >= 0) close(lfd_); lfd_ = -1; }
static std::string mime(const std::string& p) {
    if (p.size() > 5 && p.substr(p.size() - 5) == ".html") return "text/html; charset=utf-8";
    if (p.size() > 3 && p.substr(p.size() - 3) == ".js") return "application/javascript";
    if (p.size() > 4 && p.substr(p.size() - 4) == ".css") return "text/css";
    if (p.size() > 5 && p.substr(p.size() - 5) == ".json") return "application/json";
    return "application/octet-stream";
}
Httpd::Response Httpd::route(const Request& r) {
    Response resp;
    if (r.method == "GET" && (r.path == "/" || r.path.rfind("/ui/", 0) == 0)) {
        std::string rel = (r.path == "/") ? "index.html" : r.path.substr(4);
        if (rel.find("..") != std::string::npos) { resp.status = 400; resp.body = "bad path"; return resp; }
        std::ifstream f(root_ + "/" + rel, std::ios::binary);
        if (!f) { resp.status = 404; resp.body = "not found: " + rel; return resp; }
        std::stringstream b; b << f.rdbuf(); resp.body = b.str(); resp.type = mime(rel); return resp;
    }
    if (handler) return handler(r);
    resp.status = 404; resp.body = "no handler"; return resp;
}
void Httpd::process(Conn& c) {
    size_t hdr_end = c.in.find("\r\n\r\n");
    if (hdr_end == std::string::npos) return;
    std::string h = c.in.substr(0, hdr_end), lh = h;
    for (auto& ch : lh) ch = (char)tolower(ch);
    size_t content_len = 0, cl = lh.find("content-length:");
    if (cl != std::string::npos) content_len = (size_t)strtoul(h.c_str() + cl + 15, nullptr, 10);
    if (c.in.size() < hdr_end + 4 + content_len) return;          // body incomplete
    Request r;
    std::istringstream ss(h.substr(0, h.find("\r\n")));
    ss >> r.method >> r.path;
    size_t q = r.path.find('?'); if (q != std::string::npos) r.path = r.path.substr(0, q);
    r.body = c.in.substr(hdr_end + 4, content_len);
    Response resp = route(r);
    std::ostringstream out;
    out << "HTTP/1.1 " << resp.status << (resp.status == 200 ? " OK" : " ERR") << "\r\nContent-Type: " << resp.type
        << "\r\nContent-Length: " << resp.body.size() << "\r\nCache-Control: no-store\r\nConnection: close\r\n\r\n" << resp.body;
    c.out = out.str(); c.responded = true;
}
void Httpd::poll() {
    if (lfd_ < 0) return;
    for (int n = 0; n < 16; n++) {
        int fd = accept(lfd_, nullptr, nullptr);
        if (fd < 0) break;
        fcntl(fd, F_SETFL, O_NONBLOCK);
        conns_.push_back({fd, "", "", 0, std::chrono::steady_clock::now(), false});
    }
    auto now = std::chrono::steady_clock::now();
    for (auto& c : conns_) {
        if (!c.responded) {
            char buf[8192];
            for (int k = 0; k < 8; k++) {
                ssize_t r = recv(c.fd, buf, sizeof buf, 0);
                if (r > 0) c.in.append(buf, (size_t)r);
                else { if (r == 0) c.responded = true; break; }   // peer closed: drop
            }
            if (!c.responded) process(c);
        }
        if (c.responded && c.out_off < c.out.size()) {
            ssize_t w = send(c.fd, c.out.data() + c.out_off, c.out.size() - c.out_off, 0);
            if (w > 0) c.out_off += (size_t)w;
        }
    }
    conns_.erase(std::remove_if(conns_.begin(), conns_.end(), [&](Conn& c) {
        bool done = c.responded && c.out_off >= c.out.size();
        bool stale = std::chrono::duration<double>(now - c.t0).count() > 10.0;
        if (done || stale) { close(c.fd); return true; }
        return false;
    }), conns_.end());
}
