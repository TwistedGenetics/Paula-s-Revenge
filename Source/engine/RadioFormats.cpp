// Paula's Revenge - internet radio formats: AAC (ADTS), transport streams, fMP4, HLS (see RadioFormats.h)
#include "RadioFormats.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>

namespace prv::radio
{
namespace
{
    const int kRates[13] = { 96000, 88200, 64000, 48000, 44100, 32000, 24000, 22050, 16000, 12000, 11025, 8000, 7350 };
    uint32_t be32 (const uint8_t* p) { return ((uint32_t) p[0] << 24) | ((uint32_t) p[1] << 16) | ((uint32_t) p[2] << 8) | p[3]; }
    uint64_t be64 (const uint8_t* p) { return ((uint64_t) be32 (p) << 32) | be32 (p + 4); }
    std::string lower (std::string s) { for (auto& c : s) c = (char) std::tolower ((unsigned char) c); return s; }
    std::string trim (const std::string& s)
    {
        size_t a = 0, b = s.size();
        while (a < b && (unsigned char) s[a] <= ' ') ++a;
        while (b > a && (unsigned char) s[b - 1] <= ' ') --b;
        return s.substr (a, b - a);
    }
}

// ---------------------------------------------------------------- ADTS
int AdtsHeader::sampleRate() const { return rateIndex >= 0 && rateIndex < 13 ? kRates[rateIndex] : 0; }

int rateIndexFor (int hz)
{
    for (int i = 0; i < 13; ++i) if (kRates[i] == hz) return i;
    return -1;
}

bool parseAdts (const uint8_t* p, size_t n, AdtsHeader& h)
{
    if (n < 7 || p[0] != 0xFF || (p[1] & 0xF6) != 0xF0) return false;   // 12-bit sync, layer 00
    h.profile = (p[2] >> 6) & 3;
    h.rateIndex = (p[2] >> 2) & 0xF;
    if (h.rateIndex > 12) return false;
    h.channels = ((p[2] & 1) << 2) | ((p[3] >> 6) & 3);
    h.frameLength = ((p[3] & 3) << 11) | (p[4] << 3) | ((p[5] >> 5) & 7);
    h.headerLength = (p[1] & 1) ? 7 : 9;
    return h.frameLength >= h.headerLength + 1;
}

std::vector<uint8_t> makeAdts (int objectType, int rateIndex, int channels, size_t payload)
{
    // (HE-AAC goes as its AAC LC core: the decoder finds the SBR in it)
    const int profile = (objectType == 5 || objectType == 29 || objectType <= 0 || objectType > 4) ? 1 : objectType - 1;
    const size_t len = payload + 7;
    std::vector<uint8_t> h (7);
    h[0] = 0xFF;
    h[1] = 0xF1;
    h[2] = (uint8_t) ((profile << 6) | ((rateIndex & 0xF) << 2) | ((channels >> 2) & 1));
    h[3] = (uint8_t) (((channels & 3) << 6) | ((len >> 11) & 3));
    h[4] = (uint8_t) ((len >> 3) & 0xFF);
    h[5] = (uint8_t) (((len & 7) << 5) | 0x1F);
    h[6] = 0xFC;
    return h;
}

size_t id3Size (const uint8_t* p, size_t n)
{
    if (n < 10 || p[0] != 'I' || p[1] != 'D' || p[2] != '3') return 0;
    if ((p[6] | p[7] | p[8] | p[9]) & 0x80) return 0;
    const size_t size = ((size_t) p[6] << 21) | ((size_t) p[7] << 14) | ((size_t) p[8] << 7) | p[9];
    return 10 + size + ((p[5] & 0x10) ? 10 : 0);
}

void AdtsSplitter::feed (const uint8_t* data, size_t n, std::vector<std::vector<uint8_t>>& frames)
{
    buf.insert (buf.end(), data, data + n);
    size_t pos = 0;
    while (buf.size() - pos >= 10)
    {
        const uint8_t* p = buf.data() + pos;
        const size_t left = buf.size() - pos;
        if (const size_t tag = id3Size (p, left))
        {
            if (tag > left) { if (tag > (1u << 20)) { pos += 3; junk += 3; continue; } break; }   // (wait for the rest of it)
            pos += tag;
            continue;
        }
        AdtsHeader h;
        if (! parseAdts (p, left, h)) { ++pos; ++junk; continue; }
        if ((size_t) h.frameLength > left) break;   // (the rest of the frame is still coming)
        // the next frame should start right after it (when it's here to look at)
        AdtsHeader next;
        if ((size_t) h.frameLength + 7 <= left && ! parseAdts (p + h.frameLength, left - (size_t) h.frameLength, next)
            && id3Size (p + h.frameLength, left - (size_t) h.frameLength) == 0)
        { ++pos; ++junk; continue; }
        frames.emplace_back (p, p + h.frameLength);
        pos += (size_t) h.frameLength;
    }
    buf.erase (buf.begin(), buf.begin() + (std::ptrdiff_t) pos);
    if (buf.size() > (1u << 20)) { junk += (int64_t) buf.size(); buf.clear(); }
}

int AacDecoder::feed (const uint8_t* data, size_t n, std::vector<float>& left, std::vector<float>& right)
{
    std::vector<std::vector<uint8_t>> frames;
    split.feed (data, n, frames);
    int added = 0;
    for (auto& f : frames)
    {
        if (backend == nullptr) backend = makeAacBackend();
        if (backend == nullptr) return added;
        const size_t before = left.size();
        if (! backend->decode (f.data(), f.size(), left, right)) ++failed;
        added += (int) (left.size() - before);
        if (backend->sampleRate() > 0) rate = backend->sampleRate();
    }
    return added;
}

// ---------------------------------------------------------------- transport stream
void TsDemux::reset()
{
    buf.clear(); pes.clear();
    pmtPid = audioPid = -1; type = 0;
    kind = Audio::None;
    inPes = false;
}

void TsDemux::feed (const uint8_t* data, size_t n, std::vector<uint8_t>& audio)
{
    buf.insert (buf.end(), data, data + n);
    size_t pos = 0;
    while (buf.size() - pos >= 188)
    {
        if (buf[pos] != 0x47 || (buf.size() - pos >= 189 + 188 && buf[pos + 188] != 0x47)) { ++pos; continue; }
        packet (buf.data() + pos, audio);
        pos += 188;
    }
    buf.erase (buf.begin(), buf.begin() + (std::ptrdiff_t) pos);
}

void TsDemux::flushPes (std::vector<uint8_t>& audio)
{
    if (pes.size() >= 9 && pes[0] == 0 && pes[1] == 0 && pes[2] == 1)
    {
        const size_t hdr = 9 + (size_t) pes[8];
        if (pes.size() > hdr) audio.insert (audio.end(), pes.begin() + (std::ptrdiff_t) hdr, pes.end());
    }
    pes.clear();
    inPes = false;
}

void TsDemux::packet (const uint8_t* p, std::vector<uint8_t>& audio)
{
    const int pid = ((p[1] & 0x1F) << 8) | p[2];
    const bool start = (p[1] & 0x40) != 0;
    const int afc = (p[3] >> 4) & 3;
    if (afc == 0 || afc == 2) return;   // (no payload)
    size_t at = 4;
    if (afc == 3) at += 1 + (size_t) p[4];
    if (at >= 188) return;
    const uint8_t* pay = p + at;
    const size_t len = 188 - at;
    auto section = [&] () -> const uint8_t*
    {
        if (! start || len < 1 + (size_t) pay[0] + 8) return nullptr;
        return pay + 1 + pay[0];
    };
    if (pid == 0)
    {
        // PAT: the first programme's PMT
        if (auto* s = section())
        {
            const size_t sl = (size_t) (((s[1] & 0x0F) << 8) | s[2]);
            const uint8_t* end = s + std::min (3 + sl - 4, (size_t) (p + 188 - s));
            for (const uint8_t* e = s + 8; e + 4 <= end; e += 4)
                if (((e[0] << 8) | e[1]) != 0) { pmtPid = ((e[2] & 0x1F) << 8) | e[3]; break; }
        }
        return;
    }
    if (pid == pmtPid && audioPid < 0)
    {
        // PMT: the first AAC or MP3 stream (else the first audio-ish one)
        if (auto* s = section())
        {
            const size_t sl = (size_t) (((s[1] & 0x0F) << 8) | s[2]);
            const uint8_t* end = s + std::min (3 + sl - 4, (size_t) (p + 188 - s));
            const size_t pil = (size_t) (((s[10] & 0x0F) << 8) | s[11]);
            int otherPid = -1, otherType = 0;
            for (const uint8_t* e = s + 12 + pil; e + 5 <= end; e += 5 + (size_t) (((e[3] & 0x0F) << 8) | e[4]))
            {
                const int st = e[0], epid = ((e[1] & 0x1F) << 8) | e[2];
                if (st == 0x0F || st == 0x03 || st == 0x04)
                {
                    audioPid = epid; type = st;
                    kind = st == 0x0F ? Audio::Aac : Audio::Mp3;
                    break;
                }
                if (otherPid < 0 && (st == 0x11 || st == 0x81 || st == 0x87 || st == 0x06)) { otherPid = epid; otherType = st; }
            }
            if (audioPid < 0 && otherPid >= 0) { audioPid = otherPid; type = otherType; kind = Audio::Other; }
        }
        return;
    }
    if (pid != audioPid || audioPid < 0) return;
    if (start) { flushPes (audio); inPes = true; pes.assign (pay, pay + len); }
    else if (inPes) pes.insert (pes.end(), pay, pay + len);
    // (a PES with its length given: out as soon as it's whole)
    if (inPes && pes.size() >= 6)
    {
        const size_t plen = (size_t) ((pes[4] << 8) | pes[5]);
        if (plen > 0 && pes.size() >= 6 + plen) { pes.resize (6 + plen); flushPes (audio); }
    }
}

// ---------------------------------------------------------------- fragmented MP4
namespace
{
    struct Box { const uint8_t* body; size_t size; uint32_t type; const uint8_t* start; };
    // the boxes in [p, p + n)
    std::vector<Box> boxes (const uint8_t* p, size_t n)
    {
        std::vector<Box> out;
        size_t at = 0;
        while (n - at >= 8)
        {
            uint64_t size = be32 (p + at);
            const uint32_t type = be32 (p + at + 4);
            size_t hdr = 8;
            if (size == 1) { if (n - at < 16) break; size = be64 (p + at + 8); hdr = 16; }
            else if (size == 0) size = n - at;
            if (size < hdr || size > n - at) break;
            out.push_back ({ p + at + hdr, (size_t) size - hdr, type, p + at });
            at += (size_t) size;
        }
        return out;
    }
    constexpr uint32_t fourcc (const char* s) { return ((uint32_t) (uint8_t) s[0] << 24) | ((uint32_t) (uint8_t) s[1] << 16) | ((uint32_t) (uint8_t) s[2] << 8) | (uint8_t) s[3]; }

    // a descriptor's length (1-4 bytes of 7 bits)
    size_t descLen (const uint8_t*& p, const uint8_t* end)
    {
        size_t n = 0;
        for (int i = 0; i < 4 && p < end; ++i) { const uint8_t b = *p++; n = (n << 7) | (b & 0x7F); if (! (b & 0x80)) break; }
        return n;
    }

    bool esds (const uint8_t* p, size_t n, Mp4Track& t)
    {
        const uint8_t* end = p + n;
        p += 4;   // (version, flags)
        if (p >= end || *p++ != 0x03) return false;
        descLen (p, end);
        if (end - p < 3) return false;
        p += 2;
        const uint8_t flags = *p++;
        if (flags & 0x80) p += 2;
        if (flags & 0x40) { if (p >= end) return false; p += 1 + *p; }
        if (flags & 0x20) p += 2;
        if (p >= end || *p++ != 0x04) return false;
        descLen (p, end);
        if (end - p < 13) return false;
        const int oti = *p;
        p += 13;
        if (oti == 0x6B || oti == 0x69) { t.mp3 = true; t.objectType = 0x6B; return true; }
        if (p >= end || *p++ != 0x05) return oti == 0x40;
        const size_t l = descLen (p, end);
        return parseAudioSpecificConfig (p, std::min (l, (size_t) (end - p)), t);
    }

    bool findAudio (const uint8_t* p, size_t n, Mp4Track& t, int depth)
    {
        if (depth > 10) return false;
        for (auto& b : boxes (p, n))
        {
            switch (b.type)
            {
                case fourcc ("moov"): case fourcc ("trak"): case fourcc ("mdia"): case fourcc ("minf"): case fourcc ("stbl"):
                    if (findAudio (b.body, b.size, t, depth + 1)) return true;
                    break;
                case fourcc ("stsd"):
                    if (b.size > 8 && findAudio (b.body + 8, b.size - 8, t, depth + 1)) return true;
                    break;
                case fourcc ("mp4a"): case fourcc ("enca"):
                    if (b.size > 28)
                    {
                        t.channels = (b.body[16] << 8) | b.body[17];
                        const int hz = (int) (be32 (b.body + 24) >> 16);
                        if (rateIndexFor (hz) >= 0) t.rateIndex = rateIndexFor (hz);
                        for (auto& c : boxes (b.body + 28, b.size - 28))
                            if (c.type == fourcc ("esds") && esds (c.body, c.size, t)) return true;
                        t.objectType = 2;   // (no esds: plain AAC LC)
                        return true;
                    }
                    break;
                case fourcc (".mp3"):
                    t.mp3 = true; t.objectType = 0x6B;
                    return true;
                default: break;
            }
        }
        return false;
    }
}

bool parseAudioSpecificConfig (const uint8_t* p, size_t n, Mp4Track& t)
{
    size_t bit = 0;
    auto get = [&] (int bits)
    {
        uint32_t v = 0;
        for (int i = 0; i < bits; ++i, ++bit)
        {
            if (bit / 8 >= n) return v << (bits - i);
            v = (v << 1) | ((p[bit / 8] >> (7 - bit % 8)) & 1);
        }
        return v;
    };
    if (n < 2) return false;
    int aot = (int) get (5);
    if (aot == 31) aot = 32 + (int) get (6);
    int sfi = (int) get (4);
    if (sfi == 15) { sfi = rateIndexFor ((int) get (24)); if (sfi < 0) sfi = 4; }
    const int ch = (int) get (4);
    if (aot == 5 || aot == 29)
    {
        // explicit HE-AAC: the AAC core underneath (its own rate)
        int ext = (int) get (4);
        if (ext == 15) get (24);
        aot = (int) get (5);
    }
    t.objectType = aot;
    t.rateIndex = sfi;
    t.channels = ch > 0 ? ch : 2;
    return aot >= 1 && aot <= 4;
}

bool parseMp4Init (const uint8_t* p, size_t n, Mp4Track& t)
{
    t = {};
    return findAudio (p, n, t, 0) && t.valid();
}

bool parseMp4Segment (const uint8_t* p, size_t n, const Mp4Track& t, std::vector<uint8_t>& audio)
{
    if (! t.valid()) return false;
    bool any = false;
    const uint8_t* moofStart = nullptr;
    std::vector<uint32_t> sizes;
    int64_t dataOffset = -1;
    for (auto& b : boxes (p, n))
    {
        if (b.type == fourcc ("moof"))
        {
            moofStart = b.start;
            sizes.clear();
            dataOffset = -1;
            uint32_t defSize = 0;
            for (auto& tr : boxes (b.body, b.size))
            {
                if (tr.type != fourcc ("traf")) continue;
                for (auto& c : boxes (tr.body, tr.size))
                {
                    if (c.size < 8) continue;
                    const uint32_t flags = be32 (c.body) & 0xFFFFFF;
                    const uint8_t* q = c.body + 4;
                    const uint8_t* end = c.body + c.size;
                    if (c.type == fourcc ("tfhd"))
                    {
                        q += 4;   // track id
                        if (flags & 0x01) q += 8;
                        if (flags & 0x02) q += 4;
                        if (flags & 0x08) q += 4;
                        if ((flags & 0x10) && q + 4 <= end) defSize = be32 (q);
                    }
                    else if (c.type == fourcc ("trun"))
                    {
                        const uint32_t count = be32 (q); q += 4;
                        if (flags & 0x01) { dataOffset = (int32_t) be32 (q); q += 4; }
                        if (flags & 0x04) q += 4;
                        for (uint32_t i = 0; i < count && q <= end; ++i)
                        {
                            if (flags & 0x100) q += 4;
                            uint32_t sz = defSize;
                            if (flags & 0x200) { if (q + 4 > end) break; sz = be32 (q); q += 4; }
                            if (flags & 0x400) q += 4;
                            if (flags & 0x800) q += 4;
                            sizes.push_back (sz);
                        }
                    }
                }
            }
        }
        else if (b.type == fourcc ("mdat") && ! sizes.empty())
        {
            const uint8_t* at = (moofStart != nullptr && dataOffset >= 0) ? moofStart + dataOffset : b.body;
            const uint8_t* end = b.body + b.size;
            if (at < b.body || at > end) at = b.body;
            for (auto sz : sizes)
            {
                if (sz == 0 || at + sz > end) break;
                if (! t.mp3)
                {
                    const auto h = makeAdts (t.objectType, t.rateIndex, t.channels, sz);
                    audio.insert (audio.end(), h.begin(), h.end());
                }
                audio.insert (audio.end(), at, at + sz);
                at += sz;
                any = true;
            }
            sizes.clear();
        }
    }
    return any;
}

// ---------------------------------------------------------------- HLS
namespace
{
    // NAME=value,NAME="quoted, value"
    std::string attribute (const std::string& attrs, const std::string& name)
    {
        size_t i = 0;
        while (i < attrs.size())
        {
            const size_t eq = attrs.find ('=', i);
            if (eq == std::string::npos) break;
            const std::string key = trim (attrs.substr (i, eq - i));
            size_t j = eq + 1;
            std::string val;
            if (j < attrs.size() && attrs[j] == '"')
            {
                const size_t close = attrs.find ('"', j + 1);
                val = attrs.substr (j + 1, (close == std::string::npos ? attrs.size() : close) - j - 1);
                j = close == std::string::npos ? attrs.size() : close + 1;
            }
            else
            {
                const size_t comma = attrs.find (',', j);
                val = attrs.substr (j, (comma == std::string::npos ? attrs.size() : comma) - j);
                j = comma == std::string::npos ? attrs.size() : comma;
            }
            if (lower (key) == lower (name)) return trim (val);
            i = attrs.find (',', j);
            if (i == std::string::npos) break;
            ++i;
        }
        return {};
    }
}

bool looksLikeHls (const std::string& text)
{
    const auto t = trim (text.substr (0, 64 * 1024));
    return t.rfind ("#EXTM3U", 0) == 0 && t.find ("#EXT-X-") != std::string::npos;
}

HlsPlaylist parseHls (const std::string& text)
{
    HlsPlaylist pl;
    std::vector<std::string> lines;
    for (size_t p = 0; p < text.size();)
    {
        size_t e = text.find_first_of ("\r\n", p);
        if (e == std::string::npos) e = text.size();
        auto l = trim (text.substr (p, e - p));
        if (! l.empty()) lines.push_back (l);
        p = e + 1;
    }
    if (lines.empty() || lines[0].rfind ("#EXTM3U", 0) != 0) return pl;
    pl.valid = true;
    HlsPlaylist::Variant pendingVariant;
    bool wantVariant = false, wantSegment = false, haveDefaultAudio = false;
    double dur = 0;
    int64_t seq = 0;
    for (size_t i = 1; i < lines.size(); ++i)
    {
        const auto& l = lines[i];
        auto after = [&l] (const char* tag) { return l.substr (std::strlen (tag)); };
        if (l.rfind ("#EXT-X-STREAM-INF:", 0) == 0)
        {
            const auto a = after ("#EXT-X-STREAM-INF:");
            pendingVariant = {};
            pendingVariant.bandwidth = std::atoi (attribute (a, "BANDWIDTH").c_str());
            pendingVariant.codecs = lower (attribute (a, "CODECS"));
            wantVariant = true;
        }
        else if (l.rfind ("#EXT-X-MEDIA:", 0) == 0)
        {
            const auto a = after ("#EXT-X-MEDIA:");
            const auto uri = attribute (a, "URI");
            if (lower (attribute (a, "TYPE")) == "audio" && ! uri.empty())
            {
                const bool def = lower (attribute (a, "DEFAULT")) == "yes";
                if (pl.audioUri.empty() || (def && ! haveDefaultAudio)) { pl.audioUri = uri; haveDefaultAudio = def; }
            }
        }
        else if (l.rfind ("#EXT-X-TARGETDURATION:", 0) == 0) pl.targetDuration = std::max (1.0, std::atof (after ("#EXT-X-TARGETDURATION:").c_str()));
        else if (l.rfind ("#EXT-X-MEDIA-SEQUENCE:", 0) == 0) { pl.mediaSequence = std::atoll (after ("#EXT-X-MEDIA-SEQUENCE:").c_str()); seq = pl.mediaSequence; }
        else if (l.rfind ("#EXT-X-ENDLIST", 0) == 0) pl.ended = true;
        else if (l.rfind ("#EXT-X-MAP:", 0) == 0) pl.mapUri = attribute (after ("#EXT-X-MAP:"), "URI");
        else if (l.rfind ("#EXT-X-KEY:", 0) == 0) pl.keyMethod = attribute (after ("#EXT-X-KEY:"), "METHOD");
        else if (l.rfind ("#EXTINF:", 0) == 0) { dur = std::atof (after ("#EXTINF:").c_str()); wantSegment = true; }
        else if (l[0] == '#') continue;
        else if (wantVariant) { pendingVariant.uri = l; pl.variants.push_back (pendingVariant); wantVariant = false; }
        else
        {
            pl.segments.push_back ({ l, wantSegment ? dur : pl.targetDuration, seq++ });
            wantSegment = false;
        }
    }
    pl.master = ! pl.variants.empty() || (! pl.audioUri.empty() && pl.segments.empty());
    return pl;
}

std::string resolveUrl (const std::string& base, const std::string& ref)
{
    if (ref.find ("://") != std::string::npos) return ref;
    const auto scheme = base.find ("://");
    if (scheme == std::string::npos) return ref;
    if (ref.rfind ("//", 0) == 0) return base.substr (0, scheme + 1) + ref;
    const auto hostEnd = base.find ('/', scheme + 3);
    const std::string origin = hostEnd == std::string::npos ? base : base.substr (0, hostEnd);
    if (! ref.empty() && ref[0] == '/') return origin + ref;
    std::string path = hostEnd == std::string::npos ? std::string ("/") : base.substr (hostEnd);
    const auto q = path.find_first_of ("?#");
    if (q != std::string::npos) path.resize (q);
    path = path.substr (0, path.rfind ('/') + 1);
    // ./ and ../
    std::string r = ref;
    while (true)
    {
        if (r.rfind ("./", 0) == 0) { r = r.substr (2); continue; }
        if (r.rfind ("../", 0) == 0)
        {
            r = r.substr (3);
            if (path.size() > 1) { path.pop_back(); path = path.substr (0, path.rfind ('/') + 1); }
            continue;
        }
        break;
    }
    return origin + path + r;
}

int chooseVariant (const HlsPlaylist& pl)
{
    auto video = [] (const std::string& c)
    {
        for (auto v : { "avc", "hvc", "hev", "vp09", "vp8", "av01", "mp4v" }) if (c.find (v) != std::string::npos) return true;
        return false;
    };
    auto playable = [] (const std::string& c)
    {
        if (c.empty()) return true;
        return c.find ("mp4a") != std::string::npos || c.find ("mp3") != std::string::npos;
    };
    int best = -1;
    for (int i = 0; i < (int) pl.variants.size(); ++i)
    {
        const auto& v = pl.variants[(size_t) i];
        if (video (v.codecs) || ! playable (v.codecs)) continue;
        if (best < 0 || v.bandwidth > pl.variants[(size_t) best].bandwidth) best = i;
    }
    if (best >= 0) return best;
    // only video ones: the smallest (its sound is in it too)
    for (int i = 0; i < (int) pl.variants.size(); ++i)
        if (best < 0 || pl.variants[(size_t) i].bandwidth < pl.variants[(size_t) best].bandwidth) best = i;
    return best;
}

// ---------------------------------------------------------------- any of them
StreamDecoder::StreamDecoder() { reset(); }
StreamDecoder::~StreamDecoder() = default;

void StreamDecoder::reset (Kind hint)
{
    k = hint;
    head.clear();
    mp3 = std::make_unique<Mp3Decoder>();
    aac = std::make_unique<AacDecoder>();
    ts.reset();
    es.clear();
    why.clear();
}

StreamDecoder::Kind StreamDecoder::sniff (const uint8_t* p, size_t n)
{
    size_t at = 0;
    while (const size_t tag = id3Size (p + at, n - at))
    {
        if (at + tag + 4 > n) return Kind::Unknown;
        at += tag;
    }
    p += at; n -= at;
    if (n >= 377 && p[0] == 0x47 && p[188] == 0x47 && p[376] == 0x47) return Kind::Ts;
    if (n < 4096) return Kind::Unknown;
    // two ADTS headers one after the other: AAC
    for (size_t i = 0; i + 16 < n && i < 8192; ++i)
    {
        AdtsHeader a, b;
        if (parseAdts (p + i, n - i, a) && i + (size_t) a.frameLength + 7 <= n && parseAdts (p + i + a.frameLength, n - i - (size_t) a.frameLength, b)
            && a.rateIndex == b.rateIndex)
            return Kind::Aac;
    }
    return Kind::Mp3;
}

int StreamDecoder::sampleRate() const
{
    if (k == Kind::Mp3) return mp3->sampleRate();
    if (k == Kind::Aac) return aac->sampleRate();
    if (k == Kind::Ts) return ts.audio() == TsDemux::Audio::Mp3 ? mp3->sampleRate() : aac->sampleRate();
    return track.mp3 ? mp3->sampleRate() : aac->sampleRate();
}

int64_t StreamDecoder::badBytes() const { return mp3->badBytes() + aac->skipped(); }

int StreamDecoder::decodeEs (const uint8_t* p, size_t n, bool isAac, std::vector<float>& l, std::vector<float>& r, bool end)
{
    if (! isAac) return mp3->feed (p, n, l, r, end);
    if (! aacAvailable()) { why = "AAC CAN'T BE PLAYED ON THIS COMPUTER"; return 0; }
    const int got = aac->feed (p, n, l, r);
    if (aac->badFrames() > 50 && aac->sampleRate() == 0) why = "THIS COMPUTER'S AAC DECODER WON'T PLAY THIS STATION";
    return got;
}

int StreamDecoder::feed (const uint8_t* data, size_t n, std::vector<float>& l, std::vector<float>& r, bool end)
{
    const uint8_t* p = data;
    size_t len = n;
    if (k == Kind::Unknown)
    {
        head.insert (head.end(), data, data + n);
        k = sniff (head.data(), head.size());
        if (k == Kind::Unknown && (end || head.size() > 64 * 1024)) k = Kind::Mp3;
        if (k == Kind::Unknown) return 0;
        p = head.data(); len = head.size();
    }
    int got = 0;
    switch (k)
    {
        case Kind::Mp3: got = decodeEs (p, len, false, l, r, end); break;
        case Kind::Aac: got = decodeEs (p, len, true, l, r, end); break;
        case Kind::Ts:
        {
            es.clear();
            ts.feed (p, len, es);
            if (ts.audio() == TsDemux::Audio::Other) why = "THE STATION'S SOUND ISN'T AAC OR MP3";
            else if (! es.empty()) got = decodeEs (es.data(), es.size(), ts.audio() == TsDemux::Audio::Aac, l, r, end);
            break;
        }
        default: break;
    }
    if (! head.empty()) head.clear();
    return got;
}

bool StreamDecoder::setInit (const uint8_t* data, size_t n)
{
    if (! parseMp4Init (data, n, track)) { why = "THE STATION'S MP4 SOUND ISN'T AAC OR MP3"; return false; }
    return true;
}

int StreamDecoder::feedMp4Segment (const uint8_t* data, size_t n, std::vector<float>& l, std::vector<float>& r)
{
    if (! track.valid()) { why = "THE STATION'S MP4 SOUND ISN'T AAC OR MP3"; return 0; }
    es.clear();
    if (! parseMp4Segment (data, n, track, es)) return 0;
    k = Kind::Unknown;   // (sampleRate() then asks the decoder the track uses)
    return decodeEs (es.data(), es.size(), ! track.mp3, l, r, false);
}
} // namespace prv::radio
