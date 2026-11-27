#include "ChopUI/Tabs.h"

namespace ui
{
    using namespace skin;
    using bc::Badge;

    // =====================================================================================
    //  ParamPage
    // =====================================================================================
    ParamPage::ParamPage (ChoppageProcessor& p) : proc (p) {}

    void ParamPage::addRow (const juce::String& group, const juce::String& title)
    {
        rows.push_back ({ group, title, {}, nextRule });
        nextRule = false;
    }

    void ParamPage::addRule() { nextRule = true; }

    void ParamPage::addText (const juce::String& t, int width)
    {
        auto c = std::make_unique<LcdCell>();
        c->text = [t] { return t; };
        c->fontSize = kFont;
        c->setInterceptsMouseClicks (false, false);
        addAndMakeVisible (*c);
        rows.back().items.push_back ({ std::move (c), width > 0 ? width : textWidth (t) });
    }

    void ParamPage::addValue (const char* id, const juce::String& caption, int width, float fineStep)
    {
        if (caption.isNotEmpty()) addText (caption);
        auto c = std::make_unique<LcdCell>();
        const Param prm = param (id);
        c->text = [prm] { return prm.text().toUpperCase(); };
        c->underline = true;
        c->fontSize = kFont;
        c->just = juce::Justification::centred;
        c->onDrag = [prm, fineStep] (float steps, const juce::ModifierKeys& m)
        {
            if (auto* ch = dynamic_cast<juce::AudioParameterChoice*> (prm.p))
                prm.set ((float) juce::jlimit (0, ch->choices.size() - 1, prm.choice() + (int) steps));
            else if (dynamic_cast<juce::AudioParameterBool*> (prm.p) != nullptr)
                prm.set (steps > 0 ? 1.0f : 0.0f);
            else
                prm.setNorm (prm.norm() + steps * (m.isShiftDown() ? fineStep * 0.2f : fineStep));
        };
        c->onClick = [prm] (const juce::MouseEvent& e)
        {
            if (e.mods.isAltDown() || e.getNumberOfClicks() > 1)   // back to default
                prm.setNorm (prm.p->getDefaultValue());
        };
        c->setTooltip ("Drag up / down or scroll. Shift = fine. Double-click = default.");
        addAndMakeVisible (*c);
        rows.back().items.push_back ({ std::move (c), width });
    }

    void ParamPage::addChoice (const char* id, juce::StringArray labels)
    {
        const Param prm = param (id);
        auto* ch = dynamic_cast<juce::AudioParameterChoice*> (prm.p);
        if (ch == nullptr) return;
        if (labels.isEmpty()) labels = ch->choices;
        for (int i = 0; i < labels.size(); ++i)
            addAction (labels[i], [prm, i] { prm.set ((float) i); }, [prm, i] { return prm.choice() == i; });
    }

    void ParamPage::addBool (const char* id, const juce::String& on, const juce::String& off)
    {
        const Param prm = param (id);
        addAction (on,  [prm] { prm.set (1.0f); }, [prm] { return prm.get() > 0.5f; });
        addAction (off, [prm] { prm.set (0.0f); }, [prm] { return prm.get() <= 0.5f; });
    }

    void ParamPage::addAction (const juce::String& t, std::function<void()> fn, std::function<bool()> isOn, int width)
    {
        auto c = std::make_unique<LcdCell>();
        c->text = [t] { return t; };
        c->isOn = isOn;
        c->boxed = true;
        c->fontSize = kFont;
        c->just = juce::Justification::centred;
        c->onClick = [fn] (const juce::MouseEvent&) { if (fn) fn(); };
        addAndMakeVisible (*c);
        rows.back().items.push_back ({ std::move (c), width > 0 ? width : textWidth (t) });
    }

    void ParamPage::addCustom (std::function<juce::String()> text, std::function<void (float, bool)> drag, std::function<void()> reset, int width)
    {
        auto c = std::make_unique<LcdCell>();
        c->text = std::move (text);
        c->underline = true;
        c->fontSize = kFont;
        c->just = juce::Justification::centred;
        c->onDrag = [drag] (float steps, const juce::ModifierKeys& m) { if (drag) drag (steps, m.isShiftDown()); };
        c->onClick = [reset] (const juce::MouseEvent& e) { if ((e.mods.isAltDown() || e.getNumberOfClicks() > 1) && reset) reset(); };
        c->setTooltip ("Drag up / down or scroll. Shift = fine. Double-click = automatic.");
        addAndMakeVisible (*c);
        rows.back().items.push_back ({ std::move (c), width });
    }

    void ParamPage::resized()
    {
        int y = 0;
        for (auto& r : rows)
        {
            if (r.ruleAbove) y += 8;
            int x = 150 + titleW;
            for (auto& it : r.items)
            {
                it.cell->setBounds (x, y + 3, it.width, rowHeight - 6);
                x += it.width + 10;
            }
            y += rowHeight;
        }
    }

    void ParamPage::paint (juce::Graphics& g)
    {
        int y = 0;
        g.setFont (lcd (kFont));
        for (auto& r : rows)
        {
            if (r.ruleAbove)
            {
                g.setColour (lcdc().dim);
                g.fillRect (0, y + 2, getWidth(), 2);
                y += 8;
            }
            g.setColour (lcdc().ink);
            g.drawText (r.group, 0, y, 150, rowHeight, juce::Justification::centredLeft);
            if (titleW > 0) g.drawText (r.title, 150, y, titleW, rowHeight, juce::Justification::centredLeft);
            y += rowHeight;
        }
    }

    void ParamPage::refresh() { for (auto& r : rows) for (auto& it : r.items) it.cell->repaint(); }

    // =====================================================================================
    //  SAMPLE — waveform
    // =====================================================================================
    int WaveView::numSamples() const
    {
        const auto& slot = proc.getSlot (proc.getSelectedSlot());
        return slot.content != nullptr ? juce::jmax (1, slot.content->buffer->numSamples) : 1;
    }

    // the view in samples; a different break (or file) starts from the whole break again
    void WaveView::setView (double start, double length)
    {
        followBreak();
        const double n = (double) numSamples();
        const double minLen = juce::jmin (n, juce::jmax (64.0, (double) getWidth() / 4.0));
        length = juce::jlimit (minLen, n, length);
        start = juce::jlimit (0.0, n - length, start);
        viewStart = start;
        viewLen = length >= n ? 0.0 : length;
        if (viewLen == 0.0) viewStart = 0.0;
        repaint();
        if (onViewChanged) onViewChanged();
    }

    bool WaveView::zoomed() const { return viewLen > 0.0; }

    bool WaveView::followBreak()
    {
        const int slot = proc.getSelectedSlot();
        const int n = proc.getSlot (slot).content != nullptr ? numSamples() : -1;
        if (slot == viewSlot && n == viewN) return false;
        viewSlot = slot; viewN = n;
        const bool was = viewLen > 0.0;
        viewStart = 0.0; viewLen = 0.0;
        return was;
    }

    void WaveView::zoomBy (double factor, int anchorX)
    {
        followBreak();
        const double n = (double) numSamples();
        const double len = viewLen > 0.0 ? viewLen : n;
        const double frac = anchorX >= 0 ? juce::jlimit (0.0, 1.0, (double) anchorX / juce::jmax (1, getWidth())) : 0.5;
        const double at = viewStart + frac * len;                    // this sample stays under the mouse
        const double newLen = len * factor;
        setView (at - frac * newLen, newLen);
    }

    void WaveView::scrollBy (double fractionOfView)
    {
        followBreak();
        if (viewLen <= 0.0) return;
        setView (viewStart + fractionOfView * viewLen, viewLen);
    }

    void WaveView::zoomSlice()
    {
        const auto& slot = proc.getSlot (proc.getSelectedSlot());
        if (slot.content == nullptr) return;
        const auto reg = slot.content->region (proc.getSelectedSlice());
        const double margin = juce::jmax (256.0, reg.length() * 0.15);
        setView (reg.start - margin, reg.length() + 2.0 * margin);
    }

    void WaveView::zoomAll() { setView (0.0, (double) numSamples()); }

    juce::String WaveView::viewText() const
    {
        const auto& slot = proc.getSlot (proc.getSelectedSlot());
        if (slot.content == nullptr) return "-";
        const double sr = juce::jmax (1.0, slot.content->buffer->sampleRate);
        const double n = (double) numSamples(), len = viewLen > 0.0 ? viewLen : n;
        const double x = n / len;
        return juce::String (viewStart / sr, 2) + " - " + juce::String ((viewStart + len) / sr, 2) + " S  X"
             + (x < 10.0 ? juce::String (x, 1) : juce::String (juce::roundToInt (x)));
    }

    int WaveView::xForSample (int s) const
    {
        const double len = viewLen > 0.0 ? viewLen : (double) numSamples();
        return (int) std::floor ((s - viewStart) * getWidth() / len);
    }

    int WaveView::sampleForX (int x) const
    {
        const double len = viewLen > 0.0 ? viewLen : (double) numSamples();
        const int n = proc.getSlot (proc.getSelectedSlot()).content != nullptr ? numSamples() : 0;
        return juce::jlimit (0, n, (int) std::lround (viewStart + (double) juce::jlimit (0, getWidth(), x) * len / juce::jmax (1, getWidth())));
    }

    void WaveView::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
    {
        // sideways or shift: scroll; up / down: zoom in / out round the mouse
        const float side = w.deltaX != 0.0f ? w.deltaX : (e.mods.isShiftDown() ? w.deltaY : 0.0f);
        if (side != 0.0f) { scrollBy (-side * 2.0); return; }
        if (w.deltaY != 0.0f) zoomBy (w.deltaY > 0 ? 0.8 : 1.25, e.x);
    }

    int WaveView::sliceAtX (int x) const
    {
        const auto& slot = proc.getSlot (proc.getSelectedSlot());
        if (slot.content == nullptr) return -1;
        return slot.content->slices.sliceAt (sampleForX (x));
    }

    int WaveView::markerNear (int x) const
    {
        const auto& slot = proc.getSlot (proc.getSelectedSlot());
        if (slot.content == nullptr) return -1;
        for (int k = 1; k < slot.content->slices.count; ++k)          // marker 0 (the start) stays put
            if (std::abs (xForSample (slot.content->slices.starts[(size_t) k]) - x) <= 6) return k;
        return -1;
    }

    void WaveView::paint (juce::Graphics& g)
    {
        const auto& slot = proc.getSlot (proc.getSelectedSlot());
        auto r = getLocalBounds();
        auto badgeRow = r.removeFromBottom (badgeRowH);
        r.removeFromBottom (6);

        if (slot.content == nullptr)
        {
            g.setColour (lcdc().ink);
            g.setFont (lcd (34.0f));
            g.drawText (slot.status == ChoppageProcessor::Status::loading ? "LOADING..." : "DROP A BREAK OR PRESS LOAD",
                        r, juce::Justification::centred);
            return;
        }
        const auto& c = *slot.content;
        const auto& buf = *c.buffer;
        const int n = buf.numSamples;
        if (followBreak() && onViewChanged) juce::MessageManager::callAsync ([f = onViewChanged] { f(); });
        const float mid = (float) r.getCentreY(), half = (float) r.getHeight() * 0.48f;

        // muted slices are shaded, the selected one is lit
        for (int k = 0; k < c.numSlices(); ++k)
        {
            const auto reg = c.region (k);
            const int x0 = xForSample (reg.start), x1 = xForSample (reg.end);
            if (slot.program.slice[(size_t) k].mute)
            {
                g.setColour (lcdc().shade.withAlpha (0.55f));
                g.fillRect (x0, r.getY(), x1 - x0, r.getHeight());
            }
            if (k == proc.getSelectedSlice())
            {
                g.setColour (juce::Colours::white.withAlpha (0.2f));
                g.fillRect (x0, r.getY(), x1 - x0, r.getHeight());
            }
        }

        g.setColour (lcdc().faint);
        g.fillRect ((float) r.getX(), mid, (float) r.getWidth(), 1.0f);
        g.setColour (lcdc().ink);
        for (int x = 0; x < r.getWidth(); ++x)
        {
            const int s0 = juce::jlimit (0, n - 1, sampleForX (x));
            const int s1 = juce::jlimit (s0 + 1, n, sampleForX (x + 1));
            const auto pk = buf.peakRange (s0, s1);
            const float top = mid - juce::jlimit (0.0f, 1.0f, pk.second) * half;
            const float bot = mid - juce::jlimit (-1.0f, 0.0f, pk.first) * half;
            g.fillRect ((float) x, top, 1.0f, juce::jmax (1.0f, bot - top));
        }

        // markers (the one being dragged follows the mouse)
        for (int k = 0; k < c.slices.count; ++k)
        {
            const int x = k == dragMarker && dragX >= 0 ? dragX : xForSample (c.slices.starts[(size_t) k]);
            g.setColour (juce::Colours::white);
            g.fillRect (x, r.getY(), 2, r.getHeight());
        }

        // envelope handle of the selected slice: its gate (yellow)
        {
            const int k = proc.getSelectedSlice();
            const auto reg = c.region (k);
            const float gate = slot.program.slice[(size_t) k].gate * slot.program.all.gate;
            const float x0 = (float) xForSample (reg.start) + 2.0f;
            const float x1 = x0 + ((float) xForSample (reg.end) - x0) * gate;
            juce::Path env;
            env.startNewSubPath (x0, (float) r.getBottom() - 4.0f);
            env.lineTo (x0 + 6.0f, (float) r.getY() + 8.0f);
            env.lineTo (juce::jmax (x0 + 8.0f, x1 - 8.0f), (float) r.getY() + 8.0f);
            env.lineTo (x1, (float) r.getBottom() - 4.0f);
            g.setColour (lcdc().mark);
            g.strokePath (env, juce::PathStrokeType (3.0f));
        }

        // play head: the slice playing now
        if (proc.getSliceOf (proc.getSelectedSlot()) >= 0)
        {
            const auto reg = c.region (proc.getSliceOf (proc.getSelectedSlot()));
            g.setColour (lcdc().mark);
            g.fillRect (xForSample (reg.start), r.getY(), juce::jmax (4, (xForSample (reg.end) - xForSample (reg.start)) / 6), 6);
        }

        // badges: K1, H2, S3 ... (letter only when slices get narrow)
        std::array<int, bc::kNumBadges> counts {};
        g.setFont (lcd (23.0f));
        for (int k = 0; k < c.numSlices(); ++k)
        {
            const auto reg = c.region (k);
            const int x0 = xForSample (reg.start), x1 = xForSample (reg.end);
            auto cell = juce::Rectangle<int> (x0 + 1, badgeRow.getY(), juce::jmax (4, x1 - x0 - 2), badgeRow.getHeight());
            const auto b = c.badges[(size_t) k];
            const int num = ++counts[(size_t) b];
            const bool sel = k == proc.getSelectedSlice();
            const bool muted = slot.program.slice[(size_t) k].mute;
            if (sel) { g.setColour (lcdc().ink); g.fillRect (cell); }
            else     { g.setColour (lcdc().dim); g.drawRect (cell, 2); }
            g.setColour (sel ? lcdc().inv : lcdc().ink);
            juce::String t = muted ? "M" : juce::String (bc::badgeLabel (b));
            if (! muted && cell.getWidth() > 38) t << num;
            g.drawFittedText (t, cell, juce::Justification::centred, 1, 0.6f);
        }
    }

    void WaveView::mouseDown (const juce::MouseEvent& e)
    {
        const auto& slot = proc.getSlot (proc.getSelectedSlot());
        if (slot.content == nullptr) return;
        const int slice = sliceAtX (e.x);
        if (e.mods.isPopupMenu()) { popup (slice, markerNear (e.x)); return; }
        dragMarker = markerNear (e.x);
        dragX = -1;
        if (dragMarker < 0) proc.selectSlice (slice, true);
        repaint();
    }

    void WaveView::mouseDrag (const juce::MouseEvent& e)
    {
        if (dragMarker < 0) return;
        dragX = juce::jlimit (0, getWidth() - 1, e.x);
        repaint();
    }

    void WaveView::mouseUp (const juce::MouseEvent&)
    {
        if (dragMarker >= 0 && dragX >= 0) proc.moveMarker (dragMarker, sampleForX (dragX));
        dragMarker = dragX = -1;
        repaint();
    }

    void WaveView::mouseDoubleClick (const juce::MouseEvent& e)
    {
        if (markerNear (e.x) < 0) proc.addMarker (sampleForX (e.x));
    }

    void WaveView::popup (int slice, int marker)
    {
        const auto& slot = proc.getSlot (proc.getSelectedSlot());
        if (slot.content == nullptr || slice < 0) return;
        proc.selectSlice (slice, false);
        juce::PopupMenu m, badges;
        m.addSectionHeader ("SLICE " + juce::String (slice + 1).paddedLeft ('0', 2));
        m.addItem (slot.program.slice[(size_t) slice].mute ? "UNMUTE" : "MUTE", [this, slice] { proc.toggleMute (slice); });
        for (int b = 0; b < bc::kNumBadges; ++b)
            badges.addItem (bc::badgeName ((Badge) b), true, slot.content->badges[(size_t) slice] == (Badge) b,
                            [this, slice, b] { proc.setBadge (slice, (Badge) b); });
        m.addSubMenu ("BADGE", badges);
        if (marker > 0) m.addItem ("DELETE MARKER", [this, marker] { proc.deleteMarker (marker); });
        m.addItem ("ADD MARKER HERE (or double-click)", [this, slice]
        {
            const auto reg = proc.getSlot (proc.getSelectedSlot()).content->region (slice);
            proc.addMarker (reg.start + reg.length() / 2);
        });
        m.showMenuAsync (juce::PopupMenu::Options().withMousePosition());
    }

    // =====================================================================================
    //  SAMPLE — tab
    // =====================================================================================
    namespace
    {
        using F = ChoppageProcessor::SliceField;
        const F kFields[] = { F::pitch, F::gate, F::stretch, F::reverse, F::stutter, F::trans, F::xfade };
        const char* kFieldNames[] = { "PITCH", "GATE", "STRETCH", "REVERSE", "STUTTER", "TRANS", "XFADE" };

        juce::String fieldText (F f, float v)
        {
            switch (f)
            {
                case F::pitch:   return (v > 0.0f ? "+" : "") + juce::String (v, std::abs (v - std::round (v)) > 0.01f ? 1 : 0);
                case F::gate:    return juce::String (juce::roundToInt (v * 100.0f));
                case F::stretch: return juce::String (v, 2);
                case F::reverse: return v > 0.5f ? "ON" : "OFF";
                case F::stutter: return v < 1.5f ? "OFF" : "x" + juce::String ((int) v);
                case F::trans:   return juce::String (juce::roundToInt (v * 100.0f)) + "%";
                case F::xfade:   return juce::String (v, 1) + "MS";
                case F::level:   return juce::String (v, 2);
            }
            return {};
        }

        float fieldStep (F f, float v, float steps, bool fine)
        {
            switch (f)
            {
                case F::pitch:   return v + steps * (fine ? 0.1f : 1.0f);
                case F::gate:    return v + steps * (fine ? 0.01f : 0.05f);
                case F::stretch: return v + steps * (fine ? 0.01f : 0.05f);
                case F::reverse: return steps > 0 ? 1.0f : 0.0f;
                case F::stutter:
                {
                    static const int seq[] = { 0, 2, 3, 4, 6, 8 };
                    int i = 0; while (i < 5 && seq[i] < (int) v) ++i;
                    return (float) seq[juce::jlimit (0, 5, i + (steps > 0 ? 1 : -1))];
                }
                case F::trans:   return v + steps * (fine ? 0.01f : 0.05f);
                case F::xfade:   return v + steps * (fine ? 0.1f : 0.5f);
                case F::level:   return v + steps * 0.05f;
            }
            return v;
        }
    }

    SampleTab::SampleTab (ChoppageProcessor& p) : proc (p), wave (p)
    {
        addAndMakeVisible (wave);
        // zoom keys above the waveform (the mouse wheel zooms too, round the pointer; shift + wheel scrolls)
        auto key = [this] (juce::String t, std::function<void()> fn, std::function<bool()> on, juce::String tip)
        {
            auto c = std::make_unique<LcdCell>();
            c->text = [t] { return t; };
            c->boxed = true;
            c->fontSize = 24.0f;
            c->just = juce::Justification::centred;
            c->isOn = std::move (on);
            c->onClick = [fn] (const juce::MouseEvent&) { fn(); };
            c->setTooltip (tip);
            addAndMakeVisible (*c);
            zoomKeys.push_back (std::move (c));
        };
        key ("ZOOM SLICE", [this] { wave.zoomSlice(); }, nullptr, "Fill the waveform with the selected slice");
        key ("ZOOM +", [this] { wave.zoomBy (0.5); }, nullptr, "Zoom in (or scroll the mouse wheel up over the waveform)");
        key ("ZOOM -", [this] { wave.zoomBy (2.0); }, nullptr, "Zoom out (or scroll the mouse wheel down)");
        key (juce::String::fromUTF8 ("\xe2\x97\x80"), [this] { wave.scrollBy (-0.5); }, nullptr, "Scroll left (or shift + wheel)");
        key (juce::String::fromUTF8 ("\xe2\x96\xb6"), [this] { wave.scrollBy (0.5); }, nullptr, "Scroll right (or shift + wheel)");
        key ("ZOOM OUT ALL", [this] { wave.zoomAll(); }, [this] { return ! wave.zoomed(); }, "Show the whole break");
        viewInfo.text = [this] { return wave.viewText(); };
        viewInfo.fontSize = 22.0f;
        viewInfo.just = juce::Justification::centredRight;
        viewInfo.setInterceptsMouseClicks (false, false);
        addAndMakeVisible (viewInfo);
        wave.onViewChanged = [this] { for (auto& k : zoomKeys) k->repaint(); viewInfo.repaint(); };
        for (auto* row : { &sliceRow, &allRow })
        {
            const bool isAll = row == &allRow;
            row->fontSize = 26.0f;
            row->text = [this, isAll]
            {
                return isAll ? juce::String ("ALL") : "SLICE " + juce::String (proc.getSelectedSlice() + 1).paddedLeft ('0', 2);
            };
            row->isOn = [this, isAll] { return proc.isEditingAll() == isAll; };
            row->onClick = [this, isAll] (const juce::MouseEvent&) { proc.setEditingAll (isAll); };
            addAndMakeVisible (*row);
        }
        for (int i = 0; i < 7; ++i)
        {
            auto h = std::make_unique<LcdCell>();
            const juce::String name = kFieldNames[i];
            h->text = [name] { return name; };
            h->fontSize = 22.0f;
            h->just = juce::Justification::centred;
            h->setInterceptsMouseClicks (false, false);
            addAndMakeVisible (*h);
            headers.push_back (std::move (h));
        }
        for (int row = 0; row < 2; ++row)
            for (int i = 0; i < 7; ++i)
            {
                const bool all = row == 1;
                const F f = kFields[i];
                auto c = std::make_unique<LcdCell>();
                c->fontSize = 28.0f;
                c->just = juce::Justification::centred;
                c->underline = true;
                c->text = [this, f, all] { return fieldText (f, proc.getSliceValue (f, all)); };
                c->onDrag = [this, f, all] (float steps, const juce::ModifierKeys& m)
                {
                    proc.setEditingAll (all);
                    proc.setSliceValue (f, fieldStep (f, proc.getSliceValue (f, all), steps, m.isShiftDown()), all);
                };
                c->onClick = [this, f, all] (const juce::MouseEvent& e)
                {
                    proc.setEditingAll (all);
                    if (f == F::reverse) proc.setSliceValue (f, proc.getSliceValue (f, all) > 0.5f ? 0.0f : 1.0f, all);
                    else if (e.getNumberOfClicks() > 1)
                    {
                        const bc::SliceParams d;
                        const float def = f == F::pitch ? d.pitch : f == F::gate ? d.gate : f == F::stretch ? d.stretch
                                        : f == F::stutter ? 0.0f : f == F::trans ? d.trans : d.xfadeMs;
                        proc.setSliceValue (f, def, all);
                    }
                };
                c->setTooltip ("Drag up / down or scroll (shift = fine). Double-click = default. REVERSE: click.");
                addAndMakeVisible (*c);
                cells.push_back (std::move (c));
            }
    }

    void SampleTab::resized()
    {
        auto r = getLocalBounds();
        const int rowH = 40;
        auto table = r.removeFromBottom (rowH * 3);
        r.removeFromBottom (8);
        auto keys = r.removeFromTop (38);
        r.removeFromTop (6);
        wave.setBounds (r);
        const int kw[] = { 170, 110, 110, 60, 60, 190 };
        for (size_t i = 0; i < zoomKeys.size(); ++i) { zoomKeys[i]->setBounds (keys.removeFromLeft (kw[i]).withTrimmedRight (8)); }
        viewInfo.setBounds (keys);
        const int labelW = 170;
        const int colW = (table.getWidth() - labelW) / 7;
        auto head = table.removeFromTop (rowH);
        head.removeFromLeft (labelW);
        for (int i = 0; i < 7; ++i) headers[(size_t) i]->setBounds (head.removeFromLeft (colW));
        for (int row = 0; row < 2; ++row)
        {
            auto line = table.removeFromTop (rowH);
            (row == 0 ? sliceRow : allRow).setBounds (line.removeFromLeft (labelW).reduced (0, 3));
            for (int i = 0; i < 7; ++i) cells[(size_t) (row * 7 + i)]->setBounds (line.removeFromLeft (colW).reduced (4, 3));
        }
    }

    void SampleTab::paint (juce::Graphics& g)
    {
        g.setColour (lcdc().dim);
        g.fillRect (0, getHeight() - 40 * 3 - 4, getWidth(), 2);
    }

    void SampleTab::refresh()
    {
        wave.repaint();
        sliceRow.repaint(); allRow.repaint(); viewInfo.repaint();
        for (auto& k : zoomKeys) k->repaint();
        for (auto& c : cells) c->repaint();
    }

    // =====================================================================================
    //  SEQ
    // =====================================================================================
    namespace
    {
        const char* kLaneNames[] = { "KICK", "SNARE", "GHOST", "HAT", "OPEN", "PERC", "FX" };
        const Badge kLaneBadge[] = { Badge::kick, Badge::snare, Badge::ghost, Badge::hat, Badge::open, Badge::other };
        constexpr int kHitLanes = 6;    // lane 6 is the FX row
        const char* fxLetter (bc::FxType t)
        {
            switch (t) { case bc::FxType::stutter: return "S"; case bc::FxType::reverse: return "R";
                         case bc::FxType::stretch: return "A"; case bc::FxType::tapeStop: return "T"; case bc::FxType::none: return ""; }
            return "";
        }
        int laneOf (Badge b) { for (int i = 0; i < kHitLanes; ++i) if (kLaneBadge[i] == b) return i; return kHitLanes - 1; }
    }

    int SeqGrid::visibleSteps() const
    {
        const auto& s = proc.getSlot (proc.getSelectedSlot());
        return juce::jmin (32, s.program.pattern.numSteps);
    }

    juce::Rectangle<int> SeqGrid::cell (int lane, int column) const
    {
        const int head = 26;
        const int rowH = (getHeight() - head) / kLanes;
        const int cols = visibleSteps();
        const float colW = (float) (getWidth() - laneW) / (float) cols;
        const int x0 = laneW + (int) (column * colW), x1 = laneW + (int) ((column + 1) * colW);
        return { x0 + 2, head + lane * rowH + 3, x1 - x0 - 4, rowH - 6 };
    }

    void SeqGrid::paint (juce::Graphics& g)
    {
        const auto& s = proc.getSlot (proc.getSelectedSlot());
        const auto& pat = s.program.pattern;
        const int cols = visibleSteps();
        const int first = page * 32;
        const int playing = proc.getStepOf (proc.getSelectedSlot());

        g.setFont (lcd (20.0f));
        g.setColour (lcdc().ink);
        for (int c = 0; c < cols; c += 4)
            g.drawText (juce::String (first + c + 1), cell (0, c).withY (0).withHeight (24).withWidth (60), juce::Justification::centredLeft);

        for (int lane = 0; lane < kLanes; ++lane)
        {
            const auto row = cell (lane, 0);
            g.setColour (lcdc().ink);
            g.setFont (lcd (23.0f));
            g.drawText (kLaneNames[lane], 0, row.getY(), laneW, row.getHeight(), juce::Justification::centredLeft);
            for (int c = 0; c < cols; ++c)
            {
                const int step = first + c;
                if (step >= pat.numSteps) break;
                auto r = cell (lane, c);
                juce::String t;
                bool hit = false;
                if (lane < kHitLanes)
                {
                    const int sl = pat.slice[(size_t) step];
                    if (sl >= 0 && s.content != nullptr && laneOf (s.content->badges[(size_t) sl]) == lane)
                    {
                        hit = true;
                        t = juce::String (sl + 1);
                    }
                }
                else if (pat.fx[(size_t) step].type != bc::FxType::none)
                {
                    hit = true;
                    t = juce::String (fxLetter (pat.fx[(size_t) step].type)) + juce::String (pat.fx[(size_t) step].rate);
                }
                if (step == playing)
                {
                    g.setColour (lcdc().mark.withAlpha (0.35f));
                    g.fillRect (r.expanded (2, 3));
                }
                if (hit)
                {
                    g.setColour (lcdc().ink);
                    g.fillRect (r);
                    g.setColour (lcdc().inv);
                    g.setFont (lcd (lane < kHitLanes ? 21.0f : 16.0f));
                    g.drawFittedText (t, r, juce::Justification::centred, 1, 0.5f);
                }
                if (lane == kHitLanes && step == selStep)
                {
                    g.setColour (lcdc().mark);
                    g.drawRect (r.expanded (2, 2), 3);
                }
                else
                {
                    g.setColour (c % 4 == 0 ? lcdc().dim : lcdc().faint);
                    g.drawRect (r, 2);
                }
                if (pat.locked[(size_t) step] && lane == 0)
                {
                    g.setColour (lcdc().mark);
                    g.fillRect (r.getX(), r.getY() - 3, r.getWidth(), 3);
                }
            }
        }
    }

    void SeqGrid::mouseDown (const juce::MouseEvent& e)
    {
        const auto& s = proc.getSlot (proc.getSelectedSlot());
        if (s.content == nullptr) return;
        const int cols = visibleSteps();
        for (int lane = 0; lane < kLanes; ++lane)
            for (int c = 0; c < cols; ++c)
                if (cell (lane, c).expanded (2, 3).contains (e.getPosition()))
                {
                    const int step = page * 32 + c;
                    selStep = step;                     // STEP FX / RATE now edit this step
                    if (e.mods.isShiftDown()) { proc.toggleLock (step); repaint(); return; }
                    if (lane < kHitLanes)
                    {
                        if (e.mods.isPopupMenu()) proc.swapStep (step);
                        else proc.toggleStep (step, kLaneBadge[lane]);
                    }
                    else if (e.mods.isPopupMenu()) proc.cycleFxRate (step);   // the FX row: a click selects the step
                    if (onSelect) onSelect();
                    repaint();
                    return;
                }
    }

    SeqTab::SeqTab (ChoppageProcessor& p) : proc (p), grid (p)
    {
        addAndMakeVisible (grid);
        auto tool = [this] (juce::String t, std::function<void()> fn, std::function<bool()> on)
        {
            auto c = std::make_unique<LcdCell>();
            c->text = [t] { return t; };
            c->isOn = on;
            c->boxed = true;
            c->fontSize = 24.0f;
            c->just = juce::Justification::centred;
            c->onClick = [fn] (const juce::MouseEvent&) { fn(); };
            addAndMakeVisible (*c);
            tools.push_back (std::move (c));
        };
        auto slot = [this]() -> const ChoppageProcessor::Slot& { return proc.getSlot (proc.getSelectedSlot()); };
        tool ("PATTERN",  [this] { proc.setShowOriginal (false); }, [slot] { return ! slot().showOriginal; });
        tool ("ORIGINAL", [this] { proc.setShowOriginal (true); },  [slot] { return slot().showOriginal; });
        tool ("1 BAR",  [this] { proc.setPatternLength (1); }, [slot] { return slot().program.pattern.numSteps == 16; });
        tool ("2 BARS", [this] { proc.setPatternLength (2); }, [slot] { return slot().program.pattern.numSteps == 32; });
        tool ("4 BARS", [this] { proc.setPatternLength (4); }, [slot] { return slot().program.pattern.numSteps == 64; });
        tool ("1", [this] { grid.page = 0; grid.repaint(); }, [this] { return grid.page == 0; });
        tool ("2", [this, slot] { if (slot().program.pattern.numSteps > 32) { grid.page = 1; grid.repaint(); } }, [this] { return grid.page == 1; });
        tool (juce::String::fromUTF8 ("\xe2\x97\x80"), [this] { proc.historyBack(); }, nullptr);
        tool (juce::String::fromUTF8 ("\xe2\x96\xb6"), [this] { proc.historyForward(); }, nullptr);
        tool ("CLEAR", [this] { proc.clearPattern(); }, nullptr);
        tool ("RESET", [this] { proc.session().resetToOriginal(); }, nullptr);

        memLabel.text = [] { return juce::String ("MEMORY"); };
        memLabel.fontSize = 23.0f;
        memLabel.setInterceptsMouseClicks (false, false);
        addAndMakeVisible (memLabel);
        for (int m = 0; m < bc::kMemories; ++m)
        {
            auto c = std::make_unique<LcdCell>();
            c->text = [m] { return juce::String (m + 1); };
            c->isOn = [slot, m] { return slot().memories[(size_t) m].has_value(); };
            c->boxed = true;
            c->fontSize = 22.0f;
            c->just = juce::Justification::centred;
            c->onClick = [this, m] (const juce::MouseEvent& e)
            {
                if (e.mods.isPopupMenu())
                {
                    // right-click: what goes in this memory
                    juce::PopupMenu menu;
                    menu.addSectionHeader ("MEMORY " + juce::String (m + 1));
                    menu.addItem ("Save the pattern here", [this, m] { proc.saveMemory (m); refresh(); });
                    menu.addItem ("Save the ORIGINAL break here", [this, m] { proc.session().saveOriginalToMemory (m); refresh(); });
                    menu.addItem ("Recall", [this, m] { proc.recallMemory (m); refresh(); });
                    menu.addItem ("Clear", [this, m] { proc.session().clearMemory (m); refresh(); });
                    menu.showMenuAsync (juce::PopupMenu::Options().withMousePosition());
                }
                else if (e.mods.isShiftDown()) proc.saveMemory (m);
                else if (e.mods.isAltDown()) proc.session().saveOriginalToMemory (m);
                else proc.recallMemory (m);
            };
            c->setTooltip ("Click: recall (or play G#3-B4).  Shift-click: save the pattern here.  Alt-click: save the ORIGINAL break here.  Right-click: menu.");
            addAndMakeVisible (*c);
            memories.push_back (std::move (c));
        }
        // the selected step's FX (click a cell or the FX row to pick the step)
        auto stepCell = [this] (std::function<juce::String()> text, std::function<void()> fn, std::function<bool()> on, bool boxed)
        {
            auto c = std::make_unique<LcdCell>();
            c->text = std::move (text);
            c->isOn = std::move (on);
            c->boxed = boxed;
            c->fontSize = 22.0f;
            c->just = juce::Justification::centred;
            if (fn) c->onClick = [fn] (const juce::MouseEvent&) { fn(); };
            else c->setInterceptsMouseClicks (false, false);
            addAndMakeVisible (*c);
            stepFx.push_back (std::move (c));
        };
        auto fxOf = [this, slot]() -> bc::FxEvent { const auto& p = slot().program.pattern;
                                                    return grid.selStep < p.numSteps ? p.fx[(size_t) grid.selStep] : bc::FxEvent{}; };
        stepCell ([this] { return "STEP " + juce::String (grid.selStep + 1) + " FX"; }, nullptr, nullptr, false);
        const bc::FxType types[] = { bc::FxType::none, bc::FxType::stutter, bc::FxType::reverse, bc::FxType::stretch, bc::FxType::tapeStop };
        const char* typeNames[] = { "OFF", "STUTTER", "REVERSE", "STRETCH", "TAPE STOP" };
        for (int i = 0; i < 5; ++i)
        {
            const auto t = types[i];
            stepCell ([n = juce::String (typeNames[i])] { return n; },
                      [this, t, fxOf] { const int rate = fxOf().rate > 0 ? fxOf().rate : 16; proc.setFxStep (grid.selStep, t, rate); grid.repaint(); },
                      [t, fxOf] { return fxOf().type == t; }, true);
        }
        stepCell ([] { return juce::String ("RATE"); }, nullptr, nullptr, false);
        for (int rate : { 8, 16, 32, 64 })
            stepCell ([rate] { return "1/" + juce::String (rate); },
                      [this, rate, fxOf] { const auto f = fxOf(); if (f.type != bc::FxType::none) { proc.setFxStep (grid.selStep, f.type, rate); grid.repaint(); } },
                      [rate, fxOf] { const auto f = fxOf(); return f.type != bc::FxType::none && f.rate == rate; }, true);
        grid.onSelect = [this] { for (auto& c : stepFx) c->repaint(); };

        help.text = [] { return juce::String::fromUTF8 ("CLICK ADD/REMOVE \xc2\xb7 RIGHT-CLICK SWAP SLICE / FX RATE \xc2\xb7 SHIFT-CLICK LOCK \xc2\xb7 FX ROW: PICK THE STEP"); };
        help.fontSize = 20.0f;
        help.setInterceptsMouseClicks (false, false);
        addAndMakeVisible (help);
    }

    void SeqTab::resized()
    {
        auto r = getLocalBounds();
        auto top = r.removeFromTop (38);
        const int widths[] = { 130, 140, 100, 110, 110, 44, 44, 50, 50, 100, 104 };
        int x = 0;
        for (size_t i = 0; i < tools.size() && i < 11; ++i)
        {
            if (i == 2 || i == 5 || i == 7 || i == 9) x += 12;
            tools[i]->setBounds (x, top.getY() + 2, widths[i], 34);
            x += widths[i] + 6;
        }
        auto bottom = r.removeFromBottom (36);
        auto helpRow = r.removeFromBottom (28);
        help.setBounds (helpRow);
        auto fxRow = r.removeFromBottom (38);
        {
            const int w[] = { 132, 64, 124, 124, 118, 132, 64, 74, 74, 74, 74 };
            int x = fxRow.getX();
            for (size_t i = 0; i < stepFx.size() && i < 11; ++i)
            {
                stepFx[i]->setBounds (x, fxRow.getY() + 3, w[i], 32);
                x += w[i] + (i == 0 || i == 5 ? 10 : 5);
            }
        }
        memLabel.setBounds (bottom.removeFromLeft (SeqGrid::laneW));
        const int w = bottom.getWidth() / bc::kMemories;
        for (auto& m : memories) m->setBounds (bottom.removeFromLeft (w).reduced (2, 2));
        r.removeFromTop (4);
        grid.setBounds (r);
    }

    void SeqTab::refresh()
    {
        const auto& s = proc.getSlot (proc.getSelectedSlot());
        if (s.program.pattern.numSteps <= 32) grid.page = 0;
        // follow the play head onto page 2
        const int st = proc.getStepOf (proc.getSelectedSlot());
        if (proc.getStepOf (proc.getSelectedSlot()) >= 0 && st >= 0 && s.program.pattern.numSteps > 32) grid.page = st / 32;
        grid.repaint();
        for (auto& t : tools) t->repaint();
        for (auto& m : memories) m->repaint();
        for (auto& c : stepFx) c->repaint();
    }

    // =====================================================================================
    //  DISK
    // =====================================================================================
    DiskTab::DiskTab (ChoppageProcessor& p) : proc (p)
    {
        auto setup = [this] (LcdCell& c, juce::String t, std::function<void()> fn)
        {
            c.text = [t] { return t; };
            c.boxed = true;
            c.fontSize = 24.0f;
            c.just = juce::Justification::centred;
            c.onClick = [fn] (const juce::MouseEvent&) { fn(); };
            addAndMakeVisible (c);
        };
        setup (folderCell, "FOLDER...", [this] { if (onChooseFolder) onChooseFolder(); });
        setup (rescanCell, "RESCAN", [this] { proc.scanBrowser (proc.getBrowserFolder()); });
        setup (saveCell, "SAVE PRESET", [this] { if (onSavePreset) onSavePreset(); });
        setup (loadCell, "LOAD PRESET", [this] { if (onLoadPreset) onLoadPreset(); });
        setup (upCell, juce::String::fromUTF8 ("\xe2\x96\xb2"), [this] { scroll = std::max (0, scroll - visibleRows()); repaint(); });
        setup (downCell, juce::String::fromUTF8 ("\xe2\x96\xbc"), [this]
        { scroll = std::max (0, std::min ((int) entries.size() - visibleRows(), scroll + visibleRows())); repaint(); });
        pathCell.text = [this]
        {
            if (proc.session().browserScanning()) return juce::String ("SCANNING...");
            return proc.getBrowserFolder().getFullPathName().toUpperCase();
        };
        pathCell.fontSize = 20.0f;
        pathCell.setInterceptsMouseClicks (false, false);
        addAndMakeVisible (pathCell);
    }

    int DiskTab::visibleRows() const { return std::max (1, listArea().getHeight() / rowH); }

    juce::Rectangle<int> DiskTab::listArea() const
    {
        return getLocalBounds().withTrimmedTop (40 + 34).withTrimmedBottom (30);
    }

    void DiskTab::resized()
    {
        auto top = getLocalBounds().removeFromTop (38);
        folderCell.setBounds (top.removeFromLeft (150).reduced (0, 2));
        top.removeFromLeft (10);
        rescanCell.setBounds (top.removeFromLeft (130).reduced (0, 2));
        top.removeFromLeft (10);
        saveCell.setBounds (top.removeFromLeft (190).reduced (0, 2));
        top.removeFromLeft (10);
        loadCell.setBounds (top.removeFromLeft (190).reduced (0, 2));
        top.removeFromLeft (10);
        downCell.setBounds (top.removeFromRight (50).reduced (0, 2));
        top.removeFromRight (6);
        upCell.setBounds (top.removeFromRight (50).reduced (0, 2));
        top.removeFromRight (10);
        pathCell.setBounds (top);
    }

    void DiskTab::refresh()
    {
        const auto v = proc.session().version();
        if (v != seen) { seen = v; entries = proc.session().browserEntries(); scroll = std::min (scroll, std::max (0, (int) entries.size() - 1)); }
        repaint();
        pathCell.repaint();
    }

    void DiskTab::paint (juce::Graphics& g)
    {
        const int cols[] = { 120, 90, 120 };
        auto head = getLocalBounds().withTrimmedTop (40).withHeight (32);
        g.setColour (lcdc().ink);
        g.setFont (lcd (22.0f));
        int x = 8;
        for (auto [t, w] : { std::pair<const char*, int> { "BPM", cols[0] }, { "BARS", cols[1] }, { "LENGTH", cols[2] }, { "NAME", 400 } })
        {
            g.drawText (t, x, head.getY(), w, head.getHeight(), juce::Justification::centredLeft);
            x += w;
        }
        g.setColour (lcdc().dim);
        g.fillRect (0, head.getBottom(), getWidth(), 2);

        const auto list = listArea();
        const auto& sel = proc.getSlot (proc.getSelectedSlot());
        if (entries.empty())
        {
            g.setColour (lcdc().ink);
            g.setFont (lcd (26.0f));
            g.drawFittedText (proc.session().browserScanning() ? juce::String ("SCANNING...")
                                : juce::String ("NO BREAKS HERE. PICK A FOLDER WITH WAV / AIFF / FLAC FILES"), list, juce::Justification::centred, 2);
            return;
        }
        g.setFont (lcd (24.0f));
        for (int r = 0; r < visibleRows(); ++r)
        {
            const int i = scroll + r;
            if (i >= (int) entries.size()) break;
            const auto& e = entries[(size_t) i];
            auto row = juce::Rectangle<int> (list.getX(), list.getY() + r * rowH, list.getWidth(), rowH - 2);
            const bool loaded = e.path == sel.file;
            if (loaded) { g.setColour (lcdc().ink); g.fillRect (row); }
            g.setColour (loaded ? lcdc().inv : lcdc().ink);
            int cx = 8;
            g.drawText (e.bpm > 0 ? juce::String (e.bpm, 1) : juce::String ("--"), cx, row.getY(), cols[0], rowH, juce::Justification::centredLeft); cx += cols[0];
            g.drawText (e.bars > 0 ? juce::String (e.bars) : juce::String ("-"), cx, row.getY(), cols[1], rowH, juce::Justification::centredLeft); cx += cols[1];
            g.drawText (juce::String (e.seconds, 2) + "S", cx, row.getY(), cols[2], rowH, juce::Justification::centredLeft); cx += cols[2];
            g.drawFittedText (juce::String (e.name).toUpperCase(), cx, row.getY(), row.getRight() - cx, rowH, juce::Justification::centredLeft, 1, 0.7f);
        }
        g.setColour (lcdc().ink);
        g.setFont (lcd (20.0f));
        g.drawText (juce::String (entries.size()) + " BREAKS" + juce::String::fromUTF8 (" \xc2\xb7 CLICK: LOAD INTO BREAK ")
                    + juce::String (proc.getSelectedSlot() + 1) + juce::String::fromUTF8 (" \xc2\xb7 SHIFT-CLICK: NEXT EMPTY SLOT"),
                    getLocalBounds().removeFromBottom (28), juce::Justification::centredLeft);
    }

    void DiskTab::mouseDown (const juce::MouseEvent& e)
    {
        const auto list = listArea();
        if (! list.contains (e.getPosition())) return;
        const int i = scroll + (e.y - list.getY()) / rowH;
        if (i < 0 || i >= (int) entries.size()) return;
        int slot = proc.getSelectedSlot();
        if (e.mods.isShiftDown())
            for (int k = 0; k < bc::kSlots; ++k)
                if (proc.getSlot (k).status == ChoppageProcessor::Status::empty) { slot = k; break; }
        proc.loadFile (slot, juce::File (entries[(size_t) i].path));
        proc.selectSlot (slot);
    }

    void DiskTab::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w)
    {
        if (std::abs (w.deltaY) <= 0.0f) return;
        scroll = juce::jlimit (0, std::max (0, (int) entries.size() - visibleRows()), scroll + (w.deltaY > 0 ? -3 : 3));
        repaint();
    }

    // =====================================================================================
    //  FX
    // =====================================================================================
    FxTab::FxTab (ChoppageProcessor& p) : ParamPage (p)
    {
        namespace P = bc::params;
        rowHeight = 36;
        titleW = 0;
        addRow ("FILTER", "");
        addValue (P::filter, "CUTOFF", 110); addChoice (P::filterType); addValue (P::filterRes, "RES", 100);
        addRow ("DISTORT", "");
        addValue (P::dist, "DRIVE", 110); addValue (P::distTone, "TONE", 110, 0.02f);
        addRow ("CRUSH", "");
        addValue (P::crush, "AMOUNT", 110);
        addRow ("REVERB", "");
        addValue (P::reverb, "MIX", 90); addValue (P::revDecay, "DECAY", 90); addValue (P::revDamp, "DAMP", 90);
        addValue (P::revPre, "PRE", 110, 0.01f); addValue (P::revWidth, "WIDTH", 90);
        addRow ("ECHO +", "");
        addValue (P::echoTime, "TIME", 90); addValue (P::echoFb, "FDBK", 90); addValue (P::echoMix, "MIX", 90);
        addBool (P::echoMono, "MONO", "STEREO");
        addRule();
        addRow ("PADS", "");
        addValue (P::stutRate, "STUTTER", 90); addValue (P::rollRate, "ROLL", 90); addValue (P::tapeMs, "TAPE STOP", 130);
        addRow ("", "");
        addValue (P::akCycLen, "AKAIZE", 130, 0.005f); addValue (P::akRatio, "", 90); addValue (P::diveRes, "FILT DIVE RES", 100);
        addRow ("", "");
        addValue (P::dropRate, "S950 DROP", 140); addText ("QUANT"); addValue (P::padQuant, "", 110);
        addRow ("SLICES", "");
        addValue (P::slCycLen, "STRETCH CYCLE", 130, 0.005f); addValue (P::slCycFreq, "", 120, 0.005f);
        addText ("(MORE ON AKAI)");
    }

    // =====================================================================================
    //  AKAI: the Akaizer, set as in Akaizer v1.2 / Twisted-Timestretch
    // =====================================================================================
    AkaiTab::AkaiTab (ChoppageProcessor& p) : ParamPage (p)
    {
        namespace P = bc::params;
        rowHeight = 36;
        titleW = 170;
        addRow ("MODE", "");
        addValue (P::akMode, "", 160); addValue (P::akPitch, "IN TIME PITCH", 120, 0.02f);
        addText ("IN TIME: THE METALLIC SOUND, THE BEAT KEPT (PAD, SLICES, RENDER)");
        addRule();
        addRow ("RENDER", "");
        addAction ("AKAIZE THE BREAK", [this] { proc.akaizeSelected(); }, nullptr, 260);
        addCustom ([this] {
                       const int slot = proc.getSelectedSlot();
                       const auto& s = proc.getSlot (slot);
                       if (s.content == nullptr) return juce::String ("NO BREAK");
                       return "BREAK " + juce::String (slot + 1) + ": " + juce::String (proc.session().loopBpm (slot), 2) + " BPM";
                   }, nullptr, nullptr, 300);
        addText ("-> 16-BIT WAV");
        addRow ("", "STRETCH");
        addValue (P::akrStretch, "", 150, 0.002f); addText ("SYNC"); addBool (P::akrSync, "ON", "OFF");
        addRow ("", "LOOP BPM");
        addCustom ([this] {
                       const int slot = proc.getSelectedSlot();
                       const double b = proc.session().loopBpm (slot);
                       return b > 1.0 ? juce::String (b, 2) + (proc.session().loopBpmSet (slot) ? "" : " AUTO") : juce::String ("-");
                   },
                   [this] (float steps, bool fine) {
                       const int slot = proc.getSelectedSlot();
                       const double b = proc.session().loopBpm (slot);
                       if (b > 1.0) proc.session().setLoopBpm (slot, b + steps * (fine ? 0.01 : 0.5));
                   },
                   [this] { proc.session().setLoopBpm (proc.getSelectedSlot(), 0.0); }, 200);
        addText ("TO");
        addCustom ([this] { const double h = proc.getHostBpm(); return h > 1.0 ? "HOST " + juce::String (h, 2) : juce::String ("NO HOST >"); },
                   nullptr, nullptr, 190);
        addValue (P::akrTarget, "TARGET", 150, 0.002f);
        addRow ("", "CYCLE");
        addValue (P::akrCycLen, "LENGTH", 140, 0.005f); addValue (P::akrCycFreq, "FREQ", 130, 0.005f);
        addBool (P::akrFreqManual, "MANUAL", "LINKED");
        addRow ("", "PITCH");
        addValue (P::akrPitch, "", 110, 0.02f); addText ("(REPITCH BEFORE THE STRETCH, LENGTH KEPT)");
        addRule();
        addRow ("LIVE", "AKAIZE PAD");
        addValue (P::akCycLen, "LENGTH", 140, 0.005f); addValue (P::akCycFreq, "FREQ", 130, 0.005f);
        addBool (P::akFreqManual, "MANUAL", "LINKED");
        addRow ("", "PAD STRETCH");
        addValue (P::akRatio, "", 110);
        addRow ("", "SLICE STRETCH");
        addValue (P::slCycLen, "LENGTH", 140, 0.005f); addValue (P::slCycFreq, "FREQ", 130, 0.005f);
        addBool (P::slFreqManual, "MANUAL", "LINKED");
        addRow ("", "");
        addText ("DRAG / SCROLL - DOUBLE-CLICK RESETS - LINKED = 41000/LEN");
    }

    // =====================================================================================
    //  SET
    // =====================================================================================
    SetTab::SetTab (ChoppageProcessor& p) : ParamPage (p)
    {
        namespace P = bc::params;
        rowHeight = 36;
        titleW = 160;
        addRow ("TEMPO", "SOURCE");
        addChoice (P::tempoSrc); addText ("SPEED"); addChoice (P::speed);
        addRow ("", "FIT");
        addBool (P::fit, "ON", "OFF"); addChoice (P::fitMode);
        addRow ("", "TRIGGER");
        addChoice (P::trigger); addText ("BREAK"); addValue (P::breakMode, "", 150);
        addRow ("", "GATE");
        addChoice (P::gateMode); addText ("VEL>SLICE"); addBool (P::velSlice, "ON", "OFF");
        addRow ("", "MUTE GROUP");
        for (int g = 0; g < bc::kMuteGroups; ++g)
            addAction (juce::String (g + 1), [this, g] { proc.session().setMuteGroup (proc.getSelectedSlot(), g); },
                       [this, g] { return proc.session().muteGroup (proc.getSelectedSlot()) == g; });
        addText ("(THIS BREAK - SAME GROUP CUTS)");
        addRow ("", "FX QUANTISE");
        {
            // the FX pads wait for the next line of this grid (choices in grid order; the parameter keeps its old order)
            const Param prm = param (P::padQuant);
            const int order[] = { 0, 4, 1, 2, 3, 5 };
            const char* names[] = { "OFF", "1/32", "1/16", "1/8", "1/4", "1 BAR" };
            for (int k = 0; k < 6; ++k)
            {
                const int i = order[k];
                addAction (names[k], [prm, i] { prm.set ((float) i); }, [prm, i] { return prm.choice() == i; });
            }
        }
        addRule();
        addRow ("SOUND", "CHARACTER");
        addValue (P::character, "", 160); addText ("LCD"); addChoice (P::lcdColour);
        addRow ("", "SLICING");
        auto sel = [this]() -> const ChoppageProcessor::Slot& { return proc.getSlot (proc.getSelectedSlot()); };
        addAction ("TRANSIENTS", [this, sel] { const auto& s = sel(); proc.reslice (proc.getSelectedSlot(), false, s.sensitivity, s.gridPerBar); },
                   [sel] { return ! sel().gridSlicing; });
        addAction ("GRID", [this, sel] { const auto& s = sel(); proc.reslice (proc.getSelectedSlot(), true, s.sensitivity, s.gridPerBar); },
                   [sel] { return sel().gridSlicing; });
        for (int g : { 8, 16, 32 })
            addAction ("1/" + juce::String (g), [this, sel, g] { const auto& s = sel(); proc.reslice (proc.getSelectedSlot(), true, s.sensitivity, g); },
                       [sel, g] { return sel().gridSlicing && sel().gridPerBar == g; });
        // SENSITIVITY: drag / scroll (shift = fine) re-finds the transients; double-click = 62%
        addText ("SENS");
        addCustom ([sel] { return juce::String (juce::roundToInt (sel().sensitivity * 100.0f)) + "%"; },
                   [this, sel] (float steps, bool fine)
                   {
                       const auto& s = sel();
                       if (s.content == nullptr) return;
                       proc.reslice (proc.getSelectedSlot(), false, juce::jlimit (0.05f, 0.95f, s.sensitivity + steps * (fine ? 0.01f : 0.05f)), s.gridPerBar);
                   },
                   [this, sel] { proc.reslice (proc.getSelectedSlot(), false, 0.62f, sel().gridPerBar); }, 100);
        addRule();
        addRow ("GENERATE", "STYLE");
        addValue (P::style, "", 170); addText ("LAYERS"); addBool (P::layers, "ON", "OFF"); addText ("SWING"); addValue (P::swing, "", 100, 0.02f);
    }
}
