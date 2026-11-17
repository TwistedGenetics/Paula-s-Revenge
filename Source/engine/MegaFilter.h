// Paula's Revenge - the Mega Synth filters on an instrument (PARAMS > FILTER > MODEL)
// MODEL 0 is Paula's own state-variable filter (LOW / HIGH / BAND, exactly as before). The rest are Mega Synth's
// (Source/engine/mega, the same code): 17 classic analogue models - each at its own slope and Low Pass is the
// original circuit; with another type or slope it lends a multimode filter its character - and 18 character
// models (formants, combs, phasers, resamplers...) with a CHARACTER knob. DRIVE (0-24 dB) and MIX go with them.
#pragma once
#include "Song.h"
#include <memory>
#include <vector>

namespace tg { class FilterUnit; }

namespace prv
{
constexpr int kFilterClassicModels = 17, kFilterCharModels = 18;
constexpr int kFilterModels = 1 + kFilterClassicModels + kFilterCharModels;   // (0 = Paula's own)
const char* filterModelName (int model);          // "PAULA SVF", "LADDER", "FORMANT"...
const char* filterModelAbout (int model);         // a line about it
const char* filterCharName (int model);           // what CHARACTER does on it ("" = not used)
bool filterIsCharModel (int model);
int  filterNativeSlope (int model);                // a classic model's own slope (0-3); 1 otherwise
bool filterTypeAllowsSlope (FilterType, int slope);
// the menu's groups: name, and the models in it
struct FilterGroup { const char* name; std::vector<int> models; };
const std::vector<FilterGroup>& filterGroups();

// does this instrument's filter (with a pattern command's type, if any) run on the Mega Synth code?
inline bool usesMegaFilter (const Instrument& in, FilterType type) { return type != FilterType::Off && (in.filterModel > 0 || type >= FilterType::Notch); }

// one filter (stereo) for a voice: follows the instrument's settings, set again as they change
class MegaFilter
{
public:
    MegaFilter();
    ~MegaFilter();
    void reset();
    // its settings (type: the instrument's, or a pattern command's); a change crossfades (no click)
    void setup (const Instrument& in, FilterType type, double sampleRate);
    void setCutoff (float hz, float res);
    void process (float& l, float& r);
private:
    std::unique_ptr<tg::FilterUnit> unit;
    int key = -1;
    double sr = 48000.0;
};

// the filter's gain at each of these frequencies (the PARAMS page's curve; tests)
std::vector<float> megaFilterResponse (const Instrument& in, double sampleRate, const std::vector<double>& hz);
} // namespace prv
