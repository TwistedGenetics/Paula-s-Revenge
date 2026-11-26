#include "helix/UiLayout.h"
#include "helix/Params.h"

namespace helix {

static void oscPage(UiPage& pg, bool b)
{
    auto P = [b](int a, int bb) { return b ? bb : a; };
    const std::string X = b ? "B" : "A";
    pg.sections = {
        {"Osc " + X, {P(P_A_ON, P_B_ON), P(P_A_TABLE, P_B_TABLE), P(P_A_POS, P_B_POS), P(P_A_LEVEL, P_B_LEVEL)}},
        {"Pitch", {P(P_A_OCT, P_B_OCT), P(P_A_SEMI, P_B_SEMI), P(P_A_FINE, P_B_FINE), P(P_A_PAN, P_B_PAN)}},
        {"Warp", {P(P_A_W1_MODE, P_B_W1_MODE), P(P_A_W1_AMT, P_B_W1_AMT), P(P_A_W2_MODE, P_B_W2_MODE), P(P_A_W2_AMT, P_B_W2_AMT)}},
        {"Unison", {P(P_A_UNISON, P_B_UNISON), P(P_A_DETUNE, P_B_DETUNE), P(P_A_BLEND, P_B_BLEND), P(P_A_SPREAD, P_B_SPREAD)}},
        {"Phase / Route", {P(P_A_PHASE, P_B_PHASE), P(P_A_RAND, P_B_RAND), P(P_A_TO_F1, P_B_TO_F1), P(P_A_TO_F2, P_B_TO_F2)}},
    };
}

static std::vector<UiPage> build()
{
    std::vector<UiPage> pages;
    UiPage a{"OSC A", {}}; oscPage(a, false); pages.push_back(a);
    UiPage b{"OSC B", {}}; oscPage(b, true); pages.push_back(b);

    pages.push_back({"SUB/NOISE", {
        {"Sub", {P_SUB_ON, P_SUB_SHAPE, P_SUB_OCT, P_SUB_LEVEL}},
        {"Sub Out", {P_SUB_PAN, P_SUB_DIRECT, P_SUB_TO_F1, P_SUB_TO_F2}},
        {"Noise", {P_NOISE_ON, P_NOISE_TYPE, P_NOISE_LEVEL, P_NOISE_PAN}},
        {"Noise Opts", {P_NOISE_PITCH, P_NOISE_LOOP, P_NOISE_TO_F1, P_NOISE_TO_F2}},
    }});

    pages.push_back({"FILTERS", {
        {"Filter 1", {P_F1_ON, P_F1_TYPE, P_F1_CUTOFF, P_F1_RES}},
        {"F1 Tone", {P_F1_DRIVE, P_F1_KEYTRK, P_F1_FAT, P_F1_LEVEL}},
        {"Filter 2", {P_F2_ON, P_F2_TYPE, P_F2_CUTOFF, P_F2_RES}},
        {"F2 Tone", {P_F2_DRIVE, P_F2_KEYTRK, P_F2_FAT, P_F2_LEVEL}},
        {"Routing", {P_FILTER_BLEND, -1, -1, -1}},
    }});

    UiPage env{"ENV", {}};
    for (int e = 0; e < 4; ++e)
    {
        const int base = P_E1_A + e * (P_E2_A - P_E1_A);
        const std::string n = std::to_string(e + 1) + (e == 0 ? " (Amp)" : "");
        env.sections.push_back({"Env " + n, {base, base + 1, base + 2, base + 3}});
        env.sections.push_back({"Env " + std::to_string(e + 1) + " Curves", {base + 4, base + 5, base + 6, -1}});
    }
    pages.push_back(env);

    UiPage lfo{"LFO/PERF", {}};
    for (int l = 0; l < 4; ++l)
    {
        const int base = P_L1_SHAPE + l * (P_L2_SHAPE - P_L1_SHAPE);
        const std::string n = std::to_string(l + 1);
        const std::string grp = l < 2 ? "LFO 1-2" : "LFO 3-4";
        lfo.sections.push_back({"LFO " + n, {base, base + 1, base + 2, base + 3}, grp});
        lfo.sections.push_back({"LFO " + n + " Opts", {base + 4, base + 5, base + 6, base + 7}, grp});
    }
    lfo.sections.push_back({"LFO Steps", {P_L1_STEPS, P_L2_STEPS, P_L3_STEPS, P_L4_STEPS}, "PERF"});
    lfo.sections.push_back({"Perf 1", {P_P1_DIV, P_P1_STEPS, P_P1_MODE, -1}, "PERF"});
    lfo.sections.push_back({"Perf 2", {P_P2_DIV, P_P2_STEPS, P_P2_MODE, -1}, "PERF"});
    for (int p = 0; p < 2; ++p)
    {
        const int base = P_P1_DIV + p * (P_P2_DIV - P_P1_DIV);
        const std::string n = std::to_string(p + 1);
        for (int s = 0; s < 16; s += 4)
            lfo.sections.push_back({"P" + n + " Steps " + std::to_string(s + 1) + "-" + std::to_string(s + 4),
                                    {base + 3 + s, base + 4 + s, base + 5 + s, base + 6 + s}, "P" + n + " STEP"});
        for (int s = 0; s < 16; s += 4)
            lfo.sections.push_back({"P" + n + " Shapes " + std::to_string(s + 1) + "-" + std::to_string(s + 4),
                                    {base + 19 + s, base + 20 + s, base + 21 + s, base + 22 + s}, "P" + n + " SHAPE"});
    }
    pages.push_back(lfo);

    UiPage mx{"MATRIX", {}};
    for (int m = 0; m < 16; ++m)
    {
        const int base = P_M1_SRC + m * (P_M2_SRC - P_M1_SRC);
        mx.sections.push_back({"Slot " + std::to_string(m + 1), {base, base + 1, base + 2, base + 3},
                               "M" + std::to_string(m / 4 * 4 + 1) + "-" + std::to_string(m / 4 * 4 + 4)});
    }
    for (int m = 0; m < 16; m += 4)
        mx.sections.push_back({"Curves " + std::to_string(m + 1) + "-" + std::to_string(m + 4),
                               {P_M1_CURVE + m * 5, P_M1_CURVE + (m + 1) * 5, P_M1_CURVE + (m + 2) * 5, P_M1_CURVE + (m + 3) * 5}, "CURVES"});
    pages.push_back(mx);

    pages.push_back({"FX", {
        {"Order 1-4", {P_FX_ORDER1, P_FX_ORDER2, P_FX_ORDER3, P_FX_ORDER4}},
        {"Order 5-7", {P_FX_ORDER5, P_FX_ORDER6, P_FX_ORDER7, -1}},
        {"Distortion", {P_DIST_ON, P_DIST_TYPE, P_DIST_DRIVE, P_DIST_MIX}},
        {"Dist Tone", {P_DIST_TONE, P_DIST_BITS, P_DIST_RATE, -1}},
        {"Chorus", {P_CHO_ON, P_CHO_MODE, P_CHO_RATE, P_CHO_MIX}},
        {"Chorus Depth", {P_CHO_DEPTH, -1, -1, -1}},
        {"Phaser", {P_PH_ON, P_PH_RATE, P_PH_DEPTH, P_PH_MIX}},
        {"Phaser Tone", {P_PH_FREQ, P_PH_FEEDBACK, -1, -1}},
        {"Delay", {P_DLY_ON, P_DLY_MODE, P_DLY_FEEDBACK, P_DLY_MIX}},
        {"Delay Time", {P_DLY_SYNC, P_DLY_DIV, P_DLY_TIME, P_DLY_TONE}},
        {"Reverb", {P_REV_ON, P_REV_TYPE, P_REV_DECAY, P_REV_MIX}},
        {"Reverb Space", {P_REV_SIZE, P_REV_DAMP, P_REV_PREDELAY, -1}},
        {"OTT", {P_OTT_ON, P_OTT_DEPTH, P_OTT_TIME, P_OTT_GAIN}},
        {"OTT Bands", {P_OTT_UP, P_OTT_DOWN, -1, -1}},
        {"EQ Low / Mid", {P_EQ_ON, P_EQ_LOW_FREQ, P_EQ_LOW_GAIN, P_EQ_MID_FREQ}},
        {"EQ Mid / High", {P_EQ_MID_GAIN, P_EQ_MID_Q, P_EQ_HIGH_FREQ, P_EQ_HIGH_GAIN}},
    }});

    pages.push_back({"MACROS", {
        {"Macros 1-4", {P_MACRO1, P_MACRO2, P_MACRO3, P_MACRO4}},
        {"Macros 5-8", {P_MACRO5, P_MACRO6, P_MACRO7, P_MACRO8}},
    }});

    pages.push_back({"BROWSER", {
        {"Global", {P_MASTER, P_POLY_MODE, P_VOICES, P_GLIDE}},
        {"Voice", {P_GLIDE_MODE, P_STEAL, P_BEND_RANGE, P_VEL_SENS}},
        {"Engine", {P_QUALITY, P_AUTO_SCALE, -1, -1}},
    }});
    return pages;
}

const std::vector<UiPage>& uiPages()
{
    static const std::vector<UiPage> pages = build();
    return pages;
}

} // namespace helix
