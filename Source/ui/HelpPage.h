// Paula's Revenge - HELP: the README, built into the app, in the app's own pixel text
#pragma once

#include <JuceHeader.h>
#include "Widgets.h"
#include "Panels.h"

namespace prv::ui
{
// One piece of the README, ready to lay out
struct HelpBlock
{
    enum class Kind { Chapter, Heading, Text, Bullet, Row, HeaderRow, Gap } kind = Kind::Text;
    juce::String text;               // (Row: the first column)
    juce::String text2;              // Row: the other columns
    int indent = 0;                  // bullets inside bullets
};

// the README (markdown) as blocks, in characters the pixel font has; Install and Building are left out
std::vector<HelpBlock> parseHelp (const juce::String& markdown);
juce::String helpText (const juce::String& s);   // markdown marks out, symbols swapped for ones the font has

class HelpPage : public juce::Component
{
public:
    HelpPage();

    std::function<void()> onClose;
    std::function<void (const juce::String&)> onStatus;
    bool launchPdf = true;              // (tests: write the PDF out, but don't open it)

    void opened();
    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;

    // tests
    int  chapterCount() const { return (int) chapters.size(); }
    juce::String chapterName (int i) const { return i >= 0 && i < (int) chapters.size() ? chapters[(size_t) i].name : juce::String(); }
    void showChapter (int i);
    int  currentChapter() const;
    int  topRow() const { return top; }
    int  rowCount() const { return (int) rows.size(); }
    juce::String rowText (int i) const { return i >= 0 && i < (int) rows.size() ? rows[(size_t) i].text : juce::String(); }
    juce::File openPdf();               // OPEN PDF MANUAL: the built-in PDF, written out and opened

private:
    std::vector<HelpBlock> blocks;
    struct Chapter { juce::String name; int block = 0; int row = 0; };
    std::vector<Chapter> chapters;
    // laid-out rows of text for the current width
    struct Row { juce::String text; int x = 0; HelpBlock::Kind kind = HelpBlock::Kind::Text; int chapter = -1; bool first = false; };
    std::vector<Row> rows;
    int top = 0, laidOutChars = -1;
    PixList list;
    PixButton pdfBtn { "OPEN PDF MANUAL" }, closeBtn { "CLOSE" };
    juce::Rectangle<int> textArea, scrollBar;
    float dragFrom = -1; int dragTop = 0;
    WheelSteps wheel;

    void layout();
    int  visibleRows() const;
    float rowH() const;
    void scrollTo (int row);
};
} // namespace prv::ui
