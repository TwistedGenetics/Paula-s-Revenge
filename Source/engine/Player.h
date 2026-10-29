// Paula's Revenge - replayer and Paula-style output stage
#pragma once

#include "Song.h"
#include "Mixer.h"
#include "PluginSynth.h"
#include "CvOut.h"
#include "DelayLine.h"
#include "Blep.h"
#include <atomic>

namespace prv
{
static_assert (kMixTracks == kMaxTracks, "one mixer strip per track");

// Akai output filter corners (Hz); see README for where these come from
constexpr double kS950Cutoff  = 16000.0;
constexpr double kS1000Cutoff = 19500.0;

struct PaulaOptions
{
    // A500/A1200/A4000/Clean: Amiga Paula. S950/S1000: Akai samplers (see README)
    // (new models go on the end: the number is saved)
    enum class Model { A500, A1200, Clean, S950, S1000, A4000 };
    Model model        = Model::A500;   // analogue output filter model
    bool isAkai() const { return model == Model::S950 || model == Model::S1000; }
    bool isAmiga() const { return model == Model::A500 || model == Model::A1200 || model == Model::A4000; }
    // Paula's staircase as band-limited steps (every Paula model with STEPPED on)
    bool usesBlep() const  { return ! isAkai() && zeroOrderHold; }
    // below volume 64 a real Paula moves sample changes onto its 55.4 kHz grid
    bool snapsVolume() const { return isAmiga() && zeroOrderHold; }
    // what the model actually does with the Amiga options
    bool stepped() const   { return model == Model::S950 ? true : model == Model::S1000 ? false : zeroOrderHold; }
    int  dropBits() const  { return model == Model::S950 ? 4 : model == Model::S1000 ? 0 : (eightBit ? 8 : 0); }   // 16 - bit depth
    bool crunches() const  { return isAmiga() && crunch8ch && eightBit; }   // 8CH GRIT in effect
    bool  zeroOrderHold = true;         // stepped Paula playback (vs. linear interpolation)
    bool  eightBit      = true;         // quantise sample data to 8 bits
    bool  crunch8ch     = true;         // 8CH GRIT: tracks 1+5, 2+6... mixed the OctaMED 8-channel way (at full level)
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

// first-order section from an analogue prototype H(s) = (1 + s/wz) / (1 + s/wp), bilinear
// (wz = 0: a plain RC low-pass). Used for the measured Amiga output responses.
struct FirstOrder
{
    double b0 = 1, b1 = 0, a1 = 0, z1 = 0;
    void set (double poleHz, double zeroHz, double fs)
    {
        const double k = 2.0 * fs;
        const double wp = 2.0 * kPi * poleHz;
        const double cz = zeroHz > 0 ? 1.0 / (2.0 * kPi * zeroHz) : 0.0, cp = 1.0 / wp;
        // (1 + cz s) / (1 + cp s), s = k (1 - z^-1) / (1 + z^-1)
        const double a0 = 1.0 + cp * k;
        b0 = (1.0 + cz * k) / a0; b1 = (1.0 - cz * k) / a0; a1 = (1.0 - cp * k) / a0;
    }
    float process (float x) { const double y = b0 * x + z1; z1 = b1 * x - a1 * y; return (float) y; }
    void reset() { z1 = 0; }
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
    // While playing a block: move to block `b` at the same line and tick (no restart). False if not in block play.
    bool switchBlock (int b);
    int  playingBlock() const { return block; }
    void stop();
    Mode getMode() const { return mode; }

    // Loop lines from..to (inclusive) of one block while playing; block < 0 = off.
    // Bxx/Dxx inside the loop are ignored so it keeps going round.
    void setLineLoop (int blockIndex, int from, int to);
    int  loopBlock() const { return lpBlock; }
    // Loop song positions from..to while playing the song; from < 0 = off. When the
    // play position is outside the range, it goes there at the end of the current block.
    void setSongLoop (int from, int to) { songLoopFrom = from; songLoopTo = std::max (from, to); }
    int  songLoopStart() const { return songLoopFrom; }
    int  songLoopEnd() const { return songLoopTo; }
    int  loopFrom() const { return lpFrom; }
    int  loopTo() const { return lpTo; }
    bool isPlaying() const { return mode != Mode::Stopped; }
    // where playback is right now, to the fraction of a line (call under the lock / on the audio thread):
    // the row that's sounding (it can be before the line the UI shows, which moves on a tick early)
    struct PlayPos { bool playing = false; int seqPos = 0, block = 0; double line = 0; int speed = 6;
                     int pass = 0; };   // pass: +1 each time playback goes back (the song's end, a looping block, loop lines)
    PlayPos playPos() const;

    // Live note preview (keyboard / MIDI). Uses its own voices. start/end
    // (in samples) limit playback to a region; loopRegion loops it.
    void jamNoteOn (int note, int instrIndex, int velocity127 = 127, int start = -1, int end = -1, bool loopRegion = false);
    // Play the instrument from sample `pos` with its own loop (ping-pong too): loop auditioning.
    void jamNoteFrom (int note, int instrIndex, int pos);
    void jamNoteOff (int note);
    void jamAllOff();

    // Render stereo audio. Caller must make sure the song is not modified meanwhile.
    void render (float* left, float* right, int numSamples);

    // External tempo control (plugin host sync)
    void setTempoOverride (double bpm) { tempoOverride = bpm; }
    void setLed (bool on) { ledOn = on; uiLed = on; }

    PaulaOptions options;
    std::atomic<bool> mute[kMaxTracks] {};

    // the mixer (EQ, inserts, sends) used when the song's mixer isn't flat
    MixerEngine& mixer() { return mixerEngine; }
    // the EQ's spectrum display at instrument level: that instrument's sound (after its UTILITY and
    // its own inserts), on its own (source = instrument index, -1 = none wanted)
    AudioTap insTap;
    // the song's plug-in instruments
    SynthRack& synths() { return rack; }
    // which track's mixer strip a plug-in instrument plays into right now
    int synthOutputTrack (int instrument) const;
    double rateFactor (const Instrument&) const;   // how fast a sample plays at C-3 (its rate, cents, BEAT SYNC)
    // an instrument switched on/off: by its ON button, or by an automation knob while playing
    bool instrumentIsOn (int instrument) const;
    void setInstrumentOn (int instrument, int state);   // (audio lock) 0 = off, 1 = on, -1 = back to its ON button
    void resetSwitches();   // automated on/off of effects and instruments back to their buttons
    int  instrumentOnOverride (int instrument) const { return instrument >= 0 && instrument < kMaxInstruments ? instOnOv[instrument] : -1; }
    bool forceMixPath = false;    // tests (and stem renders): use the mixer path even when flat
    CvBus cv;                     // CV out: the app points this at the interface's outputs for each render (all null: no CV)

    // the value an automation knob was last set to by the pattern (-1 = not yet); kind 0 = track knob, 1 = instrument knob
    float laneValue (int kind, int a, int b) const
    {
        if (kind == 0) return a >= 0 && a < kMaxTracks && b >= 0 && b < kTrackKnobs ? trackLanes[a][b].cur : -1.0f;
        return a >= 0 && a < kMaxInstruments && b >= 0 && b < kInstrumentKnobs ? instLanes[a][b].cur : -1.0f;
    }

    // stem renders: called after each block with every track (0..7, after its fader), the two
    // returns (8, 9) and the master (10)
    struct StemTap
    {
        virtual ~StemTap() = default;
        virtual void add (int stem, const float* left, const float* right, int numSamples) = 0;
    };
    StemTap* stemTap = nullptr;
    bool stemsBeforeMixer = false;   // bounces: the tracks before their mixer strips (no master)
    bool skipInstrumentFx = false;   // bounces: the instruments' own inserts left out
    // bounces: the range has ended - plug-in notes and looped samples are let go (one-shots ring on)
    void releaseForTail();
    static constexpr int kStems = kMaxTracks + kReturns + 1;

    // samples the whole output is late by (plug-in delays, MIDI-out lead)
    int latencySamples() const { return mixerEngine.totalLatency(); }

    // Read by the UI
    std::atomic<int>   uiSeqPos { 0 }, uiBlock { 0 }, uiLine { 0 }, uiTempo { 125 }, uiSpeed { 6 };
    std::atomic<bool>  uiLed { false };
    std::atomic<int>   songWraps { 0 };   // counts each time the song loops back (or stops on F00)
    std::atomic<float> uiLevel[kMaxTracks] {};
    std::atomic<float> uiOutL { 0.0f }, uiOutR { 0.0f };   // output peaks of the last block
    std::atomic<int>   uiJamPos[kJamVoices] {};             // jam voice positions (-1 = silent), for playheads
    std::atomic<int>   uiJamInstr[kJamVoices] {};
    std::atomic<int>   uiTrackInstr[kMaxTracks] {};         // the instrument each track last played (-1 none)

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
        uint8_t cmd = 0, param = 0, cmd2 = 0, param2 = 0;
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
        // playback region and direction
        double regionStart = -1, regionEnd = -1;   // slice / selection limits (samples), -1 = none
        bool   regionLoop = false;
        bool   reverse = false;
        bool   pingBack = false;                   // going backwards in a ping-pong loop
        double pitchMul = 1.0;                     // micro-tune
        // extended commands
        double glideStep = 0; int glideTicks = 0;
        int    gateTicks = -1;                     // Nxx countdown
        int    rollEvery = 0, rollVolStep = 0;     // Rxy
        int    fltType = -1; float fltCutoff = 20000.0f, fltRes = -1.0f;   // H/L/W/Q overrides
        float  drive = 0.0f; int crushBits = 0;    // Oxx / Zxx
        Cell   microCell; long long microDelay = -1; bool earlyDone = false;  // Mxx
        // a plug-in instrument's note on this track
        int    synthNote = -1, synthInstr = -1, synthVel = 100, lastBend = 8192;
        int    synthChord[kMaxChordNotes] { -1, -1, -1, -1, -1, -1, -1, -1 };   // CHORD: the notes of slot xy
        bool   synthRootOn = true;                 // (a chord without +0 doesn't sound the played note)
        bool   chordLine = false;                  // 0xy on this line made a chord (no arpeggio)
        // switched off while sounding: fades out
        int    fadeOut = 0, fadeLen = 1;
    };

    // automation: a value moving (or gliding) towards its target, one per knob
    struct Lane { float cur = -1.0f, target = 0.0f, step = 0.0f; int ticks = 0; };
    Lane trackLanes[kMaxTracks][kTrackKnobs];
    Lane instLanes[kMaxInstruments][kInstrumentKnobs];
    int  pendingGlide[kMaxTracks] {};          // Yxx: ticks for the next knob change on the track
    int  rowGlide = 0; bool knobSetThisRow = false;
    void setLane (Lane&, float value, int glide, int kind, int a, int b);
    void applyLane (int kind, int a, int b, float v);   // kind 0 = track knob (a = track, b = knob), 1 = instrument knob
    int  instOnOv[kMaxInstruments];   // automated on/off of a plug-in instrument: -1 = its ON button
    void advanceAutomation();

    // plug-in instruments
    SynthRack rack;
    int  chunkPos = 0;                          // sample offset of events inside the current chunk
    CvSourceRunner cvRunner;                    // the song's CV SOURCES
    bool cvTrig[kMaxTracks] {}, cvRel[kMaxTracks] {}, cvWasPlaying = false;
    float cvKnobA[kMaxTracks] {}, cvKnobB[kMaxTracks] {};
    int  synthTrack[kMaxInstruments] {};        // the track that played the instrument's latest note (AUTO output; its inserts' output)
    struct JamSynth { int note = -1, instr = -1, midi = -1; };
    JamSynth jamSynth[16];
    float synthL[MixerEngine::kMaxBlock] {}, synthR[MixerEngine::kMaxBlock] {};
    const Cell* chordSource = nullptr;          // (a delayed note: the cell its 0xy came from)
    // latency: samples on a track wait for its plug-in instrument
    DelayLine sampleAlign[kMaxTracks];
    int  sampleDelay[kMaxTracks] {};
    // MIDI clock
    double ppq = 0;                              // song position in beats
    long long nextClock = 0;
    bool clockStartPending = false;
    bool clockOwner[kMaxInstruments] {};
    void updateSynths (int numSamples);
    void sendClock (int kind, int offset);
    double positionInBeats (int seqPos, int line) const;
    bool isSynthVoice (const Voice& v) const;
    void synthTrigger (Voice& v, const Cell& cell, int ch);
    void synthRelease (Voice& v);
    void synthRetrigger (Voice& v, int velocityChange);
    void synthBend (Voice& v);
    void synthAllOff();

    void resetVoices();
    void doTick();
    void processRow();
    void processCell (int ch, const Cell& cell);
    void triggerNote (Voice& v, const Cell& cell, int ch);
    void applyRowCommand (Voice& v, uint8_t cmd, uint8_t param, const Cell& cell);
    void tickCommand (Voice& v, uint8_t cmd, uint8_t param, double& vib, int& trem, int& arp);
    void tickEffects (int ch);
    void scheduleEarlyNotes();
    void setInstrument (Voice& v, int instrNumber);
    double lineSamples() const;
    uint32_t rng = 0x9E3779B9u;
    uint32_t nextRandom() { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }
    void advanceLine();
    void renderChunk (float* left, float* right, int numSamples);
    void outputStage (float& L, float& R, bool led);
    MixerEngine mixerEngine;
    float trkL[kMaxTracks][MixerEngine::kMaxBlock] {}, trkR[kMaxTracks][MixerEngine::kMaxBlock] {};
    float jamL[MixerEngine::kMaxBlock] {}, jamR[MixerEngine::kMaxBlock] {};
    // instruments with their own inserts: their audio is gathered here, through the inserts, then to a track
    float instL[kMaxInstruments][MixerEngine::kMaxBlock] {}, instR[kMaxInstruments][MixerEngine::kMaxBlock] {};
    bool  instChain[kMaxInstruments] {};
    StereoUtility util[kMaxInstruments];   // each instrument's UTILITY (width, mono below)
    ChannelEqProcessor instEq[kMaxInstruments];   // each instrument's own EQ
    // a plug-in instrument's filter and envelopes on its output: per sample, its gate (bit 0) and a new
    // note (bit 1) as the song played it, then shaped after the plug-in renders
    struct SynthShape { Voice::EnvState amp, flt; Voice::Svf svf; int note = 37; uint32_t seen = 0; };
    SynthShape synthShape[kMaxInstruments];
    uint32_t synthOnSerial[kMaxInstruments] {};
    uint8_t shapeEvt[kMaxInstruments][MixerEngine::kMaxBlock] {};
    void shapeSynth (int ins, float* L, float* R, int n);
    float tapL[MixerEngine::kMaxBlock] {}, tapR[MixerEngine::kMaxBlock] {};
    bool  ledBuf[MixerEngine::kMaxBlock] {};
    double samplesPerTick() const;
    void renderVoice (Voice& v, double inc, float& outL, float& outR, int ch);
    // Paula's staircase as band-limited steps, one channel per voice
    BlepTable blepTable;
    BlepChannel blepCh[kVoices];
    double blepClock = 0;            // output samples since the start (the steps' time line)
    bool blepWasOn = false;
public:
    static constexpr double kPaulaDacRate = kPaulaClock / 64.0;   // 55,420 Hz: the grid below volume 64
    const BlepTable& blep() const { return blepTable; }
private:
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
    int rowSeq = 0, rowBlock = 0, rowLine = 0, rowTicks = 0;   // the row being heard, ticks done in it
    int pass = 0; long lastRowOrder = -1;                       // (PlayPos::pass)
    int speed = 6, tempo = 125;
    double tickRemain = 0;
    double tempoOverride = 0;
    bool ledOn = false;

    int  lpBlock = -1, lpFrom = 0, lpTo = 0;   // line loop
    int  songLoopFrom = -1, songLoopTo = -1;   // section loop
    int  nextSongPos (int prev, int wanted);   // applies the section loop
    bool inLineLoop() const { return lpBlock >= 0 && block == lpBlock && line >= lpFrom && line <= lpTo; }
    int  breakLine = -1;   // pending Dxx
    int  jumpPos   = -1;   // pending Bxx
    int  loopJumpLine = -1; // pending E6x
    int  rowDelay = 0, rowDelayCount = 0;
    bool firstTickOfRow = true;

    // output filters
    FirstOrder lpL, lpR;     // the model's fixed output filter (measured response where there is one)
    OnePole hpL, hpR;
    float limGain = 1.0f, limRelease = 0.0002f;   // the safety limiter
    Biquad  ledL, ledR;
    Biquad  akL[4], akR[4];   // Akai output filter (cascaded 2-pole sections)
    int     akStages = 0;
    PaulaOptions::Model lastModel = PaulaOptions::Model::A500;
};

} // namespace prv
