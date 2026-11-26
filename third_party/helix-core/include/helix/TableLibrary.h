// The wavetable browser's back end (non-real-time).
//
// Entries: the built-in tables first, then everything found under the user folders (Wavetables/): WAV files
// (Serum tables or single cycles) and sub-folders of single cycles. Scanning and loading run on one worker
// thread; a finished table is handed to the Engine (lock-free) by Engine::setTable.
//
// Every entry has a stable id string, which presets store:
//   "builtin:<name>"  |  "file:<path relative to its root>"  |  "folder:<path relative to its root>"
//   | "path:<absolute path>" (loaded directly) | "sample:<absolute path>#<frames>" (Sample -> Table)
#pragma once
#include "helix/Engine.h"
#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace helix {

struct TableEntry
{
    std::string id;
    std::string name;       // shown in the browser
    std::string path;       // absolute path, empty for built-ins
    int builtin = -1;       // BuiltinTable index, or -1
    bool folder = false;
    std::string group;      // where it lives, for the browser: "" built-in, else "<root>/<sub folder>"
};

constexpr int kMaxTables = 2048;   // the table parameters' range (0.5.3: was 512)

class TableLibrary
{
public:
    explicit TableLibrary(Engine& engine);
    ~TableLibrary();

    // Folders to scan (any number). Call rescan() after changing them. The first is HELIX's own Wavetables folder
    // (ids "file:<path in it>"); tables in the others get ids "file:<folder name>/<path in it>", so two folders
    // can hold files of the same name.
    void setRoots(std::vector<std::string> roots);
    void rescan();                                  // asynchronous
    bool scanning() const { return scanning_.load(); }
    unsigned scanSerial() const { return scanSerial_.load(); }   // goes up after every finished scan

    // Snapshot of the entries (thread-safe copy).
    std::vector<TableEntry> entries() const;
    int count() const;
    std::string nameAt(int index) const;            // "" when out of range
    int indexOfId(const std::string& id) const;     // -1 when unknown
    // The table parameter wraps: index i means entry (i mod count), so every knob position is a table.
    int wrap(int index) const;
    std::string idAt(int index) const;              // id of entry wrap(index)
    // True for slot ids that name a library entry (builtin:, file:, folder:), false for path: / sample:
    static bool isLibraryId(const std::string& id);

    // Load entry `index` into an engine slot, asynchronously. The slot's id updates when the table is live.
    void requestLoad(int slot, int index);
    void requestLoadId(int slot, const std::string& id);
    // Load a WAV file straight from a path (drag & drop, Sample -> Table). frames > 0 forces Sample -> Table.
    void requestLoadFile(int slot, const std::string& path, int sampleToTableFrames = 0);

    std::string slotId(int slot) const;             // id of the table last loaded into a slot
    // Blocks until all queued work is done (tests, offline rendering).
    void waitIdle();
    // Called (on the worker thread) after each successful load.
    void setOnLoaded(std::function<void(int slot, const std::string& id)> cb);
    std::string lastError() const;

private:
    struct Job
    {
        enum Kind { Scan, LoadIndex, LoadId, LoadFile } kind = Scan;
        int slot = 0, index = 0;
        std::string text;
        int frames = 0;
        Job() = default;
        Job(Kind k, int s = 0, int i = 0, std::string t = {}, int f = 0) : kind(k), slot(s), index(i), text(std::move(t)), frames(f) {}
    };
    void worker();
    void doScan();
    void doScanImpl();
    bool doLoad(int slot, const TableEntry& e);
    void push(Job j);

    Engine& engine_;
    mutable std::mutex mutex_;
    std::condition_variable cv_, idleCv_;
    std::deque<Job> jobs_;
    bool busy_ = false, quit_ = false;
    std::vector<std::string> roots_;
    std::atomic<unsigned> scanSerial_{0};
    std::vector<TableEntry> entries_;
    std::string slotId_[kNumTableSlots];
    std::string lastError_;
    std::function<void(int, const std::string&)> onLoaded_;
    std::atomic<bool> scanning_{false};
    std::thread thread_;
};

} // namespace helix
