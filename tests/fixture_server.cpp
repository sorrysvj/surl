#include "fixture_server.hpp"

#include <algorithm>
#include <cstring>
#include <vector>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
using socket_t = SOCKET;
static constexpr socket_t kInvalid = INVALID_SOCKET;
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
using socket_t = int;
static constexpr socket_t kInvalid = -1;
#endif

namespace surltest {
namespace {

void close_socket(socket_t handle) {
#ifdef _WIN32
    closesocket(handle);
#else
    close(handle);
#endif
}

struct WinsockGuard {
    WinsockGuard() {
#ifdef _WIN32
        WSADATA data;
        WSAStartup(MAKEWORD(2, 2), &data);
#endif
    }
};

void ensure_winsock() {
    static WinsockGuard guard;
    (void)guard;
}

std::string trim_copy(const std::string& text) {
    std::size_t begin = 0;
    std::size_t end = text.size();
    while (begin < end && std::isspace(static_cast<unsigned char>(text[begin]))) ++begin;
    while (end > begin && std::isspace(static_cast<unsigned char>(text[end - 1]))) --end;
    return text.substr(begin, end - begin);
}

const char* status_text(int status) {
    switch (status) {
    case 200: return "OK";
    case 301: return "Moved Permanently";
    case 302: return "Found";
    case 304: return "Not Modified";
    case 403: return "Forbidden";
    case 404: return "Not Found";
    case 500: return "Internal Server Error";
    case 503: return "Service Unavailable";
    default: return "OK";
    }
}

bool send_all(socket_t client, const std::string& data) {
    std::size_t sent = 0;
    while (sent < data.size()) {
        const int chunk = static_cast<int>(std::min<std::size_t>(data.size() - sent, 32768));
        const int written = ::send(client, data.data() + sent, chunk, 0);
        if (written <= 0) return false;
        sent += static_cast<std::size_t>(written);
    }
    return true;
}

} // namespace

FixtureServer::FixtureServer() { ensure_winsock(); }

FixtureServer::~FixtureServer() { stop(); }

void FixtureServer::add(const std::string& path, FixtureResource resource) {
    std::lock_guard<std::mutex> lock(mutex_);
    resources_[path] = std::move(resource);
}

void FixtureServer::add_html(const std::string& path, std::string body) {
    FixtureResource resource;
    resource.body = std::move(body);
    resource.content_type = "text/html; charset=utf-8";
    add(path, std::move(resource));
}

void FixtureServer::update_body(const std::string& path, std::string body, std::string etag) {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = resources_.find(path);
    if (it == resources_.end()) return;
    it->second.body = std::move(body);
    it->second.etag = std::move(etag);
}

std::string FixtureServer::base_url() const {
    return "http://127.0.0.1:" + std::to_string(port_);
}

int FixtureServer::hits(const std::string& path) const {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = hits_.find(path);
    return it == hits_.end() ? 0 : it->second;
}

void FixtureServer::reset_hits() {
    std::lock_guard<std::mutex> lock(mutex_);
    hits_.clear();
}

bool FixtureServer::start() {
    const socket_t listener = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listener == kInvalid) return false;

    int reuse = 1;
    ::setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuse),
                 sizeof(reuse));

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = 0; // let the OS pick a free port

    if (::bind(listener, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) {
        close_socket(listener);
        return false;
    }
    if (::listen(listener, 16) != 0) {
        close_socket(listener);
        return false;
    }

    sockaddr_in bound{};
#ifdef _WIN32
    int length = sizeof(bound);
#else
    socklen_t length = sizeof(bound);
#endif
    if (::getsockname(listener, reinterpret_cast<sockaddr*>(&bound), &length) != 0) {
        close_socket(listener);
        return false;
    }
    port_ = ntohs(bound.sin_port);

    listener_ = static_cast<long long>(listener);
    running_.store(true);
    thread_ = std::thread([this] { serve(); });
    return true;
}

void FixtureServer::stop() {
    if (!running_.exchange(false)) return;
    if (thread_.joinable()) thread_.join();
    if (listener_ != -1) {
        close_socket(static_cast<socket_t>(listener_));
        listener_ = -1;
    }
}

void FixtureServer::serve() {
    const socket_t listener = static_cast<socket_t>(listener_);

    while (running_.load()) {
        // A short select timeout lets stop() take effect without having to
        // poke the listener with a dummy connection.
        fd_set readable;
        FD_ZERO(&readable);
        FD_SET(listener, &readable);
        timeval timeout{};
        timeout.tv_sec = 0;
        timeout.tv_usec = 50000;

        const int ready = ::select(static_cast<int>(listener) + 1, &readable, nullptr,
                                   nullptr, &timeout);
        if (ready <= 0) continue;

        const socket_t client = ::accept(listener, nullptr, nullptr);
        if (client == kInvalid) continue;
        handle(static_cast<long long>(client));
    }
}

void FixtureServer::handle(long long client_handle) {
    const socket_t client = static_cast<socket_t>(client_handle);

    std::string request;
    char buffer[4096];
    while (request.find("\r\n\r\n") == std::string::npos && request.size() < 32768) {
        const int got = ::recv(client, buffer, sizeof(buffer), 0);
        if (got <= 0) break;
        request.append(buffer, static_cast<std::size_t>(got));
    }
    if (request.empty()) {
        close_socket(client);
        return;
    }

    // Request line: METHOD SP TARGET SP VERSION
    const std::size_t first_space = request.find(' ');
    const std::size_t second_space = request.find(' ', first_space + 1);
    if (first_space == std::string::npos || second_space == std::string::npos) {
        close_socket(client);
        return;
    }
    const std::string method = request.substr(0, first_space);
    std::string target = request.substr(first_space + 1, second_space - first_space - 1);

    const std::size_t query = target.find('?');
    const std::string path_only = (query == std::string::npos) ? target : target.substr(0, query);

    // Pull out If-None-Match for conditional-request tests.
    std::string if_none_match;
    {
        std::size_t pos = 0;
        while ((pos = request.find("\r\n", pos)) != std::string::npos) {
            pos += 2;
            const std::size_t line_end = request.find("\r\n", pos);
            if (line_end == std::string::npos || line_end == pos) break;
            const std::string line = request.substr(pos, line_end - pos);
            const std::size_t colon = line.find(':');
            if (colon != std::string::npos) {
                std::string name = line.substr(0, colon);
                std::transform(name.begin(), name.end(), name.begin(),
                               [](unsigned char c) { return static_cast<char>(::tolower(c)); });
                if (name == "if-none-match") if_none_match = trim_copy(line.substr(colon + 1));
            }
            pos = line_end;
        }
    }

    FixtureResource resource;
    bool found = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        hits_[target] += 1;
        if (path_only != target) hits_[path_only] += 1;
        const auto it = resources_.find(target);
        if (it != resources_.end()) {
            resource = it->second;
            found = true;
        } else {
            const auto fallback = resources_.find(path_only);
            if (fallback != resources_.end()) {
                resource = fallback->second;
                found = true;
            }
        }
    }

    std::string response;
    if (!found) {
        const std::string body = "not found";
        response = "HTTP/1.1 404 Not Found\r\n"
                   "Content-Type: text/plain\r\n"
                   "Content-Length: " + std::to_string(body.size()) + "\r\n"
                   "Connection: close\r\n\r\n" + body;
    } else if (!resource.etag.empty() && if_none_match == resource.etag) {
        response = "HTTP/1.1 304 Not Modified\r\n"
                   "ETag: " + resource.etag + "\r\n"
                   "Connection: close\r\n\r\n";
    } else {
        response = "HTTP/1.1 " + std::to_string(resource.status) + " " +
                   status_text(resource.status) + "\r\n";
        response += "Content-Type: " + resource.content_type + "\r\n";
        response += "Content-Length: " + std::to_string(resource.body.size()) + "\r\n";
        if (!resource.etag.empty()) response += "ETag: " + resource.etag + "\r\n";
        if (!resource.location.empty()) response += "Location: " + resource.location + "\r\n";
        response += "Connection: close\r\n\r\n";
        if (method != "HEAD") response += resource.body;
    }

    send_all(client, response);
    close_socket(client);
}

} // namespace surltest
