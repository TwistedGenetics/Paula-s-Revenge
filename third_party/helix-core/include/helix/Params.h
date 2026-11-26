// The HELIX parameter table: one list shared by the engine, the desktop wrapper, the MPC target, presets
// and the render CLI. Parameters are identified by a stable string id (presets, desktop state) and by their
// index (the MPC saves the index in projects). The list in ParamList.inc is therefore APPEND-ONLY once
// released: never reorder, never remove, never rename an id.
#pragma once
#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

namespace helix {

enum class ParamKind : uint8_t { Linear, Log, Squared, Int, Bool, Choice };

struct ParamDef
{
    const char* id;
    const char* name;
    ParamKind kind;
    float min, max, def;
    const char* unit;
    const std::vector<std::string>* choices;   // Choice only
    bool modulatable;                          // has a mod-matrix destination / ring

    float toNormalised(float plain) const;
    float toPlain(float norm) const;
    float clampPlain(float plain) const;
    std::string format(float plain) const;     // value text without unit
    int numSteps() const;                      // 0 = continuous
};

enum ParamId : int
{
#define HP(e, id, name, kind, mn, mx, df, unit, ch, mod) e,
#include "helix/ParamList.inc"
#undef HP
    kNumParams
};

const ParamDef& paramDef(int index);
int paramIndexFromId(const std::string& id);   // -1 if unknown

// Lock-free parameter store. Any thread may write; the audio thread reads at block start.
class ParamStore
{
public:
    ParamStore() { resetToDefaults(); }
    void resetToDefaults() { for (int i = 0; i < kNumParams; ++i) v_[i].store(paramDef(i).def, std::memory_order_relaxed); bump(); }
    float get(int i) const { return v_[i].load(std::memory_order_relaxed); }
    int   getInt(int i) const { float f = get(i); return (int)(f < 0 ? f - 0.5f : f + 0.5f); }
    bool  getBool(int i) const { return get(i) >= 0.5f; }
    void  set(int i, float plain) { if (i < 0 || i >= kNumParams) return; v_[i].store(paramDef(i).clampPlain(plain), std::memory_order_relaxed); bump(); }
    void  setNormalised(int i, float n) { if (i >= 0 && i < kNumParams) set(i, paramDef(i).toPlain(n)); }
    float getNormalised(int i) const { return paramDef(i).toNormalised(get(i)); }
    uint32_t serial() const { return serial_.load(std::memory_order_acquire); }
private:
    void bump() { serial_.fetch_add(1, std::memory_order_release); }
    std::atomic<float> v_[kNumParams];
    std::atomic<uint32_t> serial_{0};
};

// Choice lists (shared so UIs can show them).
namespace choices {
extern const std::vector<std::string> filterType;
extern const std::vector<std::string> filterModel;
extern const std::vector<std::string> filterKind;
extern const std::vector<std::string> filterSlope;
extern const std::vector<std::string> moOscTarget;
extern const std::vector<std::string> moFilterTarget;
extern const std::vector<std::string> fxLibType;
extern const std::vector<std::string> warpMode;
extern const std::vector<std::string> subShape;
extern const std::vector<std::string> subOctave;
extern const std::vector<std::string> noiseType;
extern const std::vector<std::string> polyMode;
extern const std::vector<std::string> glideMode;
extern const std::vector<std::string> stealMode;
extern const std::vector<std::string> lfoShape;
extern const std::vector<std::string> lfoMode;
extern const std::vector<std::string> syncDiv;
extern const std::vector<std::string> modSource;
extern const std::vector<std::string> modDest;
extern const std::vector<std::string> modCurve;
extern const std::vector<std::string> perfStepShape;
extern const std::vector<std::string> printSource;
extern const std::vector<std::string> perfFeel, perfFill, lfoJump, macroPick;
extern const std::vector<std::string> charType, chordMemory, perfLoop;
// Chord memory c: the extra notes (semitones from the played note), up to 5. Returns how many.
int chordIntervals(int c, int* out);   // Print to wavetable: what sweeps (PrintTable.h)
extern const std::vector<std::string> fxType;
extern const std::vector<std::string> distType;
extern const std::vector<std::string> delayMode;
extern const std::vector<std::string> chorusMode;
extern const std::vector<std::string> reverbType;
extern const std::vector<std::string> quality;
extern const std::vector<std::string> onOff;
}

} // namespace helix
