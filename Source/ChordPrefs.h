// Paula's Revenge - the global chord set: kept on this computer, copied into every new song
#pragma once

#include <JuceHeader.h>
#include "Prefs.h"
#include "engine/Chords.h"

namespace prv
{
inline juce::String encodeChords (const ChordTable& t)
{
    juce::MemoryOutputStream o;
    o.writeByte (1);   // (format)
    for (int i = 1; i < kChordSlots; ++i)
    {
        const auto& d = t[(size_t) i];
        if (d.empty()) continue;
        o.writeByte ((char) i);
        o.writeByte ((char) std::min ((int) d.name.size(), 255));
        o.write (d.name.data(), std::min (d.name.size(), (size_t) 255));
        o.writeByte ((char) d.count);
        for (int k = 0; k < d.count; ++k) o.writeByte ((char) d.notes[(size_t) k]);
    }
    return o.getMemoryBlock().toBase64Encoding();
}

inline bool decodeChords (const juce::String& text, ChordTable& out)
{
    juce::MemoryBlock mb;
    if (text.isEmpty() || ! mb.fromBase64Encoding (text) || mb.getSize() < 1) return false;
    juce::MemoryInputStream in (mb, false);
    if (in.readByte() != 1) return false;
    ChordTable t;
    while (! in.isExhausted())
    {
        const int slot = (int) (uint8_t) in.readByte();
        const int len = (int) (uint8_t) in.readByte();
        if (slot <= 0 || in.getNumBytesRemaining() < len + 1) return false;
        std::string name ((size_t) len, ' ');
        in.read (name.data(), len);
        const int count = (int) (uint8_t) in.readByte();
        if (count > kMaxChordNotes || in.getNumBytesRemaining() < count) return false;
        auto& d = t[(size_t) slot];
        d.name = name; d.count = count;
        for (int k = 0; k < count; ++k) d.notes[(size_t) k] = (int8_t) in.readByte();
    }
    out = t;
    return true;
}

// the global set (the factory set until one is saved)
inline ChordTable loadGlobalChords()
{
    ChordTable t = defaultChordTable();
    decodeChords (prefs().getValue ("globalChords"), t);
    return t;
}

inline void saveGlobalChords (const ChordTable& t)
{
    prefs().setValue ("globalChords", encodeChords (t));
    prefs().saveIfNeeded();
}

// slot numbers as the pattern shows them: "03 0C 1F"
inline juce::String slotList (const std::vector<int>& slots, int most = 8)
{
    juce::StringArray s;
    for (int i : slots)
    {
        if (s.size() == most) { s.add ("+" + juce::String ((int) slots.size() - most) + " MORE"); break; }
        s.add (juce::String::toHexString (i).paddedLeft ('0', 2).toUpperCase());
    }
    return s.joinIntoString (" ");
}
} // namespace prv
