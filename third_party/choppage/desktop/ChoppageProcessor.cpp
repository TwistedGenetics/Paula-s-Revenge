#include "ChoppageProcessor.h"
#include "choppage/Preset.h"
#include "ChoppageEditor.h"

using namespace bc;

namespace
{
    /** JUCE parameters from the core table: same ids, same ranges, same value text as the MPC. */
    juce::AudioProcessorValueTreeState::ParameterLayout makeLayout()
    {
        juce::AudioProcessorValueTreeState::ParameterLayout l;
        const auto& defs = paramDefs();
        for (int i = 0; i < (int) defs.size(); ++i)
        {
            const auto& d = defs[(size_t) i];
            const juce::ParameterID pid { d.id, 1 };
            if (d.kind == PKind::Bool)
                l.add (std::make_unique<juce::AudioParameterBool> (pid, d.name, d.def > 0.5f));
            else if (d.kind == PKind::Choice)
            {
                juce::StringArray ch;
                for (auto& c : d.choices) ch.add (c);
                l.add (std::make_unique<juce::AudioParameterChoice> (pid, d.name, ch, (int) d.def));
            }
            else
            {
                juce::NormalisableRange<float> r (d.min, d.max,
                    [i] (float, float, float n) { return paramFromNorm (i, n); },
                    [i] (float, float, float v) { return paramToNorm (i, v); });
                l.add (std::make_unique<juce::AudioParameterFloat> (pid, d.name, r, d.def,
                    juce::AudioParameterFloatAttributes().withStringFromValueFunction ([i] (float v, int) { return juce::String (formatParam (i, v)); })));
            }
        }
        return l;
    }
}

ChoppageProcessor::ChoppageProcessor()
    : ChoppageBase (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, &undo, "PARAMS", makeLayout())
{
    formats.registerBasicFormats();
    for (const auto& d : paramDefs()) values.push_back (apvts.getRawParameterValue (d.id));
    midiScratch.reserve (2048);

    // JUCE decodes everything it knows (WAV, AIFF, FLAC, Ogg, MP3 on macOS) for the core
    sess.setDecoder ([this] (const std::string& path, AudioFile::Status& st) -> std::shared_ptr<SampleData>
    {
        const juce::File f (path);
        if (! f.existsAsFile()) { st = AudioFile::Status::missing; return nullptr; }
        if (AudioFile::isRexFile (path)) return AudioFile::loadRex (path, st);   // REX2: the core's decoder, with ReCycle's slices
        std::unique_ptr<juce::AudioFormatReader> r (formats.createReaderFor (f));
        if (r == nullptr || r->sampleRate <= 0 || r->numChannels == 0) { st = AudioFile::Status::unreadable; return nullptr; }
        if ((double) r->lengthInSamples > 300.0 * r->sampleRate) { st = AudioFile::Status::tooLong; return nullptr; }
        const int len = (int) r->lengthInSamples;
        if (len <= 0) { st = AudioFile::Status::empty; return nullptr; }
        auto s = std::make_shared<SampleData>();
        const int chans = r->numChannels == 1 ? 1 : 2;
        s->allocate (chans, len);
        s->sampleRate = r->sampleRate;
        s->name = f.getFileNameWithoutExtension().toStdString();
        s->path = path;
        juce::AudioBuffer<float> tmp (chans, len);
        if (! r->read (&tmp, 0, len, 0, true, chans > 1)) { st = AudioFile::Status::unreadable; return nullptr; }
        for (int c = 0; c < chans; ++c) std::copy (tmp.getReadPointer (c), tmp.getReadPointer (c) + len, s->ch[c].begin());
        s->buildOverview();
        st = AudioFile::Status::ok;
        return s;
    });

    const auto docs = juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("CHOPPAGE");
    sess.setDataFolder (docs.getFullPathName().toStdString());
    browserFolder = docs.getChildFile ("Breaks");
    loadPlaces();
    startTimerHz (30);
}

ChoppageProcessor::~ChoppageProcessor() { stopTimer(); }

bool ChoppageProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto out = l.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono();
}

void ChoppageProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;
    sess.prepare (sampleRate, samplesPerBlock);
}

Engine::Settings ChoppageProcessor::readSettings() const
{
    float v[kNumCoreParams];
    for (int i = 0; i < kNumCoreParams; ++i) v[i] = values[(size_t) i]->load();
    return settingsFromParams (v);
}

void ChoppageProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    Engine::Host host;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            host.hasInfo = pos->getBpm().hasValue();
            host.bpm = pos->getBpm().orFallback (120.0);
            host.playing = pos->getIsPlaying();
            host.ppq = pos->getPpqPosition().orFallback (0.0);
        }

    auto s = readSettings();
    for (int p = 0; p < kFxPads; ++p)                     // pads automated as host parameters: press and release,
    {                                                     // on change only (the panel's pads hold on their own)
        const bool down = values[(size_t) (P_PAD1 + p)]->load() > 0.5f;
        if (down != padParamDown[(size_t) p]) { padParamDown[(size_t) p] = down; sess.setPadHeld (p, down); }
    }

    const auto& sel = sess.slot (sess.selectedSlot()).content;
    const double bpm = host.hasInfo ? host.bpm : (sel != nullptr ? sel->bpm : 120.0);
    s.sound.echoSamples = echoBeats ((int) values[P_ECHO_TIME]->load()) * currentSampleRate * 60.0 / std::max (20.0, bpm);

    midiScratch.clear();
    for (const auto meta : midi)
    {
        if (midiScratch.size() >= midiScratch.capacity()) break;
        const auto* d = meta.data;
        MidiMsg m;
        m.offset = meta.samplePosition;
        for (int k = 0; k < std::min (3, meta.numBytes); ++k) m.data[k] = d[k];
        midiScratch.push_back (m);
    }

    float* l = buffer.getWritePointer (0);
    float* r = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : l;
    sess.process (l, r, buffer.getNumSamples(), midiScratch.data(), (int) midiScratch.size(), host, s);
    for (int ch = 2; ch < buffer.getNumChannels(); ++ch) buffer.clear (ch, 0, buffer.getNumSamples());
    hostBpm = host.hasInfo ? host.bpm : 0.0;
    hostPlaying = host.playing;
    midi.clear();
}

// ---- dice, renders, browser --------------------------------------------------------------------
void ChoppageProcessor::rollPattern()
{
    sess.rollPattern ((Style) (int) plain (P_STYLE), plain (P_PAT_AMOUNT), plain (P_LAYERS) > 0.5f);
}

void ChoppageProcessor::rollFx() { sess.rollFx (plain (P_FX_AMOUNT)); }

juce::File ChoppageProcessor::bounceLoop()
{
    const auto& s = sess.slot (sess.selectedSlot());
    if (s.content == nullptr) return {};
    const double bpm = hostBpm.load() > 1.0 ? hostBpm.load() : s.content->bpm;
    auto settings = readSettings();
    settings.sound.echoSamples = echoBeats ((int) plain (P_ECHO_TIME)) * currentSampleRate * 60.0 / bpm;
    auto out = sess.bounce (settings, bpm, currentSampleRate > 0 ? currentSampleRate : 44100.0);
    if (out == nullptr) return {};
    auto name = juce::String (s.content->name).retainCharacters ("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789 _-").trim();
    auto f = juce::File::getSpecialLocation (juce::File::tempDirectory)
                 .getChildFile ("CHOPPAGE " + name + " " + juce::String (juce::roundToInt (bpm)) + "bpm.wav").getNonexistentSibling();
    const float* ch[2] = { out->ch[0].data(), out->ch[1].data() };
    return AudioFile::writeWav24 (f.getFullPathName().toStdString(), ch, 2, out->numSamples, out->sampleRate) ? f : juce::File();
}

bool ChoppageProcessor::akaizeSelected()
{
    const int slot = sess.selectedSlot();
    const auto& s = sess.slot (slot);
    if (s.content == nullptr) return false;
    // the AKAI tab's settings: SYNC (LOOP BPM -> the host's tempo, or TARGET BPM without one) or STRETCH %
    bc::Session::AkaizeRender r;
    r.sync = plain (P_AKR_SYNC) > 0.5f;
    r.targetBpm = hostBpm.load() > 1.0 ? hostBpm.load() : (double) plain (P_AKR_TARGET);
    r.stretchPercent = plain (P_AKR_STRETCH);
    r.cycleLength = juce::jlimit (20, 2000, (int) std::lround (plain (P_AKR_CYC_LEN)));
    r.cycleFreq = bc::akaizeFreq (plain (P_AKR_CYC_LEN), plain (P_AKR_CYC_FREQ), plain (P_AKR_FREQ_MAN) > 0.5f);
    r.pitch = std::round (plain (P_AKR_PITCH));
    r.inTime = plain (P_AK_MODE) > 0.5f;            // AKAIZE MODE IN TIME: the length (the beat) kept
    return sess.akaizeSlot (slot, r);
}

std::vector<float> ChoppageProcessor::undoParams() const
{
    std::vector<float> v ((size_t) kNumCoreParams);
    for (int p = 0; p < kNumCoreParams; ++p)
        v[(size_t) p] = (p >= P_PAD1 && p < P_PAD1 + kFxPads) || p == P_FILL ? 0.0f : values[(size_t) p]->load();
    return v;
}

void ChoppageProcessor::applyUndoParams (const std::vector<float>& v)
{
    if ((int) v.size() < kNumCoreParams) return;
    for (int p = 0; p < kNumCoreParams; ++p)
    {
        if ((p >= P_PAD1 && p < P_PAD1 + kFxPads) || p == P_FILL) continue;
        if (std::abs (values[(size_t) p]->load() - v[(size_t) p]) < 1.0e-7f) continue;
        if (auto* prm = apvts.getParameter (paramDefs()[(size_t) p].id))
            prm->setValueNotifyingHost (prm->convertTo0to1 (v[(size_t) p]));
    }
}

void ChoppageProcessor::keepLinkedFrequencies()
{
    // With MANUAL off, CYCLE FREQ follows CYCLE LENGTH (41000 / length) as in the original, so its value shows it
    const int sets[3][3] = { { P_AK_CYC_LEN, P_AK_CYC_FREQ, P_AK_FREQ_MAN }, { P_SL_CYC_LEN, P_SL_CYC_FREQ, P_SL_FREQ_MAN },
                             { P_AKR_CYC_LEN, P_AKR_CYC_FREQ, P_AKR_FREQ_MAN } };
    for (auto& t : sets)
    {
        if (plain (t[2]) > 0.5f) continue;
        const int linked = bc::akaizeFreq (plain (t[0]), plain (t[1]), false);
        if ((int) std::lround (plain (t[1])) != linked)
            if (auto* prm = apvts.getParameter (bc::paramDefs()[(size_t) t[1]].id))
                prm->setValueNotifyingHost (prm->convertTo0to1 ((float) linked));
    }
}

void ChoppageProcessor::scanBrowser (const juce::File& folder)
{
    browserFolder = folder;
    folder.createDirectory();
    sess.scanFolder (folder.getFullPathName().toStdString());
}

void ChoppageProcessor::browseTo (const juce::File& folder)
{
    if (! folder.isDirectory() && folder.isAChildOf (juce::File (juce::String (sess.getDataFolder())))) folder.createDirectory();   // (its own Breaks)
    if (! folder.isDirectory()) return;
    browserFolder = folder;
    sess.listFolder (folder.getFullPathName().toStdString(), "");
    savePlaces();   // (the last folder too: DISK opens there next time)
}

juce::File ChoppageProcessor::placesFile() const
{
    return juce::File (juce::String (sess.getDataFolder())).getChildFile ("places.txt");
}

void ChoppageProcessor::loadPlaces()
{
    juce::StringArray lines;
    placesFile().readLines (lines);
    for (auto& line : lines)
    {
        for (int k = 0; k < kPlaces; ++k)
            if (line.startsWith ("place" + juce::String (k + 1) + " ")) places[(size_t) k] = juce::File (line.fromFirstOccurrenceOf (" ", false, false));
        if (line.startsWith ("last ")) { const juce::File f (line.fromFirstOccurrenceOf (" ", false, false)); if (f.isDirectory()) browserFolder = f; }
    }
    if (places[0] == juce::File()) places[0] = juce::File (juce::String (sess.getDataFolder())).getChildFile ("Breaks");   // PLACE 1: the breaks folder
}

void ChoppageProcessor::savePlaces()
{
    juce::String text;
    for (int k = 0; k < kPlaces; ++k)
        if (places[(size_t) k] != juce::File()) text << "place" << (k + 1) << " " << places[(size_t) k].getFullPathName() << "\n";
    if (browserFolder != juce::File()) text << "last " << browserFolder.getFullPathName() << "\n";
    placesFile().getParentDirectory().createDirectory();
    placesFile().replaceWithText (text);
}

void ChoppageProcessor::storePlace (int k)
{
    if (k < 0 || k >= kPlaces) return;
    places[(size_t) k] = browserFolder;
    savePlaces();
}

void ChoppageProcessor::goToPlace (int k)
{
    const auto f = getPlace (k);
    if (f.isDirectory()) browseTo (f);
    else if (f != juce::File()) { f.createDirectory(); browseTo (f); }
}

// ---- state ------------------------------------------------------------------------------------------
void ChoppageProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    juce::ValueTree root ("CHOPPAGE");
    #ifdef PRV_EMBED_CHOPPAGE
    root.setProperty ("version", PRV_CHOPPAGE_VERSION, nullptr);
#else
    root.setProperty ("version", JucePlugin_VersionString, nullptr);
#endif
    root.setProperty ("browser", browserFolder.getFullPathName(), nullptr);
    root.appendChild (apvts.copyState(), nullptr);
    juce::ValueTree s ("SESSION");
    s.setProperty ("text", juce::String (sess.saveState()), nullptr);
    root.appendChild (s, nullptr);
    if (auto xml = root.createXml()) copyXmlToBinary (*xml, dest);
}

void ChoppageProcessor::setStateInformation (const void* data, int size)
{
    auto xml = getXmlFromBinary (data, size);
    if (xml == nullptr) return;
    const auto root = juce::ValueTree::fromXml (*xml);
    if (! root.hasType ("CHOPPAGE")) return;
    const auto p = root.getChildWithName (apvts.state.getType());
    if (p.isValid()) apvts.replaceState (p);
    if (root.hasProperty ("browser")) browserFolder = juce::File (root.getProperty ("browser").toString());
    const auto s = root.getChildWithName ("SESSION");
    if (s.isValid()) sess.loadState (s.getProperty ("text").toString().toStdString());
}

juce::AudioProcessorEditor* ChoppageProcessor::createEditor() { return new ChoppageEditor (*this); }

#ifndef PRV_EMBED_CHOPPAGE
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new ChoppageProcessor(); }
#endif

// ===================================================================================== presets
juce::File ChoppageProcessor::presetsFolder() const
{
    auto f = juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("CHOPPAGE").getChildFile ("Presets");
    f.createDirectory();
    return f;
}

bool ChoppageProcessor::savePreset (const juce::File& file)
{
    std::vector<float> p ((size_t) bc::kNumCoreParams);
    for (int i = 0; i < bc::kNumCoreParams; ++i) p[(size_t) i] = values[(size_t) i]->load();
    const auto f = file.hasFileExtension ("chp") ? file : file.withFileExtension ("chp");
    return bc::Preset::writeFile (f.getFullPathName().toStdString(),
                                  bc::Preset::make (f.getFileNameWithoutExtension().toStdString(), p.data(), sess.saveState()));
}

bool ChoppageProcessor::loadPreset (const juce::File& file)
{
    std::string text;
    bc::Preset::Parsed pr;
    if (! bc::Preset::readFile (file.getFullPathName().toStdString(), text) || ! bc::Preset::parse (text, pr)) return false;
    for (auto& [id, v] : pr.params)
        if (auto* prm = apvts.getParameter (id))
            prm->setValueNotifyingHost (prm->convertTo0to1 (v));
    sess.loadState (pr.session);
    return true;
}
