// Paula's Revenge - internet radio, the parts with no JUCE in them (and so tested on their own):
// the stream's ICY metadata, playlists, MP3 decoding, the rate change, the FIFO the audio thread
// plays from, the GRAB buffer (the last 5 / 10 / 20 / 30 seconds heard) and the reconnect timing.
#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace prv::radio
{
// ---- the stream's text: "StreamTitle='Artist - Title';" every icy-metaint bytes
class IcyParser
{
public:
    void reset (int metaInt);               // 0: the stream has no metadata (every byte is audio)
    // the bytes as they come: the audio ones are appended to `audio`; true when a new title arrived
    bool feed (const uint8_t* data, size_t n, std::vector<uint8_t>& audio);
    const std::string& title() const { return current; }
    static std::string titleFrom (const std::string& meta);   // StreamTitle='...' ('' if none)

private:
    int interval = 0, untilMeta = 0, metaLeft = -1;   // -1 audio, -2 the length byte, > 0 the text left
    std::string meta, current;
};

// ---- .pls / .m3u: the stream addresses in it (http / https), in order
std::vector<std::string> parsePlaylist (const std::string& text);
bool isPlaylist (const std::string& url, const std::string& contentType);

// ---- reconnecting: how long to wait before try `attempt` (1, 2, ...); -1 = give up (after 5)
constexpr int kMaxRetries = 5;
int retryDelayMs (int attempt);

// ---- MP3 frames in, stereo floats out (at the stream's own rate)
class Mp3Decoder
{
public:
    Mp3Decoder();
    ~Mp3Decoder();
    void reset();
    // decodes what it can; the samples are appended (L / R); returns the frames added
    int feed (const uint8_t* data, size_t n, std::vector<float>& left, std::vector<float>& right, bool endOfStream = false);
    int sampleRate() const { return rate; }   // 0 until the first frame
    int channels() const { return chans; }
    int64_t badBytes() const { return skipped; }   // (bytes that weren't MP3)

private:
    struct State;
    std::unique_ptr<State> s;
    std::vector<uint8_t> pending;
    int rate = 0, chans = 0;
    int64_t skipped = 0;
};

// ---- the stream's rate to the app's (4-point Hermite; keeps its place between calls)
class Resampler
{
public:
    void setRates (double from, double to);
    void reset();
    void process (const float* l, const float* r, int n, std::vector<float>& outL, std::vector<float>& outR);
    double from() const { return inRate; }

private:
    double inRate = 0, outRate = 0, step = 1, pos = 0;
    float hl[4] {}, hr[4] {};   // the last 4 input samples
    int have = 0;
};

// ---- one writer thread, one reader thread (the audio): stereo frames
class Fifo
{
public:
    void setSize (int frames);   // (not while either side is using it)
    int  size() const { return cap; }
    int  available() const;      // frames ready to read
    int  space() const;
    int  push (const float* l, const float* r, int n);   // returns how many went in
    int  pop (float* l, float* r, int n);                 // returns how many came out
    void clear();                // (the reader's side: drops everything ready)

private:
    std::vector<float> L, R;
    int cap = 0;
    std::atomic<int64_t> wrote { 0 }, read { 0 };
};

// ---- the GRAB buffer: always the latest `seconds` of what played (written by the audio thread)
class GrabBuffer
{
public:
    static constexpr int kChoices[4] = { 5, 10, 20, 30 };
    void setLength (int seconds, double rate);   // keeps what it holds, up to the new length
    int  seconds() const { return secs; }
    double rate() const { return sr; }
    void write (const float* l, const float* r, int n);
    int64_t held() const { return std::min<int64_t> (filled, (int64_t) L.size()); }   // frames
    double heldSeconds() const { return sr > 0 ? (double) held() / sr : 0.0; }
    void copy (std::vector<float>& left, std::vector<float>& right) const;   // oldest first
    void clear() { filled = 0; at = 0; }

private:
    std::vector<float> L, R;
    int secs = 10;
    double sr = 0;
    int64_t filled = 0;
    size_t at = 0;
};
} // namespace prv::radio
