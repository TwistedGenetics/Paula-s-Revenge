// Minimal JSON value, parser and writer (non-real-time). Objects keep insertion order, so writing is
// deterministic (preset files diff cleanly and null tests can compare bytes).
#pragma once
#include <string>
#include <utility>
#include <vector>

namespace helix {

class Json
{
public:
    enum class Type { Null, Bool, Number, String, Array, Object };

    Json() = default;
    Json(bool b) : type_(Type::Bool), b_(b) {}
    Json(double n) : type_(Type::Number), n_(n) {}
    Json(int n) : type_(Type::Number), n_(n) {}
    Json(const char* s) : type_(Type::String), s_(s) {}
    Json(std::string s) : type_(Type::String), s_(std::move(s)) {}
    static Json array() { Json j; j.type_ = Type::Array; return j; }
    static Json object() { Json j; j.type_ = Type::Object; return j; }

    Type type() const { return type_; }
    bool isNull() const { return type_ == Type::Null; }
    bool isNumber() const { return type_ == Type::Number; }
    bool isString() const { return type_ == Type::String; }
    bool isArray() const { return type_ == Type::Array; }
    bool isObject() const { return type_ == Type::Object; }

    double number(double fallback = 0.0) const { return type_ == Type::Number ? n_ : fallback; }
    bool boolean(bool fallback = false) const { return type_ == Type::Bool ? b_ : fallback; }
    const std::string& string() const { return s_; }

    // Arrays
    size_t size() const { return type_ == Type::Array ? arr_.size() : (type_ == Type::Object ? obj_.size() : 0); }
    const Json& operator[](size_t i) const { static const Json nul; return type_ == Type::Array && i < arr_.size() ? arr_[i] : nul; }
    void push(Json v) { if (type_ != Type::Array) { *this = array(); } arr_.push_back(std::move(v)); }

    // Objects
    const Json& get(const std::string& key) const;
    bool has(const std::string& key) const;
    void set(const std::string& key, Json v);
    const std::vector<std::pair<std::string, Json>>& items() const { return obj_; }

    std::string dump(int indent = 2) const;
    static bool parse(const std::string& text, Json& out, std::string* error = nullptr);

private:
    void dumpTo(std::string& out, int indent, int depth) const;
    Type type_ = Type::Null;
    bool b_ = false;
    double n_ = 0.0;
    std::string s_;
    std::vector<Json> arr_;
    std::vector<std::pair<std::string, Json>> obj_;
};

} // namespace helix
