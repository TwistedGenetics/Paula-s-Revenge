// Paula's Revenge - Akai S1000 System Exclusive (see AkaiSysex.h)
#include "AkaiSysex.h"
#include <algorithm>
#include <cmath>
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
        case PLIST: case SLIST:
        {
            if (end - at < 2) return false;
            const int count = m[at] | (m[at + 1] << 7); at += 2;
            const bool nib = end - at >= count * 24;   // (names as nibble pairs, or as plain codes)
            for (int i = 0; i < count; ++i)
            {
                uint8_t c[12];
                if (nib) { if (end - at < 24) break; for (int j = 0; j < 12; ++j) c[j] = (uint8_t) ((m[at + j * 2] & 15) | ((m[at + j * 2 + 1] & 15) << 4)); at += 24; }
                else     { if (end - at < 12) break; for (int j = 0; j < 12; ++j) c[j] = m[at + j]; at += 12; }
                (r.op == PLIST ? r.names : r.sampleNames).push_back (fromAkai (c, 12));
            }
            return true;
        }
        case SDATA:
        {
            if (end - at < 2) return false;
            r.sample = m[at] | (m[at + 1] << 7); at += 2;
            const int bytes = (end - at) / 2;
            if (bytes < 141) return false;   // (to SHLTO)
            return denibble (at, bytes, r.block);
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
    // SDS data packets for the sample it's taking
    if (n >= 6 && m[0] == 0xF0 && m[1] == 0x7E && m[3] == 0x02)
    {
        std::vector<int16_t> w; int pk = 0;
        const bool ok = sds::parsePacket (m, n, 16, pk, w);
        if (! ok || nakNext > 0) { if (nakNext > 0) --nakNext; out.push_back (sds::handshake (m[2], sds::NAK, ok ? pk : m[4])); return out; }
        if (takingSample >= 0 && takingSample < (int) samples.size())
        {
            auto& d = samples[(size_t) takingSample].data;
            for (int16_t x : w) if (takingCount > 0 && takingAt < (int) d.size()) { d[(size_t) takingAt++] = x; --takingCount; }
        }
        out.push_back (sds::handshake (m[2], sds::ACK, pk));
        return out;
    }
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
        case RSLIST:
        {
            std::vector<uint8_t> r; header (r, cc, SLIST); number14 (r, (int) samples.size());
            for (const auto& x : samples) { const auto c = toAkai (x.name); r.insert (r.end(), c.begin(), c.end()); }
            r.push_back (0xF7); out.push_back (r);
            break;
        }
        case RSDATA:
        {
            const int sm = m[5] | (m[6] << 7);
            if (sm < (int) samples.size()) out.push_back (sampleHeaderMessage (cc, sm, sampleHeader (samples[(size_t) sm])));
            else reply (1);
            break;
        }
        case SDATA:
        {
            Reply r; Sample meta; int len = 0;
            if (! parse (m, n, r) || ! readSampleHeader (r.block, meta, len) || len <= 0) { reply (1); break; }
            meta.data.assign ((size_t) len, 0);
            if (r.sample >= (int) samples.size()) { samples.push_back (meta); takingSample = (int) samples.size() - 1; }
            else { samples[(size_t) r.sample] = meta; takingSample = r.sample; }
            takingAt = 0; takingCount = len;
            reply (0);
            break;
        }
        case ASPACK:
        {
            const int sm = m[5] | (m[6] << 7);
            int off = 0, cnt = 0;
            for (int i = 0; i < 4; ++i) { off |= (m[7 + i] & 127) << (7 * i); cnt |= (m[11 + i] & 127) << (7 * i); }
            if (sm >= (int) samples.size()) { reply (1); break; }
            takingSample = sm; takingAt = off; takingCount = cnt;
            out.push_back (sds::handshake (cc, sds::ACK, 0));
            break;
        }
        case RSPACK:
        {
            const int sm = m[5] | (m[6] << 7);
            int off = 0, cnt = 0;
            for (int i = 0; i < 4; ++i) { off |= (m[7 + i] & 127) << (7 * i); cnt |= (m[11 + i] & 127) << (7 * i); }
            if (sm >= (int) samples.size()) { reply (1); break; }
            const auto& d = samples[(size_t) sm].data;
            off = std::clamp (off, 0, (int) d.size()); cnt = std::clamp (cnt, 0, (int) d.size() - off);
            const std::vector<int16_t> part (d.begin() + off, d.begin() + off + cnt);
            for (auto& pk : samplePackets (cc, part)) out.push_back (pk);
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

// ---- samples over MIDI (v0.74)
namespace sds
{
    std::vector<uint8_t> packet (int ch, int number, const int16_t* w, int count)
    {
        std::vector<uint8_t> m { 0xF0, 0x7E, (uint8_t) (ch & 127), 0x02, (uint8_t) (number & 127) };
        for (int i = 0; i < kWordsPerPacket; ++i)
        {
            // 16 bits, unsigned, left-justified in three 7-bit bytes (silence pads the last packet)
            const unsigned u = (unsigned) ((i < count ? (int) w[i] : 0) + 32768);
            m.push_back ((uint8_t) ((u >> 9) & 127)); m.push_back ((uint8_t) ((u >> 2) & 127)); m.push_back ((uint8_t) ((u << 5) & 0x60));
        }
        uint8_t sum = 0;
        for (size_t i = 1; i < m.size(); ++i) sum ^= m[i];
        m.push_back (sum & 127); m.push_back (0xF7);
        return m;
    }

    std::vector<uint8_t> handshake (int ch, Handshake h, int pk) { return { 0xF0, 0x7E, (uint8_t) (ch & 127), (uint8_t) h, (uint8_t) (pk & 127), 0xF7 }; }

    bool parseHandshake (const uint8_t* m, int n, int& kind, int& pk)
    {
        if (n < 5 || m[0] != 0xF0 || m[1] != 0x7E || m[3] < WAIT || m[3] > ACK) return false;
        kind = m[3]; pk = m[4];
        return true;
    }

    bool parsePacket (const uint8_t* m, int n, int bits, int& number, std::vector<int16_t>& out)
    {
        if (n < 127 || m[0] != 0xF0 || m[1] != 0x7E || m[3] != 0x02) return false;
        uint8_t sum = 0;
        for (int i = 1; i < 125; ++i) sum ^= m[i];
        if ((sum & 127) != (m[125] & 127)) return false;
        number = m[4];
        bits = std::clamp (bits, 8, 28);
        const int per = (bits + 6) / 7;   // (bytes a word)
        for (int at = 5; at + per <= 125; at += per)
        {
            uint32_t v = 0;
            for (int b = 0; b < per; ++b) v = (v << 7) | (m[at + b] & 127);
            v >>= per * 7 - bits;
            const int sv = (int) v - (1 << (bits - 1));
            out.push_back ((int16_t) std::clamp (bits >= 16 ? sv >> (bits - 16) : sv << (16 - bits), -32768, 32767));
        }
        return true;
    }
}

namespace
{
    void le16 (std::vector<uint8_t>& b, int o, int v) { b[(size_t) o] = (uint8_t) (v & 255); b[(size_t) o + 1] = (uint8_t) ((v >> 8) & 255); }
    void le32 (std::vector<uint8_t>& b, int o, int v) { le16 (b, o, v & 0xFFFF); le16 (b, o + 2, (v >> 16) & 0xFFFF); }
    int rd16 (const std::vector<uint8_t>& b, int o) { return b[(size_t) o] | (b[(size_t) o + 1] << 8); }
    int rd32 (const std::vector<uint8_t>& b, int o) { return rd16 (b, o) | (rd16 (b, o + 2) << 16); }
}

std::vector<uint8_t> sampleHeader (const Sample& smp)
{
    // SHIDENT 3, SBANDW, SPITCH (C-3 = 60), SHNAME, SSRVLD, SLOOPS, SALOOP, spare, SPTYPE, STUNO, SLOCAT, SLNGTH,
    // SSTART, SMPEND, then 8 loops of 12 (LOOPAT - the loop's END - , LLNGTH fraction:low:high, LDWELL), SSPARE,
    // SSPAIR, SSRATE, SHLTO
    std::vector<uint8_t> b (kSampleHeaderBytes, 0);
    const int len = (int) smp.data.size();
    const bool loop = smp.loopLength >= 4 && smp.loopStart >= 0 && smp.loopStart + smp.loopLength <= len;
    b[0] = 3;
    b[1] = smp.rate > 30000 ? 1 : 0;   // (bandwidth: 20 kHz, or 10)
    b[2] = 60;
    const auto nm = toAkai (smp.name); std::copy (nm.begin(), nm.end(), b.begin() + 3);
    b[15] = 0x80;
    b[16] = loop ? 1 : 0;
    b[19] = loop ? 0 : 2;              // (normal looping, or no loops)
    le32 (b, 26, len);                 // SLNGTH
    le32 (b, 34, len);                 // SMPEND
    if (loop)
    {
        le32 (b, 38, smp.loopStart + smp.loopLength);   // LOOPAT
        le16 (b, 44, smp.loopLength & 0xFFFF); le16 (b, 46, smp.loopLength >> 16);
        le16 (b, 48, 9999);            // (dwell: hold - it loops for as long as the note)
    }
    le16 (b, 138, std::clamp (smp.rate, 1, 65535));
    return b;
}

bool readSampleHeader (const std::vector<uint8_t>& b, Sample& smp, int& length)
{
    if (b.size() < 141 || b[0] != 3) return false;
    smp = Sample {};
    smp.name = fromAkai (b.data() + 3, 12);
    length = rd32 (b, 26);
    smp.rate = (b[15] & 0x80) != 0 && rd16 (b, 138) > 0 ? rd16 (b, 138) : (b[1] ? 44100 : 22050);
    const int lp = rd32 (b, 38), ll = rd16 (b, 44) | (rd16 (b, 46) << 16);
    if (b[19] <= 1 && ll >= 4 && lp - ll >= 0 && lp <= length) { smp.loopStart = lp - ll; smp.loopLength = ll; }
    return length >= 0;
}

std::vector<uint8_t> requestSamples (int cc) { std::vector<uint8_t> m; header (m, cc, RSLIST); m.push_back (0xF7); return m; }
std::vector<uint8_t> requestSampleHeader (int cc, int sm) { std::vector<uint8_t> m; header (m, cc, RSDATA); number14 (m, sm); m.push_back (0xF7); return m; }
std::vector<uint8_t> sampleHeaderMessage (int cc, int sm, const std::vector<uint8_t>& block)
{
    std::vector<uint8_t> m; header (m, cc, SDATA); number14 (m, sm); nibbles (m, block); m.push_back (0xF7); return m;
}
namespace
{
    void number28 (std::vector<uint8_t>& m, int v) { for (int i = 0; i < 4; ++i) m.push_back ((uint8_t) ((v >> (7 * i)) & 127)); }
}
std::vector<uint8_t> acceptPackets (int cc, int sm, int offset, int count)
{
    std::vector<uint8_t> m; header (m, cc, ASPACK); number14 (m, sm); number28 (m, offset); number28 (m, count); m.push_back (0xF7); return m;
}
std::vector<uint8_t> requestPackets (int cc, int sm, int offset, int count)
{
    std::vector<uint8_t> m; header (m, cc, RSPACK); number14 (m, sm); number28 (m, offset); number28 (m, count); m.push_back (1); m.push_back (0xF7); return m;
}

std::vector<std::vector<uint8_t>> samplePackets (int cc, const std::vector<int16_t>& d)
{
    std::vector<std::vector<uint8_t>> out;
    for (int at = 0, k = 0; at < (int) d.size(); at += sds::kWordsPerPacket, ++k)
        out.push_back (sds::packet (cc, k, d.data() + at, std::min (sds::kWordsPerPacket, (int) d.size() - at)));
    return out;
}

double secondsOnTheWire (bool s950, int words)
{
    // the S950: blocks of 60 words in 122 bytes; the S1000: packets of 40 in 127, and a 6-byte answer to each
    const double bytes = s950 ? std::ceil (words / 60.0) * 122.0 + 30 : std::ceil (words / 40.0) * 127.0 + 200;
    return bytes / 3125.0;
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
            if (m[at] != 'P' && m[at] != 'S') continue;
            std::string s;
            for (int j = 2; j < 12; ++j) s += m[at + j] >= 32 && m[at + j] < 127 ? (char) m[at + j] : ' ';
            while (! s.empty() && s.back() == ' ') s.pop_back();
            if (m[at] == 'P') { r.names.push_back (s); r.numbers.push_back (m[at + 1]); }
            else { r.sampleNames.push_back (s); r.sampleNumbers.push_back (m[at + 1]); }
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

namespace
{
    void tb (std::vector<uint8_t>& m, int v) { m.push_back ((uint8_t) (v & 127)); m.push_back ((uint8_t) ((v >> 7) & 127)); m.push_back ((uint8_t) ((v >> 14) & 127)); }
    int rtb (const uint8_t* p) { return (p[0] & 127) | ((p[1] & 127) << 7) | ((p[2] & 127) << 14); }
    void writeDd (std::vector<uint8_t>& b, int o, int v) { for (int i = 0; i < 4; ++i) writeDb (b, o + i * 2, (v >> (8 * i)) & 255); }
    int readDd (const std::vector<uint8_t>& b, int o) { int v = 0; for (int i = 0; i < 4; ++i) v |= readDb (b, o + i * 2) << (8 * i); return v; }
    void writeDw (std::vector<uint8_t>& b, int o, int v) { writeDb (b, o, v & 255); writeDb (b, o + 2, (v >> 8) & 255); }
    bool loops (const Sample& s) { return s.loopLength >= 4 && s.loopStart >= 0 && s.loopStart + s.loopLength <= (int) s.data.size(); }
}

std::vector<uint8_t> receptionOn (int ch) { std::vector<uint8_t> m; header (m, ch, SECRE, 0); m.push_back (0xF7); return m; }
std::vector<uint8_t> requestDump (int number) { return { 0xF0, 0x7E, 0x00, (uint8_t) (number & 127), 0x00, 0xF7 }; }
std::vector<uint8_t> ack() { return { 0xF0, 0x7E, 0x7F, 0xF7 }; }

std::vector<uint8_t> sampleDump (int number, const Sample& smp)
{
    const int len = (int) smp.data.size();
    std::vector<uint8_t> m { 0xF0, 0x7E, 0x01, (uint8_t) (number & 127), 0x00, 12 };
    tb (m, (int) std::lround (1.0e9 / std::max (1, smp.rate)));   // (the sampling period in ns)
    tb (m, len);
    // a loop: its start and end (inclusive); none: start at the end ("equal to or larger than TOTAL WORDS-5: not looping")
    if (loops (smp)) { tb (m, smp.loopStart); tb (m, smp.loopStart + smp.loopLength - 1); m.push_back (smp.pingPong ? 1 : 0); }
    else { tb (m, std::max (0, len - 1)); tb (m, std::max (0, len - 1)); m.push_back (0); }
    for (int at = 0, k = 0; at < len; at += 60, ++k)
    {
        // a block: its number, 60 words of 12 bits (offset binary: 0 = 40 00), the XOR of those 120 bytes
        m.push_back ((uint8_t) (k & 127));
        uint8_t sum = 0;
        for (int i = 0; i < 60; ++i)
        {
            const int v = at + i < len ? smp.data[(size_t) (at + i)] : 0;
            const int u = std::clamp ((v >> 4) + 2048, 0, 4095);
            const uint8_t a = (uint8_t) ((u >> 5) & 127), b = (uint8_t) ((u & 31) << 2);
            m.push_back (a); m.push_back (b); sum ^= a; sum ^= b;
        }
        m.push_back (sum & 127);
    }
    m.push_back (0xF7);
    return m;
}

std::vector<uint8_t> sampleParams (int ch, int number, const Sample& smp)
{
    // the spec's bytes 7 - 126: SNAME, SLNGTH, SMRATE, SNOMP (C3 = 960), SDFLDO, SPRLMD, SEND, SSTART, SLOOP, VC, NOREV
    std::vector<uint8_t> b (120, 0);
    std::string nm;
    for (char c : smp.name) nm += (char) (c >= 'a' && c <= 'z' ? c - 32 : c);
    setName (b, 0, nm);
    const int len = (int) smp.data.size();
    writeDd (b, 32, len);
    writeDw (b, 40, smp.rate);
    writeDw (b, 44, 960);
    writeDb (b, 52, loops (smp) ? (smp.pingPong ? 'A' : 'L') : 'O');
    // (the S900's loop is the LAST SLOOP words before the end point: a loop ends the sample there)
    writeDd (b, 56, loops (smp) ? smp.loopStart + smp.loopLength : len);
    writeDd (b, 72, loops (smp) ? smp.loopLength : 0);
    writeDb (b, 86, 'N');
    std::vector<uint8_t> m; header (m, ch, SPRM, number);
    uint8_t sum = 0;
    for (uint8_t x : b) { m.push_back (x & 127); sum ^= (uint8_t) (x & 127); }
    m.push_back (sum & 127); m.push_back (0xF7);
    return m;
}

bool parseDump (const uint8_t* m, int n, Sample& smp, int& number, int& bad)
{
    smp = Sample {}; bad = 0; number = -1;
    if (n < 20 || m[0] != 0xF0 || m[1] != 0x7E || m[2] != 0x01) return false;
    number = m[3] | (m[4] << 7);
    const int bits = std::clamp ((int) m[5], 8, 14);
    const int period = rtb (m + 6), len = rtb (m + 9), ls = rtb (m + 12), le = rtb (m + 15), mode = m[18];
    if (len <= 0 || period <= 0) return false;
    smp.rate = (int) std::lround (1.0e9 / period);
    smp.data.reserve ((size_t) len);
    const int end = m[n - 1] == 0xF7 ? n - 1 : n;
    for (int at = 19; at + 122 <= end && (int) smp.data.size() < len; at += 122)
    {
        uint8_t sum = 0;
        for (int i = 1; i <= 120; ++i) sum ^= m[at + i];
        if ((sum & 127) != (m[at + 121] & 127)) ++bad;
        for (int i = 0; i < 60 && (int) smp.data.size() < len; ++i)
        {
            const int v = (((m[at + 1 + i * 2] & 127) << 7) | (m[at + 2 + i * 2] & 127)) >> (14 - bits);
            const int sv = v - (1 << (bits - 1));
            smp.data.push_back ((int16_t) std::clamp (sv << (16 - bits), -32768, 32767));
        }
    }
    if (ls < len - 5 && le > ls && le < len && mode != 0x7F) { smp.loopStart = ls; smp.loopLength = le - ls + 1; smp.pingPong = mode == 1; }
    return ! smp.data.empty();
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
    // the sample dump (system exclusive common: only once SECRE has switched it on)
    if (n >= 6 && m[0] == 0xF0 && m[1] == 0x7E)
    {
        if (! receptionEnabled) return out;
        if (m[2] == 0x00)   // RSD
        {
            const int k = m[3];
            if (k < (int) sampleData.size()) out.push_back (sampleDump (k, sampleData[(size_t) k]));
        }
        else if (m[2] == 0x01)   // SD
        {
            Sample smp; int k = 0, bad = 0;
            if (parseDump (m, n, smp, k, bad) && bad == 0)
            {
                if (k >= (int) sampleData.size()) { sampleData.resize ((size_t) k + 1); samples.resize ((size_t) k + 1); }
                smp.name = std::to_string (k);   // (it shows its number until it's named)
                sampleData[(size_t) k] = smp; samples[(size_t) k] = smp.name;
            }
        }
        return out;
    }
    if (n < 8 || m[0] != 0xF0 || m[1] != kAkai || m[4] != kS900) return out;
    const int ch = m[2];
    if (m[3] == SECRE) { receptionEnabled = true; return out; }
    if (m[3] == SPRM && n >= 128)
    {
        const int k = m[5];
        std::vector<uint8_t> b (m + 7, m + 127);
        uint8_t sum = 0; for (uint8_t x : b) sum ^= x;
        if ((sum & 127) == (m[127] & 127) && k < (int) sampleData.size())
        {
            auto& smp = sampleData[(size_t) k];
            smp.name = name (b, 0); samples[(size_t) k] = smp.name;
            const int mode = readDb (b, 52), sl = readDd (b, 72), se = readDd (b, 56);
            if (mode == 'O') { smp.loopLength = 0; }
            else if (sl >= 4 && sl <= se) { smp.loopStart = se - sl; smp.loopLength = sl; smp.pingPong = mode == 'A'; }
        }
        return out;
    }
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

// ---- Transfer
namespace
{
    std::string upper (std::string x) { for (auto& c : x) if (c >= 'a' && c <= 'z') c = (char) (c - 32); return x; }
    std::string secs (double s) { char b[32]; std::snprintf (b, sizeof b, "%d", (int) std::ceil (s)); return b; }
}

void Transfer::fail (const std::string& why) { state = State::Failed; step = Step::None; status = why; out.clear(); }
void Transfer::finish (const std::string& what) { state = State::Done; step = Step::None; status = what; progress = 1; }
void Transfer::cancel() { if (state == State::Working) fail ("STOPPED"); }

void Transfer::sendS1000 (int cc, const Sample& smp, double now)
{
    *this = Transfer {};
    s950 = false; sending = true; ch = cc; result = smp; state = State::Working; startedAt = sentAt = now;
    result.name = upper (smp.name).substr (0, 12);
    step = Step::AskList; out.push_back (requestSamples (cc));
    status = "ASKING THE S1000 WHAT SAMPLES IT HAS...";
}

void Transfer::getS1000 (int cc, int sm, double now)
{
    *this = Transfer {};
    s950 = false; sending = false; ch = cc; number = sm; state = State::Working; startedAt = sentAt = now;
    step = Step::AskHeader; out.push_back (requestSampleHeader (cc, sm));
    status = "ASKING THE S1000 FOR THE SAMPLE...";
}

void Transfer::sendS950 (int c, const Sample& smp, double now)
{
    *this = Transfer {};
    s950 = true; sending = true; ch = c; result = smp; state = State::Working; startedAt = sentAt = now;
    result.name = upper (smp.name).substr (0, 10);
    step = Step::AskList;
    out.push_back (s900::receptionOn (c));
    out.push_back (s900::requestCatalog (c));
    status = "ASKING THE S950 WHAT'S IN IT...";
}

void Transfer::getS950 (int c, int num, const std::string& nm, double now)
{
    *this = Transfer {};
    s950 = true; sending = false; ch = c; number = num; wantName = nm; state = State::Working; startedAt = sentAt = ackAt = now;
    step = Step::AwaitDump;
    out.push_back (s900::receptionOn (c));
    out.push_back (s900::requestDump (num));
    status = "ASKING THE S950 FOR " + nm + " - IT COMES OVER MIDI AT ITS OWN PACE...";
}

void Transfer::sendPacket (double now)
{
    if (next >= (int) packets.size()) return;
    out.push_back (packets[(size_t) next]);
    sentAt = now; waitingAnswer = true;
}

void Transfer::received (const uint8_t* m, int n, double now)
{
    if (state != State::Working || m == nullptr || n < 4) return;
    if (! s950)
    {
        int kind = 0, pk = 0;
        if (sds::parseHandshake (m, n, kind, pk))
        {
            if (step == Step::Header && kind == sds::ACK) { step = Step::Accept; return; }   // (some take a header with an ACK)
            if (step == Step::Accept && kind == sds::ACK)
            {
                handshaking = true; step = Step::Packets; next = 0; tries = 0;
                status = "SENDING " + result.name + " TO THE S1000...";
                sendPacket (now);
                return;
            }
            if (step != Step::Packets) return;
            handshaking = true;
            if (kind == sds::WAIT) { held = true; sentAt = now; return; }
            held = false;
            if (kind == sds::CANCEL) { fail ("THE S1000 STOPPED THE TRANSFER"); return; }
            if (kind == sds::NAK)
            {
                if (++tries > 5) { fail ("THE S1000 KEPT REFUSING PACKET " + std::to_string (next + 1) + " - CHECK THE MIDI CABLES"); return; }
                ++resent; sendPacket (now); return;
            }
            // ACK: the next one
            if (pk != (next & 127)) return;
            ++next; tries = 0; waitingAnswer = false;
            progress = (float) next / (float) std::max<size_t> (1, packets.size());
            if (next >= (int) packets.size()) { finish (result.name + " IS IN THE S1000 (SAMPLE " + std::to_string (number + 1) + ")"); return; }
            sendPacket (now);
            return;
        }
        if (n >= 127 && m[1] == 0x7E && m[3] == 0x02 && step == Step::Packets2)
        {
            int pk = 0;
            const size_t before = result.data.size();
            if (! sds::parsePacket (m, n, 16, pk, result.data)) { result.data.resize (before); out.push_back (sds::handshake (ch, sds::NAK, m[4])); ++resent; sentAt = now; return; }
            out.push_back (sds::handshake (ch, sds::ACK, pk));
            sentAt = now;
            progress = (float) result.data.size() / (float) std::max (1, expected);
            if ((int) result.data.size() >= expected) { result.data.resize ((size_t) expected); finish ("GOT " + result.name + " FROM THE S1000"); }
            return;
        }
        Reply r;
        if (! parse (m, n, r)) return;
        if (r.op == SLIST && step == Step::AskList)
        {
            // the next number, and a name of its own (the S1000 replaces a sample with the same name)
            number = (int) r.sampleNames.size();
            std::string nm = result.name.empty() ? "PAULA" : result.name;
            for (int k = 2; std::find (r.sampleNames.begin(), r.sampleNames.end(), nm) != r.sampleNames.end(); ++k)
                nm = nm.substr (0, 12 - 1 - std::to_string (k).size()) + "-" + std::to_string (k);
            result.name = nm;
            step = Step::Header; sentAt = now;
            out.push_back (sampleHeaderMessage (ch, number, sampleHeader (result)));
            status = "MAKING " + nm + " IN THE S1000...";
        }
        else if (r.op == REPLY && step == Step::Header)
        {
            if (r.replyCode != 0) { fail ("THE S1000 COULDN'T MAKE THE SAMPLE - IS ITS MEMORY FULL?"); return; }
            step = Step::Accept; sentAt = now;
            packets = samplePackets (ch, result.data);
            out.push_back (acceptPackets (ch, number, 0, (int) result.data.size()));
        }
        else if (r.op == REPLY && r.replyCode != 0 && step == Step::AskHeader) fail ("THE S1000 HAS NO SAMPLE " + std::to_string (number + 1));
        else if (r.op == SDATA && step == Step::AskHeader)
        {
            Sample meta; int len = 0;
            if (! readSampleHeader (r.block, meta, len) || len <= 0) { fail ("THE S1000'S SAMPLE HEADER DIDN'T MAKE SENSE"); return; }
            result = meta; expected = len; result.data.clear(); result.data.reserve ((size_t) len);
            step = Step::Packets2; sentAt = now;
            out.push_back (requestPackets (ch, number, 0, len));
            status = "GETTING " + meta.name + " FROM THE S1000 (ABOUT " + secs (secondsOnTheWire (false, len)) + " S)...";
        }
        return;
    }
    // the S950
    Reply r;
    if (n >= 20 && m[1] == 0x7E && m[2] == 0x01 && step == Step::AwaitDump)
    {
        Sample smp; int k = 0, bad = 0;
        if (! s900::parseDump (m, n, smp, k, bad)) { fail ("THE S950'S SAMPLE DIDN'T MAKE SENSE"); return; }
        smp.name = wantName;
        result = smp;
        finish ("GOT " + wantName + " FROM THE S950" + (bad > 0 ? " - " + std::to_string (bad) + " DAMAGED BLOCKS (CHECK THE MIDI CABLE)" : std::string()));
        return;
    }
    if (! s900::parse (m, n, r) || r.op != s900::CAT) return;
    if (step == Step::AskList)
    {
        number = 0;
        for (int x : r.sampleNumbers) number = std::max (number, x + 1);
        std::string nm = result.name.empty() ? "PAULA" : result.name;
        for (int k = 2; std::find (r.sampleNames.begin(), r.sampleNames.end(), nm) != r.sampleNames.end(); ++k)
            nm = nm.substr (0, 10 - 1 - std::to_string (k).size()) + "-" + std::to_string (k);
        result.name = nm;
        wireMs = secondsOnTheWire (true, (int) result.data.size()) * 1000.0;
        out.push_back (s900::sampleDump (number, result));
        step = Step::Dump; sentAt = now;
        status = "SENDING " + nm + " TO THE S950 - ABOUT " + secs (wireMs / 1000.0) + " S OVER MIDI...";
    }
    else if (step == Step::Confirm)
    {
        if (std::find (r.sampleNames.begin(), r.sampleNames.end(), result.name) != r.sampleNames.end())
            finish (result.name + " IS IN THE S950 (SAMPLE " + std::to_string (number + 1) + ")");
        else finish ("SENT " + result.name + " - THE S950 DOESN'T LIST IT YET: CHECK ITS SAMPLES");
    }
}

std::vector<std::vector<uint8_t>> Transfer::poll (double now)
{
    if (state == State::Working)
    {
        const double since = now - sentAt;
        switch (step)
        {
            case Step::AskList: case Step::AskHeader:
                if (since > 3000) fail (std::string ("NO ANSWER FROM THE ") + (s950 ? "S950" : "S1000") + " - SEE HELP: SAMPLES OVER MIDI");
                break;
            case Step::Header:
                if (since > 3000) { step = Step::Accept; sentAt = now; packets = samplePackets (ch, result.data); out.push_back (acceptPackets (ch, number, 0, (int) result.data.size())); }
                break;
            case Step::Accept:
                if (since > 2000)
                {
                    // no ACK: send without answers, at MIDI's own pace
                    step = Step::Packets; next = 0; handshaking = false;
                    status = "SENDING " + result.name + " TO THE S1000 (NO ANSWERS - SENDING BLIND)...";
                    sendPacket (now);
                }
                break;
            case Step::Packets:
                if (held) { if (since > 20000) fail ("THE S1000 SAID WAIT AND NEVER CAME BACK"); break; }
                if (handshaking ? since > 1000 : since > 45)
                {
                    // (no answer in time: assume none are coming - the standard's open loop - and go on)
                    if (handshaking && ++tries > 3) { handshaking = false; }
                    ++next; tries = handshaking ? tries : 0;
                    progress = (float) next / (float) std::max<size_t> (1, packets.size());
                    if (next >= (int) packets.size()) { finish (result.name + " SENT TO THE S1000 (SAMPLE " + std::to_string (number + 1) + ")"); break; }
                    sendPacket (now);
                }
                break;
            case Step::Packets2:
                if (since > 5000) fail ("THE S1000 STOPPED SENDING (GOT " + std::to_string (result.data.size()) + " OF " + std::to_string (expected) + " WORDS)");
                break;
            case Step::Dump:
                progress = (float) std::min (1.0, since / std::max (1.0, wireMs));
                if (since > wireMs + 1500)
                {
                    out.push_back (s900::sampleParams (ch, number, result));
                    step = Step::Params; sentAt = now;
                }
                break;
            case Step::Params:
                if (since > 600) { out.push_back (s900::requestCatalog (ch)); step = Step::Confirm; sentAt = now; }
                break;
            case Step::Confirm:
                if (since > 3000) finish ("SENT " + result.name + " - THE S950 DIDN'T ANSWER TO CONFIRM IT");
                break;
            case Step::AwaitDump:
                if (now - ackAt >= 40) { out.push_back (s900::ack()); ackAt = now; }   // (in case it waits for them)
                if (since > 180000) fail ("NOTHING CAME FROM THE S950 - SEE HELP: SAMPLES OVER MIDI");
                break;
            default: break;
        }
    }
    std::vector<std::vector<uint8_t>> o;
    o.swap (out);
    return o;
}
} // namespace prv::akai
