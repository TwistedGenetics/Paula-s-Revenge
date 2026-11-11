// Paula's Revenge - Akai S1000 System Exclusive (see AkaiSysex.h)
#include "AkaiSysex.h"
#include <algorithm>
#include <cstdio>

namespace prv::akai
{
namespace
{
    constexpr uint8_t kAkai = 0x47, kS1000 = 0x48;
    const char* kChars = "0123456789 ABCDEFGHIJKLMNOPQRSTUVWXYZ#+-.";   // (codes 0 - 40)

    Field f (const char* g, const char* n, int off, int lo, int hi, Kind k = Kind::Number, std::vector<std::string> labels = {})
    {
        Field x; x.group = g; x.name = n; x.offset = off; x.lo = lo; x.hi = hi; x.kind = k; x.labels = std::move (labels); return x;
    }
    void header (std::vector<uint8_t>& m, int cc, Op op) { m = { 0xF0, kAkai, (uint8_t) (cc & 127), (uint8_t) op, kS1000 }; }
    void number14 (std::vector<uint8_t>& m, int v) { m.push_back ((uint8_t) (v & 127)); m.push_back ((uint8_t) ((v >> 7) & 127)); }
    void nibbles (std::vector<uint8_t>& m, const std::vector<uint8_t>& block) { for (uint8_t b : block) { m.push_back (b & 15); m.push_back (b >> 4); } }
}

std::string Field::text (int v) const
{
    char b[24];
    switch (kind)
    {
        case Kind::Note:
        {
            static const char* nn[] = { "C-", "C#", "D-", "D#", "E-", "F-", "F#", "G-", "G#", "A-", "A#", "B-" };
            std::snprintf (b, sizeof b, "%s%d", nn[((v % 12) + 12) % 12], v / 12 - 2); return b;
        }
        case Kind::OnOff:  return ! labels.empty() && v >= 0 && v < (int) labels.size() ? labels[(size_t) v] : std::string (v ? "ON" : "OFF");
        case Kind::Signed: std::snprintf (b, sizeof b, "%+d", v); return b;
        case Kind::Fine:   std::snprintf (b, sizeof b, "%d", v * 100 / 256); return b;
        case Kind::Number:
        default:
            if (! labels.empty() && v >= lo && v - lo < (int) labels.size()) return labels[(size_t) (v - lo)];
            std::snprintf (b, sizeof b, "%d", v); return b;
    }
}

const std::vector<Field>& programFields()
{
    static const std::vector<Field> all {
        f ("PROGRAM", "LOUDNESS", 25, 0, 99), f ("PROGRAM", "POLYPHONY", 17, 1, 16), f ("PROGRAM", "PRIORITY", 18, 0, 3, Kind::Number, { "LOW", "NORM", "HIGH", "HOLD" }),
        f ("PROGRAM", "STEREO LEVEL", 23, 0, 99), f ("PROGRAM", "PAN", 24, -50, 50, Kind::Signed), f ("PROGRAM", "OUTPUT", 22, 0, 7),
        f ("RANGE", "PLAY LOW", 19, 24, 127, Kind::Note), f ("RANGE", "PLAY HIGH", 20, 24, 127, Kind::Note), f ("RANGE", "OCTAVE SHIFT", 21, -2, 2, Kind::Signed),
        f ("RANGE", "TUNE", 66, -50, 50, Kind::Signed), f ("RANGE", "FINE", 65, 0, 255, Kind::Fine),
        f ("MODULATION", "VEL > LOUD", 26, -50, 50, Kind::Signed), f ("MODULATION", "KEY > LOUD", 27, -50, 50, Kind::Signed), f ("MODULATION", "PRESS > LOUD", 28, -50, 50, Kind::Signed),
        f ("MODULATION", "KEY > PAN", 32, -50, 50, Kind::Signed), f ("MODULATION", "BEND RANGE", 39, 0, 12), f ("MODULATION", "PRESS > PITCH", 40, -12, 12, Kind::Signed),
        f ("LFO", "RATE", 33, 0, 99), f ("LFO", "DEPTH", 34, 0, 99), f ("LFO", "DELAY", 35, 0, 99), f ("LFO", "MODWHEEL > DEPTH", 36, 0, 99),
        f ("LFO", "PRESS > DEPTH", 37, 0, 99), f ("LFO", "VEL > DEPTH", 38, 0, 99),
        f ("LFO KEYS", "KEY > RATE", 67, -50, 50, Kind::Signed), f ("LFO KEYS", "KEY > DEPTH", 68, -50, 50, Kind::Signed), f ("LFO KEYS", "KEY > DELAY", 69, -50, 50, Kind::Signed),
        f ("LFO KEYS", "DESYNC", 59, 0, 1, Kind::OnOff),
        f ("PAN LFO", "RATE", 29, 0, 99), f ("PAN LFO", "DEPTH", 30, 0, 99), f ("PAN LFO", "DELAY", 31, 0, 99), f ("PAN LFO", "MODWHEEL > PAN", 57, -50, 50, Kind::Signed),
        f ("VOICES", "KEY CROSSFADE", 41, 0, 1, Kind::OnOff), f ("VOICES", "SAMPLE COHERE", 58, 0, 1, Kind::OnOff), f ("VOICES", "REASSIGN", 61, 0, 1, Kind::OnOff, { "OLDEST", "QUIETEST" }),
        f ("VOICES", "SOFT PEDAL LOUD", 62, 0, 99), f ("VOICES", "SOFT PEDAL ATTACK", 63, 0, 99), f ("VOICES", "SOFT PEDAL FILTER", 64, 0, 99),
    };
    return all;
}

const std::vector<Field>& keygroupFields()
{
    static const std::vector<Field> all = []
    {
        std::vector<Field> v {
            f ("KEYS", "LOW NOTE", 3, 24, 127, Kind::Note), f ("KEYS", "HIGH NOTE", 4, 24, 127, Kind::Note),
            f ("KEYS", "TUNE", 6, -50, 50, Kind::Signed), f ("KEYS", "FINE", 5, 0, 255, Kind::Fine),
            f ("KEYS", "VEL CROSSFADE", 30, 0, 1, Kind::OnOff), f ("KEYS", "ATTACK HOLD", 131, 0, 1, Kind::OnOff),
            f ("FILTER", "CUTOFF", 7, 0, 99), f ("FILTER", "KEY > FREQ", 8, -24, 24, Kind::Signed), f ("FILTER", "VEL > FREQ", 9, -50, 50, Kind::Signed),
            f ("FILTER", "PRESS > FREQ", 10, -50, 50, Kind::Signed), f ("FILTER", "ENV > FREQ", 11, -50, 50, Kind::Signed),
            f ("AMP ENV", "ATTACK", 12, 0, 99), f ("AMP ENV", "DECAY", 13, 0, 99), f ("AMP ENV", "SUSTAIN", 14, 0, 99), f ("AMP ENV", "RELEASE", 15, 0, 99),
            f ("AMP ENV MOD", "VEL > ATTACK", 16, -50, 50, Kind::Signed), f ("AMP ENV MOD", "VEL > RELEASE", 17, -50, 50, Kind::Signed),
            f ("AMP ENV MOD", "OFF VEL > RELEASE", 18, -50, 50, Kind::Signed), f ("AMP ENV MOD", "KEY > DEC + REL", 19, -50, 50, Kind::Signed),
            f ("FILTER ENV", "ATTACK", 20, 0, 99), f ("FILTER ENV", "DECAY", 21, 0, 99), f ("FILTER ENV", "SUSTAIN", 22, 0, 99), f ("FILTER ENV", "RELEASE", 23, 0, 99),
            f ("FILTER ENV", "ENV > PITCH", 29, -50, 50, Kind::Signed),
            f ("FILTER ENV MOD", "VEL > ATTACK", 24, -50, 50, Kind::Signed), f ("FILTER ENV MOD", "VEL > RELEASE", 25, -50, 50, Kind::Signed),
            f ("FILTER ENV MOD", "OFF VEL > RELEASE", 26, -50, 50, Kind::Signed), f ("FILTER ENV MOD", "KEY > DEC + REL", 27, -50, 50, Kind::Signed),
            f ("FILTER ENV MOD", "VEL > AMOUNT", 28, -50, 50, Kind::Signed),
        };
        static const char* zone[] = { "ZONE 1", "ZONE 2", "ZONE 3", "ZONE 4" };
        for (int z = 0; z < 4; ++z)
        {
            const int b = 34 + 24 * z;
            v.push_back (f (zone[z], "VEL LOW", b + 12, 0, 127));
            v.push_back (f (zone[z], "VEL HIGH", b + 13, 0, 127));
            v.push_back (f (zone[z], "TUNE", b + 15, -50, 50, Kind::Signed));
            v.push_back (f (zone[z], "LOUDNESS", b + 16, -50, 50, Kind::Signed));
            v.push_back (f (zone[z], "FILTER", b + 17, -50, 50, Kind::Signed));
            v.push_back (f (zone[z], "PAN", b + 18, -50, 50, Kind::Signed));
            v.push_back (f (zone[z], "PLAY", b + 19, 0, 4, Kind::Number, { "AS SAMPLE", "LOOP IN REL", "LOOP TIL REL", "NO LOOP", "TO END" }));
        }
        return v;
    }();
    return all;
}

std::vector<std::string> groupsOf (const std::vector<Field>& fs)
{
    std::vector<std::string> g;
    for (const auto& x : fs) if (std::find (g.begin(), g.end(), x.group) == g.end()) g.push_back (x.group);
    return g;
}

int get (const std::vector<uint8_t>& block, const Field& x)
{
    if (x.offset < 0 || x.offset >= (int) block.size()) return x.lo;
    const uint8_t b = block[(size_t) x.offset];
    return x.lo < 0 ? (int) (int8_t) b : (int) b;
}

void set (std::vector<uint8_t>& block, const Field& x, int v)
{
    if (x.offset < 0 || x.offset >= (int) block.size()) return;
    v = std::clamp (v, x.lo, x.hi);
    block[(size_t) x.offset] = (uint8_t) (int8_t) v;
}

std::string fromAkai (const uint8_t* c, int n)
{
    std::string s;
    for (int i = 0; i < n; ++i) s += c[i] <= 40 ? kChars[c[i]] : '?';
    while (! s.empty() && s.back() == ' ') s.pop_back();
    return s;
}

std::vector<uint8_t> toAkai (const std::string& s)
{
    std::vector<uint8_t> c (12, 10);   // (spaces)
    const std::string chars (kChars);
    for (size_t i = 0; i < s.size() && i < 12; ++i)
    {
        char ch = s[i];
        if (ch >= 'a' && ch <= 'z') ch = (char) (ch - 32);
        const auto at = chars.find (ch);
        c[i] = at == std::string::npos ? (uint8_t) 10 : (uint8_t) at;
    }
    return c;
}

std::vector<uint8_t> requestStatus (int cc)   { std::vector<uint8_t> m; header (m, cc, RSTAT); m.push_back (0xF7); return m; }
std::vector<uint8_t> requestPrograms (int cc) { std::vector<uint8_t> m; header (m, cc, RPLIST); m.push_back (0xF7); return m; }
std::vector<uint8_t> requestProgram (int cc, int p) { std::vector<uint8_t> m; header (m, cc, RPDATA); number14 (m, p); m.push_back (0xF7); return m; }
std::vector<uint8_t> requestKeygroup (int cc, int p, int k)
{
    std::vector<uint8_t> m; header (m, cc, RKDATA); number14 (m, p); m.push_back ((uint8_t) (k & 127)); m.push_back (0xF7); return m;
}
std::vector<uint8_t> programMessage (int cc, int p, const std::vector<uint8_t>& block)
{
    std::vector<uint8_t> m; header (m, cc, PDATA); number14 (m, p); nibbles (m, block); m.push_back (0xF7); return m;
}
std::vector<uint8_t> keygroupMessage (int cc, int p, int k, const std::vector<uint8_t>& block)
{
    std::vector<uint8_t> m; header (m, cc, KDATA); number14 (m, p); m.push_back ((uint8_t) (k & 127)); nibbles (m, block); m.push_back (0xF7); return m;
}

bool parse (const uint8_t* m, int n, Reply& r)
{
    r = Reply {};
    if (m == nullptr || n < 6 || m[0] != 0xF0 || m[1] != kAkai || m[4] != kS1000) return false;
    const int end = m[n - 1] == 0xF7 ? n - 1 : n;
    r.op = m[3];
    int at = 5;
    auto denibble = [&] (int from, int bytes, std::vector<uint8_t>& out)
    {
        if (end - from < bytes * 2) return false;
        out.resize ((size_t) bytes);
        for (int i = 0; i < bytes; ++i) out[(size_t) i] = (uint8_t) ((m[from + i * 2] & 15) | ((m[from + i * 2 + 1] & 15) << 4));
        return true;
    };
    switch (r.op)
    {
        case PLIST:
        {
            if (end - at < 2) return false;
            const int count = m[at] | (m[at + 1] << 7); at += 2;
            const bool nib = end - at >= count * 24;   // (names as nibble pairs, or as plain codes)
            for (int i = 0; i < count; ++i)
            {
                uint8_t c[12];
                if (nib) { if (end - at < 24) break; for (int j = 0; j < 12; ++j) c[j] = (uint8_t) ((m[at + j * 2] & 15) | ((m[at + j * 2 + 1] & 15) << 4)); at += 24; }
                else     { if (end - at < 12) break; for (int j = 0; j < 12; ++j) c[j] = m[at + j]; at += 12; }
                r.names.push_back (fromAkai (c, 12));
            }
            return true;
        }
        case PDATA:
            if (end - at < 2) return false;
            r.program = m[at] | (m[at + 1] << 7); at += 2;
            return denibble (at, kProgramBytes, r.block);
        case KDATA:
            if (end - at < 3) return false;
            r.program = m[at] | (m[at + 1] << 7); at += 2;
            r.keygroup = m[at++];
            return denibble (at, kKeygroupBytes, r.block);
        case REPLY:
            r.replyCode = at < end ? m[at] : 0;
            return true;
        default:
            return true;   // (others: recognised, not read)
    }
}

void FakeS1000::addProgram (const std::string& name, int kgs)
{
    programNames.push_back (name);
    std::vector<uint8_t> p (kProgramBytes, 0);
    p[0] = 1;
    const auto n = toAkai (name); std::copy (n.begin(), n.end(), p.begin() + 3);
    p[17] = 16; p[19] = 24; p[20] = 127; p[25] = 80; p[39] = 2; p[42] = (uint8_t) kgs;
    programs.push_back (p);
    std::vector<std::vector<uint8_t>> ks;
    for (int k = 0; k < kgs; ++k)
    {
        std::vector<uint8_t> g (kKeygroupBytes, 0);
        g[0] = 2; g[3] = 24; g[4] = 127; g[7] = 99; g[14] = 99; g[15] = 30; g[22] = 99;
        const auto s = toAkai (name.substr (0, 10) + "-" + std::to_string (k + 1));
        std::copy (s.begin(), s.end(), g.begin() + 34);
        g[47] = 127;
        ks.push_back (g);
    }
    keygroups.push_back (ks);
}

std::vector<std::vector<uint8_t>> FakeS1000::receive (const uint8_t* m, int n)
{
    std::vector<std::vector<uint8_t>> out;
    if (n < 6 || m[0] != 0xF0 || m[1] != kAkai || m[4] != kS1000) return out;
    const int cc = m[2];
    auto reply = [&] (int code) { std::vector<uint8_t> r; header (r, cc, REPLY); r.push_back ((uint8_t) code); r.push_back (0xF7); out.push_back (r); };
    switch (m[3])
    {
        case RPLIST:
        {
            std::vector<uint8_t> r; header (r, cc, PLIST); number14 (r, (int) programNames.size());
            for (const auto& name : programNames) { const auto c = toAkai (name); r.insert (r.end(), c.begin(), c.end()); }
            r.push_back (0xF7); out.push_back (r);
            break;
        }
        case RPDATA:
        {
            const int p = m[5] | (m[6] << 7);
            if (p < (int) programs.size()) out.push_back (programMessage (cc, p, programs[(size_t) p])); else reply (1);
            break;
        }
        case RKDATA:
        {
            const int p = m[5] | (m[6] << 7), k = m[7];
            if (p < (int) programs.size() && k < (int) keygroups[(size_t) p].size()) out.push_back (keygroupMessage (cc, p, k, keygroups[(size_t) p][(size_t) k]));
            else reply (1);
            break;
        }
        case PDATA: case KDATA:
        {
            Reply r;
            if (! parse (m, n, r) || r.program < 0 || r.program >= (int) programs.size()) { reply (1); break; }
            if (r.op == PDATA) programs[(size_t) r.program] = r.block;
            else if (r.keygroup < (int) keygroups[(size_t) r.program].size()) keygroups[(size_t) r.program][(size_t) r.keygroup] = r.block;
            reply (0);
            break;
        }
        default: break;
    }
    return out;
}
} // namespace prv::akai
