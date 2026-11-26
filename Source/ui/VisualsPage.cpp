// Paula's Revenge - VISUALS (see VisualsPage.h). Our own drawings: bars, waves and ribbons moved by the sound.
#include "VisualsPage.h"
#include <cmath>

namespace prv::ui
{
namespace
{
    constexpr float kTau = 6.2831853f;

    // a colour from stops spread evenly across 0..1
    juce::Colour ramp (std::initializer_list<juce::uint32> stops, float x)
    {
        const int n = (int) stops.size();
        x = juce::jlimit (0.0f, 1.0f, x) * (float) (n - 1);
        const int i = juce::jlimit (0, n - 2, (int) x);
        const auto* s = stops.begin();
        return juce::Colour (s[i]).interpolatedWith (juce::Colour (s[i + 1]), x - (float) i);
    }
    // (each scene's colours across the screen)
    juce::Colour prismAt (float x)   { return ramp ({ 0xff16e6a2, 0xff1fd0d8, 0xff2d8cff, 0xff6a5cff, 0xffc03cff, 0xffff2f8e, 0xffff3d4a, 0xffff7a1e, 0xffffc21a, 0xffffe14a }, x); }
    juce::Colour rainbowAt (float x) { return ramp ({ 0xff22b8ff, 0xff2f6bff, 0xff7a3cff, 0xffd52fe0, 0xffff2f86, 0xffff3d3d, 0xffff7a1e, 0xffffc21e, 0xffa4e03c, 0xff2fd6a0, 0xff22b8ff }, x); }
    juce::Colour ribbonAt (float x)  { return ramp ({ 0xff2fd8ff, 0xff36e0e8, 0xff8fe6a0, 0xffffd23a, 0xffffc02a, 0xffff9a1e, 0xffff6a1e, 0xffff3b2a }, x); }
    juce::Colour pastelAt (float x)  { return ramp ({ 0xfffde28a, 0xffc9c48e, 0xff8fae96, 0xff4f8e9c, 0xff1a6ea6, 0xff3a5590, 0xff6a4478, 0xffa8325a, 0xffcc2e4c, 0xffdc5c5a,
                                                       0xffeea070, 0xfffbd384, 0xfffde28a, 0xffc9c48e, 0xff8fae96, 0xff2a7aa6 }, x); }
    juce::Colour meshAt (float x)    { return ramp ({ 0xfff4e6a8, 0xffff7ad8, 0xffffd86a, 0xff8cf06a, 0xff2ee8c0, 0xff38d8ff, 0xffffb05a, 0xffff8ab0, 0xffb090ff, 0xff7ee0a0 }, x); }

    // a soft round light: a radial gradient from the colour to nothing
    void softLight (juce::Graphics& g, float cx, float cy, float radius, juce::Colour c, float alpha)
    {
        if (alpha <= 0.002f || radius < 1) return;
        juce::ColourGradient cg (c.withAlpha (alpha), cx, cy, c.withAlpha (0.0f), cx + radius, cy, true);
        cg.addColour (0.35, c.withAlpha (alpha * 0.55f));
        g.setGradientFill (cg);
        g.fillEllipse (cx - radius, cy - radius, radius * 2, radius * 2);
    }
    // a horizontal gradient across an area, from a ramp (alpha for all of it)
    juce::ColourGradient across (juce::Rectangle<float> r, juce::Colour (*at) (float), float alpha, float lighten = 0.0f, float x0 = 0.0f, float x1 = 1.0f)
    {
        juce::ColourGradient cg (at (x0).interpolatedWith (juce::Colours::white, lighten).withAlpha (alpha), r.getX(), 0.0f,
                                 at (x1).interpolatedWith (juce::Colours::white, lighten).withAlpha (alpha), r.getRight(), 0.0f, false);
        for (int k = 1; k < 12; ++k)
        {
            const float u = (float) k / 12.0f;
            cg.addColour (u, at (x0 + (x1 - x0) * u).interpolatedWith (juce::Colours::white, lighten).withAlpha (alpha));
        }
        return cg;
    }
    float gauss (float x, float c, float w) { const float d = (x - c) / w; return std::exp (-d * d); }
}

VisualsPage::VisualsPage (PaulaProcessor& p) : proc (p)
{
    setOpaque (true);
    setWantsKeyboardFocus (true);
    for (int i = 0; i < Count; ++i)
    {
        addAndMakeVisible (sceneBtn[i]);
        sceneBtn[i].setClickingTogglesState (false);
        sceneBtn[i].onClick = [this, i] { setScene ((Scene) i); };
    }
    addAndMakeVisible (closeBtn);
    closeBtn.onClick = [this] { if (onClose) onClose(); };
    setScene (Auto);
}

VisualsPage::~VisualsPage() { stopTimer(); }

void VisualsPage::opened()
{
    for (int i = 0; i < kMaxTracks; ++i) lastNotes[i] = proc.player.uiNotes[i].load();
    idle = 0; showControls = true;
    for (auto& b : sceneBtn) b.setVisible (true);
    closeBtn.setVisible (true);
    startTimerHz (60);
    grabKeyboardFocus();
}

void VisualsPage::closed() { stopTimer(); }

void VisualsPage::setScene (Scene s)
{
    scene = s;
    autoTime = 0;
    for (int i = 0; i < Count; ++i) sceneBtn[i].setToggleState (i == (int) s, juce::dontSendNotification);
    repaint();
}

float VisualsPage::bandAt (float x01, bool smooth) const
{
    const auto& a = smooth ? slow : lv;
    const float f = juce::jlimit (0.0f, 1.0f, x01) * (kBands - 1);
    const int i = juce::jlimit (0, kBands - 2, (int) f);
    const float u = f - (float) i;
    const float s = u * u * (3 - 2 * u);
    return a[(size_t) i] * (1 - s) + a[(size_t) i + 1] * s;
}

void VisualsPage::listen (double dt)
{
    // the mix: every track's scope added up, the last kSize samples, into the spectrum
    std::array<float, kBands> target {};
    const bool playing = proc.player.isPlaying();
    if (playing)
    {
        const int n = std::clamp (proc.song.numTracks, 1, kMaxTracks);
        const uint32_t wpos = proc.player.uiScopeWrite.load();
        const int len = Player::kScopeLen;
        for (int i = 0; i < kSize; ++i)
        {
            const int idx = (int) ((wpos + (uint32_t) len * 4 - (uint32_t) kSize + (uint32_t) i) % (uint32_t) len);
            float s = 0;
            for (int tr = 0; tr < n; ++tr) s += proc.player.uiScope[tr][idx];
            work[(size_t) i] = s;
        }
        std::fill (work.begin() + kSize, work.end(), 0.0f);
        window.multiplyWithWindowingTable (work.data(), (size_t) kSize);
        fft.performFrequencyOnlyForwardTransform (work.data(), true);
        const double rate = proc.getSampleRate() > 0 ? proc.getSampleRate() : 48000.0;
        const double lo = 35.0, hi = std::min (14000.0, rate * 0.45);
        for (int b = 0; b < kBands; ++b)
        {
            const double f1 = lo * std::pow (hi / lo, b / (double) kBands), f2 = lo * std::pow (hi / lo, (b + 1) / (double) kBands);
            const int i1 = juce::jlimit (1, kSize / 2 - 2, (int) (f1 * kSize / rate));
            const int i2 = juce::jlimit (i1 + 1, kSize / 2, (int) (f2 * kSize / rate) + 1);
            float m = 0;
            for (int i = i1; i < i2; ++i) m = std::max (m, work[(size_t) i]);
            const float db = juce::Decibels::gainToDecibels (m / (kSize * 0.25f), -80.0f);
            const float tilt = 3.5f * (float) std::log2 (std::max (1.0, f1 / 150.0));
            target[(size_t) b] = juce::jlimit (0.0f, 1.0f, (db + tilt + 58.0f) / 50.0f);
        }
    }
    else
    {
        // stopped: a slow breath, so the show still moves (gently) while nothing plays
        for (int b = 0; b < kBands; ++b)
        {
            const float x = (float) b / kBands;
            target[(size_t) b] = 0.16f + 0.10f * std::sin ((float) t * 0.7f + x * 9.0f) + 0.07f * std::sin ((float) t * 1.3f - x * 17.0f);
        }
    }
    // up fast, down smoothly; a slower copy for the calmer scenes
    const float down = std::exp (-(float) dt * 5.0f), downSlow = std::exp (-(float) dt * 2.2f);
    for (int b = 0; b < kBands; ++b)
    {
        auto& v = lv[(size_t) b];
        const float tg = target[(size_t) b];
        v = tg > v ? v + (tg - v) * 0.65f : tg + (v - tg) * down;
        auto& s = slow[(size_t) b];
        s = tg > s ? s + (tg - s) * 0.25f : tg + (s - tg) * downSlow;
    }
    auto avg = [&] (int a, int b) { float s = 0; for (int i = a; i < b; ++i) s += lv[(size_t) i]; return s / (float) (b - a); };
    bass = avg (0, 14); mid = avg (14, 40); high = avg (40, kBands);
    level = avg (0, kBands);
}

void VisualsPage::step (double dt)
{
    t += dt;
    // the notes since last time: each flashes its track; any of them is a beat
    bool hit = false;
    for (int i = 0; i < kMaxTracks; ++i)
    {
        const int n = proc.player.uiNotes[i].load();
        if (n != lastNotes[i])
        {
            trackHit[i] = 1.0f; hit = true; lastNotes[i] = n;
            // RAINBOW: a note joins the stave at the right, at a height from the note itself (spaced along it)
            if (notes.empty() || notes.back().x < 0.895f) notes.push_back ({ 0.93f, std::floor (rnd() * 9.0f) * 0.5f - 1.0f, 1.0f, rnd() < 0.4f ? 1 : 0 });
        }
        trackHit[i] = std::max (0.0f, trackHit[i] - (float) dt * 3.5f);
    }
    if (dt > 0) listen (dt);
    if (hit) flash = 1.0f;
    flash = std::max (0.0f, flash - (float) dt * 2.5f);

    // stopped: a note now and then, so the stave isn't empty
    noteTimer += dt;
    if (! proc.player.isPlaying() && noteTimer > 0.9 && (notes.empty() || notes.back().x < 0.895f))
    {
        noteTimer = 0;
        notes.push_back ({ 0.93f, std::floor (rnd() * 9.0f) * 0.5f - 1.0f, 1.0f, rnd() < 0.4f ? 1 : 0 });
    }
    for (auto& n : notes) n.x -= (float) dt * 0.075f;
    notes.erase (std::remove_if (notes.begin(), notes.end(), [] (const Note& n) { return n.x < 0.08f; }), notes.end());

    // RIBBONS' lights: a few on each beat, drifting up and fading
    if (hit && lights.size() < 60)
        for (int k = 0; k < 3; ++k)
            lights.push_back ({ 0.15f + rnd() * 0.7f, 0.25f + rnd() * 0.5f, (rnd() - 0.5f) * 0.03f, -0.02f - rnd() * 0.04f, 0.4f + rnd(), 1.0f, rnd() });
    if (lights.size() < 10 && rnd() < dt * 3.0)
        lights.push_back ({ rnd(), 0.2f + rnd() * 0.6f, (rnd() - 0.5f) * 0.02f, -0.01f - rnd() * 0.02f, 0.3f + rnd() * 0.6f, 1.0f, rnd() });
    for (auto& l : lights) { l.x += l.vx * (float) dt; l.y += l.vy * (float) dt; l.life -= (float) dt * 0.22f; }
    lights.erase (std::remove_if (lights.begin(), lights.end(), [] (const Light& l) { return l.life <= 0; }), lights.end());

    // AUTO: the next scene every 20 seconds or so, on a beat (or after 24 without one)
    autoTime += dt;
    if (scene == Auto && ((autoTime > 20.0 && hit) || autoTime > 24.0))
    {
        autoTime = 0;
        autoScene = (Scene) ((int) autoScene % (Count - 1) + 1);
    }

    idle += dt;
    const bool show = idle < 2.5;
    if (show != showControls)
    {
        showControls = show;
        for (auto& b : sceneBtn) b.setVisible (show);
        closeBtn.setVisible (show);
    }
}

void VisualsPage::timerCallback()
{
    step (1.0 / 60.0);
    repaint();
}

void VisualsPage::resized()
{
    const float px = Theme::px;
    auto charsW = [px] (int c) { return (int) (px * 6 * c + px * 4); };
    const int rowH = (int) (px * 11), gap = (int) px;
    auto r = getLocalBounds().reduced ((int) (px * 3)).removeFromTop (rowH);
    closeBtn.setBounds (r.removeFromRight (charsW (11))); r.removeFromRight (gap * 3);
    for (int i = 0; i < Count; ++i) { sceneBtn[i].setBounds (r.removeFromLeft (charsW (sceneBtn[i].getButtonText().length() + 1))); r.removeFromLeft (gap); }
}

// ---------------------------------------------------------------------------------------------------------- PRISM
void VisualsPage::paintPrism (juce::Graphics& g, juce::Rectangle<float> r)
{
    g.fillAll (juce::Colours::black);
    const float W = r.getWidth(), H = r.getHeight(), y0 = r.getCentreY();
    const float x0 = r.getX() + W * 0.04f, span = W * 0.92f;
    // four lobes of colour across: each swells with its part of the sound
    const float lobeC[4] = { 0.15f, 0.39f, 0.63f, 0.86f };
    float lobeV[4];
    for (int k = 0; k < 4; ++k)
    {
        float s = 0; for (int i = k * 16; i < k * 16 + 16; ++i) s += lv[(size_t) i];
        lobeV[k] = 0.25f + 0.75f * juce::jlimit (0.0f, 1.0f, s / 16.0f * 1.35f);
    }
    auto envAt = [&] (float x)
    {
        float e = 0;
        for (int k = 0; k < 4; ++k) e = std::max (e, gauss (x, lobeC[k], 0.085f) * lobeV[k]);
        return e;
    };
    // the glow behind each lobe
    for (int k = 0; k < 4; ++k)
        softLight (g, x0 + span * lobeC[k], y0, H * (0.30f + 0.18f * lobeV[k]), prismAt (lobeC[k]), 0.16f + 0.22f * lobeV[k] * lobeV[k]);

    // the bars: thin, bright through the middle, fading out at both ends; a soft halo round each
    const int n = 66;
    const float slot = span / n, bw = slot * 0.46f;
    for (int i = 0; i < n; ++i)
    {
        const float x = (i + 0.5f) / n;
        const float e = envAt (x);
        const float v = bandAt (x);
        const float h = H * (0.025f + 0.43f * e * (0.55f + 0.45f * v)) * (0.92f + 0.08f * std::sin ((float) i * 2.3f + (float) t * 2.0f));
        const auto c = prismAt (x);
        const float cx = x0 + slot * (i + 0.5f);
        // the halo
        {
            juce::ColourGradient cg (c.withAlpha (0.0f), 0, y0 - h * 1.15f, c.withAlpha (0.0f), 0, y0 + h * 1.15f, false);
            cg.addColour (0.5, c.withAlpha (0.20f + 0.15f * e));
            g.setGradientFill (cg);
            g.fillRect (cx - slot * 0.55f, y0 - h * 1.15f, slot * 1.1f, h * 2.3f);
        }
        // the bar
        juce::ColourGradient cg (c.withAlpha (0.0f), 0, y0 - h, c.withAlpha (0.0f), 0, y0 + h, false);
        cg.addColour (0.18, c.withAlpha (0.55f));
        cg.addColour (0.38, c.withAlpha (0.95f));
        cg.addColour (0.5, c.interpolatedWith (juce::Colours::white, 0.55f + 0.3f * flash));
        cg.addColour (0.62, c.withAlpha (0.95f));
        cg.addColour (0.82, c.withAlpha (0.55f));
        g.setGradientFill (cg);
        g.fillRect (cx - bw * 0.5f, y0 - h, bw, h * 2);
    }
    // the bright line along the middle, fading out at the ends
    {
        auto line = juce::Rectangle<float> (x0, y0 - Theme::px * 0.6f, span, Theme::px * 1.2f);
        auto cg = across (line, prismAt, 0.9f, 0.6f);
        cg.setColour (0, cg.getColour (0).withAlpha (0.0f));
        cg.setColour (cg.getNumColours() - 1, cg.getColour (cg.getNumColours() - 1).withAlpha (0.0f));
        g.setGradientFill (cg);
        g.fillRect (line);
    }
    // the strands: thin waves woven through, louder sound, wider waves
    const int strands = 12;
    for (int s = 0; s < strands; ++s)
    {
        const float f = 3.2f + 0.9f * std::sin ((float) s * 1.7f);
        const float ph = (float) s * 0.47f;
        const float sp = 0.45f + 0.06f * (float) s;
        const float amp = H * (0.10f + 0.16f * level + 0.05f * flash) * (0.55f + 0.45f * std::sin ((float) s * 2.1f + 1.0f));
        juce::Path p;
        const int pts = 220;
        for (int k = 0; k <= pts; ++k)
        {
            const float x = (float) k / pts;
            const float env = 0.25f + 0.75f * envAt (x);
            const float y = y0 + amp * env * std::sin (kTau * f * x + ph - (float) t * sp) * (0.5f + 0.5f * std::sin (kTau * 0.5f * x + (float) t * 0.21f + (float) s));
            const float X = r.getX() + W * (-0.02f + 1.04f * x);
            if (k == 0) p.startNewSubPath (X, y); else p.lineTo (X, y);
        }
        g.setGradientFill (across (r, prismAt, 0.30f + 0.25f * (s % 3 == 0), 0.55f));
        g.strokePath (p, juce::PathStrokeType (std::max (1.0f, Theme::px * 0.45f)));
    }
    // small dashes out at the ends of the middle line
    g.setColour (prismAt (0.0f).withAlpha (0.7f));
    for (int k = 0; k < 3; ++k) g.fillRect (x0 - Theme::px * (6.0f + 5.0f * k), y0 - Theme::px * 0.5f, Theme::px * 3, Theme::px);
    g.setColour (prismAt (1.0f).withAlpha (0.7f));
    for (int k = 0; k < 3; ++k) g.fillRect (x0 + span + Theme::px * (3.0f + 5.0f * k), y0 - Theme::px * 0.5f, Theme::px * 3, Theme::px);
}

// ---------------------------------------------------------------------------------------------------------- RAINBOW
void VisualsPage::paintRainbow (juce::Graphics& g, juce::Rectangle<float> r)
{
    g.fillAll (juce::Colours::black);
    const float W = r.getWidth(), H = r.getHeight(), y0 = r.getCentreY();
    const float x0 = r.getX() + W * 0.07f, span = W * 0.86f;
    const int n = 96;
    const float slot = span / n, bw = slot * 0.5f;
    for (int i = 0; i < n; ++i)
    {
        const float x = (i + 0.5f) / n;
        const float d = std::abs (x - 0.5f) * 2.0f;                  // (the lows in the middle, the highs out at both sides)
        const float jitter = 0.78f + 0.22f * std::abs (std::sin ((float) i * 12.9898f));
        const float v = bandAt (d * 0.95f);
        const float h = H * (0.05f + 0.30f * v * (1.0f - 0.45f * d) * jitter + 0.03f * flash);
        const auto c = rainbowAt (x);
        const float cx = x0 + slot * (i + 0.5f);
        juce::ColourGradient halo (c.withAlpha (0.0f), 0, y0 - h * 1.1f, c.withAlpha (0.0f), 0, y0 + h * 1.1f, false);
        halo.addColour (0.5, c.withAlpha (0.16f));
        g.setGradientFill (halo);
        g.fillRect (cx - slot * 0.6f, y0 - h * 1.1f, slot * 1.2f, h * 2.2f);
        juce::ColourGradient cg (c.withAlpha (0.35f), 0, y0 - h, c.withAlpha (0.35f), 0, y0 + h, false);
        cg.addColour (0.3, c.withAlpha (0.92f));
        cg.addColour (0.5, c.interpolatedWith (juce::Colours::white, 0.15f));
        cg.addColour (0.7, c.withAlpha (0.92f));
        g.setGradientFill (cg);
        g.fillRect (cx - bw * 0.5f, y0 - h, bw, h * 2);
    }
    // the stave: five lines curving across, and the notes riding it from right to left
    const float gapL = H * 0.024f;
    auto staveY = [&] (float x) { return y0 + H * 0.05f + H * 0.13f * std::sin (kTau * 0.85f * x + 1.2f - (float) t * 0.35f) * (0.6f + 0.4f * mid); };
    const float sx0 = 0.10f, sx1 = 0.94f;
    for (int l = 0; l < 5; ++l)
    {
        juce::Path p;
        for (int k = 0; k <= 160; ++k)
        {
            const float x = sx0 + (sx1 - sx0) * k / 160.0f;
            const float X = r.getX() + W * x, Y = staveY (x) + (l - 2) * gapL;
            if (k == 0) p.startNewSubPath (X, Y); else p.lineTo (X, Y);
        }
        auto cg = across (r, rainbowAt, 0.75f, 0.45f, 0.0f, 1.0f);
        cg.setColour (0, cg.getColour (0).withAlpha (0.0f));
        g.setGradientFill (cg);
        g.strokePath (p, juce::PathStrokeType (std::max (1.0f, Theme::px * 0.45f)));
    }
    for (const auto& nt : notes)
    {
        const float fade = juce::jlimit (0.0f, 1.0f, (nt.x - 0.08f) / 0.08f) * juce::jlimit (0.0f, 1.0f, (0.95f - nt.x) / 0.04f);
        const float X = r.getX() + W * nt.x;
        const float Y = staveY (nt.x) + 2 * gapL - nt.pos * gapL;
        const float slope = (staveY (nt.x + 0.004f) - staveY (nt.x - 0.004f)) / (W * 0.008f);
        const auto c = rainbowAt (nt.x).interpolatedWith (juce::Colours::white, 0.35f).withAlpha (0.95f * fade);
        g.setColour (c);
        juce::Path head;
        head.addEllipse (-gapL * 0.68f, -gapL * 0.48f, gapL * 1.36f, gapL * 0.96f);
        head.applyTransform (juce::AffineTransform::rotation (-0.35f + std::atan (slope)).translated (X, Y));
        g.fillPath (head);
        const float stemX = X + gapL * 0.6f;
        g.fillRect (stemX - Theme::px * 0.35f, Y - gapL * 3.3f, std::max (1.0f, Theme::px * 0.7f), gapL * 3.3f);
        if (nt.kind == 1)
        {
            juce::Path flag;
            flag.startNewSubPath (stemX, Y - gapL * 3.3f);
            flag.cubicTo (stemX + gapL * 0.2f, Y - gapL * 2.4f, stemX + gapL * 1.3f, Y - gapL * 2.2f, stemX + gapL * 0.8f, Y - gapL * 1.1f);
            g.strokePath (flag, juce::PathStrokeType (std::max (1.0f, Theme::px * 0.6f)));
        }
    }
}

// ---------------------------------------------------------------------------------------------------------- RIBBONS
void VisualsPage::paintRibbons (juce::Graphics& g, juce::Rectangle<float> r)
{
    const float W = r.getWidth(), H = r.getHeight(), y0 = r.getY() + H * 0.52f;
    // a warm dark room
    {
        juce::ColourGradient bg (juce::Colour (0xff1c120c), r.getCentreX(), y0, juce::Colour (0xff050405), r.getX(), r.getY(), true);
        g.setGradientFill (bg);
        g.fillRect (r);
        softLight (g, r.getX() + W * 0.85f, y0, H * 0.45f, juce::Colour (0xffff5a1e), 0.10f + 0.10f * high);
        softLight (g, r.getX() + W * 0.20f, y0, H * 0.40f, juce::Colour (0xff1e9aff), 0.08f + 0.08f * bass);
    }
    // a faint spectrum of fine lines behind the waves
    {
        const int n = 120;
        for (int i = 0; i < n; ++i)
        {
            const float x = (float) i / n;
            const float v = bandAt (x);
            const float h = H * (0.04f + 0.30f * v * gauss (x, 0.5f, 0.32f));
            g.setColour (ribbonAt (x).withAlpha (0.10f + 0.10f * v));
            g.fillRect (r.getX() + W * x, y0 - h, std::max (1.0f, Theme::px * 0.4f), h);
            // and a dotted grid of points over the middle
            if (i % 3 == 0)
                for (int k = 1; k < 7; ++k)
                {
                    g.setColour (juce::Colours::white.withAlpha (0.035f + 0.04f * v));
                    g.fillRect (r.getX() + W * x, y0 - H * 0.06f * k, Theme::px * 0.5f, Theme::px * 0.5f);
                }
        }
    }
    // the ribbons: waves swelling through the middle, crossing each other - each a glow, a bright edge and a fine
    // thread beside it; under them the same waves upside down and fainter (their reflection)
    const int ribbons = 7, pts = 200;
    const float yc = y0 - H * 0.04f, mirror = y0 + H * 0.07f;
    for (int pass = 0; pass < 2; ++pass)
    {
        const bool reflect = pass == 0;
        for (int rb = 0; rb < ribbons; ++rb)
        {
            const float f = 2.2f + 0.35f * (float) rb;
            const float ph = (float) rb * 0.9f;
            const float sp = 0.55f + 0.12f * (float) rb;
            const float part = bandAt ((float) rb / ribbons * 0.8f + 0.05f);
            const float amp = H * (0.06f + 0.17f * part + 0.03f * flash) * (0.7f + 0.3f * std::sin ((float) rb * 1.9f));
            juce::Path edge, thread;
            for (int k = 0; k <= pts; ++k)
            {
                const float x = (float) k / pts;
                const float env = 0.05f + 0.95f * gauss (x, 0.52f + 0.08f * std::sin ((float) t * 0.2f + rb), 0.27f);
                const float y = amp * env * std::sin (kTau * f * x + ph - (float) t * sp);
                const float y2 = amp * env * 0.82f * std::sin (kTau * f * x + ph + 0.18f - (float) t * sp);
                const float Y = reflect ? 2 * mirror - yc + y * 0.8f : yc - y;   // (the reflection: mirrored about the water line)
                const float Y2 = reflect ? 2 * mirror - yc + y2 * 0.8f : yc - y2;
                const float X = r.getX() + W * x;
                if (k == 0) { edge.startNewSubPath (X, Y); thread.startNewSubPath (X, Y2); }
                else { edge.lineTo (X, Y); thread.lineTo (X, Y2); }
            }
            const float a = reflect ? 0.28f : 1.0f;
            auto edgeGrad = [&] (float alpha, float lighten)
            {
                auto cg = across (r, ribbonAt, alpha, lighten);
                cg.setColour (0, cg.getColour (0).withAlpha (0.0f));
                cg.setColour (cg.getNumColours() - 1, cg.getColour (cg.getNumColours() - 1).withAlpha (0.0f));
                return cg;
            };
            g.setGradientFill (edgeGrad (0.13f * a, 0.0f));
            g.strokePath (edge, juce::PathStrokeType (Theme::px * 3.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            g.setGradientFill (edgeGrad (0.95f * a, 0.25f));
            g.strokePath (edge, juce::PathStrokeType (std::max (1.2f, Theme::px * 0.8f), juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            g.setGradientFill (edgeGrad (0.35f * a, 0.4f));
            g.strokePath (thread, juce::PathStrokeType (std::max (1.0f, Theme::px * 0.4f)));
        }
    }
    // the lights drifting up and fading
    for (const auto& l : lights)
    {
        const auto c = ribbonAt (l.hue);
        const float X = r.getX() + W * l.x, Y = r.getY() + H * l.y;
        softLight (g, X, Y, Theme::px * 9 * l.size, c, 0.35f * l.life);
        g.setColour (c.withAlpha (0.9f * l.life));
        g.fillRoundedRectangle (X - Theme::px * 1.2f * l.size, Y - Theme::px * 2.2f * l.size, Theme::px * 2.4f * l.size, Theme::px * 4.4f * l.size, Theme::px * l.size);
    }
}

// ---------------------------------------------------------------------------------------------------------- PASTEL
void VisualsPage::paintPastel (juce::Graphics& g, juce::Rectangle<float> r)
{
    const auto navy = juce::Colour (0xff1e1f45);
    g.setColour (navy);
    g.fillRect (r);
    const float W = r.getWidth(), H = r.getHeight(), y0 = r.getY() + H * 0.5f;
    const float x0 = r.getX() + W * 0.05f, span = W * 0.90f;
    const int n = 30;
    const float slot = span / n, bw = slot * 0.5f, rad = bw * 0.5f;
    const float gap = std::max (1.5f, Theme::px * 0.5f);
    for (int i = 0; i < n; ++i)
    {
        const float x = (i + 0.5f) / n;
        const float v = bandAt (x * 0.92f, true);
        const float shape = 0.75f + 0.25f * std::sin ((float) i * 0.9f + 0.6f);
        const float h = std::max (bw * 1.2f, H * (0.07f + 0.30f * v * shape));
        const auto c = pastelAt (x);
        const float bx = x0 + slot * i + (slot - bw) * 0.5f;
        // the bar: a rounded top, a touch of light down it
        juce::Path top;
        top.addRoundedRectangle (bx, y0 - h, bw, h, rad, rad, true, true, false, false);
        g.setGradientFill (juce::ColourGradient (c.brighter (0.06f), 0, y0 - h, c.darker (0.04f), 0, y0, false));
        g.fillPath (top);
        // its reflection: muted towards grey and the navy, a little shorter, rounded at the foot
        const float rh = h * 0.92f;
        const auto rc = c.interpolatedWith (juce::Colour (0xff6c6872), 0.55f).interpolatedWith (navy, 0.35f);
        juce::Path refl;
        refl.addRoundedRectangle (bx, y0 + gap, bw, rh, rad, rad, false, false, true, true);
        g.setGradientFill (juce::ColourGradient (rc, 0, y0, rc.interpolatedWith (navy, 0.25f), 0, y0 + rh, false));
        g.fillPath (refl);
    }
}

// ---------------------------------------------------------------------------------------------------------- MESH
void VisualsPage::paintMesh (juce::Graphics& g, juce::Rectangle<float> r)
{
    g.fillAll (juce::Colours::black);
    const float W = r.getWidth(), H = r.getHeight(), y0 = r.getY() + H * 0.6f;
    const float tt = (float) t;
    // soft coloured lights in the dark
    softLight (g, r.getX() + W * (0.44f + 0.03f * std::sin (tt * 0.2f)), r.getY() + H * 0.15f, H * 0.45f, juce::Colour (0xff5a5a2a), 0.22f + 0.15f * bass);
    softLight (g, r.getX() + W * 0.40f, r.getY() + H * 0.35f, H * 0.22f, juce::Colour (0xff1e8cff), 0.30f + 0.25f * mid);
    softLight (g, r.getX() + W * 0.47f, r.getY() + H * 0.40f, H * 0.18f, juce::Colour (0xff1edc8c), 0.25f + 0.2f * mid);
    softLight (g, r.getX() + W * 0.30f, r.getY() + H * 0.62f, H * 0.32f, juce::Colour (0xff3a1aa0), 0.20f);
    softLight (g, r.getX() + W * 0.55f, r.getY() + H * 0.62f, H * 0.30f, juce::Colour (0xffa01a6a), 0.13f + 0.12f * high);
    softLight (g, r.getX() + W * 0.11f, r.getY() + H * 0.58f, H * 0.06f, juce::Colour (0xffff2a1a), 0.6f + 0.4f * flash);
    softLight (g, r.getX() + W * 0.90f, r.getY() + H * 0.55f, H * 0.05f, juce::Colour (0xff2a5aff), 0.6f + 0.3f * high);

    // the ribbons: each a band of dashed lines twisting along its length (where it turns edge-on the lines bunch up)
    struct Rib { float yc, amp, f, sp, wid, twist, tsp, hue0; };
    const Rib ribs[3] = { { -0.02f, 0.16f, 1.10f, 0.22f, 0.13f, 2.2f, 0.35f, 0.00f },
                          {  0.05f, 0.10f, 1.70f, 0.31f, 0.09f, 3.1f, 0.47f, 0.25f },
                          {  0.10f, 0.07f, 2.30f, 0.40f, 0.06f, 3.9f, 0.58f, 0.55f } };
    const int lines = 22;
    const float dash = std::max (2.0f, Theme::px * 1.3f), stepX = dash * 2.0f;
    for (int ri = 0; ri < 3; ++ri)
    {
        const auto& R = ribs[ri];
        const float loud = bandAt (0.1f + 0.35f * ri);
        const int steps = (int) (W / stepX) + 2;
        for (int l = 0; l < lines; ++l)
        {
            const float s = (float) l / (lines - 1) * 2.0f - 1.0f;   // (across the ribbon, edge to edge)
            const int chunk = 10;
            for (int k0 = 0; k0 < steps; k0 += chunk)
            {
                juce::Path p;
                float depthSum = 0;
                float xm = 0;
                for (int k = k0; k < std::min (steps, k0 + chunk); ++k)
                {
                    float yA = 0, yB = 0, d = 0;
                    for (int e = 0; e < 2; ++e)
                    {
                        const float X = r.getX() + k * stepX + (e ? dash : 0.0f);
                        const float x = (X - r.getX()) / W;
                        const float centre = y0 + H * (R.yc + R.amp * (0.6f + 0.6f * loud) * std::sin (kTau * R.f * x + tt * R.sp + ri * 1.3f)
                                                      + 0.04f * std::sin (kTau * 3.1f * x - tt * 0.5f + ri));
                        const float wide = H * R.wid * (0.55f + 0.45f * std::sin (kTau * 0.8f * x + tt * 0.3f + ri * 2.0f)) * (0.8f + 0.6f * loud);
                        const float th = kTau * R.twist * x * 0.5f + tt * R.tsp + ri * 0.7f;
                        const float y = centre + s * wide * std::cos (th) - H * 0.25f * gauss (x, 0.42f + 0.05f * ri, 0.10f) * (1.0f - 0.25f * ri) * (0.7f + 0.5f * loud) * (ri == 0 ? 1.0f : 0.35f);
                        (e ? yB : yA) = y;
                        d = 0.5f + 0.5f * s * std::sin (th);
                    }
                    const float X = r.getX() + k * stepX;
                    p.startNewSubPath (X, yA);
                    p.lineTo (X + dash, yB);
                    depthSum += d;
                    xm += X;
                }
                const int cnt = std::min (steps, k0 + chunk) - k0;
                if (cnt <= 0) continue;
                const float depth = depthSum / cnt;
                const float xc = (xm / cnt - r.getX()) / W;
                const float edgeFade = juce::jlimit (0.0f, 1.0f, std::min (xc, 1.0f - xc) * 8.0f + 0.25f);
                const auto c = meshAt (std::fmod (xc * 0.9f + R.hue0 + tt * 0.01f, 1.0f));
                g.setColour (c.withAlpha ((0.18f + 0.72f * depth) * edgeFade));
                g.strokePath (p, juce::PathStrokeType (std::max (1.0f, Theme::px * 0.45f)));
            }
        }
    }
}

void VisualsPage::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    switch (shownScene())
    {
        case Prism:   paintPrism (g, r); break;
        case Rainbow: paintRainbow (g, r); break;
        case Ribbons: paintRibbons (g, r); break;
        case Pastel:  paintPastel (g, r); break;
        case Mesh:    paintMesh (g, r); break;
        case Auto: case Count: default: paintPrism (g, r); break;
    }
}

void VisualsPage::mouseMove (const juce::MouseEvent&) { idle = 0; if (! showControls) step (0.0); }
void VisualsPage::mouseDown (const juce::MouseEvent&) { idle = 0; step (0.0); grabKeyboardFocus(); }

bool VisualsPage::keyPressed (const juce::KeyPress& k)
{
    if (k == juce::KeyPress::escapeKey) { if (onClose) onClose(); return true; }
    if (k.getKeyCode() == juce::KeyPress::tabKey) { setScene ((Scene) (((int) scene + 1) % Count)); idle = 0; return true; }
    return false;   // (Space, Return and the rest: the editor's - play and stop as usual)
}
} // namespace prv::ui
