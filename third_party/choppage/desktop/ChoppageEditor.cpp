#include "ChoppageEditor.h"

using namespace skin;
namespace P = bc::params;

static juce::String u8 (const char* s) { return juce::String::fromUTF8 (s); }

// =========================================================================================
//  Panel: the whole front, at design size
// =========================================================================================
class ChoppageEditor::Panel final : public juce::Component
{
public:
    Panel (ChoppageEditor& ed, ChoppageProcessor& p)
        : editor (ed), proc (p), sample (p), seq (p), fx (p), set (p), disk (p), akai (p),
          patternDice ("PATTERN"), fxDice ("FX"),
          patAmount (juce::Colour (0xffd7262e)), fxAmount (juce::Colour (0xffd7262e)),
          filter (juce::Colours::white), dist (juce::Colour (0xffd7262e)), crush (juce::Colours::white),
          reverb (juce::Colours::white), vol (juce::Colours::white)
    {
        const char* tabNames[] = { "SAMPLE", "SEQ", "FX", "SET", "DISK", "AKAI" };
        for (int i = 0; i < 6; ++i)
        {
            auto b = std::make_unique<ui::ModeButton> (tabNames[i]);
            b->onClick = [this, i] { show (i); };
            addAndMakeVisible (*b);
            tabs.push_back (std::move (b));
        }
        for (auto* t : { (juce::Component*) &sample, (juce::Component*) &seq, (juce::Component*) &fx, (juce::Component*) &set, (juce::Component*) &disk,
                         (juce::Component*) &akai })
            addChildComponent (t);
        disk.onChooseFolder = [this] { editor.chooseFolder(); };
        disk.onChooseFile = [this] { editor.chooseFile(); };
        seq.onShowAkai = [this] { show (5); };
        disk.onSavePreset = [this] { editor.choosePreset (true); };
        disk.onLoadPreset = [this] { editor.choosePreset (false); };

        // ---- LCD top bar ----
        auto cell = [this] (std::function<juce::String()> text, std::function<void()> click, std::function<bool()> on = nullptr, float size = 28.0f)
        {
            auto c = std::make_unique<ui::LcdCell>();
            c->text = std::move (text);
            c->isOn = std::move (on);
            c->boxed = true;
            c->fontSize = size;
            c->just = juce::Justification::centred;
            if (click) c->onClick = [click] (const juce::MouseEvent&) { click(); };
            addAndMakeVisible (*c);
            topBar.push_back (std::move (c));
            return topBar.back().get();
        };
        playCell = cell ([] { return u8 ("\xe2\x96\xb6"); }, [this] { proc.setPlaying (! proc.isPlaying()); },
                         [this] { return proc.isPlaying(); });
        playCell->setTooltip ("Play / stop with the host stopped (the host's transport runs it otherwise)");
        prevCell = cell ([] { return u8 ("\xe2\x97\x80"); }, [this] { proc.selectSlot ((proc.getSelectedSlot() + bc::kSlots - 1) % bc::kSlots); });
        nameCell = cell ([this]
        {
            const int i = proc.getSelectedSlot();
            const auto& s = proc.getSlot (i);
            juce::String name = s.content != nullptr ? juce::String (s.content->name).toUpperCase() : ChoppageProcessor::statusText (s.status);
            return "BREAK " + juce::String (i + 1) + "/8 " + u8 ("\xc2\xb7 ") + name;
        }, [this] { show (4); });
        nameCell->setTooltip ("Click: DISK, to load a break into this slot (or drop files on the panel)");
        nextCell = cell ([] { return u8 ("\xe2\x96\xb6"); }, [this] { proc.selectSlot ((proc.getSelectedSlot() + 1) % bc::kSlots); });
        loadCell = cell ([] { return juce::String ("LOAD"); }, [this] { show (4); });   // (0.17: the DISK page, as on the MPC)
        loadCell->setTooltip ("DISK: browse your folders (8 PLACES), hear a break with the arrow keys, RETURN loads it");
        dragCell = cell ([] { return juce::String ("DRAG"); }, nullptr, [] { return true; });
        dragCell->onDragOut = [this]
        {
            const auto f = proc.bounceLoop();
            if (f.existsAsFile()) juce::DragAndDropContainer::performExternalDragDropOfFiles ({ f.getFullPathName() }, false, this);
        };
        dragCell->setTooltip ("Drag the pattern out to your DAW as a WAV");
        bpmCell = cell ([this]
        {
            const double host = proc.getHostBpm();
            const auto& s = proc.getSlot (proc.getSelectedSlot());
            const double bpm = host > 1.0 ? host : (s.content != nullptr ? s.content->bpm : 0.0);
            return bpm > 0.0 ? juce::String (bpm, 1) + " BPM" : juce::String ("--- BPM");
        }, nullptr);

        // ---- slot row ----
        const char* keys[] = { "C1", "C#1", "D1", "D#1", "E1", "F1", "F#1", "G1" };
        for (int i = 0; i < bc::kSlots; ++i)
        {
            auto c = std::make_unique<ui::LcdCell>();
            const juce::String key = keys[i];
            c->text = [this, i, key]
            {
                const auto& s = proc.getSlot (i);
                const juce::String mark = proc.getStepOf (i) >= 0 ? u8 ("\xe2\x96\xb6") : juce::String (i + 1);
                return mark + " " + key + (s.status == ChoppageProcessor::Status::empty ? u8 (" \xe2\x80\x94") : juce::String());
            };
            c->isOn = [this, i] { return proc.getSelectedSlot() == i; };
            c->boxed = true;
            c->fontSize = 24.0f;
            c->just = juce::Justification::centred;
            c->onClick = [this, i] (const juce::MouseEvent& e)
            {
                proc.selectSlot (i);
                if (e.mods.isPopupMenu())
                {
                    juce::PopupMenu m;
                    m.addItem ("LOAD...", [this] { editor.chooseFile(); });
                    m.addItem ("DEMO BREAK", [this, i] { proc.loadDemo (i); });
                    m.addItem ("CLEAR SLOT", [this, i] { proc.clearSlot (i); });
                    m.showMenuAsync (juce::PopupMenu::Options().withMousePosition());
                }
            };
            c->setTooltip ("Break " + juce::String (i + 1) + " (MIDI " + key + "). Right-click: load / demo / clear");
            addAndMakeVisible (*c);
            slotCells.push_back (std::move (c));
        }

        status.text = [this] { return statusLine(); };
        status.fontSize = 22.0f;
        status.setInterceptsMouseClicks (false, false);
        addAndMakeVisible (status);

        // ---- dice ----
        patternDice.onClick = [this] { proc.rollPattern(); patternDice.flash = true; flashUntil = juce::Time::getMillisecondCounter() + 150; };
        fxDice.onClick      = [this] { proc.rollFx();      fxDice.flash = true;      flashUntil = juce::Time::getMillisecondCounter() + 150; };
        addAndMakeVisible (patternDice);
        addAndMakeVisible (fxDice);

        attach (patAmount, P::patAmount);
        attach (fxAmount, P::fxAmount);
        attach (filter, P::filter);
        attach (dist, P::dist);
        attach (crush, P::crush);
        attach (reverb, P::reverb);
        attach (vol, P::volume);
        sliderAttachments.push_back (std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, P::sampleRate, srFader));
        addAndMakeVisible (srFader);
        srFader.setTooltip ("SAMPLE RATE: 44.1 kHz down to 4.0 kHz");

        // ---- FX pads ----
        for (int i = 0; i < bc::kFxPads; ++i)
        {
            auto pad = std::make_unique<ui::Pad>();
            pad->name = P::fxPadName (i);
            pad->setting = [this, i] { return padSetting (i); };
            pad->isLit = [this, i] { return proc.isPadLit (i); };
            pad->onPress = [this, i] (bool down) { proc.setPadHeld (i, down); };
            pad->onSettingDrag = [this, i] (float steps) { nudgePadSetting (i, steps); };
            addAndMakeVisible (*pad);
            pads.push_back (std::move (pad));
        }

        addAndMakeVisible (meter);
        show (0);
        setSize (designW, designH);   // last: lays out everything made above
    }

    void show (int t)
    {
        current = juce::jlimit (0, 5, t);
        juce::Component* pages[] = { &sample, &seq, &fx, &set, &disk, &akai };
        for (int i = 0; i < 6; ++i) { pages[i]->setVisible (i == current); tabs[(size_t) i]->lit = i == current; tabs[(size_t) i]->repaint(); }
    }

    void refresh()
    {
        const int theme = (int) proc.plain (bc::P_LCD_COLOUR);
        if (theme != skin::lcdThemeIndex().load()) { skin::lcdThemeIndex() = theme; repaint(); }
        if (current == 0) sample.refresh();
        else if (current == 1) seq.refresh();
        else if (current == 2) fx.refresh();
        else if (current == 3) set.refresh();
        else if (current == 4) disk.refresh();
        else akai.refresh();
        for (auto& c : topBar) c->repaint();
        for (auto& c : slotCells) c->repaint();
        status.repaint();
        for (auto& p : pads) p->repaint();
        if (flashUntil != 0 && juce::Time::getMillisecondCounter() > flashUntil)
        {
            patternDice.flash = fxDice.flash = false;
            patternDice.repaint(); fxDice.repaint();
            flashUntil = 0;
        }
        meter.setLevel (proc.getPeak());
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds();
        // rack ears
        auto earL = r.removeFromLeft (earW), earR = r.removeFromRight (earW);
        for (auto e : { earL, earR })
        {
            g.setGradientFill (juce::ColourGradient (juce::Colour (0xffcfcbc2), (float) e.getX(), 0.0f,
                                                     juce::Colour (0xffe6e3dc), (float) e.getRight(), 0.0f, false));
            g.fillRect (e);
            g.setColour (col::line);
            g.drawRect (e, 2);
            for (float fy : { 0.12f, 0.5f, 0.88f }) drawScrew (g, { (float) e.getCentreX(), e.getHeight() * fy }, 16.0f);
        }
        g.setGradientFill (juce::ColourGradient (col::panelTop, 0.0f, 0.0f, col::panelBot, 0.0f, (float) getHeight(), false));
        g.fillRect (r);

        // header
        g.setColour (col::red);
        g.setFont (logo (40.0f));
        g.drawText ("TWISTED GENETICS", headerArea.withWidth (430).withHeight (46), juce::Justification::centredLeft);
        g.setColour (col::label);
        g.setFont (skin::ui (20.0f));
        g.drawText ("MIDI BREAK CHOPPER", headerArea.withWidth (430).withTrimmedTop (48).withHeight (26), juce::Justification::centredLeft);
        {
            juce::GlyphArrangement ga;
            ga.addLineOfText (skin::ui (64.0f), "CHOPPAGE", (float) headerArea.getX() + 460.0f, (float) headerArea.getY() + 62.0f);
            juce::Path pth; ga.createPath (pth);
            g.setColour (juce::Colour (0xff6b6b6b));
            g.strokePath (pth, juce::PathStrokeType (2.2f));
        }

        // fader / meter labels
        ui::drawLabel (g, "44.1 kHz", leftCol.withY (srFader.getY() - 30).withHeight (26), 18.0f);
        ui::drawLabel (g, "4.0 kHz", leftCol.withY (srFader.getBottom() + 2).withHeight (26), 18.0f);
        ui::drawLabel (g, "SAMPLE RATE", leftCol.withY (srFader.getBottom() + 32).withHeight (50), 19.0f);
        {
            auto read = leftCol.withY (srFader.getBottom() + 90).withHeight (40).reduced (10, 0);
            drawLcd (g, read.toFloat().expanded (6.0f));
            g.setColour (lcdc().ink);
            g.setFont (lcd (24.0f));
            g.drawText (juce::String (srFader.getValue() / 1000.0, 1) + "k", read, juce::Justification::centred);
        }
        ui::drawLabel (g, "LEVEL", meter.getBounds().withY (meter.getY() - 30).withHeight (26), 18.0f);
        ui::drawLabel (g, "VOL", vol.getBounds().withY (vol.getBottom()).withHeight (26), 19.0f);

        // the LCD
        drawLcd (g, lcdArea.toFloat());

        // knob labels
        auto knobLabel = [&g] (const juce::Slider& k, const juce::String& t)
        { ui::drawLabel (g, t, k.getBounds().withY (k.getBottom() + 2).withHeight (28).expanded (30, 0), 21.0f); };
        knobLabel (filter, "FILTER"); knobLabel (dist, "DISTORTION"); knobLabel (crush, "CRUSH"); knobLabel (reverb, "REVERB");
        ui::drawLabel (g, "AMOUNT " + proc.apvts.getParameter (P::patAmount)->getCurrentValueAsText(),
                       patAmount.getBounds().withY (patAmount.getBottom()).withHeight (24).expanded (40, 0), 17.0f);
        ui::drawLabel (g, "AMOUNT " + proc.apvts.getParameter (P::fxAmount)->getCurrentValueAsText(),
                       fxAmount.getBounds().withY (fxAmount.getBottom()).withHeight (24).expanded (40, 0), 17.0f);

        ui::drawSection (g, u8 ("FX PADS \xc2\xb7 G#1\xe2\x80\x93" "D#2 \xc2\xb7 HOLD TO PLAY \xc2\xb7 SCROLL A PAD TO CHANGE ITS SETTING"), padsHeader);

        // bottom strip
        g.setColour (col::line);
        g.fillRect (bottomArea.getX(), bottomArea.getY() - 10, bottomArea.getWidth(), 2);
        ui::drawLabel (g, u8 ("FLOPPY \xc2\xb7 DROP BREAKS / CLICK TO LOAD"), floppy.withY (floppy.getY() - 28).withHeight (26).withWidth (floppy.getWidth() + 120), 17.0f,
                       juce::Justification::centredLeft);
        g.setColour (juce::Colour (0xff1e1e1e));
        g.fillRoundedRectangle (floppy.toFloat(), 4.0f);
        g.setColour (juce::Colour (0xff9a968d));
        g.drawRoundedRectangle (floppy.toFloat(), 4.0f, 3.0f);
        auto slotR = floppy.reduced (26, 0).withSizeKeepingCentre (floppy.getWidth() - 52, 14);
        g.setColour (juce::Colours::black);
        g.fillRect (slotR);
        g.setColour (juce::Colour (0xff444444));
        g.fillRect (slotR.removeFromBottom (3));
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (floppy.contains (e.getPosition())) show (4);
    }

    void resized() override
    {
        auto r = getLocalBounds().withTrimmedLeft (earW).withTrimmedRight (earW).reduced (26, 18);
        headerArea = r.removeFromTop (100);
        auto tabRow = headerArea.withTrimmedLeft (headerArea.getWidth() - 6 * 100);
        for (auto& t : tabs) t->setBounds (tabRow.removeFromLeft (100).withTrimmedRight (12).withTrimmedBottom (6));
        r.removeFromTop (10);

        bottomArea = r.removeFromBottom (150);
        r.removeFromBottom (20);

        auto left = r.removeFromLeft (118);
        leftCol = left;
        r.removeFromLeft (20);
        auto right = r.removeFromRight (100);
        r.removeFromRight (20);

        srFader.setBounds (left.withTrimmedTop (30).withTrimmedBottom (140).withSizeKeepingCentre (70, left.getHeight() - 170));
        meter.setBounds (right.withTrimmedTop (30).withTrimmedBottom (120).withSizeKeepingCentre (44, right.getHeight() - 150));
        vol.setBounds (right.withTrimmedTop (right.getHeight() - 112).withHeight (84).withSizeKeepingCentre (80, 80));

        // pads, dice, LCD (bottom up)
        auto padRow = r.removeFromBottom (104);
        padsHeader = r.removeFromBottom (30);
        r.removeFromBottom (12);
        const int pw = (padRow.getWidth() - 7 * 10) / 8;
        for (auto& p : pads) { p->setBounds (padRow.removeFromLeft (pw)); padRow.removeFromLeft (10); }

        auto diceRow = r.removeFromBottom (112);
        r.removeFromBottom (14);
        auto half = diceRow.getWidth() / 2;
        auto dl = diceRow.removeFromLeft (half - 10), dr = diceRow.withTrimmedLeft (10);
        patAmount.setBounds (dl.removeFromRight (120).withTrimmedBottom (26).withSizeKeepingCentre (80, 80));
        patternDice.setBounds (dl.withTrimmedRight (14).withSizeKeepingCentre (dl.getWidth() - 14, 92));
        fxAmount.setBounds (dr.removeFromRight (120).withTrimmedBottom (26).withSizeKeepingCentre (80, 80));
        fxDice.setBounds (dr.withTrimmedRight (14).withSizeKeepingCentre (dr.getWidth() - 14, 92));

        lcdArea = r;
        auto glass = r.reduced (11).reduced (16, 12);
        auto bar = glass.removeFromTop (44);
        const int widths[] = { 56, 50, 0, 50, 96, 96, 190 };
        int fixed = 0; for (int w : widths) fixed += w;
        const int nameW = bar.getWidth() - fixed - 6 * 10;
        for (size_t i = 0; i < topBar.size(); ++i)
        {
            const int w = widths[i] == 0 ? nameW : widths[i];
            topBar[i]->setBounds (bar.removeFromLeft (w));
            bar.removeFromLeft (10);
        }
        glass.removeFromTop (8);
        auto slotsRow = glass.removeFromTop (38);
        const int sw = (slotsRow.getWidth() - 7 * 8) / 8;
        for (auto& c : slotCells) { c->setBounds (slotsRow.removeFromLeft (sw)); slotsRow.removeFromLeft (8); }
        glass.removeFromTop (10);
        status.setBounds (glass.removeFromBottom (30));
        glass.removeFromBottom (4);
        for (auto* t : { (juce::Component*) &sample, (juce::Component*) &seq, (juce::Component*) &fx, (juce::Component*) &set, (juce::Component*) &disk,
                         (juce::Component*) &akai })
            t->setBounds (glass);

        // bottom: floppy + the four big knobs
        auto b = bottomArea;
        floppy = b.removeFromLeft (300).withSizeKeepingCentre (300, 74).translated (0, 14);
        b.removeFromLeft (40);
        b.removeFromRight (340);
        const int kw = b.getWidth() / 4;
        for (auto* k : { &filter, &dist, &crush, &reverb })
            k->setBounds (b.removeFromLeft (kw).withTrimmedBottom (30).withSizeKeepingCentre (104, 104));
    }

private:
    void attach (ui::Knob& k, const char* id)
    {
        addAndMakeVisible (k);
        sliderAttachments.push_back (std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, id, k));
        k.setTooltip (proc.apvts.getParameter (id)->getName (64));
        k.setDoubleClickReturnValue (true, (double) proc.apvts.getParameter (id)->convertFrom0to1 (proc.apvts.getParameter (id)->getDefaultValue()));
        k.onValueChange = [this] { repaint (patAmount.getBounds().getUnion (fxAmount.getBounds()).expanded (60, 40)); };
    }

    juce::String padSetting (int i) const
    {
        auto t = [this] (const char* id) { return proc.apvts.getParameter (id)->getCurrentValueAsText().toUpperCase(); };
        switch (i)
        {
            case 0: return t (P::stutRate);
            case 1: return t (P::rollRate);
            case 2: return "HOLD";
            case 3: return juce::String (juce::roundToInt (proc.apvts.getRawParameterValue (P::tapeMs)->load())) + "MS";
            case 4: return t (P::halfMode);
            case 5: return juce::String (juce::roundToInt (proc.apvts.getRawParameterValue (P::akCycLen)->load())) + " SMP";
            case 6: return "RES " + juce::String (juce::roundToInt (proc.apvts.getRawParameterValue (P::diveRes)->load() * 100.0f));
            case 7: return juce::String (proc.apvts.getRawParameterValue (P::dropRate)->load() / 1000.0f, 1) + "K";
            default: return {};
        }
    }

    void nudgePadSetting (int i, float steps)
    {
        const char* ids[] = { P::stutRate, P::rollRate, nullptr, P::tapeMs, P::halfMode, P::akCycLen, P::diveRes, P::dropRate };
        if (ids[i] == nullptr) return;
        const ui::Param prm { proc.apvts.getParameter (ids[i]) };
        if (auto* ch = dynamic_cast<juce::AudioParameterChoice*> (prm.p))
            prm.set ((float) juce::jlimit (0, ch->choices.size() - 1, prm.choice() + (int) steps));
        else prm.setNorm (prm.norm() + steps * 0.02f);
    }

    juce::String statusLine() const
    {
        const auto& s = proc.getSlot (proc.getSelectedSlot());
        if (s.content == nullptr) return ChoppageProcessor::statusText (s.status) + u8 (" \xc2\xb7 DROP A WAV / AIFF / FLAC ON THE PANEL");
        const auto& c = *s.content;
        const double host = proc.getHostBpm();
        const juce::String dot = u8 (" \xc2\xb7 ");
        juce::String t = juce::String (c.numSlices()) + " SLICES" + dot + juce::String (c.bars) + (c.bars == 1 ? " BAR" : " BARS")
                       + dot + "BREAK " + juce::String (c.bpm, 1) + " BPM";
        t << dot << (host > 1.0 ? "HOST " + juce::String (host, 1) : juce::String ("NO HOST TEMPO"));
        {
            const int sel = proc.getSelectedSlot();
            t << dot << "GRP " << juce::String (proc.getMuteGroupOf (sel) + 1);
            bool other = false;
            for (int k = 0; k < bc::kSlots; ++k) if (k != sel && proc.getStepOf (k) >= 0) other = true;
            t << dot << (proc.getStepOf (sel) >= 0 ? "PLAYING" : other ? "OTHER PLAYING" : "STOPPED");
        }
        const auto& sl = s.program.slice[(size_t) proc.getSelectedSlice()];
        t << dot << "S" << juce::String (proc.getSelectedSlice() + 1).paddedLeft ('0', 2) << " " << bc::badgeLabel (c.badges[(size_t) proc.getSelectedSlice()]);
        if (sl.mute) t << " (MUTED)";
        return t;
    }

    ChoppageEditor& editor;
    ChoppageProcessor& proc;
    ui::SampleTab sample;
    ui::SeqTab seq;
    ui::FxTab fx;
    ui::SetTab set;
    ui::DiskTab disk;
    ui::AkaiTab akai;
    std::vector<std::unique_ptr<ui::ModeButton>> tabs;
    std::vector<std::unique_ptr<ui::LcdCell>> topBar, slotCells;
    ui::LcdCell* playCell = nullptr; ui::LcdCell* prevCell = nullptr; ui::LcdCell* nameCell = nullptr;
    ui::LcdCell* nextCell = nullptr; ui::LcdCell* loadCell = nullptr; ui::LcdCell* dragCell = nullptr; ui::LcdCell* bpmCell = nullptr;
    ui::LcdCell status;
    ui::DiceButton patternDice, fxDice;
    ui::Knob patAmount, fxAmount, filter, dist, crush, reverb, vol;
    ui::Fader srFader;
    ui::LevelMeter meter;
    std::vector<std::unique_ptr<ui::Pad>> pads;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>> sliderAttachments;
    juce::Rectangle<int> headerArea, lcdArea, padsHeader, bottomArea, floppy, leftCol;
    int current = 0;
    juce::uint32 flashUntil = 0;
    static constexpr int earW = 56;
};

// =========================================================================================
ChoppageEditor::ChoppageEditor (ChoppageProcessor& p) : juce::AudioProcessorEditor (p), proc (p)
{
    panel = std::make_unique<Panel> (*this, p);
    addAndMakeVisible (*panel);
    setResizable (true, true);
    setResizeLimits (designW / 2, designH / 2, designW * 2, designH * 2);
    if (auto* c = getConstrainer()) c->setFixedAspectRatio ((double) designW / (double) designH);
    setSize (designW * 3 / 4, designH * 3 / 4);
    if (proc.session().browserEntries().empty() && ! proc.session().browserScanning()) proc.browseTo (proc.getBrowserFolder());
    startTimerHz (30);
}

ChoppageEditor::~ChoppageEditor() { stopTimer(); }

void ChoppageEditor::resized()
{
    const float scale = (float) getWidth() / (float) designW;
    panel->setTransform (juce::AffineTransform::scale (scale));
}

void ChoppageEditor::showTab (int t) { panel->show (t); }

bool ChoppageEditor::keyPressed (const juce::KeyPress& k)
{
    const auto m = k.getModifiers();
    if (m.isCommandDown() && (k.getKeyCode() == 'Z' || k.getKeyCode() == 'z'))
    {
        if (m.isShiftDown()) proc.redoAll(); else proc.undoAll();
        return true;
    }
    if (m.isCommandDown() && (k.getKeyCode() == 'Y' || k.getKeyCode() == 'y')) { proc.redoAll(); return true; }
    return false;
}

void ChoppageEditor::timerCallback() { panel->refresh(); }

bool ChoppageEditor::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (auto& f : files)
        if (juce::File (f).hasFileExtension ("wav;aif;aiff;flac;mp3;rx2;rex")) return true;
    return false;
}

void ChoppageEditor::filesDropped (const juce::StringArray& files, int, int)
{
    // One file: into the selected slot. Several: into consecutive slots from it.
    int slot = proc.getSelectedSlot();
    for (auto& f : files)
    {
        const juce::File file (f);
        if (! file.hasFileExtension ("wav;aif;aiff;flac;mp3;rx2;rex")) continue;
        proc.loadFile (slot, file);
        if (++slot >= bc::kSlots) break;
    }
}

void ChoppageEditor::chooseFolder()
{
    chooser = std::make_unique<juce::FileChooser> ("Folder of breaks for DISK", proc.getBrowserFolder());
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                          [this] (const juce::FileChooser& fc)
                          {
                              const auto f = fc.getResult();
                              if (f.isDirectory()) proc.browseTo (f);
                          });
}

void ChoppageEditor::choosePreset (bool save)
{
    chooser = std::make_unique<juce::FileChooser> (save ? "Save a CHOPPAGE preset" : "Load a CHOPPAGE preset",
                                                   proc.presetsFolder(), "*.chp");
    const int mode = save ? juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting
                          : juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;
    chooser->launchAsync (mode, [this, save] (const juce::FileChooser& fc)
    {
        const auto f = fc.getResult();
        if (f == juce::File()) return;
        if (save) proc.savePreset (f);
        else if (f.existsAsFile()) proc.loadPreset (f);
    });
}

void ChoppageEditor::chooseFile()
{
    const int slot = proc.getSelectedSlot();
    chooser = std::make_unique<juce::FileChooser> ("Load a break into slot " + juce::String (slot + 1),
                                                   juce::File::getSpecialLocation (juce::File::userMusicDirectory),
                                                   "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.rx2;*.rex");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this, slot] (const juce::FileChooser& fc)
                          {
                              const auto f = fc.getResult();
                              if (f.existsAsFile()) proc.loadFile (slot, f);
                          });
}
