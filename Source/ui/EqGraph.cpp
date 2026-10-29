// Paula's Revenge - the channel EQ's graph
#include "EqGraph.h"
#include "ChoiceBox.h"
#include "../engine/Effects.h"
#include "../engine/Utility.h"

namespace prv::ui
{
namespace
{
    constexpr double kSpecLow = -84.0, kSpecHigh = -6.0;   // what the spectrum's height shows (dB, after the tilt)
    struct SpecColour { const char* name; juce::uint32 dark, light; };
    const SpecColour kSpecColours[] = {
        { "ICE",     0xffa8c8e8, 0xff3a5f86 }, { "CYAN",   0xff36d6e7, 0xff0b7d8c }, { "GREEN",  0xff5fdc7a, 0xff1f7f3a },
        { "LIME",    0xffc4e85a, 0xff5f7d10 }, { "AMBER",  0xffffc24a, 0xff9a6400 }, { "ORANGE", 0xffff8a3d, 0xffb04a0c },
        { "MAGENTA", 0xffff5fc8, 0xffa0247a }, { "VIOLET", 0xffa98bff, 0xff5a3fb8 } };
    const char* kTypeShort[] = { "BELL", "LOW SHELF", "HIGH SHELF", "HIGH-PASS", "LOW-PASS" };
}

EqGraph::EqGraph (PaulaProcessor& p) : proc (p)
{
    setWantsKeyboardFocus (false);
    setMouseClickGrabsKeyboardFocus (false);
    showSpectrum = prefs().getBoolValue ("eqSpectrum", true);
}

EqGraph::~EqGraph()
{
    auto& tap = proc.player.mixer().chTap;
    if (tap.source.load() == track) tap.source.store (-1);
}

ChannelEq& EqGraph::eq() { return proc.song.mixer.ch[juce::jlimit (0, kMixTracks - 1, track)].eq; }
const ChannelEq& EqGraph::eq() const { return proc.song.mixer.ch[juce::jlimit (0, kMixTracks - 1, track)].eq; }

juce::String EqGraph::hzText (float hz) { return hz < 1000.0f ? juce::String (juce::roundToInt (hz)) + " HZ" : juce::String (hz / 1000.0f, hz < 10000.0f ? 2 : 1) + " KHZ"; }
juce::String EqGraph::dbText (float db) { return (db > 0.0f ? "+" : "") + juce::String (db, 1) + " DB"; }

juce::String EqGraph::pointText (int b) const
{
    if (b < 0 || b >= kEqBands || ! eq().band[b].used) return {};
    const auto& e = eq().band[b];
    juce::String t = hzText (e.freq);
    if (e.hasGain()) t << "  " << dbText (e.gain);
    if (e.hasQ()) t << "  Q " << juce::String (e.q, 2);
    return t;
}

juce::String EqGraph::label (int b, const juce::String& what) const
{
    return "TRACK " + juce::String (track + 1) + " EQ POINT " + juce::String (b + 1) + (what.isNotEmpty() ? " " + what : juce::String());
}

void EqGraph::setTrack (int t)
{
    track = juce::jlimit (0, kMixTracks - 1, t);
    proc.player.mixer().chTap.source.store (isVisible() ? track : -1);
    std::fill (cols.begin(), cols.end(), (float) kSpecLow);
    std::fill (peaks.begin(), peaks.end(), (float) kSpecLow);
    lastCount = 0;
    selectFirst();
}

void EqGraph::selectFirst()
{
    selected = -1;
    for (int b = 0; b < kEqBands; ++b) if (eq().band[b].used) { selected = b; break; }
    repaint();
}

void EqGraph::visibilityChanged()
{
    auto& tap = proc.player.mixer().chTap;
    if (isVisible()) tap.source.store (track);
    else if (tap.source.load() == track) tap.source.store (-1);
}

void EqGraph::resized()
{
    plot = getLocalBounds().toFloat().reduced (Theme::px * 2);
    cols.assign ((size_t) std::max (1, (int) plot.getWidth()), (float) kSpecLow);
    peaks.assign (cols.size(), (float) kSpecLow);
    holds.assign (cols.size(), 0);
}

// ---- mapping: 20 Hz .. 20 kHz across, -18 .. +18 dB up
float EqGraph::hzToX (double hz) const { return plot.getX() + plot.getWidth() * (float) (std::log10 (juce::jlimit (20.0, 20000.0, hz) / 20.0) / 3.0); }
float EqGraph::xToHz (float x) const { return 20.0f * std::pow (1000.0f, juce::jlimit (0.0f, 1.0f, (x - plot.getX()) / std::max (1.0f, plot.getWidth()))); }
float EqGraph::dbToY (double db) const { return plot.getCentreY() - plot.getHeight() * 0.5f * (float) (juce::jlimit (-20.0, 20.0, db) / 20.0); }
float EqGraph::yToDb (float y) const
{
    const float db = (plot.getCentreY() - y) / std::max (1.0f, plot.getHeight() * 0.5f) * 20.0f;
    return juce::jlimit (-kEqMaxDb, kEqMaxDb, std::round (db * 2.0f) / 2.0f);   // 0.5 dB steps
}

juce::Point<float> EqGraph::pointPos (int b) const
{
    const auto& e = eq().band[b];
    return { hzToX (e.freq), dbToY (e.hasGain() ? e.gain : 0.0) };
}

int EqGraph::hitPoint (juce::Point<float> p) const
{
    const float reach = Theme::px * 6;
    int best = -1; float bestD = reach;
    for (int b = 0; b < kEqBands; ++b)
    {
        if (! eq().band[b].used) continue;
        const float d = pointPos (b).getDistanceFrom (p);
        if (d <= bestD) { bestD = d; best = b; }
    }
    return best;
}

// ---- edits (each one an undo step; a drag is one)
void EqGraph::edited()
{
    repaint();
    if (onChanged) onChanged();
}

int EqGraph::addPointAt (juce::Point<float> where)
{
    if (eq().points() >= kEqBands)
    {
        if (onStatus) onStatus ("EQ FULL: 8 POINTS (RIGHT-CLICK ONE TO REMOVE IT)");
        return -1;
    }
    const float hz = std::round (xToHz (where.x)), db = yToDb (where.y);
    proc.history.mixer ("TRACK " + juce::String (track + 1) + " EQ ADD POINT", false);
    int b = -1;
    proc.withLock ([&] { b = eq().add (hz, db); });
    selected = b;
    if (onStatus && b >= 0) onStatus ("EQ POINT " + juce::String (b + 1) + ": " + juce::String (eqTypeName (eq().band[b].type)) + "  " + pointText (b));
    edited();
    return b;
}

void EqGraph::movePoint (int b, float hz, float db, bool merge)
{
    if (b < 0 || b >= kEqBands || ! eq().band[b].used) return;
    proc.history.mixer (label (b, "MOVE"), merge);
    proc.withLock ([&]
    {
        auto& e = eq().band[b];
        e.freq = juce::jlimit (kEqMinHz, kEqMaxHz, hz);
        if (e.hasGain()) e.gain = juce::jlimit (-kEqMaxDb, kEqMaxDb, db);
        e.on = true;
    });
    edited();
}

void EqGraph::setQ (int b, float q, bool merge)
{
    if (b < 0 || b >= kEqBands || ! eq().band[b].used || ! eq().band[b].hasQ()) return;
    proc.history.mixer (label (b, "Q"), merge);
    proc.withLock ([&] { eq().band[b].q = juce::jlimit (0.3f, 8.0f, q); eq().band[b].on = true; });
    edited();
}

void EqGraph::setType (int b, EqType t)
{
    if (b < 0 || b >= kEqBands || ! eq().band[b].used) return;
    proc.history.mixer (label (b, juce::String (eqTypeName (t))), false);
    proc.withLock ([&] { auto& e = eq().band[b]; e.type = t; if (! e.hasGain()) e.gain = 0.0f; e.on = true; });
    edited();
}

void EqGraph::resetGain (int b)
{
    if (b < 0 || b >= kEqBands || ! eq().band[b].used) return;
    proc.history.mixer (label (b, "RESET GAIN"), false);
    proc.withLock ([&] { eq().band[b].gain = 0.0f; });
    edited();
}

void EqGraph::setOn (int b, bool on)
{
    if (b < 0 || b >= kEqBands || ! eq().band[b].used) return;
    proc.history.mixer (label (b, on ? "ON" : "OFF"), false);
    proc.withLock ([&] { eq().band[b].on = on; });
    edited();
}

void EqGraph::removePoint (int b)
{
    if (b < 0 || b >= kEqBands || ! eq().band[b].used) return;
    proc.history.mixer (label (b, "REMOVED"), false);
    proc.withLock ([&] { eq().remove (b); });
    if (selected == b) selectFirst();
    if (onStatus) onStatus ("EQ POINT " + juce::String (b + 1) + " REMOVED");
    edited();
}

void EqGraph::showPointMenu (int b)
{
    juce::Component::SafePointer<EqGraph> safe (this);
    std::vector<MenuItem> items;
    for (int t = 0; t < (int) EqType::Count; ++t)
        items.push_back ({ juce::String ((int) eq().band[b].type == t ? "> " : "  ") + kTypeShort[t], [safe, b, t] { if (safe) safe->setType (b, (EqType) t); } });
    items.push_back ({ eq().band[b].on ? "SWITCH OFF" : "SWITCH ON", [safe, b] { if (safe) safe->setOn (b, ! safe->eq().band[b].on); } });
    if (eq().band[b].hasGain()) items.push_back ({ "RESET GAIN", [safe, b] { if (safe) safe->resetGain (b); } });
    items.push_back ({ "REMOVE POINT", [safe, b] { if (safe) safe->removePoint (b); } });
    showMenu ("EQ POINT " + juce::String (b + 1), items);
}

// ---- mouse
void EqGraph::mouseDown (const juce::MouseEvent& e)
{
    if (auto* p = getParentComponent()) p->grabKeyboardFocus();   // (Delete removes the selected point)
    const int b = hitPoint (e.position);
    if (b < 0) { if (e.mods.isPopupMenu()) showColourMenu(); return; }
    selected = b;
    if (e.mods.isPopupMenu()) { repaint(); if (onChanged) onChanged(); showPointMenu (b); return; }
    dragBand = b; dragMoved = false;
    if (! eq().band[b].on) setOn (b, true);
    repaint();
    if (onChanged) onChanged();
}

void EqGraph::mouseDrag (const juce::MouseEvent& e)
{
    if (dragBand < 0) return;
    if (! dragMoved && e.getDistanceFromDragStart() < 2) return;
    const bool merge = dragMoved;
    dragMoved = true;
    const auto& b = eq().band[dragBand];
    float hz = std::round (xToHz (juce::jlimit (plot.getX(), plot.getRight(), e.position.x)));
    float db = b.hasGain() ? yToDb (juce::jlimit (plot.getY(), plot.getBottom(), e.position.y)) : 0.0f;
    if (e.mods.isShiftDown()) hz = b.freq;   // (Shift: up and down only)
    movePoint (dragBand, hz, db, merge);
}

void EqGraph::mouseUp (const juce::MouseEvent&)
{
    if (dragBand >= 0 && dragMoved && onStatus) onStatus ("EQ POINT " + juce::String (dragBand + 1) + ": " + pointText (dragBand));
    dragBand = -1;
    repaint();
}

void EqGraph::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu()) return;
    const int b = hitPoint (e.position);
    if (b >= 0) { selected = b; repaint(); if (onChanged) onChanged(); return; }   // (a point: just selected)
    if (plot.contains (e.position)) addPointAt (e.position);
}

void EqGraph::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    const int b = hitPoint (e.position);
    if (b < 0 || ! eq().band[b].hasQ()) return;
    wheelAccum += w.deltaY * (w.isReversed ? -1.0f : 1.0f);
    const int steps = (int) (wheelAccum / 0.1f);
    if (steps == 0) return;
    wheelAccum -= steps * 0.1f;
    selected = b;
    setQ (b, eq().band[b].q * std::pow (1.12f, (float) steps), true);
    if (onStatus) onStatus ("EQ POINT " + juce::String (b + 1) + ": Q " + juce::String (eq().band[b].q, 2));
}

void EqGraph::mouseMove (const juce::MouseEvent& e)
{
    const int h = hitPoint (e.position);
    if (h != hover) { hover = h; repaint(); }
    setMouseCursor (h >= 0 ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::NormalCursor);
}

void EqGraph::mouseExit (const juce::MouseEvent&) { if (hover >= 0) { hover = -1; repaint(); } }

// ---- spectrum
juce::String EqGraph::spectrumColourName (int i) { return kSpecColours[juce::jlimit (0, kSpectrumColours - 1, i)].name; }
juce::Colour EqGraph::spectrumColourFor (int i, bool dark) { const auto& c = kSpecColours[juce::jlimit (0, kSpectrumColours - 1, i)]; return juce::Colour (dark ? c.dark : c.light); }
int EqGraph::spectrumColour() { return juce::jlimit (0, kSpectrumColours - 1, prefs().getIntValue ("eqSpectrumColour", 0)); }
void EqGraph::setSpectrumColour (int i)
{
    prefs().setValue ("eqSpectrumColour", juce::jlimit (0, kSpectrumColours - 1, i));
    prefs().saveIfNeeded();
    if (onStatus) onStatus ("SPECTRUM COLOUR: " + spectrumColourName (i));
    repaint();
}
void EqGraph::showColourMenu()
{
    juce::Component::SafePointer<EqGraph> safe (this);
    std::vector<MenuItem> items;
    for (int i = 0; i < kSpectrumColours; ++i)
        items.push_back ({ juce::String (i == spectrumColour() ? "> " : "  ") + spectrumColourName (i), [safe, i] { if (safe) safe->setSpectrumColour (i); } });
    showMenu ("SPECTRUM COLOUR", items);
}

void EqGraph::feedSpectrum (const float* s, int n) { analyse (s, n); repaint(); }

void EqGraph::analyse (const float* s, int n)
{
    int size = 4096;
    while (size > n && size > 256) size /= 2;
    if (n < size) return;
    spectrumDb (s + (n - size), size, db);
    const double rate = proc.getSampleRate() > 0 ? proc.getSampleRate() : 48000.0;
    const double binHz = rate / size;
    const int w = (int) cols.size();
    for (int x = 0; x < w; ++x)
    {
        // 1/6-octave smoothing: the average power over a band around the column's frequency
        // (narrow enough to see notes, wide enough not to flicker), then a +4.5 dB/octave tilt
        // round 1 kHz so a mix reads flat, as on a studio analyser
        const double fc = 20.0 * std::pow (1000.0, (x + 0.5) / w);
        const double lo = fc * 0.9439, hi = fc * 1.0595;   // (2^-1/12, 2^1/12)
        int i1 = (int) std::floor (lo / binHz), i2 = (int) std::ceil (hi / binHz);
        i1 = juce::jlimit (1, size / 2, i1); i2 = juce::jlimit (i1, size / 2, i2);
        double pw = 0;
        for (int i = i1; i <= i2; ++i) pw += std::pow (10.0, db[(size_t) i] / 10.0);
        float m = (float) (10.0 * std::log10 (std::max (1e-12, pw / (i2 - i1 + 1))));
        m += 4.5f * (float) std::log2 (fc / 1000.0);
        auto& c = cols[(size_t) x];
        c = m > c ? m : std::max (m, c - 1.2f);            // up at once, down calmly (~36 dB a second)
        auto& pk = peaks[(size_t) x];
        if (c >= pk) { pk = c; holds[(size_t) x] = 45; }   // peak hold: 1.5 s, then it falls
        else if (--holds[(size_t) x] < 0) pk = std::max (c, pk - 0.6f);
    }
}

void EqGraph::tick()
{
    // (visible all the way up: isShowing() also wants a window on screen)
    for (auto* c = static_cast<juce::Component*> (this); c->getParentComponent() != nullptr; c = c->getParentComponent()) if (! c->isVisible()) return;
    if (! showSpectrum) return;
    auto& tap = proc.player.mixer().chTap;
    if (tap.source.load() != track) { tap.source.store (track); return; }
    const uint32_t c = tap.count();
    if (c == lastCount) return;   // (stopped: it stays as it was)
    lastCount = c;
    const int got = tap.read (tapBuf.data(), (int) tapBuf.size());
    analyse (tapBuf.data(), got);
    repaint();
}

// ---- drawing
void EqGraph::paint (juce::Graphics& g)
{
    const auto& sk = Theme::skin;
    const float px = Theme::px;
    const auto r = getLocalBounds().toFloat();
    g.setColour (sk.bg);
    g.fillRect (r);
    const auto in = plot;
    const auto tc = sk.track[track % 8];

    // grid: decades and the steps between them faint, the main ones a little stronger, labelled
    {
        const auto minor = sk.rule.withAlpha (sk.dark ? 0.45f : 0.22f), major = sk.rule.withAlpha (sk.dark ? 1.0f : 0.6f);
        for (double dec : { 10.0, 100.0, 1000.0, 10000.0 })
            for (int k = 2; k <= 9; ++k)
            {
                const double hz = dec * k;
                if (hz < 20.0 || hz > 20000.0) continue;
                g.setColour (k == 2 || k == 5 ? major : minor);
                g.fillRect (hzToX (hz), in.getY(), 1.0f, in.getHeight());
            }
        g.setColour (major);
        for (double hz : { 100.0, 1000.0, 10000.0 }) g.fillRect (hzToX (hz), in.getY(), 1.0f, in.getHeight());
        for (double d : { -18.0, -12.0, -6.0, 6.0, 12.0, 18.0 })
        {
            g.setColour (std::abs (d) == 12.0 || std::abs (d) == 6.0 ? major : minor);
            g.fillRect (in.getX(), dbToY (d), in.getWidth(), 1.0f);
        }
    }

    // the spectrum, behind the curve: a soft gradient fill, a crisp edge, and a faint peak-hold line
    if (showSpectrum && ! cols.empty())
    {
        const auto sc = spectrumColourFor (spectrumColour(), sk.dark);
        auto yOf = [&] (float dbv) { return in.getBottom() - (float) juce::jlimit (0.0, 1.0, (dbv - kSpecLow) / (kSpecHigh - kSpecLow)) * in.getHeight(); };
        juce::Path edge, area, peak;
        const int step = std::max (1, (int) (px * 0.75f));
        for (size_t x = 0; x < cols.size(); x += (size_t) step)
        {
            const float xx = in.getX() + (float) x, y = yOf (cols[x]), yp = yOf (peaks[x]);
            if (x == 0) { edge.startNewSubPath (xx, y); peak.startNewSubPath (xx, yp); }
            else { edge.lineTo (xx, y); peak.lineTo (xx, yp); }
        }
        edge.lineTo (in.getRight(), yOf (cols.back()));
        peak.lineTo (in.getRight(), yOf (peaks.back()));
        area = edge;
        area.lineTo (in.getRight(), in.getBottom());
        area.lineTo (in.getX(), in.getBottom());
        area.closeSubPath();
        g.saveState();
        g.reduceClipRegion (in.toNearestInt());
        g.setGradientFill (juce::ColourGradient (sc.withAlpha (sk.dark ? 0.42f : 0.30f), 0.0f, in.getY(), sc.withAlpha (0.04f), 0.0f, in.getBottom(), false));
        g.fillPath (area);
        g.setColour (sc.withAlpha (sk.dark ? 0.32f : 0.40f));
        g.strokePath (peak, juce::PathStrokeType (1.0f));
        g.setColour (sc.withAlpha (0.85f));
        g.strokePath (edge, juce::PathStrokeType (std::max (1.0f, px * 0.5f), juce::PathStrokeType::curved));
        g.restoreState();
    }

    // labels over the grid: Hz along the bottom, dB at the right
    g.setColour (sk.dim);
    g.fillRect (in.getX(), dbToY (0.0), in.getWidth(), std::max (1.0f, px * 0.5f));
    for (auto [hz, t] : { std::pair<double, const char*> { 50.0, "50" }, { 100.0, "100" }, { 200.0, "200" }, { 500.0, "500" },
                          { 1000.0, "1K" }, { 2000.0, "2K" }, { 5000.0, "5K" }, { 10000.0, "10K" } })
        PixelFont::draw (g, t, hzToX (hz) + px * 2, in.getBottom() - px * 8, px, sk.dim);
    for (auto [d, t] : { std::pair<double, const char*> { 12.0, "+12" }, { 6.0, "+6" }, { -6.0, "-6" }, { -12.0, "-12" } })
        PixelFont::draw (g, t, in.getRight() - PixelFont::textWidth (t, px) - px, dbToY (d) - px * 8.0f, px, sk.dim);

    // the response
    const auto& q = eq();
    const double sr = 48000.0;
    juce::Path path;
    const int steps = std::max (32, (int) in.getWidth() / 2);
    for (int i = 0; i <= steps; ++i)
    {
        const double hz = 20.0 * std::pow (1000.0, (double) i / steps);
        const double d = q.anyOn() ? ChannelEqProcessor::responseDb (q, std::min (hz, sr * 0.49), sr) : 0.0;
        const float x = in.getX() + in.getWidth() * (float) i / (float) steps, y = dbToY (d);
        if (i == 0) path.startNewSubPath (x, y); else path.lineTo (x, y);
    }
    // the curve: a light fill to 0 dB under it, then the line
    {
        juce::Path fill (path);
        fill.lineTo (in.getRight(), dbToY (0.0));
        fill.lineTo (in.getX(), dbToY (0.0));
        fill.closeSubPath();
        g.setColour (tc.withAlpha (0.16f));
        g.fillPath (fill);
    }
    g.setColour (tc);
    g.strokePath (path, juce::PathStrokeType (std::max (2.0f, px * 0.9f), juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // the points: a numbered square each, in its own colour; off ones hollow
    const float s = px * 9;
    for (int b = 0; b < kEqBands; ++b)
    {
        const auto& e = q.band[b];
        if (! e.used) continue;
        auto box = juce::Rectangle<float> (s, s).withCentre (pointPos (b));
        const auto c = sk.track[b % 8];
        if (b == selected || b == hover || b == dragBand)
        {
            g.setColour (sk.hot);
            g.drawRect (box.expanded (px * 1.5f), std::max (1.0f, px));
        }
        if (e.on) { g.setColour (c); g.fillRect (box); }
        else { g.setColour (sk.bg); g.fillRect (box); g.setColour (c); g.drawRect (box, std::max (1.0f, px * 0.75f)); }
        const juce::String n (b + 1);
        PixelFont::draw (g, n, box.getCentreX() - PixelFont::textWidth (n, px) * 0.5f, box.getCentreY() - px * 3.5f, px,
                         e.on ? sk.bg : c);
    }

    // big readout while dragging (or over a point)
    const int shown = dragBand >= 0 ? dragBand : hover;
    if (shown >= 0 && q.band[shown].used)
    {
        const juce::String t = "POINT " + juce::String (shown + 1) + ": " + pointText (shown);
        float big = px * 1.5f;
        if (PixelFont::textWidth (t, big) > in.getWidth() - px * 10) big = px;   // (big when it fits)
        const float tw = PixelFont::textWidth (t, big);
        auto box = juce::Rectangle<float> (tw + px * 6, big * 7 + px * 4).withPosition (in.getX() + px * 2, in.getY() + px * 2);
        if (pointPos (shown).x < in.getCentreX()) box.setX (in.getRight() - box.getWidth() - px * 2);
        g.setColour (sk.bg.withAlpha (0.92f));
        g.fillRect (box);
        g.setColour (sk.hot);
        g.drawRect (box, std::max (1.0f, px * 0.5f));
        PixelFont::draw (g, t, box.getX() + px * 3, box.getY() + px * 2, big, sk.text);
    }
    else if (q.points() == 0)
    {
        const juce::String t = "DOUBLE-CLICK TO ADD A POINT";
        PixelFont::draw (g, t, in.getCentreX() - PixelFont::textWidth (t, px) * 0.5f, dbToY (0.0) - px * 12, px, sk.dim);
    }
    Theme::bevel (g, r, false, std::max (1.0f, px * 0.5f), Theme::palFor (*this));
}
} // namespace prv::ui
