// Paula's Revenge - right-click EFFECT COMMANDS: pick a command on the left, build its value on the right
#include "CommandPicker.h"

namespace prv::ui
{
CommandPicker::CommandPicker()
{
    setWantsKeyboardFocus (true);
    const auto& cmds = commandList();
    list.getNumRows = [&cmds] { return (int) cmds.size(); };
    list.getRowText = [this] (int r) { return rowTextFor (r); };
    list.onClick = [this] (int r, const juce::MouseEvent&) { select (r); grabKeyboardFocus(); };
    list.onDoubleClick = [this] (int r) { pick (r); };
    addAndMakeVisible (list);
    insertBtn.onClick = [this] { insert(); };
    cancelBtn.onClick = [this] { if (onCancel) onCancel(); };
    addAndMakeVisible (insertBtn);
    addAndMakeVisible (cancelBtn);
}

juce::String CommandPicker::rowTextFor (int row) const
{
    // "U   CHOP FX PAD" - the code and the name; what it does is on the right
    const auto& c = commandList()[(size_t) row];
    juce::String name = nameFor ? nameFor (c) : juce::String();
    if (name.isEmpty()) name = c.name;
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
    if (same) { param = currentParam; refreshParts(); }   // the value already in the cell
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
        param = cmds[(size_t) row].param;   // a sensible value to start from
        rebuildParts();
    }
    list.repaint();
    repaint();
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

void CommandPicker::rebuildParts()
{
    for (auto& row : partBtns) for (auto& b : row) removeChildComponent (b.get());
    for (auto& b : fineBtns) removeChildComponent (b.get());
    partBtns.clear();
    fineBtns.clear();
    const auto& cmds = commandList();
    if (list.selected < 0 || list.selected >= (int) cmds.size()) { parts.clear(); return; }
    const auto& c = cmds[(size_t) list.selected];
    parts = partsFor ? partsFor (c) : commandParts (c, false);
    for (int p = 0; p < (int) parts.size(); ++p)
    {
        std::vector<std::unique_ptr<PixButton>> row;
        for (int k = 0; k < (int) parts[(size_t) p].choices.size(); ++k)
        {
            auto b = std::make_unique<PixButton> (juce::String (parts[(size_t) p].choices[(size_t) k].second), true);
            b->setClickingTogglesState (false);
            b->setWantsKeyboardFocus (false);
            b->onClick = [this, p, k] { choose (p, k); };
            addAndMakeVisible (*b);
            row.push_back (std::move (b));
        }
        partBtns.push_back (std::move (row));
        if (parts[(size_t) p].fine)
        {
            const int mask = parts[(size_t) p].mask, shift = parts[(size_t) p].shift;
            for (int d : { -16, -1, 1, 16 })
            {
                auto b = std::make_unique<PixButton> (d < 0 ? juce::String (d == -1 ? "- 1" : "- 10") : juce::String (d == 1 ? "+ 1" : "+ 10"));
                b->setWantsKeyboardFocus (false);
                b->onClick = [this, d, mask, shift]
                {
                    const int v = juce::jlimit (0, mask, ((param >> shift) & mask) + d);
                    param = (uint8_t) ((param & ~(mask << shift)) | (v << shift));
                    refreshParts();
                    repaint();
                };
                addAndMakeVisible (*b);
                fineBtns.push_back (std::move (b));
            }
        }
    }
    refreshParts();
    layoutBuilder();
}

void CommandPicker::refreshParts()
{
    for (int p = 0; p < (int) parts.size() && p < (int) partBtns.size(); ++p)
    {
        const auto& part = parts[(size_t) p];
        const int v = (param >> part.shift) & part.mask;
        for (int k = 0; k < (int) partBtns[(size_t) p].size(); ++k)
            partBtns[(size_t) p][(size_t) k]->setToggleState (part.choices[(size_t) k].first == v, juce::dontSendNotification);
    }
}

void CommandPicker::choose (int p, int k)
{
    if (p < 0 || p >= (int) parts.size() || k < 0 || k >= (int) parts[(size_t) p].choices.size()) return;
    const auto& part = parts[(size_t) p];
    const int v = part.choices[(size_t) k].first & part.mask;
    param = (uint8_t) ((param & ~(part.mask << part.shift)) | (v << part.shift));
    refreshParts();
    repaint();
}

juce::String CommandPicker::readout() const
{
    const auto& cmds = commandList();
    if (list.selected < 0 || list.selected >= (int) cmds.size()) return {};
    const auto& c = cmds[(size_t) list.selected];
    const juce::String code = juce::String::charToString ((juce::juce_wchar) commandChar (c.cmd));
    const juce::String hex = juce::String::toHexString ((int) param).toUpperCase().paddedLeft ('0', 2);
    juce::String d = describe ? describe (c.cmd, param) : juce::String (describeCommand (c.cmd, param));
    d = d.fromFirstOccurrenceOf (": ", false, false);
    // (the digit-by-digit part of a long description goes on its own lines)
    d = d.upToFirstOccurrenceOf ("  (", false, false);
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
    if (list.selected < 0 || list.selected >= (int) cmds.size()) return;
    const auto& c = cmds[(size_t) list.selected];
    const float lineH = PixelFont::textHeight (px) + px * 2;
    // the command as you'd type it, big, and what it does in words
    {
        auto r = readoutArea;
        const juce::String code = juce::String::charToString ((juce::juce_wchar) commandChar (c.cmd));
        const juce::String hex = juce::String::toHexString ((int) param).toUpperCase().paddedLeft ('0', 2);
        const float big = px * 2.0f;
        const juce::String digits = code + " " + hex.substring (0, 1) + " " + hex.substring (1);
        PixelFont::draw (g, digits, r.getX(), r.getY(), big, Theme::ink);
        const float dx = r.getX() + PixelFont::textWidth (digits + " ", big);
        const int chars = std::max (8, (int) ((r.getRight() - dx) / (px * PixelFont::kCellW)));
        auto said = wrap ("= " + readout().fromFirstOccurrenceOf (" = ", false, false), chars);
        float y = r.getY() + (PixelFont::textHeight (big) - PixelFont::textHeight (px)) * 0.5f - (said.size() > 1 ? lineH * 0.5f : 0.0f);
        for (auto& s : said) { PixelFont::draw (g, s, dx, y, px, Theme::ink); y += lineH; }
    }
    // what the command does (in full, wrapped)
    {
        juce::String over = overviewFor ? overviewFor (c) : juce::String();
        if (over.isEmpty()) over = c.overview;
        const int chars = std::max (10, (int) (textArea.getWidth() / (px * PixelFont::kCellW)));
        float y = textArea.getY();
        for (auto& s : wrap (over, chars))
        {
            if (y + lineH > textArea.getBottom() + 1.0f) break;
            PixelFont::draw (g, s, textArea.getX(), y, px, Theme::ink);
            y += lineH;
        }
    }
    // the parts' names over their buttons
    for (int p = 0; p < (int) parts.size() && p < (int) partLabels.size(); ++p)
        PixelFont::drawLeft (g, juce::String (parts[(size_t) p].label), partLabels[(size_t) p], px, Theme::ink);
}

void CommandPicker::resized()
{
    const float px = Theme::px;
    auto r = getLocalBounds().reduced ((int) (px * 2));
    r.removeFromTop ((int) (PixelFont::textHeight (px) + px * 2));
    const int rowH = (int) (px * 11);
    // the list down the left: a code and a name
    list.setBounds (r.removeFromLeft ((int) (px * PixelFont::kCellW * 26 + px * 4)));
    r.removeFromLeft ((int) (px * 4));
    auto bottom = r.removeFromBottom (rowH);
    cancelBtn.setBounds (bottom.removeFromRight ((int) (px * PixelFont::kCellW * 12 + px * 6)));
    bottom.removeFromRight ((int) (px * 2));
    insertBtn.setBounds (bottom.removeFromRight ((int) (px * PixelFont::kCellW * 14 + px * 6)));
    r.removeFromBottom ((int) (px * 2));
    builderArea = r.toFloat();
    layoutBuilder();
}

void CommandPicker::layoutBuilder()
{
    const float px = Theme::px;
    if (builderArea.isEmpty()) return;
    auto r = builderArea;
    const float lineH = PixelFont::textHeight (px) + px * 2;
    readoutArea = r.removeFromTop (std::max (PixelFont::textHeight (px * 2.0f), lineH * 2) + px * 2);
    r.removeFromTop (px * 3);
    // the buttons first (they need their room), the overview gets what's left above them
    const int rowH = (int) (px * 11), gap = (int) px * 2;
    const float cell = px * PixelFont::kCellW;
    struct Row { float y; };
    // lay the parts out from the top of a scratch area, then move them under the overview
    std::vector<std::vector<juce::Rectangle<int>>> spots (parts.size());
    std::vector<juce::Rectangle<float>> labels (parts.size());
    std::vector<juce::Rectangle<int>> fineSpots;
    float y = 0;
    for (int p = 0; p < (int) parts.size(); ++p)
    {
        labels[(size_t) p] = { r.getX(), y, r.getWidth(), lineH };
        y += lineH;
        float x = r.getX();
        for (int k = 0; k < (int) partBtns[(size_t) p].size(); ++k)
        {
            const int w = (int) (PixelFont::textWidth (partBtns[(size_t) p][(size_t) k]->getButtonText(), px) + cell * 2);
            if (x + w > r.getRight() && x > r.getX()) { x = r.getX(); y += rowH + gap; }
            spots[(size_t) p].push_back ({ (int) x, (int) y, w, rowH });
            x += w + gap;
        }
        y += rowH + gap;
        if (parts[(size_t) p].fine)
        {
            float fx = r.getX();
            for (int k = 0; k < 4; ++k)
            {
                const int w = (int) (cell * 6);
                fineSpots.push_back ({ (int) fx, (int) y, w, rowH });
                fx += w + gap;
            }
            y += rowH + gap;
        }
        y += px * 2;
    }
    const float partsH = y;
    const float textH = std::max (lineH * 2, r.getHeight() - partsH - px * 3);
    textArea = r.removeFromTop (textH);
    const float top = textArea.getBottom() + px * 3;
    partLabels.clear();
    for (int p = 0; p < (int) parts.size(); ++p)
    {
        partLabels.push_back (labels[(size_t) p].translated (0, top));
        for (int k = 0; k < (int) spots[(size_t) p].size(); ++k)
            partBtns[(size_t) p][(size_t) k]->setBounds (spots[(size_t) p][(size_t) k].translated (0, (int) top));
    }
    for (int k = 0; k < (int) fineBtns.size() && k < (int) fineSpots.size(); ++k)
        fineBtns[(size_t) k]->setBounds (fineSpots[(size_t) k].translated (0, (int) top));
    repaint();
}

bool CommandPicker::keyPressed (const juce::KeyPress& k)
{
    const auto& cmds = commandList();
    const int n = (int) cmds.size();
    const int code = k.getKeyCode();
    if (code == juce::KeyPress::escapeKey) { if (onCancel) onCancel(); return true; }
    if (code == juce::KeyPress::returnKey) { insert(); return true; }
    if (code == juce::KeyPress::upKey || code == juce::KeyPress::downKey)
    {
        select (juce::jlimit (0, n - 1, list.selected + (code == juce::KeyPress::upKey ? -1 : 1)));
        return true;
    }
    if (code == juce::KeyPress::leftKey || code == juce::KeyPress::rightKey)
    {
        // the value up / down by one
        param = (uint8_t) juce::jlimit (0, 255, (int) param + (code == juce::KeyPress::leftKey ? -1 : 1));
        refreshParts();
        repaint();
        return true;
    }
    // typing a command's letter or digit jumps to it (E again steps through the E commands)
    const auto ch = juce::CharacterFunctions::toUpperCase (k.getTextCharacter());
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
