#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace surl {

/// A small dependency-free JSON value. SURL uses JSON for its cache manifest,
/// its config file, `--json` output and for reading the GitHub releases API,
/// so the implementation needs to both parse and serialise, but it does not
/// need to be fast: manifests are small and read once per run.
class Json {
public:
    enum class Type { Null, Bool, Number, String, Array, Object };

    Json() = default;
    Json(std::nullptr_t) {}
    Json(bool v) : type_(Type::Bool), bool_(v) {}
    Json(double v) : type_(Type::Number), number_(v) {}
    Json(std::int64_t v) : type_(Type::Number), number_(static_cast<double>(v)) {}
    Json(std::uint64_t v) : type_(Type::Number), number_(static_cast<double>(v)) {}
    Json(int v) : type_(Type::Number), number_(static_cast<double>(v)) {}
    Json(std::string v) : type_(Type::String), string_(std::move(v)) {}
    Json(const char* v) : type_(Type::String), string_(v ? v : "") {}

    static Json array() {
        Json j;
        j.type_ = Type::Array;
        return j;
    }
    static Json object() {
        Json j;
        j.type_ = Type::Object;
        return j;
    }

    Type type() const { return type_; }
    bool is_null() const { return type_ == Type::Null; }
    bool is_bool() const { return type_ == Type::Bool; }
    bool is_number() const { return type_ == Type::Number; }
    bool is_string() const { return type_ == Type::String; }
    bool is_array() const { return type_ == Type::Array; }
    bool is_object() const { return type_ == Type::Object; }

    bool as_bool(bool fallback = false) const {
        return type_ == Type::Bool ? bool_ : fallback;
    }
    double as_double(double fallback = 0.0) const {
        return type_ == Type::Number ? number_ : fallback;
    }
    std::int64_t as_int(std::int64_t fallback = 0) const {
        return type_ == Type::Number ? static_cast<std::int64_t>(number_) : fallback;
    }
    std::uint64_t as_uint(std::uint64_t fallback = 0) const {
        if (type_ != Type::Number || number_ < 0) return fallback;
        return static_cast<std::uint64_t>(number_);
    }
    const std::string& as_string() const;
    std::string as_string_or(std::string_view fallback) const {
        return type_ == Type::String ? string_ : std::string(fallback);
    }

    /// Array access. push_back promotes a null value to an array.
    void push_back(Json value);
    std::size_t size() const;
    const Json& operator[](std::size_t index) const;
    const std::vector<Json>& items() const { return array_; }

    /// Object access. set() promotes a null value to an object.
    void set(std::string key, Json value);
    bool contains(std::string_view key) const;
    const Json& operator[](std::string_view key) const;
    const std::map<std::string, Json>& fields() const { return object_; }

    /// Serialises to text. indent < 0 produces compact output.
    std::string dump(int indent = -1) const;

    /// Parses JSON text. On failure returns false and fills @p error.
    static bool parse(std::string_view text, Json& out, std::string& error);

private:
    void dump_to(std::string& out, int indent, int depth) const;

    Type type_ = Type::Null;
    bool bool_ = false;
    double number_ = 0.0;
    std::string string_;
    std::vector<Json> array_;
    std::map<std::string, Json> object_;
};

/// Escapes a string for embedding in JSON output (without the quotes).
std::string json_escape(std::string_view s);

} // namespace surl
