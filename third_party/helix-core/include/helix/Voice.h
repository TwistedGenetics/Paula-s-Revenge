// One synth voice: two wavetable oscillators, sub, noise, two filters, four envelopes, four LFOs, two performers,
// and its own copy of the modulated parameters.
#pragma once
#include "helix/CharOsc.h"
#include "helix/Config.h"
#include "helix/Envelope.h"
#include "helix/Filter.h"
#include "helix/MegaFilter.h"
#include "helix/ModSystem.h"
#include "helix/Oscillator.h"
#include "helix/Params.h"

namespace helix {

struct EngineContext;

struct NoteStart
{
    int note = 60;
    float velocity = 1.0f;
    uint32_t age = 0;
    uint32_t seed = 1;
    float glideFrom = -1.0f;   // pitch (semitones) to glide from, < 0 = no glide
};

class Voice
{
public:
    static constexpr int kMaxModOffsets = kNumModSlots;
    struct ModOffset { int param; float norm; };

    void prepare(float sampleRate);
    void start(const NoteStart& s);
    // Steal: fade the current sound out over ~3 ms, then start the new note (click-free).
    void steal(const NoteStart& s);
    // Mono / legato: change pitch without restarting; retrigger = restart the envelopes too.
    void legatoTo(int note, float velocity, bool glide, bool retrigger);
    void release();
    void kill();
    void setPressure(float p) { pressure_ = p; }

    bool active() const { return amp_.active() || stealing_; }
    bool held() const { return held_; }
    bool releasing() const { return amp_.releasing(); }
    bool stealing() const { return stealing_; }
    bool sustained() const { return sustained_; }
    void setSustained(bool s) { sustained_ = s; }
    int note() const { return stealing_ ? pending_.note : note_; }
    uint32_t age() const { return stealing_ ? pending_.age : age_; }
    float level() const { return amp_.value(); }
    float pitch() const { return pitch_; }

    // Render n <= kCtrlBlock samples, adding into l / r.
    void render(const EngineContext& ctx, float* l, float* r, int n);

    // Modulation offsets applied in the last block (normalised), for the UI's mod rings and global FX params.
    int numModOffsets() const { return numOffsets_; }
    const ModOffset& modOffset(int i) const { return offsets_[i]; }
    // The voice's modulated plain parameter values from the last block.
    const float* modulatedParams() const { return vp_; }

private:
    void startNow(const NoteStart& s);
    void computeModulation(const EngineContext& ctx, int n);
    void renderOsc(const EngineContext& ctx, int slot, const float* other, int n);
    void renderFilter(int f, float* bl, float* br, float cutoffHz, int n);

    float sr_ = 44100.0f;
    int note_ = 60;
    float vel_ = 1.0f, pressure_ = 0.0f, random_ = 0.0f;
    float pitch_ = 60.0f;          // current (gliding) pitch in semitones
    float glideFrom_ = -1.0f;
    uint32_t age_ = 0;
    bool held_ = false, sustained_ = false, glide_ = false;
    double beatsSinceStart_ = 0.0;
    Rng rng_;
    Envelope amp_, env_[3];        // env_[0..2] = envelopes 2..4
    Lfo lfo_[kNumLfos];
    WtOscillator osc_[2];
    SubOscillator sub_;
    CharOsc co_;
    NoiseSource noise_;
    Filter filt_[2];
    mega::MegaFilter mfilt_[2];   // F1/F2 Model > 0: the Mega Synth models

    // Stealing
    bool stealing_ = false;
    int stealLeft_ = 0, stealLen_ = 1;
    NoteStart pending_;

    // Modulation results
    float vp_[kNumParams];
    float pitchMod_ = 0.0f, pitchModA_ = 0.0f, pitchModB_ = 0.0f, ampMod_ = 0.0f, panMod_ = 0.0f;
    float lfoRateOffset_[kNumLfos] = {};
    ModOffset offsets_[kMaxModOffsets];
    int numOffsets_ = 0;
    float lastAmpGain_ = 0.0f;

    float lastPos_[2] = {0, 0}, lastLevel_[2] = {0, 0}, lastWarp_[2][2] = {{0, 0}, {0, 0}};
    bool fresh_ = true;

    float oscL_[2][kCtrlBlock], oscR_[2][kCtrlBlock], oscM_[2][kCtrlBlock];
    float prevM_[2][kCtrlBlock];
    float sub_buf_[kCtrlBlock], noise_buf_[kCtrlBlock], co_buf_[kCtrlBlock];
    float mo_buf_[kCtrlBlock];
    float driftPhase_ = 0.0f, driftFrom_ = 0.0f, driftTo_ = 0.0f;   // Reese drift     // modulation oscillator (sine, -1..1)
    float moPhase_ = 0.0f;
    bool moOn_ = false;
    float busL_[2][kCtrlBlock], busR_[2][kCtrlBlock], directL_[kCtrlBlock], directR_[kCtrlBlock];
};

} // namespace helix
