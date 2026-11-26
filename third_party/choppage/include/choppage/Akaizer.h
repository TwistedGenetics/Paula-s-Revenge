/*
    Akaizer engine — a line-for-line port of the TIMESTRETCH routine in
    Ben Burchett's Akaizer v1.2 (Akaizer.exe, Visual Basic 6, 2006), recovered
    from the compiled machine code.

    Everything here is plain C++ (no JUCE) so it can be unit tested on its own
    and compared sample-for-sample with the browser port.

    The original, for reference (indices are 16-bit words, interleaved):

        L  = CycleLength * 2 * Channels               ' grain length
        C  = FpI4(Channels * SampleRate / CycleFreq)  ' output hop
        nOut = FpI4(nIn * Stretch / 100)
        S  = FpI4(nIn / (nOut / C))                   ' input hop
        For k = 0 To FpI4(nOut / C - 1)
            For j = 0 To L - 1
                If S*k + j > UBound(In)  Then Exit For
                If C*k + j > UBound(Out) Then Exit For
                g = Sin((j + 1) * 3.141592654 / L)
                v = FpI4(In(S*k + j) * g + Out(C*k + j))   ' clipped to 16-bit
                Out(C*k + j) = v
            Next
        Next

    FpI4 rounds half to even (the x87 default). In linked mode the frequency
    is FpI4(41000 / CycleLength) — 41000, not 44100, exactly as shipped.
*/
#pragma once

#include <cstdint>
#include <vector>

namespace akaizer
{
    /** VB6 __vbaFpI4 / CInt rounding: nearest, ties to even. */
    long long roundHalfEven (double x) noexcept;

    /** Cycle frequency the original uses when it is not set by hand. */
    int linkedFrequency (int cycleLength) noexcept;

    struct Settings
    {
        double stretchPercent = 100.0;  // 25 .. 2000 in the original UI
        int    cycleLength    = 800;    // 20 .. 2000 samples
        int    cycleFrequency = 51;     // 20 .. 2050 cycles per second
    };

    struct Result
    {
        std::vector<int16_t> samples;   // interleaved, same channel count as input
        long long grainWords = 0;       // L
        long long outHopWords = 0;      // C
        long long inHopWords = 0;       // S
    };

    /** The original algorithm. Input and output are interleaved 16-bit words. */
    Result stretch (const int16_t* interleaved, size_t numWords, int numChannels,
                    double sampleRate, const Settings& s);

    /** Varispeed repitch (pitch and speed together, like a sampler's tune
        knob), applied before stretching. Returns interleaved 16-bit. */
    std::vector<int16_t> repitch (const int16_t* interleaved, size_t numWords, int numChannels,
                                  double semitones);

    /** Full chain used by the plugin: optional repitch, then the original
        stretch. The final length is always (original length x stretch %),
        whatever the pitch, so "pitch up, stretch back to length" is one move. */
    Result render (const int16_t* interleaved, size_t numWords, int numChannels,
                   double sampleRate, const Settings& s, double semitones);

    /** Canonical 44-byte 16-bit PCM WAV, as Akaizer writes it. */
    std::vector<uint8_t> makeWav (const std::vector<int16_t>& interleaved, int numChannels, int sampleRate);
}
