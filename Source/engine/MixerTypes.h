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
constexpr int kFxParams  = 6;
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

enum class FxType : uint8_t { None = 0, Filter, Drive, Compressor, Delay, Reverb, Chorus, Plugin, Count };
constexpr int kBuiltinFxTypes = (int) FxType::Plugin;   // EMPTY + the six built-in effects

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

enum EqBandIndex { kEqHighPass = 0, kEqLowShelf, kEqMid1, kEqMid2, kEqHighShelf, kEqBands };

struct EqBand
{
    bool  on = false;
    float freq = 1000.0f;   // Hz
    float gain = 0.0f;      // dB (not used by the high-pass)
    float q = 0.7f;         // mids only
    bool operator== (const EqBand& o) const { return on == o.on && freq == o.freq && gain == o.gain && q == o.q; }
};

struct ChannelEq
{
    EqBand band[kEqBands];
    ChannelEq()
    {
        band[kEqHighPass]  = { false, 40.0f, 0.0f, 0.7f };
        band[kEqLowShelf]  = { false, 120.0f, 0.0f, 0.7f };
        band[kEqMid1]      = { false, 500.0f, 0.0f, 1.0f };
        band[kEqMid2]      = { false, 2500.0f, 0.0f, 1.0f };
        band[kEqHighShelf] = { false, 8000.0f, 0.0f, 0.7f };
    }
    bool anyOn() const { for (const auto& b : band) if (b.on) return true; return false; }
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

// Delay time in seconds for parameters p (time, sync) at a song tempo (BPM)
double delaySeconds (const float* p, double bpm);
} // namespace prv
