// Paula's Revenge - replayer and Paula-style output stage
#pragma once

#include "Song.h"
#include <atomic>

namespace prv
{

struct PaulaOptions
{
    enum class Model { A500, A1200, Clean };
    Model model        = Model::A500;   // analogue output filter model
    bool  zeroOrderHold = true;         // stepped Paula playback (vs. linear interpolation)
    bool  eightBit      = true;         // quantise sample data to 8 bits
    bool  crunch8ch     = true;         // requantise mixed track pairs in 8-track songs
    float stereoSep     = 0.2f;         // 0 = everything centred, 1 = hard Amiga L-R-R-L panning
    float master        = 1.0f;          // 0..2
};

// Simple filters used by the output stage
struct OnePole
{
    float a = 0, z = 0;
    void setLowpass (double fc, double fs) { a = (float) (1.0 - std::exp (-2.0 * kPi * fc / fs)); }
    float lp (float x) { z += a * (x - z); return z; }
    float hp (float x) { z += a * (x - z); return x - z; }
    void reset() { z = 0; }
};

struct Biquad
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    double z1 = 0, z2 = 0;
    void setLowpass (double fc, double q, double fs)
    {
        const double w = 2.0 * kPi * fc / fs, cw = std::cos (w), alpha = std::sin (w) / (2.0 * q);
        const double a0 = 1.0 + alpha;
        b0 = (1.0 - cw) * 0.5 / a0; b1 = (1.0 - cw) / a0; b2 = b0;
        a1 = -2.0 * cw / a0; a2 = (1.0 - alpha) / a0;
    }
    float process (float x)
    {
        const double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return (float) y;
    }
    void reset() { z1 = z2 = 0; }
};

class Player
{
public:
    static constexpr int kJamVoices = 4;
    static constexpr int kVoices    = kMaxTracks + kJamVoices;

    enum class Mode { Stopped, Song, Block };

    Player();

    void setSampleRate (double sr);
    void setSong (const Song* s) { song = s; }
    const Song* getSong() const { return song; }

    void playSong (int seqPos = 0, int line = 0);
    void playBlock (int block, int line = 0);
    void stop();
    Mode getMode() const { return mode; }
    bool isPlaying() const { return mode != Mode::Stopped; }

    // Live note preview (keyboard / MIDI). Uses its own voices.
    void jamNoteOn (int note, int instrIndex, int velocity127 = 127);
    void jamNoteOff (int note);
    void jamAllOff();

    // Render stereo audio. Caller must make sure the song is not modified meanwhile.
    void render (float* left, float* right, int numSamples);

    // External tempo control (plugin host sync)
    void setTempoOverride (double bpm) { tempoOverride = bpm; }
    void setLed (bool on) { ledOn = on; uiLed = on; }

    PaulaOptions options;
    std::atomic<bool> mute[kMaxTracks] {};

    // Read by the UI
    std::atomic<int>   uiSeqPos { 0 }, uiBlock { 0 }, uiLine { 0 }, uiTempo { 125 }, uiSpeed { 6 };
    std::atomic<bool>  uiLed { false };
    std::atomic<int>   songWraps { 0 };   // counts each time the song loops back (or stops on F00)
    std::atomic<float> uiLevel[kMaxTracks] {};
    std::atomic<float> uiOutL { 0.0f }, uiOutR { 0.0f };   // output peaks of the last block

private:
    struct Voice
    {
        int  instr = -1;            // instrument index
        bool active = false;
        double pos = 0;             // position in samples
        int  note = 0;
        int  finetune = 0;
        double period = 0;          // current base period
        double outPeriod = 0;       // period after arp/vibrato
        double targetPeriod = 0;    // tone portamento target
        int  volume = 64;
        int  outVolume = 64;
        int  pan = -1;              // -1 = Amiga default, 0..255 override
        // effect state
        uint8_t cmd = 0, param = 0;
        uint8_t portaSpeed = 0, vibSpeed = 0, vibDepth = 0, tremSpeed = 0, tremDepth = 0;
        uint8_t vibPos = 0, tremPos = 0, vibWave = 0, tremWave = 0, offsetMem = 0;
        int  loopRow = 0, loopCount = 0;
        int  delayTick = -1;        // note delay
        Cell delayedCell;
        float level = 0;
        int  jamNote = -1;
        // instrument shaping
        struct EnvState { int stage = 0; float level = 0, relStep = 0; };   // stage 0 idle,1 A,2 D,3 S,4 R
        EnvState ampEnv, fltEnv;
        struct Svf { float ic1L = 0, ic2L = 0, ic1R = 0, ic2R = 0, a1 = 1, a2 = 0, a3 = 0, k = 2; int count = 0; } svf;
    };

    void resetVoices();
    void doTick();
    void processRow();
    void processCell (int ch, const Cell& cell);
    void triggerNote (Voice& v, const Cell& cell, int ch);
    void tickEffects (int ch);
    void advanceLine();
    double samplesPerTick() const;
    void renderVoice (Voice& v, double inc, float& outL, float& outR);
    double vibratoDelta (uint8_t pos, uint8_t depth, uint8_t wave, int shift) const;
    void updateFilters();
    void startShaping (Voice& v);
    void releaseVoice (Voice& v);
    void shapeVoice (Voice& v, float& l, float& r);
    // returns true and sets `pos` (-1..1) when the voice has a fixed pan
    bool fixedPan (int ch, float& pos) const;

    const Song* song = nullptr;
    double sampleRate = 44100.0;
    Mode mode = Mode::Stopped;

    Voice voices[kVoices];
    int jamNext = 0;

    int seqPos = 0, block = 0, line = 0, tick = 0;
    int speed = 6, tempo = 125;
    double tickRemain = 0;
    double tempoOverride = 0;
    bool ledOn = false;

    int  breakLine = -1;   // pending Dxx
    int  jumpPos   = -1;   // pending Bxx
    int  loopJumpLine = -1; // pending E6x
    int  rowDelay = 0, rowDelayCount = 0;
    bool firstTickOfRow = true;

    // output filters
    OnePole lpL, lpR, hpL, hpR;
    Biquad  ledL, ledR;
    PaulaOptions::Model lastModel = PaulaOptions::Model::A500;
};

} // namespace prv
