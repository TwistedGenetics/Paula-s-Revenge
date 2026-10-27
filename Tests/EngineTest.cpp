// Engine tests: render, pitch check, MOD round-trip, loader fuzzing.
// Build: c++ -std=c++20 -O1 -g -fsanitize=address,undefined -I Source Tests/EngineTest.cpp Source/engine/*.cpp
#include "engine/Player.h"
#include "engine/ModuleIO.h"
#include <cstdio>
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
            if (! (expect == b)) { same = false; std::printf ("  diff line %d track %d\n", l, tr); }
        }
    CHECK (same);
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
    CHECK (t.instruments[0].data.size() == 16 && t.instruments[0].volume == 40);
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

int main()
{
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
