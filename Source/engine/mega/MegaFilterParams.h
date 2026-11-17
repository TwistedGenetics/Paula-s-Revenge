#pragma once
// The few settings the Mega Synth filter code shares with the rest of Mega Synth (its Params.h):
// the filter types and the slopes each type allows. Copied as they are, so the filters behave the same.
namespace tg
{
enum FilterType { FT_LP, FT_HP, FT_BP, FT_NOTCH, FT_PEAK, FT_AP, FT_TYPE_COUNT };
// The slope each classic model has as a low-pass (its original circuit). Low Pass at this slope
// runs that model exactly as it was.
inline int nativeFilterSlope (int model)
{
    static const int s[17] = { 1, 3, 3, 2, 2, 3, 1, 3, 3, 1, 1, 2, 3, 3, 1, 2, 3 };
    return model >= 0 && model < 17 ? s[model] : 1;
}
// Slopes that mean something for each type (bit k = slope k). Peak / Bell has none.
inline int filterSlopeMask (int type)
{
    switch (type)
    {
        case FT_BP: case FT_NOTCH: return 0b1010;   // 12, 24
        case FT_PEAK: return 0;
        default: return 0b1111;                       // LP, HP, All Pass: 6, 12, 18, 24
    }
}
// The slope actually used: the nearest one the type allows.
inline int effectiveFilterSlope (int type, int slope)
{
    const int mask = filterSlopeMask (type);
    if (mask == 0) return 1;
    if (mask & (1 << slope)) return slope;
    return slope <= 1 ? 1 : 3;
}
}
