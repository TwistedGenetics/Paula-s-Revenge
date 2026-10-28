// Paula's Revenge - the mixer: channel strips, channel EQ page, effect editor page
#include "MixerPage.h"
#include "../engine/Effects.h"

namespace prv::ui
{
// ---------------------------------------------------------------------------
namespace mixmap
{
    float freqToSlider (float hz) { return juce::jlimit (0.0f, 1.0f, std::log10 (std::max (20.0f, hz) / 20.0f) / 3.0f); }
    float sliderToFreq (float v)  { return 20.0f * std::pow (1000.0f, juce::jlimit (0.0f, 1.0f, v)); }
    float gainToSlider (float db) { return juce::jlimit (0.0f, 1.0f, (db + 18.0f) / 36.0f); }
    float sliderToGain (float v)  { return std::round ((juce::jlimit (0.0f, 1.0f, v) * 36.0f - 18.0f) * 2.0f) / 2.0f; }   // 0.5 dB steps
    float qToSlider (float q)     { return juce::jlimit (0.0f, 1.0f, std::log (std::max (0.3f, q) / 0.3f) / std::log (8.0f / 0.3f)); }
    float sliderToQ (float v)     { return 0.3f * std::pow (8.0f / 0.3f, juce::jlimit (0.0f, 1.0f, v)); }
    juce::String panText (int pan)
    {
        if (pan < 0) return "AUTO";
        if (std::abs (pan - 128) <= 2) return "C";
        return juce::String (pan < 128 ? "L" : "R") + juce::String (juce::roundToInt (std::abs (pan - 128) / 127.0 * 100.0));
    }
}

namespace
{
    juce::String hzText (float hz) { return hz < 1000.0f ? juce::String (juce::roundToInt (hz)) + " HZ" : juce::String (hz / 1000.0f, hz < 10000.0f ? 2 : 1) + " KHZ"; }
    juce::String dbSigned (float db) { return (db > 0.0f ? "+" : "") + juce::String (db, 1) + " DB"; }
    const char* kBandNames[kEqBands] = { "HIGH-PASS", "LOW SHELF", "MID 1", "MID 2", "HIGH SHELF" };
}

// ---------------------------------------------------------------------------
void MiniBar::setValue (float v, bool notify)
{
    v = juce::jlimit (0.0f, 1.0f, v);
    if (v == value && ! notify) return;
    value = v;
    repaint();
    if (notify && onChange) onChange (v);
}

void MiniBar::paint (juce::Graphics& g)
{
    const float px = Theme::px;
    auto r = getLocalBounds().toFloat().reduced (1.0f);
    g.setColour (Theme::paper);
    g.fillRect (r);
    if (showFill)
    {
        g.setColour (Theme::band);
        auto in = r.reduced (px * 0.5f);
        if (bipolar)
        {
            const float c = in.getCentreX(), x = in.getX() + in.getWidth() * value;
            g.fillRect (juce::Rectangle<float> (std::min (c, x), in.getY(), std::max (px, std::abs (x - c)), in.getHeight()));
        }
        else g.fillRect (in.withWidth (in.getWidth() * value));
    }
    Theme::bevel (g, r, false, std::max (1.0f, px * 0.5f));
    const juce::String t = label.isEmpty() ? format (value) : label + " " + format (value);
    PixelFont::drawCentred (g, t, r, px, isEnabled() ? Theme::ink : Theme::faint);
}

void MiniBar::mouseDown (const juce::MouseEvent&)
{
    if (onDragStart) onDragStart();
    dragStartValue = value;
}

void MiniBar::mouseDrag (const juce::MouseEvent& e)
{
    // relative and calm: the whole range takes about two widths of travel (Shift = fine)
    const float span = std::max (40.0f, (float) getWidth() * 2.0f) * (e.mods.isShiftDown() ? 8.0f : 1.0f);
    setValue (dragStartValue + (e.getDistanceFromDragStartX() - e.getDistanceFromDragStartY()) / span, true);
}

void MiniBar::mouseDoubleClick (const juce::MouseEvent&)
{
    if (onDoubleClick) onDoubleClick();
    else { if (onDragStart) onDragStart(); setValue (defaultValue, true); }
}

void MiniBar::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    WheelSteps steps { wheelAccum, 8.0f };
    const int n = steps (w);
    wheelAccum = steps.accum;
    if (n == 0) return;
    if (onDragStart) onDragStart();
    setValue (value - n * (e.mods.isShiftDown() ? 0.002f : 0.01f), true);
}

// ---------------------------------------------------------------------------
float FaderMeter::dbToPos (float db)
{
    if (db <= -59.9f) return 0.0f;
    if (db < -30.0f) return 0.25f * (db + 60.0f) / 30.0f;
    return juce::jmin (1.0f, 0.25f + 0.75f * (db + 30.0f) / 36.0f);
}

float FaderMeter::posToDb (float pos)
{
    pos = juce::jlimit (0.0f, 1.0f, pos);
    if (pos <= 0.003f) return -60.0f;
    if (pos < 0.25f) return -60.0f + 30.0f * pos / 0.25f;
    return -30.0f + 36.0f * (pos - 0.25f) / 0.75f;
}

juce::String FaderMeter::dbText (float db)
{
    if (db <= -59.9f) return "-INF";
    return (db > 0.04f ? "+" : "") + juce::String (db, 1);
}

void FaderMeter::setDb (float d, bool notify)
{
    d = juce::jlimit (-60.0f, 6.0f, d);
    if (std::abs (d) < 0.05f) d = 0.0f;
    if (d == db && ! notify) return;
    db = d;
    repaint();
    if (notify && onChange) onChange (d);
}

void FaderMeter::setLevels (float l, float r)
{
    auto step = [] (float in, float& lvl, float& hold, int& count)
    {
        lvl = std::max (in, lvl * 0.82f);
        if (in >= hold) { hold = in; count = 40; }
        else if (count > 0) --count;
        else hold *= 0.9f;
    };
    const float oldL = levelL, oldR = levelR, oldHL = holdL, oldHR = holdR;
    step (l, levelL, holdL, holdCountL);
    step (r, levelR, holdR, holdCountR);
    if (oldL != levelL || oldR != levelR || oldHL != holdL || oldHR != holdR) repaint();
}

juce::Rectangle<float> FaderMeter::troughArea() const
{
    auto r = getLocalBounds().toFloat();
    return r.removeFromLeft (r.getWidth() * 0.5f).reduced (Theme::px, Theme::px * 3);
}

void FaderMeter::paint (juce::Graphics& g)
{
    const auto& sk = Theme::skin;
    const float px = Theme::px;
    auto r = getLocalBounds().toFloat();
    auto trough = troughArea();
    auto meter = r.withTrimmedLeft (r.getWidth() * 0.5f).reduced (px, px * 3);

    // scale ticks (0 dB marked) between the fader and the meter
    g.setColour (Theme::ink);
    for (float d : { 6.0f, 0.0f, -6.0f, -12.0f, -24.0f, -40.0f })
    {
        const float y = trough.getBottom() - trough.getHeight() * dbToPos (d);
        g.fillRect (trough.getRight() + px * 0.5f, y - px * 0.25f, d == 0.0f ? px * 2.5f : px * 1.5f, std::max (1.0f, px * 0.5f));
    }

    // fader trough with a centre slot
    g.setColour (sk.bg);
    g.fillRect (trough);
    Theme::bevel (g, trough, false, std::max (1.0f, px * 0.5f));
    g.setColour (sk.dim);
    g.fillRect (trough.withSizeKeepingCentre (px, trough.getHeight() - px * 2));

    // meter: one bar, or L and R
    g.setColour (sk.bg);
    g.fillRect (meter);
    auto drawBar = [&] (juce::Rectangle<float> bar, float lvl, float hold)
    {
        const float pos = dbToPos (juce::Decibels::gainToDecibels (lvl, -60.0f));
        const float hpos = dbToPos (juce::Decibels::gainToDecibels (hold, -60.0f));
        const float zero = dbToPos (0.0f);
        auto lit = bar.withTop (bar.getBottom() - bar.getHeight() * pos);
        g.setColour (colour);
        g.fillRect (lit.withTop (std::max (lit.getY(), bar.getBottom() - bar.getHeight() * zero)));
        if (pos > zero)
        {
            g.setColour (sk.hot);
            g.fillRect (lit.withBottom (bar.getBottom() - bar.getHeight() * zero));
        }
        if (hold > 0.001f)
        {
            g.setColour (hpos > zero ? sk.hot : sk.text);
            g.fillRect (bar.getX(), bar.getBottom() - bar.getHeight() * hpos - px * 0.5f, bar.getWidth(), std::max (1.0f, px * 0.75f));
        }
    };
    auto inner = meter.reduced (px * 0.5f);
    if (stereo)
    {
        auto left = inner.removeFromLeft (inner.getWidth() * 0.5f - px * 0.25f);
        inner.removeFromLeft (px * 0.5f);
        drawBar (left, levelL, holdL);
        drawBar (inner, levelR, holdR);
    }
    else drawBar (inner, std::max (levelL, levelR), std::max (holdL, holdR));
    Theme::bevel (g, meter, false, std::max (1.0f, px * 0.5f));

    // the fader cap
    const float capH = px * 6;
    const float y = trough.getBottom() - trough.getHeight() * dbToPos (db);
    auto cap = juce::Rectangle<float> (trough.getX() - px, y - capH * 0.5f, trough.getWidth() + px * 2, capH);
    g.setColour (Theme::panel);
    g.fillRect (cap);
    Theme::bevel (g, cap, true, std::max (1.0f, px * 0.5f));
    g.setColour (Theme::ink);
    g.fillRect (cap.withSizeKeepingCentre (cap.getWidth() - px * 2, std::max (1.0f, px * 0.75f)));
}

void FaderMeter::mouseDown (const juce::MouseEvent& e)
{
    if (onDragStart) onDragStart();
    // clicking the trough away from the cap jumps the cap there; then drags are relative
    auto t = troughArea();
    const float capY = t.getBottom() - t.getHeight() * dbToPos (db);
    if (! e.mods.isShiftDown() && std::abs (e.position.y - capY) > Theme::px * 4 && t.expanded (Theme::px * 2, 0).contains (e.position))
        setDb (posToDb ((t.getBottom() - e.position.y) / std::max (1.0f, t.getHeight())), true);
    dragStartDb = db;
}

void FaderMeter::mouseDrag (const juce::MouseEvent& e)
{
    auto t = troughArea();
    const float fine = e.mods.isShiftDown() ? 0.15f : 1.0f;
    const float pos = dbToPos (dragStartDb) - e.getDistanceFromDragStartY() / std::max (1.0f, t.getHeight()) * fine;
    setDb (posToDb (pos), true);
}

void FaderMeter::mouseDoubleClick (const juce::MouseEvent&)
{
    if (onDragStart) onDragStart();
    setDb (0.0f, true);
}

void FaderMeter::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    WheelSteps steps { wheelAccum, 8.0f };
    const int n = steps (w);
    wheelAccum = steps.accum;
    if (n == 0) return;
    if (onDragStart) onDragStart();
    const float d = std::max (-60.0f, db);
    setDb (d <= -59.9f && n < 0 ? -40.0f : d - n * (e.mods.isShiftDown() ? 0.1f : 0.5f), true);
}

// ---------------------------------------------------------------------------
// One channel strip: a track, a return (SEND A / B) or the master
class MixStrip : public juce::Component
{
public:
    enum class Kind { Track, Return, Master };
    MixStrip (MixerPage& p, Kind k, int idx) : page (p), kind (k), index (idx)
    {
        nameBtn.setButtonText (kind == Kind::Track ? "TRK " + juce::String (idx + 1) : kind == Kind::Return ? juce::String ("SEND ") + (char) ('A' + idx) : juce::String ("MASTER"));
        nameBtn.onClick = [this] { if (kind == Kind::Track) page.openChannel (index); };
        eqBtn.onClick = [this] { if (kind == Kind::Track) page.openChannel (index); refresh(); };
        for (int k2 = 0; k2 < kInserts; ++k2)
        {
            slotBtn[k2].onClick = [this, k2]
            {
                const int s = slotIndex (k2);
                if (juce::ModifierKeys::currentModifiers.isShiftDown()) page.toggleBypass (s);
                else page.openEffect (s);
            };
            addAndMakeVisible (slotBtn[k2]);
        }
        for (int s = 0; s < kSends; ++s)
        {
            send[s].label = juce::String::charToString ((juce::juce_wchar) ('A' + s));
            send[s].onDragStart = [this, s] { page.processor().history.mixer ("TRACK " + juce::String (index + 1) + " SEND " + (char) ('A' + s), true); };
            send[s].onChange = [this, s] (float v)
            {
                auto& proc = page.processor();
                proc.withLock ([&] { proc.song.mixer.ch[index].send[s] = v; });
            };
            send[s].onDoubleClick = [this, s]
            {
                page.edit ("TRACK " + juce::String (index + 1) + " SEND OFF", false, [this, s] (MixerSettings& m) { m.ch[index].send[s] = 0.0f; });
            };
            if (kind == Kind::Track) addAndMakeVisible (send[s]);
        }
        pan.bipolar = true;
        pan.label = "PAN";
        pan.format = [this] (float v)
        {
            if (kind == Kind::Track && page.processor().song.trackPan[index] < 0) return juce::String ("AUTO");
            return mixmap::panText (juce::roundToInt (v * 255.0f));
        };
        pan.onDragStart = [this]
        {
            auto& proc = page.processor();
            if (kind == Kind::Track) proc.history.settings ("TRACK " + juce::String (index + 1) + " PAN", true);
            else proc.history.mixer (juce::String ("SEND ") + (char) ('A' + index) + " PAN", true);
            if (kind == Kind::Track && proc.song.trackPan[index] < 0) pan.setValue (128.0f / 255.0f);   // drag from the centre
        };
        pan.onChange = [this] (float v)
        {
            auto& proc = page.processor();
            int pv = juce::roundToInt (v * 255.0f);
            if (std::abs (pv - 128) <= 3) pv = 128;
            proc.withLock ([&] { if (kind == Kind::Track) proc.song.trackPan[index] = pv; else proc.song.mixer.ret[index].pan = pv; });
        };
        pan.onDoubleClick = [this]
        {
            auto& proc = page.processor();
            if (kind == Kind::Track)
            {
                proc.history.settings ("TRACK " + juce::String (index + 1) + " PAN AUTO");
                proc.withLock ([&] { proc.song.trackPan[index] = -1; });
            }
            else page.edit (juce::String ("SEND ") + (char) ('A' + index) + " PAN CENTRE", false, [this] (MixerSettings& m) { m.ret[index].pan = 128; });
            refresh();
        };
        if (kind != Kind::Master) addAndMakeVisible (pan);

        fader.stereo = kind != Kind::Track;
        fader.onDragStart = [this] { page.processor().history.mixer (nameBtn.getButtonText() + " FADER", true); };
        fader.onChange = [this] (float d)
        {
            auto& proc = page.processor();
            proc.withLock ([&]
            {
                auto& m = proc.song.mixer;
                if (kind == Kind::Track) m.ch[index].volumeDb = d;
                else if (kind == Kind::Return) m.ret[index].volumeDb = d;
                else m.master.volumeDb = d;
            });
            repaint (dbArea.toNearestInt());
        };
        addAndMakeVisible (fader);

        muteBtn.onClick = [this]
        {
            if (kind == Kind::Track) page.toggleMute (index);
            else if (kind == Kind::Return)
                page.edit (juce::String ("SEND ") + (char) ('A' + index) + " MUTE", false, [this] (MixerSettings& m) { m.ret[index].mute = ! m.ret[index].mute; });
            refresh();
        };
        soloBtn.onClick = [this] { page.toggleSolo (index); };
        addAndMakeVisible (nameBtn);
        if (kind == Kind::Track) { addAndMakeVisible (eqBtn); addAndMakeVisible (soloBtn); }
        if (kind != Kind::Master) addAndMakeVisible (muteBtn);
    }

    int slotIndex (int k) const
    {
        return kind == Kind::Track ? trackSlot (index, k) : kind == Kind::Return ? returnSlot (index, k) : masterSlot (k);
    }

    float currentDb() const
    {
        const auto& m = page.processor().song.mixer;
        return kind == Kind::Track ? m.ch[index].volumeDb : kind == Kind::Return ? m.ret[index].volumeDb : m.master.volumeDb;
    }

    void refresh()
    {
        auto& proc = page.processor();
        const auto& m = proc.song.mixer;
        for (int k = 0; k < kInserts; ++k)
        {
            const auto& s = m.slot (slotIndex (k));
            juce::String t = fxShortName (s.type);
            if (s.type == FxType::Plugin)
            {
                // the plug-in's name, cut to fit; "!" when it isn't loaded
                t = (page.processor().pluginLoaded (slotIndex (k)) ? juce::String() : juce::String ("!")) + juce::String (s.pluginName);
                const int fit = std::max (3, (int) ((slotBtn[k].getWidth() - Theme::px * 4) / (Theme::px * PixelFont::kCellW)));
                if (t.length() > fit) t = t.substring (0, fit);
            }
            slotBtn[k].setButtonText (t);
            slotBtn[k].setAlpha (s.type != FxType::None && s.bypass ? 0.45f : 1.0f);
            slotBtn[k].setToggleState (s.active(), juce::dontSendNotification);
        }
        if (kind == Kind::Track)
        {
            const auto& c = m.ch[index];
            eqBtn.setToggleState (c.eq.anyOn(), juce::dontSendNotification);
            // markers for the track's automation knobs
            const bool ka = m.trackAuto[index][0].valid(), kb = m.trackAuto[index][1].valid();
            eqBtn.setButtonText (juce::String ("EQ") + (ka || kb ? "  " : "") + (ka ? "A" : "") + (kb ? "B" : ""));
            for (int s = 0; s < kSends; ++s)
            {
                send[s].label = juce::String::charToString ((juce::juce_wchar) ('A' + s)) + (c.sendPre[s] ? "P" : "");
                send[s].setValue (c.send[s]);
                send[s].repaint();
            }
            const int p = proc.song.trackPan[index];
            pan.showFill = p >= 0;
            pan.setValue (p < 0 ? 128.0f / 255.0f : p / 255.0f);
            pan.repaint();
        }
        else if (kind == Kind::Return)
        {
            pan.setValue (m.ret[index].pan / 255.0f);
            muteBtn.setToggleState (m.ret[index].mute, juce::dontSendNotification);
        }
        fader.setDb (currentDb());
        fader.colour = kind == Kind::Track ? Theme::skin.track[index] : Theme::skin.track[kind == Kind::Return ? 5 + index : 3];
        refreshMutes();
        repaint();
    }

    void refreshMutes()
    {
        if (kind != Kind::Track) return;
        auto& pl = page.processor().player;
        const int nt = page.processor().song.numTracks;
        const bool muted = pl.mute[index].load();
        bool solo = ! muted;
        for (int t = 0; t < nt; ++t) if (t != index && ! pl.mute[t].load()) solo = false;
        if (muteBtn.getToggleState() != muted) muteBtn.setToggleState (muted, juce::dontSendNotification);
        if (soloBtn.getToggleState() != solo) soloBtn.setToggleState (solo, juce::dontSendNotification);
    }

    void tick()
    {
        auto& proc = page.processor();
        auto& mx = proc.player.mixer();
        if (kind == Kind::Track)
        {
            // the mixer's post-fader peak, or the track's level when the mixer is flat (not in use)
            float lvl = mx.trackPeak[index].exchange (0.0f);
            if (lvl == 0.0f && proc.song.mixer.isFlat() && ! proc.player.mute[index].load())
                lvl = proc.player.uiLevel[index].load();
            fader.setLevels (lvl, lvl);
            refreshMutes();
        }
        else if (kind == Kind::Return)
        {
            const float lvl = mx.returnPeak[index].exchange (0.0f);
            fader.setLevels (lvl, lvl);
        }
        else
        {
            mx.masterPeakL.exchange (0.0f); mx.masterPeakR.exchange (0.0f);
            fader.setLevels (proc.player.uiOutL.load(), proc.player.uiOutR.load());
        }
    }

    void paint (juce::Graphics& g) override
    {
        const auto& sk = Theme::skin;
        const float px = Theme::px;
        auto r = getLocalBounds().toFloat();
        g.setColour (Theme::panel);
        g.fillRect (r);
        Theme::bevel (g, r, true, std::max (1.0f, px * 0.5f));
        // colour bar along the top
        g.setColour (fader.colour);
        g.fillRect (r.reduced (px).removeFromTop (px * 2));
        g.setColour (sk.text);
        // dB readout
        g.setColour (sk.bg);
        g.fillRect (dbArea);
        PixelFont::drawCentred (g, FaderMeter::dbText (currentDb()), dbArea, px, sk.text);
        Theme::bevel (g, dbArea, false, std::max (1.0f, px * 0.5f));
        // labels in the rows a return / the master don't use
        for (auto& [area, text] : spare)
            PixelFont::drawCentred (g, text, area, px, Theme::faint);
    }

    void resized() override
    {
        const float px = Theme::px;
        const int rowH = (int) (px * 11);
        const int gap = (int) std::max (2.0f, px);
        auto r = getLocalBounds().reduced ((int) px * 1, (int) px * 1);
        r.removeFromTop ((int) (px * 3));
        spare.clear();
        auto row = [&] { auto x = r.removeFromTop (rowH); r.removeFromTop (gap); return x; };
        auto bottomRow = [&] { auto x = r.removeFromBottom (rowH); r.removeFromBottom (gap); return x; };

        nameBtn.setBounds (row());
        auto eqRow = row();
        if (kind == Kind::Track) eqBtn.setBounds (eqRow);
        else spare.push_back ({ eqRow.toFloat(), kind == Kind::Return ? "RETURN" : "OUT" });
        r.removeFromTop (gap);
        for (auto& b : slotBtn) b.setBounds (row());
        r.removeFromTop (gap);
        for (int s = 0; s < kSends; ++s)
        {
            auto sr = row();
            if (kind == Kind::Track) send[s].setBounds (sr);
        }
        auto panRow = row();
        if (kind != Kind::Master) pan.setBounds (panRow);

        auto ms = bottomRow();
        if (kind == Kind::Track)
        {
            const int w = (ms.getWidth() - gap) / 2;
            muteBtn.setBounds (ms.removeFromLeft (w));
            ms.removeFromLeft (gap);
            soloBtn.setBounds (ms);
        }
        else if (kind == Kind::Return) muteBtn.setBounds (ms);
        dbArea = bottomRow().toFloat();
        fader.setBounds (r);
    }

    MixerPage& page;
    const Kind kind;
    const int index;
    PixButton nameBtn { "" }, eqBtn { "EQ" };
    PixButton slotBtn[kInserts] { PixButton ("---"), PixButton ("---"), PixButton ("---") };
    MiniBar send[kSends] { MiniBar ("A"), MiniBar ("B") };
    MiniBar pan { "PAN" };
    FaderMeter fader;
    PixButton muteBtn { "M", true }, soloBtn { "S", true };
    juce::Rectangle<float> dbArea;
    std::vector<std::pair<juce::Rectangle<float>, juce::String>> spare;
};

// ---------------------------------------------------------------------------
MixerPage::MixerPage (PaulaProcessor& p) : proc (p)
{
    setWantsKeyboardFocus (true);
    setOpaque (true);

    closeBtn.onClick = [this] { if (onClose) onClose(); };
    backBtn.onClick = [this]
    {
        if (currentPage == Page::Effect && effectReturn == Page::Channel) openChannel (chTrack);
        else showStrips();
    };
    resetAllBtn.onClick = [this]
    {
        edit ("RESET MIXER", false, [] (MixerSettings& m) { m = MixerSettings {}; }, true);
        if (onStatus) onStatus ("MIXER RESET - FLAT AGAIN (UNDO TO GET IT BACK)");
    };
    prevBtn.onClick = [this]
    {
        if (currentPage == Page::Channel) openChannel ((chTrack + proc.song.numTracks - 1) % proc.song.numTracks);
        else if (currentPage == Page::Effect) openEffect ((fxSlot + kTotalSlots - 1) % kTotalSlots);
    };
    nextBtn.onClick = [this]
    {
        if (currentPage == Page::Channel) openChannel ((chTrack + 1) % proc.song.numTracks);
        else if (currentPage == Page::Effect) openEffect ((fxSlot + 1) % kTotalSlots);
    };
    for (auto* c : std::initializer_list<juce::Component*> { &closeBtn, &backBtn, &resetAllBtn, &prevBtn, &nextBtn })
        addChildComponent (c);

    // ---- strips
    for (int t = 0; t < kMixTracks; ++t) strips.push_back (std::make_unique<MixStrip> (*this, MixStrip::Kind::Track, t));
    for (int r = 0; r < kReturns; ++r)   strips.push_back (std::make_unique<MixStrip> (*this, MixStrip::Kind::Return, r));
    strips.push_back (std::make_unique<MixStrip> (*this, MixStrip::Kind::Master, 0));
    for (auto& s : strips) addChildComponent (*s);

    // ---- channel page
    for (int b = 0; b < kEqBands; ++b)
    {
        bandFreq[b] = std::make_unique<MiniBar> ("FREQ");
        bandGain[b] = std::make_unique<MiniBar> ("GAIN");
        bandQ[b]    = std::make_unique<MiniBar> ("Q");
        bandGain[b]->bipolar = true;
        const ChannelEq defaults;
        bandFreq[b]->defaultValue = mixmap::freqToSlider (defaults.band[b].freq);
        bandGain[b]->defaultValue = mixmap::gainToSlider (0.0f);
        bandQ[b]->defaultValue = mixmap::qToSlider (defaults.band[b].q);
        bandFreq[b]->format = [] (float v) { return hzText (mixmap::sliderToFreq (v)); };
        bandGain[b]->format = [] (float v) { return dbSigned (mixmap::sliderToGain (v)); };
        bandQ[b]->format = [] (float v) { return juce::String (mixmap::sliderToQ (v), 2); };
        auto label = [this, b] (const char* what) { return "TRACK " + juce::String (chTrack + 1) + " " + kBandNames[b] + " " + what; };
        bandOn[b].onClick = [this, b, label]
        {
            const bool on = bandOn[b].getToggleState();
            edit (label (on ? "ON" : "OFF"), false, [this, b, on] (MixerSettings& m) { m.ch[chTrack].eq.band[b].on = on; });
        };
        // moving a band's control switches the band on
        bandFreq[b]->onDragStart = [this, b, label] { proc.history.mixer (label ("FREQ"), true); };
        bandGain[b]->onDragStart = [this, b, label] { proc.history.mixer (label ("GAIN"), true); };
        bandQ[b]->onDragStart    = [this, b, label] { proc.history.mixer (label ("Q"), true); };
        bandFreq[b]->onChange = [this, b] (float v) { proc.withLock ([&] { auto& e = proc.song.mixer.ch[chTrack].eq.band[b]; e.freq = mixmap::sliderToFreq (v); e.on = true; }); refreshChannel(); };
        bandGain[b]->onChange = [this, b] (float v) { proc.withLock ([&] { auto& e = proc.song.mixer.ch[chTrack].eq.band[b]; e.gain = mixmap::sliderToGain (v); e.on = true; }); refreshChannel(); };
        bandQ[b]->onChange    = [this, b] (float v) { proc.withLock ([&] { auto& e = proc.song.mixer.ch[chTrack].eq.band[b]; e.q = mixmap::sliderToQ (v); e.on = true; }); refreshChannel(); };
        addChildComponent (bandOn[b]);
        addChildComponent (*bandFreq[b]);
        if (b != kEqHighPass) addChildComponent (*bandGain[b]);
        if (b == kEqMid1 || b == kEqMid2) addChildComponent (*bandQ[b]);
    }
    eqFlatBtn.onClick = [this]
    {
        edit ("TRACK " + juce::String (chTrack + 1) + " EQ FLAT", false, [this] (MixerSettings& m) { m.ch[chTrack].eq = ChannelEq {}; });
    };
    addChildComponent (eqFlatBtn);
    for (int k = 0; k < kInserts; ++k)
    {
        chInsert[k].onClick = [this, k]
        {
            const int s = trackSlot (chTrack, k);
            if (juce::ModifierKeys::currentModifiers.isShiftDown()) toggleBypass (s);
            else { openEffect (s); effectReturn = Page::Channel; }
        };
        addChildComponent (chInsert[k]);
    }
    for (int s = 0; s < kSends; ++s)
    {
        chSend[s].labelChars = 7;
        chSend[s].valueChars = 5;
        chSend[s].defaultValue = 0.0f;
        chSend[s].onDragStart = [this, s] { proc.history.mixer ("TRACK " + juce::String (chTrack + 1) + " SEND " + (char) ('A' + s), true); };
        chSend[s].onChange = [this, s] (float v) { proc.withLock ([&] { proc.song.mixer.ch[chTrack].send[s] = v; }); };
        chPre[s].onClick = [this, s]
        {
            const bool pre = chPre[s].getToggleState();
            edit ("TRACK " + juce::String (chTrack + 1) + " SEND " + (char) ('A' + s) + (pre ? " PRE-FADER" : " POST-FADER"), false,
                  [this, s, pre] (MixerSettings& m) { m.ch[chTrack].sendPre[s] = pre; });
        };
        addChildComponent (chSend[s]);
        addChildComponent (chPre[s]);
    }
    chFader.labelChars = 6;
    chFader.valueChars = 7;
    chFader.defaultValue = FaderMeter::dbToPos (0.0f);
    chFader.format = [] (float v) { return FaderMeter::dbText (FaderMeter::posToDb (v)) + (FaderMeter::posToDb (v) > -59.9f ? " DB" : ""); };
    chFader.onDragStart = [this] { proc.history.mixer ("TRK " + juce::String (chTrack + 1) + " FADER", true); };
    chFader.onChange = [this] (float v)
    {
        float d = FaderMeter::posToDb (v);
        if (std::abs (d) < 0.25f) d = 0.0f;
        proc.withLock ([&] { proc.song.mixer.ch[chTrack].volumeDb = d; });
    };
    addChildComponent (chFader);

    // ---- automation
    learnTrackBox.labelChars = 6; learnTrackBox.valueChars = 1;
    learnTrackBox.onChange = [this] (int) { learner.cancel(); refresh(); };
    learnABtn.onClick = [this] { armLearn (0); };
    learnBBtn.onClick = [this] { armLearn (1); };
    chClearA.onClick = [this] { edit ("TRACK " + juce::String (chTrack + 1) + " AUTO A CLEARED", false, [this] (MixerSettings& m) { m.trackAuto[chTrack][0] = AutoTarget {}; }); };
    chClearB.onClick = [this] { edit ("TRACK " + juce::String (chTrack + 1) + " AUTO B CLEARED", false, [this] (MixerSettings& m) { m.trackAuto[chTrack][1] = AutoTarget {}; }); };
    for (auto* c : std::initializer_list<juce::Component*> { &learnABtn, &learnBBtn, &learnTrackBox, &chClearA, &chClearB })
        addChildComponent (c);

    // ---- effect page
    // plug-ins
    pluginBtn.setClickingTogglesState (false);
    pluginBtn.onClick = [this] { showPluginList (true); };
    rescanBtn.onClick = [this] { proc.pluginHost.startScan(); refreshPluginList(); };
    listCloseBtn.onClick = [this] { showPluginList (false); };
    editPluginBtn.onClick = [this]
    {
        if (auto* pe = proc.pluginAt (fxSlot))
            proc.pluginHost.openEditor (pe->instance(), slotOwner (fxSlot) + " INSERT " + juce::String (fxSlot % kInserts + 1) + ": "
                                               + juce::String (proc.song.mixer.slot (fxSlot).pluginName));
        else if (onStatus) onStatus ("THIS PLUG-IN ISN'T LOADED - PICK ANOTHER WITH PLUG-IN...");
    };
    pluginList.getNumRows = [this] { return (int) pluginChoices.size(); };
    pluginList.getRowText = [this] (int r)
    {
        const auto& d = pluginChoices[(size_t) r];
        return d.name.toUpperCase() + "  (" + d.manufacturerName.toUpperCase() + ", " + (d.pluginFormatName == "AudioUnit" ? juce::String ("AU") : d.pluginFormatName.toUpperCase()) + ")";
    };
    pluginList.onClick = [this] (int r, const juce::MouseEvent&) { pluginList.selected = r; pluginList.repaint(); };
    pluginList.onDoubleClick = [this] (int r) { choosePlugin (r); };
    proc.pluginHost.onScanFinished = [this] { refreshPluginList(); if (onStatus) onStatus (proc.pluginHost.scanStatus()); };
    for (auto* c : std::initializer_list<juce::Component*> { &pluginBtn, &rescanBtn, &editPluginBtn, &listCloseBtn, &pluginList })
        addChildComponent (c);

    for (int t = 0; t < kBuiltinFxTypes; ++t)
    {
        typeBtn[t].setClickingTogglesState (false);
        typeBtn[t].onClick = [this, t] { setSlotType (fxSlot, (FxType) t); };
        addChildComponent (typeBtn[t]);
    }
    bypassBtn.onClick = [this]
    {
        // with LEARN armed, BYPASS is learned: the knob then switches the effect on and off
        if (learner.armed) { finishLearn (kAutoOnOff); refresh(); return; }
        toggleBypass (fxSlot);
    };
    defaultsBtn.onClick = [this]
    {
        const FxType type = proc.song.mixer.slot (fxSlot).type;
        if (type == FxType::Plugin)
        {
            // the plug-in starts again from its own defaults
            edit (slotOwner (fxSlot) + " " + juce::String (proc.song.mixer.slot (fxSlot).pluginName) + " DEFAULTS", false,
                  [this] (MixerSettings& m) { m.slot (fxSlot).pluginState.clear(); });
            proc.reloadMixerSlot (fxSlot);
            refresh();
            return;
        }
        edit (slotOwner (fxSlot) + " " + juce::String (fxName (type)) + " DEFAULTS", false,
              [this, type] (MixerSettings& m) { const bool b = m.slot (fxSlot).bypass; initFxSlot (m.slot (fxSlot), type); m.slot (fxSlot).bypass = b; });
    };
    addChildComponent (bypassBtn);
    addChildComponent (defaultsBtn);
    for (int i = 0; i < kFxParams; ++i)
    {
        param[i] = std::make_unique<PixSlider> ("");
        param[i]->labelChars = 9;
        param[i]->valueChars = 10;
        param[i]->onDragStart = [this, i]
        {
            const auto& s = proc.song.mixer.slot (fxSlot);
            const auto& ps = fxParams (s.type);
            if (i < (int) ps.size()) proc.history.mixer (slotOwner (fxSlot) + " " + juce::String (fxName (s.type)) + " " + juce::String (ps[(size_t) i].name), true);
        };
        param[i]->onChange = [this, i] (float v)
        {
            proc.withLock ([&] { proc.song.mixer.slot (fxSlot).p[i] = v; });
            refreshEffect();
        };
        param[i]->format = [this, i] (float v)
        {
            const auto& s = proc.song.mixer.slot (fxSlot);
            float tmp[kFxParams];
            std::copy (s.p, s.p + kFxParams, tmp);
            tmp[i] = v;
            return juce::String (fxParamText (s.type, i, tmp));
        };
        paramToggle[i] = std::make_unique<PixButton> ("", true);
        paramToggle[i]->onClick = [this, i]
        {
            const bool on = paramToggle[i]->getToggleState();
            const auto& s = proc.song.mixer.slot (fxSlot);
            const auto& ps = fxParams (s.type);
            if (i >= (int) ps.size()) return;
            edit (slotOwner (fxSlot) + " " + juce::String (fxName (s.type)) + " " + juce::String (ps[(size_t) i].name) + (on ? " ON" : " OFF"), false,
                  [this, i, on] (MixerSettings& m) { m.slot (fxSlot).p[i] = on ? 1.0f : 0.0f; });
        };
        addChildComponent (*param[i]);
        addChildComponent (*paramToggle[i]);
    }

    showStrips();
}

MixerPage::~MixerPage()
{
    proc.pluginHost.onScanFinished = nullptr;   // (the processor outlives this window)
}

template <typename Fn> void MixerPage::edit (const juce::String& label, bool merge, Fn&& fn, bool effectsChanged)
{
    proc.capturePluginStates();   // so undo brings plug-ins back as they were
    proc.history.mixer (label, merge);
    proc.withLock ([&] { fn (proc.song.mixer); });
    if (effectsChanged) proc.updateMixerEffects();
    refresh();
}

juce::String MixerPage::slotOwner (int slot)
{
    if (slot < kTrackSlots) return "TRACK " + juce::String (slot / kInserts + 1);
    slot -= kTrackSlots;
    if (slot < kReturnSlots) return juce::String ("SEND ") + (char) ('A' + slot / kInserts);
    return "MASTER";
}

void MixerPage::setTrackPan (int track, int pan, bool merge)
{
    proc.history.settings ("TRACK " + juce::String (track + 1) + " PAN", merge);
    proc.withLock ([&] { proc.song.trackPan[track] = pan; });
    refresh();
}

void MixerPage::toggleMute (int track)
{
    auto& m = proc.player.mute[track];
    m = ! m.load();
    refresh();
}

void MixerPage::toggleSolo (int track)
{
    // solo: mute everything else (or unmute all if already soloed) - as on the track headers
    auto& pl = proc.player;
    bool soloed = ! pl.mute[track].load();
    for (int t = 0; t < kMaxTracks; ++t) if (t != track && ! pl.mute[t].load()) soloed = false;
    for (int t = 0; t < kMaxTracks; ++t) pl.mute[t] = soloed ? false : (t != track);
    refresh();
}

void MixerPage::toggleBypass (int slot)
{
    const auto& s = proc.song.mixer.slot (slot);
    if (s.type == FxType::None) { if (onStatus) onStatus ("EMPTY SLOT - CLICK IT TO PICK AN EFFECT"); return; }
    const bool by = ! s.bypass;
    proc.withLock ([&] { proc.player.mixer().clearOnOff (slot); });   // (the button wins over V / J switching)
    edit (slotOwner (slot) + " " + juce::String (fxName (s.type)) + (by ? " BYPASSED" : " ON"), false,
          [slot, by] (MixerSettings& m) { m.slot (slot).bypass = by; });
}

void MixerPage::setSlotType (int slot, FxType t)
{
    const auto& s = proc.song.mixer.slot (slot);
    browsingPlugins = false;
    if (s.type == t) { resized(); refresh(); return; }
    edit (slotOwner (slot) + " INSERT " + juce::String (slot % kInserts + 1) + " " + juce::String (fxName (t)), false,
          [slot, t] (MixerSettings& m) { initFxSlot (m.slot (slot), t); }, true);
    resized();
    refresh();
}

// ---------------------------------------------------------------------------
void MixerPage::opened()
{
    showStrips();
    grabKeyboardFocus();
}

void MixerPage::showStrips()
{
    currentPage = Page::Strips;
    effectReturn = Page::Strips;
    updateVisibility();
    refresh();
}

void MixerPage::openChannel (int track)
{
    chTrack = juce::jlimit (0, std::max (0, proc.song.numTracks - 1), track);
    currentPage = Page::Channel;
    updateVisibility();
    refresh();
}

void MixerPage::openEffect (int slot)
{
    fxSlot = juce::jlimit (0, kTotalSlots - 1, slot);
    browsingPlugins = false;
    // LEARN is for the slot's own track, or the pattern's current track for returns / the master
    learnTrackBox.setValue ((fxSlot < kTrackSlots ? fxSlot / kInserts : proc.editorState.track) + 1);
    // skip the inserts of tracks the song doesn't have
    if (fxSlot < kTrackSlots && fxSlot / kInserts >= proc.song.numTracks)
        fxSlot = slot > fxSlot ? kTrackSlots : trackSlot (proc.song.numTracks - 1, kInserts - 1);
    effectReturn = currentPage == Page::Channel ? Page::Channel : (currentPage == Page::Effect ? effectReturn : Page::Strips);
    currentPage = Page::Effect;
    updateVisibility();
    refresh();
}

void MixerPage::updateVisibility()
{
    const bool strip = currentPage == Page::Strips, ch = currentPage == Page::Channel, fx = currentPage == Page::Effect;
    closeBtn.setVisible (true);
    resetAllBtn.setVisible (strip);
    backBtn.setVisible (! strip);
    prevBtn.setVisible (! strip);
    nextBtn.setVisible (! strip);
    const int nt = proc.song.numTracks;
    for (int i = 0; i < (int) strips.size(); ++i)
        strips[(size_t) i]->setVisible (strip && (i >= kMixTracks || i < nt));
    for (int b = 0; b < kEqBands; ++b)
    {
        bandOn[b].setVisible (ch);
        bandFreq[b]->setVisible (ch);
        bandGain[b]->setVisible (ch && b != kEqHighPass);
        bandQ[b]->setVisible (ch && (b == kEqMid1 || b == kEqMid2));
    }
    eqFlatBtn.setVisible (ch);
    for (auto& b : chInsert) b.setVisible (ch);
    for (auto& s : chSend) s.setVisible (ch);
    for (auto& b : chPre) b.setVisible (ch);
    chFader.setVisible (ch);
    chClearA.setVisible (ch); chClearB.setVisible (ch);
    learner.cancel();
    for (auto& b : typeBtn) b.setVisible (fx);
    for (auto* c : std::initializer_list<juce::Component*> { &pluginBtn, &rescanBtn, &editPluginBtn, &listCloseBtn, &pluginList,
                                                             &learnTrackBox, &learnABtn, &learnBBtn })
        c->setVisible (false);   // (the effect page shows the ones it needs)
    bypassBtn.setVisible (fx);
    defaultsBtn.setVisible (fx);
    for (int i = 0; i < kFxParams; ++i) { param[i]->setVisible (false); paramToggle[i]->setVisible (false); }
    resized();
    repaint();
}

void MixerPage::refresh()
{
    // the track count may have changed
    const int nt = proc.song.numTracks;
    if (currentPage == Page::Strips)
        for (int i = 0; i < kMixTracks; ++i)
            if (strips[(size_t) i]->isVisible() != (i < nt)) { updateVisibility(); break; }
    if (currentPage == Page::Channel && chTrack >= nt) chTrack = 0;

    for (auto& s : strips) if (s->isVisible()) s->refresh();
    if (currentPage == Page::Channel) refreshChannel();
    if (currentPage == Page::Effect) refreshEffect();
    repaint();
}

void MixerPage::refreshChannel()
{
    const auto& c = proc.song.mixer.ch[chTrack];
    for (int b = 0; b < kEqBands; ++b)
    {
        const auto& e = c.eq.band[b];
        bandOn[b].setToggleState (e.on, juce::dontSendNotification);
        bandFreq[b]->setValue (mixmap::freqToSlider (e.freq));
        bandGain[b]->setValue (mixmap::gainToSlider (e.gain));
        bandQ[b]->setValue (mixmap::qToSlider (e.q));
        for (auto* s : { bandFreq[b].get(), bandGain[b].get(), bandQ[b].get() }) s->setAlpha (e.on ? 1.0f : 0.5f);
    }
    for (int k = 0; k < kInserts; ++k)
    {
        const auto& s = c.inserts[k];
        chInsert[k].setButtonText (juce::String (k + 1) + ": " + (s.type == FxType::None ? juce::String ("EMPTY")
                                   : s.type == FxType::Plugin ? juce::String (s.pluginName) : juce::String (fxName (s.type)))
                                   + (s.type != FxType::None && s.bypass ? " (OFF)" : ""));
        chInsert[k].setToggleState (s.active(), juce::dontSendNotification);
    }
    for (int s = 0; s < kSends; ++s)
    {
        chSend[s].setValue (c.send[s]);
        chPre[s].setToggleState (c.sendPre[s], juce::dontSendNotification);
    }
    chFader.setValue (FaderMeter::dbToPos (c.volumeDb));
    repaint (eqGraph.toNearestInt().expanded (4));
}

void MixerPage::refreshEffect()
{
    const auto& s = proc.song.mixer.slot (fxSlot);
    for (int t = 0; t < kBuiltinFxTypes; ++t) typeBtn[t].setToggleState ((int) s.type == t && ! browsingPlugins, juce::dontSendNotification);
    const bool isPlugin = s.type == FxType::Plugin;
    const bool fx = currentPage == Page::Effect;
    pluginBtn.setToggleState (isPlugin || browsingPlugins, juce::dontSendNotification);
    pluginBtn.setVisible (fx);
    const bool learnable = fx && ! browsingPlugins && s.type != FxType::None;
    learnTrackBox.setVisible (learnable); learnABtn.setVisible (learnable); learnBBtn.setVisible (learnable);
    learnABtn.setToggleState (learner.armed && learner.what == 0, juce::dontSendNotification);
    learnBBtn.setToggleState (learner.armed && learner.what == 1, juce::dontSendNotification);
    pluginList.setVisible (fx && browsingPlugins);
    rescanBtn.setVisible (fx && browsingPlugins);
    listCloseBtn.setVisible (fx && browsingPlugins);
    editPluginBtn.setVisible (fx && isPlugin && ! browsingPlugins);
    editPluginBtn.setEnabled (proc.pluginLoaded (fxSlot));
    bypassBtn.setToggleState (s.bypass, juce::dontSendNotification);
    bypassBtn.setEnabled (s.type != FxType::None);
    defaultsBtn.setEnabled (s.type != FxType::None);
    const auto& ps = fxParams (s.type);
    bool layoutChanged = false;
    for (int i = 0; i < kFxParams; ++i)
    {
        const bool has = i < (int) ps.size() && currentPage == Page::Effect && ! browsingPlugins;
        const bool tog = has && ps[(size_t) i].toggle;
        if (param[i]->isVisible() != (has && ! tog) || paramToggle[i]->isVisible() != tog) layoutChanged = true;
        param[i]->setVisible (has && ! tog);
        paramToggle[i]->setVisible (tog);
        if (! has) continue;
        if (tog)
        {
            paramToggle[i]->setButtonText (juce::String (ps[(size_t) i].name) + ": " + (s.p[i] > 0.5f ? "ON" : "OFF"));
            paramToggle[i]->setToggleState (s.p[i] > 0.5f, juce::dontSendNotification);
        }
        else
        {
            param[i]->setLabel (ps[(size_t) i].name);
            param[i]->defaultValue = ps[(size_t) i].def;
            param[i]->setValue (s.p[i]);
            param[i]->repaint();
        }
        param[i]->setAlpha (s.bypass ? 0.5f : 1.0f);
        paramToggle[i]->setAlpha (s.bypass ? 0.5f : 1.0f);
    }
    if (layoutChanged) resized();
    repaint();
}

// ---------------------------------------------------------------- plug-ins
void MixerPage::showPluginList (bool show)
{
    browsingPlugins = show;
    if (show)
    {
        refreshPluginList();
        // first time: look for plug-ins
        if (pluginChoices.empty() && ! proc.pluginHost.isScanning()) proc.pluginHost.startScan();
    }
    resized();
    refreshEffect();
}

void MixerPage::refreshPluginList()
{
    pluginChoices = proc.pluginHost.effects();
    const auto& s = proc.song.mixer.slot (fxSlot);
    pluginList.selected = 0;
    for (int i = 0; i < (int) pluginChoices.size(); ++i)
        if (prv::PluginHost::keyFor (pluginChoices[(size_t) i]) == s.pluginKey) pluginList.selected = i;
    pluginList.ensureVisible (pluginList.selected);
    pluginList.repaint();
    repaint();
}

void MixerPage::choosePlugin (int row)
{
    if (row < 0 || row >= (int) pluginChoices.size()) return;
    const auto d = pluginChoices[(size_t) row];
    const std::string key = prv::PluginHost::keyFor (d);
    const std::string name = d.name.toUpperCase().toStdString();
    browsingPlugins = false;
    edit (slotOwner (fxSlot) + " INSERT " + juce::String (fxSlot % kInserts + 1) + " " + juce::String (name), false,
          [this, key, name] (MixerSettings& m)
          {
              auto& sl = m.slot (fxSlot);
              sl = FxSlot {};
              sl.type = FxType::Plugin;
              sl.pluginKey = key;
              sl.pluginName = name;
          }, true);
    resized();
    refreshEffect();
    if (onStatus)
    {
        if (proc.pluginLoaded (fxSlot)) onStatus (juce::String (name) + " LOADED - OPEN PLUG-IN WINDOW TO EDIT IT");
        else onStatus (juce::String (name) + ": " + proc.pluginHost.lastError (key));
    }
}

// ---------------------------------------------------------------- automation LEARN
std::vector<float> MixerPage::slotParamValues (int slot)
{
    const auto& s = proc.song.mixer.slot (slot);
    if (s.type == FxType::Plugin)
    {
        if (auto* pe = proc.pluginAt (slot)) return prv::PluginHost::parameterValues (pe->instance());
        return {};
    }
    return std::vector<float> (s.p, s.p + kFxParams);
}

juce::String MixerPage::slotParamName (int slot, int param)
{
    const auto& s = proc.song.mixer.slot (slot);
    if (param == kAutoOnOff)
        return (s.type == FxType::Plugin ? juce::String (s.pluginName) : slotOwner (slot) + " " + juce::String (fxName (s.type))) + ": ON/OFF";
    if (s.type == FxType::Plugin)
    {
        auto* pe = proc.pluginAt (slot);
        return juce::String (s.pluginName) + ": " + (pe ? prv::PluginHost::parameterName (pe->instance(), param) : "#" + juce::String (param));
    }
    const auto& ps = fxParams (s.type);
    return slotOwner (slot) + " " + juce::String (fxName (s.type)) + ": " + (param < (int) ps.size() ? juce::String (ps[(size_t) param].name) : juce::String (param));
}

void MixerPage::armLearn (int knob)
{
    if (learner.armed && learner.what == knob) { learner.cancel(); refresh(); if (onStatus) onStatus ("LEARN CANCELLED"); return; }
    const auto& s = proc.song.mixer.slot (fxSlot);
    if (s.type == FxType::None) { if (onStatus) onStatus ("PICK AN EFFECT FIRST"); refresh(); return; }
    if (s.type == FxType::Plugin && ! proc.pluginLoaded (fxSlot)) { if (onStatus) onStatus ("THE PLUG-IN ISN'T LOADED"); refresh(); return; }
    const int slot = fxSlot;
    learner.arm (knob, [this, slot] { return slotParamValues (slot); });
    if (s.type == FxType::Plugin) editPluginBtn.onClick();   // its window, to move a control in
    if (onStatus) onStatus ("NOW MOVE A CONTROL " + juce::String (s.type == FxType::Plugin ? "IN THE PLUG-IN'S WINDOW" : "ON THIS PAGE") + " FOR TRACK "
                            + juce::String (learnTrackBox.getValue()) + (knob == 0 ? " AUTO A (V)" : " AUTO B (J)") + ", OR CLICK BYPASS TO SWITCH THE EFFECT ON/OFF - LEARN AGAIN CANCELS");
    refresh();
}

void MixerPage::finishLearn (int param)
{
    const int knob = learner.what, tr = learnTrackBox.getValue() - 1;
    const juce::String name = slotParamName (fxSlot, param);
    const AutoTarget t { fxSlot, param, name.toStdString() };
    learner.cancel();
    edit ("TRACK " + juce::String (tr + 1) + (knob == 0 ? " AUTO A = " : " AUTO B = ") + name, false,
          [tr, knob, t] (MixerSettings& m) { m.trackAuto[tr][knob] = t; });
    if (onStatus) onStatus ("TRACK " + juce::String (tr + 1) + (knob == 0 ? " AUTO A (VXX) NOW MOVES " : " AUTO B (JXX) NOW MOVES ") + name
                            + (param == kAutoOnOff ? juce::String (" (00 = OFF, 01-FF = ON)") : juce::String()));
}

void MixerPage::tick()
{
    if (proc.player.latencySamples() != shownDelay) { shownDelay = proc.player.latencySamples(); repaint (titleArea.toNearestInt()); }
    if (learner.armed && currentPage == Page::Effect)
    {
        const int moved = learner.poll();
        if (moved >= 0) finishLearn (moved);
    }
    if (currentPage == Page::Effect && browsingPlugins)
    {
        const auto st = proc.pluginHost.scanStatus();
        if (st != lastScanStatus) { lastScanStatus = st; repaint (listStatusArea.toNearestInt()); }
        if (proc.pluginHost.isScanning() && (int) proc.pluginHost.effects().size() != (int) pluginChoices.size()) refreshPluginList();
    }
    if (currentPage == Page::Strips)
    {
        for (auto& s : strips) if (s->isVisible()) s->tick();
    }
    else
    {
        // keep the meters from holding stale peaks while another page is up
        auto& mx = proc.player.mixer();
        for (auto& p : mx.trackPeak) p.exchange (0.0f);
        for (auto& p : mx.returnPeak) p.exchange (0.0f);
        mx.masterPeakL.exchange (0.0f); mx.masterPeakR.exchange (0.0f);
    }
}

bool MixerPage::keyPressed (const juce::KeyPress& k)
{
    if (k == juce::KeyPress::escapeKey)
    {
        if (currentPage == Page::Strips) { if (onClose) onClose(); }
        else backBtn.onClick();
        return true;
    }
    if (k.getKeyCode() == juce::KeyPress::spaceKey)
    {
        if (onPlayStop) onPlayStop (k.getModifiers().isShiftDown());
        return true;
    }
    if (currentPage != Page::Strips)
    {
        if (k == juce::KeyPress::leftKey)  { prevBtn.onClick(); return true; }
        if (k == juce::KeyPress::rightKey) { nextBtn.onClick(); return true; }
    }
    const auto mods = k.getModifiers();
    if (mods.isCommandDown() || mods.isCtrlDown() || mods.isAltDown()) return false;   // undo etc. go to the main window

    // the computer keyboard plays the current instrument (same keys as the pattern)
    auto& st = proc.editorState;
    const auto ch = juce::CharacterFunctions::toLowerCase (k.getTextCharacter());
    if (ch == '[' || ch == ']')
    {
        st.octave = juce::jlimit (0, 6, st.octave + (ch == ']' ? 1 : -1));
        if (onStatus) onStatus ("OCTAVE " + juce::String (st.octave));
        if (onOctave) onOctave();
        return true;
    }
    if (ch == '-' || ch == '=')
    {
        // previous / next instrument, as in the pattern
        st.instrument = juce::jlimit (0, kMaxInstruments - 1, st.instrument + (ch == '=' ? 1 : -1));
        proc.jamInstrument = st.instrument;
        if (onOctave) onOctave();
        return true;
    }
    static const char* lower = "zsxdcvgbhnjm,l.;/";
    static const char* upper = "q2w3er5t6y7ui9o0p";
    int note = -1;
    for (int i = 0; lower[i]; ++i) if (ch == (juce::juce_wchar) lower[i]) note = st.octave * 12 + i + 1;
    for (int i = 0; upper[i]; ++i) if (ch == (juce::juce_wchar) upper[i]) note = (st.octave + 1) * 12 + i + 1;
    if (note <= 0) return false;
    note = juce::jlimit (1, (int) kNoteMax, note);
    if (heldKeys.count (k.getKeyCode()) == 0)
    {
        heldKeys[k.getKeyCode()] = note;
        proc.withLock ([&] { proc.player.jamNoteOn (note, st.instrument); });
    }
    return true;
}

bool MixerPage::keyStateChanged (bool)
{
    for (auto it = heldKeys.begin(); it != heldKeys.end();)
    {
        if (! juce::KeyPress::isKeyCurrentlyDown (it->first))
        {
            const int n = it->second;
            proc.withLock ([&] { proc.player.jamNoteOff (n); });
            it = heldKeys.erase (it);
        }
        else ++it;
    }
    return false;
}

void MixerPage::releaseNotes()
{
    for (const auto& kv : heldKeys)
    {
        const int n = kv.second;
        proc.withLock ([&] { proc.player.jamNoteOff (n); });
    }
    heldKeys.clear();
}

// ---------------------------------------------------------------------------
juce::String MixerPage::delayText() const
{
    // plug-in delay compensation: how late everything comes out
    const int lat = proc.player.latencySamples();
    if (lat <= 0) return {};
    const double sr = proc.getSampleRate() > 0 ? proc.getSampleRate() : 48000.0;
    return "   DELAY " + juce::String (lat * 1000.0 / sr, 1) + " MS (" + juce::String (lat) + ")";
}

juce::String MixerPage::pageTitle() const
{
    switch (currentPage)
    {
        case Page::Channel: return "MIXER - TRACK " + juce::String (chTrack + 1) + " CHANNEL";
        case Page::Effect:  return "MIXER - " + slotOwner (fxSlot) + " INSERT " + juce::String (fxSlot % kInserts + 1);
        default:
        {
            // what the computer keyboard plays here
            const int i = proc.editorState.instrument;
            const auto& in = proc.song.instruments[(size_t) juce::jlimit (0, kMaxInstruments - 1, i)];
            return "MIXER   KEYS PLAY " + juce::String::toHexString (i + 1).toUpperCase().paddedLeft ('0', 2) + " "
                 + juce::String (in.name).toUpperCase() + "  OCT " + juce::String (proc.editorState.octave)
                 + delayText();
        }
    }
}

juce::String MixerPage::effectInfo (FxType t)
{
    switch (t)
    {
        case FxType::Filter:     return "RESONANT FILTER: LOW, BAND OR HIGH-PASS";
        case FxType::Drive:      return "DRIVE, BIT CRUSH AND SAMPLE-RATE REDUCTION";
        case FxType::Compressor: return "COMPRESSOR - MIX BELOW 100% FOR PARALLEL SQUASH";
        case FxType::Delay:      return "STEREO DELAY - SYNC LOCKS TIME TO THE SONG TEMPO";
        case FxType::Reverb:     return "REVERB - ON A SEND, KEEP MIX AT 100%";
        case FxType::Chorus:     return "STEREO CHORUS";
        case FxType::Plugin:     return "VST3 / AU PLUG-IN - ITS OWN WINDOW HAS THE CONTROLS";
        default:                 return "EMPTY SLOT - PICK AN EFFECT ABOVE";
    }
}

void MixerPage::paint (juce::Graphics& g)
{
    const auto& sk = Theme::skin;
    const float px = Theme::px;
    g.fillAll (Theme::panel);
    PixelFont::drawLeft (g, pageTitle(), titleArea, px, Theme::ink);

    if (currentPage == Page::Strips)
    {
        // hint under the strips
        return;
    }
    if (currentPage == Page::Channel)
    {
        Theme::panelBox (g, bandBox, "EQ");
        Theme::panelBox (g, chInsertsBox, "INSERTS");
        Theme::panelBox (g, chSendsBox, "SENDS AND FADER");
        Theme::panelBox (g, chAutoBox, "AUTOMATION KNOBS");
        {
            auto fitT = [px] (juce::String t, float w) {
                const int n0 = std::max (4, (int) (w / (px * PixelFont::kCellW)));
                if (t.length() > n0 && t.contains (": ") && t.indexOf (": ") != t.lastIndexOf (": "))   // "A (V): PLUG: PARAM" -> "A (V): PARAM"
                    t = t.upToFirstOccurrenceOf (": ", true, false) + t.fromLastOccurrenceOf (": ", false, false);
                const int n = std::max (4, (int) (w / (px * PixelFont::kCellW))); return t.length() > n ? t.substring (0, n - 2) + ".." : t; };
            const auto& a = proc.song.mixer.trackAuto[chTrack][0];
            const auto& b = proc.song.mixer.trackAuto[chTrack][1];
            PixelFont::drawLeft (g, fitT ("A (V): " + (a.valid() ? juce::String (a.name) : juce::String ("NOT SET")), chAutoA.getWidth() - px * 2), chAutoA.withTrimmedLeft (px), px, a.valid() ? Theme::ink : Theme::faint);
            PixelFont::drawLeft (g, fitT ("B (J): " + (b.valid() ? juce::String (b.name) : juce::String ("NOT SET")), chAutoB.getWidth() - px * 2), chAutoB.withTrimmedLeft (px), px, b.valid() ? Theme::ink : Theme::faint);
        }
        paintEqGraph (g, eqGraph);
        return;
    }
    // effect page: parameter names next to the controls are in the sliders; info line
    const auto& s = proc.song.mixer.slot (fxSlot);
    g.setColour (sk.bg);
    g.fillRect (fxInfoArea);
    Theme::bevel (g, fxInfoArea, false, std::max (1.0f, px * 0.5f));
    auto fit = [px] (juce::String t, float w) { const int n = std::max (4, (int) (w / (px * PixelFont::kCellW))); return t.length() > n ? t.substring (0, n - 2) + ".." : t; };
    juce::String info = browsingPlugins ? juce::String ("DOUBLE-CLICK A PLUG-IN TO LOAD IT") : effectInfo (s.type);
    PixelFont::drawCentred (g, fit (info, fxInfoArea.getWidth() - px * 4), fxInfoArea, px, sk.text);

    if (! browsingPlugins && s.type != FxType::None)
    {
        auto fitL = [px] (juce::String t, float w) { const int n = std::max (4, (int) (w / (px * PixelFont::kCellW))); return t.length() > n ? t.substring (0, n - 2) + ".." : t; };
        const int tr = learnTrackBox.getValue() - 1;
        const auto& a = proc.song.mixer.trackAuto[tr][0];
        const auto& b = proc.song.mixer.trackAuto[tr][1];
        const juce::String t = learner.armed ? juce::String ("MOVE A CONTROL, OR CLICK BYPASS")
                             : [&] {
                                   const int room = (int) (learnInfoArea.getWidth() / (px * PixelFont::kCellW)) / 2 - 4;
                                   return "A: " + (a.valid() ? shortTargetName (a.name, room) : juce::String ("-"))
                                        + "  B: " + (b.valid() ? shortTargetName (b.name, room) : juce::String ("-")); }();
        PixelFont::drawLeft (g, fitL (t, learnInfoArea.getWidth()), learnInfoArea, px, Theme::ink);
    }
    if (browsingPlugins)
    {
        const auto st = proc.pluginHost.scanStatus();
        const juce::String t = proc.pluginHost.isScanning() || st.isNotEmpty() ? st
                             : juce::String ((int) pluginChoices.size()) + " EFFECT PLUG-INS (RESCAN AFTER INSTALLING MORE)";
        PixelFont::drawLeft (g, fit (t, listStatusArea.getWidth()), listStatusArea, px, Theme::ink);
    }
    else if (s.type == FxType::Plugin)
    {
        // which plug-in, and whether it's running
        auto r = pluginInfoArea;
        const float lh = px * 11;
        juce::PluginDescription d;
        const bool known = proc.pluginHost.findDescription (s.pluginKey, d);
        auto* pe = proc.pluginAt (fxSlot);
        juce::StringArray lines;
        lines.add (juce::String (s.pluginName));
        if (known) lines.add ("BY " + d.manufacturerName.toUpperCase() + "  -  " + (d.pluginFormatName == "AudioUnit" ? juce::String ("AU") : d.pluginFormatName.toUpperCase()));
        if (pe != nullptr)
        {
            const int lat = pe->instance().getLatencySamples();
            const double sr = proc.getSampleRate() > 0 ? proc.getSampleRate() : 48000.0;
            lines.add (lat > 0 ? "LATENCY " + juce::String (lat * 1000.0 / sr, 1) + " MS - COMPENSATED" : juce::String ("NO LATENCY"));
        }
        else
        {
            const auto err = proc.pluginHost.lastError (s.pluginKey);
            lines.add ("MISSING: " + (err.isNotEmpty() ? err : juce::String ("NOT LOADED")));
            lines.add ("AUDIO PASSES THROUGH; ITS SETTINGS ARE KEPT IN THE SONG");
        }
        for (auto& l : lines)
        {
            auto row = r.removeFromTop (lh);
            PixelFont::drawLeft (g, fit (l, row.getWidth()), row, px, (&l == &lines.getReference (0)) ? Theme::ink : Theme::ink);
        }
    }
}

void MixerPage::paintEqGraph (juce::Graphics& g, juce::Rectangle<float> r)
{
    const auto& sk = Theme::skin;
    const float px = Theme::px;
    g.setColour (sk.bg);
    g.fillRect (r);
    auto in = r.reduced (px * 2);
    auto xOf = [&] (double hz) { return in.getX() + in.getWidth() * (float) (std::log10 (hz / 20.0) / 3.0); };
    auto yOf = [&] (double db) { return in.getCentreY() - in.getHeight() * 0.5f * (float) (juce::jlimit (-20.0, 20.0, db) / 20.0); };
    // grid
    g.setColour (sk.rule);
    for (double hz : { 50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0 })
        g.fillRect (xOf (hz), in.getY(), 1.0f, in.getHeight());
    for (double db : { -12.0, -6.0, 6.0, 12.0 })
        g.fillRect (in.getX(), yOf (db), in.getWidth(), 1.0f);
    g.setColour (sk.dim);
    g.fillRect (in.getX(), yOf (0.0), in.getWidth(), std::max (1.0f, px * 0.5f));
    for (auto [hz, t] : { std::pair<double, const char*> { 100.0, "100" }, { 1000.0, "1K" }, { 10000.0, "10K" } })
        PixelFont::draw (g, t, xOf (hz) + px, in.getBottom() - px * 8, px, sk.dim);
    PixelFont::draw (g, "+12", in.getX() + px, yOf (12.0) - px * 8.5f, px, sk.dim);
    PixelFont::draw (g, "-12", in.getX() + px, yOf (-12.0) + px * 1.0f, px, sk.dim);

    // the response
    const auto& eq = proc.song.mixer.ch[chTrack].eq;
    const double sr = 48000.0;
    juce::Path path;
    const int steps = std::max (32, (int) in.getWidth() / 2);
    for (int i = 0; i <= steps; ++i)
    {
        const double hz = 20.0 * std::pow (1000.0, (double) i / steps);
        const double db = eq.anyOn() ? ChannelEqProcessor::responseDb (eq, std::min (hz, sr * 0.49), sr) : 0.0;
        const float x = in.getX() + in.getWidth() * (float) i / (float) steps, y = yOf (db);
        if (i == 0) path.startNewSubPath (x, y); else path.lineTo (x, y);
    }
    g.setColour (sk.track[chTrack]);
    g.strokePath (path, juce::PathStrokeType (std::max (1.5f, px)));
    // band handles
    for (int b = 0; b < kEqBands; ++b)
    {
        const auto& band = eq.band[b];
        if (! band.on) continue;
        const double db = ChannelEqProcessor::responseDb (eq, std::min ((double) band.freq, sr * 0.49), sr);
        auto dot = juce::Rectangle<float> (px * 3, px * 3).withCentre ({ xOf (band.freq), yOf (db) });
        g.setColour (sk.text);
        g.fillRect (dot);
    }
    Theme::bevel (g, r, false, std::max (1.0f, px * 0.5f));
}

void MixerPage::resized()
{
    const float px = Theme::px;
    const int rowH = (int) (px * 11);
    const int gap = (int) std::max (2.0f, px * 1.5f);
    auto charsW = [px] (int chars) { return (int) (px * 6 * chars + px * 4); };
    auto r = getLocalBounds().reduced (gap);

    auto head = r.removeFromTop (rowH);
    closeBtn.setBounds (head.removeFromRight (charsW (5))); head.removeFromRight (gap);
    if (currentPage == Page::Strips) { resetAllBtn.setBounds (head.removeFromRight (charsW (11))); head.removeFromRight (gap); }
    else
    {
        backBtn.setBounds (head.removeFromRight (charsW (4))); head.removeFromRight (gap * 3);
        nextBtn.setBounds (head.removeFromRight (charsW (1) + gap)); head.removeFromRight (gap);
        prevBtn.setBounds (head.removeFromRight (charsW (1) + gap)); head.removeFromRight (gap);
    }
    titleArea = head.toFloat();
    r.removeFromTop (gap);

    if (currentPage == Page::Strips) layoutStrips (r);
    else if (currentPage == Page::Channel) layoutChannel (r);
    else layoutEffect (r);
}

void MixerPage::layoutStrips (juce::Rectangle<int> r)
{
    const float px = Theme::px;
    const int gap = (int) std::max (2.0f, px);
    std::vector<MixStrip*> shown;
    for (auto& s : strips) if (s->isVisible()) shown.push_back (s.get());
    if (shown.empty()) return;
    // a little space between the tracks, the returns and the master
    const int groups = 2;
    const int n = (int) shown.size();
    const int w = (r.getWidth() - gap * (n - 1) - gap * 3 * groups) / n;
    int x = r.getX();
    for (auto* s : shown)
    {
        if (s->kind != MixStrip::Kind::Track && s == shown[(size_t) (n - 3)]) x += gap * 3;   // before the returns
        if (s->kind == MixStrip::Kind::Master) x += gap * 3;
        s->setBounds (x, r.getY(), w, r.getHeight());
        x += w + gap;
    }
}

void MixerPage::layoutChannel (juce::Rectangle<int> r)
{
    const float px = Theme::px;
    const int rowH = (int) (px * 11);
    const int gap = (int) std::max (2.0f, px * 1.5f);
    const int titleH = (int) (px * 10);
    auto charsW = [px] (int chars) { return (int) (px * 6 * chars + px * 4); };

    // right column: inserts, sends, fader
    auto right = r.removeFromRight (std::min (charsW (30), r.getWidth() / 3));
    r.removeFromRight (gap * 2);
    chInsertsBox = right.removeFromTop (titleH + (rowH + gap) * kInserts + gap * 2).toFloat();
    {
        auto in = chInsertsBox.toNearestInt().reduced (gap);
        in.removeFromTop (titleH);
        for (auto& b : chInsert) { b.setBounds (in.removeFromTop (rowH)); in.removeFromTop (gap); }
    }
    right.removeFromTop (gap * 2);
    chSendsBox = right.removeFromTop (titleH + (rowH + gap) * 3 + gap * 2).toFloat();
    {
        auto in = chSendsBox.toNearestInt().reduced (gap);
        in.removeFromTop (titleH);
        for (int s = 0; s < kSends; ++s)
        {
            auto row = in.removeFromTop (rowH); in.removeFromTop (gap);
            chPre[s].setBounds (row.removeFromRight (charsW (3)));
            row.removeFromRight (gap);
            chSend[s].setBounds (row);
        }
        chFader.setBounds (in.removeFromTop (rowH));
    }
    right.removeFromTop (gap * 2);
    chAutoBox = right.removeFromTop (titleH + (rowH + gap) * 2 + gap * 2).toFloat();
    {
        auto in = chAutoBox.toNearestInt().reduced (gap);
        in.removeFromTop (titleH);
        auto ra = in.removeFromTop (rowH); in.removeFromTop (gap);
        chClearA.setBounds (ra.removeFromRight (charsW (5))); chAutoA = ra.toFloat();
        auto rb = in.removeFromTop (rowH);
        chClearB.setBounds (rb.removeFromRight (charsW (5))); chAutoB = rb.toFloat();
    }

    // left: graph on top, then one row per band
    const int bandsH = titleH + (rowH + gap) * (kEqBands + 1) + gap * 2;
    auto graph = r.removeFromTop (std::max (rowH * 3, r.getHeight() - bandsH - gap * 2));
    eqGraph = graph.toFloat();
    r.removeFromTop (gap * 2);
    bandBox = r.removeFromTop (bandsH).toFloat();
    auto in = bandBox.toNearestInt().reduced (gap);
    in.removeFromTop (titleH);
    const int onW = charsW (10);
    for (int b = 0; b < kEqBands; ++b)
    {
        auto row = in.removeFromTop (rowH); in.removeFromTop (gap);
        bandOn[b].setBounds (row.removeFromLeft (onW)); row.removeFromLeft (gap * 2);
        const int qW = charsW (7);
        const int w = (row.getWidth() - qW - gap * 4) / 2;
        bandFreq[b]->setBounds (row.removeFromLeft (w)); row.removeFromLeft (gap * 2);
        bandGain[b]->setBounds (row.removeFromLeft (w)); row.removeFromLeft (gap * 2);
        bandQ[b]->setBounds (row);
    }
    eqFlatBtn.setBounds (in.removeFromTop (rowH).removeFromLeft (onW));
}

void MixerPage::layoutEffect (juce::Rectangle<int> r)
{
    const float px = Theme::px;
    const int rowH = (int) (px * 11);
    const int bigH = (int) (px * 15);
    const int gap = (int) std::max (2.0f, px * 1.5f);
    auto charsW = [px] (int chars) { return (int) (px * 6 * chars + px * 4); };

    auto types = r.removeFromTop (rowH);
    const int n = kBuiltinFxTypes;
    const int tw = std::min (charsW (8), (types.getWidth() - charsW (10) - gap * n) / n);
    for (int t = 0; t < n; ++t) { typeBtn[t].setBounds (types.removeFromLeft (tw)); types.removeFromLeft (gap); }
    pluginBtn.setBounds (types.removeFromLeft (charsW (10)));
    r.removeFromTop (gap * 2);
    auto row2 = r.removeFromTop (rowH);
    bypassBtn.setBounds (row2.removeFromLeft (charsW (8))); row2.removeFromLeft (gap);
    defaultsBtn.setBounds (row2.removeFromLeft (charsW (8))); row2.removeFromLeft (gap * 3);
    fxInfoArea = row2.toFloat();
    r.removeFromTop (gap * 2);
    if (! browsingPlugins && proc.song.mixer.slot (fxSlot).type != FxType::None)
    {
        auto row3 = r.removeFromTop (rowH);
        learnTrackBox.setBounds (row3.removeFromLeft (learnTrackBox.preferredWidth (px))); row3.removeFromLeft (gap * 2);
        learnABtn.setBounds (row3.removeFromLeft (charsW (16))); row3.removeFromLeft (gap);
        learnBBtn.setBounds (row3.removeFromLeft (charsW (16))); row3.removeFromLeft (gap * 2);
        learnInfoArea = row3.toFloat();
        r.removeFromTop (gap * 2);
    }
    r.removeFromTop (gap);

    if (browsingPlugins)
    {
        auto bottom = r.removeFromBottom (rowH);
        rescanBtn.setBounds (bottom.removeFromLeft (charsW (6))); bottom.removeFromLeft (gap);
        listCloseBtn.setBounds (bottom.removeFromRight (charsW (18))); bottom.removeFromRight (gap);
        listStatusArea = bottom.toFloat().withTrimmedLeft ((float) gap);
        r.removeFromBottom (gap);
        pluginList.setBounds (r);
        return;
    }
    const auto& s = proc.song.mixer.slot (fxSlot);
    if (s.type == FxType::Plugin)
    {
        editPluginBtn.setBounds (r.removeFromTop (bigH).withWidth (charsW (20)));
        r.removeFromTop (gap * 3);
        pluginInfoArea = r.removeFromTop ((int) (px * 11) * 4).toFloat();
        return;
    }
    const auto& ps = fxParams (s.type);
    auto area = r.withWidth (std::min (r.getWidth(), charsW (72)));
    for (int i = 0; i < kFxParams; ++i)
    {
        if (i >= (int) ps.size()) continue;
        auto row = area.removeFromTop (bigH); area.removeFromTop (gap * 2);
        if (ps[(size_t) i].toggle) paramToggle[i]->setBounds (row.withWidth (charsW (14)));
        else param[i]->setBounds (row);
    }
}
} // namespace prv::ui
