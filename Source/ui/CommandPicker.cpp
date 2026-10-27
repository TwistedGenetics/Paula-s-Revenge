// Paula's Revenge - right-click menu of effect commands for the pattern
#include "CommandPicker.h"

namespace prv::ui
{
CommandPicker::CommandPicker()
{
    setWantsKeyboardFocus (true);
    const auto& cmds = commandList();
    list.getNumRows = [&cmds] { return (int) cmds.size(); };
    list.getRowText = [&cmds] (int r) { return rowText (cmds[(size_t) r]); };
    list.onClick = [this] (int r, const juce::MouseEvent&) { pick (r); };
    list.onHover = [this] (int r) { if (r >= 0 && r != list.selected) { list.selected = r; list.repaint(); repaint (infoArea.toNearestInt()); } };
    addAndMakeVisible (list);
    cancelBtn.onClick = [this] { if (onCancel) onCancel(); };
    addAndMakeVisible (cancelBtn);
}

juce::String CommandPicker::rowText (const CommandInfo& c)
{
    // "C   SET VOLUME         THE NOTE'S VOLUME, 00-40 (40 = FULL)"
    return juce::String (c.code).paddedRight (' ', 4) + juce::String (c.name).paddedRight (' ', 18) + juce::String (c.overview);
}

void CommandPicker::open (int currentCmd, uint8_t currentParam)
{
    const auto& cmds = commandList();
    list.selected = 0;
    for (int i = 0; i < (int) cmds.size(); ++i)
    {
        const auto& c = cmds[(size_t) i];
        if (c.cmd != currentCmd) continue;
        if (! c.extended || (c.param >> 4) == (currentParam >> 4)) { list.selected = i; break; }
    }
    setVisible (true);
    toFront (true);
    list.ensureVisible (list.selected);
    grabKeyboardFocus();
    repaint();
}

void CommandPicker::pick (int row)
{
    const auto& cmds = commandList();
    if (row < 0 || row >= (int) cmds.size()) return;
    if (onPick) onPick (cmds[(size_t) row]);
}

void CommandPicker::paint (juce::Graphics& g)
{
    Theme::panelBox (g, getLocalBounds().toFloat(), "EFFECT COMMANDS");
    const float px = Theme::px;
    const auto& cmds = commandList();
    if (list.selected >= 0 && list.selected < (int) cmds.size())
    {
        const auto& c = cmds[(size_t) list.selected];
        juce::String t = juce::String (describeCommand (c.cmd, c.param));
        const int fit = (int) ((infoArea.getWidth() - px * 2) / (px * PixelFont::kCellW));
        if (t.length() > fit) t = t.substring (0, std::max (0, fit - 2)) + "..";
        PixelFont::drawLeft (g, "E.G. " + t, infoArea, px, Theme::ink);
    }
}

void CommandPicker::resized()
{
    const float px = Theme::px;
    auto r = getLocalBounds().reduced ((int) (px * 2));
    r.removeFromTop ((int) (PixelFont::textHeight (px) + px * 2));
    const int rowH = (int) (px * 11);
    auto bottom = r.removeFromBottom (rowH);
    cancelBtn.setBounds (bottom.removeFromRight ((int) (px * 6 * 12 + px * 4)));
    bottom.removeFromRight ((int) px * 2);
    infoArea = bottom.toFloat();
    r.removeFromBottom ((int) px);
    list.setBounds (r);
}

bool CommandPicker::keyPressed (const juce::KeyPress& k)
{
    const auto& cmds = commandList();
    const int n = (int) cmds.size();
    const int code = k.getKeyCode();
    if (code == juce::KeyPress::escapeKey) { if (onCancel) onCancel(); return true; }
    if (code == juce::KeyPress::returnKey) { pick (list.selected); return true; }
    if (code == juce::KeyPress::upKey || code == juce::KeyPress::downKey)
    {
        list.selected = juce::jlimit (0, n - 1, list.selected + (code == juce::KeyPress::upKey ? -1 : 1));
        list.ensureVisible (list.selected);
        repaint();
        return true;
    }
    // typing a command's letter or digit jumps to it (E again steps through the E commands)
    const auto ch = juce::CharacterFunctions::toUpperCase (k.getTextCharacter());
    if ((ch >= '0' && ch <= '9') || (ch >= 'A' && ch <= 'Z'))
    {
        for (int step = 1; step <= n; ++step)
        {
            const int i = (list.selected + step) % n;
            if (cmds[(size_t) i].code[0] == (char) ch) { list.selected = i; list.ensureVisible (i); repaint(); break; }
        }
        return true;
    }
    return true;   // (the menu keeps the keyboard while it's open)
}
} // namespace prv::ui
