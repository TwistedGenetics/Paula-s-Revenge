#include "helix/WavIO.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>

namespace helix {

std::vector<float> WavData::mono() const
{
    const size_t n = frames();
    std::vector<float> m(n, 0.0f);
    if (channels <= 0) return m;
    for (size_t i = 0; i < n; ++i)
    {
        float s = 0.0f;
        for (int c = 0; c < channels; ++c) s += samples[i * (size_t)channels + (size_t)c];
        m[i] = s / (float)channels;
    }
    return m;
}

static uint32_t rd32(const uint8_t* p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }
static uint16_t rd16(const uint8_t* p) { return (uint16_t)(p[0] | (p[1] << 8)); }

static bool fail(std::string* e, const char* msg) { if (e) *e = msg; return false; }

bool parseWav(const uint8_t* d, size_t size, WavData& out, std::string* err)
{
    out = WavData();
    if (!d || size < 12) return fail(err, "too small");
    if (std::memcmp(d, "RIFF", 4) != 0 || std::memcmp(d + 8, "WAVE", 4) != 0) return fail(err, "not RIFF/WAVE");
    size_t pos = 12;
    int fmtTag = 0, bits = 0, channels = 0, rate = 0, blockAlign = 0;
    const uint8_t* pcm = nullptr;
    size_t pcmSize = 0;
    bool haveFmt = false;
    while (pos + 8 <= size)
    {
        const uint8_t* ck = d + pos;
        const uint32_t ckSize = rd32(ck + 4);
        const size_t body = pos + 8;
        const size_t avail = size - body;
        const size_t len = ckSize <= avail ? ckSize : avail;   // tolerate truncated last chunk
        if (std::memcmp(ck, "fmt ", 4) == 0)
        {
            if (len < 16) return fail(err, "short fmt chunk");
            fmtTag = rd16(d + body);
            channels = rd16(d + body + 2);
            rate = (int)rd32(d + body + 4);
            blockAlign = rd16(d + body + 12);
            bits = rd16(d + body + 14);
            if (fmtTag == 0xFFFE && len >= 26) fmtTag = rd16(d + body + 24);   // WAVE_FORMAT_EXTENSIBLE subformat
            haveFmt = true;
        }
        else if (std::memcmp(ck, "data", 4) == 0)
        {
            pcm = d + body;
            pcmSize = len;
        }
        else if (std::memcmp(ck, "clm ", 4) == 0 && len >= 3)
        {
            // Serum: "<!>2048 ..." ; take the digits after "<!>"
            std::string s((const char*)d + body, len < 64 ? len : 64);
            const size_t k = s.find("<!>");
            if (k != std::string::npos)
            {
                int v = 0; size_t i = k + 3;
                while (i < s.size() && s[i] >= '0' && s[i] <= '9' && v < 1000000) v = v * 10 + (s[i++] - '0');
                if (v >= 8 && v <= 65536) out.clmFrameSize = v;
            }
        }
        if (ckSize > avail) break;
        pos = body + ckSize + (ckSize & 1u);
    }
    if (!haveFmt) return fail(err, "no fmt chunk");
    if (!pcm) return fail(err, "no data chunk");
    if (channels < 1 || channels > 16) return fail(err, "bad channel count");
    if (rate < 1000 || rate > 768000) return fail(err, "bad sample rate");
    const bool isFloat = fmtTag == 3;
    if (!(fmtTag == 1 || isFloat)) return fail(err, "unsupported format (PCM and float only)");
    if (isFloat && bits != 32 && bits != 64) return fail(err, "bad float bit depth");
    if (!isFloat && bits != 8 && bits != 16 && bits != 24 && bits != 32) return fail(err, "bad PCM bit depth");
    const int bps = bits / 8;
    if (blockAlign != bps * channels) blockAlign = bps * channels;
    const size_t frames = pcmSize / (size_t)blockAlign;
    if (frames == 0) return fail(err, "empty data");
    if (frames > (size_t)64 * 1024 * 1024) return fail(err, "too long");
    out.sampleRate = rate;
    out.channels = channels;
    out.samples.resize(frames * (size_t)channels);
    for (size_t i = 0; i < frames * (size_t)channels; ++i)
    {
        const uint8_t* s = pcm + i * (size_t)bps;
        float v = 0.0f;
        if (isFloat)
        {
            if (bits == 32) { float f; std::memcpy(&f, s, 4); v = f; }
            else { double f; std::memcpy(&f, s, 8); v = (float)f; }
            if (!std::isfinite(v)) v = 0.0f;
        }
        else switch (bits)
        {
            case 8: v = ((float)s[0] - 128.0f) / 128.0f; break;
            case 16: v = (float)(int16_t)rd16(s) / 32768.0f; break;
            case 24: { int32_t x = (int32_t)((uint32_t)s[0] << 8 | (uint32_t)s[1] << 16 | (uint32_t)s[2] << 24) >> 8; v = (float)x / 8388608.0f; break; }
            case 32: v = (float)(int32_t)rd32(s) / 2147483648.0f; break;
        }
        out.samples[i] = v;
    }
    return true;
}

bool readWavFile(const std::string& path, WavData& out, std::string* err)
{
    std::ifstream f(path, std::ios::binary);
    if (!f) return fail(err, "cannot open file");
    f.seekg(0, std::ios::end);
    const std::streamoff len = f.tellg();
    if (len <= 0 || len > (std::streamoff)512 * 1024 * 1024) return fail(err, "bad file size");
    f.seekg(0);
    std::vector<uint8_t> buf((size_t)len);
    f.read((char*)buf.data(), len);
    return parseWav(buf.data(), buf.size(), out, err);
}

static void put32(std::vector<uint8_t>& b, uint32_t v) { for (int i = 0; i < 4; ++i) b.push_back((uint8_t)(v >> (8 * i))); }
static void put16(std::vector<uint8_t>& b, uint16_t v) { b.push_back((uint8_t)v); b.push_back((uint8_t)(v >> 8)); }

bool writeWavFile(const std::string& path, const float* x, size_t frames, int channels, int rate, bool float32)
{
    const int bps = float32 ? 4 : 2;
    const uint32_t dataBytes = (uint32_t)(frames * (size_t)channels * (size_t)bps);
    std::vector<uint8_t> b;
    b.reserve(44 + dataBytes);
    b.insert(b.end(), {'R', 'I', 'F', 'F'}); put32(b, 36 + dataBytes);
    b.insert(b.end(), {'W', 'A', 'V', 'E', 'f', 'm', 't', ' '}); put32(b, 16);
    put16(b, float32 ? 3 : 1); put16(b, (uint16_t)channels); put32(b, (uint32_t)rate);
    put32(b, (uint32_t)(rate * channels * bps)); put16(b, (uint16_t)(channels * bps)); put16(b, (uint16_t)(bps * 8));
    b.insert(b.end(), {'d', 'a', 't', 'a'}); put32(b, dataBytes);
    for (size_t i = 0; i < frames * (size_t)channels; ++i)
    {
        if (float32) { uint32_t u; std::memcpy(&u, &x[i], 4); put32(b, u); }
        else
        {
            float v = x[i] < -1.0f ? -1.0f : (x[i] > 1.0f ? 1.0f : x[i]);
            put16(b, (uint16_t)(int16_t)std::lrint(v * 32767.0f));
        }
    }
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    f.write((const char*)b.data(), (std::streamsize)b.size());
    return (bool)f;
}

} // namespace helix
