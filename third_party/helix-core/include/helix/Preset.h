// HELIX presets: a versioned JSON document (non-real-time).
//
// {
//   "format": "helix-preset", "version": 1,
//   "name": "...", "category": "Bass", "author": "...", "comment": "...",
//   "params": { "<param id>": <plain value>, ... },        // every parameter, by stable id
//   "tables": { "A": "<table id>", "B": "<table id>" },      // see TableLibrary for ids
//   "lfo": [ { "steps": [16 values], "points": [[x, y], ...] }, x4 ]
// }
// Unknown ids are ignored and missing ones take their default, so presets survive version changes.
#pragma once
#include "helix/Config.h"
#include "helix/Json.h"
#include "helix/ModSystem.h"
#include "helix/Params.h"
#include <string>
#include <vector>

namespace helix {

class Engine;
class TableLibrary;

// 2 (0.3.6): the FX rack lives in the effect slots. Version-1 presets are folded when read (fxlib::foldRack).
constexpr int kPresetVersion = 2;

struct Preset
{
    std::string name = "Init", category = "Init", author = "HELIX", comment;
    float params[kNumParams];
    std::string tables[2] = {"builtin:Basic Shapes", "builtin:Basic Shapes"};
    float lfoSteps[kNumLfos][kMaxSteps];
    int lfoPoints[kNumLfos];
    float lfoPx[kNumLfos][kLfoMaxPoints], lfoPy[kNumLfos][kLfoMaxPoints];
    float scenes[kNumScenes][kNumMacros];   // pad scenes (0.4.5): "scenes": [[8 macros] x 16]

    Preset();   // = Init (all defaults)
    Json toJson() const;
    std::string toString() const { return toJson().dump(2); }
    static bool fromJson(const Json& j, Preset& out, std::string* error = nullptr);
    static bool fromString(const std::string& text, Preset& out, std::string* error = nullptr);
};

// Engine (+ library slot ids) -> preset. library may be null (tables stay at the built-in default).
Preset capturePreset(const Engine& engine, const TableLibrary* library);
// Preset -> engine. Tables load asynchronously through the library (if given).
void applyPreset(const Preset& p, Engine& engine, TableLibrary* library);
// "Mutate": nudge every continuous parameter by up to +-amount (normalised), deterministic for a seed.
// Global / structural settings (master, voices, quality, tables, routing choices) are left alone.
void mutatePreset(Preset& p, float amount, uint32_t seed);

// The 24 factory presets (built in code; also written to Presets/<Category>/*.json).
const std::vector<Preset>& factoryPresets();

} // namespace helix
