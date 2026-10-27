// Paula's Revenge - module import/export (plain C++)
#include "ModuleIO.h"
#include <cstring>
#include <cctype>

namespace prv
{
namespace
{
    // Bounds-checked big-endian reader
    struct Reader
    {
        const std::vector<uint8_t>& d;
        bool bad = false;
        explicit Reader (const std::vector<uint8_t>& data) : d (data) {}
        bool has (size_t off, size_t n) const { return off <= d.size() && n <= d.size() - off; }
        uint8_t  u8  (size_t o) { if (! has (o, 1)) { bad = true; return 0; } return d[o]; }
        int8_t   s8  (size_t o) { return (int8_t) u8 (o); }
        uint16_t u16 (size_t o) { if (! has (o, 2)) { bad = true; return 0; } return (uint16_t) ((d[o] << 8) | d[o + 1]); }
        uint32_t u32 (size_t o) { if (! has (o, 4)) { bad = true; return 0; } return ((uint32_t) d[o] << 24) | ((uint32_t) d[o + 1] << 16) | ((uint32_t) d[o + 2] << 8) | d[o + 3]; }
        std::string str (size_t o, size_t n)
        {
            std::string s;
            for (size_t i = 0; i < n && has (o + i, 1); ++i)
            {
                const char c = (char) d[o + i];
                if (c == 0) break;
                s += (c >= 32 && c < 127) ? c : ' ';
            }
            while (! s.empty() && s.back() == ' ') s.pop_back();
            return s;
        }
    };

    void put8  (std::vector<uint8_t>& v, uint8_t x)  { v.push_back (x); }
    void put16 (std::vector<uint8_t>& v, uint16_t x) { v.push_back ((uint8_t) (x >> 8)); v.push_back ((uint8_t) x); }
    void putStr (std::vector<uint8_t>& v, const std::string& s, size_t n)
    {
        for (size_t i = 0; i < n; ++i) v.push_back (i < s.size() ? (uint8_t) s[i] : 0);
    }

    int bcdToInt (int v) { return (v >> 4) * 10 + (v & 15); }
}

// ===========================================================================
IOResult loadModule (const std::vector<uint8_t>& bytes, Song& out)
{
    if (bytes.size() >= 4 && bytes[0] == 'M' && bytes[1] == 'M' && bytes[2] == 'D')
        return loadMED (bytes, out);
    return loadMOD (bytes, out);
}

// ===========================================================================
IOResult loadMOD (const std::vector<uint8_t>& bytes, Song& out)
{
    IOResult res;
    Reader r (bytes);
    if (bytes.size() < 600) { res.error = "File is too small to be a module."; return res; }

    int channels = 4, numSamples = 31;
    size_t hdrSongLen = 950, hdrOrders = 952, patStart = 1084;

    const std::string tag = bytes.size() >= 1084 ? std::string (bytes.begin() + 1080, bytes.begin() + 1084) : std::string();
    if (tag == "M.K." || tag == "M!K!" || tag == "FLT4" || tag == "4CHN") channels = 4;
    else if (tag == "6CHN") channels = 6;
    else if (tag == "8CHN" || tag == "FLT8" || tag == "CD81" || tag == "OKTA" || tag == "OCTA") channels = 8;
    else if (tag.size() == 4 && std::isdigit ((unsigned char) tag[0]) && tag[1] == 'C' && tag[2] == 'H' && tag[3] == 'N') channels = tag[0] - '0';
    else if (tag.size() == 4 && std::isdigit ((unsigned char) tag[0]) && std::isdigit ((unsigned char) tag[1]) && tag[2] == 'C' && tag[3] == 'H')
        channels = (tag[0] - '0') * 10 + (tag[1] - '0');
    else
    {
        // Old 15-sample SoundTracker module
        numSamples = 15; hdrSongLen = 470; hdrOrders = 472; patStart = 600;
        res.warnings.push_back ("No format tag found - loading as a 15-sample SoundTracker module.");
    }
    if (channels < 1 || channels > 32) { res.error = "Unsupported channel count."; return res; }
    if (channels > kMaxTracks) res.warnings.push_back ("Module has " + std::to_string (channels) + " channels - only the first 8 are kept.");

    Song s;
    s.name = r.str (0, 20);
    if (s.name.empty()) s.name = "UNTITLED";
    s.numTracks = channels <= 4 ? 4 : kMaxTracks;

    int sampleLen[31] = {};
    for (int i = 0; i < numSamples; ++i)
    {
        const size_t o = 20 + (size_t) i * 30;
        auto& in = s.instruments[(size_t) i];
        in.name = r.str (o, 22);
        sampleLen[i] = r.u16 (o + 22) * 2;
        const int ft = r.u8 (o + 24) & 15;
        in.finetune = ft > 7 ? ft - 16 : ft;
        in.volume = std::min ((int) r.u8 (o + 25), 64);
        in.loopStart = r.u16 (o + 26) * 2;
        in.loopLength = r.u16 (o + 28) * 2;
        if (in.loopLength <= 2) { in.loopStart = 0; in.loopLength = 0; }
    }

    const int songLen = std::clamp ((int) r.u8 (hdrSongLen), 1, 128);
    int maxPat = 0;
    for (int i = 0; i < 128; ++i) maxPat = std::max (maxPat, (int) r.u8 (hdrOrders + (size_t) i));
    const int numPatterns = maxPat + 1;

    s.sequence.clear();
    for (int i = 0; i < songLen; ++i) s.sequence.push_back (r.u8 (hdrOrders + (size_t) i));

    const size_t patBytes = (size_t) 64 * (size_t) channels * 4;
    s.blocks.assign ((size_t) numPatterns, Block{});
    for (int p = 0; p < numPatterns; ++p)
    {
        auto& b = s.blocks[(size_t) p];
        b.lines = 64;
        const size_t base = patStart + (size_t) p * patBytes;
        for (int row = 0; row < 64; ++row)
            for (int ch = 0; ch < channels; ++ch)
            {
                const size_t o = base + ((size_t) row * (size_t) channels + (size_t) ch) * 4;
                const uint8_t b0 = r.u8 (o), b1 = r.u8 (o + 1), b2 = r.u8 (o + 2), b3 = r.u8 (o + 3);
                if (ch >= kMaxTracks) continue;
                Cell c;
                const int period = ((b0 & 0x0F) << 8) | b1;
                c.instr = (uint8_t) ((b0 & 0xF0) | (b2 >> 4));
                if (c.instr > kMaxInstruments) c.instr = 0;
                c.note = period ? (uint8_t) noteForPeriod (period) : 0;
                c.cmd = b2 & 0x0F;
                c.param = b3;
                b.at (row, ch) = c;
            }
    }

    // Samples follow the patterns
    size_t off = patStart + (size_t) numPatterns * patBytes;
    bool truncated = false;
    for (int i = 0; i < numSamples; ++i)
    {
        auto& in = s.instruments[(size_t) i];
        int len = sampleLen[i];
        if (len <= 0) continue;
        if (off >= bytes.size()) { truncated = true; break; }
        if (off + (size_t) len > bytes.size()) { len = (int) (bytes.size() - off); truncated = true; }
        in.data.resize ((size_t) len);
        for (int k = 0; k < len; ++k) in.data[(size_t) k] = (int16_t) ((int8_t) bytes[off + (size_t) k] * 256);
        off += (size_t) sampleLen[i];
        if (in.loopStart + in.loopLength > len)
        {
            // some old modules store the loop start in bytes
            if (in.loopStart / 2 + in.loopLength <= len) in.loopStart /= 2;
            else in.loopLength = std::max (0, len - in.loopStart);
        }
        if (in.loopLength < 4) { in.loopStart = 0; in.loopLength = 0; }
    }
    if (truncated) res.warnings.push_back ("Sample data is shorter than the header says - some samples are cut off.");

    out = std::move (s);
    res.ok = true;
    return res;
}

// ===========================================================================
IOResult loadMED (const std::vector<uint8_t>& bytes, Song& out)
{
    IOResult res;
    Reader r (bytes);
    if (bytes.size() < 52) { res.error = "File is too small to be a MED module."; return res; }
    const std::string id (bytes.begin(), bytes.begin() + 4);
    const bool mmd1 = (id == "MMD1");
    if (id != "MMD0" && ! mmd1)
    {
        res.error = "This MED format (" + id + ") isn't supported yet - MMD0 and MMD1 are.";
        return res;
    }

    const uint32_t songPtr  = r.u32 (8);
    const uint32_t blockArr = r.u32 (16);
    const uint32_t smplArr  = r.u32 (24);
    const uint32_t expData  = r.u32 (32);
    if (r.bad || ! r.has (songPtr, 788)) { res.error = "MED song header is damaged."; return res; }

    Song s;
    s.name = "UNTITLED";

    // --- song structure
    const int numBlocks = r.u16 (songPtr + 504);
    const int songLen   = r.u16 (songPtr + 506);
    const int defTempo  = r.u16 (songPtr + 764);
    const int playTransp = r.s8 (songPtr + 766);
    const uint8_t flags  = r.u8 (songPtr + 767);
    const uint8_t flags2 = r.u8 (songPtr + 768);
    const int tempo2     = r.u8 (songPtr + 769);
    const int numSamples = std::min ((int) r.u8 (songPtr + 787), kMaxInstruments);
    const bool volHex = (flags & 0x10) != 0;

    s.ledFilterAtStart = (flags & 0x01) != 0;
    s.speed = std::clamp (tempo2 > 0 ? tempo2 : 6, 1, 31);

    // tempo conversion (best effort)
    if (flags2 & 0x20)
    {
        const int rpb = (flags2 & 0x1F) + 1;
        s.tempo = std::clamp (defTempo * rpb / 4, 32, 255);
    }
    else if (flags & 0x40)
    {
        static const int eightChTempo[10] = { 179, 164, 152, 141, 131, 123, 116, 110, 104, 99 };
        s.tempo = eightChTempo[std::clamp (defTempo, 1, 10) - 1];
    }
    else if (defTempo <= 10)
    {
        s.tempo = 125;
        s.speed = std::clamp (defTempo, 1, 31);
    }
    else
    {
        s.tempo = std::clamp ((int) std::lround (defTempo * 125.0 / 33.0), 32, 255);
    }

    for (int i = 0; i < 63; ++i)
    {
        const size_t o = songPtr + (size_t) i * 8;
        auto& in = s.instruments[(size_t) i];
        in.loopStart = r.u16 (o) * 2;
        in.loopLength = r.u16 (o + 2) * 2;
        in.volume = std::min ((int) r.u8 (o + 6), 64);
        in.transpose = r.s8 (o + 7);
    }

    s.sequence.clear();
    for (int i = 0; i < std::min (songLen, 256); ++i) s.sequence.push_back (r.u8 (songPtr + 508 + (size_t) i));
    if (s.sequence.empty()) s.sequence.push_back (0);

    // --- blocks
    if (numBlocks <= 0) { res.error = "MED module has no blocks."; return res; }
    s.blocks.assign ((size_t) std::min (numBlocks, kMaxBlocks), Block{});
    int maxTracks = 1;
    bool droppedTracks = false, unknownCmds = false;

    auto convertCmd = [&] (int cmd, int data, Cell& c)
    {
        c.cmd = 0; c.param = 0;
        switch (cmd)
        {
            case 0x00: c.cmd = 0x0; c.param = (uint8_t) data; break;
            case 0x01: case 0x02: case 0x03: case 0x04: case 0x05: case 0x06: case 0x07:
            case 0x0A: case 0x0B:
                c.cmd = (uint8_t) cmd; c.param = (uint8_t) data; break;
            case 0x08: case 0x0E: break; // hold/decay, synth jump: not used here
            case 0x09: if (data > 0 && data < 0x20) { c.cmd = 0xF; c.param = (uint8_t) data; } break;
            case 0x0C:
            {
                int vol = volHex ? data : bcdToInt (data);
                c.cmd = 0xC; c.param = (uint8_t) std::min (vol, 64);
                break;
            }
            case 0x0D: c.cmd = 0xA; c.param = (uint8_t) data; break;
            case 0x0F:
                if (data == 0x00) { c.cmd = 0xD; c.param = 0; }
                else if (data <= 0xF0)
                {
                    int bpm = (flags2 & 0x20) ? data * ((flags2 & 0x1F) + 1) / 4 : (int) std::lround (data * 125.0 / 33.0);
                    if (data <= 10 && ! (flags2 & 0x20)) { c.cmd = 0xF; c.param = (uint8_t) data; }
                    else { c.cmd = 0xF; c.param = (uint8_t) std::clamp (bpm, 0x20, 0xFF); }
                }
                else if (data == 0xF1) { c.cmd = 0xE; c.param = (uint8_t) (0x90 | std::clamp (s.speed / 2, 1, 15)); }
                else if (data == 0xF2) { c.cmd = 0xE; c.param = (uint8_t) (0xD0 | std::clamp (s.speed / 2, 1, 15)); }
                else if (data == 0xF3) { c.cmd = 0xE; c.param = (uint8_t) (0x90 | std::clamp (s.speed / 3, 1, 15)); }
                else if (data == 0xF8) { c.cmd = 0xE; c.param = 0x01; }
                else if (data == 0xF9) { c.cmd = 0xE; c.param = 0x00; }
                else if (data == 0xFE) { c.cmd = 0xF; c.param = 0x00; }
                else if (data == 0xFF) { c.note = kNoteOff; }
                else unknownCmds = true;
                break;
            case 0x11: c.cmd = 0xE; c.param = (uint8_t) (0x10 | (data & 15)); break;
            case 0x12: c.cmd = 0xE; c.param = (uint8_t) (0x20 | (data & 15)); break;
            case 0x14: c.cmd = 0x4; c.param = (uint8_t) data; break;
            case 0x15: c.cmd = 0xE; c.param = (uint8_t) (0x50 | (data & 15)); break;
            case 0x16: c.cmd = 0xE; c.param = (uint8_t) (0x60 | (data & 15)); break;
            case 0x18: c.cmd = 0xE; c.param = (uint8_t) (0xC0 | (data & 15)); break;
            case 0x19: c.cmd = 0x9; c.param = (uint8_t) data; break;
            case 0x1A: c.cmd = 0xE; c.param = (uint8_t) (0xA0 | (data & 15)); break;
            case 0x1B: c.cmd = 0xE; c.param = (uint8_t) (0xB0 | (data & 15)); break;
            case 0x1D: c.cmd = 0xD; c.param = (uint8_t) (((data / 10) << 4) | (data % 10)); break;
            case 0x1E: c.cmd = 0xE; c.param = (uint8_t) (0xE0 | (data & 15)); break;
            case 0x1F:
                if (data >> 4) { c.cmd = 0xE; c.param = (uint8_t) (0xD0 | (data >> 4)); }
                else if (data & 15) { c.cmd = 0xE; c.param = (uint8_t) (0x90 | (data & 15)); }
                break;
            default: unknownCmds = true; break;
        }
    };

    for (int bi = 0; bi < (int) s.blocks.size(); ++bi)
    {
        const uint32_t bp = r.u32 (blockArr + (size_t) bi * 4);
        auto& blk = s.blocks[(size_t) bi];
        if (bp == 0 || ! r.has (bp, 4)) { blk.lines = 64; continue; }

        int tracks, lines; size_t dataOff;
        if (mmd1) { tracks = r.u16 (bp); lines = r.u16 (bp + 2) + 1; dataOff = bp + 8; }
        else      { tracks = r.u8 (bp);  lines = r.u8 (bp + 1) + 1;  dataOff = bp + 2; }
        if (lines > kMaxLines) { lines = kMaxLines; res.warnings.push_back ("A block longer than 256 lines was cut short."); }
        blk.lines = std::max (1, lines);
        if (tracks > kMaxTracks) droppedTracks = true;
        maxTracks = std::max (maxTracks, std::min (tracks, kMaxTracks));

        const int stride = mmd1 ? 4 : 3;
        for (int row = 0; row < blk.lines; ++row)
            for (int t = 0; t < tracks; ++t)
            {
                const size_t o = dataOff + ((size_t) row * (size_t) tracks + (size_t) t) * (size_t) stride;
                int note, instr, cmd, data;
                if (mmd1)
                {
                    note = r.u8 (o) & 0x7F; instr = r.u8 (o + 1) & 0x3F; cmd = r.u8 (o + 2); data = r.u8 (o + 3);
                }
                else
                {
                    const uint8_t b0 = r.u8 (o), b1 = r.u8 (o + 1), b2 = r.u8 (o + 2);
                    note = b0 & 0x3F;
                    instr = (b1 >> 4) | ((b0 & 0x80) >> 3) | ((b0 & 0x40) >> 1);
                    cmd = b1 & 0x0F; data = b2;
                }
                if (t >= kMaxTracks) continue;
                Cell c;
                if (note > 0) c.note = (uint8_t) std::clamp (note + 12 + playTransp, 1, (int) kNoteMax);
                c.instr = (uint8_t) (instr <= kMaxInstruments ? instr : 0);
                convertCmd (cmd, data, c);
                blk.at (row, t) = c;
            }
    }
    s.numTracks = maxTracks <= 4 ? 4 : kMaxTracks;
    if (droppedTracks) res.warnings.push_back ("Song has more than 8 tracks - only the first 8 are kept.");
    if (unknownCmds) res.warnings.push_back ("Some MED-only commands have no equivalent and were left out.");

    // --- samples
    bool skippedSynth = false;
    for (int i = 0; i < numSamples; ++i)
    {
        const uint32_t ip = r.u32 (smplArr + (size_t) i * 4);
        if (ip == 0 || ! r.has (ip, 6)) continue;
        const uint32_t len = r.u32 (ip);
        const int16_t type = (int16_t) r.u16 (ip + 4);
        if (type != 0) { skippedSynth = true; continue; }
        auto& in = s.instruments[(size_t) i];
        const size_t avail = std::min ((size_t) len, bytes.size() - std::min (bytes.size(), (size_t) ip + 6));
        in.data.resize (avail);
        for (size_t k = 0; k < avail; ++k) in.data[k] = (int16_t) ((int8_t) bytes[ip + 6 + k] * 256);
        if (in.loopLength <= 2 || in.loopStart + in.loopLength > (int) in.data.size())
        {
            in.loopLength = std::max (0, std::min (in.loopLength, (int) in.data.size() - in.loopStart));
            if (in.loopLength <= 2) { in.loopStart = 0; in.loopLength = 0; }
        }
    }
    if (skippedSynth) res.warnings.push_back ("Synth/hybrid and multi-octave instruments aren't supported and were skipped.");

    // --- expansion data: names, finetune, song name
    if (expData != 0 && r.has (expData, 52))
    {
        const uint32_t expSmp = r.u32 (expData + 4);
        const int sEntries = r.u16 (expData + 8), sSize = r.u16 (expData + 10);
        if (expSmp && sSize >= 4)
            for (int i = 0; i < std::min (sEntries, kMaxInstruments); ++i)
            {
                const int ft = r.s8 (expSmp + (size_t) i * (size_t) sSize + 3);
                s.instruments[(size_t) i].finetune = std::clamp (ft, -8, 7);
            }
        const uint32_t iinfo = r.u32 (expData + 20);
        const int iEntries = r.u16 (expData + 24), iSize = r.u16 (expData + 26);
        if (iinfo && iSize >= 40)
            for (int i = 0; i < std::min (iEntries, kMaxInstruments); ++i)
                s.instruments[(size_t) i].name = r.str (iinfo + (size_t) i * (size_t) iSize, 40);
        const uint32_t nameP = r.u32 (expData + 44);
        const uint32_t nameL = r.u32 (expData + 48);
        if (nameP && nameL) { auto n = r.str (nameP, std::min<uint32_t> (nameL, 64)); if (! n.empty()) s.name = n; }
    }

    for (auto& q : s.sequence) q = std::clamp (q, 0, (int) s.blocks.size() - 1);
    out = std::move (s);
    res.ok = true;
    return res;
}

// ===========================================================================
IOResult saveMOD (const Song& song, std::vector<uint8_t>& out)
{
    IOResult res;
    out.clear();

    // Use 4 channels when the upper tracks are empty
    bool upperUsed = false;
    if (song.numTracks > 4)
        for (const auto& b : song.blocks)
            for (int l = 0; l < b.lines && ! upperUsed; ++l)
                for (int t = 4; t < kMaxTracks; ++t)
                    if (! b.at (l, t).isEmpty()) { upperUsed = true; break; }
    const int channels = upperUsed ? 8 : 4;

    bool tooLong = false, instrTooHigh = false, bigSample = false;
    for (const auto& b : song.blocks) if (b.lines > 64) tooLong = true;

    putStr (out, song.name, 20);
    for (int i = 0; i < 31; ++i)
    {
        const auto& in = song.instruments[(size_t) i];
        int len = (int) std::min<size_t> (in.data.size(), 131070) & ~1;
        if (in.data.size() > 131070) bigSample = true;
        putStr (out, in.name, 22);
        put16 (out, (uint16_t) (len / 2));
        put8 (out, (uint8_t) (in.finetune & 15));
        put8 (out, (uint8_t) std::clamp (in.volume, 0, 64));
        if (in.isLooped() && in.loopStart + in.loopLength <= len)
        {
            put16 (out, (uint16_t) (in.loopStart / 2));
            put16 (out, (uint16_t) std::max (1, in.loopLength / 2));
        }
        else { put16 (out, 0); put16 (out, 1); }
    }
    for (int i = 31; i < kMaxInstruments; ++i) if (song.instruments[(size_t) i].hasData()) instrTooHigh = true;

    const int numPatterns = std::min ((int) song.blocks.size(), 128);
    const int songLen = std::clamp ((int) song.sequence.size(), 1, 128);
    put8 (out, (uint8_t) songLen);
    put8 (out, 127);
    for (int i = 0; i < 128; ++i)
        put8 (out, (uint8_t) (i < (int) song.sequence.size() ? std::min (song.sequence[(size_t) i], numPatterns - 1) : 0));
    if (channels == 4) { out.push_back ('M'); out.push_back ('.'); out.push_back ('K'); out.push_back ('.'); }
    else               { out.push_back ('8'); out.push_back ('C'); out.push_back ('H'); out.push_back ('N'); }

    for (int p = 0; p < numPatterns; ++p)
    {
        const Block& b = song.blocks[(size_t) p];
        const int lines = std::min (b.lines, 64);
        // a shorter block ends with a pattern break on a free command slot
        int breakTrack = -1;
        if (lines < 64)
        {
            for (int t = 0; t < channels; ++t) if (b.at (lines - 1, t).cmd == 0 && b.at (lines - 1, t).param == 0) { breakTrack = t; break; }
            if (breakTrack < 0) breakTrack = channels - 1;
        }
        for (int row = 0; row < 64; ++row)
            for (int ch = 0; ch < channels; ++ch)
            {
                Cell c = row < lines ? b.at (row, ch) : Cell{};
                if (row == lines - 1 && ch == breakTrack) { c.cmd = 0xD; c.param = 0; }
                int period = 0;
                if (c.note == kNoteOff) { if (c.cmd == 0 && c.param == 0) { c.cmd = 0xC; c.param = 0; } }
                else if (c.note > 0) period = std::clamp (periodForInt (c.note, 0), 1, 4095);
                int instr = c.instr;
                if (instr > 31) { instr = 0; instrTooHigh = true; }
                put8 (out, (uint8_t) ((instr & 0xF0) | ((period >> 8) & 0x0F)));
                put8 (out, (uint8_t) (period & 0xFF));
                put8 (out, (uint8_t) (((instr & 0x0F) << 4) | (c.cmd & 0x0F)));
                put8 (out, c.param);
            }
    }

    for (int i = 0; i < 31; ++i)
    {
        const auto& in = song.instruments[(size_t) i];
        const int len = (int) std::min<size_t> (in.data.size(), 131070) & ~1;
        for (int k = 0; k < len; ++k)
        {
            int v = in.data[(size_t) k];
            if (in.isStereo()) v = (v + in.dataR[(size_t) k]) / 2;   // MOD samples are mono
            put8 (out, (uint8_t) (int8_t) (v >> 8));
        }
    }

    bool anyStereo = false;
    for (int i = 0; i < 31; ++i) anyStereo |= song.instruments[(size_t) i].isStereo();
    if (anyStereo) res.warnings.push_back ("Stereo samples were mixed down to mono (MOD limit).");
    bool anyShaping = false;
    for (int i = 0; i < 31; ++i) anyShaping |= song.instruments[(size_t) i].hasShaping();
    for (int t = 0; t < kMaxTracks; ++t) anyShaping |= song.trackPan[t] >= 0;
    if (anyShaping) res.warnings.push_back ("Envelopes, filters and pans can't be stored in a MOD and were left out.");
    if (song.blocks.size() > 128) res.warnings.push_back ("MOD files hold 128 patterns at most - extra blocks were left out.");
    if (tooLong) res.warnings.push_back ("Blocks longer than 64 lines were cut to 64 (MOD limit).");
    if (instrTooHigh) res.warnings.push_back ("Instruments above 31 can't go in a MOD and were left out.");
    if (bigSample) res.warnings.push_back ("Samples over 128 KB were shortened (MOD limit).");
    res.ok = true;
    return res;
}

// ===========================================================================
bool load8SVX (const std::vector<uint8_t>& bytes, Instrument& out, double& sampleRate)
{
    Reader r (bytes);
    if (bytes.size() < 12 || std::memcmp (bytes.data(), "FORM", 4) != 0 || std::memcmp (bytes.data() + 8, "8SVX", 4) != 0)
        return false;

    uint32_t oneShot = 0, repeat = 0; int rate = 8363, vol = 64;
    bool gotBody = false;
    std::string name;
    size_t o = 12;
    while (o + 8 <= bytes.size())
    {
        const std::string id (bytes.begin() + (long) o, bytes.begin() + (long) o + 4);
        const uint32_t len = r.u32 (o + 4);
        const size_t body = o + 8;
        if (id == "VHDR")
        {
            oneShot = r.u32 (body); repeat = r.u32 (body + 4);
            rate = r.u16 (body + 12);
            const uint8_t compression = r.u8 (body + 15);
            if (compression != 0) return false;
            vol = (int) (r.u32 (body + 16) * 64 / 65536);
        }
        else if (id == "NAME") name = r.str (body, len);
        else if (id == "BODY")
        {
            const size_t n = std::min ((size_t) len, bytes.size() - body);
            out.data.resize (n);
            for (size_t k = 0; k < n; ++k) out.data[k] = (int16_t) ((int8_t) bytes[body + k] * 256);
            gotBody = true;
        }
        o = body + len + (len & 1);
    }
    if (! gotBody) return false;
    out.name = name;
    out.volume = std::clamp (vol, 0, 64);
    if (repeat > 2 && oneShot + repeat <= out.data.size()) { out.loopStart = (int) oneShot; out.loopLength = (int) repeat; }
    else { out.loopStart = 0; out.loopLength = 0; }
    sampleRate = rate > 0 ? rate : 8363;
    return true;
}

} // namespace prv
