// Large-file support for 32-bit ARM (Gen1 MPCs): without it readdir() fails with EOVERFLOW on filesystems
// that hand out 64-bit directory offsets. Must come before any system header.
#if !defined(_WIN32) && !defined(_FILE_OFFSET_BITS)
#define _FILE_OFFSET_BITS 64
#endif
#include "helix/WavetableImport.h"
#include "helix/Math.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#if !defined(_WIN32)
#include <dirent.h>
#endif

namespace helix {

namespace fs = std::filesystem;

static std::string baseName(const std::string& path)
{
    const fs::path p(path);
    return p.stem().string();
}

float detectPeriod(const float* x, size_t len, int minPeriod, int maxPeriod, float threshold)
{
    minPeriod = std::max(2, minPeriod);
    if (maxPeriod <= minPeriod || len < (size_t)(2 * maxPeriod + 2)) return 0.0f;
    const int W = maxPeriod;   // integration window
    std::vector<double> d((size_t)maxPeriod + 2, 0.0);
    for (int tau = 1; tau <= maxPeriod + 1; ++tau)
    {
        double s = 0.0;
        for (int j = 0; j < W; ++j) { const double diff = (double)x[j] - (double)x[j + tau]; s += diff * diff; }
        d[(size_t)tau] = s;
    }
    // Cumulative mean normalised difference
    std::vector<double> cm(d.size(), 1.0);
    double run = 0.0;
    for (int tau = 1; tau <= maxPeriod + 1; ++tau)
    {
        run += d[(size_t)tau];
        cm[(size_t)tau] = run > 0.0 ? d[(size_t)tau] * tau / run : 1.0;
    }
    int best = -1;
    for (int tau = minPeriod; tau <= maxPeriod; ++tau)
        if (cm[(size_t)tau] < threshold)
        {
            while (tau + 1 <= maxPeriod && cm[(size_t)tau + 1] < cm[(size_t)tau]) ++tau;
            best = tau;
            break;
        }
    if (best < 0) return 0.0f;
    // Parabolic interpolation
    const double a = cm[(size_t)best - 1], b = cm[(size_t)best], c = cm[(size_t)best + 1];
    const double den = a - 2 * b + c;
    const double off = std::fabs(den) > 1e-12 ? 0.5 * (a - c) / den : 0.0;
    return (float)(best + clamp(off, -1.0, 1.0));
}

WavetablePtr sampleToTable(const float* x, size_t len, int sr, const std::string& name, const SampleToTableOptions& opt,
                           SampleToTableInfo* info)
{
    if (!x || len < 64 || sr < 1000) return nullptr;
    const int frames = clamp(opt.frames, 2, kMaxFrames);
    const int minP = std::max(2, (int)((float)sr / std::max(opt.maxHz, 1.0f)));
    const int maxP = std::max(minP + 2, (int)((float)sr / std::max(opt.minHz, 1.0f)));

    // Global pitch from the loudest stretch of the sample.
    size_t loud = 0;
    {
        const size_t win = std::min(len, (size_t)(2 * maxP + 2));
        double best = -1.0, acc = 0.0;
        for (size_t i = 0; i < len; ++i)
        {
            acc += (double)x[i] * x[i];
            if (i >= win) acc -= (double)x[i - win] * x[i - win];
            if (i + 1 >= win && acc > best) { best = acc; loud = i + 1 - win; }
        }
    }
    float period = detectPeriod(x + loud, len - loud, minP, maxP);
    const bool pitched = period > 0.0f;
    if (!pitched) period = (float)std::min<size_t>(kFrameSize, len / 2);   // unpitched (breaks): plain windows
    const int P = std::max(8, (int)std::lround(period));
    if (info) { info->pitched = pitched; info->detectedHz = pitched ? (float)sr / period : 0.0f; info->periodSamples = P; }
    if ((size_t)P * 2 > len) return nullptr;

    std::vector<float> table((size_t)frames * kFrameSize);
    std::vector<float> cycle((size_t)P + 1);
    const size_t span = len - (size_t)P - 1;
    for (int f = 0; f < frames; ++f)
    {
        size_t s = (size_t)((double)f * (double)span / (double)(frames - 1));
        if (opt.alignZeroCrossings)
        {
            // Nearest rising zero crossing within one period after s.
            for (size_t k = s; k + 1 < std::min(len - (size_t)P, s + (size_t)P); ++k)
                if (x[k] <= 0.0f && x[k + 1] > 0.0f) { s = k + 1; break; }
        }
        int pk = P;
        if (pitched && s + (size_t)(2 * maxP) < len)
        {
            // Refine the local period (vibrato, pitch drift) within +-12% of the global one.
            const float local = detectPeriod(x + s, len - s, std::max(minP, (int)(P * 0.88f)), std::min(maxP, (int)(P * 1.12f) + 1), 0.3f);
            if (local > 0.0f) pk = std::max(8, (int)std::lround(local));
        }
        if (s + (size_t)pk >= len) pk = (int)(len - s - 1);
        cycle.assign(x + s, x + s + pk);
        // Seamless wrap: blend the last 1/16 of the cycle toward the samples just before its start.
        const int fade = std::max(2, pk / 16);
        for (int i = 0; i < fade; ++i)
        {
            const float t = (float)(i + 1) / (float)(fade + 1);
            const size_t src = s + (size_t)pk - (size_t)fade + (size_t)i;
            const float wrapped = s >= (size_t)fade ? x[s - (size_t)fade + (size_t)i] : cycle[(size_t)i];
            cycle[(size_t)(pk - fade + i)] = x[src] * (1.0f - t) + wrapped * t;
        }
        resampleCycle(cycle.data(), pk, table.data() + (size_t)f * kFrameSize, kFrameSize);
    }
    return Wavetable::fromFrames(table.data(), frames, kFrameSize, name);
}

WavetablePtr importWavetable(const WavData& wav, const std::string& name, std::string* error)
{
    std::vector<float> mono = wav.mono();
    const size_t len = mono.size();
    if (len < 8) { if (error) *error = "too short"; return nullptr; }

    int frameSize = wav.clmFrameSize;
    if (frameSize > 0)
    {
        if ((size_t)frameSize > len) { if (error) *error = "clm frame size longer than the file"; return nullptr; }
        const int frames = (int)std::min<size_t>(len / (size_t)frameSize, kMaxFrames);
        return Wavetable::fromFrames(mono.data(), frames, frameSize, name);
    }
    if (len >= (size_t)kFrameSize && len % (size_t)kFrameSize == 0 && len / kFrameSize <= (size_t)kMaxFrames)
        return Wavetable::fromFrames(mono.data(), (int)(len / kFrameSize), kFrameSize, name);   // Serum default
    if (len <= 4096)
        return Wavetable::fromFrames(mono.data(), 1, (int)len, name);                         // single cycle
    // Long audio that is not a table: make one from it.
    SampleToTableOptions opt;
    return sampleToTable(mono.data(), len, wav.sampleRate, name, opt, nullptr);
}

WavetablePtr importWavetableFile(const std::string& path, std::string* error)
{
    WavData wav;
    if (!readWavFile(path, wav, error)) return nullptr;
    return importWavetable(wav, baseName(path), error);
}

static bool isWav(const fs::path& p)
{
    std::string e = p.extension().string();
    std::transform(e.begin(), e.end(), e.begin(), [](unsigned char c) { return (char)std::tolower(c); });
    return e == ".wav";
}

std::vector<std::string> listDirectory(const std::string& dir)
{
    std::vector<std::string> out;
#if !defined(_WIN32)
    // POSIX readdir with 64-bit offsets (see the top of this file): std::filesystem's iterator on 32-bit ARM
    // depends on how the C++ library itself was built.
    if (DIR* d = opendir(dir.c_str()))
    {
        while (dirent* e = readdir(d))
        {
            if (std::strcmp(e->d_name, ".") == 0 || std::strcmp(e->d_name, "..") == 0) continue;
            out.push_back(dir + (dir.empty() || dir.back() == '/' ? "" : "/") + e->d_name);
        }
        closedir(d);
    }
#else
    try
    {
        std::error_code ec;
        fs::directory_iterator it(dir, ec), end;
        for (; !ec && it != end; it.increment(ec)) out.push_back(it->path().string());
    }
    catch (...) { out.clear(); }   // a scanner must never take the host down
#endif
    std::sort(out.begin(), out.end());
    return out;
}

void listFilesRecursive(const std::string& dir, std::vector<std::string>& out, int maxDepth)
{
    if (maxDepth < 0) return;
    std::error_code ec;
    for (const std::string& p : listDirectory(dir))
    {
        if (fs::is_directory(p, ec)) listFilesRecursive(p, out, maxDepth - 1);
        else if (fs::is_regular_file(p, ec)) out.push_back(p);
    }
}

WavetablePtr importCycleFolder(const std::string& dir, std::string* error)
{
    std::error_code ec;
    std::vector<fs::path> files;
    for (const std::string& f : listDirectory(dir))
        if (fs::is_regular_file(f, ec) && isWav(f)) files.push_back(f);
    if (files.empty()) { if (error) *error = "no WAV files in folder"; return nullptr; }
    std::sort(files.begin(), files.end(), [](const fs::path& a, const fs::path& b) { return a.filename().string() < b.filename().string(); });
    std::vector<float> frames;
    int count = 0;
    for (const auto& f : files)
    {
        if (count == kMaxFrames) break;
        WavData w;
        if (!readWavFile(f.string(), w, nullptr)) continue;
        std::vector<float> m = w.mono();
        if (m.size() < 8) continue;
        frames.resize((size_t)(count + 1) * kFrameSize);
        resampleCycle(m.data(), (int)std::min<size_t>(m.size(), 65536), frames.data() + (size_t)count * kFrameSize, kFrameSize);
        ++count;
    }
    if (!count) { if (error) *error = "no readable cycles"; return nullptr; }
    return Wavetable::fromFrames(frames.data(), count, kFrameSize, fs::path(dir).filename().string());
}

} // namespace helix
