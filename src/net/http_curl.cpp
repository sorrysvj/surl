#include "surl/net/http_client.hpp"

#include "surl/util/strings.hpp"

#include <curl/curl.h>

#include <chrono>
#include <cstring>
#include <mutex>

namespace surl {
namespace {

struct WriteContext {
    std::string* body = nullptr;
    std::uint64_t limit = 0;
    bool truncated = false;
};

std::size_t write_body(char* data, std::size_t size, std::size_t nmemb, void* userdata) {
    auto* ctx = static_cast<WriteContext*>(userdata);
    const std::size_t total = size * nmemb;
    if (ctx->limit != 0 && ctx->body->size() + total > ctx->limit) {
        const std::size_t room = static_cast<std::size_t>(ctx->limit) - ctx->body->size();
        ctx->body->append(data, room);
        ctx->truncated = true;
        return 0; // aborts the transfer
    }
    ctx->body->append(data, total);
    return total;
}

std::size_t write_header(char* data, std::size_t size, std::size_t nmemb, void* userdata) {
    auto* raw = static_cast<std::string*>(userdata);
    raw->append(data, size * nmemb);
    return size * nmemb;
}

/// libcurl needs one-time global initialisation before any easy handle exists.
void ensure_global_init() {
    static std::once_flag once;
    std::call_once(once, [] { curl_global_init(CURL_GLOBAL_DEFAULT); });
}

class CurlClient final : public HttpClient {
public:
    explicit CurlClient(const HttpClientConfig& config) : config_(config) {}

    HttpResponse send(const HttpRequest& request) override;

private:
    HttpClientConfig config_;
};

HttpResponse CurlClient::send(const HttpRequest& request) {
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

    // One easy handle per request keeps the client trivially thread-safe.
    CURL* curl = curl_easy_init();
    if (curl == nullptr) return finish("could not initialise libcurl");

    const std::string url_text = request.url.to_string_no_fragment();
    std::string raw_headers;
    WriteContext write_ctx;
    write_ctx.body = &response.body;
    write_ctx.limit = request.max_body_bytes;

    curl_easy_setopt(curl, CURLOPT_URL, url_text.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_body);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &write_ctx);
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, write_header);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, &raw_headers);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, static_cast<long>(request.timeout_ms));
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS,
                     static_cast<long>(config_.connect_timeout_ms));

    if (!config_.user_agent.empty()) {
        curl_easy_setopt(curl, CURLOPT_USERAGENT, config_.user_agent.c_str());
    }
    if (!config_.proxy.empty()) {
        curl_easy_setopt(curl, CURLOPT_PROXY, config_.proxy.c_str());
    }
    if (config_.insecure) {
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    }
    if (request.follow_redirects) {
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_MAXREDIRS, static_cast<long>(request.max_redirects));
        // Never silently downgrade an https origin to plaintext.
        curl_easy_setopt(curl, CURLOPT_REDIR_PROTOCOLS_STR, "http,https");
    }
    curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, "http,https");

    if (request.head_only) {
        curl_easy_setopt(curl, CURLOPT_NOBODY, 1L);
    } else if (request.method != "GET") {
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, request.method.c_str());
    }
    if (!request.body.empty()) {
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, request.body.data());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE,
                         static_cast<long>(request.body.size()));
    }

    curl_slist* header_list = nullptr;
    for (const auto& [name, value] : request.headers) {
        const std::string line = name + ": " + value;
        header_list = curl_slist_append(header_list, line.c_str());
    }
    if (header_list != nullptr) {
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, header_list);
    }

    const CURLcode code = curl_easy_perform(curl);

    if (code != CURLE_OK && !(code == CURLE_WRITE_ERROR && write_ctx.truncated)) {
        const std::string message = curl_easy_strerror(code);
        if (header_list != nullptr) curl_slist_free_all(header_list);
        curl_easy_cleanup(curl);
        return finish("request to " + url_text + " failed: " + message);
    }

    response.truncated = write_ctx.truncated;
    response.headers = parse_raw_headers(raw_headers);

    long status = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    response.status = static_cast<int>(status);

    char* effective = nullptr;
    if (curl_easy_getinfo(curl, CURLINFO_EFFECTIVE_URL, &effective) == CURLE_OK &&
        effective != nullptr) {
        Url resolved;
        if (parse_url(effective, resolved)) response.final_url = resolved;
    }

    if (header_list != nullptr) curl_slist_free_all(header_list);
    curl_easy_cleanup(curl);

    response.elapsed_ms = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - started)
            .count());
    return response;
}

} // namespace

std::unique_ptr<HttpClient> HttpClient::create(const HttpClientConfig& config,
                                               std::string& error) {
    ensure_global_init();
    (void)error;
    return std::make_unique<CurlClient>(config);
}

const char* HttpClient::backend_name() { return "libcurl"; }

} // namespace surl
