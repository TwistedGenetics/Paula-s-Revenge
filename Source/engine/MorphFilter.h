// Paula's Revenge - the MORPH FILTER: in the spirit of E-mu's Z-plane filters (the Morpheus, the Proteus), our own
// models. A model is two filter shapes - each up to six sections: resonant peaks, notches, low- and high-passes -
// and MORPH moves between them, every section gliding from its first frequency, gain and Q to its second: a vowel
// "AH" into "EE", a phaser's notches sliding up, a comb's teeth spreading. FREQ moves the whole shape up or down
// (octaves) and EMPHASIS sharpens or softens it. Used by the MORPH FILTER effect and the MORPH synth (per voice).
// Plain C++, unit tested.
#pragma once

#include <array>
#include <string>

namespace prv
{
namespace morph
{
    enum Kind : int { Off = 0, Peak, Notch, LowPass, HighPass, BandPass };
    struct Section { Kind kind = Off; float hzA = 1000, dbA = 0, qA = 1, hzB = 1000, dbB = 0, qB = 1; };
    struct Model
    {
        const char* name;
        std::array<Section, 6> s;
        float trimDb;                   // (the model's level, so each is about as loud as the dry sound)
    };
    constexpr int kModels = 12;
    const Model& model (int i);
    const char* modelName (int i);
const char* modelChoices();   // "AH > EE|OH > AH|..." (a synth's MODEL control)
}

// one channel of it
class MorphFilter
{
public:
    void reset();
    // where it is: the model, MORPH 0..1, FREQ in octaves (-4..+4), EMPHASIS 0..1 (0.5 = as designed)
    void set (int model, float morph, float octaves, float emphasis, double sampleRate);
    float process (float x);
private:
    struct Sec { int kind = 0; float a1 = 0, a2 = 0, a3 = 0, k = 1, gain = 0; float ic1 = 0, ic2 = 0; };
    std::array<Sec, 6> sec {};
    float trim = 1;
};
} // namespace prv
