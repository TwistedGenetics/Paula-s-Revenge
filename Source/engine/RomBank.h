// Paula's Revenge - ROM's samples: up to four TONES, each a multisample - small looped samples spread across the
// keys by their root notes (a ROMpler's "ROM", from your own WAVs). Plain C++: the WAV reading is here too (with the
// 'smpl' chunk's root note and loop), so it's tested without JUCE.
#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace prv
{
constexpr int kRomTones = 4;

struct RomZone
{
    std::string name;              // (the file's, without its extension)
    int    tone = 0;               // 0 - 3
    int    root = 60;              // the MIDI note it plays at its own pitch
    float  rootCents = 0;          // (the smpl chunk's pitch fraction)
    int    lo = 0, hi = 127;       // the keys it plays (worked out from the roots: romSpread)
    double rate = 44100;           // its sample rate
    int    loopStart = -1, loopEnd = -1;   // inclusive; -1: plays once
    std::vector<int16_t> data;     // mono
    bool loops() const { return loopStart >= 0 && loopEnd > loopStart && loopEnd < (int) data.size(); }
};

struct RomBank
{
    std::vector<RomZone> zones;
    std::array<std::string, kRomTones> toneName {};   // ("WND ACORD")
    int zonesOf (int tone) const;
    size_t bytes() const;
};

// a tone's zones each take the keys nearest their root: from half-way to the one below, to half-way to the one above
void romSpread (RomBank&, int tone);
// the zone a key plays on a tone (nullptr: none)
const RomZone* romZoneFor (const RomBank&, int tone, int note);
// a WAV file (PCM 8/16/24/32-bit, float; any channels, mixed to mono) with its smpl chunk, if any
bool romReadWav (const uint8_t* data, size_t size, RomZone& out, std::string& error);
// (the root note and loop alone, for an ordinary sample: false if the file has no smpl chunk)
bool romReadSmpl (const uint8_t* data, size_t size, int& root, int& loopStart, int& loopEnd);
// a new bank: the old one with a tone's samples replaced (none: the tone emptied), spread across the keys
std::shared_ptr<const RomBank> romWithTone (const std::shared_ptr<const RomBank>& old, int tone, std::vector<RomZone> zones, const std::string& name);
// a sample library's names: "WND_XACORD0A" -> group "WND ACORD", category "WND"
std::string romGroupOf (const std::string& fileStem);
std::string romCategoryOf (const std::string& fileStem);
} // namespace prv
