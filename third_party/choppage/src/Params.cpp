#include "choppage/Params.h"
#include "choppage/Akaizer.h"
#include <cstdio>

namespace bc
{
    namespace
    {
        std::vector<std::string> characterNames()
        {
            std::vector<std::string> a;
            for (int i = 0; i < kNumCharacters; ++i) a.push_back (character (i).name);
            return a;
        }
        const std::vector<std::string> kRates { "1/8", "1/16", "1/32", "1/64" };
    }

    const std::vector<ParamDef>& paramDefs()
    {
        static const std::vector<ParamDef> d = []
        {
            std::vector<ParamDef> v;
            auto F = [&] (const char* id, const char* name, float lo, float hi, float def, PDisp disp = PDisp::plain2, float centre = 0.0f)
            { v.push_back ({ id, name, PKind::Float, lo, hi, def, disp, {}, centre }); };
            auto C = [&] (const char* id, const char* name, std::vector<std::string> ch, int def)
            { v.push_back ({ id, name, PKind::Choice, 0.0f, (float) (ch.size() - 1), (float) def, PDisp::plain2, ch }); };
            auto B = [&] (const char* id, const char* name, bool def)
            { v.push_back ({ id, name, PKind::Bool, 0.0f, 1.0f, def ? 1.0f : 0.0f }); };

            F ("srate", "Sample Rate", 4000.0f, 44100.0f, 44100.0f, PDisp::khz, 15000.0f);
            C ("character", "Character", characterNames(), 1);
            F ("volume", "Volume", -60.0f, 6.0f, 0.0f, PDisp::db);
            F ("filter", "Filter", 0, 1, 1, PDisp::percent);
            C ("filterType", "Filter Type", { "LP", "HP" }, 0);
            F ("filterRes", "Filter Res", 0, 1, 0.2f, PDisp::percent);
            F ("dist", "Distortion", 0, 1, 0, PDisp::percent);
            F ("distTone", "Dist Tone", -1, 1, 0, PDisp::signed2);
            F ("crush", "Crush", 0, 1, 0, PDisp::percent);
            F ("reverb", "Reverb", 0, 1, 0, PDisp::percent);
            F ("revDecay", "Reverb Decay", 0, 1, 0.33f, PDisp::percent);
            F ("revDamp", "Reverb Damp", 0, 1, 0.5f, PDisp::percent);
            F ("revWidth", "Reverb Width", 0, 1, 0.8f, PDisp::percent);
            F ("revPre", "Reverb Pre", 0, 100, 10, PDisp::ms0);
            F ("echoMix", "Echo Mix", 0, 1, 0, PDisp::percent);
            F ("echoFb", "Echo Feedback", 0, 0.95f, 0.4f, PDisp::percent);
            C ("echoTime", "Echo Time", { "1/16", "1/8", "1/8.", "1/4", "1/4." }, 2);
            B ("echoMono", "Echo Mono", true);
            F ("patAmount", "Pattern Amount", 0, 1, 0.35f, PDisp::percent);
            F ("fxAmount", "FX Amount", 0, 1, 0.2f, PDisp::percent);
            C ("style", "Style", styleNames(), 0);
            F ("swing", "Swing", 0.5f, 0.75f, 0.5f, PDisp::percent);
            B ("layers", "Layers", true);
            C ("speed", "Speed", { "0.5X", "1X", "2X" }, 1);
            B ("fit", "Fit", true);
            C ("fitMode", "Fit Mode", { "REPITCH", "KEEP" }, 0);
            C ("trigger", "Trigger", { "SYNC", "MIDI" }, 0);
            C ("breakMode", "Break Switch", { "RESTART", "BAR SYNC", "QUANT" }, 1);
            C ("gateMode", "Gate", { "LATCH", "HOLD", "ONE SHOT" }, 0);
            B ("velSlice", "Velocity To Start Slice", false);
            C ("tempoSrc", "Tempo Source", { "AUTO", "/2", "X2" }, 0);
            C ("padQuant", "Pad Quantise", { "OFF", "1/16", "1/8", "1/4", "1/32", "1 BAR" }, 1);   // (appended: saved values stay)
            C ("stutRate", "Stutter Rate", kRates, 1);
            C ("rollRate", "Roll Rate", kRates, 2);
            F ("tapeMs", "Tape Stop Time", 100, 3000, 700, PDisp::ms0);
            F ("akCycle", "(old) Akaize Cycle ms", 2, 60, 9.5f, PDisp::ms1);
            C ("akRatio", "Akaize Stretch", { "1.5X", "2X", "3X", "4X" }, 1);
            F ("diveRes", "Filter Dive Res", 0, 1, 0.7f, PDisp::percent);
            F ("dropRate", "S950 Drop Rate", 4000, 15600, 7500, PDisp::khz);
            F ("stretchCycle", "(old) Slice Stretch Cycle ms", 2, 60, 9.5f, PDisp::ms1);
            for (int i = 0; i < kFxPads; ++i)
            {
                static const char* ids[kFxPads] = { "pad1", "pad2", "pad3", "pad4", "pad5", "pad6", "pad7", "pad8" };
                B (ids[i], fxPadName (i), false);
            }
            C ("lcdColour", "LCD Colour", { "BLUE", "GREEN", "AMBER", "WHITE" }, 0);
            // 0.6: Akaizer settings (the ranges and defaults of Akaizer v1.2 / Twisted-Timestretch)
            F ("akCycLen", "Akaize Pad Cycle Length", 20, 2000, 800, PDisp::samples, 400);
            F ("akCycFreq", "Akaize Pad Cycle Freq", 20, 2050, 51, PDisp::cps, 200);
            B ("akFreqManual", "Akaize Pad Manual Freq", false);
            F ("slCycLen", "Slice Stretch Cycle Length", 20, 2000, 800, PDisp::samples, 400);
            F ("slCycFreq", "Slice Stretch Cycle Freq", 20, 2050, 51, PDisp::cps, 200);
            B ("slFreqManual", "Slice Stretch Manual Freq", false);
            F ("akrStretch", "Akaize Render Stretch", 25, 2000, 100, PDisp::pct2, 150);
            B ("akrSync", "Akaize Render Sync", true);
            F ("akrTarget", "Akaize Render Target BPM", 40, 300, 170, PDisp::bpm);
            F ("akrCycLen", "Akaize Render Cycle Length", 20, 2000, 800, PDisp::samples, 400);
            F ("akrCycFreq", "Akaize Render Cycle Freq", 20, 2050, 51, PDisp::cps, 200);
            B ("akrFreqManual", "Akaize Render Manual Freq", false);
            F ("akrPitch", "Akaize Render Pitch", -24, 24, 0, PDisp::semis);
            C ("akMode", "Akaize Mode", { "STRETCH", "IN TIME" }, 0);
            F ("akPitch", "Akaize In Time Pitch", -24, 24, 0, PDisp::semis);
            C ("rollMode", "Roll Mode", rollModeNames(), 0);
            C ("halfMode", "Half-Time Mode", halfModeNames(), 0);
            B ("fill", "Fill", false);
            return v;
        }();
        return d;
    }

    int paramIndex (const std::string& id)
    {
        const auto& d = paramDefs();
        for (size_t i = 0; i < d.size(); ++i) if (id == d[i].id) return (int) i;
        return -1;
    }

    float paramToNorm (int i, float plain)
    {
        const auto& p = paramDefs()[(size_t) i];
        if (p.max <= p.min) return 0.0f;
        float n = clampv ((plain - p.min) / (p.max - p.min), 0.0f, 1.0f);
        if (p.centre > 0.0f)
        {
            const float c = (p.centre - p.min) / (p.max - p.min);       // skew: norm^k, k so that c maps to 0.5
            const float k = std::log (0.5f) / std::log (c);   // c^k = 0.5
            n = std::pow (n, k);
        }
        return n;
    }

    float paramFromNorm (int i, float n)
    {
        const auto& p = paramDefs()[(size_t) i];
        n = clampv (n, 0.0f, 1.0f);
        if (p.kind == PKind::Bool) return n >= 0.5f ? 1.0f : 0.0f;
        if (p.kind == PKind::Choice) return std::round (n * (p.max - p.min));
        if (p.centre > 0.0f)
        {
            const float c = (p.centre - p.min) / (p.max - p.min);
            const float k = std::log (0.5f) / std::log (c);
            n = std::pow (n, 1.0f / k);
        }
        return p.min + n * (p.max - p.min);
    }

    std::string formatParam (int i, float v)
    {
        const auto& p = paramDefs()[(size_t) i];
        char b[64];
        if (p.kind == PKind::Bool) return v >= 0.5f ? "ON" : "OFF";
        if (p.kind == PKind::Choice)
        {
            const int k = clampv ((int) std::lround (v), 0, (int) p.choices.size() - 1);
            return p.choices[(size_t) k];
        }
        switch (p.disp)
        {
            case PDisp::percent: std::snprintf (b, sizeof b, "%d%%", (int) std::lround (v * 100.0f)); break;
            case PDisp::ms0:     std::snprintf (b, sizeof b, "%d ms", (int) std::lround (v)); break;
            case PDisp::ms1:     std::snprintf (b, sizeof b, "%.1f ms", (double) v); break;
            case PDisp::khz:     std::snprintf (b, sizeof b, "%.1f kHz", (double) v / 1000.0); break;
            case PDisp::db:      if (v <= -59.9f) return "-INF"; std::snprintf (b, sizeof b, "%.1f dB", (double) v); break;
            case PDisp::signed2: std::snprintf (b, sizeof b, "%+.2f", (double) v); break;
            case PDisp::plain2:  std::snprintf (b, sizeof b, "%.2f", (double) v); break;
            case PDisp::samples: std::snprintf (b, sizeof b, "%d smp", (int) std::lround (v)); break;
            case PDisp::cps:     std::snprintf (b, sizeof b, "%d cps", (int) std::lround (v)); break;
            case PDisp::semis:   { const int st = (int) std::lround (v); std::snprintf (b, sizeof b, st == 0 ? "0 st" : "%+d st", st); break; }
            case PDisp::pct2:    std::snprintf (b, sizeof b, "%.2f%%", (double) v); break;
            case PDisp::bpm:     std::snprintf (b, sizeof b, "%.2f BPM", (double) v); break;
        }
        return b;
    }

    int    noteRateDivisor (int idx) { static const int d[] = { 8, 16, 32, 64 }; return d[clampv (idx, 0, 3)]; }
    double echoBeats (int idx)       { static const double b[] = { 0.25, 0.5, 0.75, 1.0, 1.5 }; return b[clampv (idx, 0, 4)]; }
    double speedFactor (int idx)     { static const double s[] = { 0.5, 1.0, 2.0 }; return s[clampv (idx, 0, 2)]; }
    double akRatioValue (int idx)    { static const double r[] = { 1.5, 2.0, 3.0, 4.0 }; return r[clampv (idx, 0, 3)]; }
    int    akaizeFreq (float cycleLength, float freq, bool manual)
    {
        return manual ? clampv ((int) std::lround (freq), 20, 2050)
                      : akaizer::linkedFrequency (clampv ((int) std::lround (cycleLength), 20, 2000));
    }

    const std::vector<std::string>& rollModeNames()
    {
        static const std::vector<std::string> n { "PITCH UP", "VEL UP", "VEL UP +PITCH", "VEL DOWN", "VEL DOWN -PITCH", "PITCH DOWN" };
        return n;
    }
    const std::vector<std::string>& halfModeNames()
    {
        static const std::vector<std::string> n { "1/2", "3/4", "2/3", "1/4", "1/2 TAPE" };
        return n;
    }
    double halfTimeFactor (int mode) { static const double f[] = { 0.5, 0.75, 2.0 / 3.0, 0.25, 0.5 }; return f[clampv (mode, 0, 4)]; }

    const char* fxPadName (int i)
    {
        static const char* n[kFxPads] = { "STUTTER", "ROLL", "REVERSE", "TAPE STOP", "HALF-TIME", "AKAIZE", "FILT DIVE", "S950 DROP" };
        return n[clampv (i, 0, kFxPads - 1)];
    }

    Engine::Settings settingsFromParams (const float* v)
    {
        auto i = [v] (int k) { return (int) std::lround (v[k]); };
        Engine::Settings s;
        s.midiTrigger = i (P_TRIGGER) == 1;
        s.breakMode   = i (P_BREAK_MODE);
        s.gateMode    = i (P_GATE_MODE);
        s.velToSlice  = v[P_VEL_SLICE] > 0.5f;
        s.speed       = speedFactor (i (P_SPEED));
        s.fit         = v[P_FIT] > 0.5f;
        s.keepPitch   = i (P_FIT_MODE) == 1;
        s.tempoSourceMul = i (P_TEMPO_SRC) == 1 ? 0.5 : i (P_TEMPO_SRC) == 2 ? 2.0 : 1.0;
        s.swing       = v[P_SWING];
        s.slCycleLength = clampv (i (P_SL_CYC_LEN), 20, 2000);
        s.slCycleFreq   = akaizeFreq (v[P_SL_CYC_LEN], v[P_SL_CYC_FREQ], v[P_SL_FREQ_MAN] > 0.5f);

        s.pads.stutterDiv = noteRateDivisor (i (P_STUT_RATE));
        s.pads.rollDiv    = noteRateDivisor (i (P_ROLL_RATE));
        s.pads.tapeMs     = v[P_TAPE_MS];
        s.pads.akCycleLength = clampv (i (P_AK_CYC_LEN), 20, 2000);
        s.pads.akCycleFreq   = akaizeFreq (v[P_AK_CYC_LEN], v[P_AK_CYC_FREQ], v[P_AK_FREQ_MAN] > 0.5f);
        s.pads.akRatio    = akRatioValue (i (P_AK_RATIO));
        s.pads.akInTime   = i (P_AK_MODE) == 1;
        s.pads.akPitch    = std::pow (2.0, std::round (v[P_AK_PITCH]) / 12.0);
        s.pads.diveRes    = v[P_DIVE_RES];
        s.pads.rollMode   = clampv (i (P_ROLL_MODE), 0, 5);
        s.halfMode        = clampv (i (P_HALF_MODE), 0, 4);
        s.fill            = v[P_FILL] > 0.5f;
        {
            static const double grid16[] = { 0.0, 1.0, 2.0, 4.0, 0.5, 16.0 };
            s.pads.quantSteps = grid16[clampv (i (P_PAD_QUANT), 0, 5)];
        }

        auto& c = s.sound;
        c.character = i (P_CHARACTER);
        c.faderRate = v[P_SRATE];
        c.dropRate  = v[P_DROP_RATE];
        c.filter = v[P_FILTER]; c.highPass = i (P_FILTER_TYPE) == 1; c.filterRes = v[P_FILTER_RES];
        c.dist = v[P_DIST]; c.distTone = v[P_DIST_TONE];
        c.crush = v[P_CRUSH];
        c.reverb = v[P_REVERB]; c.revDecay = v[P_REV_DECAY]; c.revDamp = v[P_REV_DAMP];
        c.revWidth = v[P_REV_WIDTH]; c.revPreMs = v[P_REV_PRE];
        c.echoMix = v[P_ECHO_MIX]; c.echoFb = v[P_ECHO_FB]; c.echoMono = v[P_ECHO_MONO] > 0.5f;
        c.volumeDb = v[P_VOLUME];
        return s;
    }
}
