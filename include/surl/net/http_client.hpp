#pragma once

#include "surl/net/url.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace surl {

using HeaderList = std::vector<std::pair<std::string, std::string>>;

/// Looks up a header case-insensitively. Returns an empty string when absent.
std::string find_header(const HeaderList& headers, std::string_view name);

/// Parses the raw "HTTP/1.1 200 OK\r\nName: value\r\n..." block WinHTTP and
/// libcurl both hand back, into a header list.
HeaderList parse_raw_headers(std::string_view raw);

struct HttpRequest {
    std::string method = "GET";
    Url url;
    HeaderList headers;
    std::string body;

    /// Abort the transfer once this many body bytes have arrived. 0 = no limit.
    std::uint64_t max_body_bytes = 0;
    /// Total time budget for the request, in milliseconds.
    std::uint32_t timeout_ms = 30000;
    bool follow_redirects = true;
    int max_redirects = 10;
    /// When true only the headers are fetched (HEAD semantics).
    bool head_only = false;
};

struct HttpResponse {
    int status = 0;
    std::string status_text;
    HeaderList headers;
    std::string body;
    /// The URL the response actually came from, after any redirects.
    Url final_url;
    /// Set when max_body_bytes cut the download short.
    bool truncated = false;
    /// Non-empty when the transfer failed before a status line was read.
    std::string error;
    std::uint64_t elapsed_ms = 0;

    bool ok() const { return error.empty() && status >= 200 && status < 300; }
    std::string header(std::string_view name) const { return find_header(headers, name); }
    std::string content_type() const { return header("Content-Type"); }
};

struct HttpClientConfig {
    std::string user_agent;
    /// "http://host:port" or "host:port"; empty means "use system settings".
    std::string proxy;
    /// Skip TLS certificate validation. Off by default and loudly warned about.
    bool insecure = false;
    std::uint32_t connect_timeout_ms = 15000;
    std::uint32_t timeout_ms = 30000;
};

/// A thread-safe HTTP client. One instance is shared by every crawler worker.
class HttpClient {
public:
    virtual ~HttpClient() = default;
    virtual HttpResponse send(const HttpRequest& request) = 0;

    /// Creates the platform client (WinHTTP on Windows, libcurl elsewhere).
    /// Returns nullptr and fills @p error when the backend cannot start.
    static std::unique_ptr<HttpClient> create(const HttpClientConfig& config,
                                              std::string& error);

    /// Name of the compiled-in backend, for `surl doctor`.
    static const char* backend_name();
};

} // namespace surl
