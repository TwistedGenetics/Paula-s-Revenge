#pragma once
/* Parameter ids as names, for the desktop UI (the table itself is choppage-core's Params). */
#include "choppage/Params.h"

namespace bc::params
{
    // Sound
    inline constexpr const char* sampleRate = "srate";
    inline constexpr const char* character  = "character";
    inline constexpr const char* volume     = "volume";
    // Post FX knobs + FX tab detail
    inline constexpr const char* filter     = "filter";
    inline constexpr const char* filterType = "filterType";
    inline constexpr const char* filterRes  = "filterRes";
    inline constexpr const char* dist       = "dist";
    inline constexpr const char* distTone   = "distTone";
    inline constexpr const char* crush      = "crush";
    inline constexpr const char* reverb     = "reverb";
    inline constexpr const char* revDecay   = "revDecay";
    inline constexpr const char* revDamp    = "revDamp";
    inline constexpr const char* revWidth   = "revWidth";
    inline constexpr const char* revPre     = "revPre";
    inline constexpr const char* echoMix    = "echoMix";
    inline constexpr const char* echoFb     = "echoFb";
    inline constexpr const char* echoTime   = "echoTime";
    inline constexpr const char* echoMono   = "echoMono";
    // Generate
    inline constexpr const char* patAmount  = "patAmount";
    inline constexpr const char* fxAmount   = "fxAmount";
    inline constexpr const char* style      = "style";
    inline constexpr const char* swing      = "swing";
    inline constexpr const char* layers     = "layers";
    // Tempo / trigger
    inline constexpr const char* speed      = "speed";
    inline constexpr const char* fit        = "fit";
    inline constexpr const char* fitMode    = "fitMode";
    inline constexpr const char* trigger    = "trigger";
    inline constexpr const char* breakMode  = "breakMode";
    inline constexpr const char* gateMode   = "gateMode";
    inline constexpr const char* velSlice   = "velSlice";
    inline constexpr const char* tempoSrc   = "tempoSrc";
    // FX pads
    inline constexpr const char* padQuant   = "padQuant";
    inline constexpr const char* stutRate   = "stutRate";
    inline constexpr const char* rollRate   = "rollRate";
    inline constexpr const char* tapeMs     = "tapeMs";
    inline constexpr const char* akCycle    = "akCycle";
    inline constexpr const char* akRatio    = "akRatio";
    inline constexpr const char* diveRes    = "diveRes";
    inline constexpr const char* dropRate   = "dropRate";
    inline constexpr const char* stretchCycle = "stretchCycle";

        inline constexpr const char* lcdColour = "lcdColour";
    // Akaizer (0.6)
    inline constexpr const char* akCycLen      = "akCycLen";
    inline constexpr const char* akCycFreq     = "akCycFreq";
    inline constexpr const char* akFreqManual  = "akFreqManual";
    inline constexpr const char* slCycLen      = "slCycLen";
    inline constexpr const char* slCycFreq     = "slCycFreq";
    inline constexpr const char* slFreqManual  = "slFreqManual";
    inline constexpr const char* akrStretch    = "akrStretch";
    inline constexpr const char* akrSync       = "akrSync";
    inline constexpr const char* akrTarget     = "akrTarget";
    inline constexpr const char* akrCycLen     = "akrCycLen";
    inline constexpr const char* akrCycFreq    = "akrCycFreq";
    inline constexpr const char* akrFreqManual = "akrFreqManual";
    inline constexpr const char* akrPitch      = "akrPitch";
    inline constexpr const char* akMode        = "akMode";
    inline constexpr const char* akPitch       = "akPitch";
    inline constexpr const char* rollMode      = "rollMode";
    inline constexpr const char* halfMode      = "halfMode";
    inline constexpr const char* fill          = "fill";
    inline const char* padParamId (int pad) { return paramDefs()[(size_t) (P_PAD1 + pad)].id; }
    using bc::fxPadName;
}
