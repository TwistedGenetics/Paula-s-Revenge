// Paula's Revenge - right-click EFFECT COMMANDS (mock-up B): the list, and a how-to card for the command picked
#include "CommandPicker.h"

namespace prv::ui
{
namespace
{
    // the digits' colour: yellow on a dark panel, a deep amber on a light one (always readable)
    juce::Colour digitColour() { return Theme::panel.getPerceivedBrightness() < 0.5f ? juce::Colour (0xfff2c14e) : juce::Colour (0xff7a4a00); }
    juce::String afterDash (const std::string& label)
    {
        const juce::String l (label);
        return l.contains (" - ") ? l.fromFirstOccurrenceOf (" - ", false, false) : l;
    }
    juce::String hex2 (int v) { return juce::String::toHexString (v & 0xFF).toUpperCase().paddedLeft ('0', 2); }
}

CommandPicker::CommandPicker()
{
    setWantsKeyboardFocus (true);
    const auto& cmds = commandList();
    list.getNumRows = [&cmds] { return (int) cmds.size(); };
    list.getRowText = [this] (int r) { return rowTextFor (r); };
    list.onClick = [this] (int r, const juce::MouseEvent&)
    {
        // # CHORD goes straight to the chord list (its value is a chord, picked there)
        if (r >= 0 && r < (int) commandList().size() && commandList()[(size_t) r].cmd == kCmdChord) { pick (r); return; }
        select (r);
        grabKeyboardFocus();
    };
    list.onDoubleClick = [this] (int r) { pick (r); };
    addAndMakeVisible (list);
    insertBtn.onClick = [this] { insert(); };
    cancelBtn.onClick = [this] { if (onCancel) onCancel(); };
    addAndMakeVisible (insertBtn);
    addAndMakeVisible (cancelBtn);
}

juce::String CommandPicker::rowTextFor (int row) const
{
    // "U   FX PAD": the code and the name, the rest is on the card
    const auto& c = commandList()[(size_t) row];
    juce::String name = nameFor ? nameFor (c) : juce::String();
    if (name.isEmpty()) name = c.name;
    if (name.startsWith ("CHOP ")) name = name.substring (5);
    return juce::String (c.code).paddedRight (' ', 4) + name;
}

void CommandPicker::open (int currentCmd, uint8_t currentParam)
{
    const auto& cmds = commandList();
    int row = 0;
    bool same = false;
    for (int i = 0; i < (int) cmds.size(); ++i)
    {
        const auto& c = cmds[(size_t) i];
        if (c.cmd != currentCmd) continue;
        if (! c.extended || (c.param >> 4) == (currentParam >> 4)) { row = i; same = ! (currentCmd == 0 && currentParam == 0); break; }
    }
    shownRow = -1;
    select (row);
    if (same) param = currentParam;   // the value already in the cell
    setVisible (true);
    toFront (true);
    list.ensureVisible (list.selected);
    grabKeyboardFocus();
    repaint();
}

void CommandPicker::select (int row)
{
    const auto& cmds = commandList();
    if (row < 0 || row >= (int) cmds.size()) return;
    list.selected = row;
    list.ensureVisible (row);
    if (row != shownRow)
    {
        shownRow = row;
        loadCard();
    }
    list.repaint();
    repaint();
}

void CommandPicker::loadCard()
{
    const auto& c = commandList()[(size_t) list.selected];
    parts = partsFor ? partsFor (c) : commandParts (c, false);
    examples = examplesFor ? examplesFor (c) : commandExamples (c, false);
    param = examples.empty() ? c.param : examples.front();   // start from the first example
    digit = -1;
}

void CommandPicker::pick (int row)
{
    select (row);
    insert();
}

void CommandPicker::insert()
{
    const auto& cmds = commandList();
    if (list.selected < 0 || list.selected >= (int) cmds.size()) return;
    CommandInfo c = cmds[(size_t) list.selected];
    c.param = param;
    if (onPick) onPick (c);
}

void CommandPicker::pickExample (int k)
{
    if (k < 0 || k >= (int) examples.size()) return;
    param = examples[(size_t) k];
    fromExample = true;
    insert();
    fromExample = false;
}

void CommandPicker::choose (int p, int k)
{
    if (p < 0 || p >= (int) parts.size() || k < 0 || k >= (int) parts[(size_t) p].choices.size()) return;
    const auto& part = parts[(size_t) p];
    const int v = part.choices[(size_t) k].first & part.mask;
    param = (uint8_t) ((param & ~(part.mask << part.shift)) | (v << part.shift));
    repaint();
}

void CommandPicker::nudge (int p, int by)
{
    if (p < 0 || p >= (int) parts.size()) { param = (uint8_t) juce::jlimit (0, 255, (int) param + by); repaint(); return; }
    const auto& part = parts[(size_t) p];
    const int v = juce::jlimit (0, part.mask, ((param >> part.shift) & part.mask) + by);
    param = (uint8_t) ((param & ~(part.mask << part.shift)) | (v << part.shift));
    repaint();
}

void CommandPicker::typeDigit (int h)
{
    if (digit < 0 || digit >= (int) parts.size()) return;
    const auto& part = parts[(size_t) digit];
    if (part.mask == 0xFF) param = (uint8_t) (((param << 4) | h) & 0xFF);   // XX: type both digits, like the pattern
    else
    {
        param = (uint8_t) ((param & ~(part.mask << part.shift)) | ((h & part.mask) << part.shift));
        if (digit + 1 < (int) parts.size()) ++digit;                         // X, then on to Y
    }
    repaint();
}

juce::String CommandPicker::readout() const
{
    const auto& cmds = commandList();
    if (list.selected < 0 || list.selected >= (int) cmds.size()) return {};
    const auto& c = cmds[(size_t) list.selected];
    const juce::String code = juce::String::charToString ((juce::juce_wchar) commandChar (c.cmd));
    const juce::String hex = hex2 (param);
    juce::String d = describe ? describe (c.cmd, param) : juce::String (describeCommand (c.cmd, param));
    d = d.fromFirstOccurrenceOf (": ", false, false).upToFirstOccurrenceOf ("  (", false, false);
    return code + " " + hex.substring (0, 1) + " " + hex.substring (1) + " = " + d;
}

juce::StringArray CommandPicker::wrap (const juce::String& text, int chars)
{
    juce::StringArray out;
    juce::String line;
    for (auto& w : juce::StringArray::fromTokens (text, " ", ""))
    {
        if (w.isEmpty()) continue;
        if (line.isNotEmpty() && line.length() + 1 + w.length() > chars) { out.add (line); line.clear(); }
        line = line.isEmpty() ? w : line + " " + w;
    }
    if (line.isNotEmpty()) out.add (line);
    return out;
}

void CommandPicker::paint (juce::Graphics& g)
{
    Theme::panelBox (g, getLocalBounds().toFloat(), "EFFECT COMMANDS");
    const float px = Theme::px;
    const auto& cmds = commandList();
    digitBoxes.clear();
    exampleRows.clear();
    if (list.selected < 0 || list.selected >= (int) cmds.size() || cardArea.isEmpty()) return;
    const auto& c = cmds[(size_t) list.selected];
    const float cell = px * PixelFont::kCellW;
    const float lineH = PixelFont::textHeight (px) + px * 2;
    const int chars = std::max (10, (int) (cardArea.getWidth() / cell));
    auto r = cardArea;
    const auto yellow = digitColour();
    juce::String name = nameFor ? nameFor (c) : juce::String();
    if (name.isEmpty()) name = c.name;
    const bool chop = name.startsWith ("CHOP ");
    if (chop) name = name.substring (5);
    const juce::String code (c.code);

    // the card's title, underlined: "U - FX PAD (CHOPPAGE)"
    PixelFont::draw (g, code + " - " + name + (chop ? " (CHOPPAGE)" : ""), r.getX(), r.getY(), px, Theme::ink);
    r.removeFromTop (lineH);
    g.setColour (Theme::ink.withAlpha (0.5f));
    g.fillRect (r.getX(), r.getY(), r.getWidth(), std::max (1.0f, px * 0.5f));
    r.removeFromTop (px * 4);

    // what it does
    {
        juce::String over = overviewFor ? overviewFor (c) : juce::String();
        if (over.isEmpty()) over = c.overview;
        for (auto& s : wrap (over, chars)) { PixelFont::draw (g, s, r.getX(), r.getY(), px, Theme::ink); r.removeFromTop (lineH); }
    }
    r.removeFromTop (px * 5);

    // the digits, big, each with what it means under it (click one and type, or turn the wheel over it)
    {
        const float big = px * 2.2f;
        const float boxH = PixelFont::textHeight (big) + px * 4;
        const juce::String hex = hex2 (param);
        struct D { juce::String text, caption; int part; };
        std::vector<D> ds;
        ds.push_back ({ c.extended ? juce::String (c.code) : code, name, -1 });
        for (int p = 0; p < (int) parts.size(); ++p)
        {
            const auto& part = parts[(size_t) p];
            const juce::String t = part.mask == 0xFF ? hex : (part.shift == 4 ? hex.substring (0, 1) : hex.substring (1));
            ds.push_back ({ t, afterDash (part.label).upToFirstOccurrenceOf (" (", false, false), p });
        }
        float x = r.getX();
        for (auto& d : ds)
        {
            const float w = std::max (PixelFont::textWidth (d.text, big) + px * 8, PixelFont::textWidth (d.caption, px) + px * 2);
            if (x + w > r.getRight() && x > r.getX()) break;
            const juce::Rectangle<float> box (x + (w - (PixelFont::textWidth (d.text, big) + px * 8)) * 0.5f, r.getY(),
                                              PixelFont::textWidth (d.text, big) + px * 8, boxH);
            const bool typing = d.part >= 0 && d.part == digit;
            if (typing) { g.setColour (yellow.withAlpha (0.25f)); g.fillRoundedRectangle (box, px); }
            g.setColour (d.part >= 0 ? yellow : yellow.withAlpha (0.7f));
            g.drawRoundedRectangle (box, px, std::max (1.0f, px * (typing ? 0.8f : 0.5f)));
            PixelFont::drawCentred (g, d.text, box, big, yellow);
            PixelFont::drawCentred (g, d.caption, { x, box.getBottom() + px * 2, w, lineH }, px, Theme::ink);
            digitBoxes.push_back (d.part >= 0 ? box : juce::Rectangle<float>());
            x += w + px * 6;
        }
        // beside them: what it'll do, in words, and how to change it
        const float sx = x + px * 2;
        const int sideChars = std::max (8, (int) ((r.getRight() - sx) / cell));
        auto said = wrap ("= " + readout().fromFirstOccurrenceOf (" = ", false, false), sideChars);
        auto hint = wrap (c.cmd == kCmdChord ? "ENTER: THE CHORD LIST" : "CLICK A DIGIT, TYPE 0-F OR TURN THE WHEEL", sideChars);
        float sy = r.getY();
        for (auto& s : said) { PixelFont::draw (g, s, sx, sy, px, yellow); sy += lineH; }
        for (auto& s : hint) { PixelFont::draw (g, s, sx, sy, px, Theme::ink); sy += lineH; }
        r.removeFromTop (std::max (boxH + px * 2 + lineH, sy - r.getY()) + px * 5);
    }

    // what each digit can be ("PADS: 1 STUTTER - 2 ROLL ...")
    for (auto& part : parts)
    {
        juce::StringArray named;
        for (auto& ch : part.choices)
        {
            const juce::String n (ch.second);
            const bool plain = n.length() <= 2 || n.matchesWildcard ("? (*)", true);
            if (! plain || part.mask == 0xFF) named.add (n);
        }
        if (part.choices.empty()) continue;   // (# CHORD: its values are the chord list)
        const juce::String line = afterDash (part.label) + ": " + (named.isEmpty() ? juce::String ("0-F") : named.joinIntoString ("  -  "));
        for (auto& s : wrap (line, chars)) { PixelFont::draw (g, s, r.getX(), r.getY(), px, Theme::ink); r.removeFromTop (lineH); }
    }
    r.removeFromTop (px * 4);

    // examples, in a box: click one to put it in
    if (! examples.empty())
    {
        PixelFont::draw (g, "EXAMPLES - CLICK ONE TO PUT IT IN", r.getX(), r.getY(), px, Theme::ink);
        r.removeFromTop (lineH + px);
        const float rowH = lineH + px * 2;
        const int n = std::min ((int) examples.size(), std::max (0, (int) ((r.getHeight() - px * 4) / rowH)));   // (as many as fit)
        auto box = r.removeFromTop (rowH * (float) n + px * 4);
        g.setColour (Theme::ink.withAlpha (0.5f));
        g.drawRoundedRectangle (box, px, std::max (1.0f, px * 0.5f));
        float y = box.getY() + px * 2;
        for (int k = 0; k < n; ++k)
        {
            const juce::Rectangle<float> row (box.getX() + px, y, box.getWidth() - px * 2, rowH);
            const uint8_t v = examples[(size_t) k];
            juce::String d = describe ? describe (c.cmd, v) : juce::String (describeCommand (c.cmd, v));
            d = d.fromFirstOccurrenceOf (": ", false, false).upToFirstOccurrenceOf ("  (", false, false);
            const juce::String t = juce::String::charToString ((juce::juce_wchar) commandChar (c.cmd)) + hex2 (v);
            if (v == param) { g.setColour (yellow.withAlpha (0.18f)); g.fillRect (row); }
            PixelFont::draw (g, t, row.getX() + px * 2, row.getY() + px * 1.5f, px, yellow);
            PixelFont::draw (g, d.substring (0, std::max (0, chars - 7)), row.getX() + px * 2 + cell * 5, row.getY() + px * 1.5f, px, Theme::ink);
            exampleRows.push_back (row);
            y += rowH;
        }
    }
}

void CommandPicker::resized()
{
    const float px = Theme::px;
    auto r = getLocalBounds().reduced ((int) (px * 2));
    r.removeFromTop ((int) (PixelFont::textHeight (px) + px * 2));
    const int rowH = (int) (px * 11);
    // the list down the left: a code and a name
    list.setBounds (r.removeFromLeft ((int) (px * PixelFont::kCellW * 24 + px * 4)));
    r.removeFromLeft ((int) (px * 6));
    auto bottom = r.removeFromBottom (rowH);
    cancelBtn.setBounds (bottom.removeFromRight ((int) (px * PixelFont::kCellW * 12 + px * 6)));
    bottom.removeFromRight ((int) (px * 2));
    insertBtn.setBounds (bottom.removeFromRight ((int) (px * PixelFont::kCellW * 14 + px * 6)));
    r.removeFromBottom ((int) (px * 2));
    cardArea = r.toFloat();
    repaint();
}

void CommandPicker::mouseDown (const juce::MouseEvent& e)
{
    const auto p = e.position;
    for (int k = 0; k < (int) exampleRows.size(); ++k)
        if (exampleRows[(size_t) k].contains (p)) { pickExample (k); return; }
    for (int k = 0; k < (int) digitBoxes.size(); ++k)
        if (! digitBoxes[(size_t) k].isEmpty() && digitBoxes[(size_t) k].contains (p))
        {
            digit = k - 1;   // (box 0 is the command's letter)
            grabKeyboardFocus();
            repaint();
            return;
        }
    digit = -1;
    grabKeyboardFocus();
    repaint();
}

void CommandPicker::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    for (int k = 0; k < (int) digitBoxes.size(); ++k)
        if (! digitBoxes[(size_t) k].isEmpty() && digitBoxes[(size_t) k].contains (e.position))
        {
            const float d = std::abs (w.deltaY) > std::abs (w.deltaX) ? w.deltaY : -w.deltaX;
            if (d != 0.0f) nudge (k - 1, d > 0 ? 1 : -1);
            return;
        }
}

bool CommandPicker::keyPressed (const juce::KeyPress& k)
{
    const auto& cmds = commandList();
    const int n = (int) cmds.size();
    const int code = k.getKeyCode();
    if (code == juce::KeyPress::escapeKey) { if (digit >= 0) { digit = -1; repaint(); return true; } if (onCancel) onCancel(); return true; }
    if (code == juce::KeyPress::returnKey) { insert(); return true; }
    if (code == juce::KeyPress::upKey || code == juce::KeyPress::downKey)
    {
        select (juce::jlimit (0, n - 1, list.selected + (code == juce::KeyPress::upKey ? -1 : 1)));
        return true;
    }
    if (code == juce::KeyPress::leftKey || code == juce::KeyPress::rightKey)
    {
        nudge (digit, code == juce::KeyPress::leftKey ? -1 : 1);   // (no digit picked: the whole value)
        return true;
    }
    if (code == juce::KeyPress::tabKey)
    {
        // Tab: from digit to digit
        digit = parts.empty() ? -1 : (digit + 1 >= (int) parts.size() ? 0 : digit + 1);
        repaint();
        return true;
    }
    const auto ch = juce::CharacterFunctions::toUpperCase (k.getTextCharacter());
    // a digit picked: 0-F type into it
    if (digit >= 0)
    {
        const int h = (ch >= '0' && ch <= '9') ? ch - '0' : (ch >= 'A' && ch <= 'F') ? 10 + ch - 'A' : -1;
        if (h >= 0) { typeDigit (h); return true; }
    }
    // otherwise typing a command's letter or digit jumps to it (E again steps through the E commands)
    if ((ch >= '0' && ch <= '9') || (ch >= 'A' && ch <= 'Z') || ch == '#' || ch == '@' || ch == '$' || ch == '!' || ch == '?')
    {
        for (int step = 1; step <= n; ++step)
        {
            const int i = (list.selected + step) % n;
            if (cmds[(size_t) i].code[0] == (char) ch) { select (i); break; }
        }
        return true;
    }
    return true;   // (the menu keeps the keyboard while it's open)
}
} // namespace prv::ui
