// Paula's Revenge - internet radio: the plain C++ parts (see Radio.h)
#define MINIMP3_IMPLEMENTATION
#define MINIMP3_FLOAT_OUTPUT
#include "../../third_party/minimp3/minimp3.h"
#include "Radio.h"

#include <algorithm>
#include <cctype>
#include <cstring>

namespace prv::radio
{
// ---------------------------------------------------------------------------- ICY metadata
void IcyParser::reset (int metaInt)
{
    interval = std::max (0, metaInt);
    untilMeta = interval;
    metaLeft = -1;
    meta.clear();
    current.clear();
}

std::string IcyParser::titleFrom (const std::string& m)
{
    const auto k = m.find ("StreamTitle='");
    if (k == std::string::npos) return {};
    const size_t a = k + 13;
    // the title ends at "';" (a ' inside the title is allowed)
    size_t b = m.find ("';", a);
    if (b == std::string::npos) { b = m.find ('\'', a); if (b == std::string::npos) b = m.size(); }
    std::string t = m.substr (a, b - a);
    while (! t.empty() && (unsigned char) t.back() <= ' ') t.pop_back();
    size_t s = 0; while (s < t.size() && (unsigned char) t[s] <= ' ') ++s;
    return t.substr (s);
}

bool IcyParser::feed (const uint8_t* d, size_t n, std::vector<uint8_t>& audio)
{
    if (interval <= 0) { audio.insert (audio.end(), d, d + n); return false; }
    bool changed = false;
    size_t i = 0;
    while (i < n)
    {
        if (metaLeft == -1)   // audio
        {
            const size_t take = std::min ((size_t) untilMeta, n - i);
            audio.insert (audio.end(), d + i, d + i + take);
            i += take;
            untilMeta -= (int) take;
            if (untilMeta == 0) metaLeft = -2;   // next: the metadata's length byte
        }
        else if (metaLeft == -2)   // the length byte (16-byte blocks; 0 = no new text)
        {
            metaLeft = (int) d[i++] * 16;
            meta.clear();
            if (metaLeft == 0) { metaLeft = -1; untilMeta = interval; }
        }
        else   // the text
        {
            const size_t take = std::min ((size_t) metaLeft, n - i);
            meta.append ((const char*) d + i, take);
            i += take;
            metaLeft -= (int) take;
            if (metaLeft == 0)
            {
                const auto nul = meta.find ('\0');
                if (nul != std::string::npos) meta.resize (nul);
                const auto t = titleFrom (meta);
                if (! t.empty() && t != current) { current = t; changed = true; }
                metaLeft = -1;
                untilMeta = interval;
            }
        }
    }
    return changed;
}

// ---------------------------------------------------------------------------- playlists
namespace
{
    std::string lower (std::string s) { for (auto& c : s) c = (char) std::tolower ((unsigned char) c); return s; }
    std::string trim (const std::string& s)
    {
        size_t a = 0, b = s.size();
        while (a < b && (unsigned char) s[a] <= ' ') ++a;
        while (b > a && (unsigned char) s[b - 1] <= ' ') --b;
        return s.substr (a, b - a);
    }
    bool isHttp (const std::string& s) { const auto l = lower (s); return l.rfind ("http://", 0) == 0 || l.rfind ("https://", 0) == 0; }
}

std::vector<std::string> parsePlaylist (const std::string& text)
{
    std::vector<std::string> out;
    size_t p = 0;
    while (p <= text.size())
    {
        size_t e = text.find_first_of ("\r\n", p);
        if (e == std::string::npos) e = text.size();
        auto line = trim (text.substr (p, e - p));
        p = e + 1;
        if (line.empty() || line[0] == '#' || line[0] == '[') continue;
        // .pls: File1=http://...
        const auto eq = line.find ('=');
        if (eq != std::string::npos && lower (line.substr (0, 4)) == "file") line = trim (line.substr (eq + 1));
        if (isHttp (line) && std::find (out.begin(), out.end(), line) == out.end()) out.push_back (line);
    }
    return out;
}

bool isPlaylist (const std::string& url, const std::string& contentType)
{
    const auto ct = lower (contentType);
    if (ct.find ("scpls") != std::string::npos || ct.find ("mpegurl") != std::string::npos) return true;
    auto u = lower (url);
    const auto q = u.find_first_of ("?#");
    if (q != std::string::npos) u.resize (q);
    auto ends = [&u] (const char* x) { const size_t n = std::strlen (x); return u.size() >= n && u.compare (u.size() - n, n, x) == 0; };
    return ends (".pls") || ends (".m3u");   // (.m3u8 is HLS: not a playlist of streams)
}

int retryDelayMs (int attempt)
{
    if (attempt < 1 || attempt > kMaxRetries) return -1;
    static const int ms[kMaxRetries] = { 1000, 2000, 4000, 8000, 8000 };
    return ms[attempt - 1];
}

// ---------------------------------------------------------------------------- MP3
struct Mp3Decoder::State { mp3dec_t dec; };

Mp3Decoder::Mp3Decoder() : s (std::make_unique<State>()) { mp3dec_init (&s->dec); }
Mp3Decoder::~Mp3Decoder() = default;

void Mp3Decoder::reset()
{
    mp3dec_init (&s->dec);
    pending.clear();
    rate = chans = 0;
    skipped = 0;
}

int Mp3Decoder::feed (const uint8_t* data, size_t n, std::vector<float>& left, std::vector<float>& right, bool endOfStream)
{
    pending.insert (pending.end(), data, data + n);
    int added = 0;
    size_t pos = 0;
    float pcm[MINIMP3_MAX_SAMPLES_PER_FRAME];
    // minimp3 checks the frames after the one it's on to be sure of where they start, so it's only
    // given a buffer with several frames in it (8 KB: 5 or more), except at the very end
    const size_t keep = endOfStream ? 4 : 8192;
    while (pending.size() - pos >= keep)
    {
        mp3dec_frame_info_t info {};
        const int samples = mp3dec_decode_frame (&s->dec, pending.data() + pos, (int) (pending.size() - pos), pcm, &info);
        if (info.frame_bytes == 0) break;   // needs more data
        pos += (size_t) info.frame_bytes;
        if (samples <= 0) { skipped += info.frame_bytes; continue; }   // (ID3, junk, a frame it skipped)
        rate = info.hz;
        chans = info.channels;
        for (int i = 0; i < samples; ++i)
        {
            if (info.channels >= 2) { left.push_back (pcm[i * 2]); right.push_back (pcm[i * 2 + 1]); }
            else { left.push_back (pcm[i]); right.push_back (pcm[i]); }
        }
        added += samples;
    }
    pending.erase (pending.begin(), pending.begin() + (std::ptrdiff_t) pos);
    if (pending.size() > (1u << 20)) { skipped += (int64_t) pending.size(); pending.clear(); }   // (not MP3 at all)
    return added;
}

// ---------------------------------------------------------------------------- rate change
void Resampler::setRates (double from, double to)
{
    if (from != inRate || to != outRate) { inRate = from; outRate = to; reset(); }
    step = (from > 0 && to > 0) ? from / to : 1.0;
}

void Resampler::reset()
{
    pos = 1.0;
    std::fill (std::begin (hl), std::end (hl), 0.0f);
    std::fill (std::begin (hr), std::end (hr), 0.0f);
    have = 0;
}

void Resampler::process (const float* l, const float* r, int n, std::vector<float>& outL, std::vector<float>& outR)
{
    // the input with the last 3 samples of the call before in front of it
    std::vector<float> bl ((size_t) n + 3), br ((size_t) n + 3);
    for (int k = 0; k < 3; ++k) { bl[(size_t) k] = hl[k]; br[(size_t) k] = hr[k]; }
    std::copy (l, l + n, bl.begin() + 3);
    std::copy (r, r + n, br.begin() + 3);
    const int size = n + 3;
    auto herm = [] (float y0, float y1, float y2, float y3, float t)
    {
        const float c1 = 0.5f * (y2 - y0);
        const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
        const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
        return ((c3 * t + c2) * t + c1) * t + y1;
    };
    while ((int) pos + 2 < size)
    {
        const int i = (int) pos;
        const float t = (float) (pos - i);
        outL.push_back (herm (bl[(size_t) i - 1], bl[(size_t) i], bl[(size_t) i + 1], bl[(size_t) i + 2], t));
        outR.push_back (herm (br[(size_t) i - 1], br[(size_t) i], br[(size_t) i + 1], br[(size_t) i + 2], t));
        pos += step;
    }
    pos -= (double) (size - 3);
    for (int k = 0; k < 3; ++k) { hl[k] = bl[(size_t) (size - 3 + k)]; hr[k] = br[(size_t) (size - 3 + k)]; }
}

// ---------------------------------------------------------------------------- FIFO
void Fifo::setSize (int frames)
{
    cap = std::max (1, frames);
    L.assign ((size_t) cap, 0.0f);
    R.assign ((size_t) cap, 0.0f);
    wrote = 0; read = 0;
}

int Fifo::available() const { return (int) (wrote.load (std::memory_order_acquire) - read.load (std::memory_order_acquire)); }
int Fifo::space() const { return cap - available(); }

int Fifo::push (const float* l, const float* r, int n)
{
    const int64_t w = wrote.load (std::memory_order_relaxed);
    const int room = cap - (int) (w - read.load (std::memory_order_acquire));
    const int k = std::max (0, std::min (n, room));
    for (int i = 0; i < k; ++i) { const size_t at = (size_t) ((w + i) % cap); L[at] = l[i]; R[at] = r[i]; }
    wrote.store (w + k, std::memory_order_release);
    return k;
}

int Fifo::pop (float* l, float* r, int n)
{
    const int64_t rd = read.load (std::memory_order_relaxed);
    const int ready = (int) (wrote.load (std::memory_order_acquire) - rd);
    const int k = std::max (0, std::min (n, ready));
    for (int i = 0; i < k; ++i) { const size_t at = (size_t) ((rd + i) % cap); l[i] = L[at]; r[i] = R[at]; }
    read.store (rd + k, std::memory_order_release);
    return k;
}

void Fifo::clear() { read.store (wrote.load (std::memory_order_acquire), std::memory_order_release); }

// ---------------------------------------------------------------------------- GRAB
void GrabBuffer::setLength (int seconds, double rate)
{
    std::vector<float> keepL, keepR;
    if (rate == sr) copy (keepL, keepR);   // (a new rate: what's held doesn't fit, start again)
    secs = std::clamp (seconds, 1, 60);
    sr = rate;
    const size_t frames = (size_t) std::max (1.0, secs * rate);
    L.assign (frames, 0.0f);
    R.assign (frames, 0.0f);
    filled = 0; at = 0;
    const size_t k = std::min (frames, keepL.size());
    if (k > 0) write (keepL.data() + keepL.size() - k, keepR.data() + keepR.size() - k, (int) k);
}

void GrabBuffer::write (const float* l, const float* r, int n)
{
    const size_t cap = L.size();
    if (cap == 0) return;
    for (int i = 0; i < n; ++i)
    {
        L[at] = l[i]; R[at] = r[i];
        if (++at == cap) at = 0;
    }
    filled += n;
}

void GrabBuffer::copy (std::vector<float>& left, std::vector<float>& right) const
{
    const size_t cap = L.size();
    const size_t h = (size_t) held();
    left.resize (h); right.resize (h);
    size_t from = (at + cap - h) % std::max<size_t> (1, cap);
    for (size_t i = 0; i < h; ++i) { left[i] = L[from]; right[i] = R[from]; if (++from == cap) from = 0; }
}
} // namespace prv::radio
