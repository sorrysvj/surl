#include "surl/cmd/commands.hpp"

#include "surl/util/fsutil.hpp"
#include "surl/util/log.hpp"
#include "surl/util/mime.hpp"
#include "surl/util/strings.hpp"

#include <atomic>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
using socket_t = SOCKET;
constexpr socket_t kInvalidSocket = INVALID_SOCKET;
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
using socket_t = int;
constexpr socket_t kInvalidSocket = -1;
#endif

namespace fs = std::filesystem;

namespace surl {
namespace {

void close_socket(socket_t handle) {
#ifdef _WIN32
    closesocket(handle);
#else
    close(handle);
#endif
}

class SocketLibrary {
public:
    SocketLibrary() {
#ifdef _WIN32
        WSADATA data;
        ok_ = WSAStartup(MAKEWORD(2, 2), &data) == 0;
#endif
    }
    ~SocketLibrary() {
#ifdef _WIN32
        if (ok_) WSACleanup();
#endif
    }
    bool ok() const { return ok_; }

private:
    bool ok_ = true;
};

bool send_all(socket_t client, const char* data, std::size_t size) {
    std::size_t sent = 0;
    while (sent < size) {
        const int chunk = static_cast<int>(
            std::min<std::size_t>(size - sent, 64 * 1024));
        const int written = ::send(client, data + sent, chunk, 0);
        if (written <= 0) return false;
        sent += static_cast<std::size_t>(written);
    }
    return true;
}

void send_response(socket_t client, int status, std::string_view status_text,
                   std::string_view content_type, std::string_view body,
                   bool include_body = true) {
    std::string head;
    head += "HTTP/1.1 " + std::to_string(status) + " " + std::string(status_text) + "\r\n";
    head += "Content-Type: " + std::string(content_type) + "\r\n";
    head += "Content-Length: " + std::to_string(body.size()) + "\r\n";
    head += "Connection: close\r\n";
    // A local mirror is static; caching it would only confuse iteration.
    head += "Cache-Control: no-store\r\n";
    head += "X-Content-Type-Options: nosniff\r\n";
    head += "\r\n";

    if (!send_all(client, head.data(), head.size())) return;
    if (include_body && !body.empty()) send_all(client, body.data(), body.size());
}

std::string error_page(int status, std::string_view message) {
    return "<!doctype html><html><head><meta charset=\"utf-8\">"
           "<title>" + std::to_string(status) + "</title></head>"
           "<body style=\"font-family:system-ui,sans-serif;padding:2rem\">"
           "<h1>" + std::to_string(status) + "</h1><p>" + std::string(message) +
           "</p><hr><p style=\"color:#666\">surl serve</p></body></html>";
}

/// Maps a request target onto a file inside the served root, refusing anything
/// that would escape it.
bool resolve_request_path(const fs::path& root, std::string_view target, fs::path& out,
                          bool& is_directory_index) {
    is_directory_index = false;

    std::string path(target);
    const std::size_t query = path.find('?');
    if (query != std::string::npos) path = path.substr(0, query);
    const std::size_t hash = path.find('#');
    if (hash != std::string::npos) path = path.substr(0, hash);

    path = percent_decode(path);
    if (path.empty() || path.front() != '/') return false;

    fs::path candidate = root;
    for (const std::string& component : split(path, '/', false)) {
        // Reject traversal outright rather than normalising it away.
        if (component == "." || component == "..") return false;
        candidate /= utf8_to_path(component);
    }

    std::error_code ec;
    if (fs::is_directory(candidate, ec)) {
        candidate /= "index.html";
        is_directory_index = true;
    }

    if (!is_inside(root, candidate)) return false;
    out = candidate;
    return true;
}

void handle_client(socket_t client, const fs::path& root) {
    std::string request;
    char buffer[8192];

    // Read until the end of the header block; a static server needs no body.
    while (request.find("\r\n\r\n") == std::string::npos && request.size() < 64 * 1024) {
        const int got = ::recv(client, buffer, sizeof(buffer), 0);
        if (got <= 0) break;
        request.append(buffer, static_cast<std::size_t>(got));
    }
    if (request.empty()) {
        close_socket(client);
        return;
    }

    const std::size_t line_end = request.find("\r\n");
    const std::string line = request.substr(0, line_end == std::string::npos ? request.size()
                                                                             : line_end);
    const std::vector<std::string> parts = split(line, ' ', false);
    if (parts.size() < 2) {
        send_response(client, 400, "Bad Request", "text/html; charset=utf-8",
                      error_page(400, "Malformed request line."));
        close_socket(client);
        return;
    }

    const std::string& method = parts[0];
    const std::string& target = parts[1];
    const bool head_only = (method == "HEAD");

    if (method != "GET" && !head_only) {
        send_response(client, 405, "Method Not Allowed", "text/html; charset=utf-8",
                      error_page(405, "Only GET and HEAD are supported."));
        close_socket(client);
        return;
    }

    fs::path file;
    bool directory_index = false;
    if (!resolve_request_path(root, target, file, directory_index)) {
        send_response(client, 403, "Forbidden", "text/html; charset=utf-8",
                      error_page(403, "That path is outside the served directory."));
        close_socket(client);
        return;
    }

    std::string body;
    if (!read_file(file, body)) {
        log().verbose("404 " + target);
        send_response(client, 404, "Not Found", "text/html; charset=utf-8",
                      error_page(404, "Not found in this mirror."));
        close_socket(client);
        return;
    }

    const std::string extension = path_to_utf8(file.extension());
    log().verbose("200 " + target);
    send_response(client, 200, "OK", content_type_for_extension(extension), body, !head_only);
    close_socket(client);
}

} // namespace

int cmd_serve(const Options& options) {
    fs::path root;
    std::string error;
    if (!resolve_mirror_directory(options, root, error)) {
        log().error(error);
        return kExitUsage;
    }

    std::error_code ec;
    root = fs::weakly_canonical(root, ec);
    if (ec) {
        log().error("could not resolve " + path_to_utf8(root));
        return kExitError;
    }

    SocketLibrary sockets;
    if (!sockets.ok()) {
        log().error("could not initialise networking");
        return kExitError;
    }

    const socket_t listener = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listener == kInvalidSocket) {
        log().error("could not create a listening socket");
        return kExitError;
    }

    int reuse = 1;
    ::setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuse),
                 sizeof(reuse));

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(options.port);
    if (::inet_pton(AF_INET, options.bind_address.c_str(), &address.sin_addr) != 1) {
        log().error("'" + options.bind_address + "' is not a valid IPv4 address");
        close_socket(listener);
        return kExitUsage;
    }

    if (::bind(listener, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) {
        log().error("could not bind " + options.bind_address + ":" +
                    std::to_string(options.port) + " (is something already using it?)");
        close_socket(listener);
        return kExitError;
    }

    if (::listen(listener, 32) != 0) {
        log().error("could not listen on " + options.bind_address);
        close_socket(listener);
        return kExitError;
    }

    log().info("Serving " + path_to_utf8(root));
    log().info("  http://" + options.bind_address + ":" + std::to_string(options.port) + "/");
    log().info("  press Ctrl+C to stop");

    while (true) {
        const socket_t client = ::accept(listener, nullptr, nullptr);
        if (client == kInvalidSocket) continue;
        // One thread per connection: a local preview server never sees the
        // concurrency that would make this a problem.
        std::thread(handle_client, client, root).detach();
    }
}

} // namespace surl
