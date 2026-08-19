#include "surl/net/http_client.hpp"

#include "surl/util/strings.hpp"

namespace surl {

std::string find_header(const HeaderList& headers, std::string_view name) {
    for (const auto& [key, value] : headers) {
        if (iequals(key, name)) return value;
    }
    return "";
}

HeaderList parse_raw_headers(std::string_view raw) {
    HeaderList out;
    std::size_t start = 0;
    bool first_line = true;

    while (start < raw.size()) {
        std::size_t end = raw.find('\n', start);
        std::string_view line =
            (end == std::string_view::npos) ? raw.substr(start) : raw.substr(start, end - start);
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);

        if (line.empty()) {
            // Blank line ends this header block; a following block belongs to a
            // redirect hop we do not care about.
            if (end == std::string_view::npos) break;
            start = end + 1;
            first_line = true;
            continue;
        }

        if (first_line && istarts_with(line, "HTTP/")) {
            first_line = false;
        } else {
            first_line = false;
            const std::size_t colon = line.find(':');
            if (colon != std::string_view::npos) {
                std::string key = trim(line.substr(0, colon));
                std::string value = trim(line.substr(colon + 1));
                if (!key.empty()) out.emplace_back(std::move(key), std::move(value));
            }
        }

        if (end == std::string_view::npos) break;
        start = end + 1;
    }
    return out;
}

} // namespace surl
