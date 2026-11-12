// Paula's Revenge - ROM's samples (see RomBank.h)
#include "RomBank.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>

namespace prv
{
int RomBank::zonesOf (int tone) const
{
    int n = 0;
    for (const auto& z : zones) n += z.tone == tone ? 1 : 0;
    return n;
}

size_t RomBank::bytes() const
{
    size_t b = 0;
    for (const auto& z : zones) b += z.data.size() * 2;
    return b;
}

void romSpread (RomBank& bank, int tone)
{
    std::vector<RomZone*> zs;
    for (auto& z : bank.zones) if (z.tone == tone) zs.push_back (&z);
    std::stable_sort (zs.begin(), zs.end(), [] (const RomZone* a, const RomZone* b) { return a->root < b->root; });
    for (size_t k = 0; k < zs.size(); ++k)
    {
        zs[k]->lo = k == 0 ? 0 : (zs[k - 1]->root + zs[k]->root) / 2 + 1;
        zs[k]->hi = k + 1 == zs.size() ? 127 : (zs[k]->root + zs[k + 1]->root) / 2;
        if (zs[k]->hi < zs[k]->lo) zs[k]->hi = zs[k]->lo - 1;   // (two on one root: the first has the keys)
    }
}

const RomZone* romZoneFor (const RomBank& bank, int tone, int note)
{
    for (const auto& z : bank.zones) if (z.tone == tone && note >= z.lo && note <= z.hi && ! z.data.empty()) return &z;
    return nullptr;
}

namespace
{
    uint32_t u32 (const uint8_t* p) { return (uint32_t) p[0] | ((uint32_t) p[1] << 8) | ((uint32_t) p[2] << 16) | ((uint32_t) p[3] << 24); }
    uint16_t u16 (const uint8_t* p) { return (uint16_t) (p[0] | (p[1] << 8)); }

    // the chunks of a RIFF WAVE: calls f (id, body, size) for each
    template <typename F> bool chunks (const uint8_t* d, size_t n, F&& f)
    {
        if (n < 12 || std::memcmp (d, "RIFF", 4) != 0 || std::memcmp (d + 8, "WAVE", 4) != 0) return false;
        size_t i = 12;
        while (i + 8 <= n)
        {
            const uint32_t sz = u32 (d + i + 4);
            const size_t avail = std::min<size_t> (sz, n - (i + 8));
            f (d + i, d + i + 8, avail);
            i += 8 + (size_t) sz + (sz & 1);
        }
        return true;
    }
}

bool romReadSmpl (const uint8_t* d, size_t n, int& root, int& loopStart, int& loopEnd)
{
    bool found = false;
    root = 60; loopStart = loopEnd = -1;
    chunks (d, n, [&] (const uint8_t* id, const uint8_t* b, size_t sz)
    {
        if (std::memcmp (id, "smpl", 4) != 0 || sz < 36) return;
        found = true;
        root = (int) std::min<uint32_t> (u32 (b + 12), 127);
        const uint32_t loops = u32 (b + 28);
        if (loops > 0 && sz >= 36 + 24)
        {
            const uint8_t* l = b + 36;
            loopStart = (int) u32 (l + 8); loopEnd = (int) u32 (l + 12);
        }
    });
    return found;
}

bool romReadWav (const uint8_t* d, size_t n, RomZone& z, std::string& error)
{
    int fmt = 0, ch = 0, bits = 0; uint32_t rate = 0;
    const uint8_t* pcm = nullptr; size_t pcmBytes = 0;
    bool ok = chunks (d, n, [&] (const uint8_t* id, const uint8_t* b, size_t sz)
    {
        if (std::memcmp (id, "fmt ", 4) == 0 && sz >= 16)
        {
            fmt = u16 (b); ch = u16 (b + 2); rate = u32 (b + 4); bits = u16 (b + 14);
            if (fmt == 0xFFFE && sz >= 26) fmt = u16 (b + 24);   // (EXTENSIBLE: the sub-format's first two bytes)
        }
        else if (std::memcmp (id, "data", 4) == 0) { pcm = b; pcmBytes = sz; }
    });
    if (! ok) { error = "NOT A WAV FILE"; return false; }
    if (pcm == nullptr || ch <= 0 || rate == 0) { error = "NO SOUND IN IT"; return false; }
    if (! ((fmt == 1 && (bits == 8 || bits == 16 || bits == 24 || bits == 32)) || (fmt == 3 && bits == 32)))
    { error = "A KIND OF WAV ROM CAN'T READ (" + std::to_string (bits) + "-BIT, FORMAT " + std::to_string (fmt) + ")"; return false; }
    const int bps = bits / 8, frame = bps * ch;
    const size_t frames = pcmBytes / (size_t) frame;
    z.data.resize (frames);
    for (size_t f = 0; f < frames; ++f)
    {
        double sum = 0;
        for (int c = 0; c < ch; ++c)
        {
            const uint8_t* s = pcm + f * (size_t) frame + (size_t) (c * bps);
            double v = 0;
            if (fmt == 3) { float x; std::memcpy (&x, s, 4); v = x; }
            else if (bits == 8) v = (s[0] - 128) / 128.0;
            else if (bits == 16) v = (int16_t) u16 (s) / 32768.0;
            else if (bits == 24) v = (double) ((int32_t) ((uint32_t) s[0] << 8 | (uint32_t) s[1] << 16 | (uint32_t) s[2] << 24) >> 8) / 8388608.0;
            else v = (int32_t) u32 (s) / 2147483648.0;
            sum += v;
        }
        z.data[f] = (int16_t) std::clamp ((int) std::lround (sum / ch * 32768.0), -32768, 32767);
    }
    z.rate = rate;
    int root = 60, ls = -1, le = -1;
    if (romReadSmpl (d, n, root, ls, le))
    {
        z.root = root;
        chunks (d, n, [&] (const uint8_t* id, const uint8_t* b, size_t sz)
        {
            if (std::memcmp (id, "smpl", 4) == 0 && sz >= 20) z.rootCents = (float) (u32 (b + 16) / 4294967296.0 * 100.0);
        });
        if (ls >= 0 && le > ls && le < (int) frames) { z.loopStart = ls; z.loopEnd = le; }
    }
    if (z.data.empty()) { error = "NO SOUND IN IT"; return false; }
    return true;
}

std::shared_ptr<const RomBank> romWithTone (const std::shared_ptr<const RomBank>& old, int tone, std::vector<RomZone> zones, const std::string& name)
{
    auto b = std::make_shared<RomBank>();
    if (old != nullptr) *b = *old;
    tone = std::clamp (tone, 0, kRomTones - 1);
    b->zones.erase (std::remove_if (b->zones.begin(), b->zones.end(), [tone] (const RomZone& z) { return z.tone == tone; }), b->zones.end());
    for (auto& z : zones) { z.tone = tone; b->zones.push_back (std::move (z)); }
    b->toneName[(size_t) tone] = zones.empty() ? std::string() : name;
    romSpread (*b, tone);
    return b;
}

std::string romCategoryOf (const std::string& stem)
{
    const auto u = stem.find ('_');
    if (u == std::string::npos || u == 0 || u > 5) return "OTHER";
    std::string c = stem.substr (0, u);
    for (auto& x : c) x = (char) std::toupper ((unsigned char) x);
    return c;
}

std::string romGroupOf (const std::string& stem)
{
    std::string s = stem;
    for (auto& x : s) x = (char) std::toupper ((unsigned char) x);
    const auto u = s.find ('_');
    std::string cat, name = s;
    if (u != std::string::npos && u > 0 && u <= 5) { cat = s.substr (0, u); name = s.substr (u + 1); }
    // the Roland-style name: X + five letters + two for which sample ("XACORD0A")
    if (name.size() == 8 && name[0] == 'X') name = name.substr (1, 5);
    else
    {
        // otherwise: the end that numbers the samples (digits, note names, _ - spaces) taken off
        while (! name.empty() && (std::isdigit ((unsigned char) name.back()) || name.back() == '_' || name.back() == '-' || name.back() == ' ' || name.back() == '#'))
            name.pop_back();
        if (name.size() >= 2 && std::string ("ABCDEFG").find (name.back()) != std::string::npos
            && (name[name.size() - 2] == '_' || name[name.size() - 2] == ' ' || name[name.size() - 2] == '-'))
        { name.pop_back(); while (! name.empty() && (name.back() == '_' || name.back() == ' ' || name.back() == '-')) name.pop_back(); }
    }
    while (! name.empty() && name.back() == '_') name.pop_back();
    if (name.empty()) name = s;
    return cat.empty() ? name : cat + " " + name;
}
} // namespace prv
