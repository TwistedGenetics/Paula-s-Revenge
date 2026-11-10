// Paula's Revenge - a built-in synth's page
#include "EnginePage.h"
#include "ChoiceBox.h"

namespace prv::ui
{
namespace
{
    juce::String hex2 (int v) { return juce::String::toHexString (v).toUpperCase().paddedLeft ('0', 2); }
    std::vector<juce::String> choiceList (const SynthParam& q)
    {
        std::vector<juce::String> out;
        for (auto s : juce::StringArray::fromTokens (q.choices, "|", "")) out.push_back (s);
        return out;
    }
}

EnginePage::EnginePage (PaulaProcessor& p) : proc (p)
{
    Theme::markDarkPage (*this);
    setWantsKeyboardFocus (true);
    setOpaque (true);
    playBtn.onStateChange = [this]
    {
        const bool down = playBtn.isDown();
        if (down == playingHeld) return;
        playingHeld = down;
        const int note = proc.editorState.octave * 12 + 1;
        proc.withLock ([&] { if (down) proc.player.jamNoteOn (note, proc.editorState.instrument); else proc.player.jamNoteOff (note); });
    };
    optionsBtn.onClick = [this] { if (onOptions) onOptions(); };
    closeBtn.onClick = [this] { if (onClose) onClose(); };
    for (auto* c : { &playBtn, &optionsBtn, &closeBtn }) addAndMakeVisible (c);
}

EnginePage::~EnginePage()
{
    proc.withLock ([&] { proc.player.jamAllOff(); });
}

Instrument& EnginePage::inst()
{
    return proc.song.instruments[(size_t) juce::jlimit (0, kMaxInstruments - 1, proc.editorState.instrument)];
}

float EnginePage::valueOf (int param) const
{
    const auto v = readBuiltinState (proc.song.instruments[(size_t) juce::jlimit (0, kMaxInstruments - 1, proc.editorState.instrument)]);
    return param >= 0 && param < (int) v.size() ? v[(size_t) param] : 0.0f;
}

void EnginePage::edit (int param, float value, bool merge)
{
    const auto& ps = builtinParams (inst().pluginKey);
    if (param < 0 || param >= (int) ps.size()) return;
    const auto& q = ps[(size_t) param];
    value = juce::jlimit (std::min (q.min, q.max), std::max (q.min, q.max), value);
    proc.history.instrument (proc.editorState.instrument, juce::String (inst().pluginName) + ": " + q.name, merge);
    proc.withLock ([&]
    {
        auto v = readBuiltinState (inst());
        v[(size_t) param] = value;
        writeBuiltinState (inst(), v);
    });
    if (onStatus) onStatus (juce::String (inst().pluginName) + " " + q.name + ": " + juce::String (q.text (value)));
    sync();
    if (onChanged) onChanged();
}

void EnginePage::setValue (int param, float value) { edit (param, value, false); }

juce::Component* EnginePage::controlFor (int param) const
{
    for (const auto& r : rows)
        if (r.param == param)
        {
            if (r.slider) return r.slider.get();
            if (r.number) return r.number.get();
            if (r.cycle) return r.cycle.get();
            if (! r.choices.empty()) return r.choices.front().get();
        }
    return nullptr;
}

void EnginePage::build()
{
    // one row per control, in boxes by section (in the synth's own order)
    for (auto& r : rows) { if (r.slider) removeChildComponent (r.slider.get()); if (r.number) removeChildComponent (r.number.get()); if (r.cycle) removeChildComponent (r.cycle.get()); for (auto& b : r.choices) removeChildComponent (b.get()); }
    rows.clear(); boxes.clear();
    shownKey = inst().pluginKey;
    const auto& ps = builtinParams (shownKey);
    for (int i = 0; i < (int) ps.size(); ++i)
    {
        const auto& q = ps[(size_t) i];
        Row r; r.param = i;
        if (q.kind == 3)
        {
            const auto names = choiceList (q);
            if (names.size() <= 4)
                for (int k = 0; k < (int) names.size(); ++k)
                {
                    auto b = std::make_unique<PixButton> (names[(size_t) k], true);
                    b->setClickingTogglesState (false);
                    b->onClick = [this, i, k] { edit (i, (float) k, false); };
                    b->onRightClick = [this, i] { knobMenu (i); };
                    addAndMakeVisible (*b);
                    r.choices.push_back (std::move (b));
                }
            else
            {
                r.cycle = std::make_unique<PixButton> ("");
                r.cycle->onClick = [this, i, names]
                {
                    juce::Component::SafePointer<EnginePage> safe (this);
                    std::vector<MenuItem> items;
                    for (int k = 0; k < (int) names.size(); ++k)
                        items.push_back ({ (std::lround (valueOf (i)) == k ? "> " : "  ") + names[(size_t) k], [safe, i, k] { if (safe) safe->edit (i, (float) k, false); } });
                    showMenu (builtinParams (shownKey)[(size_t) i].name, std::move (items));
                };
                r.cycle->onRightClick = [this, i] { knobMenu (i); };
                addAndMakeVisible (*r.cycle);
            }
        }
        else if (q.kind == 2)
        {
            r.number = std::make_unique<NumberBox> (q.name, (int) q.min, (int) q.max, (int) q.def);
            r.number->compact = true; r.number->labelChars = 9; r.number->valueChars = 3;
            r.number->format = [q] (int v) { return juce::String (q.text ((float) v)); };
            r.number->onChange = [this, i] (int v) { edit (i, (float) v, true); };
            addAndMakeVisible (*r.number);
        }
        else
        {
            r.slider = std::make_unique<PixSlider> (q.name);
            r.slider->labelChars = 9; r.slider->valueChars = 6.5f;
            r.slider->format = [q] (float v01) { return juce::String (q.text (q.toValue (v01))); };
            r.slider->defaultValue = q.to01 (q.def);
            r.slider->onChange = [this, i, q] (float v01) { edit (i, q.toValue (v01), true); };
            r.slider->onRightClick = [this, i] { knobMenu (i); };
            addAndMakeVisible (*r.slider);
        }
        const juce::String sec (q.section);
        if (boxes.empty() || boxes.back().title != sec) boxes.push_back ({ sec, {}, {} });
        boxes.back().rowIndex.push_back ((int) rows.size());
        rows.push_back (std::move (r));
    }
}

void EnginePage::sync()
{
    const auto v = readBuiltinState (inst());
    const auto& ps = builtinParams (shownKey);
    for (auto& r : rows)
    {
        const auto& q = ps[(size_t) r.param];
        const float x = v[(size_t) r.param];
        if (r.slider) r.slider->setValue (q.to01 (x));
        if (r.number) r.number->setValue ((int) std::lround (x));
        for (int k = 0; k < (int) r.choices.size(); ++k) r.choices[(size_t) k]->setToggleState (std::lround (x) == k, juce::dontSendNotification);
        if (r.cycle) r.cycle->setButtonText (juce::String (q.name) + ": " + juce::String (q.text (x)));
    }
    repaint();
}

void EnginePage::refresh()
{
    if (inst().pluginKey != shownKey || rows.empty()) { build(); resized(); }
    shownInstrument = proc.editorState.instrument;
    sync();
}

void EnginePage::tick()
{
    if (shownInstrument != proc.editorState.instrument || inst().pluginKey != shownKey) refresh();
}

void EnginePage::knobMenu (int param)
{
    const auto& ps = builtinParams (shownKey);
    if (param < 0 || param >= (int) ps.size()) return;
    const int i = proc.editorState.instrument;
    const juce::String name = juce::String (inst().pluginName) + ": " + ps[(size_t) param].name;
    juce::Component::SafePointer<EnginePage> safe (this);
    static const char* letters[] = { "L", "Q", "H", "W" };
    std::vector<MenuItem> items;
    for (int k = 0; k < kInstrumentKnobs; ++k)
        items.push_back ({ juce::String ("KNOB ") + letters[k] + " (" + letters[k] + "XX ON ITS NOTES)", [safe, i, k, param, name]
        {
            if (! safe) return;
            safe->proc.history.instrument (i, juce::String ("KNOB ") + letters[k] + " = " + name);
            safe->proc.withLock ([&] { safe->proc.song.instruments[(size_t) i].knobs[k] = AutoTarget { kAutoInstrumentBase + i, param, name.toStdString() }; });
            if (safe->onStatus) safe->onStatus (juce::String ("KNOB ") + letters[k] + " NOW MOVES " + name + " - " + letters[k] + "XX ON THIS INSTRUMENT'S NOTES (00-FF)");
        } });
    const int tr = proc.editorState.track;
    for (int k = 0; k < 2; ++k)
        items.push_back ({ "TRACK " + juce::String (tr + 1) + (k == 0 ? " AUTO A (VXX)" : " AUTO B (JXX)"), [safe, tr, k, i, param, name]
        {
            if (! safe) return;
            safe->proc.history.mixer ("TRACK " + juce::String (tr + 1) + (k == 0 ? " AUTO A = " : " AUTO B = ") + name);
            safe->proc.withLock ([&] { safe->proc.song.mixer.trackAuto[tr][k] = AutoTarget { kAutoInstrumentBase + i, param, name.toStdString() }; });
            if (safe->onStatus) safe->onStatus ("TRACK " + juce::String (tr + 1) + (k == 0 ? " AUTO A (VXX) NOW MOVES " : " AUTO B (JXX) NOW MOVES ") + name);
        } });
    showMenu (ps[(size_t) param].name, std::move (items));
}

void EnginePage::resized()
{
    const float px = Theme::px;
    const int rowH = (int) (px * 11);
    const int gap = (int) std::max (2.0f, px);
    const int title = (int) (px * 11);
    auto r = getLocalBounds().reduced (gap);
    auto top = r.removeFromTop (rowH);
    auto charsW = [px] (int chars) { return (int) (px * 6 * chars + px * 4); };
    closeBtn.setBounds (top.removeFromRight (charsW (5))); top.removeFromRight (gap);
    optionsBtn.setBounds (top.removeFromRight (charsW (17))); top.removeFromRight (gap);
    playBtn.setBounds (top.removeFromRight (charsW (14))); top.removeFromRight (gap * 2);
    titleArea = top.toFloat();
    r.removeFromTop (gap * 2);
    // the boxes in columns: as few columns as fit, each filled top to bottom. A row of choice buttons too narrow for
    // its words takes two lines (half the buttons on each), so no word is ever cut.
    auto choiceLines = [&] (const Row& rw, int width)
    {
        if (rw.choices.empty()) return 1;
        const int n = (int) rw.choices.size();
        int widest = 0;
        for (const auto& c : rw.choices) widest = std::max (widest, charsW (c->getButtonText().length()));
        for (int lines = 1; lines < n; ++lines)
        {
            const int per = (n + lines - 1) / lines;
            if (widest * per + gap * (per - 1) <= width) return lines;
        }
        return n;
    };
    auto boxHeight = [&] (const Box& b, int width)
    {
        int lines = 0;
        for (int ri : b.rowIndex) lines += choiceLines (rows[(size_t) ri], width - gap * 4);
        return gap * 4 + title + lines * (rowH + gap);
    };
    int cols = 1, colW = r.getWidth();
    for (int pass = 0; pass < 2; ++pass)
    {
        int total = 0;
        for (const auto& b : boxes) total += boxHeight (b, colW) + gap * 2;
        cols = juce::jlimit (1, 3, (int) std::ceil ((double) total / std::max (1, r.getHeight())));
        colW = (r.getWidth() - gap * 3 * (cols - 1)) / std::max (1, cols);
    }
    int total = 0;
    for (const auto& b : boxes) total += boxHeight (b, colW) + gap * 2;
    int col = 0, y = r.getY();
    const int perCol = (int) std::ceil ((double) total / cols);
    int used = 0;
    for (auto& b : boxes)
    {
        const int h = boxHeight (b, colW);
        if (used > 0 && used + h > perCol + rowH && col < cols - 1) { ++col; y = r.getY(); used = 0; }
        juce::Rectangle<int> area (r.getX() + col * (colW + gap * 3), y, colW, h);
        b.area = area.toFloat();
        auto in = area.reduced (gap * 2).withTrimmedTop (title);
        for (int ri : b.rowIndex)
        {
            auto& rw = rows[(size_t) ri];
            const int lines = choiceLines (rw, in.getWidth());
            auto row = in.removeFromTop (rowH); in.removeFromTop (gap);
            if (rw.slider) rw.slider->setBounds (row);
            if (rw.number) rw.number->setBounds (row.removeFromLeft (std::min (row.getWidth(), rw.number->preferredWidth (px))));
            if (rw.cycle) rw.cycle->setBounds (row);
            if (! rw.choices.empty())
            {
                const int n = (int) rw.choices.size();
                const int perLine = (n + lines - 1) / lines;
                int k = 0;
                for (int line = 0; line < lines; ++line)
                {
                    if (line > 0) { row = in.removeFromTop (rowH); in.removeFromTop (gap); }
                    const int m = std::min (perLine, n - k);
                    const int w = (row.getWidth() - gap * (perLine - 1)) / perLine;
                    for (int j = 0; j < m; ++j, ++k) { rw.choices[(size_t) k]->setBounds (row.removeFromLeft (j < perLine - 1 ? w : row.getWidth())); row.removeFromLeft (gap); }
                }
            }
        }
        y += h + gap * 2; used += h + gap * 2;
    }
}

void EnginePage::paint (juce::Graphics& g)
{
    const auto& pal = Theme::palFor (*this);
    const float px = Theme::px;
    g.fillAll (pal.panel);
    // the title: as much of it as fits (never cut)
    const juce::String name = juce::String (inst().pluginName);
    const juce::String what = inst().pluginKey == kSubKey ? " - CLICK-FREE SUB BASS" : inst().pluginKey == kGrainKey ? " - GRAIN SYNTH" : "";
    const juce::String num = hex2 (proc.editorState.instrument + 1);
    for (const auto& t : { "INSTRUMENT " + num + ": " + name + what, "INSTRUMENT " + num + ": " + name, num + " " + name, name })
        if (PixelFont::textWidth (t, px) <= titleArea.getWidth() || t == name) { PixelFont::drawLeft (g, t, titleArea, px, pal.ink); break; }
    for (const auto& b : boxes) Theme::panelBox (g, b.area, b.title, pal);
    if (hasKeyboardFocus (true)) { g.setColour (pal.ink); g.drawRect (getLocalBounds().toFloat(), std::max (2.0f, px * 0.5f)); }
}

// play it from the computer keyboard (as the pattern's keys)
bool EnginePage::keyPressed (const juce::KeyPress& k)
{
    if (k.getKeyCode() == juce::KeyPress::escapeKey) { if (onClose) onClose(); return true; }
    if (k.getModifiers().isCommandDown() || k.getModifiers().isCtrlDown()) return false;
    static const char* lower = "zsxdcvgbhnjm,l.;/";
    static const char* upper = "q2w3er5t6y7ui9o0p";
    const auto ch = juce::CharacterFunctions::toLowerCase (k.getTextCharacter());
    int note = -1;
    for (int i = 0; lower[i]; ++i) if (ch == (juce::juce_wchar) lower[i]) note = proc.editorState.octave * 12 + i + 1;
    for (int i = 0; upper[i]; ++i) if (ch == (juce::juce_wchar) upper[i]) note = (proc.editorState.octave + 1) * 12 + i + 1;
    if (note <= 0) return false;
    note = juce::jlimit (1, (int) kNoteMax, note);
    if (heldKeys.count (k.getKeyCode()) == 0)
    {
        heldKeys[k.getKeyCode()] = note;
        proc.withLock ([&] { proc.player.jamNoteOn (note, proc.editorState.instrument); });
        proc.keyNoteInput (note, true);
    }
    return true;
}

bool EnginePage::keyStateChanged (bool)
{
    for (auto it = heldKeys.begin(); it != heldKeys.end();)
    {
        if (! juce::KeyPress::isKeyCurrentlyDown (it->first))
        {
            const int n = it->second;
            proc.withLock ([&] { proc.player.jamNoteOff (n); });
            proc.keyNoteInput (n, false);
            it = heldKeys.erase (it);
        }
        else ++it;
    }
    return false;
}
} // namespace prv::ui
