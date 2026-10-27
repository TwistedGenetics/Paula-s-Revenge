// Paula's Revenge - plug-in instruments as the player sees them
#include "PluginSynth.h"

namespace prv
{
std::vector<SynthRack::Pending> SynthRack::plan (const Song& song, double sr, bool reloadAll) const
{
    std::vector<Pending> out;
    for (int i = 0; i < kMaxInstruments; ++i)
    {
        const auto& in = song.instruments[(size_t) i];
        const std::string key = in.isPlugin() ? in.pluginKey : std::string();
        const bool have = synths[i] != nullptr || (tried[i] && ! key.empty());
        if (key == keys[i] && (key.empty() || have) && ! (reloadAll && ! key.empty())) continue;
        Pending p { i, key, nullptr };
        if (! key.empty() && factory) p.synth = factory (i, in, sr);
        out.push_back (std::move (p));
    }
    return out;
}

void SynthRack::apply (std::vector<Pending>& changes, std::vector<std::unique_ptr<PluginSynth>>& old)
{
    for (auto& p : changes)
    {
        if (synths[p.instrument]) old.push_back (std::move (synths[p.instrument]));
        synths[p.instrument] = std::move (p.synth);
        keys[p.instrument] = p.key;
        tried[p.instrument] = ! p.key.empty();
    }
    changes.clear();
    count = 0;
    for (auto& s : synths) if (s) ++count;
}
} // namespace prv
