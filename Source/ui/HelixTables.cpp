// Paula's Revenge - HELIX's wavetables on its page (see HelixTables.h)
#include "HelixTables.h"
#include "../Prefs.h"
#include "../engine/HelixSynth.h"
#include <map>
#include <mutex>

namespace prv::ui::helixtables
{
juce::File folder()
{
    const auto p = prefs().getValue ("helixTableFolder");
    return p.isEmpty() ? juce::File() : juce::File (p);
}

void setFolder (const juce::File& f)
{
    prefs().setValue ("helixTableFolder", f.getFullPathName());
    prefs().saveIfNeeded();
    library (true);
}

namespace
{
    bool isWav (const juce::File& f) { return f.hasFileExtension ("wav"); }
    // a folder holding only single-cycle WAVs (no sub-folders, none longer than a cycle) is one table of its frames,
    // as HELIX itself reads it; any other WAV is a table of its own (a Serum-style table, or one cycle)
    void walk (const juce::File& root, const juce::File& dir, int depth, std::vector<Entry>& out)
    {
        const auto items = dir.findChildFiles (juce::File::findFilesAndDirectories | juce::File::ignoreHiddenFiles, false);
        bool hasWav = false, hasDir = false, hasTable = false;
        for (const auto& c : items)
        {
            if (c.isDirectory()) hasDir = true;
            else if (isWav (c)) { hasWav = true; if (c.getSize() > 32768 + 1024) hasTable = true; }
        }
        const juce::String rel = dir == root ? juce::String() : dir.getRelativePathFrom (root).replaceCharacter ('\\', '/').toUpperCase();
        if (dir != root && hasWav && ! hasDir && ! hasTable)
        {
            out.push_back ({ dir.getFileName().toUpperCase() + " (CYCLES)", rel.upToLastOccurrenceOf ("/", false, false), "dir:" + dir.getFullPathName() });
            return;
        }
        juce::Array<juce::File> files, dirs;
        for (const auto& c : items) (c.isDirectory() ? dirs : files).add (c);
        files.sort(); dirs.sort();
        for (const auto& c : files)
            if (isWav (c)) out.push_back ({ c.getFileNameWithoutExtension().toUpperCase(), rel, "path:" + c.getFullPathName() });
        if (depth < 6)
            for (const auto& d : dirs) walk (root, d, depth + 1, out);
    }

    struct Cache
    {
        std::mutex lock;
        std::map<juce::String, std::shared_ptr<const Preview>> made;
        std::map<juce::String, bool> making;
        bool fresh = false;
        juce::ThreadPool pool { 1 };
    };
    Cache& cache() { static Cache c; return c; }
}

const std::vector<Entry>& library (bool rescan)
{
    static std::vector<Entry> lib;
    static juce::String scanned;
    static bool done = false;
    const auto dir = folder();
    if (rescan || ! done || scanned != dir.getFullPathName())
    {
        done = true;
        scanned = dir.getFullPathName();
        lib.clear();
        if (dir.isDirectory()) walk (dir, dir, 0, lib);
    }
    return lib;
}

juce::StringArray groups()
{
    juce::StringArray g;
    for (const auto& e : library()) if (! g.contains (e.group)) g.add (e.group);
    return g;
}

juce::String nameOf (const juce::String& id)
{
    if (id.startsWith ("builtin:")) return id.fromFirstOccurrenceOf (":", false, false).toUpperCase();
    if (id.startsWith ("path:")) return juce::File (id.fromFirstOccurrenceOf (":", false, false)).getFileNameWithoutExtension().toUpperCase();
    if (id.startsWith ("dir:")) return juce::File (id.fromFirstOccurrenceOf (":", false, false)).getFileName().toUpperCase();
    return id.toUpperCase();
}

std::shared_ptr<const Preview> preview (const juce::String& id)
{
    auto& c = cache();
    {
        std::lock_guard<std::mutex> lk (c.lock);
        if (auto it = c.made.find (id); it != c.made.end()) return it->second;
        if (c.making[id]) return nullptr;
        c.making[id] = true;
    }
    c.pool.addJob ([id]
    {
        const auto p = helixTablePreview (id.toStdString(), 24, 256);
        auto pv = std::make_shared<Preview>();
        pv->totalFrames = p.totalFrames;
        pv->frames = p.frames;
        auto& cc = cache();
        std::lock_guard<std::mutex> lk (cc.lock);
        cc.made[id] = pv;
        cc.making.erase (id);
        cc.fresh = true;
    });
    return nullptr;
}

bool newPreviews()
{
    auto& c = cache();
    std::lock_guard<std::mutex> lk (c.lock);
    const bool f = c.fresh;
    c.fresh = false;
    return f;
}
} // namespace prv::ui::helixtables
