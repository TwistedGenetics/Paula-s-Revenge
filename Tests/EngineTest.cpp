// Engine tests: render, pitch check, MOD round-trip, loader fuzzing.
// Build: c++ -std=c++20 -O1 -g -fsanitize=address,undefined -I Source Tests/EngineTest.cpp Source/engine/*.cpp
#include "engine/Player.h"
#include "engine/ModuleIO.h"
#include "engine/SongOps.h"
#include "engine/Commands.h"
#include "engine/TimeStretch.h"
#include "engine/Slicer.h"
#include "engine/Mixer.h"
#include "engine/Effects.h"
#include "engine/PluginSynth.h"
#include "engine/Commands.h"
#include "AkaizerGolden.h"
#include <cstdio>
#include <set>
#include <functional>
#include <random>
#include <fstream>
#include <cstdlib>

using namespace prv;

static int failures = 0;
#define CHECK(cond) do { if (! (cond)) { std::printf ("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); ++failures; } } while (0)

static Song makeSong()
{
    Song s;
    s.name = "TEST TUNE";
    auto& sq = s.instruments[0];
    sq.name = "SQUARE";
    sq.data.resize (32);
    for (int i = 0; i < 32; ++i) sq.data[(size_t) i] = (int16_t) (i < 16 ? 20000 : -20000);
    sq.loopStart = 0; sq.loopLength = 32; sq.volume = 48;

    auto& nz = s.instruments[1];
    nz.name = "NOISE HAT";
    std::mt19937 rng (1);
    nz.data.resize (2000);
    for (int i = 0; i < 2000; ++i) nz.data[(size_t) i] = (int16_t) ((int) (rng() % 40000) - 20000) * (2000 - i) / 2000;

    s.blocks.assign (2, Block{});
    auto& b = s.blocks[0];
    for (int l = 0; l < 64; l += 4)
    {
        b.at (l, 0) = Cell { 25, 1, 0, 0 };          // C-2
        b.at (l + 2, 1) = Cell { 37, 2, 0xC, 0x20 };  // hat
    }
    b.at (8, 0)  = Cell { 25, 1, 0x0, 0x37 };         // arpeggio
    b.at (16, 0) = Cell { 25, 1, 0x4, 0x46 };         // vibrato
    b.at (20, 0) = Cell { 37, 0, 0x3, 0x08 };         // tone porta to C-3
    b.at (24, 2) = Cell { 0, 0, 0xE, 0x00 };          // LED on
    b.at (28, 2) = Cell { 0, 0, 0xE, 0x01 };          // LED off
    b.at (30, 3) = Cell { kNoteOff, 0, 0, 0 };
    b.at (40, 4) = Cell { 49, 1, 0xA, 0x02 };         // track 5 vol slide
    b.at (48, 5) = Cell { 49, 2, 0xE, 0x93 };         // retrig
    b.at (60, 6) = Cell { 0, 0, 0xF, 0x8C };          // tempo 140
    auto& b2 = s.blocks[1];
    b2.lines = 32;
    b2.at (0, 0) = Cell { 37, 1, 0, 0 };
    b2.at (10, 1) = Cell { 0, 0, 0xD, 0x00 };         // break
    s.sequence = { 0, 1, 0 };
    return s;
}

static void testRender()
{
    Song s = makeSong();
    Player p; p.setSampleRate (48000); p.setSong (&s);
    for (int mode = 0; mode < 3; ++mode)
    {
        p.options.zeroOrderHold = mode != 2;
        p.options.eightBit = mode != 2;
        p.options.model = mode == 0 ? PaulaOptions::Model::A500 : mode == 1 ? PaulaOptions::Model::A1200 : PaulaOptions::Model::Clean;
        p.playSong();
        std::vector<float> L (48000 * 10), R (L.size());
        p.render (L.data(), R.data(), (int) L.size());
        double sum = 0; float peak = 0; bool finite = true;
        for (size_t i = 0; i < L.size(); ++i)
        {
            finite &= std::isfinite (L[i]) && std::isfinite (R[i]);
            peak = std::max ({ peak, std::abs (L[i]), std::abs (R[i]) });
            sum += L[i] * L[i] + R[i] * R[i];
        }
        const double rms = std::sqrt (sum / (2.0 * L.size()));
        std::printf ("render mode %d: peak %.3f rms %.4f\n", mode, peak, rms);
        CHECK (finite); CHECK (peak <= 1.0f); CHECK (rms > 0.01);
        if (mode == 0)
        {
            if (const char* outPath = std::getenv ("PRV_RENDER_OUT"))
            {
                std::ofstream f (outPath, std::ios::binary);
                f.write ((const char*) L.data(), (std::streamsize) (L.size() * sizeof (float)));
            }
        }
    }
}

static void testPitch()
{
    // A looped 32-sample square at C-3 (period ~214) should be ~16574/32 = 518 Hz
    Song s = makeSong();
    s.blocks[0].clear();
    s.blocks[0].at (0, 0) = Cell { 37, 1, 0, 0 };
    s.sequence = { 0 };
    Player p; p.setSampleRate (48000); p.setSong (&s);
    p.options.model = PaulaOptions::Model::Clean; p.options.zeroOrderHold = false;
    p.playBlock (0);
    std::vector<float> L (48000), R (48000);
    p.render (L.data(), R.data(), 48000);
    int crossings = 0;
    for (size_t i = 1; i < L.size(); ++i) if ((L[i - 1] < 0) != (L[i] < 0)) ++crossings;
    const double hz = crossings / 2.0;
    std::printf ("pitch: %.1f Hz (expect ~%.1f)\n", hz, kPaulaClock / periodFor (37, 0) / 32.0);
    CHECK (std::abs (hz - kPaulaClock / periodFor (37, 0) / 32.0) < 3.0);
}

static void testModRoundTrip()
{
    Song s = makeSong();
    std::vector<uint8_t> bytes;
    auto w = saveMOD (s, bytes);
    CHECK (w.ok);
    for (auto& m : w.warnings) std::printf ("  save warning: %s\n", m.c_str());
    Song t;
    auto r = loadModule (bytes, t);
    CHECK (r.ok);
    CHECK (t.numTracks == 8);
    CHECK (t.sequence == s.sequence);
    CHECK (t.name == s.name);
    CHECK (t.instruments[0].name == "SQUARE");
    CHECK (t.instruments[0].isLooped());
    CHECK (t.instruments[0].data.size() == 32);
    CHECK (t.instruments[1].data[5] == (int16_t) ((s.instruments[1].data[5] >> 8) * 256));
    bool same = true;
    for (int l = 0; l < 64; ++l)
        for (int tr = 0; tr < 8; ++tr)
        {
            const Cell a = s.blocks[0].at (l, tr), b = t.blocks[0].at (l, tr);
            Cell expect = a;
            if (a.note == kNoteOff) { expect.note = 0; expect.cmd = 0xC; expect.param = 0; }
            // loading bakes the square's volume (48) into C commands in column 2
            if (a.instr == 1 && expect.cmd != 0xC) { expect.cmd2 = 0xC; expect.param2 = 48; }
            if (! (expect == b)) { same = false; std::printf ("  diff line %d track %d\n", l, tr); }
        }
    CHECK (same);
    CHECK (t.instruments[0].volume == 64);
    CHECK (t.blocks[1].at (31, 0).cmd == 0xD || t.blocks[1].at (31, 2).cmd == 0xD);
}

static void testFuzz()
{
    std::mt19937 rng (42);
    Song s = makeSong();
    std::vector<uint8_t> mod;
    saveMOD (s, mod);
    int loaded = 0;
    for (int iter = 0; iter < 3000; ++iter)
    {
        std::vector<uint8_t> d = mod;
        if (iter % 3 == 0) d.resize (rng() % (d.size() + 1));
        const int flips = 1 + (int) (rng() % 50);
        for (int k = 0; k < flips && ! d.empty(); ++k) d[rng() % d.size()] = (uint8_t) rng();
        if (iter % 5 == 0 && d.size() >= 4) { d[0] = 'M'; d[1] = 'M'; d[2] = 'D'; d[3] = (iter % 10 == 0) ? '0' : '1'; }
        Song t;
        auto r = loadModule (d, t);
        if (r.ok)
        {
            ++loaded;
            Player p; p.setSampleRate (44100); p.setSong (&t);
            p.playSong();
            std::vector<float> L (4410), R (4410);
            p.render (L.data(), R.data(), 4410);
        }
    }
    std::printf ("fuzz: %d/3000 loaded without crashing\n", loaded);
}

static void test8SVX()
{
    std::vector<uint8_t> d = { 'F','O','R','M', 0,0,0,0, '8','S','V','X',
                               'V','H','D','R', 0,0,0,20,  0,0,0,4, 0,0,0,8, 0,0,0,0, 0x20,0xAB, 1, 0, 0,1,0,0,
                               'B','O','D','Y', 0,0,0,12, 1,2,3,4,5,6,7,8,9,10,11,12 };
    Instrument in; double sr = 0;
    CHECK (load8SVX (d, in, sr));
    CHECK (in.data.size() == 12);
    CHECK ((int) sr == 0x20AB);
    CHECK (in.loopStart == 4 && in.loopLength == 8);
}


static void testMED()
{
    // Hand-built MMD0: 1 block (4 tracks, 4 lines), 1 sample
    std::vector<uint8_t> d (2000, 0);
    auto w32 = [&] (size_t o, uint32_t v) { d[o] = (uint8_t) (v >> 24); d[o+1] = (uint8_t) (v >> 16); d[o+2] = (uint8_t) (v >> 8); d[o+3] = (uint8_t) v; };
    auto w16 = [&] (size_t o, uint16_t v) { d[o] = (uint8_t) (v >> 8); d[o+1] = (uint8_t) v; };
    d[0]='M'; d[1]='M'; d[2]='D'; d[3]='0';
    const size_t song = 52, blockArr = 900, blk = 910, smplArr = 1000, smp = 1010;
    w32 (8, song); w32 (16, blockArr); w32 (24, smplArr);
    // sample 0: rep 2 words, replen 4 words, vol 40, transpose 0
    w16 (song + 0, 2); w16 (song + 2, 4); d[song + 6] = 40;
    w16 (song + 504, 1);    // numblocks
    w16 (song + 506, 2);    // songlen
    d[song + 508] = 0; d[song + 509] = 0;
    w16 (song + 764, 33);   // deftempo -> ~125 bpm
    d[song + 767] = 0x10;   // volhex
    d[song + 769] = 3;      // secondary tempo = speed 3
    d[song + 787] = 1;      // numsamples
    w32 (blockArr, blk);
    d[blk] = 4; d[blk + 1] = 3;   // 4 tracks, 4 lines
    // line 0 track 0: note 13 (C-2 in MED), instr 1, cmd C data 0x20
    d[blk + 2] = 13; d[blk + 3] = 0x1C; d[blk + 4] = 0x20;
    // line 1 track 1: instr 33 (bit5 set via b0 bit6), note 1, cmd 0xF data 0xFF (note off)
    d[blk + 2 + (1*4 + 1) * 3] = 0x40 | 1; d[blk + 3 + (1*4 + 1) * 3] = 0x1F; d[blk + 4 + (1*4 + 1) * 3] = 0xFF;
    w32 (smplArr, smp);
    w32 (smp, 16); w16 (smp + 4, 0);
    for (int i = 0; i < 16; ++i) d[smp + 6 + (size_t) i] = (uint8_t) (i * 8);
    Song t;
    auto r = loadModule (d, t);
    for (auto& m : r.warnings) std::printf ("  med warning: %s\n", m.c_str());
    if (! r.ok) std::printf ("  med error: %s\n", r.error.c_str());
    CHECK (r.ok);
    CHECK (t.blocks.size() == 1 && t.blocks[0].lines == 4);
    CHECK (t.numTracks == 4);
    CHECK (t.speed == 3 && t.tempo == 125);
    CHECK (t.sequence.size() == 2);
    const Cell c = t.blocks[0].at (0, 0);
    CHECK (c.note == 25 && c.instr == 1 && c.cmd == 0xC && c.param == 0x20);
    const Cell c2 = t.blocks[0].at (1, 1);
    CHECK (c2.instr == 33 && c2.note == kNoteOff);
    CHECK (t.instruments[0].data.size() == 16 && t.instruments[0].volume == 64);   // its 40 went onto the notes (that one has C20 already)
    CHECK (t.instruments[0].loopStart == 4 && t.instruments[0].loopLength == 8);
}

static void testStereo()
{
    // left = square, right = silence: a stereo sample must stay stereo,
    // even on track 1 (which the Amiga panning would send hard left)
    Song s;
    auto& in = s.instruments[0];
    in.data.resize (64); in.dataR.assign (64, 0);
    for (int i = 0; i < 64; ++i) in.data[(size_t) i] = (int16_t) (i < 32 ? 20000 : -20000);
    in.loopStart = 0; in.loopLength = 64; in.volume = 64;
    CHECK (in.isStereo());
    s.numTracks = 4;
    s.blocks[0].at (0, 1) = Cell { 37, 1, 0, 0 };   // track 2 = Amiga right side
    s.sequence = { 0 };
    Player p; p.setSampleRate (48000); p.setSong (&s);
    p.options.model = PaulaOptions::Model::Clean;
    p.playBlock (0);
    std::vector<float> L (9600), R (9600);
    p.render (L.data(), R.data(), 9600);
    double eL = 0, eR = 0;
    for (size_t i = 0; i < L.size(); ++i) { eL += L[i] * L[i]; eR += R[i] * R[i]; }
    std::printf ("stereo: left energy %.2f right energy %.4f\n", eL, eR);
    CHECK (eL > 10.0);
    CHECK (eR < eL * 0.001);

    // MOD export mixes stereo down and says so
    std::vector<uint8_t> bytes;
    auto w = saveMOD (s, bytes);
    CHECK (w.ok);
    bool warned = false;
    for (auto& m : w.warnings) warned |= m.find ("Stereo") != std::string::npos;
    CHECK (warned);
}

// mono drum-like sample, looped noise
static Instrument drum()
{
    Instrument in;
    std::mt19937 rng (3);
    in.data.resize (4000);
    for (auto& v : in.data) v = (int16_t) ((int) (rng() % 40000) - 20000);
    in.loopStart = 0; in.loopLength = 4000; in.volume = 64;
    return in;
}

static void energies (Player& p, int n, double& eL, double& eR)
{
    std::vector<float> L ((size_t) n), R ((size_t) n);
    p.render (L.data(), R.data(), n);
    eL = eR = 0;
    for (int i = 0; i < n; ++i) { eL += L[(size_t) i] * L[(size_t) i]; eR += R[(size_t) i] * R[(size_t) i]; }
}

static void testBalance()
{
    // a mono drum on every track at default settings: left and right close in level
    for (int tracks : { 4, 8 })
        for (int crunch = 0; crunch < 2; ++crunch)
        {
            Song s; s.numTracks = tracks;
            s.instruments[0] = drum();
            for (int t = 0; t < tracks; ++t) s.blocks[0].at (0, t) = Cell { 37, 1, 0, 0 };
            s.sequence = { 0 };
            Player p; p.setSampleRate (48000); p.setSong (&s);
            p.options.crunch8ch = crunch;
            p.playBlock (0);
            double eL, eR; energies (p, 24000, eL, eR);
            const double db = 10.0 * std::log10 (eL / eR);
            std::printf ("balance %d tracks crunch %d: L/R %.2f dB\n", tracks, crunch, db);
            CHECK (std::abs (db) < 1.0);
        }
    // a single drum on track 1: default SEP (20%) must stay in both ears
    {
        Song s; s.numTracks = 4; s.instruments[0] = drum();
        s.blocks[0].at (0, 0) = Cell { 37, 1, 0, 0 }; s.sequence = { 0 };
        Player p; p.setSampleRate (48000); p.setSong (&s); p.playBlock (0);
        double eL, eR; energies (p, 24000, eL, eR);
        const double db = 10.0 * std::log10 (eL / eR);
        std::printf ("track 1 alone at default SEP: L/R %.2f dB\n", db);
        CHECK (db > 1.0 && db < 6.0);
        // track pan centre overrides the Amiga side
        s.trackPan[0] = 128;
        p.playBlock (0); energies (p, 24000, eL, eR);
        std::printf ("track 1 with track pan centre: L/R %.2f dB\n", 10.0 * std::log10 (eL / eR));
        CHECK (std::abs (10.0 * std::log10 (eL / eR)) < 0.2);
        // instrument pan hard right
        s.trackPan[0] = -1; s.instruments[0].amigaPan = false; s.instruments[0].pan = 255;
        p.playBlock (0); energies (p, 24000, eL, eR);
        CHECK (eL < eR * 0.01);
        // 8xx beats both
        s.trackPan[0] = 0; s.blocks[0].at (0, 0).cmd = 8; s.blocks[0].at (0, 0).param = 128;
        p.playBlock (0); energies (p, 24000, eL, eR);
        CHECK (std::abs (10.0 * std::log10 (eL / eR)) < 0.2);
    }
}

static void testEnvelopeAndFilter()
{
    Song s; s.numTracks = 4; s.instruments[0] = drum();
    s.blocks[0].at (0, 1) = Cell { 37, 1, 0, 0 };
    s.blocks[0].at (4, 1) = Cell { kNoteOff, 0, 0, 0 };
    s.sequence = { 0 };
    Player p; p.setSampleRate (48000); p.setSong (&s); p.options.model = PaulaOptions::Model::Clean;
    // without an envelope, OFF cuts dead; at speed 6 / 125 bpm a line is 5760 samples
    const int off = 4 * 5760;
    std::vector<float> L (72000), R (72000);
    p.playBlock (0);
    p.render (L.data(), R.data(), 72000);
    auto rms = [&] (int a, int b) { double e = 0; for (int i = a; i < b; ++i) e += R[(size_t) i] * R[(size_t) i]; return std::sqrt (e / (b - a)); };
    const double cut = rms (off + 400, off + 2000);
    // with a 0.5 s release the note keeps sounding after OFF, then dies away
    s.instruments[0].volEnv = { true, 0.001f, 0.1f, 1.0f, 0.5f };
    p.playBlock (0);
    p.render (L.data(), R.data(), 72000);
    const double held = rms (off + 400, off + 2000);
    const double gone = rms (off + 25000, off + 27000);
    std::printf ("envelope: after OFF no-env %.4f, release %.4f, end of release %.4f\n", cut, held, gone);
    CHECK (cut < 1e-6 && held > 0.05 && gone < 1e-6);
    // attack: starts quiet
    s.instruments[0].volEnv = { true, 0.2f, 0.1f, 1.0f, 0.1f };
    p.playBlock (0);
    p.render (L.data(), R.data(), 48000);
    CHECK (rms (0, 400) < rms (2400, 2800) * 0.3);

    // low-pass at 300 Hz removes most of the noise's energy
    s.instruments[0].volEnv.on = false;
    s.blocks[0].at (4, 1) = Cell{};
    p.playBlock (0); p.render (L.data(), R.data(), 9600);
    const double open = rms (2000, 9600);
    s.instruments[0].filter = FilterType::LowPass; s.instruments[0].cutoff = 300.0f;
    p.playBlock (0); p.render (L.data(), R.data(), 9600);
    const double closed = rms (2000, 9600);
    // filter envelope opening it back up
    s.instruments[0].filterEnv = { true, 0.001f, 5.0f, 1.0f, 0.1f };
    s.instruments[0].filterEnvAmount = 6.0f;
    p.playBlock (0); p.render (L.data(), R.data(), 9600);
    const double envd = rms (2000, 9600);
    std::printf ("filter: open %.4f, LP 300 Hz %.4f, with +6 oct env %.4f\n", open, closed, envd);
    CHECK (closed < open * 0.4 && envd > closed * 1.5);
    bool finite = true; for (float v : R) finite &= std::isfinite (v);
    s.instruments[0].filter = FilterType::HighPass; s.instruments[0].resonance = 1.0f; s.instruments[0].cutoff = 19000.0f;
    p.playBlock (0); p.render (L.data(), R.data(), 48000);
    for (float v : R) finite &= std::isfinite (v) && std::abs (v) < 2.0f;
    s.instruments[0].filter = FilterType::BandPass;
    p.playBlock (0); p.render (L.data(), R.data(), 48000);
    for (float v : R) finite &= std::isfinite (v) && std::abs (v) < 2.0f;
    CHECK (finite);

    // shaping can't go in a MOD: warned
    std::vector<uint8_t> bytes;
    auto w = saveMOD (s, bytes);
    bool warned = false; for (auto& m : w.warnings) warned |= m.find ("Envelopes") != std::string::npos;
    CHECK (warned);
}

static void testSongOps()
{
    Song s;
    s.blocks.assign (4, Block{});
    for (int b = 0; b < 4; ++b) s.blocks[(size_t) b].at (0, 0).note = (uint8_t) (10 + b);   // mark each block
    s.sequence = { 0, 1, 2, 1, 3 };
    auto noteAt = [&] (int pos) { return (int) s.blocks[(size_t) s.sequence[(size_t) pos]].at (0, 0).note; };
    std::vector<int> before; for (int i = 0; i < 5; ++i) before.push_back (noteAt (i));

    // clone in place at position 1 (block 1): no free block, so new block 4; nothing renumbered
    CHECK (cloneInPlace (s, 1) == 4);
    CHECK (s.blocks.size() == 5);
    CHECK ((s.sequence == std::vector<int> { 0, 4, 2, 1, 3 }));
    for (int i = 0; i < 5; ++i) CHECK (noteAt (i) == before[(size_t) i]);   // sounds identical
    // an empty block not used in the song is the next free one
    {
        Song t;
        t.blocks.assign (4, Block{});
        t.blocks[0].at (0, 0).note = 10; t.blocks[2].at (5, 3).cmd = 0xC;   // block 1 and 3 empty
        t.sequence = { 0, 2, 3 };                                            // 3 is used, 1 is free
        CHECK (nextFreeBlock (t) == 1);
        CHECK (cloneInPlace (t, 1) == 1 && t.blocks.size() == 4 && (t.sequence == std::vector<int> { 0, 1, 3 }));
        CHECK (t.blocks[1].at (5, 3).cmd == 0xC);
    }

    // swap block numbers 0 and 1: music unchanged
    CHECK (swapBlockNumbers (s, 0, 1));
    for (int i = 0; i < 5; ++i) CHECK (noteAt (i) == before[(size_t) i]);
    CHECK (s.sequence[0] == 1);

    // move position 4 to the front
    CHECK (moveSequenceEntry (s, 4, 0));
    CHECK (noteAt (0) == before[4] && noteAt (1) == before[0]);

    // block limit
    Song full; full.blocks.assign (kMaxBlocks, Block{}); full.sequence = { 0 };
    for (auto& b : full.blocks) b.at (0, 0).note = 1;   // nothing free
    CHECK (cloneInPlace (full, 0) == -1);
}

// ---------------------------------------------------------------- v0.4 tests
static Song oneShot (std::vector<int16_t> data, bool loop = true)
{
    Song s; s.numTracks = 4;
    auto& in = s.instruments[0];
    in.data = std::move (data); in.volume = 64;
    if (loop) { in.loopStart = 0; in.loopLength = (int) in.data.size(); }
    s.sequence = { 0 };
    return s;
}

static std::vector<float> renderTrack (Song& s, int n, int sr = 48000)
{
    Player p; p.setSampleRate (sr); p.setSong (&s);
    p.options.model = PaulaOptions::Model::Clean; p.options.zeroOrderHold = false; p.options.eightBit = false;
    p.options.stereoSep = 0.0f;
    p.playBlock (0);
    std::vector<float> L ((size_t) n), R ((size_t) n);
    p.render (L.data(), R.data(), n);
    for (int i = 0; i < n; ++i) L[(size_t) i] += R[(size_t) i];
    return L;
}

static int firstSound (const std::vector<float>& v, float thr = 1e-4f)
{
    for (size_t i = 0; i < v.size(); ++i) if (std::abs (v[i]) > thr) return (int) i;
    return -1;
}

static double crossingsHz (const std::vector<float>& v, int a, int b, int sr)
{
    int c = 0;
    for (int i = a + 1; i < b; ++i) if ((v[(size_t) i - 1] < 0) != (v[(size_t) i] < 0)) ++c;
    return c / 2.0 / ((b - a) / (double) sr);
}

static std::vector<int16_t> square (int period, int len)
{
    std::vector<int16_t> d ((size_t) len);
    for (int i = 0; i < len; ++i) d[(size_t) i] = (int16_t) ((i % period) < period / 2 ? 16000 : -16000);
    return d;
}

static void testExtendedCommands()
{
    const int line = 5760;   // speed 6, 125 bpm, 48 kHz
    // describe every command
    for (int c = 0; c < kNumCommands; ++c) CHECK (! describeCommand (c, 0x40).empty());
    CHECK (commandChar (cmdLetter ('L')) == 'L' && commandFromChar ('l') == cmdLetter ('L'));

    // second column: volume in column 1, low-pass in column 2
    {
        Song s = oneShot (square (8, 64));
        s.blocks[0].at (0, 0) = { 37, 1, 0xC, 0x40 };
        const auto open = renderTrack (s, 9600);
        s.blocks[0].at (0, 0).cmd2 = cmdLetter ('L'); s.blocks[0].at (0, 0).param2 = 0x30;
        const auto shut = renderTrack (s, 9600);
        double eo = 0, es = 0; for (int i = 2000; i < 9600; ++i) { eo += open[(size_t) i] * open[(size_t) i]; es += shut[(size_t) i] * shut[(size_t) i]; }
        std::printf ("2nd column low-pass: energy %.1f -> %.1f\n", eo, es);
        CHECK (es < eo * 0.2);
    }
    // chance: K00 never, KFF always
    {
        Song s = oneShot (square (8, 64));
        s.blocks[0].at (0, 0) = { 37, 1, cmdLetter ('K'), 0x00 };
        CHECK (firstSound (renderTrack (s, 4000)) < 0);
        s.blocks[0].at (0, 0).param = 0xFF;
        CHECK (firstSound (renderTrack (s, 4000)) == 0 || firstSound (renderTrack (s, 4000)) < 5);
    }
    // micro-move: M40 = half a line late; MC0 on line 1 = half a line early
    {
        Song s = oneShot (square (8, 64));
        s.blocks[0].at (0, 0) = { 37, 1, cmdLetter ('M'), 0x40 };
        const int late = firstSound (renderTrack (s, 2 * line));
        s.blocks[0].at (0, 0) = Cell{};
        s.blocks[0].at (1, 0) = { 37, 1, cmdLetter ('M'), 0xC0 };
        const int early = firstSound (renderTrack (s, 2 * line));
        std::printf ("micro-move: late onset %d (expect ~%d), early onset %d (expect ~%d)\n", late, line / 2, early, line / 2);
        CHECK (std::abs (late - line / 2) < 50 && std::abs (early - line / 2) < 50);
    }
    // gate: N03 cuts after 3 ticks (no envelope)
    {
        Song s = oneShot (square (8, 64));
        s.blocks[0].at (0, 0) = { 37, 1, cmdLetter ('N'), 3 };
        const auto v = renderTrack (s, line);
        double after = 0; for (int i = 3 * 960 + 100; i < line; ++i) after += std::abs (v[(size_t) i]);
        CHECK (after < 1e-3);
    }
    // reverse: a rising ramp played backwards starts high
    {
        std::vector<int16_t> ramp (4000);
        for (int i = 0; i < 4000; ++i) ramp[(size_t) i] = (int16_t) (i * 8);
        Song s = oneShot (ramp, false);
        s.blocks[0].at (0, 0) = { 37, 1, cmdLetter ('U'), 1 };
        const auto v = renderTrack (s, 200);
        std::printf ("reverse: first sample %.3f\n", v[2]);
        CHECK (v[2] > 0.5f);
    }
    // slice: 16 flat slices with different levels, S05 plays the 6th
    {
        std::vector<int16_t> d (1600);
        for (int i = 0; i < 1600; ++i) d[(size_t) i] = (int16_t) ((i / 100) * 1000);
        Song s = oneShot (d, false);
        s.blocks[0].at (0, 0) = { 37, 1, cmdLetter ('S'), 5 };
        const auto v = renderTrack (s, 6000);
        const float expect = 5000.0f / 32768.0f;
        std::printf ("slice: level %.3f (expect %.3f)\n", v[10], expect);
        CHECK (std::abs (v[10] - expect) < 0.01f);
        // and stops at the end of the slice (100 samples at 16574 Hz ~ 290 output samples)
        CHECK (std::abs (v[1000]) < 1e-6f);
    }
    // micro-tune and per-instrument rate both change the pitch
    {
        Song s = oneShot (square (32, 64));
        s.blocks[0].at (0, 0) = { 37, 1, 0, 0 };
        const double base = crossingsHz (renderTrack (s, 48000), 1000, 48000, 48000);
        s.blocks[0].at (0, 0) = { 37, 1, cmdLetter ('T'), (uint8_t) (128 + 100) };   // +100 cents
        const double tuned = crossingsHz (renderTrack (s, 48000), 1000, 48000, 48000);
        s.blocks[0].at (0, 0) = { 37, 1, 0, 0 };
        s.instruments[0].rate = kAmigaC3Rate * 2.0;
        const double fast = crossingsHz (renderTrack (s, 48000), 1000, 48000, 48000);
        std::printf ("pitch: base %.1f, +100c %.1f (expect %.1f), rate x2 %.1f\n", base, tuned, base * std::pow (2.0, 1.0 / 12.0), fast);
        CHECK (std::abs (tuned - base * std::pow (2.0, 1.0 / 12.0)) < 3.0);
        CHECK (std::abs (fast - base * 2.0) < 3.0);
    }
    // roll, glide, overdrive, crush, random: run and stay finite
    {
        Song s = oneShot (square (8, 64));
        s.instruments[1] = s.instruments[0];
        s.blocks[0].at (0, 0) = { 37, 1, cmdLetter ('R'), 0x23, cmdLetter ('O'), 0xC0 };
        s.blocks[0].at (2, 0) = { 49, 1, cmdLetter ('G'), 0x06, cmdLetter ('Z'), 4 };
        s.blocks[0].at (4, 0) = { 37, 2, cmdLetter ('X'), 0x05, cmdLetter ('X'), 0x23 };
        s.blocks[0].at (6, 0) = { 37, 1, cmdLetter ('H'), 0x80, cmdLetter ('Q'), 0xF0 };
        s.blocks[0].at (8, 0) = { 37, 1, cmdLetter ('W'), 0x80, cmdLetter ('P'), 0x80 };
        const auto v = renderTrack (s, 12 * line);
        bool ok = true; for (float x : v) ok &= std::isfinite (x) && std::abs (x) < 4.0f;
        CHECK (ok);
    }
    // MOD export drops G-Z and the second column, converts rates, and says so
    {
        Song s = oneShot (square (8, 64));
        s.instruments[0].rate = 32000.0;
        s.blocks[0].at (0, 0) = { 37, 1, cmdLetter ('L'), 0x40, 0xC, 0x20 };
        std::vector<uint8_t> bytes;
        auto w = saveMOD (s, bytes);
        bool fx = false, rate = false;
        for (auto& m : w.warnings) { fx |= m.find ("G-Z") != std::string::npos; rate |= m.find ("rate") != std::string::npos; }
        CHECK (fx && rate);
        Song back; CHECK (loadModule (bytes, back).ok);
        CHECK (back.blocks[0].at (0, 0).cmd == 0xC && back.blocks[0].at (0, 0).param == 0x20);   // column 2 kept as the one PT command
        CHECK (std::abs ((int) back.instruments[0].data.size() - (int) std::lround (64 / (32000.0 / kAmigaC3Rate))) <= 2);
    }
}

static void testTimeStretch()
{
    const int sr = 44100;
    std::vector<float> sine ((size_t) sr);
    for (int i = 0; i < sr; ++i) sine[(size_t) i] = 0.5f * std::sin (2.0 * kPi * 441.0 * i / sr);
    for (auto mode : { StretchSettings::Mode::Classic, StretchSettings::Mode::Revised, StretchSettings::Mode::Hard })
    {
        StretchSettings st; st.mode = mode; st.cycleLength = 1000; st.sampleRate = sr;
        st.timeFactor = 2.0;
        auto out = cyclicStretch ({ sine }, st)[0];
        CHECK (std::abs ((int) out.size() - 2 * sr) <= 1);
        const double hz = crossingsHz (out, 1000, (int) out.size() - 1000, sr);
        st.timeFactor = 1.0; st.semitones = 12.0;
        auto up = cyclicStretch ({ sine }, st)[0];
        const double hzUp = crossingsHz (up, 1000, (int) up.size() - 2000, sr);
        std::printf ("stretch mode %d: x2 length keeps %.1f Hz (expect 441), +12 st gives %.1f Hz (expect 882)\n", (int) mode, hz, hzUp);
        CHECK (std::abs (hz - 441.0) < 441.0 * 0.05);
        CHECK (std::abs (hzUp - 882.0) < 882.0 * 0.08);
        CHECK (up.size() == sine.size());
    }
    // joins: CLASSIC/REVISED don't click (like the S3000XL recording), HARD does
    for (auto mode : { StretchSettings::Mode::Classic, StretchSettings::Mode::Revised, StretchSettings::Mode::Hard })
    {
        StretchSettings st; st.mode = mode; st.timeFactor = 1.37; st.cycleLength = 500; st.sampleRate = sr;
        auto out = cyclicStretch ({ sine }, st)[0];
        const int C = cycleSamples (st);
        double atJoin = 0, elsewhere = 0; int nj = 0, ne = 0;
        // compare the step across each join with the steps right next to it
        // (a 441 Hz sine and 500-sample cycles always join at the same phase)
        for (size_t i = (size_t) C; i + 2 < out.size(); i += (size_t) C)
        {
            atJoin += std::abs (out[i] - out[i - 1]); ++nj;
            elsewhere += 0.5 * (std::abs (out[i - 1] - out[i - 2]) + std::abs (out[i + 1] - out[i])); ++ne;
        }
        const double ratio = (atJoin / nj) / (elsewhere / ne);
        std::printf ("joins mode %d: step at joins %.2fx a normal step\n", (int) mode, ratio);
        if (mode == StretchSettings::Mode::Hard) CHECK (ratio > 3.0);
        else CHECK (ratio < 1.5);
    }
    // CLASSIC = the measured Akai model: fixed cycles, each starting at output/T,
    // linear crossfade over 2/3 of a cycle from the previous grain's continuation
    {
        std::vector<float> noise (20000); uint32_t r = 1;
        for (auto& v : noise) { r = r * 1664525u + 1013904223u; v = (float) ((r >> 9) & 0xffff) / 32768.0f - 1.0f; }
        StretchSettings st; st.timeFactor = 2.0; st.cycleLength = 300; st.sampleRate = 44100;
        auto out = cyclicStretch ({ noise }, st)[0];
        const int C = 300, X = 200;
        double err = 0;
        for (int j = 0; j < C; ++j)
        {
            const float w = j < X ? (float) (j + 1) / (float) (X + 1) : 1.0f;
            const float expect = noise[(size_t) (C / 2 + j)] * w + noise[(size_t) (C + j)] * (1.0f - w);
            err = std::max (err, (double) std::abs (out[(size_t) (C + j)] - expect));
        }
        CHECK (err < 1e-5);
    }
    // cycle length is in 44.1 kHz samples: 1000 at the Amiga rate is ~376 samples
    {
        StretchSettings st; st.cycleLength = 1000; st.sampleRate = kAmigaC3Rate;
        CHECK (std::abs (cycleSamples (st) - 376) <= 1);
    }
    // stereo channels stay the same length and aligned
    {
        StretchSettings st; st.timeFactor = 0.5; st.sampleRate = sr;
        auto out = cyclicStretch ({ sine, sine }, st);
        CHECK (out.size() == 2 && out[0] == out[1] && std::abs ((int) out[0].size() - sr / 2) <= 1);
    }
}

// ---------------------------------------------------------------- v0.9: plug-in slots
namespace
{
    struct GainFx : Effect
    {
        float g; int* alive;
        GainFx (float gain, int* a) : g (gain), alive (a) { ++*alive; }
        ~GainFx() override { --*alive; }
        void prepare (double) override {}
        void reset() override {}
        void process (float* l, float* r, int n, const float*, double) override { for (int i = 0; i < n; ++i) { l[i] *= g; r[i] *= g; } }
    };
}

static void testPluginSlots()
{
    MixerEngine mx;
    mx.prepare (48000);
    int alive = 0, made = 0;
    mx.pluginFactory = [&] (int, const FxSlot& s, double) -> std::unique_ptr<Effect>
    {
        if (s.pluginKey == "missing") return nullptr;
        ++made;
        return std::make_unique<GainFx> (s.pluginKey == "half" ? 0.5f : 2.0f, &alive);
    };
    MixerSettings m;
    auto& sl = m.ch[0].inserts[0];
    sl.type = FxType::Plugin; sl.pluginKey = "half"; sl.pluginName = "HALF";
    CHECK (! m.isFlat());
    mx.syncEffectsNow (m);
    CHECK (made == 1 && alive == 1);
    // one track, 0.25 in: the plug-in halves it
    std::vector<float> L (64, 0.25f), R (64, 0.25f), oL (64), oR (64);
    float* tl[1] = { L.data() }; float* tr[1] = { R.data() };
    mx.process (m, tl, tr, 1, nullptr, nullptr, oL.data(), oR.data(), 64, 120);
    CHECK (std::abs (oL[10] - 0.125f) < 1e-6f);
    // same plug-in again: not reloaded; a different one: replaced
    auto changes = mx.plan (m);
    CHECK (changes.empty());
    sl.pluginKey = "double";
    std::vector<std::unique_ptr<Effect>> old;
    changes = mx.plan (m);
    CHECK (changes.size() == 1);
    mx.apply (changes, old);
    CHECK (old.size() == 1);
    old.clear();
    CHECK (alive == 1);
    std::fill (L.begin(), L.end(), 0.25f); std::fill (R.begin(), R.end(), 0.25f);
    mx.process (m, tl, tr, 1, nullptr, nullptr, oL.data(), oR.data(), 64, 120);
    CHECK (std::abs (oL[10] - 0.5f) < 1e-6f);
    // reloadAll / invalidate make a fresh one
    mx.invalidate (0);
    changes = mx.plan (m); CHECK (changes.size() == 1); mx.apply (changes, old); old.clear();
    changes = mx.plan (m, true); CHECK (changes.size() == 3);   /* + the two returns' effects */ mx.apply (changes, old); old.clear();
    CHECK (alive == 1);
    // a missing plug-in: the slot passes audio through, and isn't retried every time
    sl.pluginKey = "missing";
    mx.syncEffectsNow (m);
    CHECK (alive == 0 && mx.effectAt (0) == nullptr);
    CHECK (mx.plan (m).empty());
    std::fill (L.begin(), L.end(), 0.25f); std::fill (R.begin(), R.end(), 0.25f);
    mx.process (m, tl, tr, 1, nullptr, nullptr, oL.data(), oR.data(), 64, 120);
    CHECK (std::abs (oL[10] - 0.25f) < 1e-6f);
    // slots compare their plug-in and its state
    FxSlot a, b; a.type = b.type = FxType::Plugin; a.pluginKey = b.pluginKey = "x";
    CHECK (a == b);
    b.pluginState = { 1, 2, 3 };
    CHECK (! (a == b));
    CHECK (fxName (FxType::Plugin) == "PLUG-IN");
    std::printf ("plug-in slots: load, swap, reload, missing pass-through ok\n");
}

// ---------------------------------------------------------------- v0.10: automation + plug-in instruments
namespace
{
    struct Ev { char type; int a, b, offset; long long when; };   // n = note on, f = off, a = all off, b = bend, c = cc, p = param
    struct FakeSynth : PluginSynth
    {
        std::vector<Ev>* log; long long* clock; float level = 0.25f;
        FakeSynth (std::vector<Ev>* l, long long* c) : log (l), clock (c) {}
        void prepare (double, int) override {}
        void noteOn (int n, int v, int o) override { log->push_back ({ 'n', n, v, o, *clock + o }); }
        void noteOff (int n, int o) override { log->push_back ({ 'f', n, 0, o, *clock + o }); }
        void allNotesOff (int o) override { log->push_back ({ 'a', 0, 0, o, *clock + o }); }
        void pitchBend (int v, int o) override { log->push_back ({ 'b', v, 0, o, *clock + o }); }
        void controller (int c, int v, int o) override { log->push_back ({ 'c', c, v, o, *clock + o }); }
        void setParameter (int i, float v) override { log->push_back ({ 'p', i, (int) std::lround (v * 1000), 0, *clock }); }
        void render (float* l, float* r, int n) override { std::fill (l, l + n, level); std::fill (r, r + n, level); }
    };
    struct ParamFx : Effect
    {
        std::vector<std::pair<int, float>>* log;
        explicit ParamFx (std::vector<std::pair<int, float>>* l) : log (l) {}
        void prepare (double) override {}
        void reset() override {}
        void process (float*, float*, int, const float*, double) override {}
        void setParameter (int i, float v) override { log->push_back ({ i, v }); }
    };
}

static void testAutomation()
{
    // ---- V / J / Y on a plug-in effect in track 1's first insert
    {
        Song song;
        song.numTracks = 4;
        song.tempo = 125; song.speed = 6;
        auto& sl = song.mixer.ch[0].inserts[0];
        sl.type = FxType::Plugin; sl.pluginKey = "fx"; sl.pluginName = "FX";
        song.mixer.trackAuto[0][0] = { trackSlot (0, 0), 3, "FX: MIX" };
        song.mixer.trackAuto[0][1] = { trackSlot (0, 0), 7, "FX: SIZE" };
        auto& b = song.blocks[0];
        b.at (0, 0) = { 0, 0, cmdLetter ('V'), 0x80 };
        b.at (1, 0) = { 0, 0, cmdLetter ('Y'), 0x06, cmdLetter ('J'), 0xFF };   // glides over 6 ticks
        b.at (3, 0) = { 0, 0, cmdLetter ('V'), 0x00 };                          // jumps (the Y was used up)
        std::vector<std::pair<int, float>> log;
        Player p; p.setSampleRate (48000); p.setSong (&song);
        p.mixer().pluginFactory = [&log] (int, const FxSlot&, double) { return std::make_unique<ParamFx> (&log); };
        p.mixer().syncEffectsNow (song.mixer);
        p.playBlock (0);
        std::vector<float> L (48000), R (48000);
        p.render (L.data(), R.data(), (int) (48000 * 0.12 * 4));   // 4 lines
        std::vector<float> mix, size;
        for (auto& e : log) (e.first == 3 ? mix : size).push_back (e.second);
        std::printf ("automation: MIX values %zu (first %.3f, last %.3f), SIZE values %zu (first %.3f, last %.3f)\n",
                     mix.size(), mix.empty() ? -1.f : mix.front(), mix.empty() ? -1.f : mix.back(), size.size(), size.empty() ? -1.f : size.front(), size.empty() ? -1.f : size.back());
        CHECK (mix.size() == 2 && std::abs (mix[0] - 128 / 255.0f) < 1e-4f && mix[1] == 0.0f);
        // J without a value before it: no glide start point, so it jumps to FF
        CHECK (! size.empty() && size.back() == 1.0f);
        // a second J with a glide moves in steps
        song.blocks[0].at (5, 0) = { 0, 0, cmdLetter ('Y'), 0x06, cmdLetter ('J'), 0x00 };
        log.clear();
        p.playBlock (0);
        p.render (L.data(), R.data(), (int) (48000 * 0.12 * 7));
        size.clear();
        for (auto& e : log) if (e.first == 7) size.push_back (e.second);
        std::printf ("glide: %zu SIZE values:", size.size()); for (float v : size) std::printf (" %.2f", v); std::printf ("\n");
        CHECK (size.size() >= 7 && size.back() == 0.0f);
        bool down = true; for (size_t i = 2; i < size.size(); ++i) down = down && size[i] <= size[i - 1] + 1e-6f;
        CHECK (down);
    }
    // ---- a built-in effect is automated through its slot value
    {
        MixerEngine mx; mx.prepare (48000);
        MixerSettings m;
        initFxSlot (m.ch[0].inserts[0], FxType::Drive);
        m.ch[0].inserts[0].p[4] = 1.0f;   // mix
        m.ch[0].inserts[0].p[0] = 0.0f;   // no drive
        mx.syncEffectsNow (m);
        std::vector<float> a (256), b2 (256), o1 (256), o2 (256);
        for (int i = 0; i < 256; ++i) a[(size_t) i] = 0.3f * (float) std::sin (i * 0.1);
        auto run = [&] (std::vector<float>& out)
        {
            std::vector<float> l = a, r = a; float* tl[1] = { l.data() }; float* tr[1] = { r.data() };
            std::vector<float> outR (256);
            mx.process (m, tl, tr, 1, nullptr, nullptr, out.data(), outR.data(), 256, 120);
        };
        run (o1);
        mx.setAutomation (trackSlot (0, 0), 0, 1.0f);   // DRIVE all the way up
        run (o2);
        double d = 0; for (int i = 0; i < 256; ++i) d += std::abs (o1[(size_t) i] - o2[(size_t) i]);
        CHECK (d > 1.0);
        CHECK (m.ch[0].inserts[0].p[0] == 0.0f);   // the song's own value is untouched
    }
}

static void testPluginInstruments()
{
    Song song;
    song.numTracks = 4;
    auto& in = song.instruments[0];
    in.plugin = true; in.pluginKey = "synth"; in.pluginName = "SYNTH"; in.bendRange = 2;
    in.knobs[0] = { kAutoInstrumentBase, 11, "SYNTH: CUTOFF" };
    in.pluginOutput = 2;   // into track 3's strip
    auto& b = song.blocks[0];
    b.at (0, 1) = { 37, 1, 0xC, 0x20 };                      // C-3, velocity from C20
    b.at (2, 1) = { kNoteOff, 0, 0, 0 };
    b.at (4, 1) = { 39, 1, 0xE, 0xD2 };                      // delayed 2 ticks
    b.at (5, 1) = { 0, 0, 0x1, 0x01 };                       // slide up: pitch bend
    b.at (6, 1) = { 0, 0, cmdLetter ('L'), 0x80, cmdLetter ('O'), 0x40 };   // knob 1, mod wheel
    b.at (7, 1) = { 41, 1, 0xE, 0x92 };                      // retrigger every 2 ticks
    b.at (8, 1) = { 43, 1, cmdLetter ('S'), 0x03 };          // sample-only command: ignored
    std::vector<Ev> log; long long clock = 0;
    Player p; p.setSampleRate (48000); p.setSong (&song);
    p.synths().factory = [&] (int, const Instrument&, double) { return std::make_unique<FakeSynth> (&log, &clock); };
    p.synths().syncNow (song, 48000);
    CHECK (p.synths().any());
    p.playBlock (0);
    const int tickLen = (int) std::lround (48000 * 2.5 / 125);
    std::vector<float> L (512), R (512);
    float trackPeak3 = 0;
    for (int done = 0; done < tickLen * 6 * 10; done += 512)
    {
        p.render (L.data(), R.data(), 512);
        clock += 512;
        trackPeak3 = std::max (trackPeak3, p.uiLevel[2].load());
    }
    p.stop();
    auto first = [&] (char t, int a) { for (auto& e : log) if (e.type == t && (a < 0 || e.a == a)) return &e; return (Ev*) nullptr; };
    auto count = [&] (char t, int a) { int n = 0; for (auto& e : log) if (e.type == t && (a < 0 || e.a == a)) ++n; return n; };
    const Ev* on60 = first ('n', 60);
    std::printf ("synth: %zu events; C-3 on vel %d; off 60 at %lld (line 2 = %d); D-3 on at %lld (line 4 + 2 ticks = %d)\n",
                 log.size(), on60 ? on60->b : -1, first ('f', 60) ? first ('f', 60)->when : -1, tickLen * 12,
                 first ('n', 62) ? first ('n', 62)->when : -1, tickLen * 26);
    CHECK (on60 && on60->b == 64 && on60->when < 4);
    CHECK (first ('f', 60) && std::abs ((int) (first ('f', 60)->when - tickLen * 12)) <= 2);
    CHECK (first ('n', 62) && std::abs ((int) (first ('n', 62)->when - tickLen * 26)) <= 2);
    CHECK (count ('b', -1) >= 4);                                   // the slide bends
    bool up = false; for (auto& e : log) if (e.type == 'b' && e.a > 8192) up = true;
    CHECK (up);
    const Ev* knob = first ('p', 11);
    CHECK (knob && knob->b == 502);                                 // L80 = 128/255
    const Ev* cc = first ('c', 1);
    CHECK (cc && cc->b == 32);                                      // O40 -> mod wheel 32
    CHECK (count ('n', 64) >= 3);                                   // E92 retriggers E-3
    CHECK (count ('n', 66) == 1);                                   // the S03 note still plays
    CHECK (count ('a', -1) >= 1);                                   // stop: all notes off
    std::printf ("synth into track 3: level %.3f, track 2 %.3f\n", trackPeak3, p.uiLevel[1].load());
    CHECK (trackPeak3 > 0.2f);
    // AUTO output: the lowest track that played it
    song.instruments[0].pluginOutput = -1;
    CHECK (p.synthOutputTrack (0) == 1);
    // live notes
    log.clear();
    p.jamNoteOn (37, 0, 100);
    p.jamNoteOff (37);
    CHECK (log.size() == 2 && log[0].type == 'n' && log[0].a == 60 && log[0].b == 100 && log[1].type == 'f');
    // a missing synth: notes are silent, nothing breaks
    p.synths().factory = nullptr;
    p.synths().invalidate (0);
    p.synths().syncNow (song, 48000);
    CHECK (! p.synths().any());
    p.playBlock (0);
    p.render (L.data(), R.data(), 512);
    // command list knows V, J, Y
    int found = 0;
    for (auto& c : commandList()) if (c.cmd == cmdLetter ('V') || c.cmd == cmdLetter ('J') || c.cmd == cmdLetter ('Y')) ++found;
    CHECK (found == 3);
    CHECK (commandName (cmdLetter ('V')) == "AUTO A");
}

// ---------------------------------------------------------------- v0.10.3: switching effects and instruments on / off
static void testOnOff()
{
    // ---- an effect: V00 switches it off, V01-FF on (even when its BYPASS is pressed)
    {
        MixerEngine mx; mx.prepare (48000);
        MixerSettings m;
        initFxSlot (m.ch[0].inserts[0], FxType::Drive);
        m.ch[0].inserts[0].p[0] = 1.0f; m.ch[0].inserts[0].p[4] = 1.0f;   // full drive, all wet
        mx.syncEffectsNow (m);
        std::vector<float> a (256);
        for (int i = 0; i < 256; ++i) a[(size_t) i] = 0.3f * (float) std::sin (i * 0.1);
        auto run = [&]
        {
            std::vector<float> l = a, r = a, out (256), outR (256); float* tl[1] = { l.data() }; float* tr[1] = { r.data() };
            mx.process (m, tl, tr, 1, nullptr, nullptr, out.data(), outR.data(), 256, 120);
            double d = 0; for (int i = 0; i < 256; ++i) d += std::abs (out[(size_t) i] - a[(size_t) i]);
            return d;   // how far from the dry signal
        };
        const int slot = trackSlot (0, 0);
        const double on = run();
        mx.setAutomation (slot, kAutoOnOff, 0.0f);
        const double off = run();
        m.ch[0].inserts[0].bypass = true;
        const double bypassed = run();
        mx.setAutomation (slot, kAutoOnOff, 1.0f / 255.0f);
        const double forcedOn = run();
        mx.clearOnOff();
        const double backToButton = run();
        std::printf ("effect on/off: on %.2f, V00 %.4f, bypassed %.4f, V01 %.2f, cleared %.4f\n", on, off, bypassed, forcedOn, backToButton);
        CHECK (on > 1.0 && off < 1e-4 && bypassed < 1e-4 && forcedOn > 1.0 && backToButton < 1e-4);
        CHECK (mx.onOffOverride (slot) == -1);
    }
    // ---- a plug-in instrument: AUTO A switches it; notes while it's off don't play
    {
        Song song;
        song.numTracks = 4;
        auto& in = song.instruments[0];
        in.plugin = true; in.pluginKey = "synth"; in.pluginName = "SYNTH";
        in.knobs[1] = { kAutoInstrumentBase, kAutoOnOff, "SYNTH: ON/OFF" };   // Q
        song.mixer.trackAuto[0][0] = { kAutoInstrumentBase, kAutoOnOff, "SYNTH: ON/OFF" };
        auto& b = song.blocks[0];
        b.at (0, 0) = { 37, 1, 0, 0 };
        b.at (2, 0) = { 0, 0, cmdLetter ('V'), 0x00 };    // off: C-3 stops
        b.at (3, 0) = { 39, 1, 0, 0 };                    // not played
        b.at (5, 0) = { 0, 0, cmdLetter ('V'), 0xFF };    // on again
        b.at (6, 0) = { 41, 1, 0, 0 };                    // plays
        b.at (8, 0) = { 43, 1, cmdLetter ('Q'), 0x00 };   // its own knob Q switches it off on this note
        b.at (10, 0) = { 45, 1, 0, 0 };                   // not played
        std::vector<Ev> log; long long clock = 0;
        Player p; p.setSampleRate (48000); p.setSong (&song);
        p.synths().factory = [&] (int, const Instrument&, double) { return std::make_unique<FakeSynth> (&log, &clock); };
        p.synths().syncNow (song, 48000);
        p.playBlock (0);
        const int tickLen = (int) std::lround (48000 * 2.5 / 125);
        std::vector<float> L (512), R (512);
        float levelOff = 0, levelOn = 0;
        for (int done = 0; done < tickLen * 6 * 12; done += 512)
        {
            p.render (L.data(), R.data(), 512);
            clock += 512;
            const int line = p.uiLine.load();
            const float pk = p.uiLevel[0].exchange (0.0f);   // track 1's meter (the synth's DC would be blocked at the output)
            if (line == 3 || line == 4) levelOff = std::max (levelOff, pk);
            if (line == 6 || line == 7) levelOn = std::max (levelOn, pk);
        }
        auto count = [&] (char t, int a) { int n = 0; for (auto& e : log) if (e.type == t && (a < 0 || e.a == a)) ++n; return n; };
        std::printf ("instrument on/off: notes C-3 %d D-3 %d E-3 %d F#3 %d G#3 %d, level while off %.3f, on %.3f, offs %d\n",
                     count ('n', 60), count ('n', 62), count ('n', 64), count ('n', 66), count ('n', 68), levelOff, levelOn, count ('a', -1));
        CHECK (count ('n', 60) == 1 && count ('f', 60) == 1);
        CHECK (count ('n', 62) == 0 && count ('n', 64) == 1);
        CHECK (count ('n', 68) == 0);
        CHECK (levelOff < 1e-6f && levelOn > 0.05f);
        CHECK (! p.synthIsOn (0) && p.synthOnOverride (0) == 0);
        p.stop();
        CHECK (p.synthIsOn (0) && p.synthOnOverride (0) == -1);   // stop: back to its ON button
        // its ON button off
        song.instruments[0].pluginOff = true;
        log.clear();
        p.jamNoteOn (37, 0, 100);
        p.render (L.data(), R.data(), 512);
        float pk = 0; for (float x : L) pk = std::max (pk, std::abs (x));
        CHECK (! p.synthIsOn (0) && pk < 1e-6f);
        p.jamAllOff();
    }
}

// ---------------------------------------------------------------- v0.9: AKAIZER
// the same integer-only signal as Tests/tools/akaizer_ref.js makes
static std::vector<int16_t> akaizerTestInput (int words, uint32_t seed, bool loud)
{
    std::vector<int16_t> v ((size_t) words);
    uint32_t s = seed;
    for (int i = 0; i < words; ++i)
    {
        s = s * 1664525u + 1013904223u;
        int x = ((i * 37) % 2000 - 1000) * 20 + (int) ((s >> 16) & 0x3fff) - 8192;
        if (loud && (i % 3000) < 1500) x = x * 3 / 2;
        v[(size_t) i] = (int16_t) std::clamp (x, -32768, 32767);
    }
    return v;
}

static uint32_t fnv16 (const std::vector<int16_t>& a)
{
    uint32_t h = 0x811c9dc5u;
    for (int16_t x : a)
    {
        const uint32_t u = (uint16_t) x;
        h = (h ^ (u & 0xff)) * 16777619u;
        h = (h ^ (u >> 8)) * 16777619u;
    }
    return h;
}

static void testAkaizer()
{
    // rounding: half to even, as VB6
    CHECK (akaizerRound (2.5) == 2 && akaizerRound (3.5) == 4 && akaizerRound (-2.5) == -2 && akaizerRound (2.6) == 3 && akaizerRound (-0.4) == 0);
    CHECK (akaizerLinkedFreq (800) == 51 && akaizerLinkedFreq (1000) == 41);
    // bit-exact against the reference routine (golden values from the JS original)
    int exact = 0;
    for (const auto& g : kAkaizerGolden)
    {
        const auto in = akaizerTestInput (g.frames * g.channels, g.seed, g.loud != 0);
        const int f = g.freq > 0 ? g.freq : akaizerLinkedFreq (g.cycle);
        const auto out = akaizerStretch (in, g.channels, g.rate, g.percent, g.cycle, f);
        const bool ok = (int) out.size() == g.outLength && fnv16 (out) == g.hash && out[out.size() / 2] == g.middle;
        if (! ok) std::printf ("akaizer case %d ch %d %.1f%% cycle %d: length %d (want %d) hash %08x (want %08x)\n",
                               exact, g.channels, g.percent, g.cycle, (int) out.size(), g.outLength, fnv16 (out), g.hash);
        CHECK (ok);
        ++exact;
        int peak = 0; for (auto x : out) peak = std::max (peak, std::abs ((int) x));
        CHECK (peak <= 32768);
    }
    std::printf ("akaizer: %d golden cases checked\n", exact);

    // through the editor's interface: lengths, pitch-then-stretch, stereo both ways
    const int sr = 44100;
    std::vector<float> sine ((size_t) sr);
    for (int i = 0; i < sr; ++i) sine[(size_t) i] = 0.5f * (float) std::sin (2.0 * kPi * 441.0 * i / sr);
    StretchSettings st; st.mode = StretchSettings::Mode::Akaizer; st.sampleRate = sr; st.cycleLength = 800;
    for (double t : { 0.5, 1.0, 2.13, 4.0 })
    {
        st.timeFactor = t;
        auto out = cyclicStretch ({ sine }, st)[0];
        CHECK (std::abs ((int) out.size() - (int) std::lround (sr * t)) <= 2);
        float pk = 0; for (float v : out) pk = std::max (pk, std::abs (v));
        CHECK (pk <= 1.0f && pk > 0.3f);
    }
    // the same as calling the routine directly at 100% / no pitch
    {
        st.timeFactor = 2.13;
        auto out = cyclicStretch ({ sine }, st)[0];
        std::vector<int16_t> in ((size_t) sr);
        for (int i = 0; i < sr; ++i) in[(size_t) i] = (int16_t) std::lround (sine[(size_t) i] * 32768.0f);
        auto ref = akaizerStretch (in, 1, sr, 213.0, 800, 51);
        bool same = ref.size() == out.size();
        for (size_t i = 0; same && i < ref.size(); ++i) same = std::lround (out[i] * 32768.0f) == ref[i];
        CHECK (same);
    }
    // +12 semitones: pitched up an octave, length still as asked
    {
        st.timeFactor = 1.0; st.semitones = 12;
        auto out = cyclicStretch ({ sine }, st)[0];
        const double hz = crossingsHz (out, 2000, (int) out.size() - 2000, sr);
        std::printf ("akaizer +12 st: %.1f Hz (expect 882), length %d\n", hz, (int) out.size());
        CHECK (std::abs (hz - 882.0) < 882.0 * 0.08);
        CHECK (std::abs ((int) out.size() - sr) <= 2);
        st.semitones = 0;
    }
    // stereo: the quirk (interleaved) and separate channels both give two equal-length channels
    {
        std::vector<float> other (sine.size());
        for (size_t i = 0; i < other.size(); ++i) other[i] = -sine[i];
        st.timeFactor = 1.5; st.cycleLength = 333;
        st.stereoQuirk = true;
        auto q = cyclicStretch ({ sine, other }, st);
        st.stereoQuirk = false;
        auto sep = cyclicStretch ({ sine, other }, st);
        CHECK (q.size() == 2 && q[0].size() == q[1].size() && sep[0].size() == sep[1].size());
        // separate: the right is exactly the inverted left
        bool inv = true;
        for (size_t i = 0; inv && i < sep[0].size(); ++i) inv = std::abs (sep[0][i] + sep[1][i]) < 2.0f / 32768.0f;
        CHECK (inv);
        // the quirk swaps sides on odd hops, so it isn't
        double diff = 0;
        for (size_t i = 0; i < q[0].size(); ++i) diff += std::abs (q[0][i] + q[1][i]);
        std::printf ("akaizer stereo quirk: L/R mixing %.1f\n", diff);
        CHECK (diff > 1.0);
    }
    // manual frequency changes the grain spacing
    {
        st.cycleLength = 800; st.timeFactor = 2.0; st.stereoQuirk = true;
        st.cycleFreq = 0;  auto a = cyclicStretch ({ sine }, st)[0];
        st.cycleFreq = 120; auto b = cyclicStretch ({ sine }, st)[0];
        CHECK (a.size() == b.size() && a != b);
    }
}

// ---------------------------------------------------------------- v0.6 tests
static double rmsOf (const std::vector<float>& v, int a, int b)
{
    double e = 0; for (int i = a; i < b; ++i) e += v[(size_t) i] * v[(size_t) i];
    return std::sqrt (e / std::max (1, b - a));
}

static void testDoubleHalve()
{
    Block b; b.lines = 4;
    for (int l = 0; l < 4; ++l) b.at (l, 0) = Cell { (uint8_t) (37 + l), 1, 0xC, (uint8_t) (l * 8), cmdLetter ('L'), (uint8_t) l };
    CHECK (doubleBlockLength (b));
    CHECK (b.lines == 8);
    bool same = true;
    for (int l = 0; l < 4; ++l)
    {
        const auto& a = b.at (l, 0); const auto& c = b.at (l + 4, 0);
        same = same && a.note == c.note && a.instr == c.instr && a.cmd == c.cmd && a.param == c.param && a.cmd2 == c.cmd2 && a.param2 == c.param2;
    }
    CHECK (same);
    CHECK (b.at (0, 0).note == 37 && b.at (3, 0).note == 40);   // first half untouched
    bool differed = true;
    Block h = b;
    CHECK (halveBlockLength (h, differed) && ! differed && h.lines == 4 && h.at (5, 0).note == 0);
    b.at (6, 0).note = 60;
    CHECK (halveBlockLength (b, differed) && differed && b.lines == 4);
    Block big; big.lines = 200;
    CHECK (! doubleBlockLength (big) && big.lines == 200);
    Block one; one.lines = 1;
    CHECK (! halveBlockLength (one, differed));
    Block full; full.lines = 128;
    CHECK (doubleBlockLength (full) && full.lines == 256);
}

static void testLineLoop()
{
    Song s = oneShot (square (100, 4800));
    auto& b = s.blocks[0]; b.lines = 16;
    b.at (0, 0) = { 37, 1, 0, 0 };
    b.at (6, 1) = { 0, 0, 0xD, 0x00 };    // a pattern break inside the loop is ignored
    Player p; p.setSampleRate (48000); p.setSong (&s);
    p.setLineLoop (0, 4, 7);
    p.playBlock (0, 4);
    std::vector<float> L (960), R (960);
    int minLine = 99, maxLine = -1;
    for (int k = 0; k < 6 * 40; ++k)   // ~40 lines
    {
        p.render (L.data(), R.data(), 960);
        minLine = std::min (minLine, p.uiLine.load()); maxLine = std::max (maxLine, p.uiLine.load());
    }
    std::printf ("line loop 4-7: lines seen %d..%d\n", minLine, maxLine);
    CHECK (minLine == 4 && maxLine == 7);
    // without the loop the break moves on
    p.setLineLoop (-1, 0, 0);
    p.playBlock (0, 4);
    bool left = false;
    for (int k = 0; k < 6 * 10; ++k) { p.render (L.data(), R.data(), 960); left = left || p.uiLine.load() < 4 || p.uiLine.load() > 7; }
    CHECK (left);
    // a loop on another block doesn't affect this one
    s.blocks.push_back (Block{});
    p.setLineLoop (1, 0, 3);
    p.playBlock (0, 0);
    int maxL = 0;
    for (int k = 0; k < 6 * 12; ++k) { p.render (L.data(), R.data(), 960); maxL = std::max (maxL, p.uiLine.load()); }
    CHECK (maxL > 3);
}

static void testPingPong()
{
    // a rising ramp: a forward loop jumps back down, ping-pong turns round smoothly
    std::vector<int16_t> ramp (64);
    for (int i = 0; i < 64; ++i) ramp[(size_t) i] = (int16_t) (i * 400);
    for (bool ping : { false, true })
    {
        Song s = oneShot (ramp);
        s.instruments[0].rate = 48000;   // one sample per output sample on C-3
        s.instruments[0].pingPong = ping;
        s.blocks[0].at (0, 0) = { 37, 1, 0, 0 };
        for (bool zoh : { false, true })
        {
            Player p; p.setSampleRate (48000); p.setSong (&s);
            p.options.model = PaulaOptions::Model::Clean; p.options.zeroOrderHold = zoh; p.options.eightBit = false; p.options.stereoSep = 0.0f;
            p.playBlock (0);
            std::vector<float> L (2000), R (2000);
            p.render (L.data(), R.data(), 2000);
            float maxStep = 0; int turns = 0, lastDir = 0;
            for (int i = 200; i < 1999; ++i)
            {
                const float d = L[(size_t) i + 1] - L[(size_t) i];
                maxStep = std::max (maxStep, std::abs (d));
                const int dir = d > 1e-6f ? 1 : d < -1e-6f ? -1 : 0;   // (stepped playback repeats a sample at each turn)
                if (dir != 0) { if (lastDir != 0 && dir != lastDir) ++turns; lastDir = dir; }
            }
            std::printf ("ping-pong %d (stepped %d): biggest step %.3f, turns %d\n", (int) ping, (int) zoh, maxStep, turns);
            if (ping) { CHECK (maxStep < 0.05f); CHECK (turns >= 20); }
            else CHECK (maxStep > 0.2f);
        }
    }
}

static void testSlices()
{
    // four flat slices, each louder than the one before
    std::vector<int16_t> d (4000);
    for (int i = 0; i < 4000; ++i) d[(size_t) i] = (int16_t) ((i / 1000 + 1) * 4000);
    Song s = oneShot (d, false);
    auto& in = s.instruments[0];
    in.rate = 48000;
    in.slices = { 0, 1000, 2000, 3000 };
    int a = 0, b = 0;
    CHECK (in.sliceRange (3, a, b) && a == 3000 && b == 4000);
    CHECK (! in.sliceRange (4, a, b));
    auto level = [&] (Cell c)
    {
        s.blocks[0].clear();
        s.blocks[0].at (0, 0) = c;
        auto v = renderTrack (s, 3000);
        const int on = firstSound (v);
        if (on < 0) return std::pair<double, int> { 0.0, 0 };
        int len = 0; for (int i = on; i < 3000; ++i) if (std::abs (v[(size_t) i]) > 1e-4f) ++len;
        return std::pair<double, int> { rmsOf (v, on + 10, on + 500), len };
    };
    // slice to keys: D-3 = slice 3
    in.sliceKeys = true;
    const auto s1 = level ({ 37, 1, 0, 0 }), s3 = level ({ 39, 1, 0, 0 }), s5 = level ({ 41, 1, 0, 0 });
    std::printf ("slice keys: slice 1 level %.3f len %d, slice 3 level %.3f len %d, slice 5 %.3f\n", s1.first, s1.second, s3.first, s3.second, s5.first);
    CHECK (std::abs (s3.first / s1.first - 3.0) < 0.1);
    CHECK (std::abs (s1.second - 1000) <= 20);   // stops at the end of the slice
    CHECK (s5.first == 0.0);                      // no slice 5
    // Sxx uses the slice points (S01 = slice 2)
    in.sliceKeys = false;
    const auto sx = level ({ 37, 1, cmdLetter ('S'), 0x01 });
    CHECK (std::abs (sx.first / s1.first - 2.0) < 0.1 && std::abs (sx.second - 1000) <= 20);
    // without slice points S still divides into 16
    in.slices.clear();
    const auto s16 = level ({ 37, 1, cmdLetter ('S'), 0x00 });
    CHECK (std::abs (s16.second - 250) <= 10);
    // keyboard / MIDI notes follow slice to keys too
    in.slices = { 0, 1000, 2000, 3000 }; in.sliceKeys = true;
    {
        Player p; p.setSampleRate (48000); p.setSong (&s);
        p.options.model = PaulaOptions::Model::Clean; p.options.zeroOrderHold = false; p.options.eightBit = false; p.options.stereoSep = 0.0f;
        p.jamNoteOn (40, 0);   // D#3 = slice 4
        std::vector<float> L (3000), R (3000);
        p.render (L.data(), R.data(), 3000);
        for (int i = 0; i < 3000; ++i) L[(size_t) i] += R[(size_t) i];
        int len = 0; for (float x : L) if (std::abs (x) > 1e-4f) ++len;
        std::printf ("jam slice 4: len %d\n", len);
        CHECK (std::abs (len - 1000) <= 20);
    }
    // finding hits
    {
        const int sr = 44100;
        std::vector<int16_t> hits ((size_t) sr * 2), none;
        std::mt19937 rng (3);
        const int at[] = { 0, 11025, 22050, 33075, 52000, 70000 };
        for (int h : at)
            for (int i = 0; i < 6000; ++i)
                hits[(size_t) (h + i)] = (int16_t) (((int) (rng() % 30000) - 15000) * std::exp (-i / 1200.0));
        auto found = detectSlices (hits, none, sr, 0.5f);
        std::printf ("detect: %d slices:", (int) found.size());
        for (int f : found) std::printf (" %d", f);
        std::printf ("\n");
        CHECK (found.size() == 6);
        for (size_t k = 0; k < std::min (found.size(), (size_t) 6); ++k) CHECK (std::abs (found[k] - at[k]) < 300);
        CHECK (evenSlices (1000, 4) == std::vector<int> ({ 0, 250, 500, 750 }));
        std::vector<int> t { 900, -5, 300, 300, 1200, 0 };
        tidySlices (t, 1000);
        CHECK (t == std::vector<int> ({ 0, 300, 900 }));
    }
}

static void testNoteTranspose()
{
    Song s = oneShot (square (100, 4800));
    s.instruments[0].rate = 48000;   // C-3 plays the square at 480 Hz
    s.blocks[0].at (0, 0) = { 37, 1, 0, 0 };
    const double base = crossingsHz (renderTrack (s, 9600), 1000, 9000, 48000);
    s.blocks[0].at (0, 0) = { 37, 1, cmdLetter ('I'), 0x8C };
    const double up = crossingsHz (renderTrack (s, 9600), 1000, 9000, 48000);
    s.blocks[0].at (0, 0) = { 37, 1, cmdLetter ('I'), 0x7B };
    const double down = crossingsHz (renderTrack (s, 9600), 1000, 9000, 48000);
    std::printf ("I command: %.1f Hz, I8C %.1f Hz (x2), I7B %.1f Hz (-5 st)\n", base, up, down);
    CHECK (std::abs (base - 480) < 10);
    CHECK (std::abs (up / base - 2.0) < 0.03);
    CHECK (std::abs (down / base - std::pow (2.0, -5 / 12.0)) < 0.03);
    CHECK (describeCommand (cmdLetter ('I'), 0x8C).find ("+12") != std::string::npos);
}

static void testInstrumentLevel()
{
    // the instrument's VOL turns everything down, Cxx still sets each note's volume
    std::vector<int16_t> dc (480, 16000);
    auto level = [&] (int instrVol, int cParam)
    {
        Song s = oneShot (dc);
        s.instruments[0].volume = instrVol;
        s.blocks[0].at (0, 0) = { 37, 1, (uint8_t) (cParam >= 0 ? 0xC : 0), (uint8_t) std::max (0, cParam) };
        auto v = renderTrack (s, 2000);
        return rmsOf (v, 500, 2000);
    };
    const double full = level (64, -1), half = level (32, -1), c20 = level (64, 0x20), c20half = level (32, 0x20);
    std::printf ("instrument level: full %.3f, VOL 32 %.3f, C20 %.3f, C20 at VOL 32 %.3f\n", full, half, c20, c20half);
    CHECK (std::abs (half / full - 0.5) < 0.02);
    CHECK (std::abs (c20 / full - 0.5) < 0.02);
    CHECK (std::abs (c20half / full - 0.25) < 0.02);
    // MOD export writes C commands as they sound (C20 at VOL 32 = C10 in ProTracker)
    Song s = oneShot (dc);
    s.instruments[0].volume = 32;
    s.blocks[0].at (0, 0) = { 37, 1, 0xC, 0x20 };
    std::vector<uint8_t> bytes;
    CHECK (saveMOD (s, bytes).ok);
    Song t;
    CHECK (loadModule (bytes, t).ok);
    std::printf ("exported C20 at VOL 32 -> C%02X\n", t.blocks[0].at (0, 0).param);
    CHECK (t.blocks[0].at (0, 0).cmd == 0xC && t.blocks[0].at (0, 0).param == 0x10);
}

static void testBlockSwitchAndSections()
{
    // block play: switching block keeps the line and doesn't restart
    Song s = oneShot (square (100, 4800));
    s.blocks.assign (2, Block{});
    s.blocks[1].lines = 32;
    s.sequence = { 0, 1 };
    Player p; p.setSampleRate (48000); p.setSong (&s);
    std::vector<float> L (5760), R (5760);   // one line
    p.playBlock (0);
    for (int k = 0; k < 10; ++k) p.render (L.data(), R.data(), 5760);
    const int before = p.uiLine.load();
    CHECK (p.switchBlock (1));
    CHECK (p.uiBlock.load() == 1 && p.uiLine.load() == before);
    p.render (L.data(), R.data(), 5760);
    std::printf ("switch block at line %d -> block %d line %d\n", before, p.uiBlock.load(), p.uiLine.load());
    CHECK (p.uiBlock.load() == 1 && p.uiLine.load() == before + 1);
    CHECK (! p.switchBlock (1));                  // already there
    // a line past the end of a shorter block wraps
    p.playBlock (0, 40); CHECK (p.switchBlock (1) && p.uiLine.load() == 8);
    p.playSong (0); CHECK (! p.switchBlock (0));  // only in block play

    // sections
    Song t;
    t.blocks.assign (1, Block{}); t.blocks[0].lines = 4;
    t.sequence = { 0, 0, 0, 0, 0, 0, 0, 0 };
    t.sections = { { 2, "DROP" }, { 5, "BREAK" } };
    int a = 0, b = 0;
    sectionRange (t, 3, a, b); CHECK (a == 2 && b == 4);
    sectionRange (t, 0, a, b); CHECK (a == 0 && b == 1);
    sectionRange (t, 7, a, b); CHECK (a == 5 && b == 7);
    CHECK (sectionAt (t.sections, 5) == 1 && sectionAt (t.sections, 4) == -1);
    auto x = t.sections;
    sectionsInserted (x, 3, 2, 10); CHECK (x[0].pos == 2 && x[1].pos == 7);
    x = t.sections; sectionsInserted (x, 2, 1, 9); CHECK (x[0].pos == 3);   // inserted before the divider
    x = t.sections; sectionsRemoved (x, 1, 2, 6); CHECK (x.size() == 2 && x[0].pos == 1 && x[1].pos == 3);
    x = t.sections; sectionsRemoved (x, 2, 3, 5); CHECK (x.size() == 1 && x[0].pos == 2 && x[0].name == "BREAK");
    x = { { 9, "GONE" }, { 1, "B" }, { 1, "C" } }; tidySections (x, 8); CHECK (x.size() == 1 && x[0].name == "C");

    // section loop: song play goes round positions 2..4
    {
        Player q; q.setSampleRate (48000); q.setSong (&t);
        q.setSongLoop (2, 4);
        q.playSong (0);
        std::vector<int> seen;
        for (int k = 0; k < 4 * 14; ++k) { q.render (L.data(), R.data(), 5760); if (seen.empty() || seen.back() != q.uiSeqPos.load()) seen.push_back (q.uiSeqPos.load()); }
        std::printf ("section loop 2-4 from 0:");
        for (int v : seen) std::printf (" %d", v);
        std::printf ("\n");
        CHECK ((std::vector<int> (seen.begin(), seen.begin() + 8) == std::vector<int> { 0, 2, 3, 4, 2, 3, 4, 2 }));   // outside the loop: goes there after this block
        // switching the loop to another section while inside one: goes there at the end of the block
        q.setSongLoop (5, 7);
        int at = q.uiSeqPos.load(), next = -1;
        for (int k = 0; k < 8 && next < 0; ++k) { q.render (L.data(), R.data(), 5760); if (q.uiSeqPos.load() != at) next = q.uiSeqPos.load(); }
        CHECK (next == 5);
    }
}

// ---------------------------------------------------------------- v0.8 mixer
static std::vector<float> renderMix (Song& s, int n, bool force, PaulaOptions::Model model = PaulaOptions::Model::Clean, std::vector<float>* right = nullptr)
{
    Player p; p.setSampleRate (48000); p.setSong (&s);
    p.options.model = model; p.options.stereoSep = 0.2f;
    p.forceMixPath = force;
    p.mixer().syncEffectsNow (s.mixer);
    p.playBlock (0);
    std::vector<float> L ((size_t) n), R ((size_t) n);
    p.render (L.data(), R.data(), n);
    if (right) *right = R;
    return L;
}

static void testMixer()
{
    // a flat mixer sounds exactly like no mixer (4 and 8 tracks, A500, stereo panning)
    for (int tracks : { 4, 8 })
    {
        Song s = makeSong();
        s.numTracks = tracks;
        std::vector<float> ra, rb;
        auto a = renderMix (s, 48000, false, PaulaOptions::Model::A500, &ra);
        auto b = renderMix (s, 48000, true, PaulaOptions::Model::A500, &rb);
        double maxDiff = 0;
        for (size_t i = 0; i < a.size(); ++i) maxDiff = std::max ({ maxDiff, (double) std::abs (a[i] - b[i]), (double) std::abs (ra[i] - rb[i]) });
        std::printf ("flat mixer, %d tracks: biggest difference %.2e\n", tracks, maxDiff);
        CHECK (maxDiff < (tracks == 8 ? 0.02 : 1e-5));   // (8-track CRUNCH squashes each track instead of each pair)
    }
    {
        MixerSettings m;
        CHECK (m.isFlat());
        m.ch[2].send[0] = 0.1f; CHECK (! m.isFlat());
        m = MixerSettings {}; m.ch[0].eq.band[kEqMid1].on = true; CHECK (! m.isFlat());
        m = MixerSettings {}; initFxSlot (m.ch[1].inserts[2], FxType::Delay); CHECK (! m.isFlat());
        m.ch[1].inserts[2].bypass = true; CHECK (m.isFlat());
        CHECK (m.ret[0].inserts[0].type == FxType::Reverb && m.ret[1].inserts[0].type == FxType::Delay);
    }

    // one quiet square on track 1; the fader, sends and returns act on it
    auto quiet = [] { Song s = oneShot (square (100, 4800)); s.instruments[0].volume = 16; s.blocks[0].at (0, 0) = { 37, 1, 0, 0 }; return s; };
    {
        Song s = quiet();
        const double base = rmsOf (renderMix (s, 9600, true), 2000, 9600);
        s.mixer.ch[0].volumeDb = -6.0206f;
        const double half = rmsOf (renderMix (s, 9600, true), 2000, 9600);
        s.mixer.ch[0].volumeDb = -60.0f;
        const double off = rmsOf (renderMix (s, 9600, true), 2000, 9600);
        std::printf ("fader: 0 dB %.4f, -6 dB %.4f, off %.6f\n", base, half, off);
        CHECK (std::abs (half / base - 0.5) < 0.01 && off < 1e-6);
        // post-fader send into a return with no effects adds a copy; pre-fader send works with the fader down
        s.mixer.ch[0].volumeDb = 0.0f;
        for (auto& r : s.mixer.ret) for (auto& sl : r.inserts) sl = FxSlot {};
        s.mixer.ch[0].send[0] = 0.5f;
        const double withSend = rmsOf (renderMix (s, 9600, true), 2000, 9600);
        s.mixer.ch[0].volumeDb = -60.0f; s.mixer.ch[0].sendPre[0] = true;
        const double preOnly = rmsOf (renderMix (s, 9600, true), 2000, 9600);
        s.mixer.ret[0].mute = true;
        const double muted = rmsOf (renderMix (s, 9600, true), 2000, 9600);
        std::printf ("send: post 50%% %.4f (x%.2f), pre-fader only %.4f (x%.2f), return muted %.6f\n", withSend, withSend / base, preOnly, preOnly / base, muted);
        CHECK (std::abs (withSend / base - 1.5) < 0.02 && std::abs (preOnly / base - 0.5) < 0.02 && muted < 1e-6);
    }
    // EQ curve and processing
    {
        ChannelEq eq;
        eq.band[kEqMid1] = { true, 1000.0f, 12.0f, 1.0f };
        eq.band[kEqLowShelf] = { true, 120.0f, -6.0f, 0.7f };
        eq.band[kEqHighPass] = { true, 40.0f, 0.0f, 0.7f };
        const double at1k = ChannelEqProcessor::responseDb (eq, 1000, 48000), at30 = ChannelEqProcessor::responseDb (eq, 25, 48000),
                     at10k = ChannelEqProcessor::responseDb (eq, 10000, 48000), at80 = ChannelEqProcessor::responseDb (eq, 80, 48000);
        std::printf ("EQ: 1k %.1f dB, 80 Hz %.1f dB, 25 Hz %.1f dB, 10k %.1f dB\n", at1k, at80, at30, at10k);
        CHECK (std::abs (at1k - 12.0) < 0.5 && at80 < -3.5 && at30 < -10 && std::abs (at10k) < 0.5);
        ChannelEqProcessor proc; proc.prepare (48000);
        std::vector<float> L (9600), R (9600);
        for (int i = 0; i < 9600; ++i) L[(size_t) i] = R[(size_t) i] = 0.1f * (float) std::sin (2 * 3.14159265358979 * 1000 * i / 48000.0);
        proc.process (L.data(), R.data(), 9600, eq);
        double e = 0; for (int i = 4800; i < 9600; ++i) e += L[(size_t) i] * L[(size_t) i];
        const double g = std::sqrt (e / 4800) / (0.1 / std::sqrt (2.0));
        std::printf ("EQ on a 1 kHz tone: x%.2f (expect ~3.98)\n", g);
        CHECK (std::abs (g - 3.98) < 0.2);
    }
    // each effect does something sensible
    {
        auto run = [] (FxType t, std::vector<float> in, std::function<void (FxSlot&)> set)
        {
            FxSlot sl; initFxSlot (sl, t); set (sl);
            auto fx = makeEffect (t); fx->prepare (48000);
            std::vector<float> R = in;
            for (int i = 0; i < (int) in.size(); i += 512)
                fx->process (in.data() + i, R.data() + i, std::min (512, (int) in.size() - i), sl.p, 120.0);
            return in;
        };
        std::vector<float> sine5k (24000), imp (48000, 0.0f), loud (24000);
        for (int i = 0; i < 24000; ++i) { sine5k[(size_t) i] = 0.5f * (float) std::sin (2 * 3.14159265358979 * 5000 * i / 48000.0); loud[(size_t) i] = 0.9f * (float) std::sin (2 * 3.14159265358979 * 200 * i / 48000.0); }
        imp[0] = 1.0f;
        const double r5k = rmsOf (sine5k, 4800, 24000);
        const double lp = rmsOf (run (FxType::Filter, sine5k, [] (FxSlot& s) { s.p[0] = 0; s.p[1] = 0.4f; }), 4800, 24000);   // ~316 Hz low-pass
        const double hp = rmsOf (run (FxType::Filter, sine5k, [] (FxSlot& s) { s.p[0] = 1; s.p[1] = 0.4f; }), 4800, 24000);
        std::printf ("filter: 5 kHz through LP 316 Hz x%.3f, HP x%.3f\n", lp / r5k, hp / r5k);
        CHECK (lp / r5k < 0.05 && std::abs (hp / r5k - 1.0) < 0.1);
        auto crushed = run (FxType::Drive, sine5k, [] (FxSlot& s) { s.p[0] = 0; s.p[1] = 0; });   // 2 bits
        std::set<float> levels (crushed.begin() + 100, crushed.end());
        std::printf ("drive at 2 bits: %d distinct levels\n", (int) levels.size());
        CHECK (levels.size() <= 5);
        const double comp = rmsOf (run (FxType::Compressor, loud, [] (FxSlot& s) { s.p[0] = 0.5f; s.p[1] = 1.0f; s.p[4] = 0; }), 4800, 24000);
        std::printf ("compressor -24 dB 20:1: loud tone x%.3f\n", comp / rmsOf (loud, 4800, 24000));
        CHECK (comp / rmsOf (loud, 4800, 24000) < 0.25);
        auto echo = run (FxType::Delay, imp, [] (FxSlot& s) { s.p[0] = (float) (std::log (100.0) / std::log (2000.0)); s.p[1] = 0; s.p[2] = 0; s.p[3] = 1; s.p[5] = 1; });
        int at = (int) (std::max_element (echo.begin() + 10, echo.end()) - echo.begin());
        std::printf ("delay 100 ms: echo at sample %d (expect 4800)\n", at);
        CHECK (std::abs (at - 4800) <= 3);
        auto synced = run (FxType::Delay, imp, [] (FxSlot& s) { s.p[0] = 0.5f; s.p[1] = 1; s.p[2] = 0; s.p[3] = 1; s.p[5] = 1; });   // 1/8 at 120 = 250 ms
        at = (int) (std::max_element (synced.begin() + 10, synced.end()) - synced.begin());
        std::printf ("delay 1/8 at 120 BPM: echo at sample %d (expect 12000)\n", at);
        CHECK (std::abs (at - 12000) <= 3);
        auto verb = run (FxType::Reverb, imp, [] (FxSlot& s) { s.p[4] = 1; });
        const double tail = rmsOf (verb, 9600, 24000);
        std::printf ("reverb tail rms %.5f\n", tail);
        CHECK (tail > 1e-4);
        auto ch = run (FxType::Chorus, sine5k, [] (FxSlot&) {});
        bool finite = true; double diff = 0;
        for (size_t i = 0; i < ch.size(); ++i) { finite &= std::isfinite (ch[i]); diff += std::abs (ch[i] - sine5k[i]); }
        CHECK (finite && diff > 10.0);
        for (int t = 1; t < (int) FxType::Count; ++t)
            for (int k = 0; k < (int) fxParams ((FxType) t).size(); ++k)
            {
                FxSlot sl; initFxSlot (sl, (FxType) t);
                CHECK (! fxParamText ((FxType) t, k, sl.p).empty());
            }
    }
    // a song with a delay insert keeps sounding after the note stops; the mixer runs through the Player
    {
        Song s = quiet();
        s.instruments[0].loopLength = 0; s.instruments[0].data.resize (480);
        initFxSlot (s.mixer.ch[0].inserts[0], FxType::Delay);
        s.mixer.ch[0].inserts[0].p[1] = 0; s.mixer.ch[0].inserts[0].p[0] = (float) (std::log (100.0) / std::log (2000.0));
        s.mixer.ch[0].inserts[0].p[2] = 0.5f;
        auto v = renderMix (s, 24000, false);
        const double after = rmsOf (v, 4700, 5200);   // the first echo, well after the 10 ms note
        std::printf ("song with a delay insert: echo rms %.5f\n", after);
        CHECK (after > 1e-3);
    }
}

static void testAkaiModels()
{
    PaulaOptions o;
    o.model = PaulaOptions::Model::S950;
    CHECK (o.stepped() && o.dropBits() == 4 && ! o.crunches());
    o.model = PaulaOptions::Model::S1000;
    CHECK (! o.stepped() && o.dropBits() == 0);
    o.model = PaulaOptions::Model::A500; o.eightBit = true;
    CHECK (o.dropBits() == 8);

    auto play = [] (PaulaOptions::Model m, std::vector<int16_t> data, double rate)
    {
        Song s = oneShot (std::move (data));
        s.instruments[0].rate = rate;
        s.blocks[0].at (0, 0) = { 37, 1, 0, 0 };
        Player p; p.setSampleRate (48000); p.setSong (&s);
        p.options.model = m; p.options.stereoSep = 0.0f;
        p.playBlock (0);
        std::vector<float> L (24000), R (24000);
        p.render (L.data(), R.data(), 24000);
        for (int i = 0; i < 24000; ++i) L[(size_t) i] += R[(size_t) i];
        return L;
    };
    // 12-bit: a signal below the S950's smallest step vanishes, the S1000 keeps it
    {
        std::vector<int16_t> tiny (480);
        for (int i = 0; i < 480; ++i) tiny[(size_t) i] = (int16_t) ((i % 48) < 24 ? 12 : 0);
        const double r950 = rmsOf (play (PaulaOptions::Model::S950, tiny, 48000), 4000, 24000);
        const double r1000 = rmsOf (play (PaulaOptions::Model::S1000, tiny, 48000), 4000, 24000);
        std::printf ("tiny signal: S950 rms %.2e, S1000 rms %.2e\n", r950, r1000);
        CHECK (r950 < 1e-6 && r1000 > 1e-5);
    }
    // output filters: 6-pole at 16 kHz (S950), 8-pole at 19.5 kHz (S1000)
    auto tone = [] (double hz)
    {
        std::vector<int16_t> d (4800);   // 0.1 s loop at 48 kHz, whole cycles
        for (int i = 0; i < 4800; ++i) d[(size_t) i] = (int16_t) (12000 * std::sin (2 * 3.14159265358979 * hz * i / 48000.0));
        return d;
    };
    auto gainDb = [&] (PaulaOptions::Model m, double hz)
    {
        const double ref = rmsOf (play (PaulaOptions::Model::Clean, tone (hz), 48000), 4000, 24000);
        return 20 * std::log10 (rmsOf (play (m, tone (hz), 48000), 4000, 24000) / ref);
    };
    const double a1k = gainDb (PaulaOptions::Model::S950, 1000), a10k = gainDb (PaulaOptions::Model::S950, 10000), a20k = gainDb (PaulaOptions::Model::S950, 20000);
    const double b10k = gainDb (PaulaOptions::Model::S1000, 10000), b20k = gainDb (PaulaOptions::Model::S1000, 20000);
    std::printf ("S950: 1k %.1f dB, 10k %.1f dB, 20k %.1f dB   S1000: 10k %.1f dB, 20k %.1f dB\n", a1k, a10k, a20k, b10k, b20k);
    CHECK (std::abs (a1k) < 0.5 && std::abs (a10k) < 1.0 && a20k < -9.0);
    CHECK (std::abs (b10k) < 0.5 && b20k < -3.0 && b20k > a20k);
}

int main()
{
    testDoubleHalve();
    testLineLoop();
    testPingPong();
    testSlices();
    testNoteTranspose();
    testAkaiModels();
    testInstrumentLevel();
    testBlockSwitchAndSections();
    testMixer();
    testExtendedCommands();
    testTimeStretch();
    testAkaizer();
    testPluginSlots();
    testAutomation();
    testPluginInstruments();
    testOnOff();
    testBalance();
    testEnvelopeAndFilter();
    testSongOps();
    testStereo();
    testMED();
    testRender();
    testPitch();
    testModRoundTrip();
    test8SVX();
    testFuzz();
    std::printf (failures ? "%d FAILURES\n" : "ALL PASSED\n", failures);
    return failures ? 1 : 0;
}
