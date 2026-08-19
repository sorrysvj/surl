#pragma once

#include <atomic>
#include <map>
#include <mutex>
#include <string>
#include <thread>

namespace surltest {

/// A canned HTTP response served by the fixture.
struct FixtureResource {
    std::string body;
    std::string content_type = "text/html; charset=utf-8";
    int status = 200;
    std::string etag;      ///< when set, If-None-Match yields 304
    std::string location;  ///< when set, sent as a Location header
};

/// A tiny single-purpose HTTP/1.1 server used by the integration tests.
///
/// It binds 127.0.0.1 on an ephemeral port so tests never collide, records how
/// many times each path was requested, and understands just enough of the
/// protocol to exercise SURL's downloader: status codes, ETags, conditional
/// requests and redirects.
class FixtureServer {
public:
    FixtureServer();
    ~FixtureServer();

    FixtureServer(const FixtureServer&) = delete;
    FixtureServer& operator=(const FixtureServer&) = delete;

    /// Starts listening. Returns false when a socket could not be opened.
    bool start();
    void stop();

    /// Registers a resource at an absolute path such as "/index.html".
    void add(const std::string& path, FixtureResource resource);
    void add_html(const std::string& path, std::string body);

    /// "http://127.0.0.1:<port>"
    std::string base_url() const;
    unsigned short port() const { return port_; }

    /// How many times a path has been requested since the server started.
    int hits(const std::string& path) const;
    void reset_hits();

    /// Replaces a resource's body and bumps its ETag, simulating a site that
    /// changed between two runs.
    void update_body(const std::string& path, std::string body, std::string etag);

private:
    void serve();
    void handle(long long client);

    std::map<std::string, FixtureResource> resources_;
    mutable std::mutex mutex_;
    std::map<std::string, int> hits_;

    long long listener_ = -1;
    unsigned short port_ = 0;
    std::atomic<bool> running_{false};
    std::thread thread_;
};

} // namespace surltest
