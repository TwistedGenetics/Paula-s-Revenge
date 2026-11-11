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
        case Kind::Sixteenths: std::snprintf (b, sizeof b, "%+.2f", v / 16.0); return b;
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

namespace
{
    // the S900's packings: DB = [d6..d0] [d7]; DW = [d6..d0] [d7] [d14..d8] [d15]
    int readDb (const std::vector<uint8_t>& b, int o) { return (b[(size_t) o] & 127) | ((b[(size_t) o + 1] & 1) << 7); }
    void writeDb (std::vector<uint8_t>& b, int o, int v) { b[(size_t) o] = (uint8_t) (v & 127); b[(size_t) o + 1] = (uint8_t) ((v >> 7) & 1); }
    int width (const Field& x) { return x.enc == 2 ? 4 : x.enc == 1 ? 2 : 1; }
}

int get (const std::vector<uint8_t>& block, const Field& x)
{
    if (x.offset < 0 || x.offset + width (x) > (int) block.size()) return x.lo;
    if (x.enc == 1)
    {
        const int v = readDb (block, x.offset);
        if (x.bit >= 0) return (((v >> x.bit) & 1) != 0) != x.invert ? 1 : 0;
        return x.lo < 0 ? (int) (int8_t) (uint8_t) v : v;
    }
    if (x.enc == 2)
    {
        const int v = readDb (block, x.offset) | (readDb (block, x.offset + 2) << 8);
        return x.lo < 0 ? (int) (int16_t) (uint16_t) v : v;
    }
    const uint8_t b = block[(size_t) x.offset];
    return x.lo < 0 ? (int) (int8_t) b : (int) b;
}

void set (std::vector<uint8_t>& block, const Field& x, int v)
{
    if (x.offset < 0 || x.offset + width (x) > (int) block.size()) return;
    v = std::clamp (v, x.lo, x.hi);
    if (x.enc == 1)
    {
        if (x.bit >= 0)
        {
            int old = readDb (block, x.offset);
            const bool on = (v != 0) != x.invert;
            old = on ? (old | (1 << x.bit)) : (old & ~(1 << x.bit));
            writeDb (block, x.offset, old);
        }
        else writeDb (block, x.offset, v & 255);
        return;
    }
    if (x.enc == 2)
    {
        const int u = v & 0xFFFF;
        writeDb (block, x.offset, u & 255); writeDb (block, x.offset + 2, u >> 8);
        return;
    }
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

// ---- S900 / S950
namespace s900
{
namespace
{
    constexpr uint8_t kS900 = 0x40;
    Field db (const char* g, const char* n, int off, int lo, int hi, Kind k = Kind::Number, std::vector<std::string> labels = {})
    {
        Field x = f (g, n, off, lo, hi, k, std::move (labels)); x.enc = 1; return x;
    }
    Field dw (const char* g, const char* n, int off, int lo, int hi, Kind k = Kind::Signed) { Field x = f (g, n, off, lo, hi, k); x.enc = 2; return x; }
    Field bit (const char* g, const char* n, int off, int b, bool invert = false, std::vector<std::string> labels = {})
    {
        Field x = f (g, n, off, 0, 1, Kind::OnOff, std::move (labels)); x.enc = 1; x.bit = b; x.invert = invert; return x;
    }
    void header (std::vector<uint8_t>& m, int ch, int op, int number) { m = { 0xF0, kAkai, (uint8_t) (ch & 15), (uint8_t) op, kS900, (uint8_t) (number & 127), 0 }; }
}

const std::vector<Field>& programFields()
{
    // (offsets from the program's first data byte - the spec's byte 7)
    static const std::vector<Field> all {
        dw ("PROGRAM", "KEY TILT (LOUD)", 32, -50, 50), db ("PROGRAM", "POSITIONAL XFADE", 42, 0, 1, Kind::OnOff),
    };
    return all;
}

const std::vector<Field>& keygroupFields()
{
    static const std::vector<Field> all {
        db ("KEYS", "HIGH NOTE", 0, 24, 127, Kind::Note), db ("KEYS", "LOW NOTE", 2, 24, 127, Kind::Note), db ("KEYS", "VEL SWITCH", 4, 0, 128),
        db ("KEYS", "MIDI CH OFFSET", 40, 0, 15), bit ("KEYS", "TRANSPOSE", 36, 0, true), bit ("KEYS", "ONE SHOT", 36, 3),
        db ("AMP ENV", "ATTACK", 6, 0, 99), db ("AMP ENV", "DECAY", 8, 0, 99), db ("AMP ENV", "SUSTAIN", 10, 0, 99), db ("AMP ENV", "RELEASE", 12, 0, 99),
        db ("VELOCITY", "VEL > ATTACK", 18, 0, 99), db ("VELOCITY", "VEL > RELEASE", 20, -50, 50, Kind::Signed), db ("VELOCITY", "VEL > LOUD", 22, 0, 99),
        db ("VELOCITY", "VEL > FILTER", 14, 0, 99), bit ("VELOCITY", "RELEASE VEL", 36, 4, false, { "NOTE-OFF", "NOTE-ON" }),
        db ("FILTER", "KEY TRACK", 16, 0, 99), db ("FILTER", "ENV AMOUNT", 46, -50, 50, Kind::Signed), db ("FILTER", "ATTACK", 68, 0, 99),
        db ("FILTER", "DECAY", 70, 0, 99), db ("FILTER", "SUSTAIN", 72, 0, 99), db ("FILTER", "RELEASE", 74, 0, 99),
        db ("PITCH WARP", "WARP", 26, -50, 50, Kind::Signed), db ("PITCH WARP", "WARP TIME", 28, 0, 99), db ("PITCH WARP", "VEL > WARP", 24, 0, 99),
        db ("LFO", "RATE", 32, 0, 99), db ("LFO", "DEPTH", 34, 0, 99), db ("LFO", "DELAY", 30, 0, 99), db ("LFO", "AFTERTOUCH", 42, 0, 99),
        db ("LFO", "MOD WHEEL", 44, 0, 99), bit ("LFO", "DESYNC", 36, 2),
        bit ("CROSSFADE", "VEL XFADE", 36, 1), bit ("CROSSFADE", "XFADE CURVE", 36, 5), db ("CROSSFADE", "50% AT VELOCITY", 76, 0, 127),
        dw ("SOFT SAMPLE", "TUNE", 84, -384, 384, Kind::Sixteenths), db ("SOFT SAMPLE", "FILTER", 88, 0, 99), db ("SOFT SAMPLE", "LOUDNESS", 90, -50, 50, Kind::Signed),
        dw ("LOUD SAMPLE", "TUNE", 128, -384, 384, Kind::Sixteenths), db ("LOUD SAMPLE", "FILTER", 132, 0, 99), db ("LOUD SAMPLE", "LOUDNESS", 134, -50, 50, Kind::Signed),
    };
    return all;
}

std::vector<uint8_t> requestCatalog (int ch) { std::vector<uint8_t> m; header (m, ch, RCAT, 0); m.push_back (0xF7); return m; }
std::vector<uint8_t> requestProgram (int ch, int p) { std::vector<uint8_t> m; header (m, ch, RPRGM, p); m.push_back (0xF7); return m; }

std::vector<uint8_t> programMessage (int ch, int p, const std::vector<uint8_t>& block, const std::vector<std::vector<uint8_t>>& kgs)
{
    std::vector<uint8_t> m; header (m, ch, PRGM, p);
    uint8_t sum = 0;
    auto add = [&] (const std::vector<uint8_t>& v) { for (uint8_t b : v) { m.push_back (b & 127); sum ^= (uint8_t) (b & 127); } };
    add (block);
    for (const auto& k : kgs) add (k);
    m.push_back (sum & 127); m.push_back (0xF7);
    return m;
}

std::string name (const std::vector<uint8_t>& block, int o)
{
    std::string s;
    for (int i = 0; i < 10 && o + i * 2 + 1 < (int) block.size(); ++i)
    {
        const int c = readDb (block, o + i * 2);
        s += c >= 32 && c < 127 ? (char) c : ' ';
    }
    while (! s.empty() && s.back() == ' ') s.pop_back();
    return s;
}

void setName (std::vector<uint8_t>& block, int o, const std::string& n)
{
    for (int i = 0; i < 10 && o + i * 2 + 1 < (int) block.size(); ++i) writeDb (block, o + i * 2, i < (int) n.size() ? (uint8_t) n[(size_t) i] : ' ');
}

bool parse (const uint8_t* m, int n, Reply& r)
{
    r = Reply {};
    if (m == nullptr || n < 8 || m[0] != 0xF0 || m[1] != kAkai || m[4] != kS900) return false;
    r.op = m[3];
    const int end = m[n - 1] == 0xF7 ? n - 1 : n;   // (the checksum is the byte before it)
    const int data = 7, dataEnd = end - 1;
    if (r.op == CAT)
    {
        for (int at = data; at + 12 <= dataEnd; at += 12)
        {
            if (m[at] != 'P') continue;   // (programs only; 'S' is a sample)
            std::string s;
            for (int j = 2; j < 12; ++j) s += m[at + j] >= 32 && m[at + j] < 127 ? (char) m[at + j] : ' ';
            while (! s.empty() && s.back() == ' ') s.pop_back();
            r.names.push_back (s); r.numbers.push_back (m[at + 1]);
        }
        return true;
    }
    if (r.op == PRGM)
    {
        r.program = m[5];
        if (dataEnd - data < kProgramBytes) return false;
        uint8_t sum = 0;
        for (int i = data; i < dataEnd; ++i) sum ^= m[i];
        if ((sum & 127) != (m[dataEnd] & 127)) return false;   // (a damaged message)
        r.block.assign (m + data, m + data + kProgramBytes);
        for (int at = data + kProgramBytes; at + kKeygroupBytes <= dataEnd; at += kKeygroupBytes)
            r.keygroups.emplace_back (m + at, m + at + kKeygroupBytes);
        return true;
    }
    return true;
}

void FakeS950::addProgram (int number, const std::string& nm, int kgs)
{
    Prog p; p.number = number;
    p.block.assign (kProgramBytes, 0);
    setName (p.block, kProgramName, nm);
    writeDb (p.block, 44, 255);
    writeDb (p.block, 46, kgs);
    for (int k = 0; k < kgs; ++k)
    {
        std::vector<uint8_t> g (kKeygroupBytes, 0);
        writeDb (g, 0, 127); writeDb (g, 2, 24); writeDb (g, 4, 128); writeDb (g, 8, 80); writeDb (g, 10, 99); writeDb (g, 12, 30);
        writeDb (g, 16, 50); writeDb (g, 22, 30); writeDb (g, 30, 64); writeDb (g, 32, 42); writeDb (g, 36, 4); writeDb (g, 38, 255);
        writeDb (g, 44, 50); writeDb (g, 68, 20); writeDb (g, 70, 20); writeDb (g, 72, 20); writeDb (g, 74, 20); writeDb (g, 76, 64);
        writeDb (g, 88, 99); writeDb (g, 132, 99);
        setName (g, kSoftName, nm.substr (0, 7) + " S" + std::to_string (k + 1));
        setName (g, kLoudName, nm.substr (0, 7) + " L" + std::to_string (k + 1));
        p.keygroups.push_back (g);
    }
    programs.push_back (p);
}

std::vector<std::vector<uint8_t>> FakeS950::receive (const uint8_t* m, int n)
{
    std::vector<std::vector<uint8_t>> out;
    if (n < 8 || m[0] != 0xF0 || m[1] != kAkai || m[4] != kS900) return out;
    const int ch = m[2];
    if (m[3] == RCAT)
    {
        std::vector<uint8_t> r; header (r, ch, CAT, 0);
        uint8_t sum = 0;
        auto entry = [&] (char type, int num, const std::string& nm)
        {
            uint8_t e[12] = { (uint8_t) type, (uint8_t) num };
            for (int j = 0; j < 10; ++j) e[2 + j] = j < (int) nm.size() ? (uint8_t) nm[(size_t) j] : ' ';
            for (uint8_t b : e) { r.push_back (b); sum ^= b; }
        };
        for (const auto& p : programs) entry ('P', p.number, name (p.block, kProgramName));
        for (int i = 0; i < (int) samples.size(); ++i) entry ('S', i, samples[(size_t) i]);
        r.push_back (sum & 127); r.push_back (0xF7);
        out.push_back (r);
    }
    else if (m[3] == RPRGM)
    {
        for (const auto& p : programs) if (p.number == m[5]) out.push_back (programMessage (ch, p.number, p.block, p.keygroups));
    }
    else if (m[3] == PRGM)
    {
        Reply r;
        if (s900::parse (m, n, r))
            for (auto& p : programs) if (p.number == r.program) { p.block = r.block; p.keygroups = r.keygroups; }
    }
    return out;
}
}
} // namespace prv::akai
