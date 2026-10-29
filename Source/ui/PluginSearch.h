// Paula's Revenge - finding VST3 / AU plug-ins: type to search, and favourites (starred, listed first,
// or on their own). Used by a plug-in instrument's list and the mixer's effect plug-in list.
#pragma once

#include <JuceHeader.h>
#include "../PluginHost.h"
#include "../Prefs.h"
#include "Widgets.h"

namespace prv::ui
{
class PluginSearchBar : public juce::Component
{
public:
    PluginSearchBar()
    {
        favBtn.onClick = [this] { if (selected) if (auto* d = selected()) { toggleFavourite (*d); if (onChanged) onChanged(); } };
        onlyBtn.onClick = [this] { favouritesOnly = onlyBtn.getToggleState(); if (onChanged) onChanged(); };
        addAndMakeVisible (favBtn);
        addAndMakeVisible (onlyBtn);
        setInterceptsMouseClicks (false, true);
    }

    juce::String query;
    bool favouritesOnly = false;
    std::function<void()> onChanged;                                  // the search or the favourites changed: list again
    std::function<const juce::PluginDescription*()> selected;         // the highlighted plug-in (for FAVOURITE)

    // ---- favourites (remembered on this computer)
    static juce::StringArray favourites() { return juce::StringArray::fromLines (prefs().getValue ("pluginFavourites")); }
    static bool isFavourite (const juce::PluginDescription& d) { return favourites().contains (juce::String (PluginHost::keyFor (d))); }
    static void toggleFavourite (const juce::PluginDescription& d)
    {
        auto f = favourites();
        const juce::String k (PluginHost::keyFor (d));
        if (f.contains (k)) f.removeString (k); else f.add (k);
        f.removeEmptyStrings();
        prefs().setValue ("pluginFavourites", f.joinIntoString ("\n"));
        prefs().saveIfNeeded();
    }

    // what to list: every word of the search in its name, maker or format; favourites first (or only)
    std::vector<juce::PluginDescription> filter (const std::vector<juce::PluginDescription>& all) const
    {
        const auto words = juce::StringArray::fromTokens (query.toLowerCase(), " ", "");
        const auto favs = favourites();
        std::vector<juce::PluginDescription> starred, rest;
        for (const auto& d : all)
        {
            const auto hay = (d.name + " " + d.manufacturerName + " " + d.pluginFormatName + (d.pluginFormatName == "AudioUnit" ? " au" : "")).toLowerCase();
            bool ok = true;
            for (auto& w : words) if (w.isNotEmpty() && ! hay.contains (w)) ok = false;
            if (! ok) continue;
            const bool fav = favs.contains (juce::String (PluginHost::keyFor (d)));
            if (fav) starred.push_back (d);
            else if (! favouritesOnly) rest.push_back (d);
        }
        starred.insert (starred.end(), rest.begin(), rest.end());
        return starred;
    }
    static juce::String rowText (const juce::PluginDescription& d)
    {
        return juce::String (isFavourite (d) ? "* " : "  ") + d.name.toUpperCase() + "  (" + d.manufacturerName.toUpperCase() + ", "
             + (d.pluginFormatName == "AudioUnit" ? juce::String ("AU") : d.pluginFormatName.toUpperCase()) + ")";
    }

    // typing: letters, digits and - . _ go into the search; Space too once it has started;
    // Backspace takes one off; Esc clears it (false: the key wasn't for the search)
    bool keyPressed (const juce::KeyPress& k) override
    {
        const auto m = k.getModifiers();
        if (m.isCommandDown() || m.isCtrlDown() || m.isAltDown()) return false;
        const int code = k.getKeyCode();
        if (code == juce::KeyPress::backspaceKey) { if (query.isEmpty()) return false; setQuery (query.dropLastCharacters (1)); return true; }
        if (code == juce::KeyPress::escapeKey) { if (query.isEmpty()) return false; setQuery ({}); return true; }
        const auto c = k.getTextCharacter();
        if (c == ' ' && query.isEmpty()) return false;
        if (juce::CharacterFunctions::isLetterOrDigit (c) || c == ' ' || c == '-' || c == '.' || c == '_')
        {
            if (query.length() < 30) setQuery (query + juce::String::charToString (c));
            return true;
        }
        return false;
    }
    void setQuery (const juce::String& q) { query = q.toUpperCase(); repaint(); if (onChanged) onChanged(); }
    void setFavouritesOnly (bool on) { favouritesOnly = on; onlyBtn.setToggleState (on, juce::dontSendNotification); if (onChanged) onChanged(); }

    void paint (juce::Graphics& g) override
    {
        const float px = Theme::px;
        const auto& pal = Theme::palFor (*this);
        g.setColour (Theme::skin.bg);
        g.fillRect (textArea);
        Theme::bevel (g, textArea, false, std::max (1.0f, px * 0.5f), pal);
        const auto inner = textArea.reduced (px * 2, 0);
        if (query.isEmpty()) PixelFont::drawLeft (g, "TYPE TO SEARCH", inner, px, Theme::skin.dim);
        else
        {
            juce::String t = "SEARCH: " + query + "_";
            const int n = std::max (4, (int) (inner.getWidth() / (px * PixelFont::kCellW)));
            if (t.length() > n) t = t.substring (t.length() - n);
            PixelFont::drawLeft (g, t, inner, px, Theme::skin.text);
        }
    }
    void resized() override
    {
        const float px = Theme::px;
        const int gap = (int) std::max (2.0f, px * 1.5f);
        auto charsW = [px] (int chars) { return (int) (px * 6 * chars + px * 4); };
        auto r = getLocalBounds();
        onlyBtn.setBounds (r.removeFromRight (charsW (15))); r.removeFromRight (gap);
        favBtn.setBounds (r.removeFromRight (charsW (9))); r.removeFromRight (gap);
        textArea = r.toFloat();
    }

    PixButton favBtn { "FAVOURITE" }, onlyBtn { "FAVOURITES ONLY", true };

private:
    juce::Rectangle<float> textArea;
};
} // namespace prv::ui
