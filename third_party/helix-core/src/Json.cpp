#include "helix/Json.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace helix {

const Json& Json::get(const std::string& key) const
{
    static const Json nul;
    if (type_ != Type::Object) return nul;
    for (const auto& kv : obj_) if (kv.first == key) return kv.second;
    return nul;
}
bool Json::has(const std::string& key) const
{
    if (type_ != Type::Object) return false;
    for (const auto& kv : obj_) if (kv.first == key) return true;
    return false;
}
void Json::set(const std::string& key, Json v)
{
    if (type_ != Type::Object) *this = object();
    for (auto& kv : obj_) if (kv.first == key) { kv.second = std::move(v); return; }
    obj_.emplace_back(key, std::move(v));
}

static void escape(std::string& out, const std::string& s)
{
    out += '"';
    for (unsigned char c : s)
    {
        switch (c)
        {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) { char b[8]; std::snprintf(b, sizeof b, "\\u%04x", c); out += b; }
                else out += (char)c;
        }
    }
    out += '"';
}

std::string Json::dump(int indent) const { std::string s; dumpTo(s, indent, 0); s += '\n'; return s; }

void Json::dumpTo(std::string& out, int indent, int depth) const
{
    auto nl = [&](int d) { if (indent > 0) { out += '\n'; out.append((size_t)(indent * d), ' '); } };
    switch (type_)
    {
        case Type::Null: out += "null"; break;
        case Type::Bool: out += b_ ? "true" : "false"; break;
        case Type::Number:
        {
            if (!std::isfinite(n_)) { out += "0"; break; }
            char b[40];
            // %.9g round-trips a float exactly; integers print without a fraction.
            if (n_ == std::floor(n_) && std::fabs(n_) < 1e15) std::snprintf(b, sizeof b, "%.0f", n_);
            else std::snprintf(b, sizeof b, "%.9g", n_);
            out += b;
            break;
        }
        case Type::String: escape(out, s_); break;
        case Type::Array:
        {
            // Short arrays of numbers stay on one line.
            bool flat = true;
            for (const Json& v : arr_) if (v.type_ == Type::Array || v.type_ == Type::Object) { flat = false; break; }
            out += '[';
            for (size_t i = 0; i < arr_.size(); ++i)
            {
                if (i) out += flat ? ", " : ",";
                if (!flat) nl(depth + 1);
                arr_[i].dumpTo(out, indent, depth + 1);
            }
            if (!flat && !arr_.empty()) nl(depth);
            out += ']';
            break;
        }
        case Type::Object:
        {
            out += '{';
            for (size_t i = 0; i < obj_.size(); ++i)
            {
                if (i) out += ',';
                nl(depth + 1);
                escape(out, obj_[i].first);
                out += indent > 0 ? ": " : ":";
                obj_[i].second.dumpTo(out, indent, depth + 1);
            }
            if (!obj_.empty()) nl(depth);
            out += '}';
            break;
        }
    }
}

namespace {
struct Parser
{
    explicit Parser(const std::string& text) : s(text) {}
    const std::string& s;
    size_t i = 0;
    std::string err;
    int depth = 0;
    void ws() { while (i < s.size() && (s[i] == ' ' || s[i] == '\n' || s[i] == '\r' || s[i] == '\t')) ++i; }
    bool fail(const char* m) { if (err.empty()) err = std::string(m) + " at " + std::to_string(i); return false; }
    bool lit(const char* w) { size_t n = 0; while (w[n]) ++n; if (s.compare(i, n, w) != 0) return fail("bad literal"); i += n; return true; }
    bool str(std::string& out)
    {
        if (i >= s.size() || s[i] != '"') return fail("expected string");
        ++i;
        while (i < s.size() && s[i] != '"')
        {
            char c = s[i++];
            if (c == '\\')
            {
                if (i >= s.size()) return fail("bad escape");
                char e = s[i++];
                switch (e)
                {
                    case 'n': out += '\n'; break; case 't': out += '\t'; break; case 'r': out += '\r'; break;
                    case 'b': out += '\b'; break; case 'f': out += '\f'; break;
                    case 'u':
                    {
                        if (i + 4 > s.size()) return fail("bad \\u");
                        const unsigned v = (unsigned)std::strtoul(s.substr(i, 4).c_str(), nullptr, 16);
                        i += 4;
                        if (v < 0x80) out += (char)v;
                        else if (v < 0x800) { out += (char)(0xC0 | (v >> 6)); out += (char)(0x80 | (v & 0x3F)); }
                        else { out += (char)(0xE0 | (v >> 12)); out += (char)(0x80 | ((v >> 6) & 0x3F)); out += (char)(0x80 | (v & 0x3F)); }
                        break;
                    }
                    default: out += e;
                }
            }
            else out += c;
        }
        if (i >= s.size()) return fail("unterminated string");
        ++i;
        return true;
    }
    bool value(Json& out)
    {
        if (++depth > 64) return fail("too deep");
        ws();
        if (i >= s.size()) return fail("unexpected end");
        const char c = s[i];
        bool ok = true;
        if (c == '{')
        {
            ++i; out = Json::object(); ws();
            if (i < s.size() && s[i] == '}') { ++i; --depth; return true; }
            for (;;)
            {
                ws(); std::string k;
                if (!str(k)) return false;
                ws(); if (i >= s.size() || s[i] != ':') return fail("expected :");
                ++i; Json v;
                if (!value(v)) return false;
                out.set(k, std::move(v));
                ws();
                if (i < s.size() && s[i] == ',') { ++i; continue; }
                if (i < s.size() && s[i] == '}') { ++i; break; }
                return fail("expected , or }");
            }
        }
        else if (c == '[')
        {
            ++i; out = Json::array(); ws();
            if (i < s.size() && s[i] == ']') { ++i; --depth; return true; }
            for (;;)
            {
                Json v;
                if (!value(v)) return false;
                out.push(std::move(v));
                ws();
                if (i < s.size() && s[i] == ',') { ++i; continue; }
                if (i < s.size() && s[i] == ']') { ++i; break; }
                return fail("expected , or ]");
            }
        }
        else if (c == '"') { std::string t; ok = str(t); out = Json(t); }
        else if (c == 't') { ok = lit("true"); out = Json(true); }
        else if (c == 'f') { ok = lit("false"); out = Json(false); }
        else if (c == 'n') { ok = lit("null"); out = Json(); }
        else
        {
            const char* b = s.c_str() + i;
            char* e = nullptr;
            const double v = std::strtod(b, &e);
            if (e == b) return fail("bad value");
            i += (size_t)(e - b);
            out = Json(v);
        }
        --depth;
        return ok;
    }
};
} // namespace

bool Json::parse(const std::string& text, Json& out, std::string* error)
{
    Parser p(text);
    Json v;
    if (!p.value(v)) { if (error) *error = p.err; return false; }
    p.ws();
    if (p.i != text.size()) { if (error) *error = "trailing characters"; return false; }
    out = std::move(v);
    return true;
}

} // namespace helix
