#pragma once
/*
    Session — CHOPPAGE without a host: the 8 slots, loading, slicing, badges,
    slice settings, patterns, history, memories, the break browser, Akaize
    render, bounce, and save / restore. Owns the audio Engine.

    Threads
      * audio:   process() only (lock-free, never waits)
      * control: everything else, from any non-audio thread (UI, MPC param
                 changes, the MPC worker). Guarded by one mutex.
      * worker:  decoding and analysis. Results are installed by poll(), which
                 the host calls regularly from a control thread (desktop: a UI
                 timer; MPC: its worker loop).
*/
#include "choppage/Engine.h"
#include "choppage/AudioFile.h"
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <optional>
#include <thread>

namespace bc
{
    class Session
    {
    public:
        enum class Status { empty, loading, ready, missing, unreadable };
        static const char* statusText (Status);

        struct Slot
        {
            Status status = Status::empty;
            std::string file;               // "" for the built-in demo / renders kept in memory
            bool demo = false;
            ContentPtr content;
            SlotProgram program;             // what plays (pattern = current or original)
            Pattern current, original;
            std::vector<Pattern> history;
            int historyPos = -1;
            bool showOriginal = false;
            std::array<std::optional<Pattern>, kMemories> memories;
            bool gridSlicing = false;
            float sensitivity = 0.62f;
            int gridPerBar = 16;
            int muteGroup = 0;                 // 0-3 (kept when another file loads)
            double loopBpm = 0.0;              // AKAIZE SYNC: hand-set loop tempo (0 = the break's own)
            uint32_t version = 0;
        };

        struct BrowserEntry { std::string path, name; double bpm = 0, seconds = 0; int bars = 0; bool isDir = false; bool isUp = false; };

        enum class SliceField { pitch, gate, stretch, reverse, stutter, trans, xfade, level };

        using Decoder = std::function<std::shared_ptr<SampleData> (const std::string& path, AudioFile::Status&)>;

        Session();
        ~Session();

        // ---- setup ------------------------------------------------------------------------------
        void prepare (double sampleRate, int maxBlock);
        /** Desktop: decode with JUCE (FLAC, MP3...). Default: AudioFile::load (WAV / AIFF). */
        void setDecoder (Decoder d);
        /** Where Akaize renders and bounces are written. */
        void setDataFolder (const std::string& folder);
        const std::string& getDataFolder() const { return dataFolder; }

        // ---- audio thread ------------------------------------------------------------------------
        void process (float* left, float* right, int n, const MidiMsg* midi, int numMidi,
                      const Engine::Host&, const Engine::Settings&) noexcept;
        Engine& engine() noexcept { return eng; }

        // ---- control side --------------------------------------------------------------------------
        /** Installs finished loads, frees unused audio, handles pattern-memory notes. Call often. */
        void poll();
        bool isBusy() const;
        void waitUntilIdle (int timeoutMs = 10000);

        const Slot& slot (int i) const { return slots[(size_t) clampv (i, 0, kSlots - 1)]; }
        int  selectedSlot() const noexcept { return eng.selectedSlot.load(); }
        void selectSlot (int);
        int  selectedSlice() const noexcept { return selSlice; }
        void selectSlice (int, bool audition = true);
        bool editingAll() const noexcept { return editAll; }
        void setEditingAll (bool b) { editAll = b; ++uiVersion; }
        uint32_t version() const noexcept { return uiVersion.load(); }

        void loadFile (int slot, const std::string& path);
        void loadDemo (int slot);
        void clearSlot (int slot);
        void reslice (int slot, bool grid, float sensitivity, int gridPerBar);
        /** Slicing settings kept for the slot's next load (no reslice now). */
        void setSlicing (int slot, bool grid, float sensitivity, int gridPerBar);

        float getSliceValue (SliceField, bool all) const;
        void  setSliceValue (SliceField, float, bool all);
        void  toggleMute (int slice);
        void  setBadge (int slice, Badge);
        void  moveMarker (int marker, int position);
        void  addMarker (int position);
        void  deleteMarker (int marker);
        /** The start of the drum hit nearest `position` within ±radius samples (where the level jumps most, moved
            back to the zero crossing just before it), in the selected break. position itself if there is none. */
        int   snapToHit (int position, int radius) const;

        void rollPattern (Style, float amount, bool layers);
        void rollFx (float amount);
        void historyBack();
        void historyForward();
        void setShowOriginal (bool);
        void setPatternLength (int bars);
        void toggleStep (int step, Badge lane);
        void setStepSlice (int step, int slice);     // -1 = rest (MPC step knobs)
        void swapStep (int step);
        void toggleLock (int step);
        void setFxStep (int step, FxType, int rate);
        void cycleFxRate (int step);
        void saveMemory (int memory);
        void recallMemory (int memory);
        void saveOriginalToMemory (int memory);   // the break as recorded, no FX
        void clearMemory (int memory);
        void clearPattern();
        void resetToOriginal();      // pattern = the break as recorded, FX lane empty (undoable)

        // ---- break browser ----------------------------------------------------------------------------
        /** Scans `folder` (and its sub-folders, 3 deep) on the worker; entries sorted by BPM then name. */
        /** Browser audition: decodes the file here (the caller's thread, never the audio thread) and loops it dry
            until another file, auditionStop() or a load. Returns false if the file can't be read. */
        /** Mute group of a break (0-3): breaks in the same group cut each other, different groups play together.
            It belongs to the slot, so loading another file keeps it. */
        void setMuteGroup (int slot, int group);
        int  muteGroup (int slot) const;

        bool audition (const std::string& path);
        void auditionStop();
        std::string auditioning() const { return auditionPath; }
        void scanFolder (const std::string& folder);
        // One folder only, for browsing: ".." first (unless folder == top), then its sub-folders (A-Z), then its breaks.
        void listFolder (const std::string& folder, const std::string& top);
        std::vector<BrowserEntry> browserEntries() const;
        std::string browserFolder() const;
        bool browserScanning() const { return scanning.load(); }

        // ---- renders -------------------------------------------------------------------------------------
        /** AKAIZE RENDER: Akaizer (S-series cyclic) stretch of the slot's break to `targetBpm`
            (or by `stretchPercent` when targetBpm <= 0), cycle length in samples. The result replaces
            the slot's audio, slices scaled, badges kept, and is written to the data folder. */
        /** AKAIZE render settings: Akaizer v1.2's, as Twisted-Timestretch sets them. */
        struct AkaizeRender
        {
            bool   sync = true;            // stretch = loop BPM / target BPM (else stretchPercent)
            double targetBpm = 170.0;      // the host's tempo (or TARGET BPM with no host)
            double stretchPercent = 100.0; // 25 .. 2000
            int    cycleLength = 800;      // 20 .. 2000 samples
            int    cycleFreq = 51;         // 20 .. 2050 cps (the caller links it to the length unless MANUAL)
            double pitch = 0.0;            // semitones of repitch before the stretch, -24 .. 24
        };
        /** Renders the break the Akaizer way (16-bit, exactly as the original computes it), writes
            Renders/<name>_akaized.wav and loads it back into the slot (slices moved, settings and pattern kept). */
        bool akaizeSlot (int slot, const AkaizeRender&);
        bool akaizeSlot (int slot, double targetBpm, double stretchPercent, int cycleLength);   // (older form)
        /** LOOP BPM of a break for SYNC: set by hand, or 0 for the tempo found when it loaded. */
        void   setLoopBpm (int slot, double bpm);
        double loopBpm (int slot) const;      // what SYNC uses (the hand-set one, else the break's own)
        bool   loopBpmSet (int slot) const;
        /** One pass of the selected break's pattern, rendered with these settings. */
        std::shared_ptr<SampleData> bounce (const Engine::Settings&, double bpm, double sampleRate);

        // ---- state ---------------------------------------------------------------------------------------
        std::string saveState() const;
        void loadState (const std::string&);

        // UI pads / transport (atomics on the engine)
        void setPlaying (bool b) { eng.uiPlay = b; }
        bool isPlaying() const { return eng.playingSlot.load() >= 0; }
        void setPadHeld (int pad, bool held) { if (pad >= 0 && pad < kFxPads) eng.uiPad[(size_t) pad] = held; }

    private:
        std::vector<SamplePtr> auditionKeep;   // buffers the audio thread may still hold
        std::string auditionPath;
        struct Job { int slot; std::string path; std::optional<std::string> restore; bool grid; float sens; int gpb; };
        struct Done { int slot; std::string path; ContentPtr content; AudioFile::Status status; std::optional<std::string> restore; };

        void workerLoop();
        void installContent (int slot, ContentPtr, const std::string& path, bool demo, const std::string* restore);
        void publish (int slot);
        void changed (int slot);
        void pushHistory (Slot&, const Pattern&);
        ContentPtr withBadges (const SlotContent&, const std::array<Badge, kMaxSlices>&) const;
        ContentPtr withSlices (const SlotContent&, const SliceTable&) const;
        void keep (const ContentPtr& c) { keepAlive.push_back (c); }

        Engine eng;
        std::array<Slot, kSlots> slots;
        std::vector<ContentPtr> keepAlive;
        mutable std::recursive_mutex mutex;
        int selSlice = 0;
        bool editAll = false;
        std::atomic<uint32_t> uiVersion { 1 };
        std::string dataFolder;
        double sr = 44100.0;

        // worker
        Decoder decoder;
        std::thread worker;
        std::mutex qMutex;
        std::condition_variable qCv;
        std::deque<Job> jobs;
        std::deque<Done> done;
        struct ScanJob { std::string folder, top; bool flat = false; };
        std::deque<ScanJob> scanJobs;
        bool quit = false;
        std::atomic<int> pending { 0 };
        std::atomic<bool> scanning { false };
        std::vector<BrowserEntry> browser;
        std::string browserDir;
    };

    /** The analysis that turns a decoded break into slot content (used by Session and the tests). */
    ContentPtr analyseBreak (const SamplePtr& buf, bool grid, float sensitivity, int gridPerBar, const std::string* restore,
                             bool fileSlices = true);   // a REX file's own slice points when it has them
}
