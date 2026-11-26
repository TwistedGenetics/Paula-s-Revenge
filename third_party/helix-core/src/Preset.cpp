#include "helix/Preset.h"
#include "helix/FxLibrary.h"
#include "helix/Engine.h"
#include "helix/Math.h"
#include "helix/TableLibrary.h"

namespace helix {

Preset::Preset()
{
    for (int i = 0; i < kNumParams; ++i) params[i] = paramDef(i).def;
    LfoShapes d;
    for (int l = 0; l < kNumLfos; ++l)
    {
        for (int s = 0; s < kMaxSteps; ++s) lfoSteps[l][s] = d.steps[l][s].load();
        lfoPoints[l] = d.numPoints[l].load();
        for (int k = 0; k < kLfoMaxPoints; ++k) { lfoPx[l][k] = d.px[l][k].load(); lfoPy[l][k] = d.py[l][k].load(); }
    }
    MacroScenes sc;
    for (int s = 0; s < kNumScenes; ++s) for (int m = 0; m < kNumMacros; ++m) scenes[s][m] = sc.v[s][m].load();
}

Json Preset::toJson() const
{
    Json j = Json::object();
    j.set("format", "helix-preset");
    j.set("version", kPresetVersion);
    j.set("name", name);
    j.set("category", category);
    j.set("author", author);
    j.set("comment", comment);
    Json ps = Json::object();
    for (int i = 0; i < kNumParams; ++i) ps.set(paramDef(i).id, (double)params[i]);
    j.set("params", ps);
    Json t = Json::object();
    t.set("A", tables[0]);
    t.set("B", tables[1]);
    j.set("tables", t);
    Json lfos = Json::array();
    for (int l = 0; l < kNumLfos; ++l)
    {
        Json o = Json::object();
        Json st = Json::array();
        for (int s = 0; s < kMaxSteps; ++s) st.push((double)lfoSteps[l][s]);
        o.set("steps", st);
        Json pts = Json::array();
        for (int k = 0; k < clamp(lfoPoints[l], 2, kLfoMaxPoints); ++k)
        {
            Json pt = Json::array();
            pt.push((double)lfoPx[l][k]);
            pt.push((double)lfoPy[l][k]);
            pts.push(pt);
        }
        o.set("points", pts);
        lfos.push(o);
    }
    j.set("lfo", lfos);
    Json sc = Json::array();
    for (int s = 0; s < kNumScenes; ++s)
    {
        Json row = Json::array();
        for (int m = 0; m < kNumMacros; ++m) row.push((double)scenes[s][m]);
        sc.push(row);
    }
    j.set("scenes", sc);
    return j;
}

bool Preset::fromJson(const Json& j, Preset& out, std::string* error)
{
    if (!j.isObject() || j.get("format").string() != "helix-preset")
    {
        if (error) *error = "not a HELIX preset";
        return false;
    }
    const int version = (int)j.get("version").number(0);
    if (version < 1) { if (error) *error = "bad preset version"; return false; }
    Preset p;   // defaults for anything missing
    if (j.get("name").isString()) p.name = j.get("name").string();
    if (j.get("category").isString()) p.category = j.get("category").string();
    if (j.get("author").isString()) p.author = j.get("author").string();
    if (j.get("comment").isString()) p.comment = j.get("comment").string();
    for (const auto& kv : j.get("params").items())
    {
        const int idx = paramIndexFromId(kv.first);
        if (idx >= 0 && kv.second.isNumber()) p.params[idx] = paramDef(idx).clampPlain((float)kv.second.number());
    }
    if (j.get("tables").get("A").isString()) p.tables[0] = j.get("tables").get("A").string();
    if (j.get("tables").get("B").isString()) p.tables[1] = j.get("tables").get("B").string();
    const Json& lfos = j.get("lfo");
    for (int l = 0; l < kNumLfos && l < (int)lfos.size(); ++l)
    {
        const Json& st = lfos[(size_t)l].get("steps");
        for (int s = 0; s < kMaxSteps && s < (int)st.size(); ++s) p.lfoSteps[l][s] = clamp((float)st[(size_t)s].number(), -1.0f, 1.0f);
        const Json& pts = lfos[(size_t)l].get("points");
        if (pts.size() >= 2)
        {
            const int n = std::min((int)pts.size(), kLfoMaxPoints);
            float lastX = 0.0f;
            for (int k = 0; k < n; ++k)
            {
                const float x = clamp((float)pts[(size_t)k][0].number(), lastX, 1.0f);   // keep x rising
                p.lfoPx[l][k] = x;
                p.lfoPy[l][k] = clamp((float)pts[(size_t)k][1].number(), -1.0f, 1.0f);
                lastX = x;
            }
            p.lfoPoints[l] = n;
        }
    }
    const Json& sc = j.get("scenes");
    for (int s = 0; s < kNumScenes && s < (int)sc.size(); ++s)
        for (int m = 0; m < kNumMacros && m < (int)sc[(size_t)s].size(); ++m)
            p.scenes[s][m] = clamp((float)sc[(size_t)s][(size_t)m].number(), 0.0f, 1.0f);
    // Presets from before 0.3.6 (version 1): the FX rack's switched-on effects move into the effect slots.
    if (version < 2) fxlib::foldRack(p.params);
    out = p;
    return true;
}

bool Preset::fromString(const std::string& text, Preset& out, std::string* error)
{
    Json j;
    if (!Json::parse(text, j, error)) return false;
    return fromJson(j, out, error);
}

Preset capturePreset(const Engine& e, const TableLibrary* lib)
{
    Preset p;
    for (int i = 0; i < kNumParams; ++i) p.params[i] = e.params().get(i);
    if (lib)
        for (int s = 0; s < 2; ++s)
        {
            // A library table is named by the table parameter (it may not have finished loading yet);
            // a loose file or Sample -> Table result by the slot's own id.
            const std::string slot = lib->slotId(s);
            p.tables[s] = TableLibrary::isLibraryId(slot) || slot.empty() ? lib->idAt(e.params().getInt(s == 0 ? P_A_TABLE : P_B_TABLE)) : slot;
        }
    const LfoShapes& sh = e.lfoShapes();
    for (int l = 0; l < kNumLfos; ++l)
    {
        for (int s = 0; s < kMaxSteps; ++s) p.lfoSteps[l][s] = sh.steps[l][s].load();
        p.lfoPoints[l] = sh.numPoints[l].load();
        for (int k = 0; k < kLfoMaxPoints; ++k) { p.lfoPx[l][k] = sh.px[l][k].load(); p.lfoPy[l][k] = sh.py[l][k].load(); }
    }
    for (int s = 0; s < kNumScenes; ++s) for (int m = 0; m < kNumMacros; ++m) p.scenes[s][m] = e.scenes().v[s][m].load();
    return p;
}

void applyPreset(const Preset& p, Engine& e, TableLibrary* lib)
{
    LfoShapes& sh = e.lfoShapes();
    for (int l = 0; l < kNumLfos; ++l)
    {
        for (int s = 0; s < kMaxSteps; ++s) sh.steps[l][s].store(p.lfoSteps[l][s]);
        for (int k = 0; k < kLfoMaxPoints; ++k) { sh.px[l][k].store(p.lfoPx[l][k]); sh.py[l][k].store(p.lfoPy[l][k]); }
        sh.numPoints[l].store(p.lfoPoints[l]);
    }
    for (int s = 0; s < kNumScenes; ++s) for (int m = 0; m < kNumMacros; ++m) e.scenes().v[s][m].store(p.scenes[s][m]);
    float params[kNumParams];
    std::copy(p.params, p.params + kNumParams, params);
    if (lib)
        for (int s = 0; s < 2; ++s)
        {
            const int idx = lib->indexOfId(p.tables[s]);
            const int param = s == 0 ? P_A_TABLE : P_B_TABLE;
            // Keep the saved index when it already names this table (the index wraps); point it at the table
            // otherwise. A table outside the library (loose file, Sample -> Table) leaves the index alone.
            if (idx >= 0) { if (lib->wrap((int)params[param]) != idx) params[param] = (float)idx; }
            else params[param] = e.params().get(param);
            lib->requestLoadId(s, p.tables[s]);
        }
    for (int i = 0; i < kNumParams; ++i) e.params().set(i, params[i]);
}

void mutatePreset(Preset& p, float amount, uint32_t seed)
{
    Rng rng(seed ? seed : 1u);
    for (int i = 0; i < kNumParams; ++i)
    {
        const ParamDef& d = paramDef(i);
        const bool continuous = d.kind == ParamKind::Linear || d.kind == ParamKind::Log || d.kind == ParamKind::Squared;
        if (!continuous || i == P_MASTER) continue;
        // Leave modulation amounts that are off alone (nudging 0 would switch a dead slot on).
        if (i >= P_M1_SRC && i <= P_M16_CURVE && ((i - P_M1_SRC) % (P_M2_SRC - P_M1_SRC)) == 2 && p.params[i] == 0.0f) continue;
        const float n = d.toNormalised(p.params[i]) + amount * rng.bi();
        p.params[i] = d.toPlain(clamp(n, 0.0f, 1.0f));
    }
}

} // namespace helix
