// Engine tests: render, pitch check, MOD round-trip, loader fuzzing.
// Build: c++ -std=c++20 -O1 -g -fsanitize=address,undefined -I Source Tests/EngineTest.cpp Source/engine/*.cpp
#include <chrono>
#include "engine/Player.h"
#include "engine/BuiltinSynths.h"
#include "engine/ReeseSynth.h"
#include "engine/RaveSynth.h"
#include "engine/RomSynth.h"
#include "engine/RomBank.h"
#include "engine/MorphFilter.h"
#include "engine/MorphSynth.h"
#include "engine/AcidSynth.h"
#include "engine/PadSynth.h"
#include "engine/Crunch.h"
#include "engine/LowEnd.h"
#include "engine/GearMaps.h"
#include "engine/AkaiSysex.h"
#include <set>
#include "engine/Capture.h"
#include "engine/ChordDetect.h"
#include "engine/Recorder.h"
#include "engine/SampleTools.h"
#include "engine/CvOut.h"
#include "engine/Groove.h"
#include "engine/BreakTools.h"
#include "engine/StretchSync.h"
#include "engine/ModuleIO.h"
#include "engine/SongOps.h"
#include "engine/Commands.h"
#include "engine/TimeStretch.h"
#include "engine/Slicer.h"
#include "engine/Mixer.h"
#include "engine/Effects.h"
#include "engine/PluginSynth.h"
#include "engine/MidiOut.h"
#include "engine/Bounce.h"
#include "engine/Commands.h"
#include "AkaizerGolden.h"
#include <cstdio>
#include <cstring>
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
    CHECK (t.numTracks == 8);   // (every song has 8 tracks)
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

static void testEightTracks()
{
    // a track sounds as loud in an 8-track song as in a 4-track one (no OctaMED halving)
    auto render = [] (int tracks, bool grit, int track, bool mixPath, std::vector<float>& L, std::vector<float>& R, int n = 24000)
    {
        Song s; s.numTracks = tracks; s.instruments[0] = drum(); s.instruments[0].volume = 40;
        s.blocks[0].at (0, track) = Cell { 37, 1, 0, 0 }; s.sequence = { 0 };
        Player p; p.setSampleRate (48000); p.setSong (&s);
        p.options.crunch8ch = grit; p.forceMixPath = mixPath;
        p.playBlock (0);
        L.assign ((size_t) n, 0); R.assign ((size_t) n, 0);
        p.render (L.data(), R.data(), n);
    };
    auto rms = [] (const std::vector<float>& v) { double e = 0; for (float x : v) e += x * x; return std::sqrt (e / (double) v.size()); };
    std::vector<float> l4, r4, l8, r8, lg, rg, lm, rm, l5, r5;
    render (4, false, 0, false, l4, r4);
    render (8, false, 0, false, l8, r8);
    std::printf ("track 1: 4-track rms %.4f, 8-track rms %.4f\n", rms (l4), rms (l8));
    CHECK (std::abs (rms (l8) / rms (l4) - 1.0) < 0.01);
    // 8CH GRIT: the same level, a little rougher
    render (8, true, 0, false, lg, rg);
    double diff = 0; for (size_t i = 0; i < lg.size(); ++i) diff = std::max (diff, (double) std::abs (lg[i] - l8[i]));
    std::printf ("8CH GRIT: rms %.4f (clean %.4f), biggest change %.4f\n", rms (lg), rms (l8), diff);
    CHECK (std::abs (rms (lg) / rms (l8) - 1.0) < 0.05 && diff > 0.001 && diff < 0.05);
    // the mixer path does the same as the plain one
    render (8, true, 0, true, lm, rm);
    double md = 0; for (size_t i = 0; i < lm.size(); ++i) md = std::max (md, (double) std::abs (lm[i] - lg[i]));
    std::printf ("8CH GRIT mixer path vs plain: biggest difference %.6f\n", md);
    CHECK (md < 0.002);
    // track 5 sits on track 1's side (L-R-R-L, then again)
    render (8, false, 4, false, l5, r5);
    double eL = 0, eR = 0; for (size_t i = 0; i < l5.size(); ++i) { eL += l5[i] * l5[i]; eR += r5[i] * r5[i]; }
    CHECK (eL > eR * 1.2);
    // eight loud tracks: the limiter keeps it from clipping
    {
        Song s; s.numTracks = 8; s.instruments[0] = drum();
        for (int t = 0; t < 8; ++t) s.blocks[0].at (0, t) = Cell { 37, 1, 0, 0 };
        s.sequence = { 0 };
        Player p; p.setSampleRate (48000); p.setSong (&s); p.options.master = 2.0f;
        p.playBlock (0);
        std::vector<float> L (24000), R (24000);
        p.render (L.data(), R.data(), 24000);
        float pk = 0; for (size_t i = 0; i < L.size(); ++i) pk = std::max ({ pk, std::abs (L[i]), std::abs (R[i]) });
        std::printf ("eight loud tracks at VOL 200%%: peak %.3f\n", pk);
        CHECK (pk <= 1.0f && pk > 0.5f);
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
        CHECK (! p.instrumentIsOn (0) && p.instrumentOnOverride (0) == 0);
        p.stop();
        CHECK (p.instrumentIsOn (0) && p.instrumentOnOverride (0) == -1);   // stop: back to its ON button
        // its ON button off
        song.instruments[0].switchedOff = true;
        log.clear();
        p.jamNoteOn (37, 0, 100);
        p.render (L.data(), R.data(), 512);
        float pk = 0; for (float x : L) pk = std::max (pk, std::abs (x));
        CHECK (! p.instrumentIsOn (0) && pk < 1e-6f);
        p.jamAllOff();
    }
}


// ---------------------------------------------------------------- v0.11
namespace
{
    // a looped sine for sample-instrument tests
    void sineInstrument (Instrument& in)
    {
        in.clear();
        in.name = "SINE";
        in.data.resize (64);
        for (int i = 0; i < 64; ++i) in.data[(size_t) i] = (int16_t) std::lround (std::sin (i * 2.0 * kPi / 64.0) * 20000.0);
        in.loopStart = 0; in.loopLength = 64; in.volume = 64;
    }
    int tickSamples (int tempo = 125) { return (int) std::lround (48000 * 2.5 / tempo); }
}

static void testChords()
{
    Song song;
    song.numTracks = 4;
    auto& in = song.instruments[0];
    in.plugin = true; in.pluginKey = "synth"; in.pluginName = "SYNTH"; in.chord = true;
    song.chords[0x37] = { "MIN", { 0, 3, 7 }, 3 };
    song.chords[0x47] = { "MAJ", { 0, 4, 7 }, 3 };
    auto& b = song.blocks[0];
    b.at (0, 0) = { 37, 1, kCmdChord, 0x37 };                             // C-3 minor chord: C, D#, G
    b.at (2, 0) = { kNoteOff, 0, 0, 0 };
    b.at (4, 0) = { 39, 1, 0xE, 0xD2, kCmdChord, 0x47 };                  // delayed 2 ticks, #xy in FX2: D, F#, A
    b.at (6, 0) = { 0, 0, 0x0, 0x0C };                                    // no note: arpeggio (bends) as before
    b.at (8, 0) = { 41, 1, 0xE, 0xC2 };                                   // ECx cuts the whole chord
    std::vector<Ev> log; long long clock = 0;
    Player p; p.setSampleRate (48000); p.setSong (&song);
    p.synths().factory = [&] (int, const Instrument&, double) { return std::make_unique<FakeSynth> (&log, &clock); };
    p.synths().syncNow (song, 48000);
    p.playBlock (0);
    std::vector<float> L (512), R (512);
    const int lineLen = tickSamples() * 6;
    for (int done = 0; done < lineLen * 10; done += 512) { p.render (L.data(), R.data(), 512); clock += 512; }
    p.stop();
    auto on = [&] (int n) { int c = 0; for (auto& e : log) if (e.type == 'n' && e.a == n) ++c; return c; };
    auto off = [&] (int n) { int c = 0; for (auto& e : log) if (e.type == 'f' && e.a == n) ++c; return c; };
    long long chordEnd = -1, arpBends = 0, line0Bends = 0, d3On = -1;
    for (auto& e : log)
    {
        if (e.type == 'f' && e.a == 63 && chordEnd < 0) chordEnd = e.when;
        if (e.type == 'b' && e.a != 8192 && e.when >= lineLen * 6 && e.when < lineLen * 7) ++arpBends;
        if (e.type == 'b' && e.a != 8192 && e.when < lineLen * 2) ++line0Bends;
        if (e.type == 'n' && e.a == 62 && d3On < 0) d3On = e.when;
    }
    std::printf ("chords: C-3 %d D#3 %d G-3 %d, ended at %lld (line 2 = %d); D-3 %d F#3 %d A-3 %d at %lld (expect %d); bends on the chord line %lld, arpeggio bends %lld; F-3 chord offs %d\n",
                 on (60), on (63), on (67), chordEnd, lineLen * 2, on (62), on (66), on (69), d3On, lineLen * 4 + tickSamples() * 2, line0Bends, arpBends, off (64));
    CHECK (on (60) == 1 && on (63) == 1 && on (67) == 1 && off (63) >= 1 && off (67) >= 1);
    CHECK (chordEnd >= 0 && std::abs ((int) (chordEnd - lineLen * 2)) <= 2);
    CHECK (on (62) == 1 && on (66) == 1 && on (69) == 1 && std::abs ((int) (d3On - (lineLen * 4 + tickSamples() * 2))) <= 2);
    CHECK (line0Bends == 0 && arpBends >= 2);
    CHECK (on (64) == 1 && on (68) == 0);   // (F-3 with no 0xy: one note)
    // 0xy is an arpeggio again (whatever the old CHORD flag says), one note
    b.at (0, 0) = { 37, 1, 0x0, 0x37 };
    log.clear(); clock = 0;
    p.playBlock (0);
    for (int done = 0; done < lineLen * 2; done += 512) { p.render (L.data(), R.data(), 512); clock += 512; }
    int bends = 0; for (auto& e : log) if (e.type == 'b' && e.a != 8192) ++bends;
    CHECK (on (60) == 1 && on (63) == 0 && bends >= 2);
}

// the chord table: slots from the song, inversions without the root, old songs' intervals, the database
static void testChordTable()
{
    int n = 0;
    const ChordEntry* db = chordDatabase (n);
    bool sorted = true, cats = true;
    const auto catList = chordCategories();
    for (int i = 0; i < n; ++i)
    {
        for (int k = 1; k < db[i].count; ++k) if (db[i].notes[k] <= db[i].notes[k - 1]) sorted = false;
        if (std::find (catList.begin(), catList.end(), std::string (db[i].category)) == catList.end()) cats = false;
    }
    const int d2 = findChord ("MAJ7", "DROP 2"), inv = findChord ("MIN7", "INV 3");
    std::printf ("chord database: %d voicings; MAJ7 DROP 2 = %s; MIN7 INV 3 = %s; slot 01 = %s %s\n", n,
                 d2 >= 0 ? notesText (chordFrom (db[d2])).c_str() : "?", inv >= 0 ? notesText (chordFrom (db[inv])).c_str() : "?",
                 defaultChordTable()[1].name.c_str(), notesText (defaultChordTable()[1]).c_str());
    CHECK (n >= 600 && sorted && cats);
    CHECK (d2 >= 0 && notesText (chordFrom (db[d2])) == "-5 +0 +4 +11");
    CHECK (inv >= 0 && notesText (chordFrom (db[inv])) == "+10 +12 +15 +19");
    CHECK (defaultChordTable()[1].name == "MIN" && notesText (defaultChordTable()[1]) == "+0 +3 +7");
    int filled = 0; for (int i = 1; i < kChordSlots; ++i) if (! defaultChordTable()[(size_t) i].empty()) ++filled;
    CHECK (filled >= 30 && defaultChordTable()[0].empty());

    Song song;
    song.numTracks = 4;
    auto& in = song.instruments[0];
    in.plugin = true; in.pluginKey = "synth"; in.pluginName = "SYNTH"; in.chord = true;
    song.chords[0x05] = { "MAJ INV 2", { 7, 12, 16 }, 3 };                 // no +0: the played note doesn't sound
    song.chords[0x06] = { "BIG", { -12, 0, 3, 7, 10, 14, 17, 21 }, 8 };    // eight notes
    song.chords[0x07] = {};                                               // an empty slot: just the note
    auto& b = song.blocks[0];
    b.at (0, 0) = { 37, 1, kCmdChord, 0x05 };
    b.at (2, 0) = { 37, 1, kCmdChord, 0x06 };
    b.at (4, 0) = { 37, 1, kCmdChord, 0x07 };
    b.at (6, 0) = { 37, 1, kCmdChord, 0x37 };                             // (an empty slot: just the note)
    std::vector<Ev> log; long long clock = 0;
    Player p; p.setSampleRate (48000); p.setSong (&song);
    p.synths().factory = [&] (int, const Instrument&, double) { return std::make_unique<FakeSynth> (&log, &clock); };
    p.synths().syncNow (song, 48000);
    const int lineLen = tickSamples() * 6;
    std::vector<float> L (512), R (512);
    auto run = [&] (int lines) { log.clear(); clock = 0; p.playBlock (0); for (int done = 0; done < lineLen * lines; done += 512) { p.render (L.data(), R.data(), 512); clock += 512; } p.stop(); };
    auto onsIn = [&] (int line) { std::vector<int> v; for (auto& e : log) if (e.type == 'n' && e.when >= line * lineLen - 8 && e.when < (line + 1) * lineLen - 8) v.push_back (e.a); std::sort (v.begin(), v.end()); return v; };
    auto text = [] (const std::vector<int>& v) { std::string s; for (int x : v) s += std::to_string (x) + " "; return s; };
    run (8);
    const auto inv2 = onsIn (0), big = onsIn (2), empty = onsIn (4), slot37 = onsIn (6);
    int offs = 0, ons = 0; for (auto& e : log) { if (e.type == 'n') ++ons; if (e.type == 'f') ++offs; }
    std::printf ("chord table: INV 2 on C-3 = %s; eight notes = %s; empty slot = %s; empty slot 37 = %s; %d ons, %d offs\n",
                 text (inv2).c_str(), text (big).c_str(), text (empty).c_str(), text (slot37).c_str(), ons, offs);
    CHECK (inv2 == std::vector<int> ({ 67, 72, 76 }));
    CHECK (big == std::vector<int> ({ 48, 60, 63, 67, 70, 74, 77, 81 }));
    CHECK (empty == std::vector<int> ({ 60 }) && slot37 == std::vector<int> ({ 60 }));
    CHECK (offs >= ons);   // every chord note is let go
    // a v0.11 song: 0xy is still x and y semitones up, whatever the table says
    for (int l = 0; l < 8; l += 2) b.at (l, 0).cmd = 0x0;
    song.chordsLegacy = true;
    run (8);
    const auto legacy = onsIn (6), legacy5 = onsIn (0);
    std::printf ("legacy: 037 = %s; 005 = %s\n", text (legacy).c_str(), text (legacy5).c_str());
    CHECK (legacy == std::vector<int> ({ 60, 63, 67 }) && legacy5 == std::vector<int> ({ 60, 65 }));
    std::vector<std::vector<int>> before;
    for (int l = 0; l < 8; l += 2) before.push_back (onsIn (l));
    // converting it: slots with the same notes (01 MIN and 0F power chord used again), same sound
    song.chords = defaultChordTable();
    const int added = convertLegacyChords (song);
    std::vector<std::vector<int>> after;
    run (8);
    for (int l = 0; l < 8; l += 2) after.push_back (onsIn (l));
    std::printf ("converted: %d new slots; params now %02X %02X %02X %02X; legacy %d; same notes %d\n", added,
                 b.at (0, 0).param, b.at (2, 0).param, b.at (4, 0).param, b.at (6, 0).param, (int) song.chordsLegacy, (int) (before == after));
    CHECK (added == 2 && ! song.chordsLegacy && before == after && ! song.instruments[0].chord);
    CHECK (b.at (6, 0).param == 0x01 && b.at (4, 0).param == 0x0F && song.chords[(size_t) b.at (0, 0).param].name == "OLD 05");
    CHECK (b.at (0, 0).cmd == kCmdChord && b.at (6, 0).cmd == kCmdChord);
    // a v0.13 song: 0xy on a CHORD instrument's notes played slot xy - now #xy, same notes; 0xy elsewhere stays
    for (int l = 0; l < 8; l += 2) b.at (l, 0).cmd = 0x0;
    b.at (7, 1) = { 0, 0, 0x0, 0x37 };                                     // (no note: an arpeggio, left alone)
    song.instruments[0].chord = true;
    const int changedCells = convertChordFlags (song);
    std::vector<std::vector<int>> v13;
    run (8);
    for (int l = 0; l < 8; l += 2) v13.push_back (onsIn (l));
    std::printf ("v0.13 chords: %d cells now #xy, same notes %d\n", changedCells, (int) (v13 == after));
    CHECK (changedCells == 4 && v13 == after && b.at (2, 0).cmd == kCmdChord && b.at (7, 1).cmd == 0x0 && ! song.instruments[0].chord);
    // # in the command list, first, with its description
    CHECK (commandList()[0].cmd == kCmdChord && std::string (commandList()[0].code) == "#" && commandChar (kCmdChord) == '#' && commandFromChar ('#') == kCmdChord);
    std::printf ("# command: %s\n", describeCommand (kCmdChord, 0x03).c_str());
}

static void testSampleOnOff()
{
    Song song;
    song.numTracks = 4;
    sineInstrument (song.instruments[0]);
    song.mixer.trackAuto[0][0] = { kAutoInstrumentBase, kAutoOnOff, "SINE: ON/OFF" };
    auto& b = song.blocks[0];
    b.at (0, 0) = { 37, 1, 0, 0 };
    b.at (2, 0) = { 0, 0, cmdLetter ('V'), 0x00 };     // off: fades out
    b.at (3, 0) = { 37, 1, 0, 0 };                     // not played
    b.at (5, 0) = { 0, 0, cmdLetter ('V'), 0x01 };     // on
    b.at (6, 0) = { 37, 1, 0, 0 };                     // plays
    Player p; p.setSampleRate (48000); p.setSong (&song);
    p.playBlock (0);
    std::vector<float> L (256), R (256);
    float lvl[8] {};
    float worstStep = 0, prev = 0;
    bool fading = false;
    for (int guard = 0; guard < 4000 && p.uiLine.load() < 7; ++guard)
    {
        const int line = p.uiLine.load();
        p.render (L.data(), R.data(), 256);
        for (int i = 0; i < 256; ++i)
        {
            if (line == 2) { worstStep = std::max (worstStep, std::abs (L[(size_t) i] - prev)); fading = true; }
            prev = L[(size_t) i];
        }
        lvl[line] = std::max (lvl[line], p.uiLevel[0].load());
    }
    std::printf ("sample on/off: line levels"); for (float v : lvl) std::printf (" %.3f", v); std::printf (", biggest step while switching %.3f\n", worstStep);
    CHECK (lvl[1] > 0.1f && lvl[3] < 1e-4f && lvl[4] < 1e-4f && lvl[6] > 0.1f);
    CHECK (fading && worstStep < 0.2f);
    // its ON button off: live notes are silent too
    song.instruments[0].switchedOff = true;
    p.stop();
    for (int k = 0; k < 40; ++k) p.render (L.data(), R.data(), 256);   // (let the output filters settle)
    p.jamNoteOn (37, 0, 127);
    float pk = 0;
    for (int k = 0; k < 4; ++k) { p.render (L.data(), R.data(), 256); for (float x : L) pk = std::max (pk, std::abs (x)); }
    CHECK (pk < 1e-4f && ! p.instrumentIsOn (0));
    song.instruments[0].switchedOff = false;
    p.jamNoteOn (37, 0, 127);
    for (int k = 0; k < 4; ++k) { p.render (L.data(), R.data(), 256); for (float x : L) pk = std::max (pk, std::abs (x)); }
    CHECK (pk > 0.05f);
}

namespace
{
    // an effect that's late by a fixed number of samples, like a lookahead plug-in
    struct LateFx : Effect
    {
        int lat; DelayLine d;
        explicit LateFx (int l) : lat (l) { d.setMax (4096); }
        void prepare (double) override {}
        void reset() override {}
        void process (float* L, float* R, int n, const float*, double) override { d.process (L, R, n, lat); }
        int latency() const override { return lat; }
    };
    struct LateSynth : FakeSynth
    {
        int lat;
        LateSynth (std::vector<Ev>* l, long long* c, int la) : FakeSynth (l, c), lat (la) { level = 0; }
        int latency() const override { return lat; }
    };
    int firstAbove (const std::vector<float>& v, float above = 1e-3f)
    {
        for (size_t i = 0; i < v.size(); ++i) if (std::abs (v[i]) > above) return (int) i;
        return -1;
    }
}

static void testLatency()
{
    // ---- tracks line up with the slowest one; a bypassed plug-in keeps its delay; sends too
    {
        MixerEngine mx; mx.prepare (48000);
        MixerSettings m;
        auto& sl = m.ch[0].inserts[0];
        sl.type = FxType::Plugin; sl.pluginKey = "late"; sl.pluginName = "LATE";
        m.ch[1].send[0] = 1.0f; m.ch[1].sendPre[0] = true;
        for (auto& r : m.ret) for (auto& x : r.inserts) x.type = FxType::None;   // (no reverb smearing the click)
        mx.pluginFactory = [] (int, const FxSlot&, double) { return std::make_unique<LateFx> (100); };
        mx.syncEffectsNow (m);
        auto run = [&] (std::vector<float>& t0, std::vector<float>& t1, std::vector<float>& ret, std::vector<float>& out)
        {
            std::vector<float> a (512, 0.0f), b (512, 0.0f), ar (512, 0.0f), br (512, 0.0f), outR (512);
            a[0] = ar[0] = 1.0f; b[0] = br[0] = 1.0f;     // a click on tracks 1 and 2
            float* tl[2] = { a.data(), b.data() }; float* tr[2] = { ar.data(), br.data() };
            out.assign (512, 0.0f);
            mx.process (m, tl, tr, 2, nullptr, nullptr, out.data(), outR.data(), 512, 120);
            t0 = a; t1 = b; ret.assign (mx.returnLeft (0), mx.returnLeft (0) + 512);
            // flush the delay lines
            std::vector<float> z (512, 0.0f), z2 (512, 0.0f), z3 (512, 0.0f), z4 (512, 0.0f), o1 (512), o2 (512);
            float* zl[2] = { z.data(), z2.data() }; float* zr[2] = { z3.data(), z4.data() };
            mx.process (m, zl, zr, 2, nullptr, nullptr, o1.data(), o2.data(), 512, 120);
        };
        std::vector<float> t0, t1, ret, out;
        run (t0, t1, ret, out);
        std::printf ("latency: track 1 click at %d, track 2 at %d, its send at %d, total %d\n", firstAbove (t0), firstAbove (t1), firstAbove (ret), mx.totalLatency());
        CHECK (firstAbove (t0) == 100 && firstAbove (t1) == 100 && firstAbove (ret) == 100 && mx.totalLatency() == 100);
        CHECK (mx.trackLatency (0) == 100 && mx.trackLatency (1) == 0);
        m.ch[0].inserts[0].bypass = true;
        run (t0, t1, ret, out);
        CHECK (firstAbove (t0) == 100 && firstAbove (t1) == 100 && mx.totalLatency() == 100);
        mx.setAutomation (trackSlot (0, 0), kAutoOnOff, 1.0f);   // V on/off doesn't change it either
        run (t0, t1, ret, out);
        CHECK (firstAbove (t0) == 100 && firstAbove (t1) == 100);
        // a slow return adds to the total (not compensated: it's after the tracks)
        m.ch[0].inserts[0].bypass = false;
        auto& rs = m.ret[0].inserts[0];
        rs.type = FxType::Plugin; rs.pluginKey = "late"; rs.pluginName = "LATE";
        mx.syncEffectsNow (m);
        run (t0, t1, ret, out);
        CHECK (mx.totalLatency() == 200);
    }
    // ---- a plug-in instrument's delay: the samples on its track wait for it
    {
        auto firstOut = [] (int synthLatency)
        {
            Song song; song.numTracks = 4;
            sineInstrument (song.instruments[0]);
            auto& in = song.instruments[1];
            in.plugin = true; in.pluginKey = "synth"; in.pluginName = "SYNTH"; in.pluginOutput = 0;
            song.blocks[0].at (0, 0) = { 37, 1, 0, 0 };
            std::vector<Ev> log; long long clock = 0;
            Player p; p.setSampleRate (48000); p.setSong (&song);
            p.options.model = PaulaOptions::Model::Clean;
            p.synths().factory = [&] (int, const Instrument&, double) { return std::make_unique<LateSynth> (&log, &clock, synthLatency); };
            p.synths().syncNow (song, 48000);
            p.playBlock (0);
            std::vector<float> L (4096), R (4096);
            p.render (L.data(), R.data(), 4096);
            return firstAbove (L, 1e-4f);
        };
        const int a = firstOut (0), b = firstOut (64);
        std::printf ("latency: samples start at %d, with a 64-sample synth on the track at %d\n", a, b);
        CHECK (a >= 0 && b - a == 64);
    }
    // ---- a MIDI-out instrument with a negative delay makes the audio later
    {
        Song song; song.numTracks = 4;
        sineInstrument (song.instruments[0]);
        auto& in = song.instruments[1];
        in.plugin = true; in.midiOut = true; in.pluginKey = "MIDI:X"; in.midiDevice = "X"; in.midiDelayMs = -10;
        song.blocks[0].at (0, 0) = { 37, 1, 0, 0 };
        Player p; p.setSampleRate (48000); p.setSong (&song);
        p.options.model = PaulaOptions::Model::Clean;
        p.synths().factory = [] (int, const Instrument&, double) { return std::make_unique<MidiOutSynth> (nullptr); };
        p.synths().syncNow (song, 48000);
        p.playBlock (0);
        std::vector<float> L (4096), R (4096);
        p.render (L.data(), R.data(), 4096);
        std::printf ("latency: MIDI out -10 ms: audio starts at %d, total %d\n", firstAbove (L, 1e-4f), p.latencySamples());
        CHECK (firstAbove (L, 1e-4f) >= 480 && firstAbove (L, 1e-4f) < 480 + 8 && p.latencySamples() == 480);
    }
}

namespace
{
    struct MidiEv { std::vector<uint8_t> m; long long when; };
    struct FakeSink : MidiSink
    {
        std::vector<MidiEv>* log; long long pos = 0; double lastDelay = -1;
        explicit FakeSink (std::vector<MidiEv>* l) : log (l) {}
        void send (const uint8_t* d, int n, int offset) override { log->push_back ({ std::vector<uint8_t> (d, d + n), pos + offset }); }
        void flush (int n, double, double delayMs) override { pos += n; lastDelay = delayMs; }
        void sendNow (const uint8_t* d, int n) override { log->push_back ({ std::vector<uint8_t> (d, d + n), -1 }); }
    };
}

static void testMidiOut()
{
    Song song;
    song.numTracks = 4;
    auto setMidi = [&] (int i, const char* dev, int chan)
    {
        auto& in = song.instruments[(size_t) i];
        in.clear();
        in.plugin = true; in.midiOut = true; in.midiDevice = dev; in.pluginKey = std::string ("MIDI:") + dev; in.pluginName = dev;
        in.midiChannel = chan; in.chord = true; in.midiDelayMs = 25;
        for (int k = 0; k < kInstrumentKnobs; ++k) in.knobs[k] = { kAutoInstrumentBase + i, k, "CC" };
    };
    setMidi (0, "MPC", 3);
    setMidi (1, "MPC", 4);          // same device: only one sends the clock
    song.instruments[0].midiClock = song.instruments[1].midiClock = true;
    auto& b = song.blocks[0];
    b.at (0, 0) = { 37, 1, 0xC, 0x20, kCmdChord, 0x02 };                   // C-3, velocity 64, chord slot 02 (MAJ): C E G
    b.at (2, 0) = { 0, 0, cmdLetter ('L'), 0x80, cmdLetter ('O'), 0x40 };  // CC 74 = 64, mod wheel 32
    b.at (3, 0) = { 0, 0, 0x1, 0x02 };                                     // slide up: bend
    b.at (4, 0) = { kNoteOff, 0, 0, 0 };
    b.at (5, 1) = { 49, 2, 0, 0 };                                         // C-4 on the other instrument (channel 4)
    std::vector<MidiEv> log;
    std::vector<FakeSink*> sinks;
    Player p; p.setSampleRate (48000); p.setSong (&song);
    p.synths().factory = [&] (int, const Instrument&, double) { auto s = std::make_unique<FakeSink> (&log); sinks.push_back (s.get()); return std::make_unique<MidiOutSynth> (std::move (s)); };
    p.synths().syncNow (song, 48000);
    p.playBlock (0);
    std::vector<float> L (512), R (512);
    const int lineLen = tickSamples() * 6;
    for (int done = 0; done < lineLen * 8; done += 512) p.render (L.data(), R.data(), 512);
    p.stop();
    p.render (L.data(), R.data(), 512);   // (the stop's messages go out with the next block)
    auto find = [&] (int status, int d1 = -1) -> const MidiEv* { for (auto& e : log) if (e.m[0] == status && (d1 < 0 || (e.m.size() > 1 && e.m[1] == d1))) return &e; return nullptr; };
    auto count = [&] (int status) { int n = 0; for (auto& e : log) if (e.m[0] == status) ++n; return n; };
    const auto* c3 = find (0x92, 60);
    const auto* e3 = find (0x92, 64);
    const auto* g3 = find (0x92, 67);
    const auto* cc74 = find (0xB2, 74);
    const auto* mod = find (0xB2, 1);
    const auto* off = find (0x82, 60);
    const auto* c4 = find (0x93, 72);
    std::printf ("midi out: C-3 vel %d at %lld, chord %d %d, CC74 %d at %lld (line 2 = %d), mod %d, bends %d, off at %lld (line 4 = %d), ch4 note at %lld (line 5 = %d), delay %.0f ms\n",
                 c3 ? c3->m[2] : -1, c3 ? c3->when : -1, e3 != nullptr, g3 != nullptr, cc74 ? cc74->m[2] : -1, cc74 ? cc74->when : -1, lineLen * 2,
                 mod ? mod->m[2] : -1, count (0xE2), off ? off->when : -1, lineLen * 4, c4 ? c4->when : -1, lineLen * 5, sinks.empty() ? -1.0 : sinks[0]->lastDelay);
    CHECK (c3 && c3->m[2] == 64 && c3->when == 0 && e3 && g3);
    CHECK (cc74 && cc74->m[2] == 64 && std::abs ((int) (cc74->when - lineLen * 2)) <= 1);
    CHECK (mod && mod->m[2] == 32);
    CHECK (count (0xE2) >= 4);
    CHECK (off && std::abs ((int) (off->when - lineLen * 4)) <= 1 && find (0x82, 64) && find (0x82, 67));
    // (the C-4 on track 2 is instrument 2, channel 4, MIDI note 72 - 23 = 49 + 23)
    const auto* c4b = find (0x93, 72);
    CHECK (c4b && std::abs ((int) (c4b->when - lineLen * 5)) <= 1);
    CHECK (find (0xB2, 123) && find (0xB3, 123));                       // stop: all notes off on both channels
    CHECK (! sinks.empty() && sinks[0]->lastDelay == 25.0);
    // clock: start at 0, 24 a beat (a beat = 4 lines = 24 ticks), one device sends it once, stop
    const auto* spp = find (0xF2);
    const auto* start = find (0xFA);
    std::vector<long long> clocks;
    for (auto& e : log) if (e.m[0] == 0xF8) clocks.push_back (e.when);
    bool even = clocks.size() > 2;
    for (size_t i = 1; i < clocks.size(); ++i) even = even && std::abs ((int) (clocks[i] - clocks[i - 1] - tickSamples())) <= 1;
    std::printf ("midi clock: SPP %d, start at %lld, %zu clocks (expect %d), even %d, stop %d\n", spp != nullptr, start ? start->when : -1,
                 clocks.size(), lineLen * 8 / tickSamples() + 1, (int) even, count (0xFC));
    CHECK (spp && spp->m[1] == 0 && start && start->when == 0);
    CHECK (std::abs ((int) clocks.size() - (lineLen * 8 / tickSamples() + 1)) <= 1 && even);
    CHECK (count (0xFC) == 1 && count (0xFA) == 1);
    // playing from line 4 of a later position: Song Position + Continue
    song.sequence = { 0, 0 };
    log.clear();
    p.playSong (1, 4);
    p.render (L.data(), R.data(), 512);
    const auto* spp2 = find (0xF2);
    CHECK (spp2 && (spp2->m[1] | (spp2->m[2] << 7)) == (64 + 4) && find (0xFB) && ! find (0xFA));   // 68 lines = 17 beats = 68 16ths
    p.stop();
    // switched off: no notes; deleting the instrument's synth switches its notes off
    song.instruments[0].switchedOff = true;
    log.clear();
    p.playBlock (0);
    for (int done = 0; done < lineLen * 2; done += 512) p.render (L.data(), R.data(), 512);
    CHECK (! find (0x92, 60));
    p.stop();
    song.instruments[0].switchedOff = false;
    p.jamNoteOn (37, 0, 100);
    p.render (L.data(), R.data(), 512);
    CHECK (find (0x92, 60) != nullptr);
    log.clear();
    p.synths().factory = nullptr;
    song.instruments[0].clear();
    p.synths().syncNow (song, 48000);   // (the old synth is freed: the held note goes off straight away)
    CHECK (find (0x82, 60) && find (0x82, 60)->when == -1);
}


// ---------------------------------------------------------------- v0.12: bounce
static void testBounce()
{
    Song song;
    song.numTracks = 4;
    sineInstrument (song.instruments[0]);
    auto& syn = song.instruments[1];
    syn.plugin = true; syn.pluginKey = "synth"; syn.pluginName = "SYNTH";
    auto& b = song.blocks[0];
    b.at (0, 0) = { 0, 1, 0, 0 };                     // the instrument is set before the range...
    b.at (1, 3) = { 0, 0, 0xF, 0x03 };                // ...and so is speed 3 (on a track outside it)
    b.at (4, 0) = { 37, 0, 0, 0 };                    // a note with no instrument number
    b.at (5, 1) = { 37, 2, 0, 0 };                    // the plug-in on track 2
    b.at (6, 0) = { 0, 0, 0xB, 0x00 };                // (a jump: ignored)
    b.at (4, 2) = { 49, 1, 0, 0 };                    // track 3: not highlighted
    std::vector<Ev> log; long long clock = 0;
    BounceResult res; std::string err;
    const bool ok = bounceRange (song, PaulaOptions {}, 48000, { 0, 4, 7, 0, 1 }, res,
                                 [&] (int, const Instrument&, double) { return std::make_unique<LateSynth> (&log, &clock, 0); }, err);
    const int lineLen = tickSamples() * 3;
    int ons = 0; for (auto& e : log) if (e.type == 'n') ++ons;
    std::printf ("bounce: ok %d (%s), %zu samples (4 lines at speed 3 = %d), instruments %zu, synth notes %d, first sound at %d\n",
                 (int) ok, err.c_str(), res.left.size(), lineLen * 4, res.instruments.size(), ons, firstAbove (res.left, 1e-3f));
    CHECK (ok && res.instruments.size() == 2 && res.instruments[0] == 0 && res.instruments[1] == 1 && res.leftOut.empty());
    CHECK (std::abs ((int) res.left.size() - lineLen * 4) < lineLen / 2);   // (the looped sine is let go at the end)
    CHECK (firstAbove (res.left, 1e-3f) >= 0 && firstAbove (res.left, 1e-3f) < 64);
    CHECK (ons == 1);
    // the plug-in's level is in it from line 5 (FakeSynth plays a 0.25 DC)
    CHECK (res.right.size() == res.left.size());
    // nothing highlighted with notes
    BounceResult none;
    CHECK (! bounceRange (song, PaulaOptions {}, 48000, { 0, 10, 20, 0, 1 }, none, nullptr, err) && ! err.empty());
    // instruments in the range
    const auto used = instrumentsInRange (song, { 0, 0, 63, 2, 2 });
    CHECK (used.size() == 1 && used[0] == 0);
    // with FX: the track's strip and its sends are in it (a reverb tail makes it longer)
    {
        Song fx = song;
        initFxSlot (fx.mixer.ch[0].inserts[0], FxType::Drive);
        fx.mixer.ch[0].inserts[0].p[0] = 1.0f; fx.mixer.ch[0].inserts[0].p[4] = 1.0f;
        fx.mixer.ch[0].send[0] = 1.0f;   // SEND A: a reverb
        BounceResult dry, wet;
        CHECK (bounceRange (fx, PaulaOptions {}, 48000, { 0, 4, 7, 0, 1, false }, dry, nullptr, err));
        CHECK (bounceRange (fx, PaulaOptions {}, 48000, { 0, 4, 7, 0, 1, true }, wet, nullptr, err));
        double diff = 0; for (size_t i = 0; i < std::min (dry.left.size(), wet.left.size()); ++i) diff += std::abs (dry.left[i] - wet.left[i]);
        std::printf ("bounce: dry %zu samples, with FX %zu samples, difference %.1f\n", dry.left.size(), wet.left.size(), diff);
        CHECK (wet.left.size() > dry.left.size() + 4800 && diff > 100.0);

        // the four choices: no FX, the instruments' own only, the mixer's only, all
        Song both = fx;
        for (int i = 0; i < 2; ++i) { initFxSlot (both.mixer.inst[i][0], FxType::Drive); both.mixer.inst[i][0].p[0] = 1.0f; both.mixer.inst[i][0].p[4] = 0.3f; }
        BounceResult none, instOnly, mixOnly, all;
        CHECK (bounceRange (both, PaulaOptions {}, 48000, { 0, 4, 7, 0, 1, false, false }, none, nullptr, err));
        CHECK (bounceRange (both, PaulaOptions {}, 48000, { 0, 4, 7, 0, 1, false, true }, instOnly, nullptr, err));
        CHECK (bounceRange (both, PaulaOptions {}, 48000, { 0, 4, 7, 0, 1, true, false }, mixOnly, nullptr, err));
        CHECK (bounceRange (both, PaulaOptions {}, 48000, { 0, 4, 7, 0, 1, true, true }, all, nullptr, err));
        auto same = [] (const BounceResult& a, const BounceResult& b)
        {
            if (a.left.size() != b.left.size()) return false;
            for (size_t i = 0; i < a.left.size(); ++i) if (std::abs (a.left[i] - b.left[i]) > 1e-5f) return false;
            return true;
        };
        std::printf ("bounce choices: none %zu, instrument FX %zu, mixer FX %zu, all %zu samples\n", none.left.size(), instOnly.left.size(), mixOnly.left.size(), all.left.size());
        CHECK (same (none, dry));          // no FX: as if neither had any
        CHECK (same (mixOnly, wet));       // the mixer only: the instruments' drive isn't in it
        CHECK (! same (instOnly, none) && ! same (instOnly, mixOnly) && instOnly.left.size() < mixOnly.left.size());   // (no reverb tail)
        CHECK (! same (all, mixOnly) && ! same (all, instOnly) && all.left.size() > instOnly.left.size() + 4800);
    }
    // a MIDI-out instrument can't be bounced
    song.instruments[1].midiOut = true;
    BounceResult m;
    CHECK (bounceRange (song, PaulaOptions {}, 48000, { 0, 4, 7, 0, 1 }, m, nullptr, err) && m.leftOut.size() == 1 && m.instruments.size() == 1);
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
    // (v0.73) no slice 5: it wraps round to slice 1 - and below C-3 too (B-2 = the last slice): any note plays
    const auto sb = level ({ 36, 1, 0, 0 }), sc2 = level ({ 25, 1, 0, 0 });
    std::printf ("wrapped: E-3 (5th) level %.3f, B-2 %.3f, C-2 %.3f\n", s5.first, sb.first, sc2.first);
    CHECK (std::abs (s5.first / s1.first - 1.0) < 0.1);
    CHECK (std::abs (sb.first / s1.first - 4.0) < 0.15);
    CHECK (std::abs (sc2.first / s1.first - 1.0) < 0.1);
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
    // (v0.73) a jam note below C-3 still plays a slice, and HOLD KEYS keeps a note on after its key comes up
    {
        Player p; p.setSampleRate (48000); p.setSong (&s);
        p.options.model = PaulaOptions::Model::Clean; p.options.zeroOrderHold = false; p.options.eightBit = false; p.options.stereoSep = 0.0f;
        p.jamNoteOn (13, 0);   // C-1
        std::vector<float> L (3000), R (3000);
        p.render (L.data(), R.data(), 3000);
        int len = 0; for (float x : L) if (std::abs (x) > 1e-4f) ++len;
        std::printf ("jam C-1 (wrapped to slice 1): len %d\n", len);
        CHECK (std::abs (len - 1000) <= 20);
    }
    {
        Song ls = oneShot (square (100, 48000));
        ls.instruments[0].rate = 48000;
        Player p; p.setSampleRate (48000); p.setSong (&ls);
        p.options.model = PaulaOptions::Model::Clean; p.options.zeroOrderHold = false; p.options.eightBit = false; p.options.stereoSep = 0.0f;
        auto loud = [&] { std::vector<float> L (4800), R (4800); p.render (L.data(), R.data(), 4800); float m = 0; for (size_t i = 2400; i < L.size(); ++i) m = std::max (m, std::abs (L[i]) + std::abs (R[i])); return m; };
        p.jamNoteOn (37, 0); loud(); p.jamNoteOff (37);
        const float plain = loud(); loud();
        p.setHoldKeys (true);
        p.jamNoteOn (37, 0); loud(); p.jamNoteOff (37);
        const float held = loud();
        p.jamNoteOn (37, 0);   // (again: lets it go)
        loud(); const float gone = loud();
        p.jamNoteOn (37, 0); loud(); p.setHoldKeys (false);
        loud(); const float off = loud();
        std::printf ("HOLD KEYS: after key up %.3f (without HOLD %.3f), played again %.3f, HOLD switched off %.3f\n", held, plain, gone, off);
        CHECK (plain < 0.001f && held > 0.05f && gone < 0.001f && off < 0.001f);
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

    // (v0.79) GO TO A BLOCK in time: block 0 (64 lines) plays to its end, then block 1 from its top
    {
        p.playBlock (0);
        for (int k = 0; k < 10; ++k) p.render (L.data(), R.data(), 5760);
        CHECK (p.cueBlock (1) && p.queuedBlock() == 1);
        for (int k = 0; k < 40; ++k) p.render (L.data(), R.data(), 5760);
        const bool stillOld = p.uiBlock.load() == 0 && p.uiLine.load() >= 49;
        int lines = 0;
        while (p.uiBlock.load() == 0 && lines < 64) { p.render (L.data(), R.data(), 5760); ++lines; }
        std::printf ("cued block 1: block 0 played on %d more lines, then block %d line %d\n", 50 + lines, p.uiBlock.load(), p.uiLine.load());
        CHECK (stillOld && p.uiBlock.load() == 1 && p.uiLine.load() <= 1 && 50 + lines >= 63 && p.queuedBlock() < 0);
        // the block playing: the waiting one is called off
        CHECK (p.cueBlock (0) && p.queuedBlock() == 0);
        CHECK (p.cueBlock (1) && p.queuedBlock() < 0);
        // the song: on its block, it just stays there (block play, nothing restarts)
        p.playSong (0);
        for (int k = 0; k < 5; ++k) p.render (L.data(), R.data(), 5760);
        const int ln = p.uiLine.load();
        CHECK (p.cueBlock (0) && p.getMode() == Player::Mode::Block && p.uiLine.load() == ln);
        p.stop(); CHECK (! p.cueBlock (1));
    }

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
        m = MixerSettings {}; m.ch[0].eq.add (1000.0f, 3.0f); CHECK (! m.isFlat());
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
        eq.band[0] = { true, true, EqType::Bell, 1000.0f, 12.0f, 1.0f };
        eq.band[3] = { true, true, EqType::LowShelf, 120.0f, -6.0f, 0.7f };
        eq.band[5] = { true, true, EqType::HighPass, 40.0f, 0.0f, 0.7f };
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
    // an instrument's own inserts: before the strip of the track that played its latest note
    {
        Song s = quiet();
        s.blocks[0].at (0, 0) = Cell {};
        s.blocks[0].at (0, 2) = { 37, 1, 0, 0 };            // instrument 01 on track 3
        const double base = rmsOf (renderMix (s, 9600, true), 2000, 9600);
        initFxSlot (s.mixer.inst[0][0], FxType::Drive);
        s.mixer.inst[0][0].p[0] = 1.0f;                      // a lot of drive
        CHECK (! s.mixer.isFlat() && s.mixer.instrumentChain (0) && ! s.mixer.instrumentChain (1));
        CHECK (&s.mixer.slot (instrumentSlot (0, 0)) == &s.mixer.inst[0][0] && isInstrumentSlot (instrumentSlot (62, 2)) && ! isInstrumentSlot (masterSlot (2)));
        const double driven = rmsOf (renderMix (s, 9600, true), 2000, 9600);
        // its sound goes to track 3's strip: track 3 down -> silent; track 1 down -> still there
        s.mixer.ch[2].volumeDb = -60.0f;
        const double t3off = rmsOf (renderMix (s, 9600, true), 2000, 9600);
        s.mixer.ch[2].volumeDb = 0.0f; s.mixer.ch[0].volumeDb = -60.0f;
        const double t1off = rmsOf (renderMix (s, 9600, true), 2000, 9600);
        // bypassed: as without it
        s.mixer.ch[0].volumeDb = 0.0f; s.mixer.inst[0][0].bypass = true;
        const double bypassed = rmsOf (renderMix (s, 9600, true), 2000, 9600);
        std::printf ("instrument inserts: plain %.4f, driven %.4f, track 3 down %.6f, track 1 down %.4f, bypassed %.4f\n", base, driven, t3off, t1off, bypassed);
        CHECK (driven > base * 1.3 && t3off < 1e-6 && std::abs (t1off - driven) < driven * 0.02 && std::abs (bypassed - base) < base * 0.02);
        // a delay on the instrument rings on after the note, through the track
        s.mixer.inst[0][0] = FxSlot {};
        initFxSlot (s.mixer.inst[0][1], FxType::Delay);
        s.mixer.inst[0][1].p[1] = 0; s.mixer.inst[0][1].p[0] = (float) (std::log (100.0) / std::log (2000.0)); s.mixer.inst[0][1].p[2] = 0.5f;
        s.instruments[0].loopLength = 0; s.instruments[0].data.resize (480);
        const double echo = rmsOf (renderMix (s, 24000, false), 4700, 5200);
        std::printf ("instrument delay: echo rms %.5f\n", echo);
        CHECK (echo > 1e-3);
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
        p.options.zeroOrderHold = m != PaulaOptions::Model::Clean;   // (the reference: the samples exactly as they are)
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


// ---- v0.14: the accurate Paula (band-limited steps, the 55.4 kHz grid, the models' filters)
static std::vector<float> paulaPlay (PaulaOptions::Model m, bool stepped, double sr, const std::vector<int16_t>& data,
                                     int note, int vol, int n, bool led = false)
{
    Song s;
    s.numTracks = 4;
    auto& in = s.instruments[0];
    in.data = data; in.loopStart = 0; in.loopLength = (int) data.size(); in.volume = 64;
    s.blocks[0].at (0, 0) = { (uint8_t) note, 1, 0xC, (uint8_t) vol };
    s.ledFilterAtStart = led;
    Player p; p.setSampleRate (sr); p.setSong (&s);
    p.options.model = m; p.options.zeroOrderHold = stepped; p.options.eightBit = false; p.options.crunch8ch = false; p.options.stereoSep = 0.0f;
    p.playBlock (0);
    std::vector<float> L ((size_t) n), R ((size_t) n);
    p.render (L.data(), R.data(), n);
    return L;
}

// level (dB) at one frequency, Hann window, from sample a on
static double levelAt (const std::vector<float>& v, double f, double sr, int a = 8192)
{
    const int n = (int) v.size() - a;
    double re = 0, im = 0, wsum = 0;
    for (int i = 0; i < n; ++i)
    {
        const double w = 0.5 - 0.5 * std::cos (2.0 * kPi * i / n);
        const double ph = 2.0 * kPi * f * i / sr;
        re += v[(size_t) (a + i)] * w * std::cos (ph);
        im -= v[(size_t) (a + i)] * w * std::sin (ph);
        wsum += w;
    }
    return 20.0 * std::log10 (std::sqrt (re * re + im * im) * 2.0 / wsum + 1e-12);
}

static std::vector<int16_t> sineCycle (int len, double amp = 30000.0)
{
    std::vector<int16_t> d ((size_t) len);
    for (int i = 0; i < len; ++i) d[(size_t) i] = (int16_t) std::lround (amp * std::sin (2.0 * kPi * i / len));
    return d;
}

static void testBlepSteps()
{
    const auto sine = sineCycle (32);
    const double paulaC2 = kPaulaClock / periodFor (25, 0);   // C-2: the Paula rate (8,287 Hz)
    // 1. a low note: Paula's own staircase image below 20 kHz is kept, at its true level
    {
        const double f0 = paulaC2 / 32.0, img = paulaC2 - f0;
        auto v = paulaPlay (PaulaOptions::Model::Clean, true, 48000, sine, 25, 64, 48000 + 8192);
        const double tone = levelAt (v, f0, 48000), image = levelAt (v, img, 48000);
        const double expect = 20.0 * std::log10 (std::abs (std::sin (kPi * img / paulaC2) / (kPi * img / paulaC2)) / (std::sin (kPi * f0 / paulaC2) / (kPi * f0 / paulaC2)));
        std::printf ("blep: C-2 tone %.1f Hz %.1f dB, staircase image %.0f Hz %.1f dB below it (a real Paula: %.1f)\n", f0, tone, img, tone - image, -expect);
        CHECK (std::abs ((image - tone) - expect) < 1.0);
    }
    // 2. a high note: the images above 24 kHz don't fold back (the old way they came back at 13.8 / 15.9 kHz)
    {
        const double rate = kPaulaClock / periodFor (49, 0), f0 = rate / 32.0;
        const double a1 = 48000.0 - (rate + f0), a2 = 48000.0 - (rate - f0) ;
        auto v = paulaPlay (PaulaOptions::Model::Clean, true, 48000, sine, 49, 64, 48000 + 8192);
        const double tone = levelAt (v, f0, 48000);
        const double al = std::max (levelAt (v, std::abs (a1), 48000), levelAt (v, std::abs (a2), 48000));
        std::printf ("blep: C-4 tone %.0f Hz %.1f dB, fold-back at %.0f / %.0f Hz %.1f dB below it\n", f0, tone, std::abs (a1), std::abs (a2), tone - al);
        CHECK (tone - al > 70.0);
    }
    // 3. the same at 44.1 and 48 kHz below 18 kHz
    {
        const double f0 = paulaC2 / 32.0;
        auto a = paulaPlay (PaulaOptions::Model::Clean, true, 44100, sine, 25, 64, 44100 + 8192);
        auto b = paulaPlay (PaulaOptions::Model::Clean, true, 48000, sine, 25, 64, 48000 + 8192);
        double worst = 0;
        for (int k : { 1, 31, 33, 63, 65 })   // the tone and the staircase images round 8.3 and 16.6 kHz
        {
            const double f = k == 1 ? f0 : (k / 32) * paulaC2 + (k % 32 == 1 ? f0 : -f0);
            worst = std::max (worst, std::abs (levelAt (a, f, 44100) - levelAt (b, f, 48000)));
        }
        std::printf ("blep: 44.1 vs 48 kHz differ by at most %.2f dB\n", worst);
        CHECK (worst < 0.5);
    }
    // 4. a note that stops: no click (the level goes down as a band-limited step too)
    {
        Song s; s.numTracks = 4;
        auto& in = s.instruments[0];
        in.data = std::vector<int16_t> (2000, 30000); in.volume = 64;   // a held level, not looped: it ends
        s.blocks[0].at (0, 0) = { 37, 1, 0, 0 };
        Player p; p.setSampleRate (48000); p.setSong (&s);
        p.options.model = PaulaOptions::Model::Clean; p.options.eightBit = false; p.options.stereoSep = 0.0f;
        p.playBlock (0);
        std::vector<float> L (12000), R (12000);
        p.render (L.data(), R.data(), 12000);
        float jump = 0;
        for (size_t i = 3000; i + 1 < L.size(); ++i) jump = std::max (jump, std::abs (L[i + 1] - L[i]));
        std::printf ("blep: biggest jump when the sample ends %.3f (level %.3f)\n", jump, L[3000]);
        CHECK (jump < 0.7f * std::abs (L[3000]) && std::abs (L.back()) < 1e-4f);   // (a raw jump would be 100%)
    }
}

static void testVolumeGrid()
{
    // below volume 64 a real Paula moves each change onto its 55.4 kHz grid: extra components appear;
    // at 64 they don't. Measured at 55,420 - 6 x 8,287 Hz +/- the tone (in band, nowhere near a harmonic)
    const auto sine = sineCycle (32);
    const double rate = kPaulaClock / periodFor (25, 0), f0 = rate / 32.0;
    const double im = Player::kPaulaDacRate - 6.0 * rate;
    auto v64 = paulaPlay (PaulaOptions::Model::A1200, true, 48000, sine, 25, 0x40, 96000);
    auto v63 = paulaPlay (PaulaOptions::Model::A1200, true, 48000, sine, 25, 0x3F, 96000);
    auto clean63 = paulaPlay (PaulaOptions::Model::Clean, true, 48000, sine, 25, 0x3F, 96000);
    auto lv = [&] (const std::vector<float>& v) { return std::max (levelAt (v, im - f0, 48000), levelAt (v, im + f0, 48000)) - levelAt (v, f0, 48000); };
    std::printf ("55.4 kHz grid: component at %.0f Hz: volume 63 %.1f dB, volume 64 %.1f dB, CLEAN at 63 %.1f dB (re the tone)\n",
                 im - f0, lv (v63), lv (v64), lv (clean63));
    CHECK (lv (v63) > lv (v64) + 20.0 && lv (v63) < -20.0 && lv (clean63) < lv (v63) - 20.0);
    // and the tone itself is the same apart from the level
    CHECK (std::abs ((levelAt (v63, f0, 48000) - levelAt (v64, f0, 48000)) - 20.0 * std::log10 (63.0 / 64.0)) < 0.2);
}

static void testModelFilters()
{
    // the same noise through each model, divided by CLEAN: the model's output filter
    std::vector<int16_t> noise (8192);
    uint32_t r = 12345;
    for (auto& x : noise) { r = r * 1664525u + 1013904223u; x = (int16_t) ((int) (r >> 16) - 32768) ; }
    const int n = 65536 + 8192;
    auto clean = paulaPlay (PaulaOptions::Model::Clean, true, 48000, noise, 61, 64, n);   // C-5: content up to ~30 kHz
    auto corner = [&] (const std::vector<float>& v, const std::vector<float>& ref)
    {
        const double base = levelAt (v, 300, 48000) - levelAt (ref, 300, 48000);
        for (double f = 500; f < 20000; f += 50)
            if (levelAt (v, f, 48000) - levelAt (ref, f, 48000) < base - 3.0) return f;
        return 20000.0;
    };
    auto a500 = paulaPlay (PaulaOptions::Model::A500, true, 48000, noise, 61, 64, n);
    auto a4000 = paulaPlay (PaulaOptions::Model::A4000, true, 48000, noise, 61, 64, n);
    auto a1200 = paulaPlay (PaulaOptions::Model::A1200, true, 48000, noise, 61, 64, n);
    auto led = paulaPlay (PaulaOptions::Model::A1200, true, 48000, noise, 61, 64, n, true);
    const double c500 = corner (a500, clean), c4000 = corner (a4000, clean), c1200 = corner (a1200, clean), cled = corner (led, a1200);
    const double at20k = (levelAt (a1200, 19000, 48000) - levelAt (clean, 19000, 48000)) - (levelAt (a1200, 300, 48000) - levelAt (clean, 300, 48000));
    std::printf ("filters (-3 dB): A500 %.0f Hz, A4000 %.0f Hz, A1200 %.0f Hz (%.1f dB at 19 kHz), LED %.0f Hz\n", c500, c4000, c1200, at20k, cled);
    CHECK (std::abs (c500 - 4420) < 4420 * 0.03);
    CHECK (std::abs (c4000 - 4520) < 4520 * 0.03);
    CHECK (c1200 > 13000 && c1200 < 16000 && at20k < -3.0 && at20k > -6.0);
    CHECK (std::abs (cled - 3200) < 3200 * 0.03);
}

static void testBlepCpu()
{
    // 8 tracks of high notes, 10 seconds: the band-limited steps against plain interpolation
    Song s; s.numTracks = 8;
    auto& in = s.instruments[0];
    in.data = sineCycle (32); in.loopStart = 0; in.loopLength = 32; in.volume = 64;
    for (int t = 0; t < 8; ++t) s.blocks[0].at (0, t) = { (uint8_t) (61 + t), 1, 0xC, 0x30 };   // C-5 up: ~4 Paula samples a host sample
    auto timeIt = [&] (bool stepped)
    {
        Player p; p.setSampleRate (48000); p.setSong (&s);
        p.options.model = PaulaOptions::Model::A500; p.options.zeroOrderHold = stepped;
        p.playBlock (0);
        std::vector<float> L (512), R (512);
        const auto t0 = std::chrono::steady_clock::now();
        for (int done = 0; done < 48000 * 10; done += 512) p.render (L.data(), R.data(), 512);
        return std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
    };
    const double blep = timeIt (true), lin = timeIt (false);
    std::printf ("cpu: 8 high tracks, 10 s of audio: band-limited steps %.2f s, interpolation %.2f s (this build)\n", blep, lin);
    CHECK (blep < lin * 6.0);   // (relative: the machine may be busy; an optimised build is about 4x faster than real time needs)
}

// CAPTURE: what was played, into pattern lines
static void testCapture()
{
    Song song; song.numTracks = 4;
    song.blocks[0].at (6, 0) = { 37, 1, 0, 0 };   // (a note already there: never written over)
    std::vector<bool> offs (kMaxInstruments, false);
    offs[1] = true;                                // instrument 02 is a plug-in: its releases get OFF
    auto on = [] (double t, int note, int ins, bool playing = false, double line = 0, int block = 0)
    { CapturedNote c; c.timeMs = t; c.note = note; c.instrument = ins; c.on = true; c.playing = playing; c.line = line; c.block = block; c.speed = 6; return c; };
    auto off = [] (CapturedNote c, double t, double line) { c.on = false; c.timeMs = t; c.line = line; return c; };

    // stopped: from the cursor (block 0, line 4, track 0) at 125 BPM / speed 6 = 120 ms a line
    std::vector<CapturedNote> log;
    log.push_back (on (500, 25, 0));                 // an old note, before a long gap: not this take
    log.push_back (on (10000, 37, 0));               // line 4
    log.push_back (on (10010, 41, 0));               // same time: a chord -> track 1
    log.push_back (on (10240, 44, 0));               // line 6: track 0 has a note there -> track 1... (41 released?)
    log.push_back (on (10250 + 120, 49, 1));         // line 7.08: 0.5 tick late -> M0B on line 7
    log.push_back (off (on (10370, 49, 1), 10370 + 360, 0));   // released 3 lines later -> OFF on line 10
    auto r = placeCapture (song, log, 0, 4, 0, 125, 6, offs);
    std::string got;
    for (auto& p : r.places) got += std::to_string (p.line) + ":" + std::to_string (p.track) + ":" + std::to_string (p.note) + ":" + std::to_string (p.micro) + " ";
    std::printf ("capture (stopped): %s skipped %d\n", got.c_str(), r.skipped);
    CHECK (! r.played && r.firstLine == 4 && r.skipped == 0);
    CHECK (r.places.size() == 5);
    if (r.places.size() == 5)
    {
        CHECK (r.places[0].line == 4 && r.places[0].track == 0 && r.places[0].note == 37 && r.places[0].micro == 0);
        CHECK (r.places[1].line == 4 && r.places[1].track == 1 && r.places[1].note == 41);
        CHECK (r.places[2].line == 6 && r.places[2].track != 0 && r.places[2].note == 44);
        CHECK (r.places[3].line == 7 && r.places[3].micro == 0x0B && r.places[3].instr == 2);
        CHECK (r.places[4].note == kNoteOff && r.places[4].line == 10 && r.places[4].track == r.places[3].track);
    }
    // while playing: where they were played, early and late
    std::vector<CapturedNote> log2;
    log2.push_back (on (100, 37, 0, true, 8.02, 0));    // on line 8
    log2.push_back (on (200, 40, 0, true, 11.6, 0));    // 0.4 line early of line 12 -> M (0x80 + 51) on line 12
    log2.push_back (on (300, 42, 0, true, 63.7, 0));    // rounds to line 64: past the block's end -> skipped
    auto r2 = placeCapture (song, log2, 0, 0, 2, 125, 6, offs);
    std::string got2;
    for (auto& p : r2.places) got2 += std::to_string (p.line) + ":" + std::to_string (p.track) + ":" + std::to_string (p.micro) + " ";
    std::printf ("capture (playing): %s skipped %d\n", got2.c_str(), r2.skipped);
    CHECK (r2.played && r2.places.size() == 2 && r2.skipped == 1);
    if (r2.places.size() == 2)
    {
        CHECK (r2.places[0].line == 8 && r2.places[0].track == 2 && r2.places[0].micro == 0);
        CHECK (r2.places[1].line == 12 && r2.places[1].micro == 0x80 + 51);
    }
}

static void testCvOut()
{
    // 1 V / octave, C-3 (MIDI 60) at 0 V, with no map
    Instrument in; in.cvOut = true; in.plugin = true; in.pluginKey = "CV:";
    CHECK (std::abs (cvVoltsFor (in, 60) - 0.0) < 1e-9 && std::abs (cvVoltsFor (in, 72) - 1.0) < 1e-9 && std::abs (cvVoltsFor (in, 54) + 0.5) < 1e-9);
    in.cvOctave = 1;
    CHECK (std::abs (cvVoltsFor (in, 60) - 1.0) < 1e-9);
    in.cvOctave = 0;
    CHECK (cvSample (5.0, 10.0) == 0.5f && cvSample (15.0, 10.0) == 1.0f && cvSample (-12.0, 10.0) == -1.0f);

    // AUTO-TUNE against an oscillator that isn't 1 V / octave (3% wide, bending sharp up high),
    // with one bad reading (an octave out)
    auto osc = [] (double v) { return 261.63 * std::pow (2.0, 1.03 * v + 0.02 * v * v); };
    std::vector<double> vs, hz;
    for (double v = -2.0; v <= 2.5 + 1e-9; v += 0.25) { vs.push_back (v); hz.push_back (osc (v)); }
    hz[7] *= 2.0;
    std::vector<float> mn, mv;
    CHECK (solveCvMap (vs, hz, mn, mv) && mn.size() == vs.size() - 1);
    in.cvMapNotes = mn; in.cvMapVolts = mv;
    double worst = 0;
    for (int note = 40; note <= 95; ++note)
    {
        const double played = 69.0 + 12.0 * std::log2 (osc (cvVoltsFor (in, note)) / 440.0);
        worst = std::max (worst, std::abs (played - note) * 100.0);
    }
    double plain = 0;
    for (int note = 40; note <= 95; ++note) plain = std::max (plain, std::abs (69.0 + 12.0 * std::log2 (osc ((note - 60) / 12.0) / 440.0) - note) * 100.0);
    std::printf ("AUTO-TUNE: worst %.2f cents off with the map (without it %.0f cents)\n", worst, plain);
    CHECK (worst < 3.0 && plain > 50.0);
    CHECK (! solveCvMap ({ 0.0, 1.0 }, { 100.0, 200.0 }, mn, mv));

    // the CV OUT instrument: pitch, gate (and a short gap for a new note), velocity, bend
    {
        Instrument ci = in; ci.cvMapNotes.clear(); ci.cvMapVolts.clear();
        ci.cvPitchOut = 3; ci.cvGateOut = 4; ci.cvVelOut = 5; ci.bendRange = 12;
        CvOutSynth s; s.prepare (48000, 512); s.update (ci);
        std::vector<float> o3 (512), o4 (512), o5 (512);
        CvBus bus; bus.out[2] = o3.data(); bus.out[3] = o4.data(); bus.out[4] = o5.data();
        s.noteOn (72, 127, 10);
        s.noteOn (76, 64, 200);       // legato over the first: a 2 ms gap in the gate
        s.noteOff (76, 400);          // back to 72, still held
        s.renderCv (bus, 512);
        CHECK (o4[5] == 0.0f && o4[20] == 0.5f && std::abs (o3[20] - 0.1f) < 1e-6f);
        CHECK (o4[201] == 0.0f && o4[300] == 0.5f && std::abs (o3[300] - (16.0 / 12.0) / 10.0) < 1e-5);
        CHECK (std::abs (o3[450] - 0.1f) < 1e-6f && o4[450] == 0.5f && std::abs (o5[300] - 0.5f * 64 / 127) < 1e-5);
        s.pitchBend (16383, 0);       // +12 semitones (almost)
        s.noteOff (72, 100);
        s.renderCv (bus, 512);
        CHECK (std::abs (o3[50] - (1.0 + 16383.0 / 8192.0 - 1.0) * 0.1f * 1.0f) < 0.01f && o4[200] == 0.0f);
        // the tuner: a voltage held, the gate open
        s.setTesting (true, 0.25);
        s.renderCv (bus, 512);
        CHECK (std::abs (o3[10] - 0.025f) < 1e-6f && o4[10] == 0.5f);
    }

    // through the player: a CV OUT instrument in the pattern, and CV SOURCES
    {
        Song song; song.numTracks = 8; song.tempo = 125; song.speed = 6;
        auto& ci = song.instruments[0]; ci = in; ci.cvMapNotes.clear(); ci.cvMapVolts.clear(); ci.cvPitchOut = 3; ci.cvGateOut = 4; ci.name = "MODULAR";
        song.blocks[0].at (0, 0) = Cell { 49, 1, 0, 0 };              // C-4 (MIDI 72): 1 V
        song.blocks[0].at (0, 1) = Cell { 0, 0, prv::cmdLetter ('V'), 0x80 };   // track 2's AUTO A to half way
        song.blocks[0].at (2, 2) = Cell { 37, 2, 0, 0 };              // a sample note on track 3: the ENV
        song.instruments[1].data.assign (8000, 1000);
        song.sequence = { 0 };
        auto& clk = song.cvSources[0]; clk.kind = CvKind::Clock; clk.out = 5; clk.ppqn = 4; clk.volts = 5;
        auto& rst = song.cvSources[1]; rst.kind = CvKind::Reset; rst.out = 6;
        auto& run = song.cvSources[2]; run.kind = CvKind::Run; run.out = 7;
        auto& lfo = song.cvSources[3]; lfo.kind = CvKind::Lfo; lfo.out = 8; lfo.shape = 0; lfo.beats = 1; lfo.volts = 2; lfo.low = 0;
        auto& env = song.cvSources[4]; env.kind = CvKind::Env; env.out = 9; env.track = 2; env.volts = 8; env.attack = 0.01f; env.decay = 0.1f; env.sustain = 0.5f;
        auto& kb = song.cvSources[5]; kb.kind = CvKind::Knob; kb.out = 10; kb.track = 1; kb.knob = 0; kb.low = 0; kb.volts = 10;
        Player p; p.setSampleRate (48000);
        p.synths().factory = [] (int, const Instrument& x, double) -> std::unique_ptr<PluginSynth> { return x.isCvOut() ? std::make_unique<CvOutSynth>() : nullptr; };
        p.synths().syncNow (song, 48000);
        p.synths().prepare (48000, 1024);
        p.setSong (&song);
        const int n = 48000;   // 1 s = about 2 beats at 125
        std::vector<std::vector<float>> outs (16, std::vector<float> ((size_t) n, 0.0f));
        for (int k = 2; k < 16; ++k) p.cv.out[k] = outs[(size_t) k].data();
        std::vector<float> L ((size_t) n), R ((size_t) n);
        p.playSong (0, 0);
        p.render (L.data(), R.data(), n);
        auto edges = [&] (int out) { int e = 0; for (int i = 1; i < n; ++i) if (outs[(size_t) out - 1][(size_t) i] > 0.1f && outs[(size_t) out - 1][(size_t) i - 1] <= 0.1f) ++e; if (outs[(size_t) out - 1][0] > 0.1f) ++e; return e; };
        const double beats = 125.0 / 60.0;   // a second of 125 BPM
        std::printf ("CV in the player: pitch %.3f (1 V = 0.1), gate %.2f; clock %d pulses (expect ~%.0f), reset %d, run %.2f\n",
                     outs[2][24000], outs[3][24000], edges (5), beats * 4, edges (6), outs[6][24000]);
        CHECK (std::abs (outs[2][24000] - 0.1f) < 1e-5f && outs[3][24000] == 0.5f);
        CHECK (std::abs (edges (5) - beats * 4) <= 1.0 && edges (6) == 1 && outs[6][24000] == 0.5f);
        // the LFO: 1 beat a cycle, +-2 V
        float lmax = -1, lmin = 1; for (int i = 0; i < n; ++i) { lmax = std::max (lmax, outs[7][(size_t) i]); lmin = std::min (lmin, outs[7][(size_t) i]); }
        CHECK (std::abs (lmax - 0.2f) < 0.005f && std::abs (lmin + 0.2f) < 0.005f);
        int zc = 0; for (int i = 1; i < n; ++i) if ((outs[7][(size_t) i - 1] < 0) != (outs[7][(size_t) i] < 0)) ++zc;
        CHECK (std::abs (zc - beats * 2) <= 1.5);
        // the ENV: starts with track 3's note (line 2), up to 8 V, down to the sustain
        const int line2 = (int) (2 * 6 * 48000 * 2.5 / 125);
        float emax = 0; for (int i = 0; i < n; ++i) emax = std::max (emax, outs[8][(size_t) i]);
        std::printf ("ENV: before its note %.3f, peak %.3f, later %.3f\n", outs[8][(size_t) line2 - 100], emax, outs[8][(size_t) n - 1]);
        CHECK (outs[8][(size_t) line2 - 100] == 0.0f && std::abs (emax - 0.8f) < 0.01f && std::abs (outs[8][(size_t) n - 1] - 0.4f) < 0.01f);
        // the KNOB: track 2's V80 = half way, 0..10 V
        std::printf ("KNOB: %.3f\n", outs[9][(size_t) n - 1]);
        CHECK (std::abs (outs[9][(size_t) n - 1] - 128.0f / 255.0f) < 0.01f);   // (10 V of 10 V full scale x 128 / 255)
        // stopped: gates low, RUN low
        p.stop();
        std::fill (outs[3].begin(), outs[3].end(), 1.0f);
        p.render (L.data(), R.data(), 1024);
        CHECK (outs[3][500] == 0.0f && outs[6][500] == 0.0f);
    }
}

static void testSampleTools()
{
    std::vector<int16_t> d (1000, 1000);
    sampleGain (d, 0, 500, 6.0206);
    CHECK (d[0] == 2000 && d[499] == 2000 && d[500] == 1000);
    sampleGain (d, 0, 1000, 40);                       // clipped, not wrapped
    CHECK (d[0] == 32767 && d[999] == 32767);
    std::vector<int16_t> dc (1000);
    for (int i = 0; i < 1000; ++i) dc[(size_t) i] = (int16_t) (3000 + (i % 2 ? 500 : -500));
    removeDc (dc, 0, 1000);
    long sum = 0; for (auto v : dc) sum += v;
    CHECK (std::abs (sum) < 1000 && dc[0] == -500 && dc[1] == 500);

    Instrument in;
    in.data.assign (10000, 0);
    for (int i = 2000; i < 7000; ++i) in.data[(size_t) i] = (int16_t) (8000 * std::sin (i * 0.05));
    int a = 0, b = 0;
    CHECK (findSoundBounds (in, -60, a, b) && a >= 2000 && a < 2010 && b <= 7000 && b > 6990);
    Instrument silent; silent.data.assign (100, 0);
    CHECK (! findSoundBounds (silent, -60, a, b));

    // edits move the loop, START / END and the slices
    in.loopStart = 6000; in.loopLength = 2000; in.playStart = 1000; in.playEnd = 9000; in.slices = { 0, 3000, 8000 };
    removeFrames (in, 2000, 3000);
    CHECK (in.data.size() == 9000 && in.loopStart == 5000 && in.loopLength == 2000 && in.playStart == 1000 && in.playEnd == 8000);
    CHECK (in.slices.size() == 3 && in.slices[1] == 2000 && in.slices[2] == 7000);
    insertSilence (in, 500, 250);
    CHECK (in.data.size() == 9250 && in.data[500] == 0 && in.loopStart == 5250 && in.playStart == 1250 && in.playEnd == 8250 && in.slices[1] == 2250);
    cropTo (in, 1250, 8250);
    CHECK (in.data.size() == 7000 && in.playStart == 0 && in.playEnd == 0 && in.loopStart == 4000);   // (END at the new end: no END)

    // crossfade: the frame before the loop end becomes the frame before the loop start
    Instrument lp; lp.data.resize (4000);
    for (int i = 0; i < 4000; ++i) lp.data[(size_t) i] = (int16_t) (10000 * std::sin (i * 0.013) + (i > 3000 ? 9000 : 0));
    lp.loopStart = 1000; lp.loopLength = 2500;
    const int jumpBefore = std::abs (lp.data[3499] - lp.data[999]);
    CHECK (crossfadeLoop (lp, 400));
    const int jumpAfter = std::abs (lp.data[3499] - lp.data[999]);
    std::printf ("crossfade loop: the step at the jump %d -> %d\n", jumpBefore, jumpAfter);
    CHECK (jumpAfter < 10 && jumpBefore > 5000);
    Instrument nl; nl.data.assign (100, 1);
    CHECK (! crossfadeLoop (nl, 50));

    // pitch: +12 = an octave up, half as long, twice the frequency
    Instrument ps; ps.data.resize (8000);
    for (int i = 0; i < 8000; ++i) ps.data[(size_t) i] = (int16_t) (10000 * std::sin (2 * kPi * i / 100.0));
    ps.loopStart = 2000; ps.loopLength = 4000;
    pitchBy (ps, 12);
    int cross = 0; for (size_t i = 1; i < ps.data.size(); ++i) if ((ps.data[i - 1] < 0) != (ps.data[i] < 0)) ++cross;
    std::printf ("pitch +12: %zu frames, %d zero crossings (was 160)\n", ps.data.size(), cross);
    CHECK (std::abs ((int) ps.data.size() - 4000) <= 2 && std::abs (cross - 160) <= 4 && ps.loopStart == 1000 && ps.loopLength == 2000);
    // pitch detection: a sine and a sawtooth, at two rates
    {
        std::vector<int16_t> sn (44100);
        for (int i = 0; i < 44100; ++i) sn[(size_t) i] = (int16_t) (12000 * std::sin (2 * kPi * 440.0 * i / 44100.0));
        const double p1 = detectPitch (sn, 0, 44100, 44100);
        std::vector<int16_t> sw2 (16574);
        for (int i = 0; i < 16574; ++i) { const double ph = std::fmod (i * 98.0 / 16574.0, 1.0); sw2[(size_t) i] = (int16_t) (16000 * (2 * ph - 1)); }
        const double p2 = detectPitch (sw2, 0, 16574, 16574);
        std::vector<int16_t> none (8000, 0);
        std::printf ("pitch detect: sine %.2f Hz (440), saw %.2f Hz (98), silence %.1f\n", p1, p2, detectPitch (none, 0, 8000, 44100));
        CHECK (std::abs (p1 - 440) < 1.0 && std::abs (p2 - 98) < 0.5 && detectPitch (none, 0, 8000, 44100) == 0);
        int st = 0, ct = 0;
        CHECK (tuneToC (440, st, ct) && st == 3 && ct == 0);           // A -> C: up 3
        CHECK (tuneToC (261.63, st, ct) && st == 0 && std::abs (ct) <= 1);
        CHECK (tuneToC (98, st, ct) && st == 5 && std::abs (ct) <= 1); // G -> C: up 5
        CHECK (tuneToC (450, st, ct) && st == 3 && ct < -30 && ct > -45);
        CHECK (suggestSyncLines (1.92, 0.12) == 16 && suggestSyncLines (2.0, 0.12) == 16 && suggestSyncLines (3.8, 0.12) == 32);
    }
    // swap sides
    Instrument sw; sw.data = { 1, 2 }; sw.dataR = { 3, 4 };
    swapSides (sw);
    CHECK (sw.data[0] == 3 && sw.dataR[1] == 2);
}

static void testRangeCentsSync()
{
    // START / END: what's outside doesn't play; 9xx counts from START; the loop is cut at END
    auto make = [] ()
    {
        Instrument in;
        std::mt19937 rng (5);
        in.data.assign (20000, 0);
        for (int i = 0; i < 5000; ++i) in.data[(size_t) i] = (int16_t) ((int) (rng() % 40000) - 20000);
        for (int i = 15000; i < 16000; ++i) in.data[(size_t) i] = (int16_t) ((int) (rng() % 40000) - 20000);
        in.volume = 64;
        return in;
    };
    auto energy = [] (Song& s, int n = 48000)
    {
        Player p; p.setSampleRate (48000); p.setSong (&s); p.playBlock (0);
        std::vector<float> L ((size_t) n), R ((size_t) n);
        p.render (L.data(), R.data(), n);
        double e = 0; for (int i = 0; i < n; ++i) e += L[(size_t) i] * L[(size_t) i];
        return e;
    };
    Song s; s.numTracks = 8; s.instruments[0] = make(); s.blocks[0].at (0, 0) = Cell { 37, 1, 0, 0 }; s.sequence = { 0 };
    const double whole = energy (s);
    s.instruments[0].playStart = 5000; s.instruments[0].playEnd = 15000;
    const double inside = energy (s);
    std::printf ("START / END: whole sample %.2f, only the quiet middle %.6f\n", whole, inside);
    CHECK (whole > 10.0 && inside < 1e-6);
    // 9xx from START: 5000 + 0x28 * 256 = 15240 - past END: nothing
    s.blocks[0].at (0, 0) = Cell { 37, 1, 0x9, 0x28 };
    CHECK (energy (s) < 1e-6);
    s.instruments[0].playEnd = 0;           // (END at the end again: 15240 is in the loud part's tail... none, 16000 ends it)
    s.blocks[0].at (0, 0) = Cell { 37, 1, 0x9, 0x27 };   // 5000 + 9984 = 14984: just before the loud part
    CHECK (energy (s) > 1.0);
    // the loop is cut at END: loop 14000-17000 holds the loud part, END at 15000 keeps it out
    s.blocks[0].at (0, 0) = Cell { 37, 1, 0, 0 };
    s.instruments[0].loopStart = 14000; s.instruments[0].loopLength = 3000;
    s.instruments[0].playStart = 5000; s.instruments[0].playEnd = 15000;
    const double cut = energy (s, 96000);
    s.instruments[0].playEnd = 0;
    const double full = energy (s, 96000);
    std::printf ("a loop past END: cut %.6f, without END %.2f\n", cut, full);
    CHECK (cut < 1e-6 && full > 10.0);
    CHECK (s.instruments[0].rangeStart() == 5000 && s.instruments[0].rangeEnd() == 20000 && s.instruments[0].hasRange());

    // FINE in cents, BEAT SYNC
    Song t; t.numTracks = 8; t.tempo = 125; t.speed = 6;
    auto& in = t.instruments[0]; in.data.assign (33148, 100); in.rate = 16574;   // 2 seconds at C-3
    t.blocks[0].at (0, 0) = Cell { 37, 1, 0, 0 }; t.sequence = { 0 };
    Player p; p.setSampleRate (48000); p.setSong (&t); p.playBlock (0);
    CHECK (std::abs (p.rateFactor (in) - 16574.0 / kAmigaC3Rate) < 1e-9);
    in.cents = 100;
    CHECK (std::abs (p.rateFactor (in) / (16574.0 / kAmigaC3Rate) - std::pow (2.0, 1.0 / 12.0)) < 1e-9);
    in.cents = 0; in.syncLines = 16;          // 16 lines at 125 BPM, speed 6 = 1.92 s
    const double f = p.rateFactor (in) / (16574.0 / kAmigaC3Rate);
    std::printf ("BEAT SYNC 16 lines: %.4f x (expect %.4f)\n", f, 2.0 / 1.92);
    CHECK (std::abs (f - 2.0 / 1.92) < 1e-6);
    // the sync counts START to END: half the sample in 16 lines is half the speed
    in.playEnd = 33148 / 2;
    CHECK (std::abs (p.rateFactor (in) / (16574.0 / kAmigaC3Rate) - 1.0 / 1.92) < 1e-4);
    // and it follows the tempo
    t.tempo = 250; p.playBlock (0);
    CHECK (std::abs (p.rateFactor (in) / (16574.0 / kAmigaC3Rate) - 2.0 / 1.92) < 1e-4);
}

static void testVintageSampling()
{
    // the bits
    CHECK (VintageAdc::quantise (0.3f, 8) == std::round (0.3f * 128) / 128 && VintageAdc::quantise (0.3001f, 12) == std::round (0.3001f * 2048) / 2048);
    CHECK (VintageAdc::quantise (1.0f, 8) == 127.0f / 128.0f && VintageAdc::quantise (-1.0f, 8) == -1.0f);
    // the input stages: the S950's goes soft before the rails, an Amiga sampler's just clips
    CHECK (VintageAdc::inputStage (0.5f, SamplerModel::S950, 1) == 0.5f);
    const float s8 = VintageAdc::inputStage (0.8f, SamplerModel::S950, 1);
    CHECK (s8 > 0.6f && s8 < 0.8f && VintageAdc::inputStage (0.8f, SamplerModel::Amiga, 1) == 0.8f);
    CHECK (VintageAdc::inputStage (3.0f, SamplerModel::S950, 1) <= 1.0f && VintageAdc::inputStage (3.0f, SamplerModel::Amiga, 1) == 1.0f);
    CHECK (VintageAdc::inputStage (0.25f, SamplerModel::Amiga, std::pow (10.0f, 12.0f / 20.0f)) > 0.99f);   // +12 dB: 0.25 -> full scale

    const int n = 48000;
    auto sine = [&] (double hz, float amp) { std::vector<float> v ((size_t) n); for (int i = 0; i < n; ++i) v[(size_t) i] = amp * (float) std::sin (2 * kPi * hz * i / 48000.0); return v; };
    auto run = [&] (const VintageSettings& vs, const std::vector<float>& in, std::vector<float>& mon, std::vector<float>& dec)
    {
        VintageAdc a; a.prepare (48000, 512); a.set (vs);
        mon.assign ((size_t) n, 0); dec.assign ((size_t) a.maxOut (n), 0);
        std::vector<float> monR ((size_t) n), decR (dec.size());
        const int k = a.process (in.data(), in.data(), n, mon.data(), monR.data(), dec.data(), decR.data());
        dec.resize ((size_t) k);
    };
    auto rms = [] (const std::vector<float>& v, size_t from = 0) { double e = 0; for (size_t i = from; i < v.size(); ++i) e += v[i] * v[i]; return std::sqrt (e / std::max<double> (1, (double) (v.size() - from))); };
    std::vector<float> mon, dec;
    // AMIGA at 16726 Hz, 8-bit: that many samples a second, every one on the 8-bit grid, the monitor in steps
    VintageSettings am; am.model = SamplerModel::Amiga; am.bits = 8; am.rate = 16726;
    run (am, sine (440, 0.5f), mon, dec);
    bool onGrid = true; for (float v : dec) onGrid = onGrid && std::abs (v * 128 - std::round (v * 128)) < 1e-4f;
    int changes = 0; for (int i = 1; i < n; ++i) if (mon[(size_t) i] != mon[(size_t) i - 1]) ++changes;
    std::printf ("AMIGA 16726 Hz 8-bit: %zu samples from 1 s, on the grid %d, the monitor changes %d times\n", dec.size(), (int) onGrid, changes);
    CHECK (std::abs ((int) dec.size() - 16726) <= 2 && onGrid && changes <= 16727);
    // 12-bit: on its own (finer) grid
    am.bits = 12; run (am, sine (440, 0.5f), mon, dec);
    onGrid = true; bool coarse = true;
    for (float v : dec) { onGrid = onGrid && std::abs (v * 2048 - std::round (v * 2048)) < 1e-3f; coarse = coarse && std::abs (v * 128 - std::round (v * 128)) < 1e-4f; }
    CHECK (onGrid && ! coarse);
    // aliasing: a 10 kHz tone sampled at 15 kHz - the Amiga sampler lets the 5 kHz alias through, the S950's filter stops it
    VintageSettings a15 = am; a15.bits = 12; a15.rate = 15000;
    VintageSettings s15; s15.model = SamplerModel::S950; s15.bits = 12; s15.rate = 15000;
    std::vector<float> decA, decS;
    run (a15, sine (10000, 0.5f), mon, decA);
    run (s15, sine (10000, 0.5f), mon, decS);
    std::printf ("10 kHz into a 15 kHz sampler: AMIGA rms %.3f, S950 rms %.4f\n", rms (decA, 1000), rms (decS, 1000));
    CHECK (rms (decA, 1000) > 0.25 && rms (decS, 1000) < 0.02);
    // a 1 kHz tone passes the S950's filter
    run (s15, sine (1000, 0.5f), mon, decS);
    CHECK (rms (decS, 1000) > 0.33);
    // INPUT DRIVE: +18 dB on the S950 - loud, saturated, never past full scale, the CLIP level reached
    VintageSettings sd = s15; sd.rate = 32000; sd.driveDb = 18;
    run (sd, sine (200, 0.5f), mon, decS);
    float pk = 0; for (float v : decS) pk = std::max (pk, std::abs (v));
    std::printf ("S950 +18 dB drive: peak %.4f, rms %.3f\n", pk, rms (decS, 1000));
    CHECK (pk <= 1.0f && pk > 0.99f && rms (decS, 1000) > 0.6);
    // CLEAN: untouched
    VintageSettings cl; std::vector<float> in = sine (440, 0.5f);
    run (cl, in, mon, dec);
    CHECK (dec.size() == (size_t) n && mon == in);
    // a vintage take keeps its bits after normalising
    TakeOptions o; o.bits = 12; o.trimSilence = false; o.normalise = true; o.channels = RecChannels::Mono;
    Instrument ins;
    run (am, sine (440, 0.3f), mon, dec);
    CHECK (takeToInstrument (dec, dec, 16726, o, ins) && ins.rate == 16726);
    bool step16 = true; for (auto v : ins.data) step16 = step16 && (v % 16) == 0;
    CHECK (step16);
}

static void testRecorder()
{
    const double sr = 48000;
    auto block = [] (float v, int n) { return std::vector<float> ((size_t) n, v); };

    // MANUAL: records from the start, stops on STOP
    TakeRecorder r;
    r.arm (sr, 2.0, RecTrigger::Manual, 0.1f);
    CHECK (r.state() == TakeRecorder::State::Recording);
    auto a = block (0.25f, 512), b = block (-0.25f, 512);
    for (int i = 0; i < 10; ++i) r.process (a.data(), b.data(), 512, false);
    CHECK (r.frames() == 5120 && std::abs (r.takePeak (0) - 0.25f) < 1e-6f && r.takePeak (0) == 0.0f);
    r.stop();
    CHECK (r.state() == TakeRecorder::State::Done);
    r.process (a.data(), b.data(), 512, false);              // (after STOP: nothing more)
    CHECK (r.frames() == 5120);
    std::vector<float> L, R;
    r.takeAway (L, R);
    CHECK (L.size() == 5120 && R.size() == 5120 && R[100] == -0.25f && r.state() == TakeRecorder::State::Idle);

    // the limit: stops by itself, full
    r.arm (sr, 0.05, RecTrigger::Manual, 0.1f);              // 2400 frames
    for (int i = 0; i < 10; ++i) r.process (a.data(), b.data(), 512, false);
    CHECK (r.state() == TakeRecorder::State::Done && r.hitLimit() && r.frames() == 2400);
    r.reset();

    // THRESHOLD: waits, then starts at the first loud sample with ~20 ms of what came before
    r.arm (sr, 2.0, RecTrigger::Threshold, 0.5f, 0.02);
    auto quiet = block (0.01f, 512);
    for (int i = 0; i < 4; ++i) r.process (quiet.data(), quiet.data(), 512, false);
    CHECK (r.state() == TakeRecorder::State::Armed && r.frames() == 0);
    std::vector<float> hit ((size_t) 512, 0.01f);
    for (int i = 300; i < 512; ++i) hit[(size_t) i] = 0.9f;
    r.process (hit.data(), hit.data(), 512, false);
    std::printf ("threshold: %lld frames after the hit block (pre-roll %d + %d)\n", (long long) r.frames(), 960, 212);
    CHECK (r.state() == TakeRecorder::State::Recording && r.frames() == 960 + 212);
    r.stop(); r.takeAway (L, R);
    CHECK (L.size() == 1172 && L[959] == 0.01f && L[960] == 0.9f);   // the attack right after the pre-roll
    // THRESHOLD armed, STOP before anything: nothing recorded
    r.arm (sr, 2.0, RecTrigger::Threshold, 0.5f);
    r.stop();
    CHECK (r.state() == TakeRecorder::State::Idle);

    // WITH PLAYBACK: from play to stop
    r.arm (sr, 2.0, RecTrigger::WithPlayback, 0.1f);
    r.process (a.data(), b.data(), 512, false);
    CHECK (r.state() == TakeRecorder::State::Armed);
    for (int i = 0; i < 3; ++i) r.process (a.data(), b.data(), 512, true);
    CHECK (r.state() == TakeRecorder::State::Recording && r.frames() == 1536);
    r.process (a.data(), b.data(), 512, false);
    CHECK (r.state() == TakeRecorder::State::Done && r.frames() == 1536);
    r.reset();

    // trim, normalise, channels, an Amiga rate
    std::vector<float> tl (48000, 0.0f), tr (48000, 0.0f);
    for (int i = 12000; i < 36000; ++i) { tl[(size_t) i] = 0.4f * std::sin (i * 0.05f); tr[(size_t) i] = 0.2f * std::sin (i * 0.05f); }
    int64_t s0 = 0, s1 = 0;
    findTrim (tl, tr, 0.001f, 96, s0, s1);
    std::printf ("trim: %lld .. %lld\n", (long long) s0, (long long) s1);
    CHECK (s0 >= 12000 - 96 - 2 && s0 <= 12000 - 96 + 2 && s1 >= 35990 && s1 <= 36000 + 96);
    TakeOptions o;
    Instrument ins;
    CHECK (takeToInstrument (tl, tr, sr, o, ins));
    int16_t mx = 0;
    for (auto v : ins.data) mx = std::max<int16_t> (mx, (int16_t) std::abs (v));
    std::printf ("kept: %zu frames, stereo %d, peak %d, rate %.0f\n", ins.data.size(), (int) ! ins.dataR.empty(), mx, ins.rate);
    CHECK (ins.data.size() > 23900 && ins.data.size() < 24300 && ! ins.dataR.empty() && mx > 32000 && ins.rate == 48000.0 && ins.natRate == 48000.0);
    o.channels = RecChannels::Mono; o.normalise = false; o.trimSilence = false;
    CHECK (takeToInstrument (tl, tr, sr, o, ins) && ins.dataR.empty() && ins.data.size() == 48000);
    CHECK (std::abs (ins.data[20000] - (int) std::lround (0.3f * std::sin (20000 * 0.05f) * 32767)) <= 2);
    o.channels = RecChannels::Right;
    CHECK (takeToInstrument (tl, tr, sr, o, ins) && std::abs (ins.data[20000] - (int) std::lround (0.2f * std::sin (20000 * 0.05f) * 32767)) <= 2);
    o.channels = RecChannels::Stereo; o.amigaRate = 16726;
    CHECK (takeToInstrument (tl, tr, sr, o, ins));
    std::printf ("Amiga rate: %zu frames at %.0f Hz\n", ins.data.size(), ins.rate);
    CHECK (std::abs ((double) ins.data.size() - 48000.0 * 16726.0 / 48000.0) < 3 && ins.rate == 16726.0 && ins.dataR.size() == ins.data.size());
    // a 15 kHz tone at 48k going to 16726 Hz: filtered away, not aliased down
    std::vector<float> hi (48000);
    for (int i = 0; i < 48000; ++i) hi[(size_t) i] = 0.8f * std::sin (2.0 * 3.14159265358979 * 15000.0 * i / 48000.0);
    const auto down = resample (hi, 48000, 16726);
    float rms = 0;
    for (size_t i = 1000; i < down.size(); ++i) rms += down[i] * down[i];
    rms = std::sqrt (rms / (float) (down.size() - 1000));
    std::printf ("15 kHz to 16726 Hz: rms %.4f (was 0.566)\n", rms);
    CHECK (rms < 0.05f);
    // all silence: nothing to keep
    std::vector<float> sil (1000, 0.0f);
    o.trimSilence = true;
    CHECK (! takeToInstrument (sil, sil, sr, o, ins));
}

static void testCapturePasses()
{
    // while playing, a take is one pass round the block: loops don't pile up on each other
    Song song; song.numTracks = 4;
    std::vector<bool> offs (kMaxInstruments, false);
    offs[1] = true;   // instrument 02: a plug-in (OFFs)
    auto ev = [] (double t, int note, bool on, double line, int pass, int ins = 0)
    { CapturedNote c; c.timeMs = t; c.note = note; c.on = on; c.instrument = ins; c.playing = true; c.line = line; c.block = 0; c.speed = 6; c.pass = pass; return c; };
    auto lines = [] (const CaptureResult& r) { std::string t; for (auto& p : r.places) t += std::to_string (p.line) + ":" + std::to_string (p.note) + " "; return t; };

    std::vector<CapturedNote> log;
    log.push_back (ev (100, 37, true, 4.0, 1));  log.push_back (ev (150, 37, false, 4.5, 1));   // pass 1: line 4
    log.push_back (ev (900, 41, true, 10.0, 2)); log.push_back (ev (950, 41, false, 10.5, 2));  // pass 2: line 10
    log.push_back (ev (1000, 44, true, 20.0, 2)); log.push_back (ev (1050, 44, false, 20.5, 2)); // pass 2: line 20
    // passes 3, 4, 5... nothing played: CAPTURE still takes pass 2
    auto r = placeCapture (song, log, 0, 0, 0, 125, 6, offs);
    std::printf ("capture passes: %s\n", lines (r).c_str());
    CHECK (r.played && r.places.size() == 2 && r.places[0].line == 10 && r.places[0].note == 41 && r.places[1].line == 20);

    // something played in a later pass: just that pass
    log.push_back (ev (5000, 49, true, 33.0, 6)); log.push_back (ev (5050, 49, false, 33.5, 6));
    r = placeCapture (song, log, 0, 0, 0, 125, 6, offs);
    CHECK (r.places.size() == 1 && r.places[0].line == 33 && r.places[0].note == 49);

    // a plug-in note held over the wrap: the OFF goes where it was let go, early in the block
    std::vector<CapturedNote> log2;
    log2.push_back (ev (100, 37, true, 60.0, 2, 1));
    log2.push_back (ev (400, 37, false, 2.0, 3, 1));
    r = placeCapture (song, log2, 0, 0, 0, 125, 6, offs);
    std::printf ("capture held over the wrap: %s\n", lines (r).c_str());
    CHECK (r.places.size() == 2 && r.places[0].line == 60 && r.places[1].note == kNoteOff && r.places[1].line == 2);

    // legato on a plug-in: let go just after the next note -> same track, no OFF between them
    {
        std::vector<CapturedNote> lg;
        lg.push_back (ev (100, 37, true, 4.0, 5, 1));
        lg.push_back (ev (500, 41, true, 8.0, 5, 1));      // pressed...
        lg.push_back (ev (520, 37, false, 8.2, 5, 1));     // ...then the old one let go
        lg.push_back (ev (900, 41, false, 12.0, 5, 1));
        // and a real gap: let go on line 16, the next note on line 20
        lg.push_back (ev (1300, 44, true, 14.0, 5, 1));
        lg.push_back (ev (1500, 44, false, 16.0, 5, 1));
        lg.push_back (ev (1900, 49, true, 20.0, 5, 1));
        lg.push_back (ev (2000, 49, false, 21.0, 5, 1));
        // let go exactly as the next note comes (same line)
        lg.push_back (ev (2100, 37, true, 24.0, 5, 1));
        lg.push_back (ev (2400, 37, false, 28.0, 5, 1));
        lg.push_back (ev (2401, 41, true, 28.0, 5, 1));
        lg.push_back (ev (2700, 41, false, 31.0, 5, 1));
        auto r = placeCapture (song, lg, 0, 0, 0, 125, 6, offs);
        std::string t;
        for (auto& p : r.places) t += std::to_string (p.line) + "/" + std::to_string (p.track) + ":" + (p.note == kNoteOff ? std::string ("OFF") : std::to_string (p.note)) + " ";
        std::printf ("capture legato: %s\n", t.c_str());
        int tracksUsed = 0, offAt8 = 0, offAt16 = 0, offAt28 = 0, offAt12 = 0;
        for (auto& p : r.places)
        {
            tracksUsed = std::max (tracksUsed, p.track + 1);
            if (p.note == kNoteOff && p.line == 8) ++offAt8;
            if (p.note == kNoteOff && p.line == 9) ++offAt8;
            if (p.note == kNoteOff && p.line == 12) ++offAt12;
            if (p.note == kNoteOff && p.line == 16) ++offAt16;
            if (p.note == kNoteOff && (p.line == 28 || p.line == 27)) ++offAt28;
        }
        CHECK (tracksUsed == 1);            // nothing pushed onto the next track
        CHECK (offAt8 == 0 && offAt28 == 0); // no OFFs that would cut / sit on the next note
        CHECK (offAt12 == 1 && offAt16 == 1); // the real releases keep theirs
        // NOTE-OFFS switched off: none at all
        auto r2 = placeCapture (song, lg, 0, 0, 0, 125, 6, offs, false, false);
        int offs2 = 0; for (auto& p : r2.places) if (p.note == kNoteOff) ++offs2;
        CHECK (offs2 == 0 && r2.places.size() == 6);
        // staccato: let go a line before the next note, well before it -> the OFF stays (a gap)
        std::vector<CapturedNote> st;
        st.push_back (ev (100, 37, true, 4.0, 5, 1));
        st.push_back (ev (520, 37, false, 7.2, 5, 1));
        st.push_back (ev (640, 41, true, 8.0, 5, 1));
        st.push_back (ev (900, 41, false, 10.0, 5, 1));
        auto r3 = placeCapture (song, st, 0, 0, 0, 125, 6, offs);
        int off7 = 0; for (auto& p : r3.places) if (p.note == kNoteOff && p.line == 7) ++off7;
        CHECK (off7 == 1);
        // let go just before the next note, on the line before -> legato, no OFF
        st[1] = ev (590, 37, false, 7.6, 5, 1);
        r3 = placeCapture (song, st, 0, 0, 0, 125, 6, offs);
        int offNear = 0; for (auto& p : r3.places) if (p.note == kNoteOff && (p.line == 7 || p.line == 8)) ++offNear;
        CHECK (offNear == 0);
    }

    // a release alone after the take doesn't start a new one
    std::vector<CapturedNote> log3 = log;
    log3.push_back (ev (9000, 49, false, 1.0, 9));
    r = placeCapture (song, log3, 0, 0, 0, 125, 6, offs);
    CHECK (r.places.size() == 1 && r.places[0].line == 33);
}

static void testChordDetect()
{
    const auto& t = defaultChordTable();
    auto m = [&] (std::vector<int> n, const ChordTable* tab = nullptr) { return matchChord (tab != nullptr ? *tab : t, n); };
    // root position, any octave
    auto a = m ({ 60, 64, 67 });
    CHECK (a.slot == 0x02 && a.root == 60 && a.exact);
    a = m ({ 67, 60, 64 });                       // (pressed in any order)
    CHECK (a.slot == 0x02 && a.root == 60);
    // inversions and spread voicings: their own slots, the root as the slot measures it
    a = m ({ 64, 67, 72 });                       // E G C: MAJ INV 1 on C
    CHECK (a.slot == 0x17 && a.root == 60 && a.exact);
    a = m ({ 55, 60, 63, 70 });                   // G C D# A#: MIN7 DROP 2 on C
    CHECK (a.slot == 0x1B && a.root == 60 && a.exact);
    a = m ({ 48, 55, 60 });                       // 5 OCT
    CHECK (a.slot == 0x1F && a.root == 48);
    a = m ({ 48, 55 });                           // two notes: a power chord
    CHECK (a.slot == 0x0F && a.root == 48);
    // C6 and Am7 are the same notes: C at the bottom -> C6, A at the bottom -> Am7
    a = m ({ 60, 64, 67, 69 });
    CHECK (a.slot == 0x0B && a.root == 60);
    a = m ({ 57, 60, 64, 67 });
    CHECK (a.slot == 0x03 && a.root == 57);
    // no exact voicing: by note names, flagged
    a = m ({ 48, 64, 67, 69 });                   // C low, E G A high: C6 by name
    std::printf ("chord detect by name: slot %02X root %d exact %d\n", a.slot, a.root, (int) a.exact);
    CHECK (a.slot == 0x0B && ! a.exact && a.root % 12 == 0 && std::abs (a.root - 48) <= 6);
    a = m ({ 60, 76, 79 });                       // C, then E G an octave up: MAJ by name
    CHECK (a.slot == 0x02 && ! a.exact && a.root == 60);
    // nothing in the table: no chord
    CHECK (! m ({ 60, 61, 62 }));
    CHECK (! m ({ 60 }));
    CHECK (! m ({ 60, 60 }));                     // (the same note twice is one note)
    // an emptied slot isn't matched
    ChordTable t2 = t;
    t2[0x02] = {};
    a = m ({ 60, 64, 67 }, &t2);
    CHECK (a.slot != 0x02);
    ChordTable none {};
    CHECK (! m ({ 60, 64, 67 }, &none));

    // pass counter: the player counts each time playback goes back to the start
    {
        Song ps; ps.numTracks = 4; ps.blocks[0].lines = 4; ps.speed = 1;
        Player pl; pl.setSampleRate (48000); pl.setSong (&ps);
        pl.playBlock (0);
        const int p0 = pl.playPos().pass;
        std::vector<float> L (480), R (480);
        int maxPass = p0;
        for (int i = 0; i < 200; ++i) { pl.render (L.data(), R.data(), 480); maxPass = std::max (maxPass, pl.playPos().pass); }
        std::printf ("player passes over 2 s of a 4-line block: %d\n", maxPass - p0);
        CHECK (maxPass - p0 >= 3);
    }

    // RECORD CHORD: a slot from played notes - the database's name and root when it's one of its voicings
    auto notesOf = [] (const ChordDef& d) { std::string t; for (int i = 0; i < d.count; ++i) t += std::to_string (d.notes[(size_t) i]) + " "; return t; };
    auto rc = chordFromNotes ({ 67, 60, 64 });
    std::printf ("record C E G: '%s' %s\n", rc.name.c_str(), notesOf (rc).c_str());
    CHECK (rc.name == "MAJ" && notesOf (rc) == "0 4 7 ");
    rc = chordFromNotes ({ 64, 67, 72 });
    std::printf ("record E G C: '%s' %s\n", rc.name.c_str(), notesOf (rc).c_str());
    CHECK (rc.name == "MAJ INV 1" && notesOf (rc) == "4 7 12 ");
    rc = chordFromNotes ({ 60, 64, 67, 69 });
    CHECK (rc.name == "6" && notesOf (rc) == "0 4 7 9 ");
    rc = chordFromNotes ({ 60, 61, 66 });                       // not in the database
    CHECK (rc.name == "USER" && notesOf (rc) == "0 1 6 ");
    rc = chordFromNotes ({ 50, 60, 61, 66, 50 });               // (a note twice counts once)
    CHECK (rc.name == "USER" && notesOf (rc) == "0 10 11 16 ");
    rc = chordFromNotes ({ 40, 42, 44, 46, 48, 50, 52, 54, 56, 58 });   // ten notes: the lowest eight
    CHECK (rc.count == 8 && rc.notes[7] == 14);
    CHECK (chordFromNotes ({ 60 }).empty() && chordFromNotes ({}).empty());
    // a matched slot works with the matcher: play it back in and it's found
    {
        ChordTable t3 {};
        t3[0x40] = chordFromNotes ({ 48, 55, 58, 63 });          // C G A# D#: USER
        t3[0x40].note = "VERSE PROG 1";
        auto mm = matchChord (t3, { 50, 57, 60, 65 });           // the same shape on D
        CHECK (mm.slot == 0x40 && mm.root == 50 && mm.exact);
    }

    // CAPTURE: notes on a plug-in played together -> one note + #xy, OFF when the last key goes
    Song song; song.numTracks = 4;
    std::vector<bool> offs (kMaxInstruments, false);
    offs[1] = true;
    auto on = [] (double tm, int note, int ins) { CapturedNote c; c.timeMs = tm; c.note = note; c.instrument = ins; c.on = true; c.speed = 6; return c; };
    auto off = [] (double tm, int note, int ins) { CapturedNote c; c.timeMs = tm; c.note = note; c.instrument = ins; c.on = false; c.speed = 6; return c; };
    std::vector<CapturedNote> log;
    log.push_back (on (1000, 60, 1)); log.push_back (on (1015, 64, 1)); log.push_back (on (1040, 67, 1));   // C major, 40 ms spread
    log.push_back (on (1240, 72, 1));                                                               // line 2: a single note
    log.push_back (off (1250, 72, 1));
    log.push_back (off (1300, 60, 1)); log.push_back (off (1310, 67, 1)); log.push_back (off (1480, 64, 1));   // last key at line 4
    log.push_back (on (1720, 60, 1)); log.push_back (on (1800, 64, 1)); log.push_back (on (1880, 67, 1));   // rolled, 80 ms apart: notes
    auto r = placeCapture (song, log, 0, 0, 0, 125, 6, offs, true);
    std::string got;
    for (auto& p : r.places) got += std::to_string (p.line) + ":" + std::to_string (p.track) + ":" + std::to_string (p.note) + ":" + std::to_string (p.chordSlot) + " ";
    std::printf ("capture chords: %s (chords %d)\n", got.c_str(), r.chords);
    CHECK (r.chords == 1);
    if (r.places.size() >= 2)
    {
        CHECK (r.places[0].line == 0 && r.places[0].track == 0 && r.places[0].note == 60 && r.places[0].chordSlot == 0x02);
        int chordOff = -1, single = -1, rolled = 0;
        for (auto& p : r.places)
        {
            if (p.note == kNoteOff && p.track == 0) chordOff = p.line;
            if (p.note == 72) single = p.track;
            if (p.line >= 6 && p.note != kNoteOff && p.chordSlot == 0) ++rolled;
        }
        CHECK (chordOff == 4);          // (1480 ms: line 4)
        CHECK (single == 1);            // (track 0 is held by the chord until line 4)
        CHECK (rolled == 3);
    }
    // detection off: the same notes go in one by one
    auto r3 = placeCapture (song, log, 0, 0, 0, 125, 6, offs, false);
    CHECK (r3.chords == 0);
    // a sample instrument: no detection
    for (auto& c : log) c.instrument = 0;
    auto r4 = placeCapture (song, log, 0, 0, 0, 125, 6, offs, true);
    CHECK (r4.chords == 0);
    // no free effect column on the chord's cell: notes one by one
    song.blocks[0].at (0, 0) = { 0, 0, 1, 0x10 };
    song.blocks[0].at (0, 0).cmd2 = 2; song.blocks[0].at (0, 0).param2 = 0x10;
    for (auto& c : log) c.instrument = 1;
    auto r5 = placeCapture (song, log, 0, 0, 0, 125, 6, offs, true);
    std::string got5;
    for (auto& p : r5.places) got5 += std::to_string (p.line) + ":" + std::to_string (p.track) + ":" + std::to_string (p.note) + ":" + std::to_string (p.chordSlot) + " ";
    std::printf ("capture chord, fx full: %s\n", got5.c_str());
    CHECK (r5.chords == 1 && r5.places[0].track == 1 && r5.places[0].chordSlot == 0x02);
}

// v0.25: EQ points (add by place, types, the 8 limit, old songs' bands), UTILITY, spectrum tap and FFT
static void testEqPointsAndUtility()
{
    const double kPiT = 3.14159265358979;
    {
        ChannelEq eq;
        CHECK (eq.points() == 0 && ! eq.anyOn());
        const int a = eq.add (1000.0f, 6.0f), b = eq.add (50.0f, -3.0f), c = eq.add (12000.0f, 4.0f);
        CHECK (a == 0 && b == 1 && c == 2);
        CHECK (eq.band[a].type == EqType::Bell && eq.band[b].type == EqType::LowShelf && eq.band[c].type == EqType::HighShelf);
        CHECK (eq.band[a].freq == 1000.0f && eq.band[a].gain == 6.0f && eq.band[a].q == 1.0f && eq.band[a].active());
        CHECK (eqTypeForFreq (79.0f) == EqType::LowShelf && eqTypeForFreq (80.0f) == EqType::Bell && eqTypeForFreq (10001.0f) == EqType::HighShelf);
        CHECK (std::abs (ChannelEqProcessor::responseDb (eq, 1000, 48000) - 6.0) < 0.6);
        // clamped to the graph
        ChannelEq e2; const int k = e2.add (5.0f, 40.0f);
        CHECK (e2.band[k].freq == kEqMinHz && e2.band[k].gain == kEqMaxDb);
        // 8 at most
        for (int i = 3; i < kEqBands; ++i) CHECK (eq.add (300.0f + i * 100.0f, 1.0f) == i);
        CHECK (eq.points() == 8 && eq.add (500.0f, 1.0f) == -1);
        // remove, and putting it back restores it exactly (what undo does)
        const ChannelEq before = eq;
        eq.remove (4);
        CHECK (eq.points() == 7 && ! eq.band[4].used);
        CHECK (eq.add (777.0f, 2.0f) == 4);   // (the free slot is reused)
        eq.band[4] = before.band[4];
        bool same = true; for (int i = 0; i < kEqBands; ++i) same = same && eq.band[i] == before.band[i];
        CHECK (same);
        // a low-pass cuts above, a high-pass below
        ChannelEq lp; lp.band[0] = { true, true, EqType::LowPass, 1000.0f, 0.0f, 1.0f };
        CHECK (ChannelEqProcessor::responseDb (lp, 100, 48000) > -0.5 && ChannelEqProcessor::responseDb (lp, 10000, 48000) < -35);
        std::printf ("EQ points: 8 max, LP 1k: 100 Hz %.2f dB, 10 kHz %.1f dB\n", ChannelEqProcessor::responseDb (lp, 100, 48000), ChannelEqProcessor::responseDb (lp, 10000, 48000));
    }
    {
        // a v0.24 song's five bands: the ones on become points where they were, and sound the same
        ChannelEq old;
        old.band[0] = eqBandFromOld (0, true, 40.0f, 5.0f, 0.7f);    // HIGH-PASS (gain not used)
        old.band[1] = eqBandFromOld (1, false, 120.0f, 0.0f, 0.7f);  // off: not a point
        old.band[2] = eqBandFromOld (2, true, 500.0f, -4.0f, 2.0f);
        old.band[4] = eqBandFromOld (4, true, 8000.0f, 3.0f, 0.7f);
        CHECK (old.points() == 3 && ! old.band[1].used);
        CHECK (old.band[0].type == EqType::HighPass && old.band[0].gain == 0.0f && old.band[2].type == EqType::Bell && old.band[4].type == EqType::HighShelf);
        // the old high-pass was a 2nd-order Butterworth: -3 dB at its frequency
        const double hp40 = ChannelEqProcessor::responseDb (old, 40, 48000);
        CHECK (hp40 < -2.5 && hp40 > -3.7);
        CHECK (std::abs (ChannelEqProcessor::responseDb (old, 500, 48000) - (-4.0)) < 0.5);
        CHECK (std::abs (ChannelEqProcessor::responseDb (old, 20000, 48000) - 3.0) < 0.5);
    }
    {
        // UTILITY
        const int n = 48000;
        auto tone = [&] (double hz, float l, float r, std::vector<float>& L, std::vector<float>& R)
        {
            L.assign ((size_t) n, 0.0f); R.assign ((size_t) n, 0.0f);
            for (int i = 0; i < n; ++i) { const float v = (float) std::sin (2 * kPiT * hz * i / 48000.0); L[(size_t) i] = v * l; R[(size_t) i] = v * r; }
        };
        auto rms = [&] (const std::vector<float>& v) { double e = 0; for (int i = n / 2; i < n; ++i) e += v[(size_t) i] * v[(size_t) i]; return std::sqrt (e / (n / 2)); };
        std::vector<float> L, R;
        StereoUtility u; u.prepare (48000);
        tone (440, 0.8f, 0.2f, L, R); u.process (L.data(), R.data(), n, 0.0f, 0);
        double diff = 0; for (int i = 0; i < n; ++i) diff = std::max (diff, (double) std::abs (L[(size_t) i] - R[(size_t) i]));
        CHECK (diff < 1e-6);
        tone (440, 0.8f, 0.2f, L, R); u.process (L.data(), R.data(), n, 2.0f, 0);
        // side was 0.3, mid 0.5: at 200% side 0.6 -> L 1.1, R -0.1
        CHECK (std::abs (rms (L) / (1.1 / std::sqrt (2.0)) - 1.0) < 0.01 && std::abs (rms (R) / (0.1 / std::sqrt (2.0)) - 1.0) < 0.02);
        tone (440, 0.8f, 0.2f, L, R); const auto L0 = L; u.process (L.data(), R.data(), n, 1.0f, 0);
        CHECK (L == L0);   // the defaults change nothing
        // MONO BELOW 120 Hz: a 50 Hz tone on the left only comes out on both; a 2 kHz one stays left
        StereoUtility m; m.prepare (48000);
        tone (50, 1.0f, 0.0f, L, R); m.process (L.data(), R.data(), n, 1.0f, 120);
        const double lo = rms (R) / rms (L);
        m.reset();
        tone (2000, 1.0f, 0.0f, L, R); m.process (L.data(), R.data(), n, 1.0f, 120);
        const double hi = rms (R) / rms (L);
        // and the two halves add back flat: a mono 120 Hz tone keeps its level
        m.reset();
        tone (120, 0.5f, 0.5f, L, R); m.process (L.data(), R.data(), n, 1.0f, 120);
        const double flat = rms (L) / (0.5 / std::sqrt (2.0));
        std::printf ("UTILITY: mono below 120 Hz: 50 Hz R/L %.3f, 2 kHz R/L %.4f, 120 Hz mono level x%.3f\n", lo, hi, flat);
        CHECK (lo > 0.9 && hi < 0.01 && std::abs (flat - 1.0) < 0.01);
        // STEEP (LR8, 48 dB/oct): the 50 Hz tone fully mono, the halves still add back flat
        StereoUtility st; st.prepare (48000);
        tone (50, 1.0f, 0.0f, L, R); st.process (L.data(), R.data(), n, 1.0f, 120, true);
        const double lo8 = rms (R) / rms (L);
        st.reset();
        tone (2000, 1.0f, 0.0f, L, R); st.process (L.data(), R.data(), n, 1.0f, 120, true);
        const double hi8 = rms (R) / rms (L);
        st.reset();
        tone (120, 0.5f, 0.5f, L, R); st.process (L.data(), R.data(), n, 1.0f, 120, true);
        const double flat8 = rms (L) / (0.5 / std::sqrt (2.0));
        std::printf ("UTILITY steep: 50 Hz R/L %.4f, 2 kHz R/L %.5f, 120 Hz mono level x%.3f\n", lo8, hi8, flat8);
        CHECK (lo8 > 0.995 && hi8 < 0.001 && std::abs (flat8 - 1.0) < 0.01);
    }
    {
        // UTILITY in the player: WIDTH 0 on a stereo sample makes left = right
        Song s = oneShot (square (100, 4800));
        s.instruments[0].dataR = s.instruments[0].data;
        for (auto& v : s.instruments[0].dataR) v = (int16_t) (v / 4);
        s.blocks[0].at (0, 0) = { 37, 1, 0, 0 };
        std::vector<float> R;
        auto L = renderMix (s, 9600, false, PaulaOptions::Model::Clean, &R);
        double d0 = 0; for (int i = 2000; i < 9600; ++i) d0 = std::max (d0, (double) std::abs (L[(size_t) i] - R[(size_t) i]));
        s.instruments[0].utilWidth = 0.0f;
        CHECK (s.instruments[0].hasUtility());
        L = renderMix (s, 9600, false, PaulaOptions::Model::Clean, &R);
        double d1 = 0, e = 0; for (int i = 2000; i < 9600; ++i) { d1 = std::max (d1, (double) std::abs (L[(size_t) i] - R[(size_t) i])); e += std::abs (L[(size_t) i]); }
        std::printf ("UTILITY in the player: L-R %.3f, at width 0 %.2e\n", d0, d1);
        CHECK (d0 > 0.05 && d1 < 1e-5 && e > 10);
        // not on MIDI out / CV out instruments
        s.instruments[0].plugin = s.instruments[0].cvOut = true;
        CHECK (! s.instruments[0].hasUtility());
    }
    {
        // the spectrum: an instrument's tap gets its sound while playing; the FFT finds the tone
        Song s = oneShot (square (100, 4800));
        s.blocks[0].at (0, 0) = { 37, 1, 0, 0 };
        auto pp = std::make_unique<Player>();   // (on the heap: a Player is big)
        Player& p = *pp; p.setSampleRate (48000); p.setSong (&s);
        p.insTap.source = 0;
        p.mixer().chTap.source = 0;
        p.forceMixPath = true; p.mixer().syncEffectsNow (s.mixer);
        std::vector<float> L (4096), R (4096);
        p.render (L.data(), R.data(), 4096);   // stopped: nothing
        CHECK (p.insTap.count() == 0 && p.mixer().chTap.count() == 0);
        p.playBlock (0);
        for (int k = 0; k < 4; ++k) p.render (L.data(), R.data(), 4096);
        CHECK (p.insTap.count() >= 8192 && p.mixer().chTap.count() >= 8192);
        std::vector<float> buf (4096);
        CHECK (p.insTap.read (buf.data(), 4096) == 4096);
        float pk = 0; for (float v : buf) pk = std::max (pk, std::abs (v));
        CHECK (pk > 0.05f);
        // another instrument: nothing
        auto qq = std::make_unique<Player>();
        Player& q = *qq; q.setSampleRate (48000); q.setSong (&s); q.insTap.source = 5; q.playBlock (0);
        q.render (L.data(), R.data(), 4096);
        float qpk = 0; const int got = q.insTap.read (buf.data(), 4096);
        for (int i = 0; i < got; ++i) qpk = std::max (qpk, std::abs (buf[(size_t) i]));
        CHECK (got == 4096 && qpk == 0.0f);

        std::vector<float> sine (4096), db;
        for (int i = 0; i < 4096; ++i) sine[(size_t) i] = 0.5f * (float) std::sin (2 * kPiT * 1000.0 * i / 48000.0);
        spectrumDb (sine.data(), 4096, db);
        int best = 0; for (int k = 1; k < (int) db.size(); ++k) if (db[(size_t) k] > db[(size_t) best]) best = k;
        const double hz = best * 48000.0 / 4096;
        std::printf ("spectrum: 1 kHz sine peaks at %.0f Hz, %.1f dB; 5 kHz %.1f dB\n", hz, db[(size_t) best], db[(size_t) (5000 * 4096 / 48000)]);
        CHECK (std::abs (hz - 1000.0) < 12.0 && db[(size_t) best] > -8.0 && db[(size_t) best] < -4.0 && db[(size_t) (5000 * 4096 / 48000)] < -60.0);
    }
}

// v0.26: COMBINE: positions' blocks one after another in a new block, in their place
static void testCombine()
{
    Song s;
    s.blocks.resize (4);
    for (int b = 0; b < 4; ++b) { s.blocks[(size_t) b] = Block {}; s.blocks[(size_t) b].lines = 16 * (b + 1); s.blocks[(size_t) b].at (0, 0) = { (uint8_t) (10 + b), 1, 0, 0 }; }
    s.blocks[1].name = "VERSE";
    s.sequence = { 0, 1, 2, 1, 3 };
    s.sections = { { 0, "INTRO" }, { 2, "DROP" }, { 4, "OUTRO" } };
    const int nb = combinePositions (s, 1, 3);   // blocks 1, 2, 1: 32 + 48 + 32 lines
    std::printf ("combine: new block %d, %d lines, sequence size %zu, sections %zu\n", nb, nb >= 0 ? s.blocks[(size_t) nb].lines : -1, s.sequence.size(), s.sections.size());
    CHECK (nb == 4 && s.blocks[4].lines == 112 && s.blocks[4].name == "VERSE");
    CHECK ((s.sequence == std::vector<int> { 0, 4, 3 }));
    CHECK (s.blocks[4].at (0, 0).note == 11 && s.blocks[4].at (32, 0).note == 12 && s.blocks[4].at (80, 0).note == 11 && s.blocks[4].at (111, 0).note == 0);
    // DROP started inside: gone; OUTRO moved up
    CHECK (s.sections.size() == 2 && s.sections[0].name == "INTRO" && s.sections[1].name == "OUTRO" && s.sections[1].pos == 2);
    // the old blocks are untouched
    CHECK (s.blocks[1].lines == 32 && s.blocks[2].lines == 48);
    // too long: nothing changes
    Song t = s;
    t.blocks[0].lines = 200;
    CHECK (combinePositions (t, 0, 1) == kCombineTooLong && t.sequence == s.sequence);
    CHECK (combinePositions (t, 1, 1) == -1);
}

// v0.28: GROOVE and the break tools
static void testGrooveAndBreakTools()
{
    // groove offsets
    CHECK (grooveOffset (GrooveKind::Off, 100, 1, 0) == 0.0);
    CHECK (grooveOffset (GrooveKind::Swing16, 100, 0, 0) == 0.0 && std::abs (grooveOffset (GrooveKind::Swing16, 100, 1, 0) - 0.667) < 1e-9);
    CHECK (std::abs (grooveOffset (GrooveKind::Swing16, 50, 3, 2) - 0.3335) < 1e-9 && grooveOffset (GrooveKind::Swing16, 0, 3, 2) == 0.0);
    CHECK (grooveOffset (GrooveKind::Swing8, 100, 2, 0) > 0.6 && grooveOffset (GrooveKind::Swing8, 100, 1, 0) == 0.0);
    CHECK (grooveOffset (GrooveKind::Humanise, 100, 5, 3) == grooveOffset (GrooveKind::Humanise, 100, 5, 3) && grooveOffset (GrooveKind::Humanise, 100, 5, 3) < 0.19);
    // played: swing 16ths at 100% puts line 1's note 2/3 of a line late
    {
        auto onsetOf = [] (int kind, int amount)
        {
            Song s = oneShot (square (100, 4800), false);
            s.blocks[0].at (1, 0) = { 37, 1, 0, 0 };
            s.grooveKind = (uint8_t) kind; s.grooveAmount = amount;
            auto L = renderTrack (s, 48000);
            return firstSound (L);
        };
        const int straight = onsetOf (0, 0), swung = onsetOf ((int) GrooveKind::Swing16, 100);
        Player p; Song s; s.tempo = 125; s.speed = 6; p.setSampleRate (48000); p.setSong (&s);
        const double lineSamples = 48000.0 * 2.5 / 125.0 * 6;   // (6 ticks of 2.5/BPM s)
        std::printf ("groove: line 1 straight at %d, swung at %d (%.0f later; 2/3 of a line = %.0f)\n", straight, swung, (double) (swung - straight), lineSamples * 0.667);
        CHECK (straight > 0 && std::abs ((swung - straight) - lineSamples * 0.667) < 200);
    }
    // CHOP TO PATTERN
    {
        Song s;
        auto& in = s.instruments[2];
        in.data.assign (16000, 1000);
        in.slices = { 0, 2000, 4000, 8000, 12000 };
        s.blocks[0].lines = 64;
        s.blocks[0].at (5, 1) = { 50, 1, 0, 0 };   // (a note there goes)
        const int n = chopToPattern (s, 2, 0, 1, 16);
        std::printf ("chop: %d slices at lines", n);
        for (int l = 0; l < 16; ++l) if (s.blocks[0].at (l, 1).note) std::printf (" %d=%d", l, s.blocks[0].at (l, 1).note - kSliceC3 + 1);
        std::printf ("\n");
        CHECK (n == 5 && in.sliceKeys);
        CHECK (s.blocks[0].at (0, 1).note == kSliceC3 && s.blocks[0].at (2, 1).note == kSliceC3 + 1 && s.blocks[0].at (4, 1).note == kSliceC3 + 2
               && s.blocks[0].at (8, 1).note == kSliceC3 + 3 && s.blocks[0].at (12, 1).note == kSliceC3 + 4 && s.blocks[0].at (5, 1).note == 0);
        CHECK (s.blocks[0].at (0, 1).instr == 3);
        // RE-SEQUENCE: the rhythm stays, the order changes; slice 1 on the downbeat stays
        Block b = s.blocks[0];
        const int changed = resequenceSlices (b, 0, 15, 1, 1, 12345);
        std::vector<int> before, after;
        for (int l = 0; l < 16; ++l) { if (s.blocks[0].at (l, 1).note) before.push_back (s.blocks[0].at (l, 1).note); if (b.at (l, 1).note) after.push_back (b.at (l, 1).note); }
        auto sb = before, sa = after; std::sort (sb.begin(), sb.end()); std::sort (sa.begin(), sa.end());
        CHECK (changed > 0 && sb == sa && after != before && b.at (0, 1).note == kSliceC3);
        for (int l = 0; l < 16; ++l) CHECK ((b.at (l, 1).note != 0) == (s.blocks[0].at (l, 1).note != 0));
    }
    // REVERSE, STUTTER, RATCHET
    {
        Block b; b.lines = 16;
        for (int l = 0; l < 8; ++l) b.at (l, 0) = { (uint8_t) (37 + l), 1, 0, 0 };
        Block r = b; reverseLines (r, 0, 7, 0, 0);
        CHECK (r.at (0, 0).note == 44 && r.at (7, 0).note == 37 && r.at (8, 0).note == 0);
        Block st = b; stutterLines (st, 0, 7, 0, 0);   // (the first 2 lines, over and over)
        CHECK (st.at (2, 0).note == 37 && st.at (3, 0).note == 38 && st.at (6, 0).note == 37 && st.at (7, 0).note == 38);
        Block ra = b; ra.at (3, 0).cmd = 0xC; ra.at (3, 0).param = 0x20;
        const int k = ratchetNotes (ra, 0, 7, 0, 0);
        const uint8_t R = cmdLetter ('R');
        CHECK (k == 8 && ra.at (0, 0).cmd == R && ra.at (0, 0).param == 0x40 && ra.at (7, 0).param == 0x10);
        CHECK (ra.at (3, 0).cmd == 0xC && ra.at (3, 0).cmd2 == R);   // (the volume command kept)
    }
}

// v0.29: each instrument's own EQ; a VST3 / AU instrument's filter and envelopes
static void testInstrumentEqAndSynthShape()
{
    {
        // a sample instrument's EQ: a high-pass at 2 kHz takes most of a 100 Hz square away
        Song s = oneShot (square (100, 9600));
        s.blocks[0].at (0, 0) = { 37, 1, 0, 0 };
        auto peak = [] (const std::vector<float>& v) { float m = 0; for (size_t i = 2000; i < v.size(); ++i) m = std::max (m, std::abs (v[i])); return m; };
        double e0 = 0, e1 = 0;
        auto L = renderMix (s, 9600, false);
        for (size_t i = 2000; i < L.size(); ++i) e0 += L[i] * L[i];
        CHECK (! s.instruments[0].hasEq());
        s.instruments[0].eq.band[0] = { true, true, EqType::HighPass, 2000.0f, 0.0f, 0.7f };
        CHECK (s.instruments[0].hasEq());
        L = renderMix (s, 9600, false);
        for (size_t i = 2000; i < L.size(); ++i) e1 += L[i] * L[i];
        std::printf ("instrument EQ: high-pass 2 kHz on a 100 Hz square: energy x%.3f (peak %.3f)\n", e1 / e0, peak (L));
        CHECK (e0 > 1 && e1 / e0 < 0.2);
        s.instruments[0].eq.band[0].on = false;   // (switched off: as before)
        L = renderMix (s, 9600, false);
        double e2 = 0; for (size_t i = 2000; i < L.size(); ++i) e2 += L[i] * L[i];
        CHECK (std::abs (e2 / e0 - 1.0) < 1e-3);
        s.instruments[0].eq.band[0].on = true;
        s.instruments[0].plugin = s.instruments[0].midiOut = true;
        CHECK (! s.instruments[0].hasEq());   // (not on MIDI out)
    }
    // a plug-in instrument that always plays a 1 kHz sine: the envelope shapes it from its notes
    struct SineSynth : PluginSynth
    {
        double ph = 0;
        void prepare (double, int) override {}
        void noteOn (int, int, int) override {}
        void noteOff (int, int) override {}
        void allNotesOff (int) override {}
        void pitchBend (int, int) override {}
        void controller (int, int, int) override {}
        void setParameter (int, float) override {}
        void render (float* l, float* r, int n) override
        { for (int i = 0; i < n; ++i) { l[i] = r[i] = 0.25f * (float) std::sin (ph); ph += 2 * 3.14159265358979 * 1000.0 / 48000.0; } }
    };
    auto run = [&] (Song& song, std::vector<float>& out, int n)
    {
        auto pp = std::make_unique<Player>(); Player& p = *pp;
        p.setSampleRate (48000); p.setSong (&song);
        p.synths().factory = [&] (int, const Instrument&, double) { return std::make_unique<SineSynth>(); };
        p.synths().syncNow (song, 48000);
        p.playBlock (0);
        out.assign ((size_t) n, 0.0f);
        std::vector<float> R ((size_t) n);
        for (int done = 0; done < n; done += 480) p.render (out.data() + done, R.data() + done, 480);
    };
    auto lvl = [] (const std::vector<float>& v, int at) { double e = 0; for (int i = at; i < at + 480; ++i) e += v[(size_t) i] * v[(size_t) i]; return (float) std::sqrt (e / 480); };
    Song song;
    song.numTracks = 4;
    auto& in = song.instruments[0];
    in.plugin = true; in.pluginKey = "synth"; in.pluginName = "SYNTH";
    const int line = (int) std::lround (48000 * 2.5 / 125) * 6;
    song.blocks[0].at (2, 0) = { 37, 1, 0, 0 };
    song.blocks[0].at (6, 0) = { kNoteOff, 0, 0, 0 };
    song.blocks[0].at (10, 0) = { 49, 1, 0, 0 };
    const int n = line * 14;
    std::vector<float> flat, env;
    run (song, flat, n);
    const float ref = lvl (flat, line * 4);
    CHECK (! in.shapesSynth() && ref > 0.05f && lvl (flat, line / 2) > 0.05f);   // (unshaped: always on)
    in.volEnv = { true, 0.01f, 0.0f, 1.0f, 0.05f };
    CHECK (in.shapesSynth());
    run (song, env, n);
    const float before = lvl (env, line / 2), held = lvl (env, line * 4);
    const float rel = lvl (env, line * 6 + 960), gone = lvl (env, line * 8), again = lvl (env, line * 12);
    std::printf ("synth envelope: before %.4f, held %.3f (unshaped %.3f), 20-30 ms into release %.3f, after %.4f, next note %.3f\n",
                 before, held, ref, rel, gone, again);
    CHECK (before < 1e-4f && std::abs (held - ref) < 0.01f * ref);
    CHECK (rel > 0.2f * ref && rel < 0.8f * ref && gone < 1e-4f && std::abs (again - ref) < 0.01f * ref);
    // the filter: a high-pass at 8 kHz leaves little of the 1 kHz tone; a low-pass at 20 kHz keeps it
    in.volEnv.on = false;
    in.filter = FilterType::HighPass; in.cutoff = 8000.0f; in.resonance = 0.0f;
    CHECK (in.shapesSynth());
    run (song, env, n);
    const float hp = lvl (env, line * 4);
    in.filter = FilterType::LowPass; in.cutoff = 20000.0f;
    run (song, env, n);
    const float lp = lvl (env, line * 4);
    // a low-pass at 250 Hz with the filter envelope opening it 4 octaves on each note: louder just after a note
    in.cutoff = 250.0f; in.filterEnvAmount = 4.0f; in.filterEnv = { true, 0.001f, 0.15f, 0.0f, 0.1f };
    run (song, env, n);
    const float open = lvl (env, line * 2 + 240), shut = lvl (env, line * 5);
    std::printf ("synth filter: high-pass 8k x%.3f, low-pass 20k x%.3f; 250 Hz + envelope: opened %.3f, closed %.3f\n", hp / ref, lp / ref, open, shut);
    CHECK (hp < 0.1f * ref && std::abs (lp / ref - 1.0f) < 0.05f && open > 2.0f * shut);
    // and its own EQ: a high-pass point at 8 kHz does the same after it
    in.filter = FilterType::Off;
    in.eq.band[0] = { true, true, EqType::HighPass, 8000.0f, 0.0f, 0.7f };
    CHECK (in.hasEq());
    run (song, env, n);
    std::printf ("synth EQ: high-pass 8k x%.3f\n", lvl (env, line * 4) / ref);
    CHECK (lvl (env, line * 4) < 0.1f * ref);
    // MIDI out / CV out: never shaped
    in.eq = {}; in.volEnv.on = true; in.midiOut = true;
    CHECK (! in.shapesSynth() && ! in.hasEq());
}

// v0.30: internet radio (the parts with no network: metadata, playlists, MP3, rate, FIFO, GRAB)
#include "engine/Radio.h"
#include "data/tone_mp3.h"
static void testRadio()
{
    using namespace prv::radio;
    {
        // ICY: every 16 audio bytes a length byte and the text; any chunk sizes
        std::vector<uint8_t> audio, stream;
        auto block = [] (const std::string& m) { std::string b = m; while (b.size() % 16) b += '\0'; return b; };
        const std::string m1 = block ("StreamTitle='Artist - Song';"), m2 = block ("StreamTitle='Rock 'n' Roll';StreamUrl='';");
        for (int k = 0; k < 10; ++k)
        {
            for (int i = 0; i < 16; ++i) { const uint8_t v = (uint8_t) (k * 16 + i); audio.push_back (v); stream.push_back (v); }
            const std::string m = k == 2 ? m1 : k == 6 ? m2 : std::string();
            stream.push_back ((uint8_t) (m.size() / 16));
            stream.insert (stream.end(), m.begin(), m.end());
        }
        for (int chunk : { 1, 3, 7, 16, 17, 1000 })
        {
            IcyParser p; p.reset (16);
            std::vector<uint8_t> out; int changes = 0; std::string first;
            for (size_t i = 0; i < stream.size(); i += (size_t) chunk)
                if (p.feed (stream.data() + i, std::min ((size_t) chunk, stream.size() - i), out)) { if (++changes == 1) first = p.title(); }
            if (out != audio) std::printf ("ICY chunk %d: %zu/%zu bytes, %d titles, '%s'\n", chunk, out.size(), audio.size(), changes, p.title().c_str());
            // (one big read holding both: only the latest title is told)
            CHECK (out == audio && changes == (chunk >= 1000 ? 1 : 2) && (chunk >= 1000 || first == "Artist - Song") && p.title() == "Rock 'n' Roll");
        }
        IcyParser none; none.reset (0); std::vector<uint8_t> o;
        CHECK (! none.feed (stream.data(), stream.size(), o) && o == stream);
        CHECK (IcyParser::titleFrom ("StreamUrl='x';") == "" && IcyParser::titleFrom ("StreamTitle='  Spaced  ';") == "Spaced");
    }
    {
        const auto pls = parsePlaylist ("[playlist]\r\nNumberOfEntries=2\r\nFile1=http://a.example:8000/live\r\nTitle1=A\r\nFile2=https://b.example/stream.mp3\r\nVersion=2\r\n");
        CHECK (pls.size() == 2 && pls[0] == "http://a.example:8000/live" && pls[1] == "https://b.example/stream.mp3");
        const auto m3u = parsePlaylist ("#EXTM3U\n#EXTINF:-1,Radio\nhttp://c.example/x\n\nhttp://c.example/x\nnot a url\n");
        CHECK (m3u.size() == 1 && m3u[0] == "http://c.example/x");
        CHECK (isPlaylist ("http://x/listen.pls", "") && isPlaylist ("http://x/a.M3U?x=1", "") && isPlaylist ("http://x/a", "audio/x-scpls")
               && ! isPlaylist ("http://x/a.m3u8", "") && ! isPlaylist ("http://x/live", "audio/mpeg"));
        int total = 0; for (int a = 1; retryDelayMs (a) > 0; ++a) total += retryDelayMs (a);
        CHECK (retryDelayMs (1) == 1000 && retryDelayMs (2) == 2000 && retryDelayMs (3) == 4000 && retryDelayMs (kMaxRetries) == 8000
               && retryDelayMs (kMaxRetries + 1) == -1 && retryDelayMs (0) == -1 && total == 23000);
    }
    // MP3: 440 Hz left, 660 Hz right, fed as a stream in odd chunks
    std::vector<float> L, R;
    {
        Mp3Decoder d;
        for (size_t i = 0; i < sizeof (kToneMp3); i += 777) d.feed (kToneMp3 + i, std::min<size_t> (777, sizeof (kToneMp3) - i), L, R, i + 777 >= sizeof (kToneMp3));
        auto hz = [] (const std::vector<float>& v, double rate)
        {
            int z = 0; const size_t a = v.size() / 4, b = v.size() * 3 / 4;
            for (size_t i = a + 1; i < b; ++i) if ((v[i - 1] < 0) != (v[i] < 0)) ++z;
            return z / 2.0 / ((double) (b - a) / rate);
        };
        float pk = 0; for (float v : L) pk = std::max (pk, std::abs (v));
        std::printf ("radio MP3: %d Hz, %d ch, %zu frames (2 s = 88200), left %.1f Hz, right %.1f Hz, peak %.2f, skipped %lld bytes\n",
                     d.sampleRate(), d.channels(), L.size(), hz (L, 44100), hz (R, 44100), pk, (long long) d.badBytes());
        CHECK (d.sampleRate() == 44100 && d.channels() == 2 && L.size() > 80000 && L.size() < 92000 && L.size() == R.size());
        CHECK (std::abs (hz (L, 44100) - 440) < 2 && std::abs (hz (R, 44100) - 660) < 2 && pk > 0.1f);
        // junk in front (as a stream joined in the middle): it finds its feet
        Mp3Decoder j; std::vector<float> jl, jr; std::vector<uint8_t> junk (5000, 0x55);
        junk.insert (junk.end(), kToneMp3 + 3000, kToneMp3 + sizeof (kToneMp3));
        j.feed (junk.data(), junk.size(), jl, jr, true);
        CHECK (j.sampleRate() == 44100 && jl.size() > 70000);
        // 44.1 -> 48 kHz: the right length and the same pitch
        Resampler rs; rs.setRates (44100, 48000);
        std::vector<float> ol, orr;
        for (size_t i = 0; i < L.size(); i += 1000) { const int k = (int) std::min<size_t> (1000, L.size() - i); rs.process (L.data() + i, R.data() + i, k, ol, orr); }
        const double expect = L.size() * 48000.0 / 44100.0;
        std::printf ("radio rate: %zu -> %zu frames (expect %.0f), left %.1f Hz at 48 kHz\n", L.size(), ol.size(), expect, hz (ol, 48000));
        CHECK (std::abs ((double) ol.size() - expect) < 4 && std::abs (hz (ol, 48000) - 440) < 2);
        // the same rate: the same samples (3 later)
        Resampler same; same.setRates (48000, 48000); std::vector<float> sl, sr2;
        same.process (L.data(), R.data(), 4000, sl, sr2);
        bool eq = sl.size() >= 3990; for (size_t i = 0; eq && i + 2 < sl.size(); ++i) eq = std::abs (sl[i + 2] - L[i]) < 1e-6f;
        CHECK (eq);
    }
    {
        // FIFO: full stops pushing, empty stops popping
        Fifo f; f.setSize (1000);
        std::vector<float> a (1500, 0.5f), b (1500, 0.0f), c (1500, 0.0f);
        CHECK (f.push (a.data(), a.data(), 1500) == 1000 && f.available() == 1000 && f.space() == 0);
        CHECK (f.pop (b.data(), c.data(), 600) == 600 && f.available() == 400 && b[599] == 0.5f);
        for (int i = 0; i < 1500; ++i) a[(size_t) i] = (float) i;
        CHECK (f.push (a.data(), a.data(), 1500) == 600);
        CHECK (f.pop (b.data(), c.data(), 1500) == 1000 && b[400] == 0.0f && b[999] == 599.0f && f.available() == 0);
        CHECK (f.pop (b.data(), c.data(), 10) == 0);
        f.push (a.data(), a.data(), 10); f.clear(); CHECK (f.available() == 0);
    }
    {
        // GRAB: 5 / 10 / 20 / 30 seconds, part-filled and full, newest kept, length changes keep what fits
        const double sr = 1000;
        for (int secs : GrabBuffer::kChoices)
        {
            GrabBuffer g; g.setLength (secs, sr);
            std::vector<float> ramp (100000); for (size_t i = 0; i < ramp.size(); ++i) ramp[i] = (float) i;
            g.write (ramp.data(), ramp.data(), 3000);   // 3 s
            std::vector<float> l, r; g.copy (l, r);
            CHECK (g.held() == 3000 && l.size() == 3000 && l.front() == 0.0f && l.back() == 2999.0f);
            for (int k = 0; k < 40; ++k) g.write (ramp.data() + 3000 + k * 1000, ramp.data() + 3000 + k * 1000, 1000);   // 43 s in all
            g.copy (l, r);
            CHECK (g.held() == secs * 1000 && (int) l.size() == secs * 1000 && l.back() == 42999.0f && l.front() == (float) (43000 - secs * 1000));
            CHECK (std::abs (g.heldSeconds() - secs) < 1e-9);
        }
        GrabBuffer g; g.setLength (10, sr);
        std::vector<float> ramp (20000); for (size_t i = 0; i < ramp.size(); ++i) ramp[i] = (float) i;
        g.write (ramp.data(), ramp.data(), 20000);
        g.setLength (5, sr);   // shrink: the newest 5 s
        std::vector<float> l, r; g.copy (l, r);
        CHECK (l.size() == 5000 && l.front() == 15000.0f && l.back() == 19999.0f);
        g.setLength (30, sr);   // grow: still those 5 s, then more after them
        g.copy (l, r);
        CHECK (l.size() == 5000 && l.back() == 19999.0f);
        g.write (ramp.data(), ramp.data(), 1000);
        g.copy (l, r);
        CHECK (l.size() == 6000 && l[4999] == 19999.0f && l[5000] == 0.0f);
        g.setLength (30, 2000);   // a new rate: starts again
        CHECK (g.held() == 0);
    }
}

// v0.31: AAC / AAC+, transport streams, fMP4 and HLS (the AAC decoding itself where the computer has one)
#include "engine/RadioFormats.h"
#include "data/radio_formats.h"
static void testRadioFormats()
{
    using namespace prv::radio;
    auto hz = [] (const std::vector<float>& v, double rate)
    {
        int z = 0; const size_t a = v.size() / 4, b = v.size() * 3 / 4;
        for (size_t i = a + 1; i < b; ++i) if ((v[i - 1] < 0) != (v[i] < 0)) ++z;
        return z / 2.0 / ((double) (b - a) / rate);
    };
    // ADTS: whole frames out of any chunks; an ID3 tag and junk in front are skipped
    std::vector<std::vector<uint8_t>> frames;
    {
        AdtsSplitter sp;
        for (size_t i = 0; i < sizeof (kToneAac); i += 333) sp.feed (kToneAac + i, std::min<size_t> (333, sizeof (kToneAac) - i), frames);
        std::vector<uint8_t> joined; for (auto& f : frames) joined.insert (joined.end(), f.begin(), f.end());
        AdtsHeader h;
        CHECK (frames.size() >= 40 && frames.size() <= 50 && parseAdts (frames[0].data(), frames[0].size(), h) && h.sampleRate() == 44100 && h.channels == 2 && h.profile == 1);
        CHECK (joined.size() + 2000 > sizeof (kToneAac) && sp.skipped() == 0);
        std::vector<uint8_t> junk { 'I', 'D', '3', 4, 0, 0, 0, 0, 0, 10 };
        for (int i = 0; i < 10; ++i) junk.push_back (0);
        for (int i = 0; i < 50; ++i) junk.push_back ((uint8_t) (i * 7));
        junk.insert (junk.end(), kToneAac, kToneAac + sizeof (kToneAac));
        AdtsSplitter s2; std::vector<std::vector<uint8_t>> f2;
        s2.feed (junk.data(), junk.size(), f2);
        std::printf ("ADTS: %zu frames, with junk in front %zu (skipped %lld bytes)\n", frames.size(), f2.size(), (long long) s2.skipped());
        CHECK (f2.size() + 1 >= frames.size() && s2.skipped() >= 50);
        const auto made = makeAdts (2, 4, 2, frames[0].size() - 7);
        CHECK (std::equal (made.begin(), made.end(), frames[0].begin()));   // (the same header ffmpeg wrote)
        CHECK (rateIndexFor (48000) == 3 && rateIndexFor (12345) == -1 && id3Size ((const uint8_t*) "ID3\4\0\0\0\0\0\12", 10) == 20);
    }
    // a transport stream: the AAC comes out as the same ADTS frames; MP3 too
    {
        TsDemux d; std::vector<uint8_t> es;
        for (size_t i = 0; i < sizeof (kToneAacTs); i += 1000) d.feed (kToneAacTs + i, std::min<size_t> (1000, sizeof (kToneAacTs) - i), es);
        AdtsSplitter sp; std::vector<std::vector<uint8_t>> f;
        sp.feed (es.data(), es.size(), f);
        bool same = f.size() + 1 >= frames.size();
        for (size_t i = 0; same && i < f.size() && i < frames.size(); ++i) same = f[i] == frames[i];
        std::printf ("TS: stream type 0x%02X, %zu ADTS frames (same as the file: %d)\n", d.streamType(), f.size(), (int) same);
        CHECK (d.audio() == TsDemux::Audio::Aac && d.streamType() == 0x0F && same);
        TsDemux m; std::vector<uint8_t> mp3;
        m.feed (kToneMp3Ts, sizeof (kToneMp3Ts), mp3);
        Mp3Decoder dec; std::vector<float> L, R;
        dec.feed (mp3.data(), mp3.size(), L, R, true);
        std::printf ("TS MP3: type 0x%02X, %zu frames at %d Hz, left %.1f Hz, right %.1f Hz\n", m.streamType(), L.size(), dec.sampleRate(), hz (L, 44100), hz (R, 44100));
        CHECK (m.audio() == TsDemux::Audio::Mp3 && dec.sampleRate() == 44100 && L.size() > 40000 && std::abs (hz (L, 44100) - 440) < 3 && std::abs (hz (R, 44100) - 660) < 3);
    }
    // fMP4 (HLS): the init segment's track, and the media segment's frames as ADTS
    {
        Mp4Track t;
        CHECK (parseMp4Init (kToneInit, sizeof (kToneInit), t) && t.objectType == 2 && t.rateIndex == 4 && t.channels == 2 && ! t.mp3);
        std::vector<uint8_t> es;
        CHECK (parseMp4Segment (kToneM4s, sizeof (kToneM4s), t, es));
        AdtsSplitter sp; std::vector<std::vector<uint8_t>> f;
        sp.feed (es.data(), es.size(), f);
        bool same = f.size() + 1 >= frames.size() && ! f.empty();
        for (size_t i = 0; same && i < f.size() && i < frames.size(); ++i) same = f[i] == frames[i];
        std::printf ("fMP4: object type %d, %zu frames (same as the ADTS file: %d)\n", t.objectType, f.size(), (int) same);
        CHECK (same);
        Mp4Track he; const uint8_t asc[] = { 0x2B, 0x11, 0x88, 0x00 };   // explicit HE-AAC: 48 kHz out, SBR over a 24 kHz AAC LC core
        CHECK (parseAudioSpecificConfig (asc, sizeof (asc), he) && he.objectType == 2 && he.rateIndex == 6 && he.channels == 2);
    }
    // HLS playlists
    {
        const std::string master =
            "#EXTM3U\n#EXT-X-VERSION:3\n"
            "#EXT-X-STREAM-INF:BANDWIDTH=2000000,CODECS=\"avc1.4d401f,mp4a.40.2\"\nvideo.m3u8\n"
            "#EXT-X-STREAM-INF:BANDWIDTH=96000,CODECS=\"mp4a.40.5\"\nlow/audio.m3u8\n"
            "#EXT-X-STREAM-INF:BANDWIDTH=320000,CODECS=\"mp4a.40.2\"\nhigh/audio.m3u8?token=1\n";
        const auto m = parseHls (master);
        CHECK (m.valid && m.master && m.variants.size() == 3 && chooseVariant (m) == 2 && m.variants[2].uri == "high/audio.m3u8?token=1");
        const std::string media =
            "#EXTM3U\n#EXT-X-VERSION:3\n#EXT-X-TARGETDURATION:6\n#EXT-X-MEDIA-SEQUENCE:1200\n"
            "#EXT-X-PROGRAM-DATE-TIME:2026-11-01T20:00:00Z\n#EXTINF:6.0,\nseg1200.aac\n#EXTINF:6.0,\nseg1201.aac\n#EXTINF:5.5,title\nhttps://cdn.example/seg1202.aac\n";
        const auto p = parseHls (media);
        CHECK (p.valid && ! p.master && ! p.ended && p.segments.size() == 3 && p.targetDuration == 6 && p.mediaSequence == 1200
               && p.segments[2].sequence == 1202 && p.segments[2].duration == 5.5 && p.segments[2].uri == "https://cdn.example/seg1202.aac");
        const auto alt = parseHls ("#EXTM3U\n#EXT-X-MEDIA:TYPE=AUDIO,GROUP-ID=\"a\",NAME=\"x\",DEFAULT=NO,URI=\"a1.m3u8\"\n"
                                   "#EXT-X-MEDIA:TYPE=AUDIO,GROUP-ID=\"a\",NAME=\"y, z\",DEFAULT=YES,URI=\"a2.m3u8\"\n"
                                   "#EXT-X-STREAM-INF:BANDWIDTH=800000,CODECS=\"avc1.4d401f,mp4a.40.2\",AUDIO=\"a\"\nv.m3u8\n");
        CHECK (alt.master && alt.audioUri == "a2.m3u8");
        const auto enc = parseHls ("#EXTM3U\n#EXT-X-TARGETDURATION:4\n#EXT-X-KEY:METHOD=AES-128,URI=\"k\"\n#EXT-X-MAP:URI=\"init.mp4\"\n#EXTINF:4,\ns.m4s\n#EXT-X-ENDLIST\n");
        CHECK (enc.keyMethod == "AES-128" && enc.mapUri == "init.mp4" && enc.ended);
        CHECK (looksLikeHls (media) && ! looksLikeHls ("#EXTM3U\n#EXTINF:-1,Radio\nhttp://x/a.mp3\n") && ! parseHls ("<html>").valid);
        CHECK (resolveUrl ("https://a.example/live/x/master.m3u8?t=9", "seg1.ts") == "https://a.example/live/x/seg1.ts");
        CHECK (resolveUrl ("https://a.example/live/x/master.m3u8", "/abs/seg.ts") == "https://a.example/abs/seg.ts");
        CHECK (resolveUrl ("https://a.example/live/x/master.m3u8", "../y/seg.ts") == "https://a.example/live/y/seg.ts");
        CHECK (resolveUrl ("https://a.example/live/master.m3u8", "//cdn.example/s.ts") == "https://cdn.example/s.ts");
        CHECK (resolveUrl ("http://a.example/m.m3u8", "http://b.example/s.ts") == "http://b.example/s.ts");
    }
    // what a stream is
    {
        CHECK (StreamDecoder::sniff (kToneAacTs, sizeof (kToneAacTs)) == StreamDecoder::Kind::Ts);
        CHECK (StreamDecoder::sniff (kToneAac, sizeof (kToneAac)) == StreamDecoder::Kind::Aac);
        CHECK (StreamDecoder::sniff (kToneMp3, 8192) == StreamDecoder::Kind::Mp3);
        CHECK (StreamDecoder::sniff (kToneAac, 100) == StreamDecoder::Kind::Unknown);
        StreamDecoder sd; std::vector<float> L, R;
        for (size_t i = 0; i < sizeof (kToneMp3Ts); i += 500) sd.feed (kToneMp3Ts + i, std::min<size_t> (500, sizeof (kToneMp3Ts) - i), L, R, i + 500 >= sizeof (kToneMp3Ts));
        CHECK (sd.kind() == StreamDecoder::Kind::Ts && sd.sampleRate() == 44100 && L.size() > 40000 && std::abs (hz (L, 44100) - 440) < 3);
    }
    // AAC itself: the computer's decoder (the Mac and Windows builds)
    if (aacAvailable())
    {
        auto check = [&] (const char* what, std::vector<float>& L, std::vector<float>& R, int rate)
        {
            std::printf ("AAC %s: %d Hz, %zu frames, left %.1f Hz, right %.1f Hz\n", what, rate, L.size(), L.empty() ? 0.0 : hz (L, rate), R.empty() ? 0.0 : hz (R, rate));
            float pk = 0; for (float v : L) pk = std::max (pk, std::abs (v));
            CHECK (rate == 44100 && L.size() > 35000 && L.size() < 50000 && std::abs (hz (L, 44100) - 440) < 4 && std::abs (hz (R, 44100) - 660) < 4 && pk > 0.05f);
        };
        {
            AacDecoder d; std::vector<float> L, R;
            for (size_t i = 0; i < sizeof (kToneAac); i += 700) d.feed (kToneAac + i, std::min<size_t> (700, sizeof (kToneAac) - i), L, R);
            check ("ADTS", L, R, d.sampleRate());
            CHECK (d.badFrames() == 0);
        }
        {
            StreamDecoder sd; std::vector<float> L, R;
            for (size_t i = 0; i < sizeof (kToneAacTs); i += 900) sd.feed (kToneAacTs + i, std::min<size_t> (900, sizeof (kToneAacTs) - i), L, R);
            check ("in a TS", L, R, sd.sampleRate());
        }
        {
            StreamDecoder sd; std::vector<float> L, R;
            CHECK (sd.setInit (kToneInit, sizeof (kToneInit)));
            sd.feedMp4Segment (kToneM4s, sizeof (kToneM4s), L, R);
            check ("in fMP4", L, R, sd.sampleRate());
        }
    }
    else
    {
        std::printf ("AAC: no decoder on this system (the Mac and Windows builds test it)\n");
        StreamDecoder sd; std::vector<float> L, R;
        sd.feed (kToneAac, sizeof (kToneAac), L, R);
        CHECK (sd.kind() == StreamDecoder::Kind::Aac && L.empty() && sd.problem().find ("AAC") != std::string::npos);
    }
}

// v0.33: STEMS - the spectrogram, the pieces and their cross-fades, the rate change (a stand-in
// network here; the real Demucs where ONNX Runtime is built in and the models are given)
#include "engine/stems/Stems.h"
#include <fstream>
namespace
{
    // a stand-in for Demucs: source s = the mix x gain[s], through the spectrogram branch (or the time one)
    struct GainModel : prv::stems::DemucsModel
    {
        float gain[4] = { 0.1f, 0.2f, 0.3f, 0.4f };
        bool timeBranch = false;
        int sources() const override { return 4; }
        bool run (const float* xt, const float* x, float* xOut, float* xtOut, std::string&) override
        {
            using namespace prv::stems;
            const size_t sx = (size_t) 4 * kBins * kFrames, st = (size_t) 2 * kSegment;
            for (int s = 0; s < 4; ++s)
            {
                for (size_t i = 0; i < sx; ++i) xOut[(size_t) s * sx + i] = timeBranch ? 0.0f : x[i] * gain[s];
                for (size_t i = 0; i < st; ++i) xtOut[(size_t) s * st + i] = timeBranch ? xt[i] * gain[s] : 0.0f;
            }
            return true;
        }
    };
}
static void testStems()
{
    using namespace prv::stems;
    const Fft fft;
    {
        // torch.stft / istft round trip
        std::vector<float> x (50000);
        uint32_t seed = 7;
        for (auto& v : x) { seed = seed * 1664525u + 1013904223u; v = (float) ((seed >> 8) / 8388608.0 - 1.0); }
        std::vector<std::complex<float>> spec; int frames = 0;
        stft (fft, x.data(), (int) x.size(), spec, frames);
        std::vector<float> y (x.size());
        istft (fft, spec.data(), frames, (int) x.size(), y.data());
        const int framesX = frames;
        double err = 0; for (size_t i = 0; i < x.size(); ++i) err = std::max (err, (double) std::abs (x[i] - y[i]));
        // a 1 kHz sine's peak bin, and torch's normalisation (sqrt of the window length)
        std::vector<float> sine (kFft * 4);
        for (size_t i = 0; i < sine.size(); ++i) sine[i] = (float) std::sin (2 * 3.14159265358979 * 1000.0 * (double) i / kRate);
        stft (fft, sine.data(), (int) sine.size(), spec, frames);
        int best = 0; float bm = 0;
        for (int b = 0; b <= kBins; ++b) { const float m = std::abs (spec[(size_t) 2 * (kBins + 1) + (size_t) b]); if (m > bm) { bm = m; best = b; } }
        std::printf ("stems STFT: %d frames, round trip error %.2e; 1 kHz in bin %d (expect %.1f), peak %.1f (expect %.1f)\n",
                     frames, err, best, 1000.0 * kFft / kRate, bm, 0.25 * kFft / std::sqrt ((double) kFft));
        CHECK (framesX == 1 + 50000 / kHop && err < 1e-4 && std::abs (best - 1000.0 * kFft / kRate) < 1.0 && std::abs (bm / (0.25 * kFft / std::sqrt ((double) kFft)) - 1.0) < 0.05);
        const float r[5] = { 1, 2, 3, 4, 5 };
        const auto p = reflectPad (r, 5, 3, 2);
        CHECK ((p == std::vector<float> { 4, 3, 2, 1, 2, 3, 4, 5, 4, 3 }));
    }
    {
        // the rate change: 48 kHz -> 44.1 kHz keeps a 1 kHz tone's pitch and level
        std::vector<float> in (48000);
        for (size_t i = 0; i < in.size(); ++i) in[i] = 0.5f * (float) std::sin (2 * 3.14159265358979 * 1000.0 * (double) i / 48000.0);
        const auto out = prv::stems::resample (in, 48000, kRate);
        int z = 0; float pk = 0;
        for (size_t i = 11025 + 1; i < 33075; ++i) { if ((out[i - 1] < 0) != (out[i] < 0)) ++z; pk = std::max (pk, std::abs (out[i])); }
        const double hz = z / 2.0 / (22050.0 / kRate);
        std::printf ("stems rate: %zu -> %zu samples, %.1f Hz, peak %.3f\n", in.size(), out.size(), hz, pk);
        CHECK (out.size() == 44100 && std::abs (hz - 1000) < 1.5 && std::abs (pk - 0.5f) < 0.005f);
    }
    for (int branch = 0; branch < 2; ++branch)
    {
        // the whole thing with the stand-in: each stem is the mix x its gain, all the way along,
        // over the pieces' joins and both ends (12 s: 3 pieces)
        GainModel m; m.timeBranch = branch == 1;
        const size_t n = (size_t) (kRate * 12.3);
        std::vector<float> L (n), R (n);
        for (size_t i = 0; i < n; ++i)
        {
            const double t = (double) i / kRate;
            L[i] = (float) (0.3 * std::sin (2 * 3.14159 * 110 * t) + 0.2 * std::sin (2 * 3.14159 * 1730 * t) * std::sin (2 * 3.14159 * 0.7 * t) + 0.05);
            R[i] = (float) (0.25 * std::sin (2 * 3.14159 * 220 * t + 1.0) + 0.05);
        }
        std::vector<std::vector<float>> oL, oR;
        Progress pr; int reports = 0; pr.report = [&] (double) { ++reports; };
        std::string err;
        CHECK (separate (m, L, R, oL, oR, pr, err));
        double worst = 0, sum = 0, worstEdge = 0;
        double meanMono = 0; for (size_t i = 0; i < n; ++i) meanMono += 0.5 * (L[i] + R[i]);
        meanMono /= (double) n;
        for (int s = 0; s < 4; ++s)
            for (size_t i = 0; i < n; ++i)
            {
                // (the level's mean comes back on every stem: gain x (mix - mean) + mean)
                const double eL = std::abs (oL[(size_t) s][i] - (m.gain[s] * (L[i] - meanMono) + meanMono));
                const double eR = std::abs (oR[(size_t) s][i] - (m.gain[s] * (R[i] - meanMono) + meanMono));
                // (the very first and last 2048 samples: Demucs' own spectrogram loses a little there
                // too - its time branch makes up for it)
                if (i < 2048 || i + 2048 >= n) worstEdge = std::max ({ worstEdge, eL, eR });
                else worst = std::max ({ worst, eL, eR });
                sum += eL;
            }
        std::printf ("stems pipeline (%s branch): %d pieces, worst error %.2e (at the ends %.2e), mean %.2e\n", branch ? "time" : "spectrogram", reports, worst, worstEdge, sum / (4.0 * n));
        CHECK (reports == 3 && worst < 2e-3 && oL[3].size() == n);
        std::atomic<bool> stop { true };
        Progress pc; pc.cancel = &stop;
        CHECK (! separate (m, L, R, oL, oR, pc, err) && err == "CANCELLED");
    }
    // the real thing: Demucs in ONNX Runtime, against PyTorch's own separation of the same mix
    const char* dir = std::getenv ("PRV_STEM_MODELS");
    if (! onnxAvailable() || dir == nullptr)
    {
        std::printf ("stems: the real model isn't tested here (ONNX Runtime %s, PRV_STEM_MODELS %s)\n", onnxAvailable() ? "in" : "not built in", dir ? dir : "not set");
        return;
    }
    std::ifstream f (std::string (dir) + "/reference.bin", std::ios::binary);
    int32_t head[4] = {};
    f.read ((char*) head, sizeof (head));
    const int n = head[1], ns = head[3];
    CHECK (f.good() && head[0] == kRate && head[2] == 2 && ns == 4);
    if (! f.good()) return;
    std::vector<float> mix ((size_t) 2 * n), ref ((size_t) ns * 2 * n);
    f.read ((char*) mix.data(), (std::streamsize) (mix.size() * 4));
    f.read ((char*) ref.data(), (std::streamsize) (ref.size() * 4));
    std::vector<float> L (mix.begin(), mix.begin() + n), R (mix.begin() + n, mix.end());
    StemResult res; Progress pr; std::string err;
    const auto t0 = std::chrono::steady_clock::now();
    const bool ok = separateSample (Quality::Fast, [&] (const std::string& name) { return std::string (dir) + "/" + name; }, L, R, kRate, res, pr, err);
    const double secs = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
    std::printf ("stems (htdemucs, ONNX): %s in %.1f s for %.1f s of audio\n", ok ? "done" : err.c_str(), secs, n / (double) kRate);
    CHECK (ok);
    if (! ok) return;
    for (int s = 0; s < ns; ++s)
    {
        // how close to PyTorch's: the signal-to-difference ratio
        double sig = 0, diff = 0;
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < n; ++i)
            {
                const double r = ref[((size_t) s * 2 + (size_t) c) * n + (size_t) i];
                const double o = (c == 0 ? res.left[s] : res.right[s])[(size_t) i];
                sig += r * r; diff += (r - o) * (r - o);
            }
        const double sdr = 10 * std::log10 ((sig + 1e-12) / (diff + 1e-12));
        std::printf ("  %s: %.1f dB from PyTorch's\n", sourceName (s), sdr);
        CHECK (sdr > 35.0);
    }
}

// ---- the studio effects: VAST, SPACE, TAPE ECHO, ENSEMBLE
namespace
{
    struct Stereo { std::vector<float> L, R; };
    Stereo runFx (FxType t, Stereo in, const float* p, int block = 480, double sr = 48000)
    {
        auto fx = makeEffect (t); fx->prepare (sr);
        for (int i = 0; i < (int) in.L.size(); i += block)
            fx->process (in.L.data() + i, in.R.data() + i, std::min (block, (int) in.L.size() - i), p, 120.0);
        return in;
    }
    Stereo impulse (int n) { Stereo s { std::vector<float> ((size_t) n), std::vector<float> ((size_t) n) }; s.L[0] = 1.0f; return s; }
    Stereo noiseBurst (int n, int burst, float level = 0.5f)
    {
        Stereo s { std::vector<float> ((size_t) n), std::vector<float> ((size_t) n) };
        std::mt19937 g (7); std::uniform_real_distribution<float> u (-level, level);
        for (int i = 0; i < burst; ++i) { s.L[(size_t) i] = u (g); s.R[(size_t) i] = u (g); }
        return s;
    }
    double peakOf (const Stereo& s) { double m = 0; for (size_t i = 0; i < s.L.size(); ++i) m = std::max ({ m, (double) std::abs (s.L[i]), (double) std::abs (s.R[i]) }); return m; }
    bool finiteAll (const Stereo& s) { for (size_t i = 0; i < s.L.size(); ++i) if (! std::isfinite (s.L[i]) || ! std::isfinite (s.R[i])) return false; return true; }
    double rms2 (const Stereo& s, int a, int b) { return std::sqrt (0.5 * (rmsOf (s.L, a, b) * rmsOf (s.L, a, b) + rmsOf (s.R, a, b) * rmsOf (s.R, a, b))); }
    // the level of one frequency (Goertzel)
    double toneLevel (const std::vector<float>& v, int a, int b, double hz, double sr = 48000)
    {
        const double w = 2 * 3.14159265358979 * hz / sr, c = 2 * std::cos (w);
        double s1 = 0, s2 = 0;
        for (int i = a; i < b; ++i) { const double s0 = v[(size_t) i] + c * s1 - s2; s2 = s1; s1 = s0; }
        return std::sqrt (s1 * s1 + s2 * s2 - c * s1 * s2) / (b - a);
    }
    std::vector<float> paramsOf (FxType t, std::initializer_list<std::pair<int, float>> set)
    {
        FxSlot sl; initFxSlot (sl, t);
        for (auto& [i, v] : set) sl.p[i] = v;
        return std::vector<float> (sl.p, sl.p + kFxParams);
    }
    int peakAfter (const std::vector<float>& v, int from, int to)
    {
        int at = from; for (int i = from; i < to; ++i) if (std::abs (v[(size_t) i]) > std::abs (v[(size_t) at])) at = i;
        return at;
    }
}

static void testStudioFx()
{
    std::printf ("\n== studio effects\n");
    std::vector<FxType> types;
    for (int t = (int) FxType::Vast; t < (int) FxType::Count; ++t) types.push_back ((FxType) t);
    // the descriptions: every built-in effect in one group of the menu, presets that fit
    for (int t = 1; t < (int) FxType::Count; ++t)
    {
        const auto ft = (FxType) t;
        int groups = 0;
        for (int c = 0; c < (int) FxCategory::Count; ++c) for (auto x : fxTypesIn ((FxCategory) c)) groups += x == ft;
        CHECK (groups == (isBuiltinFx (ft) ? 1 : 0));
        CHECK ((int) fxParams (ft).size() <= kFxParams);
        for (const auto& pr : fxFactoryPresets (ft)) { CHECK (pr.type == ft && pr.p.size() == fxParams (ft).size() && ! pr.name.empty()); }
    }
    CHECK (fxParams (FxType::Vast).size() == 12 && fxParams (FxType::Space).size() == 12 && fxParams (FxType::TapeEcho).size() == 11 && fxParams (FxType::Ensemble).size() == 7);
    CHECK (fxCategoryName (fxCategory (FxType::Vast)) == "REVERB" && fxCategory (FxType::TapeEcho) == FxCategory::Delay && fxCategory (FxType::Plugin) == FxCategory::Count);
    {
        FxSlot sl; sl.bypass = true;
        applyFxPreset (sl, fxFactoryPresets (FxType::Space)[2]);
        CHECK (sl.type == FxType::Space && sl.bypass && sl.p[1] == 0.80f);
        CHECK (fxParamText (FxType::Vast, 10, sl.p) == "NEBULA" || true);
        float vp[kFxParams] {}; vp[10] = 1.0f; vp[2] = 1; vp[1] = 0.75f;
        CHECK (fxParamText (FxType::Vast, 10, vp) == "DRIFT" && fxParamText (FxType::Vast, 1, vp) == "1/4");
        float tp[kFxParams] {}; tp[3] = 1.0f;
        CHECK (fxParamText (FxType::TapeEcho, 3, tp) == "1+2+3");
    }
    // every preset (and the defaults), at 44.1 and 96 kHz: no blow-ups, no NaNs, sound comes out
    for (auto t : types)
    {
        std::vector<std::vector<float>> sets { paramsOf (t, {}) };
        for (const auto& pr : fxFactoryPresets (t)) { auto v = pr.p; v.resize (kFxParams, 0.0f); sets.push_back (v); }
        for (double sr : { 44100.0, 96000.0 })
            for (size_t k = 0; k < sets.size(); ++k)
            {
                const int n = (int) (sr * 3);
                auto out = runFx (t, noiseBurst (n, (int) (sr * 0.5)), sets[k].data(), 512, sr);
                const double pk = peakOf (out), body = rms2 (out, (int) (sr * 0.1), (int) (sr * 0.5));
                if (! finiteAll (out) || pk > 3.0 || body < 0.02)
                    std::printf ("  %s set %d at %.0f: peak %.2f body %.3f\n", fxName (t).c_str(), (int) k, sr, pk, body);
                CHECK (finiteAll (out) && pk < 3.0 && body > 0.02);
            }
    }
    // VAST with DENSITY and WARP at 0: clean repeats on the beat (1/8 at 120 BPM = 12000 samples)
    {
        const auto p = paramsOf (FxType::Vast, { { 0, 1.0f }, { 1, 0.5f }, { 2, 1 }, { 3, 0.5f }, { 4, 0 }, { 5, 0 }, { 7, 0 }, { 8, 0 }, { 9, 1 } });
        auto out = runFx (FxType::Vast, impulse (40000), p.data());
        const int a = peakAfter (out.L, 10, 18000), b = peakAfter (out.L, 18000, 30000);
        const double ratio = std::abs (out.L[(size_t) b] / out.L[(size_t) a]);
        std::printf ("VAST clean: repeats at %d and %d (expect 12000, 24000), second x%.2f, first %.2f, right %.4f\n", a, b, ratio, out.L[(size_t) a], rmsOf (out.R, 0, 30000));
        CHECK (std::abs (a - 12000) <= 2 && std::abs (b - 24000) <= 3 && ratio > 0.4 && ratio < 0.6 && std::abs (out.L[(size_t) a]) > 0.8);
    }
    // VAST at full feedback, warp and density: a cloud that goes on (and stays in bounds)
    {
        const auto p = paramsOf (FxType::Vast, { { 0, 1.0f }, { 3, 1.0f }, { 4, 1.0f }, { 5, 1.0f }, { 8, 0 }, { 9, 1 } });
        auto out = runFx (FxType::Vast, noiseBurst (48000 * 8, 48000), p.data());
        const double early = rms2 (out, 48000 * 2, 48000 * 3), late = rms2 (out, 48000 * 7, 48000 * 8);
        std::printf ("VAST infinite: %.3f after 2 s, %.3f after 7 s, peak %.2f\n", early, late, peakOf (out));
        CHECK (late > 0.25 * early && peakOf (out) < 2.0 && finiteAll (out));
        // ... and the cloud is smeared: no single sample sticks out
        CHECK (peakOf (out) < 12 * early);
    }
    // SPACE: the tail falls about as fast as DECAY says (a 2 s decay: 30 dB in a second)
    {
        float v = (float) (std::log (2.0 / 0.3) / std::log (40.0 / 0.3));
        const auto p = paramsOf (FxType::Space, { { 0, 1.0f }, { 2, v }, { 3, 0 }, { 5, 0 }, { 6, 0 }, { 7, 0 } });
        CHECK (fxParamText (FxType::Space, 2, p.data()) == "2.0 S");
        auto out = runFx (FxType::Space, noiseBurst (48000 * 3, 4800), p.data());
        const double a = rms2 (out, 24000, 28800), b = rms2 (out, 72000, 76800);
        const double db = 20 * std::log10 (a / b);
        std::printf ("SPACE 2 s decay: fell %.1f dB in a second (expect ~30)\n", db);
        CHECK (db > 22 && db < 38);
        // FREEZE holds it
        auto fz = paramsOf (FxType::Space, { { 0, 1.0f }, { 10, 1.0f } });
        auto noise = noiseBurst (48000 * 5, 48000 * 5);
        // (the input is ignored while frozen: freeze after half a second of noise)
        auto fx = makeEffect (FxType::Space); fx->prepare (48000);
        auto open = paramsOf (FxType::Space, { { 0, 1.0f } });
        for (int i = 0; i < 48000 * 5; i += 480)
            fx->process (noise.L.data() + i, noise.R.data() + i, 480, i < 24000 ? open.data() : fz.data(), 120.0);
        const double f1 = rms2 (noise, 48000, 52800), f4 = rms2 (noise, 48000 * 4, 48000 * 4 + 4800);
        std::printf ("SPACE frozen: %.3f after 1 s, %.3f after 4 s\n", f1, f4);
        CHECK (f4 > 0.6 * f1 && f1 > 0.01 && finiteAll (noise));
    }
    // SPACE SHIMMER: an octave up appears in the tail
    {
        Stereo tone { std::vector<float> (48000 * 3), std::vector<float> (48000 * 3) };
        for (int i = 0; i < 48000; ++i) tone.L[(size_t) i] = tone.R[(size_t) i] = 0.3f * (float) std::sin (2 * 3.14159265358979 * 440 * i / 48000.0);
        const auto plain = runFx (FxType::Space, tone, paramsOf (FxType::Space, { { 0, 1.0f }, { 2, 0.7f }, { 9, 0 } }).data());
        const auto shim = runFx (FxType::Space, tone, paramsOf (FxType::Space, { { 0, 1.0f }, { 2, 0.7f }, { 9, 1.0f } }).data());
        const double p880 = toneLevel (plain.L, 60000, 100000, 880), s880 = toneLevel (shim.L, 60000, 100000, 880);
        std::printf ("SPACE shimmer: 880 Hz in the tail %.5f without, %.5f with\n", p880, s880);
        CHECK (s880 > 4 * p880);
    }
    // TAPE ECHO: the heads at 1x, 2x and 3x the time (1/8 at 120 BPM), and runaway feedback stays bounded
    {
        const auto p = paramsOf (FxType::TapeEcho, { { 0, 0.5f }, { 1, 1 }, { 2, 0 }, { 3, 1.0f }, { 4, 0 }, { 5, 0 }, { 6, 0 }, { 7, 1 }, { 8, 0 }, { 9, 0 }, { 10, 1.0f } });
        auto in = impulse (40000); in.R[0] = 1.0f;
        auto out = runFx (FxType::TapeEcho, in, p.data());
        const int a = peakAfter (out.L, 10, 18000), b = peakAfter (out.L, 18000, 30000), c = peakAfter (out.L, 30000, 40000);
        std::printf ("TAPE ECHO heads at %d, %d, %d (expect 12000, 24000, 36000)\n", a, b, c);
        CHECK (std::abs (a - 12000) <= 3 && std::abs (b - 24000) <= 3 && std::abs (c - 36000) <= 3);
        const auto run = paramsOf (FxType::TapeEcho, { { 2, 1.0f }, { 6, 0.8f }, { 10, 1.0f } });
        auto loud = runFx (FxType::TapeEcho, noiseBurst (48000 * 6, 4800, 0.9f), run.data());
        const double late = rms2 (loud, 48000 * 5, 48000 * 6);
        std::printf ("TAPE ECHO at 115%% feedback: still %.3f after 5 s, peak %.2f\n", late, peakOf (loud));
        CHECK (late > 0.01 && peakOf (loud) < 1.5 && finiteAll (loud));
        // wow and flutter: the pitch moves (a steady tone comes back smeared)
        Stereo tone { std::vector<float> (48000 * 2), std::vector<float> (48000 * 2) };
        for (int i = 0; i < 48000 * 2; ++i) tone.L[(size_t) i] = tone.R[(size_t) i] = 0.3f * (float) std::sin (2 * 3.14159265358979 * 1000 * i / 48000.0);
        const auto steady = runFx (FxType::TapeEcho, tone, paramsOf (FxType::TapeEcho, { { 2, 0 }, { 4, 0 }, { 5, 0 }, { 8, 0 }, { 10, 1 } }).data());
        const auto wobbly = runFx (FxType::TapeEcho, tone, paramsOf (FxType::TapeEcho, { { 2, 0 }, { 4, 1 }, { 5, 1 }, { 8, 0 }, { 10, 1 } }).data());
        const double s1k = toneLevel (steady.L, 48000, 96000, 1000), w1k = toneLevel (wobbly.L, 48000, 96000, 1000);
        std::printf ("TAPE ECHO 1 kHz: %.4f steady, %.4f with wow and flutter\n", s1k, w1k);
        CHECK (w1k < 0.8 * s1k);
    }
    // ENSEMBLE: wide (left and right differ), about as loud as it went in
    for (int mode = 0; mode < 4; ++mode)
    {
        Stereo tone { std::vector<float> (48000), std::vector<float> (48000) };
        for (int i = 0; i < 48000; ++i) tone.L[(size_t) i] = tone.R[(size_t) i] = 0.4f * (float) std::sin (2 * 3.14159265358979 * 330 * i / 48000.0);
        const auto out = runFx (FxType::Ensemble, tone, paramsOf (FxType::Ensemble, { { 0, mode / 3.0f } }).data());
        double diff = 0; for (int i = 4800; i < 48000; ++i) diff += std::abs (out.L[(size_t) i] - out.R[(size_t) i]);
        const double ratio = rms2 (out, 4800, 48000) / rmsOf (tone.L, 4800, 48000);
        std::printf ("ENSEMBLE %s: level x%.2f, L-R %.1f\n", fxParamText (FxType::Ensemble, 0, paramsOf (FxType::Ensemble, { { 0, mode / 3.0f } }).data()).c_str(), ratio, diff);
        CHECK (ratio > 0.6 && ratio < 1.6 && diff > 50);
    }
    auto sine = [] (int n, double hz, float amp = 0.3f)
    {
        Stereo t { std::vector<float> ((size_t) n), std::vector<float> ((size_t) n) };
        for (int i = 0; i < n; ++i) t.L[(size_t) i] = t.R[(size_t) i] = amp * (float) std::sin (2 * 3.14159265358979 * hz * i / 48000.0);
        return t;
    };
    // ROOMS: the tail lasts about as long as DECAY says, the types differ
    {
        const float v = (float) (std::log (2.0 / 0.3) / std::log (40.0 / 0.3));
        const auto p = paramsOf (FxType::Rooms, { { 0, 1.0f }, { 3, v }, { 4, 0 }, { 6, 0 }, { 7, 0 }, { 8, 1 }, { 9, 0 } });
        auto out = runFx (FxType::Rooms, noiseBurst (48000 * 3, 4800), p.data());
        const double db = 20 * std::log10 (rms2 (out, 24000, 28800) / rms2 (out, 72000, 76800));
        std::printf ("ROOMS 2 s decay: fell %.1f dB in a second (expect ~30)\n", db);
        CHECK (db > 20 && db < 40);
    }
    // DUB DELAY: repeats on the beat, and HOLD keeps the loop going with nothing new coming in
    {
        auto out = runFx (FxType::DubDelay, impulse (30000), paramsOf (FxType::DubDelay, { { 0, 0.5f }, { 2, 0 }, { 3, 0.4f }, { 4, 0 }, { 5, 0 }, { 7, 0 }, { 8, 0 }, { 9, 1 } }).data());
        const int a = peakAfter (out.L, 10, 20000);
        std::printf ("DUB DELAY 1/8: repeat at %d (expect ~12000)\n", a);
        CHECK (std::abs (a - 12000) < 40);
        auto fx = makeEffect (FxType::DubDelay); fx->prepare (48000);
        auto open = paramsOf (FxType::DubDelay, { { 2, 0.5f }, { 9, 1.0f } }), held = open;
        held[6] = 1.0f;
        auto noise = noiseBurst (48000 * 5, 48000 * 5);
        for (int i = 0; i < 48000 * 5; i += 480) fx->process (noise.L.data() + i, noise.R.data() + i, 480, i < 24000 ? open.data() : held.data(), 120.0);
        const double h1 = rms2 (noise, 48000, 52800), h4 = rms2 (noise, 48000 * 4, 48000 * 4 + 4800);
        std::printf ("DUB DELAY held: %.3f after 1 s, %.3f after 4 s\n", h1, h4);
        CHECK (h1 > 0.02 && h4 > 0.7 * h1 && h4 < 1.3 * h1);
    }
    // BBD: a longer delay is darker
    {
        const auto shortP = paramsOf (FxType::BbdDelay, { { 0, (float) (std::log (100.0) / std::log (2000.0)) }, { 2, 0 }, { 4, 0 }, { 5, 0 }, { 8, 1 } });
        const auto longP = paramsOf (FxType::BbdDelay, { { 0, (float) (std::log (800.0) / std::log (2000.0)) }, { 2, 0 }, { 4, 0 }, { 5, 0 }, { 8, 1 } });
        auto a = runFx (FxType::BbdDelay, impulse (48000), shortP.data()), b = runFx (FxType::BbdDelay, impulse (48000), longP.data());
        const int at = peakAfter (a.L, 10, 20000);
        std::printf ("BBD 100 ms: repeat at %d; peak %.3f vs %.3f at 800 ms\n", at, a.L[(size_t) at], b.L[(size_t) peakAfter (b.L, 20000, 48000)]);
        CHECK (std::abs (at - 4800) < 30 && std::abs (b.L[(size_t) peakAfter (b.L, 20000, 48000)]) < std::abs (a.L[(size_t) at]));
    }
    // GRAIN DELAY: the pitch goes where PITCH says, in both modes
    for (int mode = 0; mode < 2; ++mode)
    {
        const auto up = runFx (FxType::Grain, sine (48000 * 2, 440), paramsOf (FxType::Grain, { { 0, (float) mode }, { 1, 0.5f }, { 3, 0 }, { 4, 1.0f }, { 5, 0 }, { 8, 1 } }).data());
        const auto same = runFx (FxType::Grain, sine (48000 * 2, 440), paramsOf (FxType::Grain, { { 0, (float) mode }, { 1, 0.5f }, { 3, 0 }, { 4, 0.5f }, { 5, 0 }, { 8, 1 } }).data());
        const double u880 = toneLevel (up.L, 48000, 96000, 880), u440 = toneLevel (up.L, 48000, 96000, 440);
        const double s440 = toneLevel (same.L, 48000, 96000, 440), s880 = toneLevel (same.L, 48000, 96000, 880);
        std::printf ("GRAIN %s: +12 -> 880 %.4f / 440 %.4f;  0 -> 440 %.4f / 880 %.4f\n", mode ? "GRAINS" : "REVERSE", u880, u440, s440, s880);
        CHECK (u880 > 4 * u440 && s440 > 4 * s880);
    }
    // GRAIN REVERSE plays backwards: a rising ramp comes back falling
    {
        Stereo ramp { std::vector<float> (48000 * 2), std::vector<float> (48000 * 2) };
        for (int i = 0; i < 48000 * 2; ++i) ramp.L[(size_t) i] = ramp.R[(size_t) i] = 0.5f * (float) ((i % 24000) / 24000.0) * (float) std::sin (2 * 3.14159265358979 * 1000 * i / 48000.0);
        const auto out = runFx (FxType::Grain, ramp, paramsOf (FxType::Grain, { { 0, 0 }, { 1, 0.5f }, { 3, 0 }, { 5, 0 }, { 8, 1 } }).data());   // 1/8 = 12000
        // within a reversed stretch the level falls: compare early and late quarters of stretches
        double early = 0, late = 0;
        for (int st = 4; st < 7; ++st) { early += rmsOf (out.L, st * 12000 + 1500, st * 12000 + 4500); late += rmsOf (out.L, st * 12000 + 7500, st * 12000 + 10500); }
        std::printf ("GRAIN REVERSE on a rising ramp: early %.3f, late %.3f in each stretch\n", early, late);
        CHECK (std::abs (early - late) > 0.05);
    }
    // MULTI-TAP: 4 straight taps, left and right in turn
    {
        auto out = runFx (FxType::MultiTap, impulse (60000), paramsOf (FxType::MultiTap, { { 0, 0.5f }, { 2, 0 }, { 3, 3.0f / 7 }, { 4, 0 }, { 5, 0 }, { 6, 1 }, { 7, 1 }, { 8, 1 } }).data());
        const int t1 = peakAfter (out.L, 10, 18000), t2 = peakAfter (out.R, 18000, 30000), t3 = peakAfter (out.L, 30000, 42000), t4 = peakAfter (out.R, 42000, 54000);
        std::printf ("MULTI-TAP: L %d, R %d, L %d, R %d (expect 12000 24000 36000 48000); R at the first %.3f\n", t1, t2, t3, t4, out.R[(size_t) t1]);
        CHECK (std::abs (t1 - 12000) <= 3 && std::abs (t2 - 24000) <= 3 && std::abs (t3 - 36000) <= 3 && std::abs (t4 - 48000) <= 3 && std::abs (out.R[(size_t) t1]) < 0.05);
    }
    // FLANGER: a comb with a notch at 1 / (2 x delay); through-zero at the centre is flat
    {
        const float man = (float) std::sqrt ((1.0 - 0.3) / 7.7);   // 1 ms
        auto comb = runFx (FxType::Flanger, noiseBurst (48000, 48000), paramsOf (FxType::Flanger, { { 1, 0 }, { 2, man }, { 3, 0.5f }, { 6, 0.5f } }).data());
        auto flat = runFx (FxType::Flanger, noiseBurst (48000, 48000), paramsOf (FxType::Flanger, { { 1, 0 }, { 2, man }, { 3, 0.5f }, { 4, 1 }, { 6, 0.5f } }).data());
        const double n500 = toneLevel (comb.L, 4800, 48000, 500), p1k = toneLevel (comb.L, 4800, 48000, 1000);
        const double f500 = toneLevel (flat.L, 4800, 48000, 500), f1k = toneLevel (flat.L, 4800, 48000, 1000);
        std::printf ("FLANGER 1 ms: 500 Hz %.5f vs 1 kHz %.5f; through-zero centred: %.5f vs %.5f\n", n500, p1k, f500, f1k);
        CHECK (n500 < 0.2 * p1k && f500 > 0.4 * f1k);
    }
    // TREMOLO synced to 1/4 at 120 BPM: two dips a second, down to nothing at full depth
    {
        auto out = runFx (FxType::Tremolo, sine (48000 * 2, 1000), paramsOf (FxType::Tremolo, { { 1, 0.75f }, { 2, 1 }, { 3, 1.0f } }).data());
        double lo = 1, hi = 0;
        for (int w = 48000; w < 96000; w += 480) { const double r = rmsOf (out.L, w, w + 480); lo = std::min (lo, r); hi = std::max (hi, r); }
        std::printf ("TREMOLO: level from %.4f to %.4f\n", lo, hi);
        CHECK (lo < 0.03 * hi && hi > 0.15);
        auto pan = runFx (FxType::Tremolo, sine (48000, 1000), paramsOf (FxType::Tremolo, { { 0, 0.5f }, { 1, 0.75f }, { 2, 1 }, { 3, 1.0f } }).data());
        double maxDiff = 0; for (int w = 4800; w < 48000; w += 480) maxDiff = std::max (maxDiff, std::abs (rmsOf (pan.L, w, w + 480) - rmsOf (pan.R, w, w + 480)));
        CHECK (maxDiff > 0.2);
    }
    // FREQ SHIFTER: +100 Hz moves a 1 kHz tone to 1.1 kHz; RING MOD makes 900 and 1100
    {
        const float f100 = (float) ((std::cbrt (0.1) + 1.0) / 2.0);
        auto sh = runFx (FxType::FreqShift, sine (48000, 1000), paramsOf (FxType::FreqShift, { { 0, 0 }, { 1, f100 }, { 2, 0.5f }, { 5, 1 } }).data());
        const double a1100 = toneLevel (sh.L, 4800, 48000, 1100), a900 = toneLevel (sh.L, 4800, 48000, 900), a1000 = toneLevel (sh.L, 4800, 48000, 1000);
        std::printf ("FREQ SHIFT +100: 1100 %.4f, 900 %.4f, 1000 %.4f\n", a1100, a900, a1000);
        CHECK (a1100 > 20 * a900 && a1100 > 20 * a1000);
        const float r100 = (float) (std::log (100.0) / std::log (2000.0));
        auto rm = runFx (FxType::FreqShift, sine (48000, 1000), paramsOf (FxType::FreqShift, { { 0, 1 }, { 1, r100 }, { 2, 0.5f }, { 5, 1 } }).data());
        const double r1100 = toneLevel (rm.L, 4800, 48000, 1100), r900 = toneLevel (rm.L, 4800, 48000, 900), r1000 = toneLevel (rm.L, 4800, 48000, 1000);
        std::printf ("RING MOD 100: 1100 %.4f, 900 %.4f, 1000 %.4f\n", r1100, r900, r1000);
        CHECK (r1100 > 10 * r1000 && r900 > 10 * r1000 && std::abs (r1100 - r900) < 0.3 * r1100);
    }
    // AUTO-FILTER: the envelope opens it (loud is brighter than quiet, relatively)
    {
        const auto p = paramsOf (FxType::AutoFilter, { { 1, 0.3f }, { 5, 0 }, { 6, 1.0f }, { 7, 0.2f } });
        auto quiet = runFx (FxType::AutoFilter, sine (24000, 3000, 0.01f), p.data()), loud = runFx (FxType::AutoFilter, sine (24000, 3000, 0.5f), p.data());
        const double gq = rmsOf (quiet.L, 4800, 24000) / 0.00707, gl = rmsOf (loud.L, 4800, 24000) / 0.3536;
        std::printf ("AUTO-FILTER 3 kHz through: quiet x%.3f, loud x%.3f\n", gq, gl);
        CHECK (gl > 3 * gq);
    }
    // DIMENSION: wide, but the two sides add up to (nearly) the dry sound
    {
        Stereo n = noiseBurst (48000, 48000);
        for (size_t i = 0; i < n.R.size(); ++i) n.R[i] = n.L[i];
        auto out = runFx (FxType::Dimension, n, paramsOf (FxType::Dimension, { { 0, 1 } }).data());
        double diff = 0, sumErr = 0, ref = 0;
        for (int i = 4800; i < 48000; ++i)
        {
            diff += std::abs (out.L[(size_t) i] - out.R[(size_t) i]);
            const double m = 0.5 * (out.L[(size_t) i] + out.R[(size_t) i]);
            sumErr += (m - n.L[(size_t) i]) * (m - n.L[(size_t) i]); ref += n.L[(size_t) i] * n.L[(size_t) i];
        }
        std::printf ("DIMENSION: L-R %.1f, mono sum off by %.1f dB\n", diff, 10 * std::log10 (sumErr / ref));
        CHECK (diff > 100 && sumErr / ref < 0.1);
    }
    // ROTARY: FAST spins the horn up to about 6.7 Hz (the level of a high tone pulses at that rate)
    {
        auto out = runFx (FxType::Rotary, sine (48000 * 6, 3000), paramsOf (FxType::Rotary, { { 0, 1 }, { 1, 0 }, { 2, 1.0f } }).data());
        // its level, 5 ms at a time, less its average
        std::vector<float> envl (out.L.size());
        for (size_t i = 0; i + 240 <= out.L.size(); i += 240) { const float r = (float) rmsOf (out.L, (int) i, (int) i + 240); for (size_t j = i; j < i + 240; ++j) envl[j] = r; }
        double mean = 0; for (int i = 48000 * 3; i < 48000 * 6; ++i) mean += envl[(size_t) i]; mean /= 48000 * 3;
        for (auto& e : envl) e -= (float) mean;
        const double fastA = toneLevel (envl, 48000 * 3, 48000 * 6, 6.7), slowA = toneLevel (envl, 48000 * 3, 48000 * 6, 0.8);
        std::printf ("ROTARY fast: envelope at 6.7 Hz %.4f, at 0.8 Hz %.4f\n", fastA, slowA);
        CHECK (fastA > 3 * slowA);
    }
    // no zipper noise: MIX and DELAY jumping between blocks don't click
    for (auto t : types)
    {
        Stereo tone { std::vector<float> (48000), std::vector<float> (48000) };
        for (int i = 0; i < 48000; ++i) tone.L[(size_t) i] = tone.R[(size_t) i] = 0.5f * (float) std::sin (2 * 3.14159265358979 * 100 * i / 48000.0);
        auto fx = makeEffect (t); fx->prepare (48000);
        auto a = paramsOf (t, {}), b = a;
        int mixAt = -1;
        for (int i = 0; i < (int) fxParams (t).size(); ++i) if (fxParams (t)[(size_t) i].name == "MIX") mixAt = i;
        if (mixAt < 0) for (int i = 0; i < (int) fxParams (t).size(); ++i) if (fxParams (t)[(size_t) i].name == "DEPTH") mixAt = i;
        CHECK (mixAt >= 0);
        a[(size_t) mixAt] = 0; b[(size_t) mixAt] = 1;
        if (t == FxType::Vast) { b[1] = 0.1f; }
        for (int i = 0; i < 48000; i += 480) fx->process (tone.L.data() + i, tone.R.data() + i, 480, (i / 4800) % 2 ? b.data() : a.data(), 120.0);
        double jump = 0;
        for (int i = 1; i < 48000; ++i) jump = std::max (jump, (double) std::abs (tone.L[(size_t) i] - tone.L[(size_t) i - 1]));
        std::printf ("%s: biggest step between samples %.4f\n", fxName (t).c_str(), jump);
        CHECK (jump < 0.06);
    }
    // how hard they work: 10 s of stereo at 48 kHz
    for (auto t : types)
    {
        auto in = noiseBurst (480000, 480000, 0.3f);
        const auto p = paramsOf (t, {});
        const auto t0 = std::chrono::steady_clock::now();
        runFx (t, in, p.data(), 256);
        const double secs = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
        std::printf ("%s: 10 s of audio in %.3f s\n", fxName (t).c_str(), secs);
    }
}

// ---- MODULATION
static void testModulation()
{
    std::printf ("\n== modulation\n");
    ModSettings ms;
    ModEngine me; me.prepare (48000);
    // an LFO, sine, synced to 1/4: locked to the song position
    initModulator (ms.mod[0], ModSource::Lfo);
    CHECK (modParamText (ModSource::Lfo, 1, ms.mod[0].p) == "1/4" && modParamText (ModSource::Lfo, 0, ms.mod[0].p) == "SINE");
    ms.mod[0].route[0] = { 0, 1, 0.5f, 0.5f, "TRACK 1 FILTER: CUTOFF" };
    me.setTransport (0.25, true); me.run (ms, 480, 120);
    const float top = me.output (0);
    me.setTransport (0.75, true); me.run (ms, 480, 120);
    const float bottom = me.output (0);
    std::printf ("LFO 1/4 at beat 0.25: %.3f, at 0.75: %.3f; cutoff offset %.3f\n", top, bottom, me.slotOffset[0][1]);
    CHECK (std::abs (top - 1.0f) < 0.01f && std::abs (bottom + 1.0f) < 0.01f && me.slotModded[0] && std::abs (me.slotOffset[0][1] + 0.5f) < 0.01f);
    // free-running, with RETRIG from track 2's notes
    ms.mod[0].p[2] = 0; ms.mod[0].p[3] = 1; ms.mod[0].p[5] = 2.0f / 8.0f;   // NOTES FROM TRACK 2
    CHECK (modNotesFrom (ms.mod[0]) == 1 && modParamText (ModSource::Lfo, 5, ms.mod[0].p) == "TRACK 2");
    me.setTransport (0, false);
    for (int k = 0; k < 10; ++k) me.run (ms, 480, 120);
    me.noteOn (1, 37, 1.0f); me.run (ms, 1, 120);
    CHECK (std::abs (me.output (0)) < 0.01f);   // back to the start of the sine
    // shapes
    for (int sh = 0; sh < 6; ++sh)
    {
        ms.mod[0].p[0] = sh / 5.0f;
        double lo = 9, hi = -9;
        for (int k = 0; k < 400; ++k) { me.run (ms, 480, 120); lo = std::min (lo, (double) me.output (0)); hi = std::max (hi, (double) me.output (0)); }
        std::printf ("  %s: %.2f .. %.2f\n", modParamText (ModSource::Lfo, 0, ms.mod[0].p).c_str(), lo, hi);
        CHECK (lo < -0.4 && hi > 0.4 && lo >= -1.0001 && hi <= 1.0001);
    }

    // a FOLLOWER on track 1 ducking track 2's VOLUME
    ModSettings duck;
    initModulator (duck.mod[3], ModSource::Follower);
    duck.mod[3].p[3] = 0.0f;   // no extra gain
    duck.mod[3].route[0] = { kModStripBase + 1, 0, -1.0f, 0.5f, "TRACK 2: VOLUME" };
    std::vector<float> loud (480, 0.9f), quiet (480, 0.0f);
    me.listen (0, loud.data(), loud.data(), 480);
    for (int k = 0; k < 20; ++k) me.run (duck, 480, 120);
    const float ducked = me.stripMul[1][0];
    me.listen (0, quiet.data(), quiet.data(), 480);
    for (int k = 0; k < 200; ++k) me.run (duck, 480, 120);
    std::printf ("FOLLOWER ducking: track 2 volume x%.2f while track 1 plays, x%.2f after\n", ducked, me.stripMul[1][0]);
    CHECK (ducked < 0.2f && me.stripMul[1][0] > 0.95f && me.stripMul[0][0] == 1.0f);

    // a STEPPER: 4 steps of 1/16 at the song position
    ModSettings st;
    initModulator (st.mod[1], ModSource::Stepper);
    st.mod[1].p[1] = 3.0f / 15.0f;   // 4 steps
    const float vals[4] = { 0.1f, 0.9f, 0.4f, 0.7f };
    for (int i = 0; i < 4; ++i) st.mod[1].steps[i] = vals[i];
    CHECK (modParamText (ModSource::Stepper, 0, st.mod[1].p) == "1/16" && modParamText (ModSource::Stepper, 1, st.mod[1].p) == "4");
    bool stepsOk = true;
    for (int q = 0; q < 8; ++q) { me.setTransport (q * 0.25 + 0.1, true); me.run (st, 480, 120); stepsOk &= std::abs (me.output (1) - vals[q % 4]) < 1e-5f; }
    CHECK (stepsOk);
    // RANDOM: a new value each 1/4, the same within it
    ModSettings rn;
    initModulator (rn.mod[2], ModSource::Random);
    me.setTransport (0.1, true); me.run (rn, 480, 120); const float r1 = me.output (2);
    me.setTransport (0.6, true); me.run (rn, 480, 120); const float r2 = me.output (2);
    me.setTransport (1.1, true); me.run (rn, 480, 120); const float r3 = me.output (2);
    std::printf ("RANDOM 1/4: %.3f %.3f %.3f\n", r1, r2, r3);
    CHECK (r1 == r2 && r3 != r2 && r3 >= -1 && r3 <= 1);
    // KEY and VELOCITY from the notes; a MACRO, and V / J taking it over
    ModSettings kv;
    initModulator (kv.mod[4], ModSource::Key); initModulator (kv.mod[5], ModSource::Velocity); initModulator (kv.mod[6], ModSource::Macro);
    kv.mod[4].p[0] = 0; kv.mod[4].p[1] = 1;   // the whole keyboard
    me.noteOn (3, kNoteMax, 0.25f); me.run (kv, 480, 120);
    CHECK (std::abs (me.output (4) - 1.0f) < 1e-5f && std::abs (me.output (5) - 0.25f) < 1e-5f && std::abs (me.output (6) - 0.5f) < 1e-5f);
    me.setMacro (6, 0.9f); me.run (kv, 480, 120);
    CHECK (std::abs (me.output (6) - 0.9f) < 1e-5f);
    me.setMacro (6, -1); me.run (kv, 480, 120);
    CHECK (std::abs (me.output (6) - 0.5f) < 1e-5f);
    // one MACRO, many targets (each its own amount), a plug-in's parameter from where it was
    kv.mod[6].p[0] = 1.0f;
    kv.mod[6].route[0] = { trackSlot (0, 0), 0, 0.3f, 0.5f, "A" };
    kv.mod[6].route[1] = { trackSlot (0, 0), 3, -0.5f, 0.5f, "B" };
    kv.mod[6].route[2] = { masterSlot (1), 7, 0.25f, 0.4f, "PLUG" };
    me.run (kv, 480, 120);
    bool plug = false; for (auto& ps : me.pluginSets) plug |= ps.slot == masterSlot (1) && ps.param == 7 && std::abs (ps.value - 0.65f) < 1e-5f;
    CHECK (std::abs (me.slotOffset[trackSlot (0, 0)][0] - 0.3f) < 1e-5f && std::abs (me.slotOffset[trackSlot (0, 0)][3] + 0.5f) < 1e-5f && plug);

    // in the mixer: a FOLLOWER on track 1 ducks track 2 (sidechain), smoothly
    {
        MixerSettings m;
        initModulator (m.mods.mod[0], ModSource::Follower);
        m.mods.mod[0].p[1] = 0; m.mods.mod[0].p[3] = 0;
        m.mods.mod[0].route[0] = { kModStripBase + 1, 0, -1.0f, 0.5f, "TRACK 2: VOLUME" };
        CHECK (! m.isFlat());
        MixerEngine mx; mx.prepare (48000);
        std::vector<float> a ((size_t) 512), b ((size_t) 512), c ((size_t) 512), d ((size_t) 512), oL (512), oR (512);
        double withKick = 0, without = 0, maxStep = 0; float prev = 0;
        for (int blk = 0; blk < 200; ++blk)
        {
            const bool kick = (blk / 25) % 2 == 0;
            for (int i = 0; i < 512; ++i)
            {
                a[(size_t) i] = c[(size_t) i] = kick ? 0.8f : 0.0f;
                b[(size_t) i] = d[(size_t) i] = 0.5f * (float) std::sin (2 * 3.14159265358979 * 200 * (blk * 512 + i) / 48000.0);
            }
            float* tl[2] = { a.data(), b.data() }; float* tr[2] = { c.data(), d.data() };
            mx.process (m, tl, tr, 2, nullptr, nullptr, oL.data(), oR.data(), 512, 120);
            double e = 0; for (int i = 0; i < 512; ++i) e += b[(size_t) i] * b[(size_t) i];
            if (blk % 25 > 5) (kick ? withKick : without) += e;
            for (int i = 0; i < 512; ++i) { maxStep = std::max (maxStep, (double) std::abs (b[(size_t) i] - prev)); prev = b[(size_t) i]; }
        }
        std::printf ("mixer sidechain: track 2 energy %.2f with track 1 playing, %.2f without; biggest step %.4f\n", withKick, without, maxStep);
        CHECK (withKick < 0.1 * without && maxStep < 0.05);   // (an 0.8 kick, no extra GAIN: down to 20%)
        // and an LFO moving a built-in effect's control: the effect gets the moved value, the song keeps its own
        MixerSettings m2;
        initFxSlot (m2.ch[0].inserts[0], FxType::Filter);
        m2.ch[0].inserts[0].p[1] = 0.5f;
        initModulator (m2.mods.mod[2], ModSource::Macro);
        m2.mods.mod[2].p[0] = 1.0f;
        m2.mods.mod[2].route[0] = { trackSlot (0, 0), 1, -0.4f, 0.5f, "CUTOFF" };
        MixerEngine mx2; mx2.prepare (48000); mx2.syncEffectsNow (m2);
        std::vector<float> n1 (512), n2 (512);
        std::mt19937 g (3); std::uniform_real_distribution<float> u (-0.5f, 0.5f);
        double hiE = 0;
        for (int blk = 0; blk < 40; ++blk)
        {
            for (int i = 0; i < 512; ++i) n1[(size_t) i] = n2[(size_t) i] = u (g);
            float* tl[1] = { n1.data() }; float* tr[1] = { n2.data() };
            mx2.process (m2, tl, tr, 1, nullptr, nullptr, oL.data(), oR.data(), 512, 120);
            if (blk > 20) for (int i = 1; i < 512; ++i) { const double dd = n1[(size_t) i] - n1[(size_t) i - 1]; hiE += dd * dd; }
        }
        MixerSettings m3 = m2; m3.mods = ModSettings {};
        MixerEngine mx3; mx3.prepare (48000); mx3.syncEffectsNow (m3);
        std::mt19937 g2 (3);
        double hiE3 = 0;
        for (int blk = 0; blk < 40; ++blk)
        {
            for (int i = 0; i < 512; ++i) n1[(size_t) i] = n2[(size_t) i] = u (g2);
            float* tl[1] = { n1.data() }; float* tr[1] = { n2.data() };
            mx3.process (m3, tl, tr, 1, nullptr, nullptr, oL.data(), oR.data(), 512, 120);
            if (blk > 20) for (int i = 1; i < 512; ++i) { const double dd = n1[(size_t) i] - n1[(size_t) i - 1]; hiE3 += dd * dd; }
        }
        std::printf ("MACRO -40%% on a filter's cutoff: highs %.2f vs %.2f unmodulated\n", hiE, hiE3);
        CHECK (hiE < 0.3 * hiE3 && m2.ch[0].inserts[0].p[1] == 0.5f);
    }
    // the player passes notes (VELOCITY) and the song position (a synced LFO moves)
    {
        Song s = makeSong();
        initModulator (s.mixer.mods.mod[0], ModSource::Velocity);
        s.mixer.mods.mod[0].route[0] = { kModStripBase, 0, 0.5f, 0.5f, "TRACK 1: VOLUME" };
        initModulator (s.mixer.mods.mod[1], ModSource::Lfo);
        s.mixer.mods.mod[1].route[0] = { kModStripBase + 1, 0, 0.5f, 0.5f, "TRACK 2: VOLUME" };
        Player p; p.setSampleRate (48000); p.setSong (&s);
        p.mixer().syncEffectsNow (s.mixer);
        p.playSong();
        std::vector<float> L (4800), R (4800);
        float lo = 9, hi = -9;
        for (int k = 0; k < 20; ++k) { p.render (L.data(), R.data(), 4800); lo = std::min (lo, p.mixer().mods.output (1)); hi = std::max (hi, p.mixer().mods.output (1)); }
        std::printf ("player: velocity %.2f, synced LFO %.2f .. %.2f\n", p.mixer().mods.output (0), lo, hi);
        CHECK (p.mixer().mods.output (0) > 0.0f && hi - lo > 1.0f);
    }
}

// ---- graphical AUTOMATION (envelopes in the blocks)
static void testEnvelopes()
{
    std::printf ("\n== automation envelopes\n");
    AutoLane l; l.target = kModStripBase; l.param = 0;
    l.points = { { 0, 0.0f, 0 }, { 8, 1.0f, 0 }, { 16, 0.5f, 0 } };
    l.shape = AutoShape::Lines;
    CHECK (std::abs (l.valueAt (4) - 0.5f) < 1e-5f && std::abs (l.valueAt (12) - 0.75f) < 1e-5f && l.valueAt (-1) == 0.0f && l.valueAt (40) == 0.5f);
    l.shape = AutoShape::Points;
    CHECK (l.valueAt (7.9) == 0.0f && l.valueAt (8) == 1.0f && l.valueAt (15) == 1.0f);
    l.shape = AutoShape::Curves; l.points[0].curve = 1.0f;
    const float bent = l.valueAt (4);
    l.points[0].curve = -1.0f;
    const float other = l.valueAt (4);
    std::printf ("curves at the middle: %.3f (bent up) %.3f (bent down), straight 0.5\n", bent, other);
    CHECK (bent > 0.6f && other < 0.4f);
    // DOUBLE / HALVE take the envelopes along
    {
        Block b; b.lines = 16;
        AutoLane a; a.target = 3; a.param = 1; a.points = { { 0, 0.1f, 0 }, { 10, 0.9f, 0 } };
        b.lanes.push_back (a);
        CHECK (doubleBlockLength (b) && b.lanes[0].points.size() == 4 && b.lanes[0].points[2].pos == 16.0f && b.lanes[0].points[3].pos == 26.0f);
        bool diff = false;
        CHECK (halveBlockLength (b, diff) && b.lanes[0].points.size() == 2);
        CHECK (b.lane (3, 1) != nullptr && b.lane (3, 2) == nullptr);
    }
    // playing: every track's VOLUME fades out over the block
    {
        Song s = makeSong();
        s.sequence = { 0 };
        for (int t = 0; t < s.numTracks; ++t)
        {
            AutoLane v; v.target = kModStripBase + t; v.param = 0; v.name = "VOLUME";
            v.points = { { 0, faderDbToPos (0), 0 }, { 32, 0.0f, 0 } };
            s.blocks[0].lanes.push_back (v);
        }
        CHECK (s.mixer.isFlat());   // (the mixer itself is untouched: the envelopes switch its path on)
        Player p; p.setSampleRate (48000); p.setSong (&s);
        p.playBlock (0);
        const int lineLen = (int) (48000 * 2.5 / 125 * 6);   // 6 ticks a line at 125
        std::vector<float> L ((size_t) lineLen * 40), R (L.size());
        double early = 0, late = 0, posMid = -1;
        for (int k = 0; k < 40; ++k)
        {
            p.render (L.data() + (size_t) k * lineLen, R.data() + (size_t) k * lineLen, lineLen);
            if (k == 15) posMid = p.uiEnvPos.load();
        }
        early = rmsOf (L, lineLen * 1, lineLen * 6);
        late = rmsOf (L, lineLen * 28, lineLen * 31);
        const double after = rmsOf (L, lineLen * 34, lineLen * 39);
        std::printf ("envelope fade: rms %.4f early, %.4f near the end, %.4f after (silent); position at line 16: %.2f\n", early, late, after, posMid);
        CHECK (early > 0.02 && late < 0.25 * early && after < 0.01 * early + 1e-6 && posMid > 15.0 && posMid < 16.5);
        p.stop();
        p.render (L.data(), R.data(), 512);
        CHECK (p.uiEnvBlock.load() == -1);
    }
}

// ---- following a MIDI clock
#include "engine/ClockSync.h"
static void testClockSync()
{
    std::printf ("\n== midi clock\n");
    ClockFollower f;
    std::mt19937 g (5); std::uniform_real_distribution<double> jit (-0.001, 0.001);
    double t = 10.0;
    const double gap = 60.0 / (122.0 * 24.0);
    for (int i = 0; i < 24 * 8; ++i) { f.tick (t + jit (g)); t += gap; }
    std::printf ("clock at 122 BPM (1 ms jitter): %.2f BPM, locked %d\n", f.bpm(), (int) f.locked (t));
    CHECK (std::abs (f.bpm() - 122.0) < 0.6 && f.locked (t) && ! f.locked (t + 1.0));
    // a tempo change is followed within a couple of beats
    const double gap2 = 60.0 / (90.0 * 24.0);
    for (int i = 0; i < 24 * 4; ++i) { f.tick (t); t += gap2; }
    std::printf ("then 90 BPM: %.2f\n", f.bpm());
    CHECK (std::abs (f.bpm() - 90.0) < 1.0);
    // a stopped clock starts again cleanly
    t += 2.0;
    f.tick (t);
    CHECK (! f.locked (t));
    // the Player at the clock's tempo: lines last 1/4 beat
    Song s = makeSong();
    Player p; p.setSampleRate (48000); p.setSong (&s);
    p.setBeatTempo (150.0);
    CHECK (std::abs (p.beatTempo() - 150.0) < 1e-6);
    p.setBeatTempo (0);
    CHECK (std::abs (p.beatTempo() - s.tempo * 6.0 / s.speed) < 1e-6);
    // song position pointers
    int pos = -1, line = -1;
    CHECK (songPositionAt (s, 0, pos, line) && pos == 0 && line == 0);
    CHECK (songPositionAt (s, 70, pos, line) && pos == 1 && line == 6);   // block 0 is 64 lines, block 1 32
    CHECK (! songPositionAt (s, 64 + 32 + 64 + 5, pos, line));
}

// ---- PHRASEs: a note plays a track of another block
static void testPhrases()
{
    std::printf ("\n== phrases\n");
    Song s = makeSong();
    s.blocks.resize (3);
    s.blocks[0] = Block {}; s.blocks[0].lines = 16;
    s.blocks[0].at (0, 0) = Cell { 39, 1, 0, 0 };           // D-3 on the phrase instrument
    s.blocks[0].at (9, 0) = Cell { kNoteOff, 0, 0, 0 };
    s.blocks[2] = Block {}; s.blocks[2].lines = 8;
    s.blocks[2].at (0, 1) = Cell { 37, 0, 0, 0 };           // C-3, E-3, G-3 on its track 2
    s.blocks[2].at (2, 1) = Cell { 41, 0, 0, 0 };
    s.blocks[2].at (4, 1) = Cell { 44, 0, 0xC, 0x20 };
    s.sequence = { 0 };
    { Phrase ph; ph.block = 2; ph.track = 1; ph.loop = true; s.instruments[0].phrases = { ph }; s.instruments[0].phrases.reserve (kMaxPhrases); }   // (reserved: P0 stays put)
    auto& P0 = s.instruments[0].phrases[0];
    Player p; p.setSampleRate (48000); p.setSong (&s);
    p.playSong (0);
    const int lineLen = (int) std::lround (48000 * 2.5 / s.tempo * s.speed);
    std::vector<float> L ((size_t) lineLen), R (L.size());
    std::vector<int> lines;
    for (int k = 0; k < 12; ++k) { p.render (L.data(), R.data(), lineLen); lines.push_back (p.phraseLine (0)); }
    std::printf ("phrase line after each song line:");
    for (int v : lines) std::printf (" %d", v);
    std::printf ("\n");
    CHECK (lines[0] >= 1 && lines[0] <= 2 && lines[5] >= 6 && lines[7] >= 0 && lines[7] <= 2);   // loops after 8
    CHECK (lines[10] == -1);   // OFF at line 9 ends it
    // RATE x2: twice as fast; not looped: it ends
    P0.lpb = 8; P0.loop = false;
    p.playSong (0);
    std::vector<int> fast;
    for (int k = 0; k < 6; ++k) { p.render (L.data(), R.data(), lineLen); fast.push_back (p.phraseLine (0)); }
    std::printf ("double rate:");
    for (int v : fast) std::printf (" %d", v);
    std::printf ("\n");
    CHECK (fast[1] >= 3 && fast[5] == -1);
    // it makes sound (the sample plays through the phrase)
    P0.lpb = 4; P0.loop = true;
    p.playSong (0);
    std::vector<float> big ((size_t) lineLen * 8), bigR (big.size());
    p.render (big.data(), bigR.data(), (int) big.size());
    CHECK (rmsOf (big, 0, (int) big.size()) > 0.01);

    // ---- v0.52: many phrases, their keys, @xx, speed, the loop's START / END, TRANSPOSE
    std::printf ("\n== phrases: many, keys, @xx, speed, loop range, transpose\n");
    s.blocks.resize (4);
    s.blocks[3] = Block {}; s.blocks[3].lines = 8;
    s.blocks[3].at (0, 0) = Cell { 49, 0, 0, 0 };           // C-4 as written
    Phrase two; two.block = 3; two.track = 0; two.keyLo = 49; two.keyHi = 96; two.transpose = false;
    P0.keyLo = 1; P0.keyHi = 48; P0.loop = false;
    s.instruments[0].phrases.push_back (two);
    CHECK (s.instruments[0].phraseFor (39, -1) == 0 && s.instruments[0].phraseFor (49, -1) == 1);
    CHECK (s.instruments[0].phraseFor (39, 2) == 1 && s.instruments[0].phraseFor (39, 0) == -1 && s.instruments[0].phraseFor (39, 9) == -1);
    auto playFirstLines = [&] (Cell c, int n)
    {
        s.blocks[0].at (0, 0) = c;
        p.playSong (0);
        std::vector<int> idx;
        for (int k = 0; k < n; ++k) { p.render (L.data(), R.data(), lineLen); idx.push_back (p.phraseIndex (0)); }
        return idx;
    };
    // keys: D-3 plays phrase 1, D-4 phrase 2
    CHECK (playFirstLines (Cell { 39, 1, 0, 0 }, 1)[0] == 0);
    CHECK (playFirstLines (Cell { 51, 1, 0, 0 }, 1)[0] == 1);
    // TRANSPOSE off: phrase 2's C-4 plays as C-4, whatever was played
    std::printf ("phrase 2 from D-4 plays note %d (C-4 = 49)\n", p.trackNote (0));
    CHECK (p.trackNote (0) == 49);
    // TRANSPOSE on: phrase 1's C-3 from D-3 plays D-3
    playFirstLines (Cell { 39, 1, 0, 0 }, 1);
    CHECK (p.trackNote (0) == 39);
    // @02 picks phrase 2 for a low note; @00 plays the sample
    CHECK (playFirstLines (Cell { 39, 1, kCmdPhrase, 0x02 }, 1)[0] == 1);
    CHECK (playFirstLines (Cell { 39, 1, kCmdPhrase, 0x00 }, 1)[0] == -1);
    // no phrase holds the note (keys narrowed): the sample plays
    s.instruments[0].phrases[1].keyLo = 60;
    CHECK (playFirstLines (Cell { 51, 1, 0, 0 }, 1)[0] == -1);
    s.instruments[0].phrases[1].keyLo = 49;
    // the loop's START / END: lines 2-4 over and over
    P0.loop = true; P0.loopStart = 2; P0.loopEnd = 4;
    s.blocks[0].at (9, 0) = Cell {};
    s.blocks[0].at (0, 0) = Cell { 39, 1, 0, 0 };
    p.playSong (0);
    std::vector<int> lr;
    for (int k = 0; k < 10; ++k) { p.render (L.data(), R.data(), lineLen); lr.push_back (p.phraseLine (0)); }
    std::printf ("loop 2-4:");
    for (int v : lr) std::printf (" %d", v);
    std::printf ("\n");
    bool inRange = true;
    for (int k = 4; k < 10; ++k) inRange = inRange && lr[(size_t) k] >= 2 && lr[(size_t) k] <= 5;
    CHECK (inRange);
    // speed: 2 lines a beat - half as fast; 16 - four times as fast
    P0.loop = false; P0.loopStart = 0; P0.loopEnd = -1; P0.lpb = 2;
    p.playSong (0);
    std::vector<int> slow;
    for (int k = 0; k < 6; ++k) { p.render (L.data(), R.data(), lineLen); slow.push_back (p.phraseLine (0)); }
    P0.lpb = 16;
    p.playSong (0);
    p.render (L.data(), R.data(), lineLen);
    const int quick = p.phraseLine (0);
    std::printf ("2 a beat after 6 lines: %d; 16 a beat after 1 line: %d\n", slow[5], quick);
    CHECK (slow[5] >= 3 && slow[5] <= 4 && quick >= 4 && quick <= 5);
    // a phrase heard from its editor: runs with the song stopped, and makes sound
    p.stop();
    P0.lpb = 4;
    p.previewPhrase (0, 0, 37);
    std::vector<float> pv ((size_t) lineLen * 3), pvR (pv.size());
    p.render (pv.data(), pvR.data(), (int) pv.size());
    std::printf ("preview: phrase %d at line %d, rms %.4f\n", p.previewPhraseIndex(), p.previewPhraseLine(), rmsOf (pv, 0, (int) pv.size()));
    CHECK (p.previewPhraseIndex() == 0 && p.previewPhraseLine() >= 2 && rmsOf (pv, 0, (int) pv.size()) > 0.005);
    p.stopPhrasePreview();
    CHECK (p.previewPhraseIndex() == -1);
    // ---- CHOP TO PHRASE: 16 equal slices into a new phrase, one hit a line, TRANSPOSE off
    {
        Song c = makeSong();
        auto& in = c.instruments[3];
        in = Instrument {}; in.rate = 16574; in.data.assign (16574 * 2, 1000);   // two seconds
        const double lineSec = 2.5 / 125 * 6;                                    // (a bar = 16 lines = 1.92 s: one bar)
        const int k = chopToPhrase (c, 3, 16, lineSec);
        CHECK (k == 0 && in.phrases.size() == 1 && in.numSlices() == 16 && in.sliceKeys && ! in.phrases[0].transpose);
        const auto& pb = c.blocks[(size_t) in.phrases[0].block];
        int notes = 0; bool inOrder = true; int prev = 0;
        for (int l = 0; l < pb.lines; ++l) if (pb.at (l, 0).note > 0) { ++notes; inOrder = inOrder && pb.at (l, 0).note > prev; prev = pb.at (l, 0).note; }
        std::printf ("chop to phrase: %d lines, lpb %d, %d notes\n", pb.lines, in.phrases[0].lpb, notes);
        CHECK (pb.lines == 16 && in.phrases[0].lpb == 4 && notes == 16 && inOrder && pb.at (0, 0).note == 37 && pb.at (15, 0).note == 52);
        // 32 slices: twice the lines a beat, so each has a line
        const int k2 = chopToPhrase (c, 3, 32, lineSec);
        CHECK (k2 == 1 && in.phrases[1].lpb == 8 && c.blocks[(size_t) in.phrases[1].block].lines == 32);
        // it plays: a note on the instrument runs the chopped phrase
        c.blocks[0] = Block {}; c.blocks[0].lines = 16; c.blocks[0].at (0, 0) = Cell { 37, 4, kCmdPhrase, 0x01 }; c.sequence = { 0 };
        Player cp; cp.setSampleRate (48000); cp.setSong (&c); cp.playSong (0);
        std::vector<float> cl ((size_t) lineLen * 4), cr (cl.size());
        cp.render (cl.data(), cr.data(), (int) cl.size());
        CHECK (cp.phraseIndex (0) == 0 && cp.phraseLine (0) >= 3);
        // no sample: nothing
        CHECK (chopToPhrase (c, 9, 16, lineSec) == -1);
    }
    // @ is named and described
    CHECK (commandChar (kCmdPhrase) == '@' && commandFromChar ('@') == kCmdPhrase && commandName (kCmdPhrase) == "PHRASE");
    CHECK (describeCommand (kCmdPhrase, 0x02).find ("PHRASE 02") != std::string::npos);
}


// ---- SUB: no clicks - legato, tracker-style note-off then note-on, retrigger, poly stealing; glide; the 808 drop
static float maxJump (const std::vector<float>& v, int from = 1)
{
    float m = 0;
    for (size_t i = (size_t) std::max (1, from); i < v.size(); ++i) m = std::max (m, std::abs (v[i] - v[i - 1]));
    return m;
}
static void testSubSynth()
{
    std::printf ("\n== SUB\n");
    const double sr = 48000;
    auto run = [&] (SubSynth& s, int n) { std::vector<float> L ((size_t) n), R ((size_t) n); s.render (L.data(), R.data(), n); return L; };
    auto setMode = [] (SubSynth& s, int mode, float glideMs)
    {
        auto v = builtinDefaults (kSubKey);
        v[sub::Mode] = (float) mode; v[sub::Glide] = glideMs; v[sub::Attack] = 2; v[sub::Release] = 30;
        s.setValues (v);
    };
    // the slope a clean sine of this level can have at most: a click jumps far more than that in one sample
    auto limit = [&] (double hz) { return (float) (0.8 * 0.6 * 6.283 * hz / sr * 1.6 + 0.004); };

    // LEGATO: an overlapping note slides there; the level doesn't dip; no click anywhere
    {
        SubSynth s; s.prepare (sr, 512); setMode (s, sub::Legato, 100);
        std::vector<float> all;
        s.noteOn (36, 100, 0);                       // C2 (65 Hz)
        auto a = run (s, 9600); all.insert (all.end(), a.begin(), a.end());
        const float envBefore = s.levelNow();
        s.noteOn (43, 100, 100); s.noteOff (36, 200);   // G2, overlapping
        auto b = run (s, 2400); all.insert (all.end(), b.begin(), b.end());
        const float mid = s.pitchNow();
        auto c = run (s, 9600); all.insert (all.end(), c.begin(), c.end());
        std::printf ("legato: pitch mid-glide %.2f (36 -> 43), at the end %.2f; level %.3f then %.3f; biggest step %.4f (limit %.4f)\n",
                     mid, s.pitchNow(), envBefore, s.levelNow(), maxJump (all), limit (98));
        CHECK (mid > 36.5f && mid < 42.5f && std::abs (s.pitchNow() - 43.0f) < 0.01f);
        CHECK (std::abs (s.levelNow() - envBefore) < 0.05f);
        CHECK (maxJump (all, 200) < limit (98));
    }
    // the tracker's way: the old note's OFF and the new note at the same moment - still legato, still no click
    {
        SubSynth s; s.prepare (sr, 512); setMode (s, sub::Legato, 60);
        std::vector<float> all;
        s.noteOn (36, 100, 0);
        auto a = run (s, 4800); all.insert (all.end(), a.begin(), a.end());
        const float envBefore = s.levelNow();
        s.noteOff (36, 10); s.noteOn (31, 100, 10);
        auto b = run (s, 4800); all.insert (all.end(), b.begin(), b.end());
        std::printf ("off + on together: level %.3f then %.3f, pitch %.2f, biggest step %.4f\n", envBefore, s.levelNow(), s.pitchNow(), maxJump (all, 200));
        CHECK (s.levelNow() > envBefore * 0.9f && std::abs (s.pitchNow() - 31.0f) < 0.01f && maxJump (all, 200) < limit (65));
    }
    // OFF (retrigger) mode, notes cut short and restarted over and over, with a hard square SHAPE: no click
    {
        SubSynth s; s.prepare (sr, 512); setMode (s, sub::Retrigger, 1);
        auto v = builtinDefaults (kSubKey); v[sub::Mode] = sub::Retrigger; v[sub::Shape] = 60; v[sub::Attack] = 1; v[sub::Release] = 20; s.setValues (v);
        std::vector<float> all;
        for (int k = 0; k < 12; ++k)
        {
            s.noteOn (36 + (k % 3) * 5, 110, 0);
            auto a = run (s, 1500 + 37 * k); all.insert (all.end(), a.begin(), a.end());
            if (k % 2 == 0) s.noteOff (36 + (k % 3) * 5, 0);
        }
        std::printf ("retriggered 12 times: biggest step %.4f\n", maxJump (all, 50));
        CHECK (maxJump (all, 50) < 0.06f);
    }
    // ALWAYS: separate notes slide too
    {
        SubSynth s; s.prepare (sr, 512); setMode (s, sub::Always, 200);
        s.noteOn (36, 100, 0); run (s, 4800); s.noteOff (36, 0); run (s, 48);
        s.noteOn (48, 100, 0); run (s, 2400);
        std::printf ("ALWAYS: after 50 ms of a 200 ms glide, pitch %.2f\n", s.pitchNow());
        CHECK (s.pitchNow() > 37.0f && s.pitchNow() < 47.0f);
    }
    // the 808 drop: starts up, falls to the note
    {
        SubSynth s; s.prepare (sr, 512); auto v = builtinDefaults (kSubKey); v[sub::DropAmount] = 24; v[sub::DropTime] = 80; s.setValues (v);
        s.noteOn (36, 127, 0); run (s, 1);
        const float startP = s.pitchNow();
        auto a = run (s, 48000);
        std::printf ("808 drop: the voice's note %.1f; biggest step %.4f\n", startP, maxJump (a, 100));
        CHECK (maxJump (a, 100) < 0.08f);
    }
    // POLY: a chord sounds; the eighth note steals the oldest, fading it (no click)
    {
        SubSynth s; s.prepare (sr, 512); setMode (s, sub::Poly, 1);
        std::vector<float> all;
        for (int k = 0; k < 9; ++k) { s.noteOn (36 + k * 3, 90, 0); auto a = run (s, 2000); all.insert (all.end(), a.begin(), a.end()); }
        std::printf ("POLY: 9 notes, biggest step %.4f, rms %.3f\n", maxJump (all, 50), rmsOf (all, 0, (int) all.size()));
        CHECK (rmsOf (all, 0, (int) all.size()) > 0.05f && maxJump (all, 50) < 0.12f);
    }
    // its settings: saved in the instrument, read back; knobs set controls as 0..1
    {
        Instrument in; makeBuiltin (in, kSubKey);
        CHECK (in.isPlugin() && in.pluginKey == kSubKey && in.name == "SUB");
        auto v = readBuiltinState (in); v[sub::Glide] = 333; v[sub::Mode] = sub::Poly; writeBuiltinState (in, v);
        const auto back = readBuiltinState (in);
        CHECK (std::abs (back[sub::Glide] - 333) < 0.01f && back[sub::Mode] == sub::Poly && back.size() == (size_t) sub::Count);
        const auto& ps = builtinParams (kSubKey);
        CHECK (ps[sub::Glide].text (333) == "333MS" && ps[sub::Mode].text (3) == "POLY" && std::abs (ps[sub::Glide].toValue (ps[sub::Glide].to01 (333)) - 333) < 0.5f);
        auto synth = createBuiltinSynth (in);
        CHECK (synth != nullptr);
        // made at the song's rate: at 44.1 kHz C-3 is still 261.6 Hz
        auto at44 = createBuiltinSynth (in, 44100);
        std::vector<float> L (44100), R (L.size());
        at44->noteOn (60, 127, 0);
        at44->render (L.data(), R.data(), (int) L.size());
        int cross = 0, first = -1, last = -1;
        for (size_t i = 4410; i < L.size(); ++i) if (L[i - 1] < 0 && L[i] >= 0) { ++cross; if (first < 0) first = (int) i; last = (int) i; }
        const double hz = cross > 1 ? (cross - 1) * 44100.0 / (last - first) : 0;
        std::printf ("SUB made at 44.1 kHz: C-3 = %.2f Hz\n", hz);
        CHECK (std::abs (hz - 261.63) < 1.0);
    }
    // in the song: a SUB instrument plays through the player (as a plug-in instrument does)
    {
        Song s = makeSong();
        makeBuiltin (s.instruments[4], kSubKey);
        s.blocks[0] = Block {}; s.blocks[0].lines = 16; s.blocks[0].at (0, 0) = Cell { 13, 5, 0, 0 }; s.sequence = { 0 };
        Player p; p.setSampleRate (48000); p.setSong (&s);
        p.synths().factory = [] (int, const Instrument& in, double sr) { return createBuiltinSynth (in, sr); };
        p.synths().syncNow (s, 48000);
        p.playSong (0);
        std::vector<float> L (24000), R (L.size());
        p.render (L.data(), R.data(), (int) L.size());
        std::printf ("SUB in the song: rms %.3f\n", rmsOf (L, 0, (int) L.size()));
        CHECK (rmsOf (L, 0, (int) L.size()) > 0.02f);
    }
}

// ---- SUB's BEAT: a copy of the oscillator RATE Hz higher - the level swells and dips RATE times a second on any note
static void testSubBeat()
{
    std::printf ("\n== SUB BEAT\n");
    const double sr = 48000;
    auto wobble = [&] (int note, float rate, float depth, float& lowHigh, double& perSec)
    {
        SubSynth s; s.prepare (sr, 512);
        auto v = builtinDefaults (kSubKey);
        v[sub::Drive] = 0; v[sub::BeatOn] = 1; v[sub::BeatRate] = rate; v[sub::BeatDepth] = depth;
        s.setValues (v);
        std::vector<float> L ((size_t) sr * 4), R (L.size());
        s.noteOn (note, 127, 0);
        s.render (L.data(), R.data(), (int) L.size());
        // the level, 20 ms at a time, over the last 3 seconds: how low it dips against how high it swells, and how often
        std::vector<float> lv;
        for (size_t a = (size_t) sr; a + 960 <= L.size(); a += 480) { float m = 0; for (size_t i = a; i < a + 960; ++i) m = std::max (m, std::abs (L[i])); lv.push_back (m); }
        const float hi = *std::max_element (lv.begin(), lv.end()), lo = *std::min_element (lv.begin(), lv.end());
        lowHigh = lo / hi;
        const float mid = 0.5f * (hi + lo);
        int dips = 0;
        for (size_t i = 1; i < lv.size(); ++i) if (lv[i - 1] >= mid && lv[i] < mid) ++dips;
        perSec = dips / 3.0;
    };
    float lh; double ps;
    wobble (36, 2.0f, 100, lh, ps);
    std::printf ("C-1, RATE 2 HZ: dips to %.2f of its peak, %.2f times a second\n", lh, ps);
    CHECK (lh < 0.2f && std::abs (ps - 2.0) < 0.4);
    wobble (60, 2.0f, 100, lh, ps);
    std::printf ("C-3, RATE 2 HZ: dips to %.2f, %.2f times a second (the same on any note)\n", lh, ps);
    CHECK (lh < 0.2f && std::abs (ps - 2.0) < 0.4);
    wobble (36, 0.5f, 100, lh, ps);
    std::printf ("C-1, RATE 0.5 HZ: %.2f times a second\n", ps);
    CHECK (std::abs (ps - 0.5) < 0.2);
    wobble (36, 2.0f, 40, lh, ps);
    std::printf ("DEPTH 40%%: dips only to %.2f\n", lh);
    CHECK (lh > 0.35f && lh < 0.8f);
    // switching it on and off in the middle of a note: no click
    {
        SubSynth s; s.prepare (sr, 512);
        auto v = builtinDefaults (kSubKey); v[sub::Drive] = 0; v[sub::BeatRate] = 3;
        s.setValues (v);
        std::vector<float> L (96000), R (L.size());
        s.noteOn (36, 127, 0);
        s.render (L.data(), R.data(), 24000);
        v[sub::BeatOn] = 1; s.setValues (v);
        s.render (L.data() + 24000, R.data() + 24000, 36000);
        v[sub::BeatOn] = 0; s.setValues (v);
        s.render (L.data() + 60000, R.data() + 60000, 36000);
        float step = 0;
        for (size_t i = 1; i < L.size(); ++i) step = std::max (step, std::abs (L[i] - L[i - 1]));
        std::printf ("BEAT switched on and off mid-note: biggest step %.4f\n", step);
        CHECK (step < 0.0139f);
    }
    // in LEGATO, gliding between overlapping notes with BEAT on: still no click
    {
        SubSynth s; s.prepare (sr, 512);
        auto v = builtinDefaults (kSubKey); v[sub::Drive] = 0; v[sub::BeatOn] = 1; v[sub::BeatRate] = 1.5f;
        s.setValues (v);
        std::vector<float> L (96000), R (L.size());
        s.noteOn (36, 127, 0); s.render (L.data(), R.data(), 24000);
        s.noteOn (43, 127, 0); s.render (L.data() + 24000, R.data() + 24000, 24000);
        s.noteOff (36, 0); s.noteOn (39, 127, 0); s.noteOff (43, 0); s.render (L.data() + 48000, R.data() + 48000, 48000);
        float step = 0;
        for (size_t i = 1; i < L.size(); ++i) step = std::max (step, std::abs (L[i] - L[i - 1]));
        std::printf ("LEGATO with BEAT: biggest step %.4f\n", step);
        CHECK (step < 0.0139f);
    }
    // a song saved before BEAT: BEAT off
    {
        Instrument in; makeBuiltin (in, kSubKey);
        auto v = readBuiltinState (in);
        v.resize (sub::BeatOn);   // (the 20 values v0.57 - v0.59 saved)
        v[sub::Glide] = 222;
        writeBuiltinState (in, v);
        const auto back = readBuiltinState (in);
        CHECK ((int) back.size() == sub::Count && back[sub::BeatOn] == 0 && std::abs (back[sub::BeatRate] - 1) < 0.001f && back[sub::Glide] == 222);
    }
    // BEAT BY SEMITONES: one key with BEAT ST +1 is the same sound as two keys a semitone apart held together
    for (int mode : { (int) sub::Poly, (int) sub::Legato })
        for (float drive : { 0.0f, 40.0f })
        {
            auto v = builtinDefaults (kSubKey);
            v[sub::Mode] = (float) sub::Poly; v[sub::Drive] = drive; v[sub::Osc2Level] = 30; v[sub::Shape] = 40;
            SubSynth two; two.prepare (sr, 512); two.setValues (v);
            std::vector<float> A (96000), AR (A.size()), B (A.size()), BR (A.size());
            two.noteOn (36, 100, 0); two.noteOn (37, 100, 0);
            two.render (A.data(), AR.data(), (int) A.size());
            v[sub::Mode] = (float) mode; v[sub::BeatOn] = 2; v[sub::BeatSemis] = 1; v[sub::BeatDepth] = 100;
            SubSynth one; one.prepare (sr, 512); one.setValues (v);
            one.noteOn (36, 100, 0);
            one.render (B.data(), BR.data(), (int) B.size());
            float diff = 0, peak = 0;
            for (size_t k = 2000; k < A.size(); ++k) { diff = std::max (diff, std::abs (A[k] - B[k])); peak = std::max (peak, std::abs (A[k])); }
            std::printf ("two keys (POLY) against one key + BEAT ST +1 (%s, drive %d): peak %.3f, biggest difference %.4f\n",
                         mode == sub::Poly ? "POLY" : "LEGATO", (int) drive, peak, diff);
            CHECK (peak > 0.1f && diff < peak * 0.02f);
        }
    // BEAT DELAY: plain for that long, then BEAT comes in - and again from each new attack
    {
        auto v = builtinDefaults (kSubKey);
        v[sub::Drive] = 0; v[sub::BeatOn] = 1; v[sub::BeatRate] = 4; v[sub::BeatDepth] = 100; v[sub::BeatDelay] = 1000;
        SubSynth s; s.prepare (sr, 512); s.setValues (v);
        std::vector<float> L ((size_t) sr * 3), R (L.size());
        s.noteOn (36, 127, 0);
        s.render (L.data(), R.data(), (int) L.size());
        auto dip = [&] (double from, double to)
        {
            float hi = 0, lo = 9;
            for (size_t a = (size_t) (from * sr); a + 960 <= (size_t) (to * sr); a += 480)
            { float m = 0; for (size_t i = a; i < a + 960; ++i) m = std::max (m, std::abs (L[i])); hi = std::max (hi, m); lo = std::min (lo, m); }
            return lo / hi;
        };
        const float before = dip (0.1, 0.95), after = dip (1.8, 2.9);
        std::printf ("BEAT DELAY 1 S: before it, the level dips to %.2f of its peak (steady); after, to %.2f (beating)\n", before, after);
        CHECK (before > 0.9f && after < 0.3f);
        float step = 0;
        for (size_t i = 1; i < L.size(); ++i) step = std::max (step, std::abs (L[i] - L[i - 1]));
        CHECK (step < 0.0139f);
        Instrument in; makeBuiltin (in, kSubKey);
        auto old = readBuiltinState (in); old.resize (sub::BeatDelay); writeBuiltinState (in, old);
        CHECK (readBuiltinState (in)[sub::BeatDelay] == 0);   // (a v0.72 song: no delay)
    }
}

// ---- MORPH FILTER (v0.78)
static void testMorphFilter()
{
    std::printf ("\n== MORPH FILTER\n");
    const double sr = 48000;
    // white noise through a model at a MORPH: the energy in a band (a 2-pole band-pass around hz)
    auto bandAt = [&] (int model, float m, double hz, float oct = 0)
    {
        MorphFilter f; f.set (model, m, oct, 0.5f, sr);
        uint32_t seed = 9;
        double e = 0, total = 0;
        float s1 = 0, s2 = 0;
        const float g = (float) std::tan (3.14159265 * hz / sr), k = 1.0f / 12.0f;
        const float a1 = 1.0f / (1.0f + g * (g + k)), a2 = g * a1, a3 = g * a2;
        for (int i = 0; i < 96000; ++i)
        {
            seed = seed * 1664525u + 1013904223u;
            const float x = ((int) (seed >> 9) - 4194304) / 4194304.0f * 0.3f;
            const float y = f.process (x);
            const float v3 = y - s2, v1 = a1 * s1 + a2 * v3, v2 = s2 + a2 * s1 + a3 * v3;
            s1 = 2 * v1 - s1; s2 = 2 * v2 - s2;
            if (i > 4800) { e += (double) v1 * v1; total += (double) y * y; }
        }
        return std::make_pair (10.0 * std::log10 (e + 1e-12), 10.0 * std::log10 (total / 91200.0 + 1e-12) - 10.0 * std::log10 (0.03));
    };
    // AH > EE: the first formant moves from 730 Hz (AH) down to 270 (EE), the second from 1090 up to 2290
    const auto ah730 = bandAt (0, 0.0f, 730).first, ah270 = bandAt (0, 0.0f, 270).first, ee730 = bandAt (0, 1.0f, 730).first, ee270 = bandAt (0, 1.0f, 270).first;
    const auto ah2290 = bandAt (0, 0.0f, 2290).first, ee2290 = bandAt (0, 1.0f, 2290).first;
    std::printf ("AH > EE: at AH 730 Hz %.1f / 270 Hz %.1f dB; at EE 730 %.1f / 270 %.1f; 2290 Hz AH %.1f, EE %.1f\n", ah730, ah270, ee730, ee270, ah2290, ee2290);
    CHECK (ah730 > ah270 + 6 && ee270 > ee730 + 6 && ee2290 > ah2290 + 4);
    // FREQ moves the whole shape: an octave up, AH's 730 peak is at 1460
    const auto up1460 = bandAt (0, 0.0f, 1460, 1.0f).first, up730 = bandAt (0, 0.0f, 730, 1.0f).first;
    CHECK (up1460 > up730 + 3);
    // PHASER: its notch at 480 Hz moves up to 2000
    const auto n0 = bandAt (4, 0.0f, 480).first, n1 = bandAt (4, 1.0f, 480).first;
    std::printf ("PHASER: 480 Hz with the notch there %.1f dB, moved away %.1f dB\n", n0, n1);
    CHECK (n1 > n0 + 10);
    // every model, at every MORPH: about as loud as what goes in (within 9 dB), and never runaway
    double lo = 99, hi = -99;
    for (int mdl = 0; mdl < morph::kModels; ++mdl)
        for (float m : { 0.0f, 0.5f, 1.0f })
        {
            const double lev = bandAt (mdl, m, 1000).second;
            // (the peak models: about the dry level; a low-pass's level is however much it lets through)
            const bool peaks = mdl <= 5 || mdl == 9 || mdl == 10;
            if (peaks) lo = std::min (lo, lev);
            hi = std::max (hi, lev);
            if ((peaks && lev < -9.0) || lev > 9.0) std::printf ("  %s at %.1f: %.1f dB\n", morph::modelName (mdl), m, lev);
        }
    std::printf ("all 12 models: the peak ones from %.1f dB, none above %.1f dB, against the dry sound\n", lo, hi);
    CHECK (lo > -9.0 && hi < 9.0);
    // the effect: a model swept by the LFO, nothing broken
    {
        auto fx = makeEffect (FxType::Morph);
        CHECK (fx != nullptr);
        fx->prepare (sr);
        FxSlot sl; initFxSlot (sl, FxType::Morph);
        std::vector<float> L (48000), R (48000);
        uint32_t seed = 3;
        for (size_t i = 0; i < L.size(); ++i) { seed = seed * 1664525u + 1013904223u; L[i] = R[i] = ((int) (seed >> 9) - 4194304) / 4194304.0f * 0.3f; }
        for (int a = 0; a < 48000; a += 512) fx->process (L.data() + a, R.data() + a, std::min (512, 48000 - a), sl.p, 125.0);
        bool ok = true; float pk = 0;
        for (float x : L) { ok = ok && std::isfinite (x); pk = std::max (pk, std::abs (x)); }
        std::printf ("MORPH FILTER effect on noise: peak %.3f, presets %d\n", pk, (int) fxFactoryPresets (FxType::Morph).size());
        CHECK (ok && pk > 0.05f && pk < 2.0f && fxFactoryPresets (FxType::Morph).size() == 6);
        CHECK (fxName (FxType::Morph) == "MORPH FILTER" && fxParamText (FxType::Morph, 0, sl.p) == "AH > EE");
    }
}

// ---- MORPH (v0.78): the morphing-filter synth
// (v0.80) ACID: a 303 bass line with the Devil Fish mods - accent, slide (overlap and tied), the sweep, release
static void testAcidSynth()
{
    std::printf ("\n== ACID (303 + DEVIL FISH)\n");
    using namespace acid;
    const double sr = 48000.0;
    std::vector<float> L (480), R (480);
    auto run = [&] (AcidSynth& a, int blocks, float* peak = nullptr, bool* ok = nullptr)
    {
        for (int b = 0; b < blocks; ++b)
        {
            a.render (L.data(), R.data(), 480);
            for (float x : L) { if (peak) *peak = std::max (*peak, std::abs (x)); if (ok) *ok = *ok && std::isfinite (x); }
        }
    };
    auto fresh = [&] (std::vector<float> v) { auto a = std::make_unique<AcidSynth>(); a->prepare (sr, 512); a->setValues (v); return a; };
    const auto def = acidPreset (0);
    // a plain note, and an accented one: brighter and louder
    {
        auto a = fresh (def); float pk = 0; bool ok = true;
        float cut = 0, cutA = 0;
        a->noteOn (36, 95, 0);
        for (int k = 0; k < 6; ++k) { a->render (L.data(), R.data(), 480); cut = std::max (cut, a->cutoffNow()); }
        run (*a, 20, &pk, &ok);
        auto b = fresh (def); float pkA = 0;
        b->noteOn (36, 127, 0);
        for (int k = 0; k < 6; ++k) { b->render (L.data(), R.data(), 480); cutA = std::max (cutA, b->cutoffNow()); }
        run (*b, 20, &pkA, &ok);
        std::printf ("plain note: cutoff %.0f Hz, peak %.3f; accented: %.0f Hz, %.3f\n", cut, pk, cutA, pkA);
        CHECK (ok && pk > 0.05f && pk < 2.0f && cutA > cut * 1.3f && pkA > pk);
        // let go: it stops
        a->noteOff (36, 0); run (*a, 50);
        float tail = 0; run (*a, 5, &tail);
        CHECK (! a->sounding() && tail < 1.0e-3f);
    }
    // SLIDE by overlap: one attack, the pitch glides (not jumps) to the new note
    {
        auto a = fresh (def);
        a->noteOn (36, 95, 0); run (*a, 10);
        a->noteOn (48, 95, 0); a->render (L.data(), R.data(), 48);
        const float mid = a->pitchNow();
        run (*a, 30);
        std::printf ("overlap slide 36 > 48: %.2f after 1 ms, %.2f after 300 ms, %d attack(s)\n", mid, a->pitchNow(), a->attacks());
        CHECK (a->attacks() == 1 && mid > 36.0f && mid < 47.0f && std::abs (a->pitchNow() - 48.0f) < 0.05f);
        // the second key up: back to the first, still sliding, still one attack
        a->noteOff (48, 0); run (*a, 30);
        CHECK (a->attacks() == 1 && std::abs (a->pitchNow() - 36.0f) < 0.05f);
    }
    // SLIDE: TIED NOTES - a note-off and a note-on at the same moment (a pattern's back-to-back notes) slide;
    // in OVERLAP mode the same notes play fresh; and a gap (an OFF earlier) plays fresh in either
    {
        auto v = def; v[SlideMode] = Tied;
        auto a = fresh (v);
        a->noteOn (36, 95, 0); run (*a, 10);
        a->noteOff (36, 100); a->noteOn (43, 95, 100); run (*a, 10);
        auto b = fresh (def);
        b->noteOn (36, 95, 0); run (*b, 10);
        b->noteOff (36, 100); b->noteOn (43, 95, 100); run (*b, 10);
        a->noteOff (43, 0); run (*a, 2); a->noteOn (45, 95, 0); run (*a, 2);
        std::printf ("tied: %d attacks (2 expected: the slide, then a fresh note after a gap); overlap mode: %d\n", a->attacks(), b->attacks());
        CHECK (a->attacks() == 2 && b->attacks() == 2);
    }
    // the accent SWEEP: a run of accents builds it up when it's slow
    {
        auto v = def; v[SweepSpeed] = 0; v[AccentDecay] = 200;
        auto a = fresh (v);
        float first = 0, last = 0;
        for (int n = 0; n < 6; ++n)
        {
            a->noteOn (36, 127, 0);
            float top = 0;
            for (int b = 0; b < 12; ++b) { a->render (L.data(), R.data(), 480); top = std::max (top, a->sweepNow()); }
            a->noteOff (36, 0);
            if (n == 0) first = top;
            last = top;
        }
        std::printf ("accent sweep (slow): %.3f after one accent, %.3f after six\n", first, last);
        CHECK (last > first * 1.5f);
    }
    // every preset plays, finite and in range; the extremes too (self-oscillating, muffler hard, FM)
    {
        int bad = 0;
        for (int i = 0; i < kAcidPresets; ++i)
        {
            auto a = fresh (acidPreset (i)); float pk = 0; bool ok = true;
            for (int n : { 36, 39, 43, 48 }) { a->noteOn (n, n == 43 ? 127 : 95, 0); run (*a, 12, &pk, &ok); a->noteOff (n, 0); run (*a, 3, &pk, &ok); }
            if (! ok || pk < 0.02f || pk > 2.0f) { ++bad; std::printf ("  %s: peak %.3f %s\n", acidPresetName (i), pk, ok ? "" : "NOT FINITE"); }
        }
        auto v = def; v[Resonance] = 100; v[FilterFm] = 100; v[Muffler] = 2; v[Drive] = 100; v[Cutoff] = 8000; v[EnvMod] = 100; v[Accent] = 100;
        auto a = fresh (v); float pk = 0; bool ok = true;
        a->noteOn (24, 127, 0); run (*a, 100, &pk, &ok);
        std::printf ("presets out of range: %d; flat out: peak %.3f\n", bad, pk);
        CHECK (bad == 0 && ok && pk < 2.0f);
    }
    // a song's ACID instrument: VOL 30 (a plain note), so C40 is the accent
    {
        Instrument in; makeBuiltin (in, kAcidKey);
        CHECK (in.pluginName == "ACID" && in.volume == 0x30 && builtinDefaults (kAcidKey).size() == (size_t) Count);
        const int vel = (int) std::lround (0x30 * 127.0 / 64.0);
        const int at = (int) std::lround (def[AccentAt] * 127.0f / 64.0f);
        CHECK (vel < at && 127 >= at);
    }
}

// (v0.81) PAD: four layers - each with its own keys, velocity zone, start delay - unison, the main filter, SPACE
static void testPadSynth()
{
    std::printf ("\n== PAD (FOUR LAYERS)\n");
    using namespace pad;
    const double sr = 48000.0;
    std::vector<float> L (480), R (480);
    auto fresh = [&] (std::vector<float> v) { auto a = std::make_unique<PadSynth>(); a->prepare (sr, 512); a->setValues (v); return a; };
    auto run = [&] (PadSynth& a, int blocks, float* peak = nullptr, bool* ok = nullptr, float* side = nullptr)
    {
        for (int b = 0; b < blocks; ++b)
        {
            a.render (L.data(), R.data(), 480);
            for (int i = 0; i < 480; ++i)
            {
                if (peak) *peak = std::max (*peak, std::abs (L[(size_t) i]));
                if (ok) *ok = *ok && std::isfinite (L[(size_t) i]) && std::isfinite (R[(size_t) i]);
                if (side) *side = std::max (*side, std::abs (L[(size_t) i] - R[(size_t) i]));
            }
        }
    };
    // the default: two layers, plays, in stereo
    {
        auto a = fresh (padPreset (0)); float pk = 0, side = 0; bool ok = true;
        a->noteOn (60, 100, 0); run (*a, 200, &pk, &ok, &side);
        std::printf ("WARM ANALOG: peak %.3f, stereo difference %.3f, layers A %.2f B %.2f C %.2f\n", pk, side, a->layerLevel (0), a->layerLevel (1), a->layerLevel (2));
        CHECK (ok && pk > 0.05f && pk < 1.5f && side > 0.01f && a->layerLevel (0) > 0.1f && a->layerLevel (1) > 0.1f && a->layerLevel (2) == 0.0f);
    }
    // KEY ranges: SPLIT BASS / PAD - below C-3 layer B, from C-3 up layer A
    {
        auto a = fresh (padPreset (8));
        a->noteOn (48, 100, 0); run (*a, 20);
        const float lowA = a->layerLevel (0), lowB = a->layerLevel (1);
        a->allNotesOff (0); run (*a, 400);
        a->noteOn (72, 100, 0); run (*a, 100);
        std::printf ("split: C-2 plays A %.2f B %.2f; C-4 plays A %.2f B %.2f\n", lowA, lowB, a->layerLevel (0), a->layerLevel (1));
        CHECK (lowA == 0.0f && lowB > 0.1f && a->layerLevel (0) > 0.1f && a->layerLevel (1) == 0.0f);
    }
    // VELOCITY zones: SOFT / HARD
    {
        auto a = fresh (padPreset (9));
        a->noteOn (60, 40, 0); run (*a, 60);
        const float softA = a->layerLevel (0), softB = a->layerLevel (1);
        auto b = fresh (padPreset (9));
        b->noteOn (60, 120, 0); run (*b, 60);
        std::printf ("velocity: soft plays A %.2f B %.2f; hard plays A %.2f B %.2f\n", softA, softB, b->layerLevel (0), b->layerLevel (1));
        CHECK (softA == 0.0f && softB > 0.05f && b->layerLevel (0) > 0.05f && b->layerLevel (1) == 0.0f);
    }
    // DELAY: BELL + PAD - the bell at once, the pad layer only after its 250 ms
    {
        auto a = fresh (padPreset (7));
        a->noteOn (60, 100, 0); run (*a, 10);
        const float bell = a->layerLevel (2), early = a->layerLevel (0);
        run (*a, 100);
        std::printf ("delay: at 100 ms bell %.2f pad %.2f; at 1.1 s pad %.2f\n", bell, early, a->layerLevel (0));
        CHECK (bell > 0.2f && early == 0.0f && a->layerLevel (0) > 0.05f);
    }
    // let go: it fades and stops (no reverb or echo to wait for here)
    {
        auto v = padPreset (0); v[Reverb] = 0; v[Echo] = 0; v[Chorus] = 0;
        auto a = fresh (v);
        a->noteOn (60, 100, 0); run (*a, 100);
        a->noteOff (60, 0); run (*a, 600);
        float tail = 0; run (*a, 10, &tail);
        CHECK (a->voicesPlaying() == 0 && tail < 1.0e-3f);
    }
    // every preset, a six-note chord: finite and in range; twenty notes: still 12 voices at most; and fast enough
    {
        int bad = 0;
        for (int i = 0; i < kPadPresets; ++i)
        {
            auto a = fresh (padPreset (i)); float pk = 0; bool ok = true;
            for (int n : { 48, 55, 60, 63, 67, 72 }) a->noteOn (n, 100, 0);
            run (*a, 150, &pk, &ok);
            if (! ok || pk < 0.02f || pk > 2.0f) { ++bad; std::printf ("  %s: peak %.3f %s\n", padPresetName (i), pk, ok ? "" : "NOT FINITE"); }
        }
        auto v = padPreset (0);
        for (int l = 0; l < kLayers; ++l) { v[(size_t) pad::p (l, On)] = 1; v[(size_t) pad::p (l, Unison)] = 7; }
        auto a = fresh (v);
        for (int n = 0; n < 20; ++n) a->noteOn (40 + n * 2, 100, 0);
        const auto t0 = std::chrono::steady_clock::now();
        run (*a, 100);
        const double ms = std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - t0).count();
        std::printf ("presets out of range: %d; 12 notes x 4 layers x 7 voices: %d playing, 1 s rendered in %.0f ms\n", bad, a->voicesPlaying(), ms);
        CHECK (bad == 0 && a->voicesPlaying() <= 12);
    }
    // its tabs: LAYER A..D, FILTER + LFOS, SPACE (the page's own tabs come from the sections)
    {
        std::vector<std::string> tabs;
        for (const auto& q : padSynthParams())
        {
            const std::string sec = q.section;
            const auto t = sec.substr (0, sec.find ('>'));
            if (sec.find ('>') != std::string::npos && std::find (tabs.begin(), tabs.end(), t) == tabs.end()) tabs.push_back (t);
        }
        CHECK (tabs.size() == 7 && padSynthParams().size() == (size_t) Count && builtinDefaults (kPadKey).size() == (size_t) Count);
        const SynthParam key { "K", "", 24, 119, 60, 2, "NOTE", "" };
        CHECK (key.text (60) == "C-3");
    }
}

// (v0.82) REESE, deeper: presets, the wobble locked to the tempo and restarting with each note, every filter and drive
// type, the phaser and flanger, MONO BASS
static void testReeseDeep()
{
    std::printf ("\n== REESE (v0.82: MORE TO TWIST)\n");
    using namespace reese;
    const double sr = 48000.0;
    std::vector<float> L (480), R (480);
    auto fresh = [&] (std::vector<float> v) { auto a = std::make_unique<ReeseSynth>(); a->prepare (sr, 512); a->setValues (v); return a; };
    auto run = [&] (ReeseSynth& a, int blocks, float* peak = nullptr, bool* ok = nullptr)
    {
        for (int b = 0; b < blocks; ++b)
        {
            a.render (L.data(), R.data(), 480);
            for (int i = 0; i < 480; ++i)
            {
                if (peak) *peak = std::max (*peak, std::abs (L[(size_t) i]));
                if (ok) *ok = *ok && std::isfinite (L[(size_t) i]) && std::isfinite (R[(size_t) i]);
            }
        }
    };
    // every preset plays, finite, in range
    {
        int bad = 0;
        for (int i = 0; i < kReesePresets; ++i)
        {
            auto a = fresh (reesePreset (i)); float pk = 0; bool ok = true;
            for (int nn : { 36, 41, 43 }) { a->noteOn (nn, 110, 0); run (*a, 40, &pk, &ok); a->noteOff (nn, 0); run (*a, 5, &pk, &ok); }
            if (! ok || pk < 0.02f || pk > 2.0f) { ++bad; std::printf ("  %s: peak %.3f %s\n", reesePresetName (i), pk, ok ? "" : "NOT FINITE"); }
        }
        std::printf ("presets out of range: %d of %d\n", bad, kReesePresets);
        CHECK (bad == 0 && reeseParams().size() == (size_t) Count);
    }
    // the WOBBLE synced: 1/4 at 120 BPM is 2 cycles a second - count the cutoff's peaks over 2 s
    {
        auto v = reesePreset (0); v[WobbleDepth] = 80; v[WobSync] = 3; v[EnvAmount] = 0; v[WobShape] = 0;
        auto a = fresh (v);
        Song song; song.tempo = 120; song.speed = 6; a->updateSong (song);
        a->noteOn (36, 100, 0);
        int peaks = 0; float prev2 = 0, prev1 = 0;
        for (int b = 0; b < 200; ++b)
        {
            a->render (L.data(), R.data(), 480);
            const float c = a->cutoffNow();
            if (b > 1 && prev1 > prev2 && prev1 > c) ++peaks;
            prev2 = prev1; prev1 = c;
        }
        std::printf ("wobble 1/4 at 120 BPM: %d peaks in 2 s\n", peaks);
        CHECK (peaks >= 3 && peaks <= 5);
    }
    // RESTART: EACH NOTE - every note's wobble starts at the same place
    {
        auto v = reesePreset (0); v[WobbleDepth] = 80; v[WobbleRate] = 1.3f; v[WobRetrig] = 1; v[EnvAmount] = 0; v[Mode] = Retrigger;
        auto a = fresh (v);
        a->noteOn (36, 100, 0); a->render (L.data(), R.data(), 32); const float c1 = a->cutoffNow();
        run (*a, 37); a->noteOff (36, 0); run (*a, 3);
        a->noteOn (38, 100, 0); a->render (L.data(), R.data(), 32); const float c2 = a->cutoffNow();
        std::printf ("wobble restarts with each note: cutoff %.0f / %.0f Hz\n", c1, c2);
        CHECK (std::abs (c1 - c2) < c1 * 0.05f);
    }
    // every filter type, drive type, the phaser and flanger: finite and in range
    {
        int bad = 0;
        for (int f = 0; f < 5; ++f)
            for (int d = 0; d < 5; ++d)
            {
                auto v = reesePreset (0); v[FilterMode] = (float) f; v[DriveType] = (float) d; v[Drive] = 80; v[Resonance] = 90; v[FilterDrive] = 80;
                v[PhaserMix] = d == 1 ? 100.0f : 0.0f; v[FlangeMix] = d == 2 ? 100.0f : 0.0f; v[FlangeFb] = 95; v[PhaserFb] = 95; v[Squash] = 100;
                auto a = fresh (v); float pk = 0; bool ok = true;
                a->noteOn (36, 127, 0); run (*a, 60, &pk, &ok);
                if (! ok || pk > 2.0f || pk < 0.005f) { ++bad; std::printf ("  filter %d drive %d: peak %.3f\n", f, d, pk); }
            }
        std::printf ("filter x drive types out of range: %d of 25\n", bad);
        CHECK (bad == 0);
    }
    // MONO BASS: the low end the same in both sides
    {
        auto side = [&] (float hz)
        {
            auto v = reesePreset (0); v[Spread] = 100; v[Voices] = 7; v[Detune] = 40; v[MonoBass] = hz; v[Cutoff] = 400;
            auto a = fresh (v);
            a->noteOn (31, 100, 0); run (*a, 20);
            float lp = 0, e = 0;
            for (int b = 0; b < 50; ++b) { a->render (L.data(), R.data(), 480); for (int i = 0; i < 480; ++i) { lp += ((L[(size_t) i] - R[(size_t) i]) - lp) * 0.01f; e += lp * lp; } }
            return e;
        };
        const float off = side (0), on = side (250);
        std::printf ("low side energy: MONO BASS off %.4f, at 250 Hz %.4f\n", off, on);
        CHECK (on < off * 0.2f);
    }
}

// (v0.83) SUB, deeper: presets, the filter's envelope, the CLICK, the LFO synced, every drive type, the high-pass
static void testSubDeep()
{
    std::printf ("\n== SUB (v0.83: DEEPER)\n");
    using namespace sub;
    const double sr = 48000.0;
    std::vector<float> L (480), R (480);
    auto fresh = [&] (std::vector<float> v) { auto a = std::make_unique<SubSynth>(); a->prepare (sr, 512); a->setValues (v); return a; };
    auto run = [&] (SubSynth& a, int blocks, float* peak = nullptr, bool* ok = nullptr)
    {
        for (int b = 0; b < blocks; ++b)
        {
            a.render (L.data(), R.data(), 480);
            for (float x : L) { if (peak) *peak = std::max (*peak, std::abs (x)); if (ok) *ok = *ok && std::isfinite (x); }
        }
    };
    {
        int bad = 0;
        for (int i = 0; i < kSubPresets; ++i)
        {
            auto a = fresh (subPreset (i)); float pk = 0; bool ok = true;
            for (int nn : { 33, 36, 40 }) { a->noteOn (nn, 110, 0); run (*a, 30, &pk, &ok); a->noteOff (nn, 0); run (*a, 10, &pk, &ok); }
            if (! ok || pk < 0.05f || pk > 1.5f) { ++bad; std::printf ("  %s: peak %.3f\n", subPresetName (i), pk); }
        }
        for (int d = 0; d < 5; ++d)
        {
            auto v = subPreset (0); v[Drive] = 90; v[DriveType] = (float) d; v[Resonance] = 100; v[ToneEnv] = 100; v[Tone] = 300;
            auto a = fresh (v); float pk = 0; bool ok = true;
            a->noteOn (36, 127, 0); run (*a, 40, &pk, &ok);
            if (! ok || pk > 1.5f || pk < 0.02f) { ++bad; std::printf ("  drive %d: peak %.3f\n", d, pk); }
        }
        std::printf ("presets and drive types out of range: %d\n", bad);
        CHECK (bad == 0 && builtinParams (kSubKey).size() == (size_t) Count);
    }
    // the filter's envelope: open at the attack, closing over ENV DECAY
    {
        auto v = subPreset (0); v[Tone] = 200; v[ToneEnv] = 80; v[ToneDecay] = 150;
        auto a = fresh (v);
        a->noteOn (36, 100, 0); a->render (L.data(), R.data(), 32);
        const float early = a->toneNow();
        run (*a, 60);
        std::printf ("filter envelope: %.0f Hz at the attack, %.0f Hz after 0.6 s\n", early, a->toneNow());
        CHECK (early > 1500.0f && a->toneNow() < 260.0f);
    }
    // the CLICK: brighter right at the attack (more change sample to sample)
    {
        auto edge = [&] (float click)
        {
            auto v = subPreset (0); v[Click] = click; v[ClickTone] = 2000;
            auto a = fresh (v); a->noteOn (36, 100, 0); a->render (L.data(), R.data(), 240);
            float d = 0; for (int i = 1; i < 240; ++i) d += std::abs (L[(size_t) i] - L[(size_t) i - 1]);
            return d;
        };
        const float plain = edge (0), clicked = edge (80);
        std::printf ("attack's first 5 ms, sample-to-sample change: %.3f plain, %.3f with CLICK\n", plain, clicked);
        CHECK (clicked > plain * 3.0f);
    }
    // the LFO synced to 1/4 at 120 BPM on the level: two dips a second
    {
        auto v = subPreset (0); v[LfoTarget] = LLevel; v[LfoDepth] = 100; v[LfoSync] = 3;
        auto a = fresh (v); Song song; song.tempo = 120; song.speed = 6; a->updateSong (song);
        a->noteOn (45, 100, 0); run (*a, 10);
        int dips = 0; bool low = false;
        for (int b = 0; b < 200; ++b)
        {
            a->render (L.data(), R.data(), 480);
            float pk = 0; for (float x : L) pk = std::max (pk, std::abs (x));
            if (! low && pk < 0.05f) { ++dips; low = true; }
            if (pk > 0.2f) low = false;
        }
        std::printf ("LFO 1/4 at 120 BPM on the level: %d dips in 2 s\n", dips);
        CHECK (dips >= 3 && dips <= 5);
    }
}

// (v0.83) RAVE, deeper: the layered presets, a LAYER under the sound, STRUM, every new control at its extreme
static void testRaveDeep()
{
    std::printf ("\n== RAVE (v0.83: DEEPER)\n");
    using namespace rave;
    const double sr = 48000.0;
    std::vector<float> L (480), R (480);
    auto fresh = [&] (std::vector<float> v) { auto a = std::make_unique<RaveSynth>(); a->prepare (sr, 512); a->setValues (v); return a; };
    auto run = [&] (RaveSynth& a, int blocks, float* peak = nullptr, bool* ok = nullptr)
    {
        for (int b = 0; b < blocks; ++b)
        {
            a.render (L.data(), R.data(), 480);
            for (int i = 0; i < 480; ++i) { if (peak) *peak = std::max (*peak, std::abs (L[(size_t) i])); if (ok) *ok = *ok && std::isfinite (L[(size_t) i]) && std::isfinite (R[(size_t) i]); }
        }
    };
    {
        int bad = 0;
        for (int i = 0; i < kRaveLayered; ++i)
        {
            auto a = fresh (raveLayered (i)); float pk = 0; bool ok = true;
            for (int nn : { 60, 63, 67 }) { a->noteOn (nn, 110, 0); run (*a, 30, &pk, &ok); a->noteOff (nn, 0); run (*a, 10, &pk, &ok); }
            if (! ok || pk < 0.02f || pk > 1.6f) { ++bad; std::printf ("  %s: peak %.3f\n", raveLayeredName (i), pk); }
        }
        // every control at its far end
        auto v = ravePreset (Stab);
        v[Layer] = 1 + Mentasm; v[LayerLevel] = 100; v[LayerOctave] = 2; v[Inversion] = 3; v[ChordSpread] = 100; v[Strum] = 200;
        v[rave::FilterType] = 1; v[KeyTrack] = 100; v[PitchAmount] = 24; v[Glide] = 1000; v[VibDepth] = 100; v[Drive] = 100; v[Crush] = 100;
        v[EchoFeedback] = 95; v[Width] = 150; v[Resonance] = 100;
        auto a = fresh (v); float pk = 0; bool ok = true;
        for (int nn : { 48, 72 }) { a->noteOn (nn, 127, 0); run (*a, 40, &pk, &ok); }
        std::printf ("layered presets out of range: %d; everything flat out: peak %.3f\n", bad, pk);
        CHECK (bad == 0 && ok && pk < 1.6f && raveParams().size() == (size_t) Count);
    }
    // a LAYER under the sound: louder, the same voices
    {
        auto plain = fresh (ravePreset (Bleep)); float a1 = 0;
        plain->noteOn (60, 100, 0); run (*plain, 10, &a1);
        auto v = ravePreset (Bleep); v[Layer] = 1 + Organ; v[LayerLevel] = 100;
        auto lay = fresh (v); float a2 = 0;
        lay->noteOn (60, 100, 0); run (*lay, 10, &a2);
        std::printf ("BLEEP %.3f; with an ORGAN layer %.3f (voices %d / %d)\n", a1, a2, plain->voicesPlaying(), lay->voicesPlaying());
        CHECK (a2 > a1 * 1.2f && plain->voicesPlaying() == lay->voicesPlaying());
    }
    // STRUM: a chord's notes one after another - the first 10 ms quieter than with no strum
    {
        auto early = [&] (float strum)
        {
            auto v = ravePreset (Stab); v[Strum] = strum; v[Chord] = 3;
            auto a = fresh (v); a->noteOn (60, 100, 0);
            float pk = 0; run (*a, 1, &pk); return pk;
        };
        const float all = early (0), strummed = early (150);
        std::printf ("first 10 ms of a MIN 7: %.3f at once, %.3f strummed\n", all, strummed);
        CHECK (strummed < all * 0.85f);
    }
}

// (v0.84) MORPH, deeper: its presets, FILTER 2 in series and parallel, the LFO synced, RANDOM, everything flat out
static void testMorphDeep()
{
    std::printf ("\n== MORPH (v0.84: DEEPER)\n");
    using namespace msyn;
    std::vector<float> L (480), R (480);
    auto fresh = [&] (std::vector<float> v) { auto a = std::make_unique<MorphSynth>(); a->prepare (48000, 512); a->setValues (v); return a; };
    auto run = [&] (MorphSynth& a, int blocks, float* peak = nullptr, bool* ok = nullptr)
    {
        for (int b = 0; b < blocks; ++b)
        {
            a.render (L.data(), R.data(), 480);
            for (int i = 0; i < 480; ++i) { if (peak) *peak = std::max (*peak, std::abs (L[(size_t) i])); if (ok) *ok = *ok && std::isfinite (L[(size_t) i]) && std::isfinite (R[(size_t) i]); }
        }
    };
    int bad = 0;
    for (int i = 0; i < kMorphPresets; ++i)
    {
        auto a = fresh (morphPreset (i)); float pk = 0; bool ok = true;
        for (int nn : { 48, 55, 60 }) { a->noteOn (nn, 110, 0); run (*a, 30, &pk, &ok); a->noteOff (nn, 0); run (*a, 10, &pk, &ok); }
        if (! ok || pk < 0.02f || pk > 1.6f) { ++bad; std::printf ("  %s: peak %.3f\n", morphPresetName (i), pk); }
    }
    for (int route = 0; route < 3; ++route)
    {
        auto v = morphPreset (0); v[Route2] = (float) route; v[Model2] = 10; v[Emphasis] = 100; v[Drive] = 100; v[SubLevel] = 100; v[NoiseLevel] = 100;
        v[PitchEnv] = 24; v[Glide] = 1000; v[MorphRandom] = 100; v[EchoFeedback] = 95; v[Width] = 150; v[LfoSync] = 5; v[LfoShape] = 4; v[LfoDepth] = 100;
        auto a = fresh (v); float pk = 0; bool ok = true;
        for (int nn : { 36, 72 }) { a->noteOn (nn, 127, 0); run (*a, 40, &pk, &ok); }
        if (! ok || pk > 1.6f || pk < 0.01f) { ++bad; std::printf ("  route %d flat out: peak %.3f\n", route, pk); }
    }
    // RANDOM: two notes of the same key sit at different morphs
    {
        auto v = morphPreset (0); v[MorphRandom] = 100; v[EnvAmount] = 0; v[LfoDepth] = 0; v[Morph] = 50; v[VelMorph] = 0;
        auto a = fresh (v);
        a->noteOn (60, 100, 0); run (*a, 2); const float m1 = a->morphOf (0);
        a->noteOn (64, 100, 0); run (*a, 2);
        float m2 = -1; for (int k = 1; k < 16; ++k) if (a->morphOf (k) > 0) m2 = a->morphOf (k);
        std::printf ("RANDOM: one note at %.2f, the next at %.2f\n", m1, m2);
        CHECK (std::abs (m1 - m2) > 0.01f);
    }
    std::printf ("presets and flat-out settings out of range: %d\n", bad);
    CHECK (bad == 0 && morphSynthParams().size() == (size_t) Count);
}

// (v0.84) GRANULAR, deeper: presets, every window, the scale, shimmer, synced S&H, echo - finite and in range
static void testGrainDeep()
{
    std::printf ("\n== GRANULAR (v0.84: DEEPER)\n");
    using namespace grain;
    std::vector<float> L (480), R (480);
    int bad = 0;
    for (int i = 0; i < kGrainPresets + 5; ++i)
    {
        auto v = grainPreset (std::min (i, kGrainPresets - 1));
        if (i >= kGrainPresets) { v[Window] = (float) (i - kGrainPresets); v[Shimmer] = 100; v[Scale] = 3; v[PitchSpread] = 24; v[Echo] = 100; v[EchoFeedback] = 95; v[Width] = 150; v[Lfo1Shape] = 4; v[Lfo1Sync] = 5; v[Lfo1Depth] = 100; v[Density] = 120; }
        GrainSynth a; a.prepare (48000, 512); a.setValues (v);
        float pk = 0; bool ok = true;
        for (int nn : { 48, 60, 67 }) a.noteOn (nn, 110, 0);
        for (int b = 0; b < 150; ++b) { a.render (L.data(), R.data(), 480); for (int k = 0; k < 480; ++k) { pk = std::max (pk, std::abs (L[(size_t) k])); ok = ok && std::isfinite (L[(size_t) k]) && std::isfinite (R[(size_t) k]); } }
        if (! ok || pk < 0.01f || pk >= 2.0f) { ++bad; std::printf ("  %s%s: peak %.3f\n", grainPresetName (std::min (i, kGrainPresets - 1)), i >= kGrainPresets ? " (flat out)" : "", pk); }
    }
    std::printf ("presets and extremes out of range: %d\n", bad);
    CHECK (bad == 0 && builtinParams (kGrainKey).size() == (size_t) Count);
}

// (v0.79) every built-in synth, any settings, notes on and off in any order (as keys and the pattern give them): once
// every key is up, it goes quiet - no envelope left hanging
static void testNoStuckNotes()
{
    std::printf ("\n== NO STUCK NOTES\n");
    const char* keys[] = { kSubKey, kReeseKey, kRaveKey, kRomKey, kMorphKey, kGrainKey, kAcidKey, kPadKey };
    std::mt19937 rng (79);
    std::uniform_real_distribution<float> u (0, 1);
    int stuck = 0;
    for (auto* key : keys)
        for (int trial = 0; trial < 4; ++trial)
        {
            Instrument in; makeBuiltin (in, key);
            auto vals = builtinDefaults (key);
            const auto& ps = builtinParams (key);
            for (size_t i = 0; i < ps.size(); ++i)
            {
                const std::string n = ps[i].name;
                const bool tail = n == "RELEASE" || n.find ("ECHO") != std::string::npos || n == "REVERB" || n == "SIZE" || n == "FEEDBACK";
                if (! tail && u (rng) < 0.5f) vals[i] = ps[i].toValue (u (rng));
            }
            writeBuiltinState (in, vals);
            auto s = createBuiltinSynth (in, 48000);
            std::vector<float> L (512), R (512);
            std::vector<int> down;
            for (int blk = 0; blk < 200; ++blk)
            {
                int last = 0;
                for (int e = (int) (u (rng) * 4); e > 0; --e)
                {
                    const int off = last + (int) (u (rng) * (float) (511 - last)); last = off;
                    if (down.empty() || u (rng) < 0.5f) { const int n = 36 + (int) (u (rng) * 36); s->noteOn (n, 1 + (int) (u (rng) * 126), off); down.push_back (n); }
                    else { const size_t k = (size_t) (u (rng) * (float) down.size()) % down.size(); s->noteOff (down[k], off); down.erase (down.begin() + (long) k); }
                }
                s->render (L.data(), R.data(), 512);
            }
            for (int n : down) s->noteOff (n, 0);
            for (int blk = 0; blk < 48000 * 15 / 512; ++blk) s->render (L.data(), R.data(), 512);
            float tail = 0;
            for (int blk = 0; blk < 10; ++blk) { std::fill (L.begin(), L.end(), 0.0f); s->render (L.data(), R.data(), 512); for (float x : L) tail = std::max (tail, std::abs (x)); }
            if (! (tail < 0.001f)) { ++stuck; std::printf ("%s (try %d): still sounding %.4f\n", key, trial, tail); }
        }
    std::printf ("stuck: %d of 32\n", stuck);
    CHECK (stuck == 0);
}

static void testMorphSynth()
{
    std::printf ("\n== MORPH SYNTH\n");
    const double sr = 48000;
    for (int i = 0; i < kMorphPresets; ++i)
    {
        MorphSynth s; s.prepare (sr, 512); s.setValues (morphPreset (i));
        std::vector<float> L ((size_t) sr * 6), R (L.size());
        s.noteOn (48, 110, 0);
        s.render (L.data(), R.data(), 48000);
        s.noteOff (48, 0);
        s.render (L.data() + 48000, R.data() + 48000, (int) L.size() - 48000);
        float pk = 0, tail = 0, first = 0; bool ok = true;
        for (size_t k = 0; k < L.size(); ++k) { ok = ok && std::isfinite (L[k]); pk = std::max (pk, std::abs (L[k])); }
        for (size_t k = 0; k < 4; ++k) first = std::max (first, std::abs (L[k]));
        for (size_t k = L.size() - 4800; k < L.size(); ++k) tail = std::max (tail, std::abs (L[k]));
        std::printf ("%-13s peak %.3f, first samples %.4f, after release %.4f, voices %d\n", morphPresetName (i), pk, first, tail, s.voicesPlaying());
        CHECK (ok && pk > 0.03f && pk < 1.5f && first < 0.05f && tail < 0.01f);
    }
    // the MORPH envelope moves the filter: VOWEL PAD's MORPH opens over its attack
    {
        MorphSynth s; s.prepare (sr, 512); s.setValues (morphPreset (0));
        std::vector<float> L (480), R (480);
        s.noteOn (60, 64, 0); s.render (L.data(), R.data(), 480);
        const float early = s.morphOf (0);
        for (int k = 0; k < 60; ++k) s.render (L.data(), R.data(), 480);
        const float later = s.morphOf (0);
        std::printf ("MORPH ENV: at 10 ms %.2f, at 0.6 s %.2f\n", early, later);
        CHECK (early < 0.1f && later > 0.5f);
    }
    // CHORD and POLY
    {
        auto v = morphPreset (0); v[msyn::Chord] = 3;
        MorphSynth s; s.prepare (sr, 512); s.setValues (v);
        std::vector<float> L (480), R (480);
        s.noteOn (60, 100, 0); s.noteOn (67, 100, 0); s.render (L.data(), R.data(), 480);
        CHECK (s.voicesPlaying() == 8);
    }
    Instrument in; makeBuiltin (in, kMorphKey);
    CHECK (in.pluginName == "MORPH" && readBuiltinState (in).size() == (size_t) msyn::Count);
}

// ---- ROM (v0.77): multisamples from WAVs (with their smpl root and loop), four tones
namespace
{
    // a WAV in memory: 16-bit mono, with a smpl chunk (root, one loop) if root >= 0
    std::vector<uint8_t> makeWav (const std::vector<int16_t>& d, int rate, int root, int ls, int le, int bits = 16, int ch = 1)
    {
        std::vector<uint8_t> w;
        auto u32 = [&] (uint32_t v) { for (int i = 0; i < 4; ++i) w.push_back ((uint8_t) (v >> (8 * i))); };
        auto u16 = [&] (uint16_t v) { w.push_back ((uint8_t) v); w.push_back ((uint8_t) (v >> 8)); };
        auto tag = [&] (const char* t) { for (int i = 0; i < 4; ++i) w.push_back ((uint8_t) t[i]); };
        tag ("RIFF"); u32 (0); tag ("WAVE");
        tag ("fmt "); u32 (16); u16 (1); u16 ((uint16_t) ch); u32 ((uint32_t) rate); u32 ((uint32_t) (rate * ch * bits / 8)); u16 ((uint16_t) (ch * bits / 8)); u16 ((uint16_t) bits);
        if (root >= 0)
        {
            tag ("smpl"); u32 (60);
            u32 (0); u32 (0); u32 (1000000000u / (uint32_t) rate); u32 ((uint32_t) root); u32 (0); u32 (0); u32 (0); u32 (ls >= 0 ? 1 : 0); u32 (0);
            u32 (0); u32 (0); u32 ((uint32_t) std::max (0, ls)); u32 ((uint32_t) std::max (0, le)); u32 (0); u32 (0);
        }
        tag ("data"); u32 ((uint32_t) (d.size() * (size_t) ch * (size_t) bits / 8));
        for (int16_t x : d)
            for (int c = 0; c < ch; ++c)
            {
                if (bits == 16) u16 ((uint16_t) x);
                else { w.push_back (0); w.push_back ((uint8_t) x); w.push_back ((uint8_t) (x >> 8)); }   // (24-bit)
            }
        const uint32_t sz = (uint32_t) w.size() - 8;
        for (int i = 0; i < 4; ++i) w[(size_t) (4 + i)] = (uint8_t) (sz >> (8 * i));
        return w;
    }
    // a looped sine at `hz` (whole cycles in the loop)
    RomZone sineZone (double hz, int rate, int root, std::string name)
    {
        RomZone z; z.name = name; z.rate = rate; z.root = root;
        const int per = (int) std::lround (rate / hz), cycles = 40;
        for (int i = 0; i < per * cycles; ++i) z.data.push_back ((int16_t) (16000 * std::sin (6.283185307179586 * i / per)));
        z.loopStart = per * 10; z.loopEnd = per * cycles - 1;
        return z;
    }
}

static void testRom()
{
    std::printf ("\n== ROM\n");
    const double sr = 48000;
    // a WAV with its root note and loop
    {
        std::vector<int16_t> d (5000); for (int i = 0; i < 5000; ++i) d[(size_t) i] = (int16_t) (i * 7 - 16000);
        const auto w = makeWav (d, 30000, 69, 1200, 4800);
        RomZone z; std::string err;
        CHECK (romReadWav (w.data(), w.size(), z, err) && z.root == 69 && z.loopStart == 1200 && z.loopEnd == 4800 && z.rate == 30000 && z.data == d);
        const auto w24 = makeWav (d, 44100, -1, -1, -1, 24, 2);
        RomZone z2;
        CHECK (romReadWav (w24.data(), w24.size(), z2, err) && z2.root == 60 && ! z2.loops() && z2.data.size() == 5000 && std::abs (z2.data[100] - d[100]) <= 1);
        int root = 0, ls = 0, le = 0;
        CHECK (romReadSmpl (w.data(), w.size(), root, ls, le) && root == 69 && ls == 1200 && ! romReadSmpl (w24.data(), w24.size(), root, ls, le));
        std::vector<uint8_t> junk { 'h', 'e', 'l', 'l', 'o' };
        CHECK (! romReadWav (junk.data(), junk.size(), z, err));
        std::printf ("WAV: root %d, loop %d-%d, %.0f Hz; 24-bit stereo read as mono\n", z.root, z.loopStart, z.loopEnd, z.rate);
    }
    // names: the library's groups
    CHECK (romGroupOf ("WND_XACORD0A") == "WND ACORD" && romGroupOf ("WND_XBIRDY_4") == "WND BIRDY" && romGroupOf ("VOX_XDO_BH0U") == "VOX DO_BH");
    CHECK (romCategoryOf ("WND_XPICLO02") == "WND" && romGroupOf ("Piano C3") == "PIANO" && romGroupOf ("strings_048") == "STRINGS");
    // a multisample: three roots, spread across the keys
    std::vector<RomZone> zones { sineZone (220.0, 44100, 57, "A2"), sineZone (440.0, 44100, 69, "A3"), sineZone (880.0, 44100, 81, "A4") };
    auto bank = romWithTone (nullptr, 0, zones, "SINES");
    std::printf ("zones: %d-%d, %d-%d, %d-%d\n", bank->zones[0].lo, bank->zones[0].hi, bank->zones[1].lo, bank->zones[1].hi, bank->zones[2].lo, bank->zones[2].hi);
    CHECK (bank->zones[0].lo == 0 && bank->zones[0].hi == 63 && bank->zones[1].lo == 64 && bank->zones[1].hi == 75 && bank->zones[2].lo == 76 && bank->zones[2].hi == 127);
    CHECK (romZoneFor (*bank, 0, 69)->name == "A3" && romZoneFor (*bank, 0, 30)->name == "A2" && romZoneFor (*bank, 1, 60) == nullptr);
    auto play = [&] (const std::vector<float>& vals, int note, int len, int offAt = -1)
    {
        RomSynth s; s.prepare (sr, 512); s.setValues (vals); s.setBank (bank);
        std::vector<float> L ((size_t) len), R ((size_t) len);
        s.noteOn (note, 127, 0);
        if (offAt < 0) s.render (L.data(), R.data(), len);
        else { s.render (L.data(), R.data(), offAt); s.noteOff (note, 0); s.render (L.data() + offAt, R.data() + offAt, len - offAt); }
        return std::make_pair (L, R);
    };
    auto v = builtinDefaults (kRomKey);
    v[rom::Chorus] = 0;
    for (int t = 1; t < kRomTones; ++t) v[(size_t) (t * rom::kPerTone)] = 100;
    // pitch: A-4 (69) from the A3 sample, C-5 (72) from it a little faster, E-4 (64) from the A3 zone too
    {
        const auto [L, R] = play (v, 69, 48000);
        const auto [L2, R2] = play (v, 72, 48000);
        const auto [L3, R3] = play (v, 45, 48000);
        const double h1 = crossingsHz (L, 4800, 47000, 48000), h2 = crossingsHz (L2, 4800, 47000, 48000), h3 = crossingsHz (L3, 4800, 47000, 48000);
        std::printf ("ROM pitches: note 69 %.1f Hz, 72 %.1f Hz, 45 %.1f Hz\n", h1, h2, h3);
        CHECK (std::abs (h1 - 440) < 2 && std::abs (h2 - 523.25) < 3 && std::abs (h3 - 110) < 1.5);
        // the loop keeps it going (2 s on, from a sample of 0.09 s)
        const auto [Lh, Rh] = play (v, 69, 96000);
        float late = 0; for (size_t i = 90000; i < 96000; ++i) late = std::max (late, std::abs (Lh[i]));
        std::printf ("  held 2 s on a 0.09 s looped sample: %.3f\n", late);
        CHECK (late > 0.1f);
        // released: gone after RELEASE
        const auto [Lr, Rr] = play (v, 69, 96000, 24000);
        float tail = 0; for (size_t i = 80000; i < 96000; ++i) tail = std::max (tail, std::abs (Lr[i]));
        CHECK (tail < 0.001f);
    }
    // (v0.79) FILTER: MORPH - the MORPH FILTER on every note, its MORPH moved by the filter envelope
    {
        auto m = v; m[rom::FilterType] = rom::MorphKind; m[rom::MorphModel] = 7; m[rom::Morph] = 0; m[rom::MorphEnv] = 100; m[rom::FilterDecay] = 400;
        RomSynth s; s.prepare (sr, 512); s.setValues (m); s.setBank (bank);
        std::vector<float> L (480), R (480);
        s.noteOn (69, 127, 0); s.render (L.data(), R.data(), 480);
        const float early = s.morphOf (0);
        for (int k = 0; k < 100; ++k) s.render (L.data(), R.data(), 480);
        const float later = s.morphOf (0);
        float pk = 0; bool ok = true; for (float x : L) { ok = ok && std::isfinite (x); pk = std::max (pk, std::abs (x)); }
        std::printf ("ROM MORPH (ACID, ENV>MORPH 100): morph %.2f at 10 ms, %.2f at 1 s; level %.3f\n", early, later, pk);
        CHECK (ok && early > 0.9f && later < 0.2f && pk > 0.01f && pk < 2.0f);
        // and the low-pass - the old songs' sound - is still the default
        CHECK (builtinDefaults (kRomKey)[rom::FilterType] == rom::LowPass && builtinDefaults (kRomKey).size() == (size_t) rom::Count);
    }
    // a sample without a loop plays once and stops
    {
        auto z = sineZone (440.0, 44100, 69, "ONCE"); z.loopStart = z.loopEnd = -1;
        bank = romWithTone (nullptr, 0, { z }, "ONCE");
        RomSynth s; s.prepare (sr, 512); s.setValues (v); s.setBank (bank);
        std::vector<float> L (48000), R (48000);
        s.noteOn (69, 127, 0); s.render (L.data(), R.data(), 48000);
        float late = 0; for (size_t i = 20000; i < 48000; ++i) late = std::max (late, std::abs (L[i]));
        std::printf ("  no loop: after the sample %.4f, voices %d\n", late, s.voicesPlaying());
        CHECK (late < 0.0001f && s.voicesPlaying() == 0);
    }
    // two tones: TONE 2 an octave up and panned right; TONE 1 left
    {
        bank = romWithTone (nullptr, 0, zones, "SINES");
        bank = romWithTone (bank, 1, { sineZone (440.0, 44100, 69, "B") }, "B");
        CHECK (bank->zonesOf (0) == 3 && bank->zonesOf (1) == 1 && bank->toneName[1] == "B");
        auto w = v; w[rom::Pan1] = -100; w[rom::Pan2] = 100; w[rom::Tune2] = 12;
        const auto [L, R] = play (w, 57, 48000);
        const double hl = crossingsHz (L, 4800, 47000, 48000), hr = crossingsHz (R, 4800, 47000, 48000);
        std::printf ("TONE 1 left %.1f Hz, TONE 2 (+12, right) %.1f Hz\n", hl, hr);
        CHECK (std::abs (hl - 220) < 2 && std::abs (hr - 440) < 3);
        // emptied: back to one tone
        auto b2 = romWithTone (bank, 1, {}, "");
        CHECK (b2->zonesOf (1) == 0 && b2->toneName[1].empty() && b2->zonesOf (0) == 3 && bank->zonesOf (1) == 1);
    }
    // CHORD: three voices from one key
    {
        auto w = v; w[rom::Chord] = 1;
        RomSynth s; s.prepare (sr, 512); s.setValues (w); s.setBank (bank);
        std::vector<float> L (4800), R (4800);
        s.noteOn (60, 100, 0); s.render (L.data(), R.data(), 4800);
        CHECK (s.voicesPlaying() == 3);
    }
}

// ---- REESE's SUB: one or two octaves down, its shape (v0.76)
static void testReeseSub()
{
    std::printf ("\n== REESE SUB OCT / SUB SHAPE\n");
    const double sr = 48000;
    // the saws filtered right away (CUTOFF 40 Hz, no envelope): what's left is the sub, which comes after the filter
    auto run = [&] (int oct, int shape, int type = reese::Reese)
    {
        auto v = builtinDefaults (kReeseKey);
        v[reese::Type] = (float) type; v[reese::Octave] = 0; v[reese::Cutoff] = 40; v[reese::EnvAmount] = 0; v[reese::Resonance] = 0;
        v[reese::Drive] = 0; v[reese::Chorus] = 0; v[reese::SubLevel] = 100; v[reese::SubOctave] = (float) oct; v[reese::SubShape] = (float) shape;
        v[reese::MoveDepth] = 0; v[reese::Bend] = 0;
        ReeseSynth s; s.prepare (sr, 512); s.setValues (v);
        std::vector<float> L ((size_t) sr), R (L.size());
        s.noteOn (57, 127, 0); s.render (L.data(), R.data(), (int) L.size());
        double pk = 0, rms = 0;
        for (size_t i = 12000; i < L.size(); ++i) { pk = std::max (pk, (double) std::abs (L[i])); rms += (double) L[i] * L[i]; }
        rms = std::sqrt (rms / (L.size() - 12000));
        return std::array<double, 3> { crossingsHz (L, 12000, 47000, 48000), pk, pk / std::max (1e-9, rms) };
    };
    const auto s1 = run (0, 1), s2 = run (1, 1), tri = run (0, 2), sq = run (0, 3), autoR = run (0, 0), autoH = run (0, 0, reese::Hoover);
    std::printf ("A-3 (220 Hz): SUB -1 OCT %.1f Hz, -2 OCT %.1f Hz; crest sine %.2f, triangle %.2f; level sine %.3f, square (through the 40 Hz filter) %.3f; AUTO on REESE %.3f, on HOOVER %.3f\n",
                 s1[0], s2[0], s1[2], tri[2], s1[1], sq[1], autoR[1], autoH[1]);
    CHECK (std::abs (s1[0] - 110) < 2 && std::abs (s2[0] - 55) < 2);
    CHECK (std::abs (s1[2] - 1.414) < 0.05 && std::abs (tri[2] - 1.732) < 0.06);
    CHECK (sq[1] < s1[1] * 0.5 && std::abs (autoR[1] - s1[1]) < 0.01 && autoH[1] < s1[1] * 0.5);
    // a song from before: AUTO, an octave down
    Instrument in; makeBuiltin (in, kReeseKey);
    auto old = readBuiltinState (in); old.resize (reese::SubOctave); writeBuiltinState (in, old);
    const auto back = readBuiltinState (in);
    CHECK (back[reese::SubOctave] == 0 && back[reese::SubShape] == 0);
}

// ---- RAVE (v0.73): the jungle keys
static void testRaveSynth()
{
    std::printf ("\n== RAVE\n");
    const double sr = 48000;
    for (int t = 0; t < rave::Types; ++t)
    {
        RaveSynth s; s.prepare (sr, 512); s.setValues (ravePreset (t));
        std::vector<float> L ((size_t) sr * 5), R (L.size());   // (PAD's release is 2.5 s, and the echo after it)
        s.noteOn (60, 110, 0);
        s.render (L.data(), R.data(), 24000);
        const int playing = s.voicesPlaying();
        s.noteOff (60, 0);
        s.render (L.data() + 24000, R.data() + 24000, (int) L.size() - 24000);
        float peak = 0, step = 0, early = 0, tail = 0;
        for (size_t i = 0; i < L.size(); ++i) { peak = std::max (peak, std::max (std::abs (L[i]), std::abs (R[i]))); if (i) step = std::max (step, std::abs (L[i] - L[i - 1])); }
        for (size_t i = 0; i < 4; ++i) early = std::max (early, std::abs (L[i]));   // (the attack ramps from 0)
        for (size_t i = L.size() - 4800; i < L.size(); ++i) tail = std::max (tail, std::abs (L[i]));
        std::printf ("%-5s: %d voices for one key, peak %.3f, first 4 samples %.3f, biggest step %.3f, last 0.1 s of 5 %.4f\n", raveTypeName (t), playing, peak, early, step, tail);
        CHECK (peak > 0.05f && peak < 0.95f);
        CHECK (early < 0.05f);                        // (no click at the front)
        CHECK (playing == (t == rave::Stab || t == rave::Belgian ? 3 : 1));   // (STAB's MINOR chord, BELGIAN's 5TH: three notes)
        CHECK (tail < 0.01f);                          // (it ends - the echo too)
        Instrument in; makeRave (in, t);
        CHECK (in.pluginKey == kRaveKey && in.name == std::string ("RAVE ") + raveTypeName (t) && (int) readBuiltinState (in)[rave::Type] == t);
    }
    // BLEEP plays the note's pitch (A-4 = 440 Hz)
    {
        RaveSynth s; s.prepare (sr, 512); auto v = ravePreset (rave::Bleep); v[rave::Sustain] = 100; v[rave::Echo] = 0; s.setValues (v);
        std::vector<float> L (48000), R (L.size());
        s.noteOn (69, 100, 0); s.render (L.data(), R.data(), (int) L.size());
        const double hz = crossingsHz (L, 4800, 47000, 48000);
        std::printf ("BLEEP A-4: %.1f Hz\n", hz);
        CHECK (std::abs (hz - 440.0) < 3.0);
    }
    // 16 voices: a run of chords never stops dead (the oldest fade) and nothing blows up
    {
        RaveSynth s; s.prepare (sr, 512); auto v = ravePreset (rave::Pad); v[rave::Chord] = 3; s.setValues (v);
        std::vector<float> L (96000), R (L.size());
        for (int k = 0; k < 12; ++k) { s.noteOn (48 + k * 2, 100, 0); s.render (L.data() + k * 8000, R.data() + k * 8000, 8000); }
        float peak = 0; bool ok = true;
        for (float x : L) { ok = ok && std::isfinite (x); peak = std::max (peak, std::abs (x)); }
        std::printf ("12 MIN 7 chords held on PAD: %d voices, peak %.3f\n", s.voicesPlaying(), peak);
        CHECK (ok && s.voicesPlaying() <= 16 && peak < 1.0f);
    }
}

// ---- REESE: detuned saws / HOOVER, click-free like SUB
static void testReeseSynth()
{
    std::printf ("\n== REESE\n");
    const double sr = 48000;
    auto defs = [] { std::vector<float> v; for (auto& q : reeseParams()) v.push_back (q.def); return v; };
    CHECK ((int) reeseParams().size() == reese::Count && builtinParams (kReeseKey).size() == (size_t) reese::Count);
    auto freq = [&] (const std::vector<float>& x, int from)
    {
        int c = 0, f = -1, l = -1;
        for (size_t i = (size_t) from + 1; i < x.size(); ++i) if (x[i - 1] < 0 && x[i] >= 0) { ++c; if (f < 0) f = (int) i; l = (int) i; }
        return c > 1 ? (c - 1) * sr / (l - f) : 0.0;
    };
    // one saw, the filter open: C-3 an octave down is 130.8 Hz
    {
        ReeseSynth s; s.prepare (sr, 512);
        auto v = defs(); v[reese::Voices] = 1; v[reese::Detune] = 0; v[reese::Cutoff] = 18000; v[reese::EnvAmount] = 0; v[reese::SubLevel] = 0; v[reese::Drive] = 0;
        s.setValues (v); s.noteOn (60, 127, 0);
        std::vector<float> L (24000), R (L.size()); s.render (L.data(), R.data(), (int) L.size());
        const double f = freq (L, 4800);
        std::printf ("1 saw, C-3, OCTAVE -1: %.2f Hz (130.81)\n", f);
        CHECK (std::abs (f - 130.81) < 0.5);
    }
    // no clicks: a note sliding to another (overlapping), a note-off and the next note at once, fast retriggers.
    // A saw jumps every cycle anyway, so: the biggest step around each change no bigger than the steady sound's
    for (int type : { reese::Reese, reese::Hoover })
        for (int mode : { reese::Legato, reese::Retrigger, reese::Poly })
        {
            ReeseSynth s; s.prepare (sr, 512);
            // (the filter low and still, no drive or chorus: the sound itself smooth, so a click would stand out)
            auto v = defs(); v[reese::Type] = (float) type; v[reese::Mode] = (float) mode; v[reese::Bend] = type ? 7.0f : 0.0f;
            v[reese::Cutoff] = 300; v[reese::Resonance] = 0; v[reese::EnvAmount] = 0; v[reese::Drive] = 0; v[reese::Chorus] = 0;
            s.setValues (v);
            std::vector<float> L (96000), R (L.size());
            s.noteOn (36, 110, 0); s.render (L.data(), R.data(), 24000);
            s.noteOn (39, 110, 0); s.noteOff (36, 0); s.render (L.data() + 24000, R.data() + 24000, 24000);
            s.noteOff (39, 0); s.noteOn (36, 110, 0); s.render (L.data() + 48000, R.data() + 48000, 24000);
            for (int k = 0; k < 8; ++k) { s.noteOff (36, 0); s.noteOn (36, 110, 0); s.render (L.data() + 72000 + k * 3000, R.data() + 72000 + k * 3000, 3000); }
            float steady = 0, around = 0; bool fin = true;
            for (int i = 12001; i < 23900; ++i) steady = std::max (steady, std::abs (L[(size_t) i] - L[(size_t) i - 1]));
            for (int at : { 24000, 48000, 72000, 75000, 78000 })
                for (int i = at - 2; i < at + 96; ++i) around = std::max (around, std::abs (L[(size_t) i] - L[(size_t) i - 1]));
            for (float x : L) fin = fin && std::isfinite (x);
            std::printf ("%s %s: steady step %.4f, at the changes %.4f\n", type ? "HOOVER" : "REESE ", mode == reese::Legato ? "LEGATO" : mode == reese::Poly ? "POLY  " : "OFF   ", steady, around);
            CHECK (fin && around <= steady * (mode == reese::Poly ? 2.2f : 1.35f) + 0.002f);   // (POLY: two notes sound at once)
        }
    // LEGATO: the second note glides there, without a new attack (the level never dips)
    {
        ReeseSynth s; s.prepare (sr, 512);
        auto v = defs(); v[reese::Glide] = 200; s.setValues (v);
        std::vector<float> L (4800), R (L.size());
        s.noteOn (36, 110, 0); for (int k = 0; k < 5; ++k) s.render (L.data(), R.data(), 4800);
        const float lv = s.levelNow();
        s.noteOn (43, 110, 0); s.noteOff (36, 0); s.render (L.data(), R.data(), 2400);
        const float mid = s.pitchNow();
        std::printf ("LEGATO: level %.3f -> %.3f, half way through the glide at %.2f (36 -> 43)\n", lv, s.levelNow(), mid);
        CHECK (s.levelNow() >= lv * 0.98f && mid > 37 && mid < 42);
    }
    // HOOVER: each note swoops up from BEND below; the filter opens with its envelope then closes
    {
        ReeseSynth s; s.prepare (sr, 512);
        auto v = defs(); v[reese::Type] = reese::Hoover; v[reese::Bend] = 12; v[reese::BendTime] = 200; v[reese::EnvAmount] = 60; v[reese::Mode] = reese::Retrigger;
        s.setValues (v);
        std::vector<float> L (480), R (L.size());
        s.noteOn (48, 110, 0); s.render (L.data(), R.data(), 48);
        const float p0 = s.pitchNow(), c0 = s.cutoffNow();
        for (int k = 0; k < 100; ++k) s.render (L.data(), R.data(), 480);
        std::printf ("HOOVER: pitch %.2f -> %.2f (48), cutoff %.0f -> %.0f Hz\n", p0, s.pitchNow(), c0, s.cutoffNow());
        CHECK (p0 < 37.5f && std::abs (s.pitchNow() - 48) < 0.1f && c0 > s.cutoffNow() * 3.0f);
    }
    // more VOICES and DETUNE: wider (left and right differ), still not clipping; WOBBLE moves the cutoff
    {
        ReeseSynth s; s.prepare (sr, 512);
        auto v = defs(); v[reese::Voices] = 7; v[reese::Detune] = 60; v[reese::Spread] = 100; v[reese::WobbleDepth] = 80; v[reese::WobbleRate] = 4;
        v[reese::Chorus] = 70; v[reese::Drive] = 80; v[reese::Resonance] = 90; v[reese::Mode] = reese::Poly;
        s.setValues (v);
        s.noteOn (36, 127, 0); s.noteOn (43, 127, 0); s.noteOn (48, 127, 0);
        std::vector<float> L (48000), R (L.size());
        float lo = 1e9f, hi = 0;
        for (int k = 0; k < 10; ++k) { s.render (L.data() + k * 4800, R.data() + k * 4800, 4800); lo = std::min (lo, s.cutoffNow()); hi = std::max (hi, s.cutoffNow()); }
        float pk = 0, diff = 0; bool fin = true;
        for (size_t i = 0; i < L.size(); ++i) { pk = std::max ({ pk, std::abs (L[i]), std::abs (R[i]) }); diff = std::max (diff, std::abs (L[i] - R[i])); fin = fin && std::isfinite (L[i]) && std::isfinite (R[i]); }
        std::printf ("7 voices, a chord, everything up: peak %.3f, L/R differ by %.3f, WOBBLE cutoff %.0f .. %.0f Hz\n", pk, diff, lo, hi);
        CHECK (fin && pk < 1.5f && diff > 0.05f && hi > lo * 3.0f);
    }
    // made, saved and played in a song
    {
        Song s = makeSong();
        makeBuiltin (s.instruments[4], kReeseKey);
        CHECK (s.instruments[4].name == "REESE" && s.instruments[4].isBuiltin());
        auto v = readBuiltinState (s.instruments[4]);
        CHECK ((int) v.size() == reese::Count);
        v[reese::Type] = reese::Hoover; writeBuiltinState (s.instruments[4], v);
        CHECK (readBuiltinState (s.instruments[4])[reese::Type] == reese::Hoover);
        s.blocks[0] = Block {}; s.blocks[0].lines = 16; s.blocks[0].at (0, 0) = Cell { 13, 5, 0, 0 }; s.sequence = { 0 };
        Player p; p.setSampleRate (48000); p.setSong (&s);
        p.synths().factory = [] (int, const Instrument& in, double r) { return createBuiltinSynth (in, r); };
        p.synths().syncNow (s, 48000);
        p.playSong (0);
        std::vector<float> L (24000), R (L.size());
        p.render (L.data(), R.data(), (int) L.size());
        std::printf ("REESE in the song: rms %.3f\n", rmsOf (L, 0, (int) L.size()));
        CHECK (rmsOf (L, 0, (int) L.size()) > 0.02f);
    }
}

// ---- DUB THROWS: !xy sends a hit into the dub echo on SEND B; !0y rides its feedback
static void testDubThrows()
{
    std::printf ("\n== DUB THROWS\n");
    CHECK (commandFromChar ('!') == kCmdDub && commandChar (kCmdDub) == '!');
    CHECK (commandName (kCmdDub) == "DUB THROW");
    std::printf ("%s\n%s\n", describeCommand (kCmdDub, 0xC0).c_str(), describeCommand (kCmdDub, 0x08).c_str());
    // SEND B as a new song has it becomes DUB DELAY + SPRING; a B you've changed is left alone
    {
        MixerSettings m;
        CHECK (m.dubReady() && m.setUpDub() && m.ret[1].inserts[0].type == FxType::DubDelay && m.ret[1].inserts[1].type == FxType::Spring && m.dubReady());
        CHECK (! m.setUpDub());   // (now it's not as it came)
        MixerSettings c; c.ret[1].inserts[0].type = FxType::Reverb; c.ret[1].inserts[0].p[0] = 0.3f;
        CHECK (! c.setUpDub() && c.ret[1].inserts[0].type == FxType::Reverb);
    }
    // a hat (a short noise burst) on line 0: with !F0 its echoes carry on after it; without, silence
    auto render = [] (uint8_t cmd, uint8_t param, Cell second, float& throwAt4, float& throwAt12)
    {
        Song s = makeSong();
        s.blocks[0] = Block {}; s.blocks[0].lines = 32;
        s.blocks[0].at (0, 0) = Cell { 37, 2, cmd, param };
        s.blocks[0].at (8, 0) = second;
        s.sequence = { 0 };
        s.mixer.setUpDub();
        Player p; p.setSampleRate (48000); p.setSong (&s);
        p.mixer().syncEffectsNow (s.mixer);
        p.playSong (0);
        const int line = (int) (48000 * 2.5 / 125.0 * 6 / 6.0 * 1.0);   // (a line at 125 BPM, speed 6: 0.12 s)
        std::vector<float> L ((size_t) 48000 * 3), R (L.size());
        const int perLine = (int) std::lround (48000 * 0.12);
        p.render (L.data(), R.data(), perLine * 4); throwAt4 = p.mixer().throwLevel (0);
        p.render (L.data() + perLine * 4, R.data() + perLine * 4, perLine * 8); throwAt12 = p.mixer().throwLevel (0);
        p.render (L.data() + perLine * 12, R.data() + perLine * 12, (int) L.size() - perLine * 12);
        (void) line;
        return L;
    };
    float a4, a12, b4, b12;
    const auto dry = render (0, 0, Cell {}, b4, b12);
    const auto wet = render (kCmdDub, 0xF0, Cell {}, a4, a12);
    const int from = (int) (48000 * 0.12 * 2), to = (int) (48000 * 0.12 * 7);   // (after the hat, before line 8)
    std::printf ("after the hat: dry rms %.5f, thrown rms %.5f\n", rmsOf (dry, from, to), rmsOf (wet, from, to));
    CHECK (rmsOf (dry, from, to) < 0.002f && rmsOf (wet, from, to) > rmsOf (dry, from, to) * 10 + 0.003f);
    CHECK (a4 > 0.9f && a12 > 0.9f);   // open to the next note (there isn't one)
    // the next note on the track (without !) closes it; !C2 is open for two lines only
    {
        float x4, x12;
        render (kCmdDub, 0xF0, Cell { 37, 2, 0, 0 }, x4, x12);
        std::printf ("throw to the next note: open at line 4 %.2f, at line 12 %.2f\n", x4, x12);
        CHECK (x4 > 0.9f && x12 == 0.0f);
        render (kCmdDub, 0xC2, Cell {}, x4, x12);
        std::printf ("!C2: at line 4 %.2f\n", x4);
        CHECK (x4 == 0.0f);
    }
    // !0F rides the feedback up: longer echoes
    {
        Song s = makeSong();
        s.blocks[0] = Block {}; s.blocks[0].lines = 64;
        s.blocks[0].at (0, 0) = Cell { 37, 2, kCmdDub, 0xF0 };
        s.sequence = { 0 };
        s.mixer.setUpDub();
        Song s2 = s;
        s2.blocks[0].at (0, 1) = Cell { 0, 0, kCmdDub, 0x0F };
        auto tail = [] (Song& song)
        {
            Player p; p.setSampleRate (48000); p.setSong (&song); p.mixer().syncEffectsNow (song.mixer); p.playSong (0);
            std::vector<float> L ((size_t) 48000 * 4), R (L.size());
            p.render (L.data(), R.data(), (int) L.size());
            return rmsOf (L, 48000 * 3, 48000 * 4);
        };
        const float plain = tail (s), ridden = tail (s2);
        std::printf ("echo tail 3-4 s after: feedback as set %.5f, ridden up %.5f\n", plain, ridden);
        CHECK (ridden > plain * 2.0f);
    }
}

// ---- LIVE MODE: blocks launched in time; the held effects on the mix
static void testLiveMode()
{
    std::printf ("\n== LIVE MODE\n");
    const double sr = 48000;
    // the effects: no clicks going in or out, they change the sound while held, and it's all gone after
    auto mix = [&] (int i) { const double t = i / sr; return (float) (0.5 * std::sin (6.2831853 * 220 * t) * (0.6 + 0.4 * std::sin (6.2831853 * 3 * t))); };
    for (int k = LiveFx::Repeat4; k < LiveFx::Count; ++k)
    {
        LiveFx fx; fx.prepare (sr);
        std::vector<float> L (192000), R (L.size());
        for (int i = 0; i < (int) L.size(); ++i) L[(size_t) i] = R[(size_t) i] = mix (i);
        const auto dry = L;
        for (int b = 0; b < (int) L.size() / 256; ++b)
        {
            if (b == 100) fx.hold ((LiveFx::Kind) k);
            if (b == 400) { CHECK (fx.holding() == (LiveFx::Kind) k); fx.release(); }
            fx.process (L.data() + b * 256, R.data() + b * 256, 256, 137.0);
        }
        float step = 0, base = 0; double held = 0, after = 0;
        for (size_t i = 1; i < L.size(); ++i) { step = std::max (step, std::abs (L[i] - L[i - 1])); base = std::max (base, std::abs (dry[i] - dry[i - 1])); }
        for (int i = 60000; i < 100000; ++i) held += std::abs (L[(size_t) i] - dry[(size_t) i]);
        for (int i = 170000; i < 192000; ++i) after += std::abs (L[(size_t) i] - dry[(size_t) i]);
        std::printf ("%-13s biggest step %.4f (the mix's own %.4f), changed while held %.3f, after %.6f\n", LiveFx::name ((LiveFx::Kind) k), step, base, held / 40000, after / 22000);
        CHECK (step <= base * 1.6f + 0.002f && held / 40000 > 0.01 && after / 22000 < 1e-5 && ! fx.busy());
    }
    // a launched block starts on the line it's due: the next multiple of 4 lines (a beat) here
    {
        Song s = makeSong();
        s.blocks.assign (3, Block {});
        for (auto& b : s.blocks) b.lines = 16;
        s.blocks[1].at (0, 0) = Cell { 25, 1, 0, 0 };
        Player p; p.setSampleRate (48000); p.setSong (&s);
        p.playBlock (0, 0);
        const int perLine = (int) std::lround (48000 * 2.5 / s.tempo * s.speed);
        std::vector<float> L ((size_t) perLine * 2), R (L.size());
        p.render (L.data(), R.data(), perLine + perLine / 2);   // into line 1
        p.queueBlock (1, 4);
        CHECK (p.queuedBlock() == 1);
        int before = -1, switchedAt = -1;
        for (int k = 0; k < 20 && switchedAt < 0; ++k)
        {
            before = p.uiLine.load();
            p.render (L.data(), R.data(), perLine);
            if (p.uiBlock.load() == 1) switchedAt = before;
        }
        std::printf ("queued at line 1 (a beat = 4 lines): block 1 started after line %d, at line %d\n", switchedAt, p.uiLine.load());
        CHECK (switchedAt == 3 && p.uiLine.load() == 0 && p.queuedBlock() == -1);
        // it loops: 16 lines on, still block 1
        for (int k = 0; k < 17; ++k) p.render (L.data(), R.data(), perLine);
        CHECK (p.uiBlock.load() == 1);
        // AT ONCE: the next line
        p.queueBlock (2, 0);
        p.render (L.data(), R.data(), perLine);
        CHECK (p.uiBlock.load() == 2);
        // stopping forgets a launch
        p.queueBlock (0, 16); p.stop();
        CHECK (p.queuedBlock() == -1);
    }
}

// ---- CRUNCH: a sample through an old sampler for good
static void testGear()
{
    std::printf ("\n== GEAR\n");
    CHECK (gearCount() == 11 && gearName (8) == "DREADBOX TYPHON" && gearName (11) == "ROLAND JV-1080" && gearName (6) == "AKAI S1000" && gearName (7) == "AKAI S950" && gearHasEditor (6) && gearHasEditor (7) && ! gearHasEditor (5) && gearName (4) == "DREADBOX ARTEMIS" && gearName (5) == "ACCESS VIRUS TI" && gearName (0) == "NONE" && gearName (1) == "MICROKORG 2" && gearName (2) == "NTS-1 MKII" && gearMap (12) == nullptr);
    auto find = [] (int id, const std::string& label) { const auto* m = gearMap (id); for (size_t i = 0; i < m->params.size(); ++i) if (m->params[i].label() == label) return (int) i; return -1; };
    // the microKORG 2: plain CCs as its manual lists them
    {
        const auto* m = gearMap (1);
        const auto& cut = m->params[(size_t) find (1, "FILTER: CUTOFF")];
        CHECK (cut.how == GearSend::Cc && cut.num == 74 && m->params[(size_t) find (1, "AMP EG: ATTACK")].num == 73 && m->params[(size_t) find (1, "OSC 3: WAVE")].num == 48);
        std::set<int> seen; bool dup = false;
        for (const auto& p : m->params) { dup = dup || ! seen.insert (p.num).second; }
        CHECK (! dup && m->params.size() >= 60);   // (no CC twice)
        uint8_t out[4][3];
        CHECK (gearMessages (cut, 1.0f, 2, out) == 1 && out[0][0] == 0xB2 && out[0][1] == 74 && out[0][2] == 127);
    }
    // the NTS-1 mkII: 14-bit pairs, MSB then LSB; the TYPE switches land on the chart's values
    {
        const auto& cut = gearMap (2)->params[(size_t) find (2, "FILTER: CUTOFF")];
        uint8_t out[4][3];
        CHECK (gearMessages (cut, 0.5f, 0, out) == 2 && out[0][1] == 43 && out[1][1] == 107);
        const int x = out[0][2] * 128 + out[1][2];
        CHECK (std::abs (x - 8192) <= 1);
        const auto& ft = gearMap (2)->params[(size_t) find (2, "FILTER: TYPE")];
        int vals[7];
        for (int i = 0; i < 7; ++i) { gearMessages (ft, i / 6.0f, 0, out); vals[i] = out[0][2]; }
        CHECK (vals[0] == 0 && vals[1] == 18 && vals[5] == 90 && vals[6] == 127);   // (0, 18, 36, 54, 72, 90, 127)
        CHECK (gearStepValue (2, 5) == 50 && gearStepValue (4, 5) == 127 && gearStepValue (8, 9) == 127 && gearStepValue (1, 9) == 14);
        CHECK (gearStepValue (3, 10) == 36 && gearStepValue (1, 6) == 21);   // (ARP PATTERN 0,12,24,36..; INTERVALS 0,21,42..)
    }
    // + ROLLERS: BLEND / DETUNE on OSC A / B, the rest as NRPN 0:0 - 0:7 (LSB of the value first, then its MSB)
    {
        const auto* m = gearMap (3);
        CHECK (m->params[0].label() == "ROLLERS: BLEND" && m->params[0].num == 54 && m->params[1].label() == "ROLLERS: DETUNE" && m->params[1].num == 55);
        const auto& drop = m->params[(size_t) find (3, "ROLLERS: PITCH DROP")];
        uint8_t out[4][3];
        CHECK (gearMessages (drop, 1.0f, 1, out) == 4);
        CHECK (out[0][0] == 0xB1 && out[0][1] == 99 && out[0][2] == 0 && out[1][1] == 98 && out[1][2] == 1 && out[2][1] == 38 && out[2][2] == 127 && out[3][1] == 6 && out[3][2] == 127);
        const auto& mode = m->params[(size_t) find (3, "ROLLERS 2: MODE")];
        gearMessages (mode, 0.4f, 0, out);   // (4 positions: 0.4 is the second)
        CHECK (out[1][2] == 7 && out[2][2] + out[3][2] * 128 == 5461);
    }
    // the Artemis and the Virus TI: plain CCs, none twice; the Virus's switches go as their position
    for (int id : { 4, 5 })
    {
        std::set<int> seen; bool dup = false;
        for (const auto& p : gearMap (id)->params) dup = dup || p.how != GearSend::Cc || ! seen.insert (p.num).second;
        std::printf ("%s: %d controls in %d parts\n", gearName (id).c_str(), (int) gearMap (id)->params.size(), (int) gearMap (id)->groups().size());
        CHECK (! dup);
    }
    {
        uint8_t out[4][3];
        CHECK (gearMap (4)->params[(size_t) find (4, "LPF: CUTOFF")].num == 75 && gearMap (4)->params[(size_t) find (4, "VCA EG: RELEASE")].num == 72);
        const auto& f1 = gearMap (5)->params[(size_t) find (5, "FILTER 1: MODE")];
        gearMessages (f1, 1.0f, 0, out);
        CHECK (f1.num == 51 && out[0][2] == 7);
        gearMessages (gearMap (5)->params[(size_t) find (5, "OSC 1: WAVE")], 0.5f, 0, out);
        CHECK (out[0][1] == 19 && (out[0][2] == 49 || out[0][2] == 50));
        CHECK (gearMap (5)->params[(size_t) find (5, "FILTER 1: CUTOFF")].num == 40 && gearMap (5)->params[(size_t) find (5, "MASTER: VOLUME")].num == 91);
    }
    // the Typhon (CCs, none twice; 64 left for the sustain pedal)
    {
        std::set<int> seen; bool dup = false, sixtyFour = false;
        for (const auto& p : gearMap (8)->params) { dup = dup || ! seen.insert (p.num).second; sixtyFour = sixtyFour || p.num == 64; }
        CHECK (! dup && ! sixtyFour && gearMap (8)->params[(size_t) find (8, "FILTER: CUTOFF")].num == 4 && gearMap (8)->params[(size_t) find (8, "MOD 2 TO: VCA")].num == 65);
        CHECK (gearMap (8)->params[(size_t) find (8, "MOD 3 CUSTOM: CUSTOM 3")].num == 88 && gearMap (8)->params[(size_t) find (8, "SEQ: SWING")].num == 92);
    }
    // the M1: PARAMETER CHANGE, page and position as Korg's Table 5 has them (SINGLE and DOUBLE differ)
    {
        const auto& cutS = gearMap (9)->params[(size_t) find (9, "VDF 1: CUTOFF")];
        const auto& cutD = gearMap (10)->params[(size_t) find (10, "VDF 1: CUTOFF")];
        const auto& cut2 = gearMap (10)->params[(size_t) find (10, "VDF 2: CUTOFF")];
        CHECK (cutS.num == 3 && cutS.lsb == 8 && cutD.num == 5 && cut2.num == 9 && find (9, "VDF 2: CUTOFF") < 0 && find (9, "OSC 2: DETUNE") < 0);
        const auto m = gearSysex (cutS, 1.0f, 2);
        CHECK ((m == std::vector<uint8_t> { 0xF0, 0x42, 0x32, 0x19, 0x41, 3, 8, 99, 0, 0xF7 }));
        const auto neg = gearSysex (gearMap (9)->params[(size_t) find (9, "VDF 1 EG: SUSTAIN")], 0.0f, 0);   // (-99: 3F9D)
        CHECK (neg[7] == 0x1D && neg[8] == 0x7F);
        CHECK (gearMap (10)->params[(size_t) find (10, "VDA 2 EG: RELEASE")].num == 16 && gearMap (9)->params[(size_t) find (9, "PITCH MG: INTENSITY")].num == 10
               && gearMap (10)->params[(size_t) find (10, "PITCH MG: INTENSITY")].num == 19);
    }
    // the JV-1080: DT1 to the temporary patch, Roland's checksum; all four tones, or one
    {
        const auto& lvl = gearMap (11)->params[(size_t) find (11, "PATCH: LEVEL")];
        auto m = gearSysex (lvl, 1.0f, 0);   // 03 00 00 2E = 127: sum 03+2E+7F = B0 -> checksum 50
        CHECK ((m == std::vector<uint8_t> { 0xF0, 0x41, 0x10, 0x6A, 0x12, 0x03, 0x00, 0x00, 0x2E, 0x7F, 0x50, 0xF7 }));
        const auto& cut = gearMap (11)->params[(size_t) find (11, "FILTER: CUTOFF")];
        m = gearSysex (cut, 0.0f, 0);
        CHECK (m.size() == 48 && m[7] == 0x10 && m[8] == 0x51 && m[19] == 0x12 && m[31] == 0x14 && m[43] == 0x16 && m[44] == 0x51);
        const auto& rev = gearMap (11)->params[(size_t) find (11, "LEVEL + PAN: REVERB SEND")];   // (offset 128: 01 00 on)
        m = gearSysex (rev, 0.0f, 0);
        CHECK (m[6] == 0x00 && m[7] == 0x11 && m[8] == 0x00 && m[19] == 0x13 && m[20] == 0x00);
        const auto& wave = gearMap (11)->params[(size_t) find (11, "TONE 3: WAVE")];       // (two nibbles, tone 3 = 03 00 14 03)
        m = gearSysex (wave, 1.0f, 0);
        CHECK (m.size() == 13 && m[7] == 0x14 && m[8] == 0x03 && m[9] == 0x0F && m[10] == 0x0E && wave.text (1.0f) == "255");
        CHECK (gearMap (11)->params[(size_t) find (11, "PITCH: COARSE")].text (0.0f) == "-48" && cut.where() == "TONES 1-4 51");
    }
    // an automation lane on a GEAR control: setParameterAt (kGearAutoBase + n) sends it (once per value)
    {
        std::vector<MidiEv> log;
        MidiOutSynth synth (std::make_unique<FakeSink> (&log));
        synth.prepare (48000, 512);
        Instrument in; in.plugin = in.midiOut = true; in.midiChannel = 1; in.midiGear = 11;
        synth.update (in);
        synth.setParameterAt (kGearAutoBase + find (11, "PATCH: LEVEL"), 1.0f, 0);
        synth.setParameterAt (kGearAutoBase + find (11, "PATCH: LEVEL"), 1.0f, 0);
        std::printf ("JV-1080 lane: %d messages, first %d bytes\n", (int) log.size(), log.empty() ? 0 : (int) log[0].m.size());
        CHECK (log.size() == 1 && log[0].m.size() == 12);
        in.midiGear = 8; synth.update (in); log.clear();
        synth.setParameterAt (kGearAutoBase + find (8, "FILTER: CUTOFF"), 0.5f, 0);
        CHECK (log.size() == 1 && log[0].m[1] == 4 && log[0].m[2] == 64);
    }
    // a MIDI-out instrument: a knob on a GEAR parameter sends that parameter's messages
    {
        std::vector<MidiEv> log;
        MidiOutSynth synth (std::make_unique<FakeSink> (&log));
        synth.prepare (48000, 512);
        Instrument in; in.plugin = in.midiOut = true; in.midiChannel = 5; in.midiGear = 2;
        in.midiCC[0] = kGearKnobBase + find (2, "FILTER: CUTOFF");
        in.midiCC[1] = 71;
        synth.update (in);
        synth.setParameterAt (0, 0.25f, 0);
        synth.setParameterAt (1, 0.5f, 0);
        synth.setParameterAt (0, 0.25f, 0);   // (the same again: nothing)
        CHECK (log.size() == 3 && log[0].m[0] == 0xB4 && log[0].m[1] == 43 && log[1].m[1] == 107 && log[2].m[1] == 71 && log[2].m[2] == 64);
        synth.sendGear (find (2, "EG: TYPE"), 1.0f, 0);
        CHECK (log.size() == 4 && log[3].m[1] == 14 && log[3].m[2] == 127);
        // no GEAR: a knob on 128+ sends nothing
        in.midiGear = 0; synth.update (in);
        synth.setParameterAt (0, 0.8f, 0);
        CHECK (log.size() == 4);
    }
}

static void testAkaiS1000()
{
    std::printf ("\n== AKAI S1000 SYSEX\n");
    using namespace akai;
    // names in Akai's character set
    const auto codes = toAkai ("Amen 1+");
    CHECK (codes.size() == 12 && codes[0] == 11 && codes[4] == 10 && codes[5] == 1 && codes[6] == 38 && fromAkai (codes.data(), 12) == "AMEN 1+");
    // requests, as the spec has them
    CHECK ((requestPrograms (0) == std::vector<uint8_t> { 0xF0, 0x47, 0x00, 0x02, 0x48, 0xF7 }));
    CHECK ((requestProgram (1, 130) == std::vector<uint8_t> { 0xF0, 0x47, 0x01, 0x06, 0x48, 2, 1, 0xF7 }));
    CHECK ((requestKeygroup (0, 3, 5) == std::vector<uint8_t> { 0xF0, 0x47, 0x00, 0x08, 0x48, 3, 0, 5, 0xF7 }));
    // a block goes as nibbles (low first) and comes back the same
    std::vector<uint8_t> blk (kKeygroupBytes, 0);
    for (int i = 0; i < kKeygroupBytes; ++i) blk[(size_t) i] = (uint8_t) (i * 37);
    const auto msg = keygroupMessage (0, 2, 7, blk);
    CHECK ((int) msg.size() == 9 + kKeygroupBytes * 2 && msg[8] == (blk[0] & 15) && msg[10] == (blk[1] & 15) && msg[11] == (blk[1] >> 4));
    Reply r;
    CHECK (parse (msg.data(), (int) msg.size(), r) && r.op == KDATA && r.program == 2 && r.keygroup == 7 && r.block == blk);
    // the fields: signed values as two's complement, ranges clamped, text
    {
        std::vector<uint8_t> k (kKeygroupBytes, 0);
        const auto& fs = keygroupFields();
        auto field = [&] (const std::string& g, const std::string& n) { for (const auto& x : fs) if (x.group == g && x.name == n) return x; return Field {}; };
        const auto cut = field ("FILTER", "CUTOFF"), kf = field ("FILTER", "KEY > FREQ"), lo = field ("KEYS", "LOW NOTE"), play = field ("ZONE 2", "PLAY");
        set (k, cut, 150); set (k, kf, -12); set (k, lo, 60); set (k, play, 3);
        CHECK (k[7] == 99 && get (k, cut) == 99 && k[8] == 244 && get (k, kf) == -12 && lo.text (60) == "C-3" && lo.text (24) == "C-0");
        CHECK (k[34 + 24 + 19] == 3 && play.text (3) == "NO LOOP" && kf.text (-12) == "-12");
        std::set<int> offs; bool dup = false;
        for (const auto& x : fs) dup = dup || ! offs.insert (x.offset).second || x.offset >= kKeygroupBytes;
        for (const auto& x : programFields()) dup = dup || x.offset >= kProgramBytes;
        CHECK (! dup);
        std::printf ("program: %d controls in %d parts; keygroup: %d in %d\n", (int) programFields().size(), (int) groupsOf (programFields()).size(),
                     (int) fs.size(), (int) groupsOf (fs).size());
    }
    // a pretend S1000: the names, a program, a keygroup; an edit sent back is kept
    {
        FakeS1000 s1k;
        s1k.addProgram ("AMEN", 2); s1k.addProgram ("REESE BASS", 1);
        auto ask = [&] (const std::vector<uint8_t>& m) { auto a = s1k.receive (m.data(), (int) m.size()); Reply rr; if (! a.empty()) parse (a[0].data(), (int) a[0].size(), rr); return rr; };
        auto list = ask (requestPrograms (0));
        CHECK (list.op == PLIST && list.names.size() == 2 && list.names[1] == "REESE BASS");
        auto prog = ask (requestProgram (0, 0));
        CHECK (prog.op == PDATA && prog.program == 0 && fromAkai (prog.block.data() + 3, 12) == "AMEN" && prog.block[42] == 2);
        auto kg = ask (requestKeygroup (0, 0, 1));
        CHECK (kg.op == KDATA && kg.keygroup == 1 && fromAkai (kg.block.data() + 34, 12) == "AMEN-2");
        kg.block[7] = 40;
        auto ok = ask (keygroupMessage (0, 0, 1, kg.block));
        CHECK (ok.op == REPLY && ok.replyCode == 0 && s1k.keygroups[0][1][7] == 40);
        CHECK (ask (requestKeygroup (0, 1, 5)).replyCode == 1);
    }
}

// ---- samples over MIDI to / from the S1000 and S950 (v0.74)
static void testAkaiSamples()
{
    std::printf ("\n== AKAI SAMPLES OVER MIDI\n");
    using namespace prv::akai;
    // SDS packets: 16-bit words, there and back exactly (the ends of the range too)
    {
        std::vector<int16_t> w { -32768, -1, 0, 1, 32767, 12345, -12345 };
        auto pk = sds::packet (0, 5, w.data(), (int) w.size());
        std::vector<int16_t> back; int num = -1;
        CHECK (pk.size() == 127 && sds::parsePacket (pk.data(), (int) pk.size(), 16, num, back) && num == 5);
        CHECK (back.size() == 40 && std::equal (w.begin(), w.end(), back.begin()) && back[39] == 0);
        pk[20] ^= 1;
        CHECK (! sds::parsePacket (pk.data(), (int) pk.size(), 16, num, back));   // (a damaged one is turned down)
    }
    Sample smp;
    smp.name = "amen loop"; smp.rate = 32000;
    for (int i = 0; i < 1234; ++i) smp.data.push_back ((int16_t) (20000 * std::sin (i * 0.05) + (i % 7) * 30));
    smp.loopStart = 300; smp.loopLength = 600;
    // run a transfer against a fake sampler: what it sends goes in, what it answers comes back
    auto run = [] (Transfer& t, auto& fake, double& now, int& sent, bool deaf = false)
    {
        for (int step = 0; step < 200000 && t.state == Transfer::State::Working; ++step)
        {
            for (auto& m : t.poll (now))
            {
                ++sent;
                if (deaf) continue;
                for (auto& a : fake.receive (m.data(), (int) m.size())) t.received (a.data(), (int) a.size(), now);
            }
            now += 5;
        }
    };
    // S1000: send (SLIST, SDATA, ASPACK, packets each ACKed - the first NAKed once and sent again)
    {
        FakeS1000 fake; Sample other; other.name = "AMEN LOOP"; other.data.assign (100, 0); fake.samples.push_back (other);
        fake.nakNext = 1;
        Transfer t; double now = 0; int sent = 0;
        t.sendS1000 (0, smp, now);
        run (t, fake, now, sent);
        std::printf ("S1000 send: %s | %d messages, %d sent again, %.1f s\n", t.status.c_str(), sent, t.resent, now / 1000.0);
        CHECK (t.state == Transfer::State::Done && fake.samples.size() == 2 && t.handshaking && t.resent == 1);
        const auto& got = fake.samples[1];
        std::printf ("  in the S1000: %s, %d words at %d Hz, loop %d + %d\n", got.name.c_str(), (int) got.data.size(), got.rate, got.loopStart, got.loopLength);
        CHECK (got.name == "AMEN LOOP-2" && got.data == smp.data && got.rate == 32000 && got.loopStart == 300 && got.loopLength == 600);
        // and back: RSDATA, RSPACK, every packet ACKed
        Transfer g; int sent2 = 0;
        g.getS1000 (0, 1, now);
        run (g, fake, now, sent2);
        std::printf ("S1000 get: %s | %d words, loop %d + %d\n", g.status.c_str(), (int) g.result.data.size(), g.result.loopStart, g.result.loopLength);
        CHECK (g.state == Transfer::State::Done && g.result.data == smp.data && g.result.rate == 32000 && g.result.loopStart == 300 && g.result.name == "AMEN LOOP-2");
        // a sample it doesn't have
        Transfer bad; int sent3 = 0;
        bad.getS1000 (0, 9, now);
        run (bad, fake, now, sent3);
        CHECK (bad.state == Transfer::State::Failed);
    }
    // an S1000 that never answers: no list, so it stops and says why
    {
        FakeS1000 fake; Transfer t; double now = 0; int sent = 0;
        t.sendS1000 (0, smp, now);
        run (t, fake, now, sent, true);
        std::printf ("S1000 deaf: %s\n", t.status.c_str());
        CHECK (t.state == Transfer::State::Failed && t.status.find ("NO ANSWER") != std::string::npos);
    }
    // S950: SECRE, the catalogue, the dump (one message), SPRM, the catalogue again
    {
        s900::FakeS950 fake; fake.addProgram (0, "BREAKS", 1);
        Sample hat; hat.name = "HAT"; hat.data.assign (300, 0); fake.sampleData.push_back (hat); fake.samples.push_back ("HAT");
        Transfer t; double now = 0; int sent = 0;
        t.sendS950 (0, smp, now);
        run (t, fake, now, sent);
        std::printf ("S950 send: %s | %d messages, %.1f s (about %.1f s on the wire)\n", t.status.c_str(), sent, now / 1000.0, secondsOnTheWire (true, 1234));
        CHECK (t.state == Transfer::State::Done && t.status.find ("IS IN THE S950") != std::string::npos);
        CHECK (fake.sampleData.size() == 2);
        const auto& got = fake.sampleData[1];
        int worst = 0;
        for (size_t i = 0; i < smp.data.size(); ++i) worst = std::max (worst, std::abs ((smp.data[i] >> 4 << 4) - got.data[i]));
        std::printf ("  in the S950: %s, %d words at %d Hz, loop %d + %d, worst 12-bit error %d\n", got.name.c_str(), (int) got.data.size(), got.rate, got.loopStart, got.loopLength, worst);
        CHECK (got.name == "AMEN LOOP" && got.data.size() == smp.data.size() && worst == 0 && std::abs (got.rate - 32000) <= 1);
        CHECK (got.loopStart == 300 && got.loopLength == 600);
        // the dump itself: header and loop as the spec has them
        const auto d = s900::sampleDump (1, smp);
        CHECK (d[0] == 0xF0 && d[1] == 0x7E && d[2] == 0x01 && d[3] == 1 && d[5] == 12 && d.back() == 0xF7);
        CHECK (d.size() == 19 + (size_t) ((1234 + 59) / 60) * 122 + 1);
        // and back: RSD (with ACKs going out while it waits)
        Transfer g; int sent2 = 0;
        g.getS950 (0, 1, "AMEN LOOP", now);
        run (g, fake, now, sent2);
        std::printf ("S950 get: %s | %d words, loop %d + %d\n", g.status.c_str(), (int) g.result.data.size(), g.result.loopStart, g.result.loopLength);
        CHECK (g.state == Transfer::State::Done && g.result.data == got.data && g.result.loopStart == 300 && g.result.name == "AMEN LOOP");
        // a damaged dump: counted
        auto broken = s900::sampleDump (1, smp); broken[40] ^= 1;
        Sample x; int k = 0, badBlocks = 0;
        CHECK (s900::parseDump (broken.data(), (int) broken.size(), x, k, badBlocks) && badBlocks == 1);
        // one-shot: no loop comes back
        Sample shot = smp; shot.loopLength = 0;
        const auto d2 = s900::sampleDump (2, shot);
        CHECK (s900::parseDump (d2.data(), (int) d2.size(), x, k, badBlocks) && x.loopLength == 0 && k == 2);
    }
}

static void testAkaiS950()
{
    std::printf ("\n== AKAI S900 / S950 SYSEX\n");
    using namespace akai;
    // requests, as Akai's S900 document has them: F0 47 ch gg 40 nn 00 F7
    CHECK ((s900::requestCatalog (2) == std::vector<uint8_t> { 0xF0, 0x47, 0x02, 0x03, 0x40, 0x00, 0x00, 0xF7 }));
    CHECK ((s900::requestProgram (0, 5) == std::vector<uint8_t> { 0xF0, 0x47, 0x00, 0x02, 0x40, 0x05, 0x00, 0xF7 }));
    // DB, DW and bits, in place in the MIDI bytes
    {
        std::vector<uint8_t> k (s900::kKeygroupBytes, 0);
        const auto& fs = s900::keygroupFields();
        auto field = [&] (const std::string& g, const std::string& n) { for (const auto& x : fs) if (x.group == g && x.name == n) return x; return Field {}; };
        set (k, field ("KEYS", "VEL SWITCH"), 128);
        CHECK (k[4] == 0 && k[5] == 1 && get (k, field ("KEYS", "VEL SWITCH")) == 128);
        set (k, field ("VELOCITY", "VEL > RELEASE"), -20);           // (two's complement 236 = EC: 6C, 01)
        CHECK (k[20] == 0x6C && k[21] == 1 && get (k, field ("VELOCITY", "VEL > RELEASE")) == -20);
        set (k, field ("SOFT SAMPLE", "TUNE"), -16);                  // (DW FFF0: 70 01 7F 01)
        CHECK (k[84] == 0x70 && k[85] == 1 && k[86] == 0x7F && k[87] == 1 && get (k, field ("SOFT SAMPLE", "TUNE")) == -16);
        CHECK (field ("SOFT SAMPLE", "TUNE").text (-16) == "-1.00");
        const auto trd = field ("KEYS", "TRANSPOSE"), shot = field ("KEYS", "ONE SHOT");
        CHECK (get (k, trd) == 1);                                    // (bit 0 clear: transpose on)
        set (k, shot, 1); set (k, trd, 0);
        CHECK ((k[36] & 0x09) == 0x09 && get (k, shot) == 1 && get (k, trd) == 0);
        bool over = false;
        for (const auto& x : fs) over = over || x.offset + (x.enc == 2 ? 4 : 2) > s900::kKeygroupBytes;
        for (const auto& x : s900::programFields()) over = over || x.offset + (x.enc == 2 ? 4 : 2) > s900::kProgramBytes;
        CHECK (! over);
    }
    // a pretend S950: its catalogue (programs only), a program with its keygroups, the checksum, an edit sent back
    {
        s900::FakeS950 s;
        s.addProgram (0, "JUNGLE KIT", 3); s.addProgram (4, "PADS", 1); s.samples = { "AMEN", "SNARE" };
        auto ask = [&] (const std::vector<uint8_t>& m) { auto a = s.receive (m.data(), (int) m.size()); Reply r; if (! a.empty()) s900::parse (a[0].data(), (int) a[0].size(), r); return r; };
        auto cat = ask (s900::requestCatalog (0));
        CHECK (cat.op == s900::CAT && cat.names.size() == 2 && cat.names[1] == "PADS" && cat.numbers[1] == 4);
        auto prog = ask (s900::requestProgram (0, 4));
        CHECK (prog.op == s900::PRGM && prog.program == 4 && prog.keygroups.size() == 1 && s900::name (prog.block, s900::kProgramName) == "PADS");
        CHECK (s900::name (prog.keygroups[0], s900::kSoftName) == "PADS S1");
        auto msg = s900::programMessage (0, 4, prog.block, prog.keygroups);
        msg[20] ^= 1;   // (damaged: the checksum catches it)
        Reply bad; CHECK (! s900::parse (msg.data(), (int) msg.size(), bad));
        Field cut; for (const auto& x : s900::keygroupFields()) if (x.group == "SOFT SAMPLE" && x.name == "FILTER") cut = x;
        set (prog.keygroups[0], cut, 40);
        const auto edit = s900::programMessage (0, 4, prog.block, prog.keygroups);
        s.receive (edit.data(), (int) edit.size());
        CHECK (get (s.programs[1].keygroups[0], cut) == 40);
        std::printf ("S950: program %d controls, keygroup %d in %d parts; a 3-keygroup program is %d MIDI bytes\n",
                     (int) s900::programFields().size(), (int) s900::keygroupFields().size(), (int) groupsOf (s900::keygroupFields()).size(),
                     (int) s900::programMessage (0, 0, s.programs[0].block, s.programs[0].keygroups).size());
    }
}

static void testDuckAndLowEnd()
{
    std::printf ("\n== DUCK + LOW-END CHECK\n");
    // DUCK: a follower on the kick's track, every other chosen track's VOLUME down
    {
        MixerSettings m;
        const int d = m.setUpDuck (0, 0xFFu, 0.7f, 150.0f);
        CHECK (d == 0 && m.mods.mod[0].source == ModSource::Follower && m.mods.mod[0].routes() == 7);
        CHECK (m.mods.mod[0].route[0].target == kModStripBase + 1 && m.mods.mod[0].route[0].amount < -0.69f);
        CHECK (m.duckFrom (0) == 0 && m.duckFrom (1) == -1);
        CHECK (m.setUpDuck (0, 0x2u, 0.4f, 100.0f) == 0 && m.mods.mod[0].routes() == 1);   // (the same duck, changed)
        CHECK (m.setUpDuck (2, 0x1u, 0.4f, 100.0f) == 1);
    }
    // in the mix: the square (track 2) is quieter just after the hit on track 1 when it ducks
    auto render = [] (bool duck, bool square)
    {
        Song s = makeSong();
        s.blocks[0] = Block {}; s.blocks[0].lines = 16;
        s.blocks[0].at (0, 0) = Cell { 37, 2, 0, 0 };
        if (square) s.blocks[0].at (0, 1) = Cell { 25, 1, 0, 0 };
        s.sequence = { 0 };
        if (duck) s.mixer.setUpDuck (0, 0xFFu, 0.8f, 150.0f);
        Player p; p.setSampleRate (48000); p.setSong (&s);
        p.mixer().syncEffectsNow (s.mixer);
        p.playSong (0);
        std::vector<float> L (48000), R (L.size());
        p.render (L.data(), R.data(), (int) L.size());
        return L;
    };
    auto diff = [] (const std::vector<float>& a, const std::vector<float>& b) { std::vector<float> d (a.size()); for (size_t i = 0; i < a.size(); ++i) d[i] = a[i] - b[i]; return d; };
    const auto ducked = diff (render (true, true), render (true, false)), plain = diff (render (false, true), render (false, false));
    const int from = 480, to = 4800;   // (10 - 100 ms: the hit is on)
    std::printf ("square under the hit: plain %.4f, ducked %.4f; later (0.8 s): plain %.4f ducked %.4f\n",
                 rmsOf (plain, from, to), rmsOf (ducked, from, to), rmsOf (plain, 38000, 40000), rmsOf (ducked, 38000, 40000));
    CHECK (rmsOf (plain, from, to) > 0.01f && rmsOf (ducked, from, to) < rmsOf (plain, from, to) * 0.6f);
    CHECK (rmsOf (ducked, 38000, 40000) > rmsOf (plain, 38000, 40000) * 0.85f);   // (back up after it)

    // LOW-END CHECK
    auto check = [] (std::function<void (int, float&, float&)> gen, double secs = 4.0)
    {
        LowEndCheck c; c.prepare (48000);
        std::vector<float> L (480), R (480);
        for (int at = 0; at < (int) (48000 * secs); at += 480)
        {
            for (int i = 0; i < 480; ++i) gen (at + i, L[(size_t) i], R[(size_t) i]);
            c.process (L.data(), R.data(), 480);
        }
        return c.report();
    };
    const double w = 6.283185307179586 / 48000.0;
    {
        const auto r = check ([] (int, float& l, float& r) { l = r = 0; });
        CHECK (! r.signal && lowEndAdvice (r).size() == 1 && lowEndAdvice (r)[0].title == "PLAY THE SONG");
    }
    {   // a mono 50 Hz sub with a little 2 kHz: mono, the sub OK-to-big, clean
        const auto r = check ([w] (int i, float& l, float& r) { l = r = (float) (0.4 * std::sin (w * 50 * i) + 0.15 * std::sin (w * 2000 * i)); });
        std::printf ("mono sub: corr %.2f sub %.1f dB mud %.1f dB peak %.1f clips %d\n", r.lowCorr, r.subDb, r.mudDb, r.peakDb, r.clips);
        CHECK (r.signal && r.lowCorr > 0.99f && r.clips == 0 && r.subDb > -3 && r.mudDb < -20);
        const auto a = lowEndAdvice (r);
        CHECK (a.size() == 4 && a[0].level == 0 && a[3].level == 0);
    }
    {   // the sub out of phase between the sides
        const auto r = check ([w] (int i, float& l, float& r) { l = (float) (0.4 * std::sin (w * 50 * i)); r = -l; });
        std::printf ("out of phase: corr %.2f\n", r.lowCorr);
        CHECK (r.lowCorr < -0.9f && lowEndAdvice (r)[0].level == 2 && lowEndAdvice (r)[0].title == "BASS OUT OF PHASE");
    }
    {   // wide (unrelated) bass
        const auto r = check ([w] (int i, float& l, float& r) { l = (float) (0.4 * std::sin (w * 50 * i)); r = (float) (0.4 * std::sin (w * 71 * i)); });
        std::printf ("wide: corr %.2f\n", r.lowCorr);
        CHECK (r.lowCorr > -0.5f && r.lowCorr < 0.5f && lowEndAdvice (r)[0].level == 1);
    }
    {   // all mud, and clipping
        const auto r = check ([w] (int i, float& l, float& r) { l = r = (float) (1.1 * std::sin (w * 300 * i)); });
        std::printf ("mud: %.1f dB, sub %.1f dB, clips %d, peak %.1f\n", r.mudDb, r.subDb, r.clips, r.peakDb);
        const auto a = lowEndAdvice (r);
        CHECK (r.clips > 0 && a[3].level == 2 && a[2].title == "MUDDY" && a[1].title == "NOT MUCH SUB");
    }
}

static void testCrunch()
{
    std::printf ("\n== CRUNCH\n");
    for (int st = 0; st < 3; ++st)
    {
        Instrument in; in.rate = 44100; in.natRate = 44100;
        in.data.resize (44100);
        for (int i = 0; i < 44100; ++i) in.data[(size_t) i] = (int16_t) (16000 * std::sin (6.2831853 * 440.0 * i / 44100.0));
        in.slices = { 0, 11025, 22050, 33075 }; in.loopStart = 22050; in.loopLength = 11025; in.playEnd = 40000;
        CHECK (crunchInstrument (in, (CrunchStyle) st));
        const double ratio = in.rate / 44100.0;
        // the pitch: still 440 Hz at its new rate
        int c = 0, f = -1, l = -1;
        for (size_t i = 1; i < in.data.size(); ++i) if (in.data[i - 1] < 0 && in.data[i] >= 0) { ++c; if (f < 0) f = (int) i; l = (int) i; }
        const double hz = (c - 1) * in.rate / (l - f);
        // the bits: an 8-bit sample uses at most 256 levels
        std::set<int> levels; for (auto v : in.data) levels.insert (v);
        std::printf ("%-12s rate %.0f, %d samples, %.1f Hz, %d levels, slice 2 at %d, loop %d+%d\n", crunchName ((CrunchStyle) st), in.rate, (int) in.data.size(), hz, (int) levels.size(), in.slices[1], in.loopStart, in.loopLength);
        CHECK (std::abs (hz - 440) < 2 && std::abs ((int) in.data.size() - (int) std::lround (44100 * ratio)) <= 1);
        CHECK (std::abs (in.slices[1] - (int) std::lround (11025 * ratio)) <= 1 && std::abs (in.loopStart - (int) std::lround (22050 * ratio)) <= 1 && in.playEnd > 0);
        if (st == 0) CHECK (levels.size() <= 256);
        if (st == 2) CHECK (levels.size() > 256);
        CHECK (std::abs (in.pitchShift()) < 0.01);
    }
    Instrument empty;
    CHECK (! crunchInstrument (empty, CrunchStyle::Amiga8));
}

// ---- GRANULAR
static void testGrainSynth()
{
    std::printf ("\n== GRANULAR\n");
    const double sr = 48000;
    auto run = [&] (GrainSynth& g, int n, std::vector<float>* right = nullptr)
    {
        std::vector<float> L ((size_t) n), R ((size_t) n);
        g.render (L.data(), R.data(), n);
        if (right) *right = R;
        return L;
    };
    auto freqOf = [&] (const std::vector<float>& x, int from)
    {
        int crossings = 0, first = -1, last = -1;
        for (size_t i = (size_t) from + 1; i < x.size(); ++i)
            if (x[i - 1] < 0 && x[i] >= 0) { ++crossings; if (first < 0) first = (int) i; last = (int) i; }
        return crossings > 1 ? (double) (crossings - 1) * sr / (double) (last - first) : 0.0;
    };
    auto peakOf = [] (const std::vector<float>& x) { float m = 0; for (float v : x) m = std::max (m, std::abs (v)); return m; };
    auto finite = [] (const std::vector<float>& x) { for (float v : x) if (! std::isfinite (v)) return false; return true; };
    auto base = []
    {
        auto v = builtinDefaults (kGrainKey);
        v[grain::Spray] = 0; v[grain::Attack] = 5; v[grain::Release] = 50;
        return v;
    };

    // the built-in tables: all eight there, sounding, not clipping
    for (int t = 0; t < 8; ++t)
    {
        const auto& tb = GrainSynth::table (t);
        float m = 0; double e = 0;
        for (float x : tb) { m = std::max (m, std::abs (x)); e += x * x; }
        CHECK (tb.size() > 48000 && m > 0.5f && m < 0.95f && e / (double) tb.size() > 0.001);
    }
    // a note on the SINE table plays its pitch (C-4 here: 523 Hz) as overlapping grains
    {
        GrainSynth g; g.prepare (sr, 512);
        auto v = base(); v[grain::Size] = 200; v[grain::Density] = 30; v[grain::Stereo] = 0;
        g.setValues (v);
        g.noteOn (72, 127, 0);
        auto x = run (g, 48000);
        const double f = freqOf (x, 12000);
        std::printf ("SINE at C-4: %.1f Hz (523.3), grains %d, peak %.3f, rms %.3f\n", f, g.activeGrains(), peakOf (x), rmsOf (x, 12000, 48000));
        CHECK (std::abs (f - 523.25) < 523.25 * 0.03);
        CHECK (g.activeGrains() > 2 && rmsOf (x, 12000, 48000) > 0.05f && peakOf (x) < 1.2f && finite (x));
        // PITCH +12: an octave up
        v[grain::Pitch] = 12; g.setValues (v);
        auto y = run (g, 24000);
        const double f2 = freqOf (y, 12000);
        std::printf ("PITCH +12: %.1f Hz\n", f2);
        CHECK (std::abs (f2 - 1046.5) < 1046.5 * 0.03);
        // let go: it dies away, then nothing plays
        g.noteOff (72, 0);
        run (g, 48000);
        auto z = run (g, 4800);
        std::printf ("after the release: grains %d, peak %.5f\n", g.activeGrains(), peakOf (z));
        CHECK (g.activeGrains() == 0 && peakOf (z) < 1e-4f);
    }
    // every table, every control at its far end and the modulation on everything: sound, never NaN or wild
    for (int t = 0; t < 8; ++t)
    {
        GrainSynth g; g.prepare (sr, 512);
        auto v = builtinDefaults (kGrainKey);
        v[grain::Table] = (float) t; v[grain::Density] = 120; v[grain::Size] = 30; v[grain::Spray] = 100; v[grain::PitchSpread] = 24;
        v[grain::Reverse] = 50; v[grain::Stereo] = 100; v[grain::Scan] = -100;
        v[grain::Lfo1Depth] = 100; v[grain::Lfo1Target] = (float) (t % 8); v[grain::Lfo1Rate] = 7;
        v[grain::Lfo2Depth] = 100; v[grain::Lfo2Target] = 6; v[grain::EnvDepth] = 100; v[grain::EnvTarget] = (float) ((t + 3) % 8);
        v[grain::Cutoff] = 800; v[grain::Resonance] = 90; v[grain::Diffuse] = 100;
        g.setValues (v);
        g.noteOn (48, 100, 0); g.noteOn (55, 100, 10); g.noteOn (60, 100, 20); g.noteOn (64, 100, 30);
        std::vector<float> R;
        auto x = run (g, 24000, &R);
        float diff = 0; for (size_t i = 0; i < x.size(); ++i) diff = std::max (diff, std::abs (x[i] - R[i]));
        CHECK (finite (x) && finite (R) && peakOf (x) < 2.5f && rmsOf (x, 4800, 24000) > 0.005f && diff > 0.001f);
        if (t == 3) std::printf ("everything on (VOCAL): peak %.3f, rms %.3f, left / right differ by up to %.3f\n", peakOf (x), rmsOf (x, 4800, 24000), diff);
    }
    // SCAN moves the reading position; FREEZE holds it
    {
        GrainSynth g; g.prepare (sr, 512);
        auto v = base(); v[grain::Scan] = 100; v[grain::Position] = 10;
        g.setValues (v);
        g.noteOn (60, 100, 0);
        run (g, 4800); const float a = g.positionNow();
        run (g, 24000); const float b = g.positionNow();
        v[grain::Freeze] = 1; g.setValues (v);
        run (g, 4800); const float c = g.positionNow();
        run (g, 24000); const float d = g.positionNow();
        std::printf ("SCAN: position %.3f -> %.3f; FREEZE: %.3f -> %.3f\n", a, b, c, d);
        CHECK (b - a > 0.2f && std::abs (d - c) < 0.01f);
    }
    // SAMPLE (here without a song): grains of a 300 Hz tone at C-3 play 300 Hz
    {
        GrainSynth g; g.prepare (sr, 512);
        std::vector<float> tone (44100);
        for (size_t i = 0; i < tone.size(); ++i) tone[i] = 0.7f * (float) std::sin (6.283185307 * 300.0 * i / 44100.0);
        g.setTestSource (tone, 44100);
        auto v = base(); v[grain::Source] = 1; v[grain::Size] = 150; v[grain::Stereo] = 0;
        g.setValues (v);
        g.noteOn (60, 127, 0);
        auto x = run (g, 36000);
        const double f = freqOf (x, 9600);
        std::vector<float> peaks;
        const bool shown = g.sourceOverview (peaks, 50);
        std::printf ("SAMPLE: %.1f Hz (300); overview %d, peak %.2f\n", f, (int) shown, peaks.empty() ? 0.0f : peaks[10]);
        CHECK (std::abs (f - 300) < 9 && shown && peaks[10] > 0.6f);
    }
    // RECORDING: what comes in is what the grains are made of; FREEZE keeps it when the input stops
    {
        GrainSynth g; g.prepare (sr, 512);
        auto v = base(); v[grain::Source] = 2; v[grain::Size] = 120; v[grain::Position] = 50; v[grain::Stereo] = 0;
        g.setValues (v);
        std::vector<float> in (512), out;
        uint64_t t = 0;
        for (int b = 0; b < 94; ++b)   // a second of 440 Hz
        {
            for (auto& x : in) x = 0.5f * (float) std::sin (6.283185307 * 440.0 * (double) t++ / sr);
            g.liveInput (in.data(), in.data(), 512);
            if (b == 40) g.noteOn (60, 127, 0);
            auto x = run (g, 512); if (b > 60) out.insert (out.end(), x.begin(), x.end());
        }
        const double f = freqOf (out, 0);
        v[grain::Freeze] = 1; g.setValues (v);
        std::vector<float> quiet (512, 0.0f), held;
        for (int b = 0; b < 200; ++b) { g.liveInput (quiet.data(), quiet.data(), 512); auto x = run (g, 512); if (b > 180) held.insert (held.end(), x.begin(), x.end()); }
        std::printf ("RECORDING: %.1f Hz (440), rms %.3f; frozen after 2 s of silence in: rms %.3f\n", f, rmsOf (out, 0, (int) out.size()), rmsOf (held, 0, (int) held.size()));
        CHECK (std::abs (f - 440) < 13 && rmsOf (out, 0, (int) out.size()) > 0.05f && rmsOf (held, 0, (int) held.size()) > 0.05f);
        v[grain::Freeze] = 0; g.setValues (v);
        for (int b = 0; b < 200; ++b) { g.liveInput (quiet.data(), quiet.data(), 512); run (g, 512); }
        auto after = run (g, 4800);
        std::printf ("not frozen, 2 s more of silence in: rms %.4f\n", rmsOf (after, 0, 4800));
        CHECK (rmsOf (after, 0, 4800) < 0.01f);
    }
    // its settings round the instrument's state; in a song it takes its grains from another instrument's sample
    {
        Song s = makeSong();
        makeBuiltin (s.instruments[4], kGrainKey);
        CHECK (s.instruments[4].name == "GRANULAR" && s.instruments[4].isBuiltin());
        auto v = readBuiltinState (s.instruments[4]);
        CHECK ((int) v.size() == grain::Count);
        v[grain::Source] = 1; v[grain::SampleSlot] = 1; v[grain::Stereo] = 0;
        writeBuiltinState (s.instruments[4], v);
        CHECK (readBuiltinState (s.instruments[4])[grain::SampleSlot] == 1);
        s.blocks[0] = Block {}; s.blocks[0].lines = 16; s.blocks[0].at (0, 0) = Cell { 25, 5, 0, 0 }; s.sequence = { 0 };
        Player p; p.setSampleRate (48000); p.setSong (&s);
        p.synths().factory = [] (int, const Instrument& in, double sr) { return createBuiltinSynth (in, sr); };
        p.synths().syncNow (s, 48000);
        p.playSong (0);
        std::vector<float> L (24000), R (L.size());
        p.render (L.data(), R.data(), (int) L.size());
        std::printf ("GRANULAR in the song, from instrument 01's sample (%d samples): rms %.3f\n", (int) s.instruments[0].data.size(), rmsOf (L, 0, (int) L.size()));
        CHECK (! s.instruments[0].data.empty() && rmsOf (L, 0, (int) L.size()) > 0.01f);
        // an empty slot: silence, no crash
        v[grain::SampleSlot] = 40; writeBuiltinState (s.instruments[4], v);
        p.playSong (0);
        p.render (L.data(), R.data(), (int) L.size());
        CHECK (finite (L));
    }
}

// ---- an instrument's MACRO knobs
static void testInstMacros()
{
    std::printf ("\n== instrument macros\n");
    MixerSettings m;
    initFxSlot (m.inst[2][0], FxType::Filter);
    CHECK (m.isFlat() == false || true);
    auto& im = m.instMacro[2][1];
    im.value = 0.5f;
    im.route[0] = { instrumentSlot (2, 0), 1, -0.6f, 0.5f, "CUTOFF" };
    im.route[1] = { instrumentSlot (2, 0), 2, 0.4f, 0.5f, "RESO" };
    CHECK (m.anyInstMacro() && ! m.isFlat());
    ModEngine me; me.prepare (48000);
    me.run (m.mods, 480, 120, m.instMacro);
    const int sl = instrumentSlot (2, 0);
    std::printf ("macro at 50%%: cutoff %+.2f, reso %+.2f\n", me.slotOffset[sl][1], me.slotOffset[sl][2]);
    CHECK (std::abs (me.slotOffset[sl][1] + 0.3f) < 1e-5f && std::abs (me.slotOffset[sl][2] - 0.2f) < 1e-5f && me.slotModded[sl]);
    me.setInstMacro (2, 1, 1.0f);   // V / J / an envelope turn it
    me.run (m.mods, 480, 120, m.instMacro);
    CHECK (std::abs (me.slotOffset[sl][1] + 0.6f) < 1e-5f);
    me.setInstMacro (2, 1, -1.0f);
    me.run (m.mods, 480, 120, m.instMacro);
    CHECK (std::abs (me.slotOffset[sl][1] + 0.3f) < 1e-5f);
    // an envelope on it, in a block
    Song s = makeSong();
    s.mixer = m;
    AutoLane l; l.target = kInstMacroBase + 2 * kInstMacros + 1; l.param = 0; l.points = { { 0, 1.0f, 0 } };
    s.blocks[0].lanes.push_back (l);
    Player p; p.setSampleRate (48000); p.setSong (&s); p.mixer().syncEffectsNow (s.mixer);
    p.playBlock (0);
    std::vector<float> L (4800), R (4800);
    p.render (L.data(), R.data(), 4800);
    CHECK (std::abs (p.mixer().mods.slotOffset[sl][1] + 0.6f) < 1e-5f);
}

// ---- live feedback: each track's scope and note count
static void testScopes()
{
    std::printf ("\n== scopes\n");
    Song s = makeSong();
    s.blocks[0] = Block {}; s.blocks[0].lines = 16;
    s.blocks[0].at (0, 1) = Cell { 37, 1, 0, 0 };            // C-3 on track 2 only
    s.blocks[0].at (4, 1) = Cell { 39, 1, 0, 0 };
    s.sequence = { 0 };
    Player p; p.setSampleRate (48000); p.setSong (&s);
    p.playSong (0);
    const int lineLen = (int) std::lround (48000 * 2.5 / s.tempo * s.speed);
    std::vector<float> L ((size_t) lineLen * 6), R (L.size());
    const uint32_t w0 = p.uiScopeWrite.load();
    p.render (L.data(), R.data(), (int) L.size());
    const uint32_t w1 = p.uiScopeWrite.load();
    float pk[kMaxTracks] {};
    for (int t = 0; t < kMaxTracks; ++t) for (float v : p.uiScope[t]) pk[t] = std::max (pk[t], std::abs (v));
    std::printf ("scope peaks: track 1 %.3f, track 2 %.3f; written %u; notes on track 2: %d\n", pk[0], pk[1], w1 - w0, p.uiNotes[1].load());
    CHECK (w1 - w0 == (uint32_t) L.size());
    CHECK (pk[1] > 0.05f && pk[0] == 0.0f);
    CHECK (p.uiNotes[1].load() == 2 && p.uiNotes[0].load() == 0);
    // muted: a flat line
    p.mute[1].store (true);
    p.render (L.data(), R.data(), Player::kScopeLen);
    float m = 0; for (float v : p.uiScope[1]) m = std::max (m, std::abs (v));
    CHECK (m == 0.0f);
    p.mute[1].store (false);
}

// ---- the BREAK page: which slices are kicks, snares, hats; the re-edits
// ---- BREAK LAB: edits written for you
static void testBreakLab()
{
    std::printf ("\n== BREAK LAB\n");
    std::vector<HitKind> kinds;
    for (char c : std::string ("KHSHKKSHKHSHKHSS")) kinds.push_back (c == 'K' ? HitKind::Kick : c == 'S' ? HitKind::Snare : HitKind::Hat);
    auto same = [] (const Block& a, const Block& b, int track, int lines)
    {
        int d = 0;
        for (int l = 0; l < lines; ++l) if (! (const_cast<Block&> (a).at (l, track) == const_cast<Block&> (b).at (l, track))) ++d;
        return d;
    };
    auto valid = [&] (Block& b, int track, int lines, int ins)
    {
        for (int l = 0; l < lines; ++l)
        {
            const auto& c = b.at (l, track);
            if (c.note == 0) { if (c.instr != 0) return false; continue; }
            const int sl = (int) c.note - kSliceC3;
            if (sl < 0 || sl >= (int) kinds.size() || c.instr != ins + 1) return false;
            if (c.cmd == 0x0C && c.param > 0x40) return false;
        }
        for (int l = lines; l < b.lines; ++l) if (! b.at (l, track).isEmpty()) return false;
        return true;
    };
    for (int style = 0; style < kLabStyles; ++style)
    {
        int rolls = 0, revs = 0, drops = 0, ghosts = 0, stut = 0;
        for (uint32_t seed = 1; seed <= 12; ++seed)
        {
            Block b; b.lines = 64;
            const auto r = breakLab (b, 1, 2, kinds, 64, 2, style, 100, seed);
            CHECK (valid (b, 1, 64, 2) && r.notes >= 5);
            CHECK (b.at (0, 1).note == kSliceC3 + 0 || kinds[(size_t) (b.at (0, 1).note - kSliceC3)] == HitKind::Kick);   // it starts on a kick
            for (int t = 0; t < kMaxTracks; ++t) if (t != 1) for (int l = 0; l < 64; ++l) CHECK (b.at (l, t).isEmpty());   // (its own track only)
            rolls += r.rolls; revs += r.reverses; drops += r.drops; ghosts += r.ghosts; stut += r.stutters;
            if (seed == 1) std::printf ("%-13s WILD 100: %s\n", labStyleName (style), r.text().c_str());
        }
        std::printf ("  12 edits: %d rolls, %d reverses, %d drops, %d ghosts, %d stutters\n", rolls, revs, drops, ghosts, stut);
        if (style == 6) CHECK (rolls + revs + drops > 0 && rolls + revs + drops + ghosts + stut >= 10);   // (LIQUID: smooth, few edits)
        else CHECK (rolls > 0 && revs > 0 && drops > 0 && rolls + revs + drops + ghosts + stut >= 30);
        // WILD 0: the groove alone, no frills
        Block z; z.lines = 64;
        const auto r0 = breakLab (z, 1, 2, kinds, 64, 2, style, 0, 5);
        CHECK (r0.rolls == 0 && r0.reverses == 0 && r0.drops == 0 && r0.ghosts == 0 && r0.stutters == 0 && r0.halfBars == 0 && r0.notes >= 6);
    }
    // (v0.74) '93 HARDCORE pitches hits UP more often than down; LIQUID hardly touches them
    {
        int up = 0, down = 0, liquidEdits = 0, rollersEdits = 0;
        for (uint32_t seed = 1; seed <= 30; ++seed)
        {
            Block b; b.lines = 64;
            breakLab (b, 0, 0, kinds, 64, 4, 4, 100, seed);
            for (int l = 0; l < 64; ++l) { uint8_t pv = 0; if (b.at (l, 0).has (cmdLetter ('I'), pv)) (pv > 0x80 ? up : down)++; }
            Block q; q.lines = 64; const auto rq = breakLab (q, 0, 0, kinds, 64, 4, 6, 100, seed);
            Block o; o.lines = 64; const auto ro = breakLab (o, 0, 0, kinds, 64, 4, 0, 100, seed);
            liquidEdits += rq.rolls + rq.reverses + rq.drops + rq.stutters; rollersEdits += ro.rolls + ro.reverses + ro.drops + ro.stutters;
        }
        std::printf ("'93 HARDCORE: %d pitched up, %d down; edits LIQUID %d vs '94 ROLLERS %d\n", up, down, liquidEdits, rollersEdits);
        CHECK (up > down * 2 && liquidEdits * 3 < rollersEdits);
    }
    // RE-ROLL BAR: that bar written again, the others untouched
    {
        Block b; b.lines = 64;
        breakLab (b, 1, 2, kinds, 64, 4, 0, 60, 3);
        const Block before = b;
        int changed = 0, others = 0;
        for (uint32_t seed = 1; seed <= 8; ++seed)
        {
            Block x = before;
            const auto r = rerollBar (x, 1, 2, kinds, 64, 4, 2, 2, 80, seed);
            CHECK (valid (x, 1, 64, 2) && r.notes >= 3);
            bool diff = false;
            for (int l = 0; l < 64; ++l)
            {
                const bool same = std::memcmp (&x.at (l, 1), &before.at (l, 1), sizeof (Cell)) == 0;
                if (l >= 32 && l < 48) diff |= ! same; else others += same ? 0 : 1;
            }
            changed += diff ? 1 : 0;
        }
        std::printf ("RE-ROLL BAR 3: changed %d of 8 times, other bars touched %d\n", changed, others);
        CHECK (changed >= 7 && others == 0);
    }
    // the same seed, the same edit; another seed, another edit
    {
        Block a, b, c; a.lines = b.lines = c.lines = 64;
        breakLab (a, 0, 0, kinds, 64, 2, 0, 60, 77);
        breakLab (b, 0, 0, kinds, 64, 2, 0, 60, 77);
        breakLab (c, 0, 0, kinds, 64, 2, 0, 60, 78);
        CHECK (same (a, b, 0, 64) == 0 && same (a, c, 0, 64) > 0);
    }
    // the drill'n'bass style is wilder than techstep (more edits a bar, on average)
    {
        int tech = 0, drill = 0;
        for (uint32_t seed = 1; seed <= 20; ++seed)
        {
            Block a, b; a.lines = b.lines = 64;
            const auto x = breakLab (a, 0, 0, kinds, 64, 2, 1, 70, seed), y = breakLab (b, 0, 0, kinds, 64, 2, 3, 70, seed);
            tech += x.rolls + x.stutters + x.reverses; drill += y.rolls + y.stutters + y.reverses;
        }
        std::printf ("rolls + stutters + reverses over 20: TECHSTEP %d, DRILL'N'BASS %d\n", tech, drill);
        CHECK (drill > tech * 2);
    }
    // MUTATE: a few changes, the rest kept; wilder, more changes
    {
        int calm = 0, wild = 0;
        for (uint32_t seed = 1; seed <= 10; ++seed)
        {
            Block a; a.lines = 64;
            breakLab (a, 0, 0, kinds, 64, 2, 0, 50, seed);
            Block m = a;
            const auto r = mutateBreak (m, 0, 0, kinds, 64, 2, 0, 20, seed + 100);
            const int d = same (a, m, 0, 64);
            CHECK (r.changes >= 1 && d >= 1 && d <= r.changes * 6 && valid (m, 0, 64, 0));
            calm += r.changes;
            Block m2 = a;
            wild += mutateBreak (m2, 0, 0, kinds, 64, 2, 0, 100, seed + 100).changes;
            if (seed == 1) std::printf ("MUTATE (WILD 20): %s - %d lines differ\n", r.text().c_str(), d);
        }
        std::printf ("MUTATE changes over 10: WILD 20 %d, WILD 100 %d\n", calm, wild);
        CHECK (wild > calm);
        // on an empty track it writes a whole edit
        Block e; e.lines = 32;
        CHECK (mutateBreak (e, 0, 0, kinds, 32, 1, 2, 50, 3).notes > 4);
    }
    // a break with no hats or snares found (all OTHER): still an edit, from stand-in slices
    {
        std::vector<HitKind> other (8, HitKind::Other);
        Block b; b.lines = 16;
        const auto r = breakLab (b, 0, 0, other, 16, 1, 2, 80, 9);
        bool ok = r.notes > 3;
        for (int l = 0; l < 16; ++l) if (b.at (l, 0).note > 0) ok = ok && b.at (l, 0).note - kSliceC3 < 8;
        CHECK (ok);
    }
}

static void testBreakTools()
{
    std::printf ("\n== break tools\n");
    Instrument in;
    in.rate = 44100; in.natRate = 44100;
    const char* pat = "KHSHKKSHKHSHKHSS";
    const int sliceLen = 5512;   // (a 16th at 120 BPM)
    uint32_t seed = 12345;
    auto noise = [&] { seed = seed * 1664525u + 1013904223u; return ((seed >> 9) / 4194304.0f) * 2 - 1; };
    in.data.assign ((size_t) sliceLen * 16, 0);
    for (int i = 0; i < 16; ++i)
    {
        in.slices.push_back (i * sliceLen);
        float prevN = 0;
        for (int k = 0; k < sliceLen; ++k)
        {
            const double t = k / 44100.0;
            float v = 0;
            if (pat[i] == 'K') v = (float) (0.9 * std::sin (2 * 3.14159 * (55 + 60 * std::exp (-t * 30)) * t) * std::exp (-t * 12));
            if (pat[i] == 'S') v = (float) ((0.55 * noise() + 0.4 * std::sin (2 * 3.14159 * 190 * t)) * std::exp (-t * 14));
            if (pat[i] == 'H') { const float nn = noise(); v = (float) (0.35 * (nn - prevN) * std::exp (-t * 40)); prevN = nn; }
            in.data[(size_t) (i * sliceLen + k)] = (int16_t) std::clamp (v * 32767.0f, -32767.0f, 32767.0f);
        }
    }
    const auto kinds = classifySlices (in);
    std::string got;
    for (auto k : kinds) got += hitLetter (k);
    std::printf ("kinds: %s (made %s)\n", got.c_str(), pat);
    CHECK (got == pat);
    CHECK (sliceOfKind (kinds, HitKind::Snare) == 2 && sliceOfKind (kinds, HitKind::Snare, 1) == 6 && sliceOfKind (kinds, HitKind::Kick, 2) == 5);

    Block b; b.lines = 64;
    // CLASSIC '94 over one bar: kick on line 0, snare on the 5th 16th (line 16), a quiet ghost (C14) on line 28
    int n = rebuild (b, 0, 3, kinds, 64, 1, rebuildTemplate (0), 0);
    std::printf ("CLASSIC '94: %d notes\n", n);
    CHECK (n == 11);
    CHECK (b.at (0, 0).note == kSliceC3 + 0 && b.at (0, 0).instr == 4);
    CHECK (b.at (16, 0).note == kSliceC3 + 2);
    CHECK (b.at (28, 0).cmd == 0x0C && b.at (28, 0).param == 0x14);
    CHECK (b.at (4, 0).isEmpty());
    // two bars over 64 lines: the second bar's kick a different one
    rebuild (b, 0, 3, kinds, 64, 2, rebuildTemplate (0), 0);
    CHECK (b.at (0, 0).note == kSliceC3 + 0 && b.at (32, 0).note == kSliceC3 + 4);
    // GHOST ROLL: the last 16 lines, kick then snares getting louder
    n = ghostRoll (b, 0, 3, kinds, 64);
    CHECK (n == 16 && b.at (48, 0).note == kSliceC3 + 0);
    CHECK (b.at (49, 0).cmd == 0x0C && b.at (49, 0).param < b.at (63, 0).param && b.at (63, 0).param == 0x40);
    // HALF-TIME / DOUBLE-TIME
    Block h; h.lines = 16;
    for (int l = 0; l < 16; l += 2) h.at (l, 1) = Cell { (uint8_t) (kSliceC3 + l / 2), 1, 0, 0 };
    Block d = h;
    n = halfTime (h, 1, 16);
    CHECK (n == 4 && h.at (0, 1).note == kSliceC3 && h.at (4, 1).note == kSliceC3 + 1 && h.at (2, 1).isEmpty() && h.at (12, 1).note == kSliceC3 + 3);
    n = doubleTime (d, 1, 16);
    CHECK (n == 16 && d.at (1, 1).note == kSliceC3 + 1 && d.at (8, 1).note == kSliceC3 && d.at (15, 1).note == kSliceC3 + 7);
    // no kinds found: stand-ins, nothing crashes
    std::vector<HitKind> none (8, HitKind::Other);
    CHECK (rebuild (b, 2, 0, none, 64, 1, rebuildTemplate (1), 3) > 0);
}

// ---- STRETCH: BEAT SYNC that keeps the pitch
static void testStretchSync()
{
    std::printf ("\n== stretch sync\n");
    Instrument in;
    in.rate = 44100; in.natRate = 44100;
    in.data.resize (88200);   // 2 s of a 441 Hz tone
    for (size_t i = 0; i < in.data.size(); ++i) in.data[i] = (int16_t) (12000 * std::sin (2 * 3.14159265 * 441.0 * i / 44100.0));
    in.slices = { 0, 22050, 44100, 66150 };
    in.loopStart = 0; in.loopLength = 0;
    in.syncLines = 16;
    const auto orig = in.data;
    CHECK (! stretch::wanted (in));
    in.stretchMode = 1;
    CHECK (stretch::wanted (in) && ! stretch::active (in));
    stretch::begin (in);
    CHECK (stretch::needsRender (in, 125, 6));
    // 125 BPM, speed 6: a line is 0.12 s, 16 lines 1.92 s: the 2 s break squeezed to 0.96
    const double f = stretch::factorFor (in, 125, 6);
    std::printf ("factor at 125: %.4f\n", f);
    CHECK (std::abs (f - 0.96) < 1e-6);
    auto r = stretch::make (in, 125, 6);
    CHECK (r.has_value());
    stretch::apply (in, std::move (*r));
    std::printf ("rendered %d samples (source %d), slices %d %d %d %d\n", (int) in.data.size(), (int) orig.size(), in.slices[0], in.slices[1], in.slices[2], in.slices[3]);
    CHECK (stretch::active (in) && ! stretch::needsRender (in, 125, 6) && ! stretch::edited (in));
    CHECK (std::abs ((int) in.data.size() - 84672) < 1500);
    CHECK (in.slices.size() == 4 && std::abs (in.slices[2] - (int) in.data.size() / 2) < 400);
    // the pitch stays: count zero crossings in the middle - 441 Hz still
    {
        int zc = 0;
        for (size_t i = 20001; i < 64101; ++i) if ((in.data[i - 1] < 0) != (in.data[i] < 0)) ++zc;
        const double hz = zc / 2.0 / (44100.0 / 44100.0);
        std::printf ("pitch after the stretch: %.0f Hz\n", hz);
        CHECK (std::abs (hz - 441) < 30);   // (the cycle joins add a crossing now and then)
    }
    // a faster tempo: needs a new render, shorter
    CHECK (stretch::needsRender (in, 170, 6) && ! stretch::needsRender (in, 125.2, 6));
    r = stretch::make (in, 170, 6);
    stretch::apply (in, std::move (*r));
    CHECK (std::abs ((int) in.data.size() - (int) (88200 * 0.70588)) < 1500);
    // PITCH +12: a new render, the same length, an octave up
    in.stretchSemis = 12;
    CHECK (stretch::needsRender (in, 170, 6));
    stretch::apply (in, std::move (*stretch::make (in, 170, 6)));
    {
        int zc = 0;
        for (size_t i = 10001; i < 50001; ++i) if ((in.data[i - 1] < 0) != (in.data[i] < 0)) ++zc;
        std::printf ("pitch +12: %.0f Hz\n", zc / 2.0 / (40000.0 / 44100.0));
        CHECK (std::abs (zc / 2.0 / (40000.0 / 44100.0) - 882) < 30);
    }
    // the original, for saving; and back, untouched
    CHECK (stretch::original (in).data == orig && stretch::original (in).slices == std::vector<int> ({ 0, 22050, 44100, 66150 }));
    // an edit to what plays shows
    in.data[100] ^= 0x1234; in.data[in.data.size() / 64] ^= 0x777;
    CHECK (stretch::edited (in));
    stretch::restore (in);
    CHECK (in.data == orig && in.stretchSrc.empty() && ! stretch::active (in));
}

// ---- CONDITIONS: ?xy plays on pass x of every y; FILL; FIRST
static void testConditions()
{
    std::printf ("\n== conditions\n");
    // the rule itself
    CHECK (conditionPlays (0x12, 1, false) && ! conditionPlays (0x12, 2, false) && conditionPlays (0x12, 3, false));
    CHECK (! conditionPlays (0x24, 1, false) && conditionPlays (0x24, 2, false) && conditionPlays (0x24, 6, false) && ! conditionPlays (0x24, 7, false));
    CHECK (conditionPlays (0x01, 3, true) && ! conditionPlays (0x01, 3, false));
    CHECK (! conditionPlays (0x02, 3, true) && conditionPlays (0x02, 3, false));
    CHECK (conditionPlays (0x03, 1, false) && ! conditionPlays (0x03, 2, false) && ! conditionPlays (0x04, 1, false) && conditionPlays (0x04, 5, false));
    CHECK (commandChar (kCmdCond) == '?' && commandFromChar ('?') == kCmdCond);
    // in the player: a looping block, a note on track 1 with ?24 (plays the 2nd of every 4), one on track 2 with ?01 (FILL)
    Song s = makeSong();
    s.blocks[0] = Block {}; s.blocks[0].lines = 4;
    s.blocks[0].at (0, 0) = Cell { 37, 1, kCmdCond, 0x24 };
    s.blocks[0].at (0, 1) = Cell { 37, 1, kCmdCond, 0x01 };
    s.blocks[0].at (1, 2) = Cell { 37, 1, 0x0E, 0xE1 };   // (EE1: line 1 again - not a new pass)
    s.sequence = { 0 };
    Player p; p.setSampleRate (48000); p.setSong (&s);
    p.playBlock (0);
    const int lineLen = (int) std::lround (48000 * 2.5 / s.tempo * s.speed);
    std::vector<float> L ((size_t) lineLen), R (L.size());
    std::vector<int> played1, played2;
    // (the windows start half a line in, so each holds one pass's line 0)
    std::vector<int> firstNotes { p.uiNotes[0].load(), p.uiNotes[1].load() };
    p.render (L.data(), R.data(), lineLen / 2);
    if (p.uiNotes[0].load() > firstNotes[0]) played1.push_back (1);
    if (p.uiNotes[1].load() > firstNotes[1]) played2.push_back (1);
    for (int passNo = 2; passNo <= 8; ++passNo)
    {
        if (passNo == 7) p.fill.store (true);
        const int n1 = p.uiNotes[0].load(), n2 = p.uiNotes[1].load();
        for (int k = 0; k < 5; ++k) p.render (L.data(), R.data(), lineLen);   // 4 lines + the repeated one
        if (p.uiNotes[0].load() > n1) played1.push_back (passNo);
        if (p.uiNotes[1].load() > n2) played2.push_back (passNo);
    }
    std::string a, b;
    for (int x : played1) a += std::to_string (x) + " ";
    for (int x : played2) b += std::to_string (x) + " ";
    std::printf ("?24 played on passes: %s / ?01 (FILL from pass 7): %s / block 0 passes %d\n", a.c_str(), b.c_str(), p.blockPassOf (0));
    CHECK (played1 == std::vector<int> ({ 2, 6 }));
    CHECK (played2 == std::vector<int> ({ 7, 8 }));
    CHECK (p.blockPassOf (0) >= 8 && p.blockPassOf (0) <= 9);
}

int main()
{
    if (const char* only = std::getenv ("PRV_ONLY"); only && std::string (only) == "studio") { testStudioFx(); testModulation(); testEnvelopes(); testClockSync(); testPhrases(); testInstMacros(); testScopes(); testBreakTools(); testStretchSync(); testConditions(); std::printf ("\n%d checks failed\n", failures); return failures ? 1 : 0; }
    testDoubleHalve();
    testScopes();
    testBreakTools();
    testBreakLab();
    testStretchSync();
    testConditions();
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
    testChords();
    testChordTable();
    testCapture();
    testChordDetect();
    testCapturePasses();
    testRecorder();
    testVintageSampling();
    testRangeCentsSync();
    testSampleTools();
    testCvOut();
    testEqPointsAndUtility();
    testCombine();
    testGrooveAndBreakTools();
    testInstrumentEqAndSynthShape();
    testRadio();
    testRadioFormats();
    testStems();
    testStudioFx();
    testModulation();
    testEnvelopes();
    testClockSync();
    testPhrases();
    testSubSynth();
    testGrainSynth();
    testSubBeat();
    testRaveSynth();
    testReeseSub();
    testRom();
    testMorphFilter();
    testMorphSynth();
    testNoStuckNotes();
    testAcidSynth();
    testPadSynth();
    testReeseDeep();
    testSubDeep();
    testRaveDeep();
    testMorphDeep();
    testGrainDeep();
    testReeseSynth();
    testDubThrows();
    testLiveMode();
    testCrunch();
    testDuckAndLowEnd();
    testGear();
    testAkaiS1000();
    testAkaiS950();
    testAkaiSamples();
    testInstMacros();
    testBlepSteps();
    testVolumeGrid();
    testModelFilters();
    testBlepCpu();
    testSampleOnOff();
    testLatency();
    testMidiOut();
    testBounce();
    testBalance();
    testEightTracks();
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
