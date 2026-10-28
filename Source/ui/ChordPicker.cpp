// Paula's Revenge - right-click chord list for the pattern
#include "ChordPicker.h"

namespace prv::ui
{
ChordPicker::ChordPicker()
{
    setWantsKeyboardFocus (true);
    list.getNumRows = [this] { return rows(); };
    list.getRowText = [this] (int r) { return rowText (r); };
    list.isRowDim = [this] (int r) { return r >= (int) slotRows.size(); };
    list.onClick = [this] (int r, const juce::MouseEvent&) { pick (r); };
    list.onHover = [this] (int r) { if (r >= 0 && r != list.selected) { list.selected = r; list.repaint(); } };
    addAndMakeVisible (list);
    editBtn.onClick = [this] { if (onEdit) onEdit(); };
    cancelBtn.onClick = [this] { if (onCancel) onCancel(); };
    addAndMakeVisible (editBtn);
    addAndMakeVisible (cancelBtn);
}

void ChordPicker::open (const ChordTable& table, int currentSlot)
{
    slotRows.clear();
    texts.clear();
    for (int i = 1; i < kChordSlots; ++i)
    {
        const auto& d = table[(size_t) i];
        if (d.empty()) continue;
        slotRows.push_back (i);
        texts.push_back (juce::String::toHexString (i).toUpperCase().paddedLeft ('0', 2) + "  " + juce::String (d.name).paddedRight (' ', 16) + juce::String (notesText (d)));
    }
    list.selected = 0;
    for (int r = 0; r < (int) slotRows.size(); ++r) if (slotRows[(size_t) r] == currentSlot) list.selected = r;
    typed = {};
    setVisible (true);
    toFront (true);
    list.ensureVisible (list.selected);
    grabKeyboardFocus();
    repaint();
}

juce::String ChordPicker::rowText (int row) const
{
    if (row >= 0 && row < (int) texts.size()) return texts[(size_t) row];
    if (row == (int) texts.size()) return "    EDIT CHORDS... (THE TABLE, THE GLOBAL SET, THE DATABASE)";
    return "    EFFECT COMMANDS... (THE USUAL LIST)";
}

void ChordPicker::pick (int row)
{
    if (row >= 0 && row < (int) slotRows.size()) { if (onPick) onPick (slotRows[(size_t) row]); }
    else if (row == (int) slotRows.size()) { if (onEdit) onEdit(); }
    else if (row == (int) slotRows.size() + 1) { if (onCommands) onCommands(); }
}

void ChordPicker::paint (juce::Graphics& g)
{
    Theme::panelBox (g, getLocalBounds().toFloat(), "CHORD - PICK A SLOT (0XY PLAYS SLOT XY)");
}

void ChordPicker::resized()
{
    const float px = Theme::px;
    auto r = getLocalBounds().reduced ((int) (px * 2));
    r.removeFromTop ((int) (PixelFont::textHeight (px) + px * 2));
    const int rowH = (int) (px * 11);
    auto bottom = r.removeFromBottom (rowH);
    cancelBtn.setBounds (bottom.removeFromRight ((int) (px * 6 * 12 + px * 4)));
    bottom.removeFromRight ((int) px * 2);
    editBtn.setBounds (bottom.removeFromRight ((int) (px * 6 * 14 + px * 4)));
    r.removeFromBottom ((int) px);
    list.setBounds (r);
}

bool ChordPicker::keyPressed (const juce::KeyPress& k)
{
    const int n = rows();
    const int code = k.getKeyCode();
    if (code == juce::KeyPress::escapeKey) { if (onCancel) onCancel(); return true; }
    if (code == juce::KeyPress::returnKey) { pick (list.selected); return true; }
    if (code == juce::KeyPress::upKey || code == juce::KeyPress::downKey)
    {
        list.selected = juce::jlimit (0, n - 1, list.selected + (code == juce::KeyPress::upKey ? -1 : 1));
        list.ensureVisible (list.selected);
        list.repaint();
        return true;
    }
    // two hex digits jump to that slot
    const auto ch = juce::CharacterFunctions::toUpperCase (k.getTextCharacter());
    if ((ch >= '0' && ch <= '9') || (ch >= 'A' && ch <= 'F'))
    {
        typed = (typed + juce::String::charToString (ch)).getLastCharacters (2);
        const int want = typed.getHexValue32();
        for (int r = 0; r < (int) slotRows.size(); ++r)
            if (slotRows[(size_t) r] == want || (typed.length() == 1 && (slotRows[(size_t) r] >> 4) == want))
            { list.selected = r; list.ensureVisible (r); list.repaint(); break; }
        return true;
    }
    return true;   // (the list keeps the keyboard while it's open)
}
} // namespace prv::ui
