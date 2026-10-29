// Paula's Revenge - one undo/redo history for every page
#include "History.h"
#include "PluginProcessor.h"

History::Entry History::capture (Kind kind, int index) const
{
    const auto& song = proc.song;
    const auto& st = proc.editorState;
    Entry e;
    e.kind = kind;
    e.index = index;
    e.time = juce::Time::getMillisecondCounterHiRes() * 0.001;
    e.seqPos = st.seqPos; e.blockSel = st.block; e.instrument = st.instrument;
    switch (kind)
    {
        case Kind::Block:
            e.index = song.safeBlock (index);
            e.block = std::make_shared<prv::Block> (song.blocks[(size_t) e.index]);
            e.bytes = sizeof (prv::Cell) * prv::kMaxTracks * (size_t) prv::kMaxLines;
            break;
        case Kind::Structure:
            e.structure = std::make_shared<Structure> (Structure { song.blocks, song.sequence, song.sections, song.chords, song.chordsLegacy });   // (chords: converting old ones changes both)
            e.bytes = sizeof (prv::Cell) * prv::kMaxTracks * (size_t) prv::kMaxLines * song.blocks.size();
            break;
        case Kind::Instrument:
            e.index = juce::jlimit (0, prv::kMaxInstruments - 1, index);
            e.inst = std::make_shared<prv::Instrument> (song.instruments[(size_t) e.index]);
            e.instFx = std::make_shared<std::array<prv::FxSlot, prv::kInserts>>();
            for (int k = 0; k < prv::kInserts; ++k) (*e.instFx)[(size_t) k] = song.mixer.inst[e.index][k];
            e.bytes = (e.inst->data.size() + e.inst->dataR.size()) * sizeof (int16_t) + 256;
            break;
        case Kind::Mixer:
            e.mix = std::make_shared<prv::MixerSettings> (song.mixer);
            e.bytes = sizeof (prv::MixerSettings);
            break;
        case Kind::Settings:
        {
            auto s = std::make_shared<Settings>();
            s->name = song.name; s->numTracks = song.numTracks; s->tempo = song.tempo; s->speed = song.speed;
            s->led = song.ledFilterAtStart;
            std::copy (std::begin (song.trackPan), std::end (song.trackPan), s->trackPan);
            s->chords = song.chords; s->chordsLegacy = song.chordsLegacy;
            std::copy (std::begin (song.cvSources), std::end (song.cvSources), s->cvSources);
            s->cvFullScale = song.cvFullScale; s->cvDelayMs = song.cvDelayMs;
            s->grooveKind = song.grooveKind; s->grooveAmount = song.grooveAmount;
            e.settings = s;
            e.bytes = sizeof (Settings);
            break;
        }
    }
    return e;
}

void History::record (Kind kind, int index, const juce::String& label, bool merge)
{
    const double now = juce::Time::getMillisecondCounterHiRes() * 0.001;
    if (merge && redoStack.empty() && ! undoStack.empty())
    {
        auto& last = undoStack.back();
        const int idx = kind == Kind::Block ? proc.song.safeBlock (index) : index;
        if (last.kind == kind && last.index == idx && last.label == label && now - last.time < 1.0)
        {
            last.time = now;          // keep the state from before the first change
            current = nextId++;
            return;
        }
    }
    auto e = capture (kind, index);
    e.label = label.toUpperCase();
    e.state = current;
    current = nextId++;
    undoStack.push_back (std::move (e));
    redoStack.clear();
    trim();
}

void History::block (int b, const juce::String& label, bool merge)      { record (Kind::Block, b, label, merge); }
void History::structure (const juce::String& label)                      { record (Kind::Structure, 0, label, false); }
void History::instrument (int i, const juce::String& label, bool merge)  { record (Kind::Instrument, i, label, merge); }
void History::settings (const juce::String& label, bool merge)           { record (Kind::Settings, 0, label, merge); }
void History::mixer (const juce::String& label, bool merge)              { record (Kind::Mixer, 0, label, merge); }

void History::relabelLast (const juce::String& label)
{
    if (! undoStack.empty()) undoStack.back().label = label.toUpperCase();
}

void History::dropLast()
{
    if (undoStack.empty()) return;
    current = undoStack.back().state;
    undoStack.pop_back();
}

void History::trim()
{
    size_t total = 0;
    for (const auto& e : undoStack) total += e.bytes;
    for (const auto& e : redoStack) total += e.bytes;
    while (undoStack.size() > 1 && ((int) undoStack.size() > kMaxSteps || total > kMaxBytes))
    {
        total -= undoStack.front().bytes;
        undoStack.erase (undoStack.begin());
    }
}

void History::apply (Entry& e)
{
    auto& song = proc.song;
    auto& st = proc.editorState;
    // swap under the lock; the replaced data ends up in `e` and is freed here, off the audio thread
    switch (e.kind)
    {
        case Kind::Block:
            if (e.index >= (int) song.blocks.size()) return;
            proc.withLock ([&] { std::swap (song.blocks[(size_t) e.index], *e.block); });
            st.block = e.index;
            break;
        case Kind::Structure:
            proc.withLock ([&] {
                std::swap (song.blocks, e.structure->blocks);
                std::swap (song.sequence, e.structure->sequence);
                std::swap (song.sections, e.structure->sections);
                std::swap (song.chords, e.structure->chords);
                std::swap (song.chordsLegacy, e.structure->chordsLegacy);
                if (proc.player.isPlaying()) proc.player.stop();
            });
            st.seqPos = juce::jlimit (0, (int) song.sequence.size() - 1, e.seqPos);
            st.block = song.safeBlock (e.blockSel);
            break;
        case Kind::Instrument:
            proc.withLock ([&]
            {
                proc.player.jamAllOff();
                std::swap (song.instruments[(size_t) e.index], *e.inst);
                if (e.instFx) for (int k = 0; k < prv::kInserts; ++k) std::swap (song.mixer.inst[e.index][k], (*e.instFx)[(size_t) k]);
            });
            st.instrument = e.index;
            proc.updateMixerEffects();   // (a plug-in instrument may have come or gone)
            break;
        case Kind::Mixer:
            proc.withLock ([&] { std::swap (song.mixer, *e.mix); });
            proc.updateMixerEffects();
            break;
        case Kind::Settings:
        {
            auto& s = *e.settings;
            proc.withLock ([&] {
                song.tempo = s.tempo; song.speed = s.speed;   // (the name comes from the file; every song has 8 tracks)
                song.ledFilterAtStart = s.led;
                std::copy (std::begin (s.trackPan), std::end (s.trackPan), song.trackPan);
                song.chords = s.chords; song.chordsLegacy = s.chordsLegacy;
                std::copy (std::begin (s.cvSources), std::end (s.cvSources), song.cvSources);
                song.cvFullScale = s.cvFullScale; song.cvDelayMs = s.cvDelayMs;
                song.grooveKind = s.grooveKind; song.grooveAmount = s.grooveAmount;
                if (! proc.player.isPlaying()) proc.player.setLed (s.led);
            });
            break;
        }
    }
    st.block = song.safeBlock (st.block);
    st.line = juce::jlimit (0, song.blocks[(size_t) st.block].lines - 1, st.line);
    st.track = juce::jlimit (0, song.numTracks - 1, st.track);
}

bool History::step (std::vector<Entry>& from, std::vector<Entry>& to, const char* verb)
{
    if (from.empty()) return false;
    auto e = std::move (from.back());
    from.pop_back();
    // keep what's there now so the step can be reversed
    auto back = capture (e.kind, e.kind == Kind::Structure || e.kind == Kind::Settings || e.kind == Kind::Mixer ? 0 : e.index);
    back.label = e.label;
    back.state = current;
    apply (e);
    current = e.state;
    to.push_back (std::move (back));
    if (onRestored) onRestored (juce::String (verb) + ": " + e.label);
    return true;
}

bool History::undo() { return step (undoStack, redoStack, "UNDO"); }
bool History::redo() { return step (redoStack, undoStack, "REDO"); }

void History::clear()
{
    undoStack.clear();
    redoStack.clear();
    current = saved = nextId++;
}
