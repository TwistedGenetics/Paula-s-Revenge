#include "helix/PrintTable.h"
#include "helix/Denormals.h"
#include "helix/Engine.h"
#include "helix/Math.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <memory>

namespace fs = std::filesystem;

namespace helix {

int printSourceParam(int s)
{
    switch (s)
    {
        case kPrintMacro1: return P_MACRO1;
        case kPrintMacro2: return P_MACRO2;
        case kPrintMacro3: return P_MACRO3;
        case kPrintMacro4: return P_MACRO4;
        case kPrintGrowl: return P_RS_GROWL;
        case kPrintPosA: return P_A_POS;
        case kPrintPosB: return P_B_POS;
        case kPrintCutoff1: return P_F1_CUTOFF;
        case kPrintWarpA: return P_A_W1_AMT;
        default: return -1;
    }
}

int printNote(int note) { return clamp(note, 24, 42); }

WavetablePtr printToTable(const Preset& patch, WavetablePtr tableA, WavetablePtr tableB, const PrintOptions& opt,
                          const std::string& name, std::vector<float>* framesOut, std::string* error,
                          const std::atomic<bool>* cancel)
{
    auto fail = [&](const char* m) { if (error) *error = m; return WavetablePtr(); };
    const int frames = clamp(opt.frames, 2, kMaxFrames);
    const int note = printNote(opt.note);
    const double f0 = 440.0 * std::pow(2.0, (note - 69) / 12.0);
    const double sr = f0 * kFrameSize;
    if (sr > kMaxSampleRate) return fail("note too high to print");

    ScopedFlushDenormals ftz;
    auto engine = std::make_unique<Engine>();
    constexpr int kBlock = 64;
    engine->prepare(sr, kBlock);
    engine->setRealtime(false);
    Preset p = patch;
    if (!opt.effects) for (int t : {P_I1_TYPE, P_I2_TYPE, P_X1_TYPE, P_X2_TYPE}) p.params[t] = 0.0f;
    applyPreset(p, *engine, nullptr);
    if (tableA) engine->setTable(kSlotA, tableA);
    if (tableB) engine->setTable(kSlotB, tableB);

    const int param = printSourceParam(opt.source);
    const float seconds = clamp(opt.seconds, 0.25f, 8.0f);
    // Skip the first few cycles (click of the attack), then spread the slices over the rest, on cycle bounds.
    const size_t lead = (size_t)kFrameSize * 4;
    const size_t total = lead + (size_t)std::ceil(seconds * sr / kFrameSize) * (size_t)kFrameSize;
    const size_t span = total - lead - (size_t)kFrameSize;          // first sample of the last slice, from lead
    std::vector<float> mono(total, 0.0f);
    std::vector<float> l(kBlock), r(kBlock);
    Transport tr;
    tr.valid = true; tr.playing = true; tr.bpm = 120.0; tr.ppq = 0.0;

    for (size_t pos = 0; pos < total; pos += kBlock)
    {
        if (cancel && cancel->load()) return fail("cancelled");
        if (param >= 0)
        {
            const double t = pos <= lead ? 0.0 : std::min(1.0, (double)(pos - lead) / (double)std::max<size_t>(span, 1));
            engine->params().setNormalised(param, (float)t);
        }
        MidiMsg ev[1];
        int nev = 0;
        if (pos == 0) { ev[0] = MidiMsg::make(0, 0x90, (uint8_t)note, (uint8_t)clamp(opt.velocity, 1, 127)); nev = 1; }
        const int n = (int)std::min<size_t>(kBlock, total - pos);
        engine->process(l.data(), r.data(), n, ev, nev, tr);
        tr.ppq += n / sr * (tr.bpm / 60.0);
        for (int i = 0; i < n; ++i) mono[pos + (size_t)i] = 0.5f * (l[(size_t)i] + r[(size_t)i]);
    }

    std::vector<float> out((size_t)frames * kFrameSize);
    float peak = 0.0f;
    for (int f = 0; f < frames; ++f)
    {
        size_t start = lead + (size_t)((double)span * f / (double)(frames - 1));
        start -= (start % (size_t)kFrameSize);                        // whole cycles: phase stays put frame to frame
        if (start + kFrameSize > total) start = total - kFrameSize;
        std::memcpy(out.data() + (size_t)f * kFrameSize, mono.data() + start, sizeof(float) * kFrameSize);
    }
    // Per-frame DC out, then one gain for the whole table (keeps the loudness contour of the sweep).
    for (int f = 0; f < frames; ++f)
    {
        float* fr = out.data() + (size_t)f * kFrameSize;
        double dc = 0.0;
        for (int i = 0; i < kFrameSize; ++i) dc += fr[i];
        dc /= kFrameSize;
        for (int i = 0; i < kFrameSize; ++i) { fr[i] -= (float)dc; peak = std::max(peak, std::fabs(fr[i])); }
    }
    if (!(peak > 1.0e-4f)) return fail("the print came out silent");
    const float g = 0.98f / peak;
    for (float& s : out) s *= g;

    WavetablePtr table = Wavetable::fromFrames(out.data(), frames, kFrameSize, name, true);
    if (framesOut) *framesOut = std::move(out);
    return table;
}

std::string nextPrintPath(const std::string& dir)
{
    std::error_code ec;
    fs::create_directories(dir, ec);
    for (int i = 1; i < 10000; ++i)
    {
        char buf[32];
        std::snprintf(buf, sizeof buf, "Print %03d.wav", i);
        const fs::path p = fs::path(dir) / buf;
        if (!fs::exists(p, ec)) return p.string();
    }
    return {};
}

static void put32(std::vector<uint8_t>& b, uint32_t v) { for (int i = 0; i < 4; ++i) b.push_back((uint8_t)(v >> (8 * i))); }
static void put16(std::vector<uint8_t>& b, uint16_t v) { b.push_back((uint8_t)v); b.push_back((uint8_t)(v >> 8)); }
static void putTag(std::vector<uint8_t>& b, const char* t) { b.insert(b.end(), t, t + 4); }

bool writePrintWav(const std::string& path, const std::vector<float>& frames)
{
    const char clm[] = "<!>2048 10000000 wavetable (HELIX print)";
    uint32_t clmLen = (uint32_t)std::strlen(clm);
    const uint32_t clmPad = clmLen & 1u;
    const uint32_t dataLen = (uint32_t)(frames.size() * sizeof(float));
    std::vector<uint8_t> b;
    b.reserve(64 + dataLen);
    putTag(b, "RIFF");
    put32(b, 4 + (8 + 16) + (8 + clmLen + clmPad) + (8 + dataLen));
    putTag(b, "WAVE");
    putTag(b, "fmt ");
    put32(b, 16);
    put16(b, 3);            // IEEE float
    put16(b, 1);
    put32(b, 44100);
    put32(b, 44100 * 4);
    put16(b, 4);
    put16(b, 32);
    putTag(b, "clm ");
    put32(b, clmLen);
    b.insert(b.end(), clm, clm + clmLen);
    if (clmPad) b.push_back(0);
    putTag(b, "data");
    put32(b, dataLen);
    const size_t at = b.size();
    b.resize(at + dataLen);
    std::memcpy(b.data() + at, frames.data(), dataLen);   // little-endian hosts (x86, ARM)
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    const bool ok = std::fwrite(b.data(), 1, b.size(), f) == b.size();
    return std::fclose(f) == 0 && ok;
}

} // namespace helix
