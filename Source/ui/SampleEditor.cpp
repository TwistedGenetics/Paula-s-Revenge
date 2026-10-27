// Paula's Revenge - sample (instrument waveform) editor
#include "SampleEditor.h"
#include "../SongFile.h"

namespace prv::ui
{
SampleEditor::Clip SampleEditor::clipboard;

namespace
{
    // keep the loop valid after removing [a, b)
    void loopAfterRemove (Instrument& in, int a, int b)
    {
        if (! (in.loopLength >= 4)) { in.loopStart = in.loopLength = 0; return; }
        const int n = b - a;
        int ls = in.loopStart, le = in.loopStart + in.loopLength;
        auto mapPos = [&] (int p) { return p <= a ? p : (p >= b ? p - n : a); };
        ls = mapPos (ls); le = mapPos (le);
        in.loopStart = ls; in.loopLength = le - ls;
        if (! in.isLooped()) { in.loopStart = in.loopLength = 0; }
    }

    // keep the loop in place after inserting n samples at p
    void loopAfterInsert (Instrument& in, int p, int n)
    {
        if (in.loopLength < 4) return;
        if (p <= in.loopStart) in.loopStart += n;
        else if (p < in.loopStart + in.loopLength) in.loopLength += n;
        if (! in.isLooped()) { in.loopStart = in.loopLength = 0; }
    }

    int16_t clip16 (double v) { return (int16_t) juce::jlimit (-32768, 32767, (int) std::lround (v)); }
}

SampleEditor::SampleEditor (PaulaProcessor& p) : proc (p)
{
    setWantsKeyboardFocus (true);
    setOpaque (true);

    playBtn.onClick    = [this] { play (false); };
    playSelBtn.onClick = [this] { play (true); };
    stopBtn.onClick    = [this] { stop(); };
    zoomInBtn.onClick  = [this] { zoom (0.5, hasSel() ? (selA + selB) * 0.5 : viewStart + viewLen * 0.5); };
    zoomOutBtn.onClick = [this] { zoom (2.0, viewStart + viewLen * 0.5); };
    showAllBtn.onClick = [this] { viewStart = 0; viewLen = length(); clampView(); repaint(); };
    zoomSelBtn.onClick = [this] { if (hasSel()) { viewStart = selA; viewLen = selB - selA; clampView(); repaint(); } };
    selAllBtn.onClick  = [this] { selectAll(); };
    drawBtn.onClick    = [this] { drawMode = drawBtn.getToggleState(); repaint(); };

    cutBtn.onClick      = [this] { apply (Op::Cut); };
    copyBtn.onClick     = [this] { apply (Op::Copy); };
    pasteBtn.onClick    = [this] { apply (Op::Paste); };
    deleteBtn.onClick   = [this] { apply (Op::Delete); };
    cropBtn.onClick     = [this] { apply (Op::Crop); };
    silenceBtn.onClick  = [this] { apply (Op::Silence); };
    reverseBtn.onClick  = [this] { apply (Op::Reverse); };
    normBtn.onClick     = [this] { apply (Op::Normalise); };
    fadeInBtn.onClick   = [this] { apply (Op::FadeIn); };
    fadeOutBtn.onClick  = [this] { apply (Op::FadeOut); };
    gainUpBtn.onClick   = [this] { apply (Op::GainUp); };
    gainDownBtn.onClick = [this] { apply (Op::GainDown); };
    loopSelBtn.onClick  = [this] { apply (Op::LoopToSel); };
    monoBtn.onClick     = [this] { apply (Op::ToMono); };
    undoBtn.onClick     = [this] { undo(); };
    redoBtn.onClick     = [this] { redo(); };
    closeBtn.onClick    = [this] { stop(); if (onClose) onClose(); };

    for (auto* b : { &playBtn, &playSelBtn, &loopBtn, &stopBtn, &zoomInBtn, &zoomOutBtn, &showAllBtn, &zoomSelBtn, &selAllBtn, &drawBtn,
                     &cutBtn, &copyBtn, &pasteBtn, &deleteBtn, &cropBtn, &silenceBtn, &reverseBtn, &normBtn, &fadeInBtn, &fadeOutBtn,
                     &gainDownBtn, &gainUpBtn, &loopSelBtn, &monoBtn, &undoBtn, &redoBtn, &closeBtn })
        addAndMakeVisible (b);

    instrumentSelected();
}

// ------------------------------------------------------------------ helpers
const Instrument& SampleEditor::inst() const
{
    return proc.song.instruments[(size_t) juce::jlimit (0, kMaxInstruments - 1, proc.editorState.instrument)];
}

int SampleEditor::length() const { return (int) inst().data.size(); }

void SampleEditor::instrumentSelected()
{
    if (instrIndex != proc.editorState.instrument)
    {
        instrIndex = proc.editorState.instrument;
        undoStack.clear(); redoStack.clear();
        selA = selB = 0; cursor = 0;
        viewStart = 0; viewLen = std::max (1, length());
    }
    clampView();
    repaint();
}

void SampleEditor::clampView()
{
    const double len = std::max (1, length());
    const double minLen = std::min (len, std::max (8.0, (double) waveArea.getWidth() / 24.0));
    viewLen = juce::jlimit (minLen, len, viewLen);
    viewStart = juce::jlimit (0.0, len - viewLen, viewStart);
    selA = juce::jlimit (0, length(), selA);
    selB = juce::jlimit (0, length(), selB);
    cursor = juce::jlimit (0, length(), cursor);
}

void SampleEditor::zoom (double factor, double around)
{
    const double rel = (around - viewStart) / std::max (1.0, viewLen);
    viewLen *= factor;
    clampView();
    viewStart = around - rel * viewLen;
    clampView();
    repaint();
}

double SampleEditor::xToSample (float x) const
{
    return viewStart + (x - waveArea.getX()) / std::max (1.0f, waveArea.getWidth()) * viewLen;
}

float SampleEditor::sampleToX (double s) const
{
    return waveArea.getX() + (float) ((s - viewStart) / std::max (1.0, viewLen)) * waveArea.getWidth();
}

juce::String SampleEditor::fmt (int samples) const { return juce::String (samples); }

void SampleEditor::selectAll() { selA = 0; selB = length(); repaint(); }

void SampleEditor::pushUndo()
{
    undoStack.push_back (inst());
    if (undoStack.size() > 40) undoStack.erase (undoStack.begin());
    redoStack.clear();
}

void SampleEditor::commit (Instrument&& edited, const juce::String& what)
{
    stop();
    proc.withLock ([&] { std::swap (proc.song.instruments[(size_t) proc.editorState.instrument], edited); });
    // `edited` now holds the old data and is freed here, off the audio thread
    clampView();
    repaint();
    if (onChanged) onChanged();
    if (onStatus && what.isNotEmpty()) onStatus (what);
}

void SampleEditor::undo()
{
    if (undoStack.empty()) return;
    redoStack.push_back (inst());
    Instrument prev = std::move (undoStack.back());
    undoStack.pop_back();
    commit (std::move (prev), "UNDO");
}

void SampleEditor::redo()
{
    if (redoStack.empty()) return;
    undoStack.push_back (inst());
    Instrument next = std::move (redoStack.back());
    redoStack.pop_back();
    commit (std::move (next), "REDO");
}

// ------------------------------------------------------------------ editing
void SampleEditor::apply (Op op)
{
    const Instrument& cur = inst();
    const int len = length();
    const bool sel = hasSel();
    const int a = sel ? selA : 0, b = sel ? selB : len;
    const bool stereo = cur.isStereo();

    if (op == Op::Copy || op == Op::Cut)
    {
        if (! sel) { if (onStatus) onStatus ("SELECT SOMETHING FIRST (DRAG IN THE WAVEFORM)"); return; }
        clipboard.left.assign (cur.data.begin() + a, cur.data.begin() + b);
        clipboard.right.clear();
        if (stereo) clipboard.right.assign (cur.dataR.begin() + a, cur.dataR.begin() + b);
        if (op == Op::Copy) { if (onStatus) onStatus ("COPIED " + fmt (b - a) + " SAMPLES"); return; }
    }
    if (op == Op::Paste && clipboard.empty()) { if (onStatus) onStatus ("NOTHING TO PASTE"); return; }
    if (len == 0 && op != Op::Paste) { if (onStatus) onStatus ("THIS INSTRUMENT HAS NO SAMPLE"); return; }

    pushUndo();
    Instrument e = cur;
    // (checks for a right channel directly: lengths differ part-way through an edit)
    auto forEachChan = [&] (auto&& fn) { fn (e.data); if (! e.dataR.empty()) fn (e.dataR); };
    juce::String msg;

    switch (op)
    {
        case Op::Cut:
        case Op::Delete:
        {
            if (! sel) { undoStack.pop_back(); if (onStatus) onStatus ("SELECT SOMETHING FIRST"); return; }
            forEachChan ([&] (std::vector<int16_t>& d) { d.erase (d.begin() + a, d.begin() + b); });
            loopAfterRemove (e, a, b);
            cursor = a; selA = selB = 0;
            msg = (op == Op::Cut ? "CUT " : "DELETED ") + fmt (b - a) + " SAMPLES";
            break;
        }
        case Op::Crop:
        {
            if (! sel) { undoStack.pop_back(); if (onStatus) onStatus ("SELECT WHAT TO KEEP FIRST"); return; }
            forEachChan ([&] (std::vector<int16_t>& d) { d = std::vector<int16_t> (d.begin() + a, d.begin() + b); });
            if (e.loopLength >= 4)
            {
                // keep whatever part of the loop survives the crop
                const int ls = juce::jlimit (0, b - a, e.loopStart - a);
                const int le = juce::jlimit (0, b - a, e.loopStart + e.loopLength - a);
                e.loopStart = ls; e.loopLength = le - ls;
                if (! e.isLooped()) e.loopStart = e.loopLength = 0;
            }
            selA = 0; selB = b - a; cursor = 0; viewStart = 0; viewLen = b - a;
            msg = "CROPPED TO " + fmt (b - a) + " SAMPLES";
            break;
        }
        case Op::Paste:
        {
            // match channel counts: mono clip into stereo = same on both sides,
            // stereo clip into mono = sample becomes stereo
            const bool clipStereo = ! clipboard.right.empty();
            const bool wantStereo = clipStereo || e.isStereo();
            if (wantStereo && ! e.isStereo()) e.dataR = e.data;
            const auto& cl = clipboard.left;
            const auto& cr = clipStereo ? clipboard.right : clipboard.left;
            const int at = sel ? a : cursor;
            if (sel)
            {
                forEachChan ([&] (std::vector<int16_t>& d) { d.erase (d.begin() + a, d.begin() + b); });
                loopAfterRemove (e, a, b);
            }
            e.data.insert (e.data.begin() + at, cl.begin(), cl.end());
            if (wantStereo) e.dataR.insert (e.dataR.begin() + at, cr.begin(), cr.end());
            loopAfterInsert (e, at, (int) cl.size());
            selA = at; selB = at + (int) cl.size(); cursor = selB;
            if (len == 0) { viewStart = 0; viewLen = (double) cl.size(); }
            msg = "PASTED " + fmt ((int) cl.size()) + " SAMPLES";
            break;
        }
        case Op::Silence:
            forEachChan ([&] (std::vector<int16_t>& d) { std::fill (d.begin() + a, d.begin() + b, (int16_t) 0); });
            msg = "SILENCED";
            break;
        case Op::Reverse:
            forEachChan ([&] (std::vector<int16_t>& d) { std::reverse (d.begin() + a, d.begin() + b); });
            msg = "REVERSED";
            break;
        case Op::Normalise:
        {
            int peak = 1;
            forEachChan ([&] (std::vector<int16_t>& d) { for (int i = a; i < b; ++i) peak = std::max (peak, std::abs ((int) d[(size_t) i])); });
            const double g = 32112.0 / peak;
            forEachChan ([&] (std::vector<int16_t>& d) { for (int i = a; i < b; ++i) d[(size_t) i] = clip16 (d[(size_t) i] * g); });
            msg = "NORMALISED (" + juce::String (juce::Decibels::gainToDecibels (g), 1) + " DB)";
            break;
        }
        case Op::FadeIn:
        case Op::FadeOut:
        {
            const int n = std::max (1, b - a);
            forEachChan ([&] (std::vector<int16_t>& d)
            {
                for (int i = a; i < b; ++i)
                {
                    const double t = (i - a) / (double) n;
                    d[(size_t) i] = clip16 (d[(size_t) i] * (op == Op::FadeIn ? t : 1.0 - t));
                }
            });
            msg = op == Op::FadeIn ? "FADED IN" : "FADED OUT";
            break;
        }
        case Op::GainUp:
        case Op::GainDown:
        {
            const double g = op == Op::GainUp ? 1.4125 : 0.7079;   // +/- 3 dB
            forEachChan ([&] (std::vector<int16_t>& d) { for (int i = a; i < b; ++i) d[(size_t) i] = clip16 (d[(size_t) i] * g); });
            msg = op == Op::GainUp ? "VOLUME +3 DB" : "VOLUME -3 DB";
            break;
        }
        case Op::LoopToSel:
            if (! sel) { undoStack.pop_back(); if (onStatus) onStatus ("SELECT THE LOOP FIRST"); return; }
            if (b - a < 4) { undoStack.pop_back(); if (onStatus) onStatus ("LOOP IS TOO SHORT"); return; }
            e.loopStart = a; e.loopLength = b - a;
            msg = "LOOP SET TO " + fmt (a) + " - " + fmt (b);
            break;
        case Op::ToMono:
            if (! stereo) { undoStack.pop_back(); if (onStatus) onStatus ("ALREADY MONO"); return; }
            for (size_t i = 0; i < e.data.size(); ++i) e.data[i] = (int16_t) ((e.data[i] + e.dataR[i]) / 2);
            e.dataR.clear();
            msg = "MIXED TO MONO";
            break;
        case Op::Copy:
            break;
    }
    commit (std::move (e), msg);
}

// ------------------------------------------------------------------ playback
void SampleEditor::play (bool selectionOnly)
{
    const auto& in = inst();
    if (! in.hasData()) return;
    auto audio = std::make_unique<PaulaProcessor::PreviewAudio>();
    audio->rate = kSampleRateAtC3;
    audio->left.resize (in.data.size());
    for (size_t i = 0; i < in.data.size(); ++i) audio->left[i] = in.data[i] / 32768.0f * (in.volume / 64.0f);
    if (in.isStereo())
    {
        audio->right.resize (in.dataR.size());
        for (size_t i = 0; i < in.dataR.size(); ++i) audio->right[i] = in.dataR[i] / 32768.0f * (in.volume / 64.0f);
    }
    int start = 0, end = -1;
    bool loop = loopBtn.getToggleState();
    if (selectionOnly && hasSel()) { start = selA; end = selB; }
    else if (! selectionOnly)
    {
        start = hasSel() ? selA : cursor;
        if (start >= length()) start = 0;
        if (in.isLooped() && loop) { /* play from start, loop the sample's own loop */ }
    }
    if (! selectionOnly && loop && in.isLooped())
    {
        // play up to the loop end, then loop the loop region
        auto old = proc.startPreview (std::move (audio), start, in.loopStart + in.loopLength, false);
        juce::ignoreUnused (old);
    }
    else
    {
        auto old = proc.startPreview (std::move (audio), start, end, loop);
        juce::ignoreUnused (old);
    }
    previewing = true;
}

void SampleEditor::stop()
{
    if (previewing) { auto old = proc.stopPreview(); juce::ignoreUnused (old); }
    previewing = false;
    repaint();
}

void SampleEditor::tick()
{
    if (previewing)
    {
        if (proc.previewPos.load() < 0) previewing = false;
        repaint();
    }
    if (instrIndex != proc.editorState.instrument) instrumentSelected();
}

// ------------------------------------------------------------------ layout & paint
void SampleEditor::resized()
{
    const float px = Theme::px;
    auto r = getLocalBounds();
    const int rowH = (int) (px * 11);
    const int gap = (int) std::max (2.0f, px);

    auto layoutRow = [&] (juce::Rectangle<int> row, std::initializer_list<PixButton*> buttons)
    {
        const int n = (int) buttons.size();
        const int w = (row.getWidth() - gap * (n - 1)) / n;
        for (auto* b : buttons) { b->setBounds (row.removeFromLeft (w)); row.removeFromLeft (gap); }
    };
    auto inner = r.reduced (gap);
    std::vector<juce::Rectangle<int>> rows;
    for (int i = 0; i < 5; ++i) { rows.insert (rows.begin(), inner.removeFromBottom (rowH)); inner.removeFromBottom (gap); }
    inner.removeFromBottom (gap);
    layoutRow (rows[0], { &playBtn, &playSelBtn, &loopBtn, &stopBtn, &undoBtn, &redoBtn });
    layoutRow (rows[1], { &zoomInBtn, &zoomOutBtn, &zoomSelBtn, &showAllBtn, &selAllBtn, &drawBtn });
    layoutRow (rows[2], { &cutBtn, &copyBtn, &pasteBtn, &deleteBtn, &cropBtn, &silenceBtn });
    layoutRow (rows[3], { &reverseBtn, &normBtn, &gainDownBtn, &gainUpBtn, &fadeInBtn });
    layoutRow (rows[4], { &fadeOutBtn, &loopSelBtn, &monoBtn, &closeBtn });

    auto area = inner.toFloat();
    headerArea = area.removeFromTop (px * 11);
    area.removeFromTop (px);
    overviewArea = area.removeFromBottom (px * 12);
    area.removeFromBottom (px * 2);
    waveArea = area;
    clampView();
}

void SampleEditor::paintLane (juce::Graphics& g, juce::Rectangle<float> lane, const std::vector<int16_t>& d, juce::Colour col)
{
    const auto& sk = Theme::skin;
    const float mid = lane.getCentreY();
    const float half = lane.getHeight() * 0.47f;
    g.setColour (sk.rule.withAlpha (0.8f));
    g.fillRect (lane.getX(), mid, lane.getWidth(), 1.0f);
    g.setColour (sk.dim.withAlpha (0.5f));
    g.fillRect (lane.getX(), mid - half * 0.5f, lane.getWidth(), 1.0f);
    g.fillRect (lane.getX(), mid + half * 0.5f, lane.getWidth(), 1.0f);
    if (d.empty()) return;

    g.setColour (col);
    const int n = (int) d.size();
    const double spp = viewLen / std::max (1.0f, lane.getWidth());
    if (spp >= 1.0)
    {
        const int cols = (int) lane.getWidth();
        for (int x = 0; x < cols; ++x)
        {
            const int s0 = juce::jlimit (0, n - 1, (int) (viewStart + x * spp));
            const int s1 = juce::jlimit (s0 + 1, n, (int) (viewStart + (x + 1) * spp) + 1);
            int lo = 32767, hi = -32768;
            for (int i = s0; i < s1; ++i) { lo = std::min (lo, (int) d[(size_t) i]); hi = std::max (hi, (int) d[(size_t) i]); }
            const float y1 = mid - hi / 32768.0f * half, y2 = mid - lo / 32768.0f * half;
            g.fillRect (lane.getX() + x, y1, 1.0f, std::max (1.0f, y2 - y1));
        }
    }
    else
    {
        // zoomed in: draw Paula-style steps with a dot on each sample
        const int i0 = std::max (0, (int) std::floor (viewStart));
        const int i1 = std::min (n, (int) std::ceil (viewStart + viewLen) + 1);
        const float t = std::max (1.5f, Theme::px * 0.6f);
        float prevY = -1;
        for (int i = i0; i < i1; ++i)
        {
            const float x0 = sampleToX (i), x1 = sampleToX (i + 1);
            const float y = mid - d[(size_t) i] / 32768.0f * half;
            g.fillRect (x0, y - t * 0.5f, x1 - x0, t);
            if (prevY >= 0) g.fillRect (x0 - t * 0.5f, std::min (prevY, y) - t * 0.5f, t, std::abs (prevY - y) + t);
            if (x1 - x0 > t * 5) g.fillEllipse (x0 - t, y - t, t * 2, t * 2);
            prevY = y;
        }
    }
}

void SampleEditor::paint (juce::Graphics& g)
{
    const auto& sk = Theme::skin;
    const float px = Theme::px;
    g.fillAll (Theme::panel);
    const auto& in = inst();
    const int len = length();

    // header text
    {
        const juce::String idx = juce::String::toHexString (proc.editorState.instrument + 1).toUpperCase().paddedLeft ('0', 2);
        juce::String left = "SAMPLE " + idx + "  " + (in.name.empty() ? juce::String ("(NO NAME)") : juce::String (in.name));
        juce::String right = juce::String (len) + " SMP  " + (in.isStereo() ? "STEREO" : "MONO");
        if (hasSel()) right = "SEL " + juce::String (selA) + "-" + juce::String (selB) + " (" + juce::String (selB - selA) + ")  " + right;
        else right = "POS " + juce::String (cursor) + "  " + right;
        float hp = px;
        while (hp > 1.5f && PixelFont::textWidth (left + "  " + right, hp) > headerArea.getWidth()) hp -= 0.5f;
        PixelFont::drawLeft (g, left, headerArea, hp, Theme::ink);
        PixelFont::draw (g, right, headerArea.getRight() - PixelFont::textWidth (right, hp) + hp,
                         std::round (headerArea.getCentreY() - 3.5f * hp), hp, Theme::ink);
    }

    // waveform
    g.setColour (sk.bg);
    g.fillRect (waveArea);
    if (len == 0)
    {
        PixelFont::drawCentred (g, "NO SAMPLE - LOAD ONE, OR PASTE", waveArea, px, sk.dim);
    }
    else
    {
        // loop region
        if (in.isLooped())
        {
            const float x1 = sampleToX (in.loopStart), x2 = sampleToX (in.loopStart + in.loopLength);
            g.setColour (sk.stripe);
            g.fillRect (juce::Rectangle<float> (x1, waveArea.getY(), x2 - x1, waveArea.getHeight()).getIntersection (waveArea));
        }
        // selection
        if (hasSel())
        {
            const float x1 = sampleToX (selA), x2 = sampleToX (selB);
            g.setColour (sk.band);
            g.fillRect (juce::Rectangle<float> (x1, waveArea.getY(), x2 - x1, waveArea.getHeight()).getIntersection (waveArea));
        }
        g.saveState();
        g.reduceClipRegion (waveArea.toNearestInt());
        if (in.isStereo())
        {
            auto top = waveArea.withHeight (waveArea.getHeight() * 0.5f);
            auto bot = waveArea.withTrimmedTop (waveArea.getHeight() * 0.5f);
            paintLane (g, top.reduced (0, px), in.data, sk.track[0]);
            paintLane (g, bot.reduced (0, px), in.dataR, sk.track[1]);
            g.setColour (sk.rule);
            g.fillRect (waveArea.getX(), bot.getY(), waveArea.getWidth(), 1.0f);
            PixelFont::draw (g, "L", waveArea.getX() + px * 2, top.getY() + px * 2, px, sk.text);
            PixelFont::draw (g, "R", waveArea.getX() + px * 2, bot.getY() + px * 2, px, sk.text);
        }
        else paintLane (g, waveArea.reduced (0, px), in.data, sk.track[0]);

        // loop markers
        if (in.isLooped())
        {
            g.setColour (sk.hot);
            for (int m : { in.loopStart, in.loopStart + in.loopLength })
            {
                const float x = sampleToX (m);
                g.fillRect (x - 1.0f, waveArea.getY(), 2.0f, waveArea.getHeight());
            }
            PixelFont::draw (g, "LOOP", sampleToX (in.loopStart) + px * 2, waveArea.getBottom() - px * 9, px, sk.hot);
        }
        // cursor + playhead
        g.setColour (sk.text);
        if (! hasSel()) g.fillRect (sampleToX (cursor) - 0.5f, waveArea.getY(), std::max (1.0f, px * 0.5f), waveArea.getHeight());
        if (previewing && proc.previewPos.load() >= 0)
        {
            g.setColour (sk.hot);
            g.fillRect (sampleToX (proc.previewPos.load()) - 1.0f, waveArea.getY(), 2.0f, waveArea.getHeight());
        }
        g.restoreState();
    }
    Theme::bevel (g, waveArea, false, std::max (1.0f, px * 0.5f));
    if (drawMode) PixelFont::draw (g, "DRAW MODE", waveArea.getRight() - PixelFont::textWidth ("DRAW MODE", px) - px * 2, waveArea.getY() + px * 2, px, sk.hot);

    // overview of the whole sample with the visible window
    g.setColour (sk.bg);
    g.fillRect (overviewArea);
    if (len > 0)
    {
        const float mid = overviewArea.getCentreY(), half = overviewArea.getHeight() * 0.45f;
        g.setColour (sk.dim);
        const int cols = (int) overviewArea.getWidth();
        for (int x = 0; x < cols; ++x)
        {
            const int s0 = (int) ((int64_t) x * len / cols), s1 = std::max (s0 + 1, (int) ((int64_t) (x + 1) * len / cols));
            int lo = 32767, hi = -32768;
            for (int i = s0; i < s1 && i < len; i += std::max (1, (s1 - s0) / 64)) { lo = std::min (lo, (int) in.data[(size_t) i]); hi = std::max (hi, (int) in.data[(size_t) i]); }
            g.fillRect (overviewArea.getX() + x, mid - hi / 32768.0f * half, 1.0f, std::max (1.0f, (hi - lo) / 32768.0f * half));
        }
        const float vx = overviewArea.getX() + (float) (viewStart / len) * overviewArea.getWidth();
        const float vw = std::max (2.0f, (float) (viewLen / len) * overviewArea.getWidth());
        g.setColour (sk.text);
        g.drawRect (juce::Rectangle<float> (vx, overviewArea.getY(), vw, overviewArea.getHeight()), std::max (1.0f, px * 0.5f));
    }
    Theme::bevel (g, overviewArea, false, std::max (1.0f, px * 0.5f));

    if (hasKeyboardFocus (true))
    {
        g.setColour (Theme::ink);
        g.drawRect (getLocalBounds().toFloat(), std::max (2.0f, px * 0.5f));
    }
}

// ------------------------------------------------------------------ mouse
void SampleEditor::drawAt (const juce::MouseEvent& e)
{
    const auto& in = inst();
    if (! in.hasData()) return;
    const int s = juce::jlimit (0, length() - 1, (int) std::floor (xToSample (e.position.x)));
    auto lane = waveArea;
    if (in.isStereo()) lane = drawLane == 0 ? waveArea.withHeight (waveArea.getHeight() * 0.5f) : waveArea.withTrimmedTop (waveArea.getHeight() * 0.5f);
    lane = lane.reduced (0, Theme::px);
    const float half = lane.getHeight() * 0.47f;
    const int v = juce::jlimit (-32768, 32767, (int) std::lround ((lane.getCentreY() - e.position.y) / half * 32768.0f));
    const int from = lastDrawSample < 0 ? s : lastDrawSample;
    const int fromV = lastDrawSample < 0 ? v : lastDrawValue;
    proc.withLock ([&]
    {
        auto& d = (in.isStereo() && drawLane == 1) ? proc.song.instruments[(size_t) proc.editorState.instrument].dataR
                                                   : proc.song.instruments[(size_t) proc.editorState.instrument].data;
        const int lo = std::min (from, s), hi = std::max (from, s);
        for (int i = lo; i <= hi; ++i)
        {
            const double t = hi == lo ? 1.0 : (i - from) / (double) (s - from);
            d[(size_t) i] = clip16 (fromV + (v - fromV) * t);
        }
    });
    lastDrawSample = s; lastDrawValue = v;
    repaint();
}

void SampleEditor::mouseDown (const juce::MouseEvent& e)
{
    grabKeyboardFocus();
    dragOverview = 0;
    if (overviewArea.contains (e.position) && length() > 0)
    {
        dragOverview = 1;
        viewStart = (e.position.x - overviewArea.getX()) / overviewArea.getWidth() * length() - viewLen * 0.5;
        clampView(); repaint();
        return;
    }
    if (! waveArea.contains (e.position)) return;

    if (drawMode)
    {
        pushUndo();
        drawLane = (inst().isStereo() && e.position.y > waveArea.getCentreY()) ? 1 : 0;
        lastDrawSample = -1;
        drawAt (e);
        return;
    }
    const int s = juce::jlimit (0, length(), (int) std::lround (xToSample (e.position.x)));
    if (e.mods.isShiftDown() && (hasSel() || cursor != s))
    {
        const int anchor = hasSel() ? (std::abs (s - selA) < std::abs (s - selB) ? selB : selA) : cursor;
        dragAnchor = anchor;
        selA = std::min (anchor, s); selB = std::max (anchor, s);
    }
    else
    {
        dragAnchor = s;
        cursor = s;
        selA = selB = 0;
    }
    dragging = true;
    repaint();
}

void SampleEditor::mouseDrag (const juce::MouseEvent& e)
{
    if (dragOverview)
    {
        viewStart = (e.position.x - overviewArea.getX()) / overviewArea.getWidth() * length() - viewLen * 0.5;
        clampView(); repaint();
        return;
    }
    if (drawMode) { if (lastDrawSample >= 0 || waveArea.contains (e.position)) drawAt (e); return; }
    if (! dragging) return;
    // drag past the edges to scroll
    if (e.position.x > waveArea.getRight()) { viewStart += viewLen * 0.02; clampView(); }
    if (e.position.x < waveArea.getX())     { viewStart -= viewLen * 0.02; clampView(); }
    const int s = juce::jlimit (0, length(), (int) std::lround (xToSample (juce::jlimit (waveArea.getX(), waveArea.getRight(), e.position.x))));
    selA = std::min (dragAnchor, s); selB = std::max (dragAnchor, s);
    repaint();
}

void SampleEditor::mouseUp (const juce::MouseEvent&)
{
    if (drawMode && lastDrawSample >= 0)
    {
        lastDrawSample = -1;
        if (onChanged) onChanged();
    }
    dragging = false;
    dragOverview = 0;
}

void SampleEditor::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (waveArea.contains (e.position) && ! drawMode) selectAll();
}

void SampleEditor::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    if (! waveArea.contains (e.position)) return;
    if (std::abs (w.deltaX) > std::abs (w.deltaY))
    {
        viewStart -= w.deltaX * viewLen * 0.5;
        clampView(); repaint();
    }
    else if (w.deltaY != 0)
        zoom (w.deltaY > 0 ? 0.8 : 1.25, xToSample (e.position.x));
}

// ------------------------------------------------------------------ keys
bool SampleEditor::keyPressed (const juce::KeyPress& k)
{
    const auto m = k.getModifiers();
    const int code = k.getKeyCode();
    if (m.isCommandDown())
    {
        const auto ch = juce::CharacterFunctions::toLowerCase ((juce::juce_wchar) code);
        if (ch == 'z') { if (m.isShiftDown()) redo(); else undo(); return true; }
        if (ch == 'y') { redo(); return true; }
        if (ch == 'x') { apply (Op::Cut); return true; }
        if (ch == 'c') { apply (Op::Copy); return true; }
        if (ch == 'v') { apply (Op::Paste); return true; }
        if (ch == 'a') { selectAll(); return true; }
        return false;
    }
    if (code == juce::KeyPress::spaceKey) { if (previewing) stop(); else play (hasSel()); return true; }
    if (code == juce::KeyPress::escapeKey) { if (previewing) stop(); else if (onClose) onClose(); return true; }
    if (code == juce::KeyPress::deleteKey || code == juce::KeyPress::backspaceKey) { apply (Op::Delete); return true; }
    if (code == juce::KeyPress::leftKey)  { viewStart -= viewLen * 0.1; clampView(); repaint(); return true; }
    if (code == juce::KeyPress::rightKey) { viewStart += viewLen * 0.1; clampView(); repaint(); return true; }
    if (code == juce::KeyPress::upKey)    { zoom (0.5, hasSel() ? (selA + selB) * 0.5 : viewStart + viewLen * 0.5); return true; }
    if (code == juce::KeyPress::downKey)  { zoom (2.0, viewStart + viewLen * 0.5); return true; }
    return false;
}
} // namespace prv::ui
