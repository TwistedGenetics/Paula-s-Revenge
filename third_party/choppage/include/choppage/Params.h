#pragma once
/*
    Params — every host-visible control, in one table shared by the desktop
    plugin (JUCE parameters are built from it) and the MPC plugin (SDK params).

    APPEND ONLY. MPC projects store parameters by index and desktop projects by
    id: never reorder, rename an id, or remove an entry. New ones go at the end.
*/
#include "choppage/Engine.h"
#include <string>
#include <vector>

namespace bc
{
    enum P : int
    {
        P_SRATE, P_CHARACTER, P_VOLUME,
        P_FILTER, P_FILTER_TYPE, P_FILTER_RES, P_DIST, P_DIST_TONE, P_CRUSH,
        P_REVERB, P_REV_DECAY, P_REV_DAMP, P_REV_WIDTH, P_REV_PRE,
        P_ECHO_MIX, P_ECHO_FB, P_ECHO_TIME, P_ECHO_MONO,
        P_PAT_AMOUNT, P_FX_AMOUNT, P_STYLE, P_SWING, P_LAYERS,
        P_SPEED, P_FIT, P_FIT_MODE, P_TRIGGER, P_BREAK_MODE, P_GATE_MODE, P_VEL_SLICE, P_TEMPO_SRC,
        P_PAD_QUANT, P_STUT_RATE, P_ROLL_RATE, P_TAPE_MS, P_AK_CYCLE, P_AK_RATIO, P_DIVE_RES, P_DROP_RATE, P_STRETCH_CYCLE,
        P_PAD1, P_PAD2, P_PAD3, P_PAD4, P_PAD5, P_PAD6, P_PAD7, P_PAD8,
        P_LCD_COLOUR,          // 0.2: LCD skin colour
        // 0.6: the Akaizer, set the way Twisted-Timestretch / Akaizer v1.2 sets it (P_AK_CYCLE and P_STRETCH_CYCLE,
        // cycle times in ms, are no longer used: these replace them)
        P_AK_CYC_LEN, P_AK_CYC_FREQ, P_AK_FREQ_MAN,          // the AKAIZE pad
        P_SL_CYC_LEN, P_SL_CYC_FREQ, P_SL_FREQ_MAN,          // per-slice STRETCH
        P_AKR_STRETCH, P_AKR_SYNC, P_AKR_TARGET,              // AKAIZE render: stretch % or SYNC (loop BPM -> host / TARGET)
        P_AKR_CYC_LEN, P_AKR_CYC_FREQ, P_AKR_FREQ_MAN, P_AKR_PITCH,
        kNumCoreParams
    };

    enum class PKind { Float, Bool, Choice };
    enum class PDisp { plain2, percent, ms0, ms1, khz, db, signed2, samples, cps, semis, pct2, bpm };

    struct ParamDef
    {
        const char* id;
        const char* name;
        PKind kind;
        float min, max, def;
        PDisp disp = PDisp::plain2;
        std::vector<std::string> choices {};
        float centre = 0.0f;   // > 0: skewed so this value sits mid-travel
    };

    const std::vector<ParamDef>& paramDefs();
    int paramIndex (const std::string& id);                     // -1 if unknown
    std::string formatParam (int index, float plain);           // value text, units included
    float paramToNorm (int index, float plain);
    float paramFromNorm (int index, float norm);

    // Helpers on choice lists
    int    noteRateDivisor (int idx);
    double echoBeats (int idx);
    double speedFactor (int idx);
    double akRatioValue (int idx);
    /** Akaizer CYCLE FREQ: the value set by hand when MANUAL is on, else linked to CYCLE LENGTH (41000 / length,
        as the original does). */
    int    akaizeFreq (float cycleLength, float freq, bool manual);
    const char* fxPadName (int i);

    /** Builds the engine's settings from plain values of all kNumCoreParams parameters. */
    Engine::Settings settingsFromParams (const float* plain);
}
