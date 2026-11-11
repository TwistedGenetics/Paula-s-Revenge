// Paula's Revenge - VISUALS (see VisualsPage.h)
#include "VisualsPage.h"
#include <cmath>

namespace prv::ui
{
namespace
{
    // the copper's colours: the classic raster rainbow
    juce::Colour copperColour (float h, float bright)
    {
        return juce::Colour::fromHSV (std::fmod (h, 1.0f), 0.85f, std::clamp (bright, 0.0f, 1.0f), 1.0f);
    }
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
    stars.resize (220);
    for (auto& s : stars) s = { rnd() * 2 - 1, rnd() * 2 - 1, rnd() };
    setScene (All);
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
    for (int i = 0; i < Count; ++i) sceneBtn[i].setToggleState (i == (int) s, juce::dontSendNotification);
    repaint();
}

void VisualsPage::step (double dt)
{
    t += dt;
    // the notes since last time: each flashes its track; any of them is a beat
    bool hit = false;
    for (int i = 0; i < kMaxTracks; ++i)
    {
        const int n = proc.player.uiNotes[i].load();
        if (n != lastNotes[i]) { trackHit[i] = 1.0f; hit = true; lastNotes[i] = n; }
        trackHit[i] = std::max (0.0f, trackHit[i] - (float) dt * 3.5f);
    }
    if (hit) { flash = 1.0f; speed = std::min (6.0f, speed + 2.2f); }
    flash = std::max (0.0f, flash - (float) dt * 2.5f);
    speed = 1.0f + (speed - 1.0f) * std::exp (-(float) dt * 3.0f);
    // the stars fly towards us, faster on the beat
    for (auto& s : stars)
    {
        s.z -= (float) dt * 0.25f * speed;
        if (s.z <= 0.02f) s = { rnd() * 2 - 1, rnd() * 2 - 1, 1.0f };
    }
    scroll += dt * (220.0 + 120.0 * flash) * Theme::px / 2.0;
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

void VisualsPage::paintCopper (juce::Graphics& g, juce::Rectangle<float> area)
{
    // five bars rolling up and down through each other, each a gradient of 24 raster lines; brighter on the beat
    const float h = area.getHeight();
    const int bars = 5;
    const float barH = std::max (8.0f, h / 9.0f);
    for (int b = 0; b < bars; ++b)
    {
        const float phase = (float) t * 1.3f + b * 0.55f;
        const float cy = area.getY() + h * 0.5f + std::sin (phase) * (h * 0.38f);
        const float hue = (float) b / bars + (float) t * 0.05f;
        const int lines = 24;
        for (int l = 0; l < lines; ++l)
        {
            const float u = (float) l / (lines - 1);
            const float shade = 1.0f - std::abs (u * 2 - 1);   // dark at the edges, bright in the middle
            g.setColour (copperColour (hue + u * 0.04f, 0.25f + 0.65f * shade + 0.25f * flash));
            g.fillRect (area.getX(), cy - barH * 0.5f + u * barH, area.getWidth(), barH / lines + 1.0f);
        }
    }
}

void VisualsPage::paintStars (juce::Graphics& g, juce::Rectangle<float> area)
{
    const auto c = area.getCentre();
    const float k = std::min (area.getWidth(), area.getHeight()) * 0.5f;
    for (const auto& s : stars)
    {
        const float sx = c.x + s.x / s.z * k, sy = c.y + s.y / s.z * k;
        if (! area.contains (sx, sy)) continue;
        const float b = std::clamp (1.2f - s.z, 0.15f, 1.0f);
        const float sz = std::max (1.0f, (1.0f - s.z) * 4.0f * Theme::px * 0.5f);
        g.setColour (juce::Colours::white.withAlpha (b));
        g.fillRect (sx, sy, sz, sz);
        // streaks when they rush
        if (speed > 1.8f)
        {
            const float tz = s.z + 0.04f * speed;
            g.drawLine (sx, sy, c.x + s.x / tz * k, c.y + s.y / tz * k, std::max (1.0f, sz * 0.5f));
        }
    }
}

void VisualsPage::paintScopes (juce::Graphics& g, juce::Rectangle<float> area)
{
    // each track's oscilloscope, side by side, in its own colour; a note flashes it
    const int n = std::clamp (proc.song.numTracks, 1, kMaxTracks);
    const float w = area.getWidth() / n;
    const uint32_t wpos = proc.player.uiScopeWrite.load();
    const int len = Player::kScopeLen;
    const int show = std::min (len, 1024);
    for (int tr = 0; tr < n; ++tr)
    {
        auto cell = juce::Rectangle<float> (area.getX() + tr * w, area.getY(), w, area.getHeight()).reduced (Theme::px * 2);
        const auto col = Theme::skin.track[tr % 8];
        g.setColour (col.withAlpha (0.10f + 0.35f * trackHit[tr]));
        g.fillRect (cell);
        g.setColour (col.withAlpha (0.5f + 0.5f * trackHit[tr]));
        g.drawRect (cell, std::max (1.0f, Theme::px * 0.5f));
        juce::Path p;
        const float mid = cell.getCentreY(), amp = cell.getHeight() * 0.45f;
        for (int i = 0; i < (int) cell.getWidth(); ++i)
        {
            const int idx = (int) ((wpos + (uint32_t) len - (uint32_t) show + (uint32_t) (i * show / std::max (1, (int) cell.getWidth()))) % (uint32_t) len);
            const float v = std::clamp (proc.player.uiScope[tr][idx] * 1.6f, -1.0f, 1.0f);
            const float x = cell.getX() + i, y = mid - v * amp;
            if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
        }
        g.setColour (col.brighter (0.4f + 0.6f * trackHit[tr]));
        g.strokePath (p, juce::PathStrokeType (std::max (1.5f, Theme::px * (0.8f + 0.8f * trackHit[tr]))));
        PixelFont::drawLeft (g, juce::String (tr + 1), cell.reduced (Theme::px * 2).removeFromTop (Theme::px * 9), Theme::px, col.brighter (0.6f));
    }
}

void VisualsPage::paintScroller (juce::Graphics& g, juce::Rectangle<float> area)
{
    // the sine scroller: big letters riding a wave, right to left
    const float px = Theme::px * 3.0f;
    const float cw = px * 6.0f;
    const auto& s = proc.song;
    const juce::String msg = "     PAULA'S REVENGE   *   " + juce::String (s.name).toUpperCase() + "   *   "
                             + juce::String ((int) std::lround (s.tempo)) + " BPM   *   BLOCK "
                             + juce::String (proc.player.uiBlock.load()).paddedLeft ('0', 3) + "   *   GREETINGS TO EVERY TRACKER IN THE JUNGLE   *   ";
    const int nChars = msg.length();
    const double total = nChars * cw;
    const double off = std::fmod (scroll, total);
    const int first = (int) (off / cw);
    const float baseY = area.getCentreY() - px * 3.5f;
    for (int i = 0; i * cw < area.getWidth() + cw * 2; ++i)
    {
        const int ci = (first + i) % nChars;
        const float x = area.getX() + (float) (i * cw - std::fmod (off, cw));
        const float y = baseY + std::sin ((float) t * 3.0f + x * 0.012f) * area.getHeight() * 0.28f;
        const auto col = copperColour ((float) t * 0.1f + x / std::max (1.0f, area.getWidth()), 0.95f);
        PixelFont::drawLeft (g, msg.substring (ci, ci + 1), { x, y, cw + px, px * 9.0f }, px, col.brighter (0.3f * flash));
    }
}

void VisualsPage::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::black);
    auto r = getLocalBounds().toFloat();
    if (scene == Copper) paintCopper (g, r);
    else if (scene == Stars) paintStars (g, r);
    else if (scene == Scopes) paintScopes (g, r.reduced (Theme::px * 6, r.getHeight() * 0.2f));
    else
    {
        // ALL: stars behind, copper bars through the top, the scopes in the middle, the scroller along the bottom
        paintStars (g, r);
        paintCopper (g, r.withHeight (r.getHeight() * 0.42f));
        paintScopes (g, juce::Rectangle<float> (r.getX() + Theme::px * 6, r.getY() + r.getHeight() * 0.46f, r.getWidth() - Theme::px * 12, r.getHeight() * 0.26f));
        paintScroller (g, r.withTrimmedTop (r.getHeight() * 0.74f));
    }
    if (scene == Copper || scene == Stars) paintScroller (g, r.withTrimmedTop (r.getHeight() * 0.7f));
    // the beat: a white flash over everything
    if (flash > 0.6f) { g.setColour (juce::Colours::white.withAlpha ((flash - 0.6f) * 0.25f)); g.fillRect (r); }
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
