// Paula's Revenge - mixer settings stored in the song (plain C++)
//
// Signal flow: instrument -> its 3 inserts -> the track it plays on: EQ -> 3 inserts -> pre-fader sends -> fader -> post-fader sends -> master
// Two return buses (SEND A / B) with 3 inserts each, and a master bus with 3 inserts,
// followed by the machine's output stage (Amiga / Akai) in the Player.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace prv
{
constexpr int kMixTracks = 8;      // = kMaxTracks
constexpr int kInserts   = 3;
constexpr int kSends     = 2;
constexpr int kReturns   = 2;
constexpr int kFxParams  = 12;     // (a slot's own record in the song keeps the first 6; v24 adds the rest)
constexpr int kFxSavedParams = 6;
// slot numbering across the whole mixer: tracks, then returns, then master
constexpr int kTrackSlots  = kMixTracks * kInserts;
constexpr int kReturnSlots = kReturns * kInserts;
constexpr int kMixerSlots  = kTrackSlots + kReturnSlots + kInserts;   // the mixer's own slots
// each instrument has its own inserts too, before the strip of the track it plays on
constexpr int kMixInstruments  = 63;   // = kMaxInstruments
constexpr int kInstrumentSlots = kMixInstruments * kInserts;
constexpr int kTotalSlots  = kMixerSlots + kInstrumentSlots;
inline int trackSlot (int track, int k)  { return track * kInserts + k; }
inline int returnSlot (int ret, int k)   { return kTrackSlots + ret * kInserts + k; }
inline int masterSlot (int k)            { return kTrackSlots + kReturnSlots + k; }
inline int instrumentSlot (int ins, int k) { return kMixerSlots + ins * kInserts + k; }
inline bool isInstrumentSlot (int slot)  { return slot >= kMixerSlots && slot < kTotalSlots; }

// (new types go at the end: the numbers are in the songs)
enum class FxType : uint8_t { None = 0, Filter, Drive, Compressor, Delay, Reverb, Chorus, Plugin,
                              Vast, Space, TapeEcho, Ensemble, Count };
inline bool isBuiltinFx (FxType t) { return t != FxType::None && t != FxType::Plugin && t < FxType::Count; }

// the EFFECT menu's groups
enum class FxCategory : uint8_t { Reverb = 0, Delay, Modulation, DriveCrush, FilterDynamics, Count };
std::string fxCategoryName (FxCategory);                 // "REVERB", "DRIVE & CRUSH"...
FxCategory fxCategory (FxType);
std::vector<FxType> fxTypesIn (FxCategory);              // the built-in effects in a group, menu order

// An insert slot: the effect and its parameters (all 0..1; each effect maps them).
// A plug-in slot (VST3 / AU) keeps which plug-in it is and the plug-in's own saved state.
struct FxSlot
{
    FxType type = FxType::None;
    bool   bypass = false;
    float  p[kFxParams] {};
    std::string pluginKey;              // identifies the plug-in on this computer (format + file + id)
    std::string pluginName;             // shown in the mixer, and when the plug-in is missing
    std::vector<uint8_t> pluginState;   // the plug-in's own settings
    bool active() const { return type != FxType::None && ! bypass; }
    bool operator== (const FxSlot& o) const
    {
        if (type != o.type || bypass != o.bypass) return false;
        for (int i = 0; i < kFxParams; ++i) if (p[i] != o.p[i]) return false;
        return pluginKey == o.pluginKey && pluginName == o.pluginName && pluginState == o.pluginState;
    }
};

// The channel EQ: up to 8 points, each a bell, a shelf or a pass filter, added where you double-click
// the graph. A new channel's EQ is empty (flat).
constexpr int kEqBands = 8;
constexpr int kEqOldBands = 5;   // songs before v20: HIGH-PASS, LOW SHELF, MID 1, MID 2, HIGH SHELF
enum class EqType : uint8_t { Bell = 0, LowShelf, HighShelf, HighPass, LowPass, Count };
constexpr float kEqMinHz = 20.0f, kEqMaxHz = 20000.0f, kEqMaxDb = 18.0f;

struct EqBand
{
    bool   used = false;            // a point on the graph
    bool   on = false;              // (a used point can be switched off and keep its settings)
    EqType type = EqType::Bell;
    float  freq = 1000.0f;          // Hz
    float  gain = 0.0f;             // dB (not used by the pass filters)
    float  q = 1.0f;                // bells only
    bool active() const { return used && on; }
    bool hasGain() const { return type != EqType::HighPass && type != EqType::LowPass; }
    bool hasQ() const { return type == EqType::Bell; }
    bool operator== (const EqBand& o) const { return used == o.used && on == o.on && type == o.type && freq == o.freq && gain == o.gain && q == o.q; }
};

std::string eqTypeName (EqType);          // "BELL", "LOW SHELF"...
EqType eqTypeForFreq (float hz);
// an EQ band from a song before v20 (fixed band k: HIGH-PASS, LOW SHELF, MID 1, MID 2, HIGH SHELF);
// in use only if it was on
EqBand eqBandFromOld (int k, bool on, float freq, float gain, float q);          // what a point added there is: a low shelf below 80 Hz, a high shelf above 10 kHz, else a bell

struct ChannelEq
{
    EqBand band[kEqBands];
    bool anyOn() const { for (const auto& b : band) if (b.active()) return true; return false; }
    int  points() const { int n = 0; for (const auto& b : band) if (b.used) ++n; return n; }
    // a new point there (switched on); -1 when all 8 are in use
    int  add (float hz, float db)
    {
        for (int i = 0; i < kEqBands; ++i)
            if (! band[i].used)
            {
                EqBand& b = band[i];
                b = {};
                b.used = b.on = true;
                b.type = eqTypeForFreq (hz);
                b.freq = hz < kEqMinHz ? kEqMinHz : (hz > kEqMaxHz ? kEqMaxHz : hz);
                b.gain = b.hasGain() ? (db < -kEqMaxDb ? -kEqMaxDb : (db > kEqMaxDb ? kEqMaxDb : db)) : 0.0f;
                b.q = 1.0f;
                return i;
            }
        return -1;
    }
    void remove (int i) { if (i >= 0 && i < kEqBands) band[i] = {}; }
};

struct ChannelStrip
{
    float     volumeDb = 0.0f;          // fader, -60 (off) .. +6 dB
    ChannelEq eq;
    FxSlot    inserts[kInserts];
    float     send[kSends] {};          // 0..1
    bool      sendPre[kSends] {};       // pre-fader sends
};

struct ReturnStrip
{
    float  volumeDb = 0.0f;
    int    pan = 128;                   // 0..255, 128 = centre (balance)
    bool   mute = false;
    FxSlot inserts[kInserts];
};

struct MasterStrip
{
    float  volumeDb = 0.0f;
    FxSlot inserts[kInserts];
};

// What an automation knob moves: a parameter of a mixer slot's effect, or of a
// plug-in instrument. `param` is the effect's parameter number (built-in effects:
// 0..5; plug-ins: the plug-in's own numbering); the name is kept for display.
constexpr int kAutoInstrumentBase = 1000;   // target = kAutoInstrumentBase + instrument index
struct AutoTarget
{
    int target = -1;          // mixer slot 0..kTotalSlots-1, or kAutoInstrumentBase + instrument
    int param = -1;
    std::string name;         // e.g. "VALHALLA ROOM: MIX"
    bool valid() const { return target >= 0 && param >= 0; }
    bool operator== (const AutoTarget& o) const { return target == o.target && param == o.param && name == o.name; }
};
// param = kAutoOnOff: the knob switches the effect / plug-in instrument itself on and off
// (00 = off, 01-FF = on), learned by clicking its BYPASS / ON button while LEARN is armed
constexpr int kAutoOnOff = 1 << 30;
inline bool autoValueOn (float v01) { return v01 > 0.5f / 255.0f; }
constexpr int kTrackKnobs = 2;        // V = A, J = B
constexpr int kInstrumentKnobs = 4;   // L, Q, H, W on a plug-in instrument's notes

struct MixerSettings
{
    ChannelStrip ch[kMixTracks];
    ReturnStrip  ret[kReturns];
    MasterStrip  master;
    AutoTarget   trackAuto[kMixTracks][kTrackKnobs];   // each track's AUTO A (V) and AUTO B (J)
    FxSlot       inst[kMixInstruments][kInserts];      // each instrument's own inserts
    bool instrumentChain (int ins) const               // anything in an instrument's inserts?
    {
        if (ins < 0 || ins >= kMixInstruments) return false;
        for (const auto& s : inst[ins]) if (s.type != FxType::None) return true;
        return false;
    }

    MixerSettings()
    {
        // ready-made returns: A = reverb, B = delay (silent until something is sent there)
        initReturns();
    }
    void initReturns();

    FxSlot&       slot (int index);
    const FxSlot& slot (int index) const;

    // true when the mixer changes nothing: the song plays exactly as without a mixer
    bool isFlat() const;
};

// ---- effect descriptions (names, parameter labels, defaults, value text)
struct FxParamInfo
{
    std::string name;     // short label, e.g. "CUTOFF"
    float def = 0.5f;     // default (0..1)
    bool  toggle = false; // shown as OFF / ON
};

std::string fxName (FxType);         // "REVERB"
std::string fxShortName (FxType);    // "VERB" (fits a mixer slot)
const std::vector<FxParamInfo>& fxParams (FxType);
// value text for parameter i of a slot's parameters, e.g. "1200 HZ", "1/8 D", "35%"
std::string fxParamText (FxType, int i, const float* params);
void initFxSlot (FxSlot&, FxType);   // the type with its default parameters

// the mappings the effects and their value text share
int    fxChoice (float v, int n);          // a 0..1 value as one of n choices
double vastModRateHz (float v);            // 0.02 .. 8 HZ
double spaceDecaySeconds (float v);        // 0.3 .. 40 S
double tapeToneHz (float v);               // 1.2 .. 16 KHZ
double ensembleRateScale (float v);        // X0.25 .. X4

// Delay time in seconds for parameters p (time, sync) at a song tempo (BPM)
double delaySeconds (const float* p, double bpm);
// the same for a time and a SYNC switch anywhere: free 1 ms .. 2 s, or a note length
double delaySeconds (float time, bool sync, double bpm);

// ---- presets: the factory's for each effect (the user's own are kept by the app)
struct FxPreset
{
    std::string name;
    FxType type = FxType::None;
    std::vector<float> p;            // all the effect's parameters, in order
};
const std::vector<FxPreset>& fxFactoryPresets (FxType);
void applyFxPreset (FxSlot&, const FxPreset&);   // the type and its values (bypass kept)
} // namespace prv
