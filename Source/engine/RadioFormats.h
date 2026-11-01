// Paula's Revenge - internet radio formats beyond plain MP3: AAC / AAC+ (ADTS, decoded by the
// computer's own decoder: AudioToolbox on a Mac, Media Foundation on Windows), MPEG transport
// streams, fragmented MP4 and HLS playlists. No JUCE here; the parts that don't need a decoder
// are tested anywhere, the AAC decoding on the Mac and Windows builds.
#pragma once

#include "Radio.h"
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace prv::radio
{
// ---------------------------------------------------------------- AAC
// an ADTS header (7 bytes, 9 with a CRC) - what's in it
struct AdtsHeader
{
    int profile = 1;        // object type - 1 (1 = AAC LC)
    int rateIndex = 4;      // 3 = 48000, 4 = 44100, ...
    int channels = 2;
    int frameLength = 0;    // the whole frame, header included
    int headerLength = 7;
    int sampleRate() const;
};
bool parseAdts (const uint8_t* p, size_t n, AdtsHeader&);          // false: not a header here
std::vector<uint8_t> makeAdts (int objectType, int rateIndex, int channels, size_t payload);
int  rateIndexFor (int hz);                                        // -1 if not a standard rate
size_t id3Size (const uint8_t* p, size_t n);                       // an ID3v2 tag here: its whole size (0 = none)

// ADTS bytes in (any chunk sizes, an ID3 tag or junk in between), whole frames out
class AdtsSplitter
{
public:
    void reset() { buf.clear(); }
    // the frames now complete (each with its header)
    void feed (const uint8_t* data, size_t n, std::vector<std::vector<uint8_t>>& frames);
    int64_t skipped() const { return junk; }

private:
    std::vector<uint8_t> buf;
    int64_t junk = 0;
};

// the computer's AAC decoder (none on Linux: available() is false there)
struct AacBackend
{
    virtual ~AacBackend() = default;
    // one ADTS frame in; its samples appended (stereo, at sampleRate()); false = it couldn't
    virtual bool decode (const uint8_t* frame, size_t n, std::vector<float>& left, std::vector<float>& right) = 0;
    virtual int sampleRate() const = 0;   // the output rate (HE-AAC: twice the core rate)
};
std::unique_ptr<AacBackend> makeAacBackend();
bool aacAvailable();

class AacDecoder
{
public:
    void reset() { split.reset(); backend.reset(); failed = 0; rate = 0; }
    int  feed (const uint8_t* data, size_t n, std::vector<float>& left, std::vector<float>& right);
    int  sampleRate() const { return rate; }
    int  badFrames() const { return failed; }
    int64_t skipped() const { return split.skipped(); }

private:
    AdtsSplitter split;
    std::unique_ptr<AacBackend> backend;
    int failed = 0, rate = 0;
};

// ---------------------------------------------------------------- MPEG transport stream
// 188-byte packets in; the first audio stream's bytes out (AAC as ADTS, or MP3)
class TsDemux
{
public:
    enum class Audio { None, Aac, Mp3, Other };
    void reset();
    void feed (const uint8_t* data, size_t n, std::vector<uint8_t>& audio);
    Audio audio() const { return kind; }
    int   streamType() const { return type; }

private:
    std::vector<uint8_t> buf;
    int pmtPid = -1, audioPid = -1, type = 0;
    Audio kind = Audio::None;
    bool inPes = false;
    std::vector<uint8_t> pes;   // the PES being put together (its header is cut off when it's whole)
    void packet (const uint8_t* p, std::vector<uint8_t>& audio);
    void flushPes (std::vector<uint8_t>& audio);
};

// ---------------------------------------------------------------- fragmented MP4 (HLS fMP4 / CMAF)
struct Mp4Track
{
    int objectType = 0;     // 2 = AAC LC, 5 = HE-AAC (SBR), 29 = HE-AAC v2, 0x6B / 34 = MP3
    int rateIndex = 4, channels = 2;
    bool mp3 = false;
    bool valid() const { return objectType != 0 || mp3; }
};
bool parseMp4Init (const uint8_t* p, size_t n, Mp4Track&);   // ftyp / moov: the audio track
// a media segment (moof + mdat, maybe several): its audio frames, each as ADTS (AAC) or as-is (MP3)
bool parseMp4Segment (const uint8_t* p, size_t n, const Mp4Track&, std::vector<uint8_t>& audio);
bool parseAudioSpecificConfig (const uint8_t* p, size_t n, Mp4Track&);

// ---------------------------------------------------------------- HLS
struct HlsPlaylist
{
    struct Variant { std::string uri, codecs; int bandwidth = 0; };
    struct Segment { std::string uri; double duration = 0; int64_t sequence = 0; };
    bool valid = false, master = false, ended = false;
    std::vector<Variant> variants;
    std::string audioUri;          // a master's #EXT-X-MEDIA TYPE=AUDIO rendition (DEFAULT first)
    std::vector<Segment> segments;
    double targetDuration = 6;
    int64_t mediaSequence = 0;
    std::string mapUri;            // fMP4: the init segment
    std::string keyMethod;         // "" / NONE, or AES-128 / SAMPLE-AES (encrypted: can't be played)
};
HlsPlaylist parseHls (const std::string& text);
bool looksLikeHls (const std::string& text);                  // #EXTM3U with #EXT-X- tags
std::string resolveUrl (const std::string& base, const std::string& ref);
int chooseVariant (const HlsPlaylist&);                       // the best audio one (-1 = none)

// ---------------------------------------------------------------- any of them
// A stream (or HLS segments one after another) in, stereo floats out: it works out what the
// bytes are (or is told), and uses the MP3 or AAC decoder.
class StreamDecoder
{
public:
    enum class Kind { Unknown, Mp3, Aac, Ts };
    StreamDecoder();
    ~StreamDecoder();
    void reset (Kind hint = Kind::Unknown);
    int  feed (const uint8_t* data, size_t n, std::vector<float>& left, std::vector<float>& right, bool endOfStream = false);
    // a whole fMP4 segment (after setInit with the playlist's init segment)
    bool setInit (const uint8_t* data, size_t n);
    int  feedMp4Segment (const uint8_t* data, size_t n, std::vector<float>& left, std::vector<float>& right);
    int  sampleRate() const;
    Kind kind() const { return k; }
    std::string problem() const { return why; }   // e.g. "AAC CAN'T BE DECODED ON THIS COMPUTER"
    int64_t badBytes() const;
    static Kind sniff (const uint8_t* p, size_t n);   // Unknown: not enough to tell yet

private:
    Kind k = Kind::Unknown;
    std::vector<uint8_t> head;   // (bytes kept until it can tell what they are)
    std::unique_ptr<Mp3Decoder> mp3;
    std::unique_ptr<AacDecoder> aac;
    TsDemux ts;
    Mp4Track track;
    std::vector<uint8_t> es;
    std::string why;
    int decodeEs (const uint8_t* p, size_t n, bool isAac, std::vector<float>& l, std::vector<float>& r, bool end);
};
} // namespace prv::radio
