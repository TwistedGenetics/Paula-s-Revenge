# choppage in Paula's Revenge

Copied from TwistedGenetics/Choppage: `choppage-core` (src, include, third_party/velociloops - public domain)
and `choppage-desktop/Source` (as `desktop/`, files renamed Choppage*.h/.cpp and UI/ → ChopUI/ so they can't
be mixed up with Paula's own). With `PRV_EMBED_CHOPPAGE` the processor is a `juce::AudioPluginInstance`, so
Paula hosts it like a plug-in (its own window, its state saved in the song) without a plug-in file.
