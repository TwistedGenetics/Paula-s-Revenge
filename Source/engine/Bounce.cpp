// Paula's Revenge - bouncing a highlighted part of the pattern to audio
#include <memory>
#include "Bounce.h"
#include <algorithm>
#include <cmath>
#include <set>

namespace prv
{
namespace
{
    BounceRange clampRange (const Song& song, BounceRange r)
    {
        r.block = song.safeBlock (r.block);
        const int lines = song.blocks[(size_t) r.block].lines;
        r.line0 = std::clamp (std::min (r.line0, r.line1), 0, lines - 1);
        r.line1 = std::clamp (std::max (r.line0, r.line1), 0, lines - 1);
        const int nt = std::clamp (song.numTracks, 1, kMaxTracks);
        const int t0 = std::min (r.track0, r.track1), t1 = std::max (r.track0, r.track1);
        r.track0 = std::clamp (t0, 0, nt - 1);
        r.track1 = std::clamp (t1, 0, nt - 1);
        return r;
    }

    int instrumentBefore (const Block& b, int line, int track)
    {
        for (int l = std::min (line, b.lines - 1); l >= 0; --l)
            if (b.at (l, track).instr > 0) return b.at (l, track).instr - 1;
        return -1;
    }

    struct Collect : Player::StemTap
    {
        int t0 = 0, t1 = 0;
        bool returns = false;
        std::vector<float>* L = nullptr;
        std::vector<float>* R = nullptr;
        size_t base = 0;   // where this render() call starts
        size_t fill[Player::kStems] {};
        void add (int stem, const float* l, const float* r, int n) override
        {
            const bool isReturn = stem >= kMaxTracks && stem < kMaxTracks + kReturns;
            if (! ((stem >= t0 && stem <= t1) || (returns && isReturn))) return;
            const size_t at = base + fill[stem];
            if (L->size() < at + (size_t) n) { L->resize (at + (size_t) n, 0.0f); R->resize (at + (size_t) n, 0.0f); }
            for (int i = 0; i < n; ++i) { (*L)[at + (size_t) i] += l[i]; (*R)[at + (size_t) i] += r[i]; }
            fill[stem] += (size_t) n;
        }
    };
}

std::vector<int> instrumentsInRange (const Song& song, const BounceRange& in)
{
    const auto r = clampRange (song, in);
    const auto& b = song.blocks[(size_t) r.block];
    std::set<int> used;
    for (int t = r.track0; t <= r.track1; ++t)
        for (int l = r.line0; l <= r.line1; ++l)
        {
            const auto& c = b.at (l, t);
            if (c.note == 0 || c.note == kNoteOff) continue;
            const int i = c.instr > 0 ? c.instr - 1 : instrumentBefore (b, l, t);
            if (i >= 0 && i < kMaxInstruments) used.insert (i);
        }
    return std::vector<int> (used.begin(), used.end());
}

bool bounceRange (const Song& song, const PaulaOptions& options, double sampleRate, const BounceRange& in, BounceResult& out,
                  std::function<std::unique_ptr<PluginSynth> (int, const Instrument&, double)> synthFactory, std::string& error,
                  std::function<std::unique_ptr<Effect> (int, const FxSlot&, double)> pluginFactory)
{
    out = BounceResult {};
    const auto r = clampRange (song, in);
    const auto used = instrumentsInRange (song, r);
    for (int i : used) (song.instruments[(size_t) i].isMidiOut() ? out.leftOut : out.instruments).push_back (i);
    if (out.instruments.empty())
    {
        error = out.leftOut.empty() ? "THERE ARE NO NOTES IN THE HIGHLIGHTED AREA" : "MIDI OUT PLAYS HARDWARE: IT CAN'T BE BOUNCED";
        return false;
    }

    // a song of just those lines: one block, then an empty one for the tail
    Song s = song;
    const auto& src = song.blocks[(size_t) r.block];
    Block part;
    part.lines = r.line1 - r.line0 + 1;
    for (int l = 0; l < part.lines; ++l)
        for (int t = 0; t < kMaxTracks; ++t)
        {
            Cell c = src.at (r.line0 + l, t);
            // (no jumping away, or stopping)
            auto drop = [] (uint8_t& cmd, uint8_t& p) { if (cmd == 0xB || cmd == 0xD || (cmd == 0xF && p == 0)) { cmd = 0; p = 0; } };
            drop (c.cmd, c.param); drop (c.cmd2, c.param2);
            part.at (l, t) = c;
        }
    // the first note on each track knows its instrument
    for (int t = r.track0; t <= r.track1; ++t)
        for (int l = 0; l < part.lines; ++l)
        {
            auto& c = part.at (l, t);
            if (c.note == 0 || c.note == kNoteOff) continue;
            if (c.instr == 0) { const int i = instrumentBefore (src, r.line0 + l, t); if (i >= 0) c.instr = (uint8_t) (i + 1); }
            break;
        }
    // the tempo and speed in force at the first line
    for (int l = 0; l < r.line0; ++l)
        for (int t = 0; t < kMaxTracks; ++t)
        {
            const auto& c = src.at (l, t);
            for (auto [cmd, p] : { std::pair<uint8_t, uint8_t> { c.cmd, c.param }, { c.cmd2, c.param2 } })
                if (cmd == 0xF && p > 0) { if (p < 0x20) s.speed = p; else s.tempo = p; }
        }
    Block tail;
    tail.lines = kMaxLines;
    s.blocks = { part, tail };
    s.sequence = { 0, 1 };
    s.sections.clear();

    auto heldPlayer = std::make_unique<Player>();   // (on the heap: a Player is ~470 KB)
    Player& p = *heldPlayer;
    p.options = options;
    p.setSampleRate (sampleRate);
    p.synths().factory = [f = std::move (synthFactory)] (int i, const Instrument& ins, double sr) -> std::unique_ptr<PluginSynth>
    {
        return ins.isMidiOut() || ! f ? nullptr : f (i, ins, sr);
    };
    p.synths().syncNow (s, sampleRate);
    p.synths().prepare (sampleRate, MixerEngine::kMaxBlock);
    p.setSong (&s);
    p.forceMixPath = true;
    p.stemsBeforeMixer = ! r.withFx;
    p.skipInstrumentFx = ! r.instrumentFx;
    if (r.withFx || r.instrumentFx)
    {
        p.mixer().pluginFactory = std::move (pluginFactory);
        p.mixer().syncEffectsNow (s.mixer);
    }
    for (int t = 0; t < kMaxTracks; ++t) p.mute[t] = t < r.track0 || t > r.track1;

    Collect tap;
    tap.t0 = r.track0; tap.t1 = r.track1; tap.returns = r.withFx;
    tap.L = &out.left; tap.R = &out.right;
    p.stemTap = &tap;
    const int block = 1024;
    std::vector<float> scratchL ((size_t) block), scratchR ((size_t) block);
    auto renderBlock = [&]
    {
        tap.base = out.left.size();
        for (auto& f : tap.fill) f = 0;
        p.render (scratchL.data(), scratchR.data(), block);
        float pk = 0;
        for (size_t i = tap.base; i < out.left.size(); ++i) pk = std::max ({ pk, std::abs (out.left[i]), std::abs (out.right[i]) });
        return pk;
    };

    p.playSong (0, 0);
    const auto maxRange = (size_t) (sampleRate * 60.0 * 5.0);   // (5 minutes is plenty)
    while (p.isPlaying() && p.uiSeqPos.load() == 0 && p.songWraps.load() == 0 && out.left.size() < maxRange) renderBlock();
    p.releaseForTail();
    const float quiet = std::pow (10.0f, -80.0f / 20.0f);
    size_t tailDone = 0, silent = 0;
    while (silent < (size_t) (sampleRate * 0.25) && tailDone < (size_t) (sampleRate * 10.0) && p.isPlaying())
    {
        silent = renderBlock() < quiet ? silent + (size_t) block : 0;
        tailDone += (size_t) block;
    }
    p.stemTap = nullptr;
    // cut the quiet end (keeping a few ms), and the plug-in instruments' delay at the start
    size_t end = out.left.size();
    while (end > 0 && std::abs (out.left[end - 1]) < quiet && std::abs (out.right[end - 1]) < quiet) --end;
    end = std::min (out.left.size(), end + (size_t) (sampleRate * 0.005));
    // dry: the plug-in instruments' delay on those tracks; with FX: the tracks are lined up with the slowest
    int lead = 0;
    if (r.withFx) for (int t = 0; t < std::clamp (s.numTracks, 1, kMaxTracks); ++t) lead = std::max (lead, p.mixer().trackLatency (t));
    else for (int t = r.track0; t <= r.track1; ++t) lead = std::max (lead, p.mixer().trackLatency (t));
    const size_t from = std::min ((size_t) lead, end);
    out.left.assign (out.left.begin() + (long) from, out.left.begin() + (long) end);
    out.right.assign (out.right.begin() + (long) from, out.right.begin() + (long) end);
    if (out.left.empty()) { error = "THE HIGHLIGHTED AREA IS SILENT"; return false; }
    return true;
}
} // namespace prv
