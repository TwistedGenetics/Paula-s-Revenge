// Paula's Revenge - hosting VST3 / AU effects in the mixer's insert slots
#include "PluginHost.h"
#include "Prefs.h"

namespace prv
{
// =========================================================================== PluginEffect
PluginEffect::PluginEffect (PluginHost& h, std::unique_ptr<juce::AudioPluginInstance> p, HostPlayHead* ph)
    : host (h), plugin (std::move (p)), playHead (ph)
{
    // stereo in and out where the plug-in allows it
    plugin->enableAllBuses();
    for (bool input : { true, false })
        if (auto* bus = plugin->getBus (input, 0))
            if (bus->getCurrentLayout() != juce::AudioChannelSet::stereo())
                bus->setCurrentLayout (juce::AudioChannelSet::stereo());
    plugin->setPlayHead (playHead);
}

PluginEffect::~PluginEffect()
{
    host.closeEditor (*this);   // the window goes before the plug-in
    plugin->releaseResources();
}

void PluginEffect::prepare (double sampleRate)
{
    rate = sampleRate;
    blockSize = host.maxBlock;
    plugin->setRateAndBufferSizeDetails (sampleRate, blockSize);
    plugin->prepareToPlay (sampleRate, blockSize);
    const int ch = std::max ({ 2, plugin->getTotalNumInputChannels(), plugin->getTotalNumOutputChannels() });
    buffer.setSize (ch, blockSize, false, true, false);
    midi.ensureSize (256);
}

void PluginEffect::reset() { plugin->reset(); }

void PluginEffect::process (float* left, float* right, int numSamples, const float*, double bpm)
{
    if (playHead != nullptr) playHead->bpm = bpm;
    const int ins = plugin->getTotalNumInputChannels(), outs = plugin->getTotalNumOutputChannels();
    for (int done = 0; done < numSamples;)
    {
        const int n = std::min (numSamples - done, buffer.getNumSamples());
        buffer.clear();
        if (ins >= 2) { buffer.copyFrom (0, 0, left + done, n); buffer.copyFrom (1, 0, right + done, n); }
        else if (ins == 1)
        {
            auto* d = buffer.getWritePointer (0);
            for (int i = 0; i < n; ++i) d[i] = 0.5f * (left[done + i] + right[done + i]);
        }
        juce::AudioBuffer<float> view (buffer.getArrayOfWritePointers(), buffer.getNumChannels(), n);
        midi.clear();
        plugin->processBlock (view, midi);
        if (outs >= 2) { std::copy_n (view.getReadPointer (0), n, left + done); std::copy_n (view.getReadPointer (1), n, right + done); }
        else if (outs == 1) { std::copy_n (view.getReadPointer (0), n, left + done); std::copy_n (view.getReadPointer (0), n, right + done); }
        done += n;
    }
}

std::vector<uint8_t> PluginEffect::state()
{
    juce::MemoryBlock mb;
    plugin->getStateInformation (mb);
    const auto* p = static_cast<const uint8_t*> (mb.getData());
    return std::vector<uint8_t> (p, p + mb.getSize());
}

// =========================================================================== scanning
// Plug-ins are looked at in a separate copy of the app ("--prv-scan"), one file at a
// time, so a plug-in that crashes or hangs while being looked at can't take the app
// down; it's just listed as failed. (Some plug-ins also aren't safe to load and
// unload on a background thread, which is what an in-app scan would do.)
namespace
{
    void addFormats (juce::AudioPluginFormatManager& fm)
    {
       #if JUCE_PLUGINHOST_VST3
        fm.addFormat (new juce::VST3PluginFormat());
       #endif
       #if JUCE_PLUGINHOST_AU && JUCE_MAC
        fm.addFormat (new juce::AudioUnitPluginFormat());
       #endif
    }
}

bool PluginHost::isScanChild (const juce::StringArray& args) { return args.contains ("--prv-scan"); }

void PluginHost::runScanChild (const juce::StringArray& args)
{
    const int i = args.indexOf ("--prv-scan");
    if (i < 0 || args.size() < i + 4) return;
   #if JUCE_MAC
    juce::Process::setDockIconVisible (false);
   #endif
    juce::AudioPluginFormatManager fm;
    addFormats (fm);
    juce::XmlElement root ("PRVSCAN");
    for (auto* f : fm.getFormats())
    {
        if (f->getName() != args[i + 1]) continue;
        juce::OwnedArray<juce::PluginDescription> found;
        f->findAllTypesForFile (found, args[i + 2]);
        for (auto* d : found) root.addChildElement (d->createXml().release());
    }
    root.writeTo (juce::File (args[i + 3]));
}

class ScanThread : public juce::Thread
{
public:
    explicit ScanThread (PluginHost& h) : juce::Thread ("Plug-in scan"), host (h) {}

    void setStatus (const juce::String& s) { const juce::ScopedLock sl (host.statusLock); host.status = s; }

    // looks at one plug-in file in a child copy of the app; false = it crashed, hung or failed
    bool scanInChild (juce::AudioPluginFormat& format, const juce::String& file)
    {
        const auto exe = juce::File::getSpecialLocation (juce::File::currentExecutableFile);
        const auto out = juce::File::getSpecialLocation (juce::File::tempDirectory).getNonexistentChildFile ("prv-scan", ".xml");
        juce::ChildProcess child;
        if (! child.start (juce::StringArray { exe.getFullPathName(), "--prv-scan", format.getName(), file, out.getFullPathName() }, 0))
        {
            DBG ("plug-in scan: couldn't start " << exe.getFullPathName());
            return false;
        }
        // most plug-ins take well under a second; give slow ones (big libraries) a minute
        for (int waited = 0; child.isRunning() && waited < 60000; waited += 50)
        {
            if (threadShouldExit()) { child.kill(); out.deleteFile(); return true; }
            juce::Thread::sleep (50);
        }
        if (child.isRunning()) { DBG ("plug-in scan: timed out on " << file); child.kill(); out.deleteFile(); return false; }
        auto xml = juce::parseXML (out);
        out.deleteFile();
        if (xml == nullptr) { DBG ("plug-in scan: no result for " << file << " (exit code " << (int) child.getExitCode() << ")"); return false; }
        for (auto* e : xml->getChildIterator())
        {
            juce::PluginDescription d;
            if (d.loadFromXml (*e)) host.known.addType (d);
        }
        return true;
    }

    void run() override
    {
        for (auto* format : host.formats.getFormats())
        {
            if (threadShouldExit()) break;
            if (format->isTrivialToScan())
            {
                // nothing to load (e.g. built-in formats): scan here
                juce::PluginDirectoryScanner scanner (host.known, *format, format->getDefaultLocationsToSearch(), true, juce::File(), false);
                juce::String name;
                while (! threadShouldExit() && scanner.scanNextFile (true, name)) {}
                continue;
            }
            const auto files = format->searchPathsForPlugins (format->getDefaultLocationsToSearch(), true, true);
            const auto blacklisted = host.known.getBlacklistedFiles();
            int n = 0;
            for (const auto& file : files)
            {
                ++n;
                if (threadShouldExit()) break;
                if (blacklisted.contains (file) || host.known.isListingUpToDate (file, *format)) continue;
                setStatus ("SCANNING " + format->getName().toUpperCase() + " " + juce::String (n) + " OF " + juce::String (files.size()) + ": "
                           + juce::File::createFileWithoutCheckingPath (file).getFileNameWithoutExtension().toUpperCase());
                if (! scanInChild (*format, file))
                {
                    host.known.addToBlacklist (file);   // skipped from now on (RESCAN tries again)
                    const juce::ScopedLock sl (host.statusLock);
                    host.failed.add (file);
                }
            }
        }
        host.scanning = false;
        juce::MessageManager::callAsync ([h = &host]
        {
            h->saveList();
            const int bad = h->failedFiles().size();
            { const juce::ScopedLock sl (h->statusLock);
              h->status = juce::String ((int) h->effects().size()) + " EFFECT PLUG-INS FOUND"
                        + (bad > 0 ? ", " + juce::String (bad) + " SKIPPED (CRASHED OR DIDN'T LOAD)" : juce::String()); }
            if (h->onScanFinished) h->onScanFinished();
        });
    }
private:
    PluginHost& host;
};

PluginHost::PluginHost()
{
    addFormats (formats);
    if (auto xml = juce::parseXML (prefs().getValue ("knownPlugins")))
        known.recreateFromXml (*xml);
}

PluginHost::~PluginHost()
{
    if (scanThread != nullptr) scanThread->stopThread (4000);
    closeAllEditors();
}

void PluginHost::saveList()
{
    if (auto xml = known.createXml())
    {
        prefs().setValue ("knownPlugins", xml->toString (juce::XmlElement::TextFormat().singleLine()));
        prefs().saveIfNeeded();
    }
}

void PluginHost::startScan()
{
    if (scanning) return;
    if (scanThread != nullptr) scanThread->stopThread (4000);
    { const juce::ScopedLock sl (statusLock); status = "SCANNING..."; failed.clear(); }
    known.clearBlacklistedFiles();
    scanning = true;
    scanThread = std::make_unique<ScanThread> (*this);
    scanThread->startThread();
}

juce::String PluginHost::scanStatus() const { const juce::ScopedLock sl (statusLock); return status; }
juce::StringArray PluginHost::failedFiles() const { const juce::ScopedLock sl (statusLock); return failed; }

std::vector<juce::PluginDescription> PluginHost::effects() const
{
    std::vector<juce::PluginDescription> out;
    for (const auto& d : known.getTypes())
        if (! d.isInstrument) out.push_back (d);
    std::sort (out.begin(), out.end(), [] (const auto& a, const auto& b)
    {
        const int c = a.name.compareIgnoreCase (b.name);
        return c != 0 ? c < 0 : a.pluginFormatName < b.pluginFormatName;
    });
    return out;
}

bool PluginHost::findDescription (const std::string& key, juce::PluginDescription& out) const
{
    if (auto d = known.getTypeForIdentifierString (juce::String (key)))
    {
        out = *d;
        return true;
    }
    return false;
}

juce::String PluginHost::lastError (const std::string& key) const
{
    auto it = errors.find (key);
    return it == errors.end() ? juce::String() : it->second;
}

std::unique_ptr<Effect> PluginHost::wrap (std::unique_ptr<juce::AudioPluginInstance> inst, const FxSlot& slot, double sampleRate)
{
    auto fx = std::make_unique<PluginEffect> (*this, std::move (inst), &playHead);
    fx->prepare (sampleRate);
    if (! slot.pluginState.empty())
        fx->instance().setStateInformation (slot.pluginState.data(), (int) slot.pluginState.size());
    return fx;
}

std::unique_ptr<Effect> PluginHost::create (int slotIndex, const FxSlot& slot, double sampleRate)
{
    const auto& key = slot.pluginKey;
    // finished loading in the background?
    if (slotIndex >= 0)
    {
        auto it = asyncReady.find (slotIndex);
        if (it != asyncReady.end())
        {
            auto inst = std::move (it->second);
            asyncReady.erase (it);
            if (inst != nullptr && keyFor (inst->getPluginDescription()) == key) return wrap (std::move (inst), slot, sampleRate);
        }
    }
    juce::PluginDescription desc;
    if (! findDescription (key, desc)) { errors[key] = "NOT INSTALLED ON THIS COMPUTER"; return nullptr; }
    juce::String err;
    auto inst = formats.createPluginInstance (desc, sampleRate, maxBlock, err);
    if (inst != nullptr) { errors.erase (key); return wrap (std::move (inst), slot, sampleRate); }

    // some plug-ins (e.g. AUv3) only load in the background: start that, report when ready
    if (slotIndex >= 0 && juce::MessageManager::getInstance()->isThisTheMessageThread())
    {
        if (asyncPending.count (slotIndex) == 0 || asyncPending[slotIndex] != key)
        {
            asyncPending[slotIndex] = key;
            errors[key] = "LOADING...";
            formats.createPluginInstanceAsync (desc, sampleRate, maxBlock,
                [this, slotIndex, key] (std::unique_ptr<juce::AudioPluginInstance> p, const juce::String& e)
                {
                    asyncPending.erase (slotIndex);
                    if (p == nullptr) { errors[key] = e.isNotEmpty() ? e.toUpperCase() : juce::String ("COULDN'T LOAD"); return; }
                    errors.erase (key);
                    asyncReady[slotIndex] = std::move (p);
                    if (onAsyncReady) onAsyncReady (slotIndex);
                });
        }
        return nullptr;
    }
    errors[key] = err.isNotEmpty() ? err.toUpperCase() : juce::String ("COULDN'T LOAD");
    return nullptr;
}

// =========================================================================== editor windows
struct PluginHost::Window : public juce::DocumentWindow
{
    Window (PluginHost& h, PluginEffect& e, const juce::String& title, juce::AudioProcessorEditor* ed)
        : juce::DocumentWindow (title, juce::Colours::black, juce::DocumentWindow::closeButton | juce::DocumentWindow::minimiseButton), host (h), fx (e)
    {
        setUsingNativeTitleBar (true);
        setContentOwned (ed, true);
        setResizable (ed->isResizable(), false);
        centreWithSize (getWidth(), getHeight());
        setVisible (true);
    }
    void closeButtonPressed() override { juce::MessageManager::callAsync ([h = &host, f = &fx] { h->closeEditor (*f); }); }
    PluginHost& host;
    PluginEffect& fx;
};

void PluginHost::openEditor (PluginEffect& fx, const juce::String& title)
{
    auto it = windows.find (&fx);
    if (it != windows.end()) { it->second->toFront (true); return; }
    auto& inst = fx.instance();
    juce::AudioProcessorEditor* ed = inst.hasEditor() ? inst.createEditorIfNeeded() : nullptr;
    if (ed == nullptr) ed = new juce::GenericAudioProcessorEditor (inst);
    windows[&fx] = std::make_unique<Window> (*this, fx, title, ed);
}

void PluginHost::closeEditor (PluginEffect& fx)
{
    auto it = windows.find (&fx);
    if (it == windows.end()) return;
    auto w = std::move (it->second);
    windows.erase (it);
    w->clearContentComponent();   // deletes the editor while the plug-in is still alive
}

bool PluginHost::isEditorOpen (PluginEffect& fx) const { return windows.count (&fx) > 0; }

void PluginHost::closeAllEditors()
{
    for (auto& kv : windows) kv.second->clearContentComponent();
    windows.clear();
}
} // namespace prv
