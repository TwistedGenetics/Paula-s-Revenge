// Paula's Revenge - presets for the built-in synths (SUB, REESE, RAVE, GRANULAR): the factory ones, and yours
// (MY PRESETS), kept on this computer in the app's settings so they work in any song
#pragma once

#include <JuceHeader.h>
#include "../Prefs.h"
#include "../engine/BuiltinSynths.h"
#include "../engine/RaveSynth.h"
#include "../engine/MorphSynth.h"
#include "../engine/AcidSynth.h"
#include "../engine/PadSynth.h"
#include "../engine/ReeseSynth.h"

namespace prv::ui::synthpresets
{
struct Preset
{
    std::string name, key;         // key: the synth's pluginKey ("SYN:SUB")
    std::vector<float> values;     // its controls, as readBuiltinState gives them
};

inline juce::String encode (const std::vector<Preset>& all)
{
    juce::Array<juce::var> arr;
    for (const auto& pr : all)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("name", juce::String (pr.name));
        o->setProperty ("key", juce::String (pr.key));
        juce::Array<juce::var> vals;
        for (float v : pr.values) vals.add ((double) v);
        o->setProperty ("v", vals);
        arr.add (juce::var (o));
    }
    return juce::JSON::toString (juce::var (arr), true);
}

inline std::vector<Preset> decode (const juce::String& text)
{
    std::vector<Preset> out;
    const auto v = juce::JSON::parse (text);
    if (auto* arr = v.getArray())
        for (const auto& e : *arr)
        {
            Preset pr;
            pr.name = e.getProperty ("name", "").toString().toUpperCase().trim().substring (0, 24).toStdString();
            pr.key = e.getProperty ("key", "").toString().toStdString();
            if (pr.name.empty() || builtinParams (pr.key).empty()) continue;
            if (auto* vals = e.getProperty ("v", {}).getArray())
                for (const auto& x : *vals) pr.values.push_back ((float) (double) x);
            // (a preset from before a control was added: the new one at its default)
            const auto defs = builtinDefaults (pr.key);
            for (size_t i = pr.values.size(); i < defs.size(); ++i) pr.values.push_back (defs[i]);
            pr.values.resize (defs.size());
            out.push_back (std::move (pr));
        }
    return out;
}

inline std::vector<Preset> all() { return decode (prefs().getValue ("synthPresets")); }
inline void store (const std::vector<Preset>& v) { prefs().setValue ("synthPresets", encode (v)); prefs().saveIfNeeded(); }

// yours for one synth, in name order
inline std::vector<Preset> mine (const std::string& key)
{
    std::vector<Preset> out;
    for (auto& pr : all()) if (pr.key == key) out.push_back (pr);
    std::sort (out.begin(), out.end(), [] (const Preset& a, const Preset& b) { return a.name < b.name; });
    return out;
}

// the synth's own: RAVE's nine sounds; for the others, their settings as they start
inline std::vector<Preset> factory (const std::string& key)
{
    std::vector<Preset> out;
    if (key == kRaveKey)
        for (int t = 0; t < rave::Types; ++t) out.push_back ({ raveTypeName (t), key, ravePreset (t) });
    else if (key == kMorphKey)
        for (int t = 0; t < kMorphPresets; ++t) out.push_back ({ morphPresetName (t), key, morphPreset (t) });
    else if (key == kAcidKey)
        for (int t = 0; t < kAcidPresets; ++t) out.push_back ({ acidPresetName (t), key, acidPreset (t) });
    else if (key == kReeseKey)
        for (int t = 0; t < kReesePresets; ++t) out.push_back ({ reesePresetName (t), key, reesePreset (t) });
    else if (key == kPadKey)
        for (int t = 0; t < kPadPresets; ++t) out.push_back ({ padPresetName (t), key, padPreset (t) });
    else if (! builtinParams (key).empty())
        out.push_back ({ "DEFAULTS", key, builtinDefaults (key) });
    return out;
}

inline void save (const Preset& pr)
{
    if (pr.name.empty() || builtinParams (pr.key).empty()) return;
    auto v = all();
    v.erase (std::remove_if (v.begin(), v.end(), [&] (const Preset& x) { return x.key == pr.key && x.name == pr.name; }), v.end());
    v.push_back (pr);
    store (v);
}

inline void remove (const std::string& key, const std::string& name)
{
    auto v = all();
    v.erase (std::remove_if (v.begin(), v.end(), [&] (const Preset& x) { return x.key == key && x.name == name; }), v.end());
    store (v);
}
} // namespace prv::ui::synthpresets
