#include "surl/util/json.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace surl {
namespace {

const Json& null_json() {
    static const Json kNull;
    return kNull;
}

const std::string& empty_string() {
    static const std::string kEmpty;
    return kEmpty;
}

void append_indent(std::string& out, int indent, int depth) {
    if (indent < 0) return;
    out.push_back('\n');
    out.append(static_cast<std::size_t>(indent) * static_cast<std::size_t>(depth), ' ');
}

/// Encodes a Unicode code point as UTF-8.
void append_utf8(std::string& out, std::uint32_t cp) {
    if (cp <= 0x7F) {
        out.push_back(static_cast<char>(cp));
    } else if (cp <= 0x7FF) {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp <= 0xFFFF) {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}

class Parser {
public:
    Parser(std::string_view text) : text_(text) {}

    bool parse(Json& out, std::string& error) {
        skip_ws();
        if (!parse_value(out, 0)) {
            error = error_;
            return false;
        }
        skip_ws();
        if (pos_ != text_.size()) {
            error = "trailing characters after JSON value";
            return false;
        }
        return true;
    }

private:
    static constexpr int kMaxDepth = 100;

    bool fail(const char* message) {
        if (error_.empty()) {
            char buf[160];
            std::snprintf(buf, sizeof(buf), "%s at offset %zu", message, pos_);
            error_ = buf;
        }
        return false;
    }

    void skip_ws() {
        while (pos_ < text_.size()) {
            const char c = text_[pos_];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
                ++pos_;
            } else {
                break;
            }
        }
    }

    bool literal(std::string_view word) {
        if (text_.compare(pos_, word.size(), word) != 0) return false;
        pos_ += word.size();
        return true;
    }

    bool parse_value(Json& out, int depth) {
        if (depth > kMaxDepth) return fail("nesting too deep");
        if (pos_ >= text_.size()) return fail("unexpected end of input");

        switch (text_[pos_]) {
        case 'n':
            if (!literal("null")) return fail("invalid literal");
            out = Json();
            return true;
        case 't':
            if (!literal("true")) return fail("invalid literal");
            out = Json(true);
            return true;
        case 'f':
            if (!literal("false")) return fail("invalid literal");
            out = Json(false);
            return true;
        case '"': {
            std::string s;
            if (!parse_string(s)) return false;
            out = Json(std::move(s));
            return true;
        }
        case '[':
            return parse_array(out, depth);
        case '{':
            return parse_object(out, depth);
        default:
            return parse_number(out);
        }
    }

    bool parse_string(std::string& out) {
        if (pos_ >= text_.size() || text_[pos_] != '"') return fail("expected string");
        ++pos_;
        out.clear();
        while (true) {
            if (pos_ >= text_.size()) return fail("unterminated string");
            const char c = text_[pos_];
            if (c == '"') {
                ++pos_;
                return true;
            }
            if (c == '\\') {
                ++pos_;
                if (pos_ >= text_.size()) return fail("unterminated escape");
                const char esc = text_[pos_++];
                switch (esc) {
                case '"': out.push_back('"'); break;
                case '\\': out.push_back('\\'); break;
                case '/': out.push_back('/'); break;
                case 'b': out.push_back('\b'); break;
                case 'f': out.push_back('\f'); break;
                case 'n': out.push_back('\n'); break;
                case 'r': out.push_back('\r'); break;
                case 't': out.push_back('\t'); break;
                case 'u': {
                    std::uint32_t cp = 0;
                    if (!parse_hex4(cp)) return false;
                    // Combine UTF-16 surrogate pairs into one code point.
                    if (cp >= 0xD800 && cp <= 0xDBFF && pos_ + 1 < text_.size() &&
                        text_[pos_] == '\\' && text_[pos_ + 1] == 'u') {
                        const std::size_t save = pos_;
                        pos_ += 2;
                        std::uint32_t low = 0;
                        if (parse_hex4(low) && low >= 0xDC00 && low <= 0xDFFF) {
                            cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                        } else {
                            pos_ = save;
                        }
                    }
                    append_utf8(out, cp);
                    break;
                }
                default:
                    return fail("invalid escape sequence");
                }
                continue;
            }
            out.push_back(c);
            ++pos_;
        }
    }

    bool parse_hex4(std::uint32_t& out) {
        if (pos_ + 4 > text_.size()) return fail("truncated \\u escape");
        std::uint32_t value = 0;
        for (int i = 0; i < 4; ++i) {
            const char c = text_[pos_ + static_cast<std::size_t>(i)];
            value <<= 4;
            if (c >= '0' && c <= '9') value |= static_cast<std::uint32_t>(c - '0');
            else if (c >= 'a' && c <= 'f') value |= static_cast<std::uint32_t>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') value |= static_cast<std::uint32_t>(c - 'A' + 10);
            else return fail("invalid hex digit");
        }
        pos_ += 4;
        out = value;
        return true;
    }

    bool parse_number(Json& out) {
        const std::size_t start = pos_;
        if (pos_ < text_.size() && (text_[pos_] == '-' || text_[pos_] == '+')) ++pos_;
        bool any_digit = false;
        while (pos_ < text_.size() && text_[pos_] >= '0' && text_[pos_] <= '9') {
            ++pos_;
            any_digit = true;
        }
        if (pos_ < text_.size() && text_[pos_] == '.') {
            ++pos_;
            while (pos_ < text_.size() && text_[pos_] >= '0' && text_[pos_] <= '9') {
                ++pos_;
                any_digit = true;
            }
        }
        if (!any_digit) return fail("invalid number");
        if (pos_ < text_.size() && (text_[pos_] == 'e' || text_[pos_] == 'E')) {
            ++pos_;
            if (pos_ < text_.size() && (text_[pos_] == '-' || text_[pos_] == '+')) ++pos_;
            while (pos_ < text_.size() && text_[pos_] >= '0' && text_[pos_] <= '9') ++pos_;
        }
        const std::string chunk(text_.substr(start, pos_ - start));
        out = Json(std::strtod(chunk.c_str(), nullptr));
        return true;
    }

    bool parse_array(Json& out, int depth) {
        ++pos_; // '['
        out = Json::array();
        skip_ws();
        if (pos_ < text_.size() && text_[pos_] == ']') {
            ++pos_;
            return true;
        }
        while (true) {
            skip_ws();
            Json element;
            if (!parse_value(element, depth + 1)) return false;
            out.push_back(std::move(element));
            skip_ws();
            if (pos_ >= text_.size()) return fail("unterminated array");
            if (text_[pos_] == ',') {
                ++pos_;
                continue;
            }
            if (text_[pos_] == ']') {
                ++pos_;
                return true;
            }
            return fail("expected ',' or ']'");
        }
    }

    bool parse_object(Json& out, int depth) {
        ++pos_; // '{'
        out = Json::object();
        skip_ws();
        if (pos_ < text_.size() && text_[pos_] == '}') {
            ++pos_;
            return true;
        }
        while (true) {
            skip_ws();
            std::string key;
            if (!parse_string(key)) return false;
            skip_ws();
            if (pos_ >= text_.size() || text_[pos_] != ':') return fail("expected ':'");
            ++pos_;
            skip_ws();
            Json value;
            if (!parse_value(value, depth + 1)) return false;
            out.set(std::move(key), std::move(value));
            skip_ws();
            if (pos_ >= text_.size()) return fail("unterminated object");
            if (text_[pos_] == ',') {
                ++pos_;
                continue;
            }
            if (text_[pos_] == '}') {
                ++pos_;
                return true;
            }
            return fail("expected ',' or '}'");
        }
    }

    std::string_view text_;
    std::size_t pos_ = 0;
    std::string error_;
};

} // namespace

const std::string& Json::as_string() const {
    return type_ == Type::String ? string_ : empty_string();
}

void Json::push_back(Json value) {
    if (type_ != Type::Array) {
        type_ = Type::Array;
        array_.clear();
    }
    array_.push_back(std::move(value));
}

std::size_t Json::size() const {
    if (type_ == Type::Array) return array_.size();
    if (type_ == Type::Object) return object_.size();
    return 0;
}

const Json& Json::operator[](std::size_t index) const {
    if (type_ != Type::Array || index >= array_.size()) return null_json();
    return array_[index];
}

void Json::set(std::string key, Json value) {
    if (type_ != Type::Object) {
        type_ = Type::Object;
        object_.clear();
    }
    object_[std::move(key)] = std::move(value);
}

bool Json::contains(std::string_view key) const {
    if (type_ != Type::Object) return false;
    return object_.find(std::string(key)) != object_.end();
}

const Json& Json::operator[](std::string_view key) const {
    if (type_ != Type::Object) return null_json();
    const auto it = object_.find(std::string(key));
    return it == object_.end() ? null_json() : it->second;
}

std::string json_escape(std::string_view s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (const unsigned char c : s) {
        switch (c) {
        case '"': out.append("\\\""); break;
        case '\\': out.append("\\\\"); break;
        case '\b': out.append("\\b"); break;
        case '\f': out.append("\\f"); break;
        case '\n': out.append("\\n"); break;
        case '\r': out.append("\\r"); break;
        case '\t': out.append("\\t"); break;
        default:
            if (c < 0x20) {
                char buf[8];
                std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                out.append(buf);
            } else {
                out.push_back(static_cast<char>(c));
            }
        }
    }
    return out;
}

void Json::dump_to(std::string& out, int indent, int depth) const {
    switch (type_) {
    case Type::Null:
        out.append("null");
        break;
    case Type::Bool:
        out.append(bool_ ? "true" : "false");
        break;
    case Type::Number: {
        if (std::isfinite(number_)) {
            char buf[40];
            // Integral values are written without a decimal point so that byte
            // counts and timestamps round-trip cleanly.
            if (number_ == static_cast<double>(static_cast<std::int64_t>(number_))) {
                std::snprintf(buf, sizeof(buf), "%lld",
                              static_cast<long long>(number_));
            } else {
                std::snprintf(buf, sizeof(buf), "%.17g", number_);
            }
            out.append(buf);
        } else {
            out.append("null");
        }
        break;
    }
    case Type::String:
        out.push_back('"');
        out.append(json_escape(string_));
        out.push_back('"');
        break;
    case Type::Array: {
        if (array_.empty()) {
            out.append("[]");
            break;
        }
        out.push_back('[');
        bool first = true;
        for (const Json& element : array_) {
            if (!first) out.push_back(',');
            first = false;
            append_indent(out, indent, depth + 1);
            element.dump_to(out, indent, depth + 1);
        }
        append_indent(out, indent, depth);
        out.push_back(']');
        break;
    }
    case Type::Object: {
        if (object_.empty()) {
            out.append("{}");
            break;
        }
        out.push_back('{');
        bool first = true;
        for (const auto& [key, value] : object_) {
            if (!first) out.push_back(',');
            first = false;
            append_indent(out, indent, depth + 1);
            out.push_back('"');
            out.append(json_escape(key));
            out.append("\":");
            if (indent >= 0) out.push_back(' ');
            value.dump_to(out, indent, depth + 1);
        }
        append_indent(out, indent, depth);
        out.push_back('}');
        break;
    }
    }
}

std::string Json::dump(int indent) const {
    std::string out;
    dump_to(out, indent, 0);
    return out;
}

bool Json::parse(std::string_view text, Json& out, std::string& error) {
    // Tolerate a UTF-8 BOM, which Windows editors add to config files.
    if (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEF &&
        static_cast<unsigned char>(text[1]) == 0xBB &&
        static_cast<unsigned char>(text[2]) == 0xBF) {
        text.remove_prefix(3);
    }
    Parser parser(text);
    return parser.parse(out, error);
}

} // namespace surl
