#pragma once
/*
    Widgets — the panel's parts, all drawn in code (no bitmaps), so they scale cleanly.

      Knob        S950 knob: dark cap, coloured pointer line
      Fader       the tall SAMPLE RATE fader
      ModeButton  square grey key with a red LED and its label above (the tabs)
      DiceButton  PATTERN / FX: big key with a die and a LED
      LcdCell     a piece of LCD text: click / drag / right-click, inverted when "on"
      Pad         an FX pad with its own little LCD showing its setting
      LevelMeter  vertical LED ladder
*/
#include "ChopUI/Skin.h"

namespace ui
{
    /** A host parameter, set with proper begin/end gestures. */
    struct Param
    {
        juce::RangedAudioParameter* p = nullptr;
        float get() const { return p != nullptr ? p->convertFrom0to1 (p->getValue()) : 0.0f; }
        float norm() const { return p != nullptr ? p->getValue() : 0.0f; }
        void set (float plain) const
        {
            if (p == nullptr) return;
            p->beginChangeGesture();
            p->setValueNotifyingHost (p->convertTo0to1 (plain));
            p->endChangeGesture();
        }
        void setNorm (float n) const
        {
            if (p == nullptr) return;
            p->beginChangeGesture();
            p->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, n));
            p->endChangeGesture();
        }
        juce::String text() const { return p != nullptr ? p->getCurrentValueAsText() : juce::String(); }
        int choice() const { return (int) std::round (get()); }
    };

    class Knob final : public juce::Slider
    {
    public:
        explicit Knob (juce::Colour pointer = juce::Colour (0xfff2f2f2));
        void paint (juce::Graphics&) override;
        juce::Colour pointerColour;
    };

    class Fader final : public juce::Slider
    {
    public:
        Fader();
        void paint (juce::Graphics&) override;
    };

    class ModeButton final : public juce::Button
    {
    public:
        explicit ModeButton (const juce::String& label) : juce::Button (label) { setClickingTogglesState (false); }
        void paintButton (juce::Graphics&, bool over, bool down) override;
        bool lit = false;
    };

    class DiceButton final : public juce::Button
    {
    public:
        explicit DiceButton (const juce::String& label) : juce::Button (label) {}
        void paintButton (juce::Graphics&, bool over, bool down) override;
        bool flash = false;
    };

    class LcdCell final : public juce::Component, public juce::SettableTooltipClient
    {
    public:
        std::function<juce::String()> text;
        std::function<bool()> isOn;                                   // inverted cell
        std::function<void (const juce::MouseEvent&)> onClick;
        std::function<void (float steps, const juce::ModifierKeys&)> onDrag;   // + up / right
        std::function<void()> onDragOut;                              // a drag starts an external file drag
        float fontSize = 26.0f;
        juce::Justification just = juce::Justification::centredLeft;
        bool boxed = false;       // draw a 2 px outline (a button on the LCD)
        bool underline = false;   // an editable value

        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;
        void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    private:
        float dragAccum = 0.0f;
        int lastY = 0;
        bool dragged = false;
    };

    class Pad final : public juce::Component
    {
    public:
        juce::String name;
        std::function<juce::String()> setting;
        std::function<bool()> isLit;
        std::function<void (bool down)> onPress;
        std::function<void (float steps)> onSettingDrag;

        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;
        void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    private:
        bool down = false;
    };

    class LevelMeter final : public juce::Component
    {
    public:
        void setLevel (float peak);
        void paint (juce::Graphics&) override;
    private:
        float level = 0.0f;
    };

    /** S950-style label (dark, bold, letter-spaced, centred). */
    void drawLabel (juce::Graphics&, const juce::String&, juce::Rectangle<int>, float size = 18.0f,
                    juce::Justification = juce::Justification::centred);
    /** A grey section strip with white text ("FX PADS · G#1-D#2"). */
    void drawSection (juce::Graphics&, const juce::String&, juce::Rectangle<int>);
}
