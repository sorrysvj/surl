#include "surl/net/http_client.hpp"

#include "surl/util/fsutil.hpp"
#include "surl/util/log.hpp"
#include "surl/util/strings.hpp"

#include <windows.h>
#include <winhttp.h>

#include <chrono>
#include <mutex>
#include <vector>

namespace surl {
namespace {

std::wstring to_wide(std::string_view utf8) {
    if (utf8.empty()) return {};
    const int needed = MultiByteToWideChar(CP_UTF8, 0, utf8.data(),
                                           static_cast<int>(utf8.size()), nullptr, 0);
    if (needed <= 0) return {};
    std::wstring out(static_cast<std::size_t>(needed), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), out.data(),
                        needed);
    return out;
}

std::string from_wide(const wchar_t* text, int length = -1) {
    if (text == nullptr) return {};
    const int needed =
        WideCharToMultiByte(CP_UTF8, 0, text, length, nullptr, 0, nullptr, nullptr);
    if (needed <= 0) return {};
    std::string out(static_cast<std::size_t>(needed), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text, length, out.data(), needed, nullptr, nullptr);
    if (length == -1 && !out.empty() && out.back() == '\0') out.pop_back();
    return out;
}

std::string format_win_error(DWORD code) {
    // WinHTTP errors live in winhttp.dll's message table, everything else in
    // the system table.
    const HMODULE module = GetModuleHandleW(L"winhttp.dll");
    LPWSTR buffer = nullptr;
    DWORD flags = FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                  FORMAT_MESSAGE_IGNORE_INSERTS;
    if (code >= WINHTTP_ERROR_BASE && code <= WINHTTP_ERROR_LAST && module != nullptr) {
        flags |= FORMAT_MESSAGE_FROM_HMODULE;
    }
    const DWORD length = FormatMessageW(flags, module, code,
                                        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                                        reinterpret_cast<LPWSTR>(&buffer), 0, nullptr);
    std::string message;
    if (length > 0 && buffer != nullptr) {
        message = trim(from_wide(buffer, static_cast<int>(length)));
    }
    if (buffer != nullptr) LocalFree(buffer);
    if (message.empty()) {
        message = "WinHTTP error " + std::to_string(static_cast<unsigned long>(code));
    }
    return message;
}

/// RAII wrapper for the HINTERNET handles WinHTTP hands out.
class Handle {
public:
    Handle() = default;
    explicit Handle(HINTERNET handle) : handle_(handle) {}
    ~Handle() { reset(); }

    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    Handle(Handle&& other) noexcept : handle_(other.handle_) { other.handle_ = nullptr; }
    Handle& operator=(Handle&& other) noexcept {
        if (this != &other) {
            reset();
            handle_ = other.handle_;
            other.handle_ = nullptr;
        }
        return *this;
    }

    void reset(HINTERNET handle = nullptr) {
        if (handle_ != nullptr) WinHttpCloseHandle(handle_);
        handle_ = handle;
    }
    HINTERNET get() const { return handle_; }
    explicit operator bool() const { return handle_ != nullptr; }

private:
    HINTERNET handle_ = nullptr;
};

class WinHttpClient final : public HttpClient {
public:
    explicit WinHttpClient(const HttpClientConfig& config) : config_(config) {}

    ~WinHttpClient() override = default;

    bool open(std::string& error) {
        const std::wstring agent = to_wide(config_.user_agent);

        DWORD access_type = WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY;
        std::wstring proxy_name;
        const wchar_t* proxy_arg = WINHTTP_NO_PROXY_NAME;

        if (!config_.proxy.empty()) {
            access_type = WINHTTP_ACCESS_TYPE_NAMED_PROXY;
            // WinHTTP wants "host:port" without a scheme.
            std::string proxy = config_.proxy;
            for (const char* prefix : {"http://", "https://"}) {
                if (istarts_with(proxy, prefix)) {
                    proxy = proxy.substr(std::string_view(prefix).size());
                    break;
                }
            }
            while (!proxy.empty() && proxy.back() == '/') proxy.pop_back();
            proxy_name = to_wide(proxy);
            proxy_arg = proxy_name.c_str();
        }

        session_.reset(WinHttpOpen(agent.empty() ? nullptr : agent.c_str(), access_type,
                                   proxy_arg, WINHTTP_NO_PROXY_BYPASS, 0));
        if (!session_) {
            // Automatic proxy detection needs Windows 8+; fall back if refused.
            session_.reset(WinHttpOpen(agent.empty() ? nullptr : agent.c_str(),
                                       WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, proxy_arg,
                                       WINHTTP_NO_PROXY_BYPASS, 0));
        }
        if (!session_) {
            error = "could not initialise WinHTTP: " + format_win_error(GetLastError());
            return false;
        }

        // Prefer modern TLS. Older Windows builds reject the TLS 1.3 flag, so
        // retry without it rather than failing outright.
        DWORD protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2 | 0x00002000 /* TLS1_3 */;
        if (!WinHttpSetOption(session_.get(), WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols,
                              sizeof(protocols))) {
            protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
            WinHttpSetOption(session_.get(), WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols,
                             sizeof(protocols));
        }

        DWORD decompression = WINHTTP_DECOMPRESSION_FLAG_ALL;
        WinHttpSetOption(session_.get(), WINHTTP_OPTION_DECOMPRESSION, &decompression,
                         sizeof(decompression));

        WinHttpSetTimeouts(session_.get(), static_cast<int>(config_.connect_timeout_ms),
                           static_cast<int>(config_.connect_timeout_ms),
                           static_cast<int>(config_.timeout_ms),
                           static_cast<int>(config_.timeout_ms));
        return true;
    }

    HttpResponse send(const HttpRequest& request) override;

private:
    HttpClientConfig config_;
    Handle session_;
};

HttpResponse WinHttpClient::send(const HttpRequest& request) {
    const auto started = std::chrono::steady_clock::now();
    HttpResponse response;
    response.final_url = request.url;

    const auto finish = [&](std::string error) {
        response.error = std::move(error);
        response.elapsed_ms = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - started)
                .count());
        return response;
    };

    if (!request.url.is_http()) {
        return finish("unsupported scheme: " + request.url.scheme);
    }

    const std::wstring host = to_wide(request.url.host);
    const INTERNET_PORT port = static_cast<INTERNET_PORT>(request.url.effective_port());

    Handle connection(WinHttpConnect(session_.get(), host.c_str(), port, 0));
    if (!connection) {
        return finish("could not connect to " + request.url.host + ": " +
                      format_win_error(GetLastError()));
    }

    std::string target = request.url.path;
    if (target.empty()) target = "/";
    if (request.url.has_query) {
        target.push_back('?');
        target.append(request.url.query);
    }

    DWORD request_flags = WINHTTP_FLAG_REFRESH;
    if (request.url.scheme == "https") request_flags |= WINHTTP_FLAG_SECURE;

    const std::wstring method = to_wide(request.head_only ? "HEAD" : request.method);
    const std::wstring wide_target = to_wide(target);

    // Only ask for the media types we can actually store.
    static const wchar_t* kAcceptTypes[] = {L"*/*", nullptr};

    Handle req(WinHttpOpenRequest(connection.get(), method.c_str(), wide_target.c_str(),
                                  nullptr, WINHTTP_NO_REFERER, kAcceptTypes, request_flags));
    if (!req) {
        return finish("could not create request: " + format_win_error(GetLastError()));
    }

    WinHttpSetTimeouts(req.get(), static_cast<int>(config_.connect_timeout_ms),
                       static_cast<int>(config_.connect_timeout_ms),
                       static_cast<int>(request.timeout_ms),
                       static_cast<int>(request.timeout_ms));

    DWORD redirect_policy = request.follow_redirects
                                ? WINHTTP_OPTION_REDIRECT_POLICY_DISALLOW_HTTPS_TO_HTTP
                                : WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
    WinHttpSetOption(req.get(), WINHTTP_OPTION_REDIRECT_POLICY, &redirect_policy,
                     sizeof(redirect_policy));

    if (request.follow_redirects && request.max_redirects > 0) {
        DWORD limit = static_cast<DWORD>(request.max_redirects);
        WinHttpSetOption(req.get(), WINHTTP_OPTION_MAX_HTTP_AUTOMATIC_REDIRECTS, &limit,
                         sizeof(limit));
    }

    if (config_.insecure) {
        DWORD security_flags = SECURITY_FLAG_IGNORE_UNKNOWN_CA |
                               SECURITY_FLAG_IGNORE_CERT_DATE_INVALID |
                               SECURITY_FLAG_IGNORE_CERT_CN_INVALID |
                               SECURITY_FLAG_IGNORE_CERT_WRONG_USAGE;
        WinHttpSetOption(req.get(), WINHTTP_OPTION_SECURITY_FLAGS, &security_flags,
                         sizeof(security_flags));
    }

    DWORD enable_decompression = WINHTTP_DECOMPRESSION_FLAG_ALL;
    WinHttpSetOption(req.get(), WINHTTP_OPTION_DECOMPRESSION, &enable_decompression,
                     sizeof(enable_decompression));

    std::string header_block;
    for (const auto& [name, value] : request.headers) {
        header_block.append(name);
        header_block.append(": ");
        header_block.append(value);
        header_block.append("\r\n");
    }
    const std::wstring wide_headers = to_wide(header_block);

    const BOOL sent = WinHttpSendRequest(
        req.get(),
        wide_headers.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : wide_headers.c_str(),
        wide_headers.empty() ? 0 : static_cast<DWORD>(-1),
        request.body.empty() ? WINHTTP_NO_REQUEST_DATA
                             : const_cast<char*>(request.body.data()),
        static_cast<DWORD>(request.body.size()),
        static_cast<DWORD>(request.body.size()), 0);
    if (!sent) {
        return finish("request to " + request.url.to_string_no_fragment() + " failed: " +
                      format_win_error(GetLastError()));
    }

    if (!WinHttpReceiveResponse(req.get(), nullptr)) {
        return finish("no response from " + request.url.host + ": " +
                      format_win_error(GetLastError()));
    }

    DWORD status_code = 0;
    DWORD status_size = sizeof(status_code);
    if (WinHttpQueryHeaders(req.get(),
                            WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                            WINHTTP_HEADER_NAME_BY_INDEX, &status_code, &status_size,
                            WINHTTP_NO_HEADER_INDEX)) {
        response.status = static_cast<int>(status_code);
    }

    // Raw header block, then the status text.
    {
        DWORD size = 0;
        WinHttpQueryHeaders(req.get(), WINHTTP_QUERY_RAW_HEADERS_CRLF,
                            WINHTTP_HEADER_NAME_BY_INDEX, nullptr, &size,
                            WINHTTP_NO_HEADER_INDEX);
        if (size > 0) {
            std::wstring raw(size / sizeof(wchar_t), L'\0');
            if (WinHttpQueryHeaders(req.get(), WINHTTP_QUERY_RAW_HEADERS_CRLF,
                                    WINHTTP_HEADER_NAME_BY_INDEX, raw.data(), &size,
                                    WINHTTP_NO_HEADER_INDEX)) {
                response.headers = parse_raw_headers(from_wide(raw.c_str()));
            }
        }
    }
    {
        DWORD size = 0;
        WinHttpQueryHeaders(req.get(), WINHTTP_QUERY_STATUS_TEXT,
                            WINHTTP_HEADER_NAME_BY_INDEX, nullptr, &size,
                            WINHTTP_NO_HEADER_INDEX);
        if (size > 0) {
            std::wstring text(size / sizeof(wchar_t), L'\0');
            if (WinHttpQueryHeaders(req.get(), WINHTTP_QUERY_STATUS_TEXT,
                                    WINHTTP_HEADER_NAME_BY_INDEX, text.data(), &size,
                                    WINHTTP_NO_HEADER_INDEX)) {
                response.status_text = from_wide(text.c_str());
            }
        }
    }

    // The URL after any redirects, which is what link rewriting must resolve
    // relative references against.
    {
        DWORD size = 0;
        WinHttpQueryOption(req.get(), WINHTTP_OPTION_URL, nullptr, &size);
        if (size > 0) {
            std::wstring url_text(size / sizeof(wchar_t) + 1, L'\0');
            size = static_cast<DWORD>(url_text.size() * sizeof(wchar_t));
            if (WinHttpQueryOption(req.get(), WINHTTP_OPTION_URL, url_text.data(), &size)) {
                Url resolved;
                if (parse_url(from_wide(url_text.c_str()), resolved)) {
                    response.final_url = resolved;
                }
            }
        }
    }

    if (request.head_only) {
        response.elapsed_ms = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - started)
                .count());
        return response;
    }

    std::string body;
    std::vector<char> chunk(64 * 1024);
    while (true) {
        DWORD available = 0;
        if (!WinHttpQueryDataAvailable(req.get(), &available)) {
            return finish("download interrupted: " + format_win_error(GetLastError()));
        }
        if (available == 0) break;

        while (available > 0) {
            const DWORD want =
                (available < chunk.size()) ? available : static_cast<DWORD>(chunk.size());
            DWORD read = 0;
            if (!WinHttpReadData(req.get(), chunk.data(), want, &read)) {
                return finish("download interrupted: " + format_win_error(GetLastError()));
            }
            if (read == 0) {
                available = 0;
                break;
            }
            body.append(chunk.data(), read);
            available -= read;

            if (request.max_body_bytes != 0 && body.size() > request.max_body_bytes) {
                body.resize(static_cast<std::size_t>(request.max_body_bytes));
                response.truncated = true;
                response.body = std::move(body);
                response.elapsed_ms = static_cast<std::uint64_t>(
                    std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() - started)
                        .count());
                return response;
            }
        }
    }

    response.body = std::move(body);
    response.elapsed_ms = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - started)
            .count());
    return response;
}

} // namespace

std::unique_ptr<HttpClient> HttpClient::create(const HttpClientConfig& config,
                                               std::string& error) {
    auto client = std::make_unique<WinHttpClient>(config);
    if (!client->open(error)) return nullptr;
    return client;
}

const char* HttpClient::backend_name() { return "WinHTTP"; }

} // namespace surl
