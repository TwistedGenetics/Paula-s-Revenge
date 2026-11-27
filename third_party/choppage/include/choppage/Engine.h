#pragma once
/*
    Engine — the audio-thread half of CHOPPAGE.

    Mute groups: each of the 8 breaks is in one of 4 groups (all in group 1 by default). One break per group
    plays at a time: a break note in a group that is playing hands over to the new break, which carries on
    from the same point in the bar (BAR SYNC) or restarts (RESTART). Breaks in different groups play together.
    The message thread publishes each slot's audio
    (SlotContent, immutable) and its program (slice settings + pattern, POD)
    under a spin lock; the audio thread picks them up with a try-lock at the
    top of a block, so it never waits and never allocates.

    Notes (C1 = 36):  C1-G1 break slots · G#1-D#2 FX pads (held) ·
                      E2-G3 slices 1-16 of the playing break · G#3-B4 pattern memories

    SYNC:  the pattern's steps are clocked from the host's song position
           (or an internal clock when the host is stopped and a break is latched). A break plays only once its
           key triggers it. GATE: LATCH (until the same key again, another break in its group or the host
           stopping), HOLD (while the key is held), ONE SHOT (one note plays the pattern to its end, then stops).
    MIDI:  no sequencing; slice notes play slices, break notes pick the break.

    Work is done in 64-sample chunks: MIDI and step triggers are sample-exact
    inside a chunk; FX pads switch at chunk edges (1.5 ms at 44.1 kHz).
*/
#include "choppage/Model.h"
#include <atomic>
#include <limits>
#include "choppage/SliceVoice.h"
#include "choppage/PerfFx.h"
#include "choppage/SoundChain.h"

namespace bc
{
    /** A MIDI message at a sample offset in the block. */
    struct MidiMsg
    {
        int offset = 0;
        uint8_t data[3] { 0, 0, 0 };
        int status() const noexcept { return data[0] & 0xF0; }
        bool isNoteOn() const noexcept  { return status() == 0x90 && data[2] > 0; }
        bool isNoteOff() const noexcept { return status() == 0x80 || (status() == 0x90 && data[2] == 0); }
        bool isAllOff() const noexcept  { return status() == 0xB0 && (data[1] == 120 || data[1] == 123); }
        static MidiMsg noteOn (int note, int vel, int offset = 0) { MidiMsg m; m.offset = offset; m.data[0] = 0x90; m.data[1] = (uint8_t) note; m.data[2] = (uint8_t) vel; return m; }
        static MidiMsg noteOff (int note, int offset = 0) { MidiMsg m; m.offset = offset; m.data[0] = 0x80; m.data[1] = (uint8_t) note; return m; }
    };

    /** Tiny spin lock: the message side spins, the audio side only ever try-locks. */
    class SpinLock
    {
    public:
        void lock() noexcept { while (flag.test_and_set (std::memory_order_acquire)) {} }
        bool tryLock() noexcept { return ! flag.test_and_set (std::memory_order_acquire); }
        void unlock() noexcept { flag.clear (std::memory_order_release); }
    private:
        std::atomic_flag flag = ATOMIC_FLAG_INIT;
    };

    class Engine
    {
    public:
        struct Host
        {
            bool   playing = false;
            bool   hasInfo = false;
            double bpm = 120.0;
            double ppq = 0.0;     // at the first sample of the block
        };

        struct Settings
        {
            bool   midiTrigger = false;     // TRIGGER: SYNC (false) / MIDI
            int    breakMode = 1;           // 0 RESTART, 1 BAR SYNC, 2 QUANT
            int    gateMode = 0;            // GATE: 0 LATCH, 1 HOLD, 2 ONE SHOT
            int    noteMap = 0;             // 0 CHROMATIC (C1 = break 1 ...), 1 C MAJOR (see noteUse)
            bool   velToSlice = false;
            double speed = 1.0;
            bool   fit = true;
            bool   keepPitch = false;       // FIT: REPITCH / KEEP
            double tempoSourceMul = 1.0;    // AUTO 1, /2 0.5, X2 2
            float  swing = 0.5f;
            int    slCycleLength = 800;     // per-slice STRETCH: Akaizer CYCLE LENGTH (samples) ...
            double slCycleFreq = 51.0;      // ... and CYCLE FREQ (grains per second)
            int    halfMode = 0;            // 0.17 HALF-TIME MODE: 1/2, 3/4, 2/3, 1/4, 1/2 TAPE (also pitched down an octave)
            bool   fill = false;            // 0.17: FILL on (the trig conditions FILL / NOT FILL)
            PerfFx::Settings pads;          // samplesPerBeat filled in here
            SoundChain::Settings sound;
        };

        void prepare (double sampleRate, int maxBlock);
        void reset();

        // ---- message thread --------------------------------------------------------
        void publishContent (int slot, ContentPtr);
        void publishProgram (int slot, const SlotProgram&);
        // Audition a file from the browser: loops it, dry, at its own speed, until replaced or cleared (nullptr).
        // The caller keeps the buffer alive until the audio thread has let go of it (Session does).
        void publishAudition (SamplePtr);

        // ---- audio thread --------------------------------------------------------------
        /** Renders n samples into left / right (overwrites). MIDI sorted by offset. */
        void process (float* left, float* right, int n, const MidiMsg* midi, int numMidi, const Host&, const Settings&) noexcept;

        // ---- shared with the UI (atomics) ------------------------------------------------
        std::atomic<int>  selectedSlot { 0 };     // UI / last break note
        std::atomic<int>  playingSlot  { -1 };    // the break played most recently that is still playing; -1 = silent
        std::atomic<int>  currentStep  { -1 };    // ... its step and slice
        std::atomic<int>  currentSlice { -1 };
        std::array<std::atomic<int>, kSlots> slotStep {};    // per break: the step playing, -1 when silent
        std::array<std::atomic<int>, kSlots> slotSlice {};   // per break: the slice playing, -1 when silent
        std::atomic<bool> uiPlay { false };       // the ▶ button (internal clock when the host is stopped)
        std::array<std::atomic<bool>, kFxPads> uiPad {};   // UI / automation pad holds
        std::array<std::atomic<bool>, kFxPads> padLit {};  // pad audible (for the lamps)
        std::array<std::atomic<bool>, kFxPads> padActive {};   // the pad's effect is on (after PAD QUANTISE)
        std::atomic<int>  previewSlice { -1 };    // UI click on a slice
        std::atomic<int>  sliceHit { -1 };        // a slice pad was played: slot x kMaxSlices + slice (the MPC selects it)
        std::atomic<int>  memoryRequest { -1 };   // slot * 16 + memory, for the message thread
        std::atomic<int>  lastNote { -1 };
        float getPeak() noexcept { return chain.getPeak(); }

    private:
        static constexpr int kChunk = 64;

        void pullPublished() noexcept;
        void handleMidi (const MidiMsg&, const Settings&) noexcept;
        struct Player;
        void triggerStep (Player&, int64_t globalStep, int atSample, const Settings&, double samplesPerStep) noexcept;
        bool condition (Player&, int cond, bool fxRow, int64_t cycle, const Settings&) noexcept;   // a trig condition true now?
        void playSlice (Player&, int slice, int atSample, int gateSamples, const Settings&) noexcept;
        int  groupOf (int slot) const noexcept { return clampv (program[(size_t) clampv (slot, 0, kSlots - 1)].muteGroup, 0, kMuteGroups - 1); }
        void startStopFromHost (const Host&) noexcept;
        void renderVoices (int from, int to) noexcept;

        double sr = 44100.0;
        int maxBlock = 512;

        // published by the message thread
        SpinLock lock;
        std::array<ContentPtr, kSlots> pendingContent;
        std::array<SlotProgram, kSlots> pendingProgram {};
        std::atomic<uint32_t> pendingVersion { 0 };
        uint32_t seenVersion = 0;

        SamplePtr pendingAudition;
        std::atomic<uint32_t> auditionVersion { 0 };
        uint32_t auditionSeen = 0;
        SamplePtr audition;          // audio thread's copy
        double auditionPos = 0.0;
        float auditionFade = 0.0f;   // 0..1, a short fade in so a new file doesn't click

        // audio thread's copies
        std::array<ContentPtr, kSlots> content;
        std::array<SlotProgram, kSlots> program {};

        // sequencer: one player per mute group
        struct Player
        {
            int    activeSlot = 0;
            int    pendingSlot = -1;          // QUANT waits for the next beat
            bool   latched = false;           // a break note turned it on
            int    heldBreakNote = -1;
            bool   oneShot = false, oneShotArm = false;   // ONE SHOT: stop at oneShotEnd (worked out on the next step)
            int64_t oneShotEnd = std::numeric_limits<int64_t>::max();
            double stepPos = 0.0;             // in 16ths since the pattern's step 0
            double syncOffset = 0.0;          // stepPos - host position in 16ths (0 when on the bar grid)
            int64_t lastStepGlobal = std::numeric_limits<int64_t>::min();
            bool   wasRunning = false, restartNow = false, restartFromNote = false;
            int    startStepOffset = 0;
            int    step = -1, slice = -1;
            // trig conditions: the first time round (1ST), and the last result on each row (PRE / NEI): -1 none yet
            int64_t firstCycle = std::numeric_limits<int64_t>::min();
            int8_t lastNoteResult = -1, lastFxResult = -1;
            // voices: the new slice fades in while the old one fades out
            std::array<SliceVoice, 3> voices;
            int nextVoice = 0;
        };
        std::array<Player, kMuteGroups> players;
        int    lastPlayer = 0;                // the group played most recently (slice / memory notes, the clock)
        double internalPpq = 0.0;
        bool   wasHostPlaying = false;
        double lastHostBpm = 120.0;
        int    renderedUpTo = 0;
        int    pendingSliceNote = -1;
        float mix[2][kChunk] {};

        // FX
        std::array<bool, kFxPads> midiPad {};
        std::array<int, kFxPads> padOverride { -1, -1, -1, -1, -1, -1, -1, -1 };   // CC 103+: a setting for this press (-1: the knob)
        std::array<int, kFxPads> autoPadLeft {};   // FX lane events, samples left
        // PAD QUANTISE: a press waits for the next grid line, a release (or a short tap) ends on the one after
        std::array<bool, kFxPads> padWas {}, padOn {}, padArmed {}, padStopArmed {}, padTap {};
        int autoStutterDiv = 16, autoRollDiv = 32;
        bool halfTape = false;          // HALF-TIME held in 1/2 TAPE: new slices an octave down
        uint32_t condRandom = 0x9E3779B9u;
        PerfFx perf;
        SoundChain chain;
    };
}
