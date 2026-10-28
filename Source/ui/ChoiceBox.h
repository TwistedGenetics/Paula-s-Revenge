// Paula's Revenge - a big-text question with a few answers, over the whole window
#pragma once

#include <JuceHeader.h>
#include "Widgets.h"

namespace prv::ui
{
class ChoiceBox : public juce::Component
{
public:
    ChoiceBox() { setWantsKeyboardFocus (true); setAlwaysOnTop (true); }

    // shows it over `parent` (the whole of it); `done` gets the answer's index, or -1 (CANCEL / Esc)
    void show (juce::Component& parent, const juce::String& question, const juce::StringArray& answers, std::function<void (int)> done)
    {
        title = question;
        onDone = std::move (done);
        buttons.clear();
        for (int i = 0; i <= answers.size(); ++i)
        {
            auto b = std::make_unique<PixButton> (i < answers.size() ? answers[i] : juce::String ("CANCEL"));
            const int answer = i < answers.size() ? i : -1;
            b->onClick = [this, answer] { finish (answer); };
            addAndMakeVisible (*b);
            buttons.push_back (std::move (b));
        }
        if (getParentComponent() != &parent) parent.addChildComponent (this);
        setBounds (parent.getLocalBounds());
        setVisible (true);
        toFront (true);
        grabKeyboardFocus();
        resized();
        repaint();
    }
    bool isShowing() const { return isVisible(); }
    void choose (int answer) { finish (answer); }   // (tests)
    int  numAnswers() const { return (int) buttons.size() - 1; }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colours::black.withAlpha (0.35f));
        g.setColour (Theme::panel);
        g.fillRect (box);
        Theme::bevel (g, box, true, std::max (1.0f, Theme::px * 0.5f));
        PixelFont::drawCentred (g, title, titleArea, Theme::px, Theme::ink);
    }
    void resized() override
    {
        const float px = Theme::px;
        const int rowH = (int) (px * 11), gap = (int) std::max (2.0f, px * 1.5f);
        int widest = (int) PixelFont::textWidth (title, px);
        for (auto& b : buttons) widest = std::max (widest, (int) PixelFont::textWidth (b->getButtonText(), px));
        const int w = std::min (getWidth() - gap * 4, widest + (int) (px * 16));
        const int h = rowH * (1 + (int) buttons.size()) + gap * (3 + (int) buttons.size());
        box = getLocalBounds().withSizeKeepingCentre (w, h).toFloat();
        auto r = box.toNearestInt().reduced (gap * 2);
        titleArea = r.removeFromTop (rowH).toFloat();
        r.removeFromTop (gap);
        for (auto& b : buttons) { b->setBounds (r.removeFromTop (rowH)); r.removeFromTop (gap); }
    }
    bool keyPressed (const juce::KeyPress& k) override
    {
        if (k == juce::KeyPress::escapeKey) { finish (-1); return true; }
        return true;   // (nothing else while it's up)
    }
    void mouseDown (const juce::MouseEvent& e) override { if (! box.contains (e.position)) finish (-1); }

private:
    juce::String title;
    std::vector<std::unique_ptr<PixButton>> buttons;
    std::function<void (int)> onDone;
    juce::Rectangle<float> box, titleArea;
    void finish (int answer)
    {
        setVisible (false);
        auto f = std::move (onDone);
        onDone = nullptr;
        if (f) f (answer);
    }
};
} // namespace prv::ui
