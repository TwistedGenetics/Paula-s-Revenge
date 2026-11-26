// The HELIX synth engine: voices, tables, parameters, modulation. Host-agnostic.
//
// Thread rules:
//  * process() runs on the audio thread: no allocation, no locks, no I/O.
//  * params() and lfoShapes() may be written from any thread.
//  * setTable() is called from a non-audio thread; the table is handed over through a lock-free queue and the
//    audio thread swaps the pointer at the next block. Old tables come back the same way and are released by
//    collectGarbage() (also non-audio).
#pragma once
#include "helix/Config.h"
#include "helix/Effects.h"
#include "helix/FxLibrary.h"
#include "helix/MasterOtt.h"
#include "helix/Midi.h"
#include "helix/ModSystem.h"
#include "helix/Params.h"
#include "helix/SpscQueue.h"
#include "helix/Voice.h"
#include "helix/Wavetable.h"
#include <atomic>
#include <memory>

namespace helix {

enum TableSlot : int { kSlotA = 0, kSlotB = 1, kNumTableSlots = 2 };

struct EngineContext
{
    float p[kNumParams];                         // snapshot of plain values for this block
    const Wavetable* table[kNumTableSlots] = {nullptr, nullptr};
    const LfoShapes* lfoShapes = nullptr;
    float sampleRate = 44100.0f;
    bool mipCrossfade = true;
    bool oversample = false;                     // 2x oscillators (Quality: High)
    int unisonCap = kMaxUnison;
    float pitchBend = 0.0f;                      // -1..1
    float pitchBendSemis = 0.0f;
    float modWheel = 0.0f, channelPressure = 0.0f;
    double bpm = 120.0;
    double songBeats = 0.0;                      // transport position (or the engine's own clock), quarter notes
    float lfoFreePhase[kNumLfos] = {};           // shared phase of free-running LFOs
};

class Engine
{
public:
    Engine();
    ~Engine();

    // Not real-time safe. Call before processing and whenever the sample rate changes.
    void prepare(double sampleRate, int maxBlockSize);
    void reset();

    // Real-time. Renders n frames into l / r (overwrites). Events must be sorted by offset.
    void process(float* l, float* r, int n, const MidiMsg* events, int numEvents, const Transport& transport);

    ParamStore& params() { return params_; }
    const ParamStore& params() const { return params_; }
    LfoShapes& lfoShapes() { return lfoShapes_; }
    const LfoShapes& lfoShapes() const { return lfoShapes_; }

    // Pad scenes (any thread): recall morphs the 8 macros to scene s over Scene Morph (the macro parameters take
    // the scene's values when the morph ends); store copies the macros' current values into scene s.
    const MasterOtt& masterOtt() const { return ott_; }
    MacroScenes& scenes() { return scenes_; }
    const MacroScenes& scenes() const { return scenes_; }
    void recallScene(int s) { if (s >= 0 && s < kNumScenes) sceneRequest_.store(s, std::memory_order_release); }
    void storeScene(int s);
    int lastScene() const { return lastScene_.load(std::memory_order_relaxed); }   // -1 before the first recall

    // Non-real-time: hand a table to the engine. Returns false if the hand-over queue is full (try again later).
    bool setTable(int slot, WavetablePtr table);
    WavetablePtr uiTable(int slot) const;
    void collectGarbage();

    // ---- Monitoring (any thread) ----
    // Live (modulated) value of a parameter, normalised 0..1, from the most recently started voice.
    float liveNormalised(int index) const { return live_[index].load(std::memory_order_relaxed); }
    int activeVoices() const { return activeVoices_.load(std::memory_order_relaxed); }
    // Engine CPU time as a fraction of real time on this core (smoothed). 0.35 = 35% of one core.
    float cpuLoad() const { return cpuLoad_.load(std::memory_order_relaxed); }
    // Per-voice share of the last second's render time (0..1 of real time).
    float voiceLoad(int v) const { return voiceLoad_[v].load(std::memory_order_relaxed); }
    // CPU guard: 0 = full quality; each level trades quality for CPU (see docs/PERF.md).
    int guardLevel() const { return guardLevel_.load(std::memory_order_relaxed); }
    // The CPU guard's target, fraction of one core (default 0.35, the HELIX budget).
    void setCpuBudget(float fraction) { cpuBudget_.store(fraction, std::memory_order_relaxed); }
    // Offline rendering (tests, helix-render, bounces): the CPU guard is off, so the output never depends on
    // how fast the machine is. Real-time hosts leave this on (the default).
    void setRealtime(bool rt) { realtime_.store(rt, std::memory_order_relaxed); if (!rt) guardLevel_.store(0); }

    // True while the heavy effect (OTT) is on, for the CPU meter.
    bool heavyFxActive() const { return heavyFx_.load(std::memory_order_relaxed); }

    double sampleRate() const { return sampleRate_; }

private:
    void handleMidi(const MidiMsg& m);
    void noteOn(int note, int vel);
    void noteOff(int note);
    void chordOn(int note, int vel);
    void chordOff(int note);
    uint8_t chordUsed_[128] = {};
    void allNotesOff(bool hard);
    void renderBlock(float* l, float* r, int n);
    void snapshotParams(const Transport& tr, int n);
    void swapTables();
    void publishLive();
    void runFx(float* l, float* r, int n);
    void updateGuard(double seconds, int n);
    NoteStart makeStart(int note, int vel, bool glide);
    Voice* findVoiceFor(int note);
    int monoVoice() const { return 0; }

    ParamStore params_;
    LfoShapes lfoShapes_;
    MacroScenes scenes_;
    std::atomic<int> sceneRequest_{-1}, lastScene_{-1};
    float morphFrom_[kNumMacros] = {}, morphTo_[kNumMacros] = {}, morphNow_[kNumMacros] = {};
    double morphPos_ = 0.0, morphLen_ = 0.0;
    bool morphing_ = false;
    void updateSceneMorph(int n);
    EngineContext ctx_;
    Voice voices_[kMaxVoices];
    FxChain fx_;
    fxlib::Slot lib_[fxlib::kNumSlots];
    // Sub guardian: Linkwitz-Riley 24 dB crossover (two Butterworth stages each way); the bands sum flat
    Biquad sgLp_[2], sgHp_[2];
    MasterOtt ott_;
    float sgKey_[1] = {-1.0f};
    float sgLow_[kCtrlBlock];   // Insert 1, Insert 2 (before fx_), FX 1, FX 2 (after)
    float fxp_[kNumParams];
    std::atomic<bool> heavyFx_{false};
    uint32_t ageCounter_ = 0;
    double sampleRate_ = 44100.0;
    int maxBlock_ = 512;
    float pitchBend_ = 0.0f, modWheel_ = 0.0f, chanPressure_ = 0.0f;
    bool sustain_ = false;
    float lastPitch_ = -1.0f;           // for portamento into a new voice
    int noteStack_[16];
    int noteStackSize_ = 0;
    double ownBeats_ = 0.0;
    float freePhase_[kNumLfos] = {};

    struct TableHandle { WavetablePtr ptr; int slot = 0; };
    SpscQueue<TableHandle, 16> pending_;
    SpscQueue<TableHandle, 64> retired_;
    WavetablePtr active_[kNumTableSlots];
    WavetablePtr uiTables_[kNumTableSlots];

    std::atomic<float> live_[kNumParams];
    std::atomic<int> activeVoices_{0};
    std::atomic<float> cpuLoad_{0.0f};
    std::atomic<float> voiceLoad_[kMaxVoices];
    std::atomic<int> guardLevel_{0};
    std::atomic<float> cpuBudget_{0.35f};
    std::atomic<bool> realtime_{true};
    double voiceTime_[kMaxVoices] = {};
    double voiceWindow_ = 0.0;
    double guardTimer_ = 0.0;
};

} // namespace helix
