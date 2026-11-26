#include "choppage/Session.h"
#include "choppage/Akaizer.h"
#include "choppage/Analysis.h"
#include "choppage/DemoBreak.h"
#include "choppage/Generator.h"
#include "choppage/TransientDetector.h"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>

namespace fs = std::filesystem;

namespace bc
{
    // =========================================================================================
    //  Text helpers for the saved state
    // =========================================================================================
    namespace
    {
        using KV = std::map<std::string, std::string>;

        KV parseBlock (const std::string& block)
        {
            KV kv;
            std::stringstream ss (block);
            std::string line;
            while (std::getline (ss, line))
            {
                if (! line.empty() && line.back() == '\r') line.pop_back();
                const auto sp = line.find (' ');
                if (sp == std::string::npos) kv[line] = "";
                else kv[line.substr (0, sp)] = line.substr (sp + 1);
            }
            return kv;
        }

        std::vector<std::string> split (const std::string& s, char sep)
        {
            std::vector<std::string> out;
            std::string cur;
            for (char c : s) { if (c == sep) { out.push_back (cur); cur.clear(); } else cur += c; }
            out.push_back (cur);
            return out;
        }

        std::string patternToString (const Pattern& p)
        {
            std::string a, f, l;
            for (int i = 0; i < p.numSteps; ++i)
            {
                if (i) { a += ','; f += ','; l += ','; }
                a += std::to_string (p.slice[(size_t) i]);
                f += std::to_string ((int) p.fx[(size_t) i].type) + ":" + std::to_string ((int) p.fx[(size_t) i].rate);
                l += p.locked[(size_t) i] ? '1' : '0';
            }
            return std::to_string (p.numSteps) + "|" + a + "|" + f + "|" + l;
        }

        std::optional<Pattern> patternFromString (const std::string& s)
        {
            auto parts = split (s, '|');
            if (parts.size() < 2) return std::nullopt;
            Pattern p;
            p.numSteps = clampv (std::atoi (parts[0].c_str()), 1, kMaxSteps);
            auto a = split (parts[1], ',');
            for (int i = 0; i < std::min (p.numSteps, (int) a.size()); ++i) p.slice[(size_t) i] = (int16_t) std::atoi (a[(size_t) i].c_str());
            if (parts.size() > 2)
            {
                auto f = split (parts[2], ',');
                for (int i = 0; i < std::min (p.numSteps, (int) f.size()); ++i)
                {
                    const auto c = f[(size_t) i].find (':');
                    p.fx[(size_t) i].type = (FxType) clampv (std::atoi (f[(size_t) i].c_str()), 0, kNumFxTypes - 1);
                    p.fx[(size_t) i].rate = (uint8_t) clampv (c == std::string::npos ? 16 : std::atoi (f[(size_t) i].c_str() + c + 1), 8, 64);
                }
            }
            if (parts.size() > 3)
            {
                auto l = split (parts[3], ',');
                for (int i = 0; i < std::min (p.numSteps, (int) l.size()); ++i) p.locked[(size_t) i] = l[(size_t) i] == "1";
            }
            return p;
        }

        std::string sliceParamsToString (const SliceParams& s)
        {
            std::ostringstream o;
            o << s.pitch << ',' << s.gate << ',' << s.stretch << ',' << (s.reverse ? 1 : 0) << ',' << s.stutter << ','
              << s.trans << ',' << s.xfadeMs << ',' << (s.mute ? 1 : 0) << ',' << s.level;
            return o.str();
        }

        SliceParams sliceParamsFromString (const std::string& str)
        {
            SliceParams s;
            auto t = split (str, ',');
            if (t.size() >= 9)
            {
                s.pitch = (float) std::atof (t[0].c_str()); s.gate = (float) std::atof (t[1].c_str()); s.stretch = (float) std::atof (t[2].c_str());
                s.reverse = t[3] == "1"; s.stutter = std::atoi (t[4].c_str()); s.trans = (float) std::atof (t[5].c_str());
                s.xfadeMs = (float) std::atof (t[6].c_str()); s.mute = t[7] == "1"; s.level = (float) std::atof (t[8].c_str());
            }
            return s;
        }

        Badge badgeFromLetter (char c)
        {
            switch (c) { case 'K': return Badge::kick; case 'S': return Badge::snare; case 'G': return Badge::ghost;
                         case 'H': return Badge::hat; case 'O': return Badge::open; default: return Badge::other; }
        }

        void remapPattern (Pattern& p, int from, int delta)
        {
            for (int i = 0; i < p.numSteps; ++i)
            {
                auto& v = p.slice[(size_t) i];
                if (v < 0) continue;
                if (delta < 0 && v == from) v = -1;
                else if (v >= from) v = (int16_t) std::max (0, v + delta);
            }
        }
    }

    ContentPtr analyseBreak (const SamplePtr& buf, bool grid, float sens, int gridPerBar, const std::string* restore, bool fileSlices)
    {
        auto c = std::make_shared<SlotContent>();
        c->buffer = buf;
        c->name = buf->name;
        const auto tempo = Analysis::estimateTempo (buf->lengthSeconds(), buf->name);
        c->bpm = tempo.bpm;
        c->bars = tempo.bars;
        if (buf->fileBars > 0 && buf->fileBpm > 1.0) { c->bpm = buf->fileBpm; c->bars = buf->fileBars; }   // REX: as ReCycle says

        KV kv;
        if (restore != nullptr) kv = parseBlock (*restore);
        if (kv.count ("slices"))
        {
            if (kv.count ("bpm"))  c->bpm = std::atof (kv["bpm"].c_str());
            if (kv.count ("bars")) c->bars = std::atoi (kv["bars"].c_str());
            c->slices = SliceTable::fromString (kv["slices"]);
            c->slices.normalise (buf->numSamples);
        }
        if (c->slices.isEmpty() && fileSlices && ! buf->fileMarkers.empty())
        {
            // a REX file's own chops (RESLICE finds new ones)
            SliceTable t;
            for (int s : buf->fileMarkers) if (t.count < kMaxSlices) t.starts[(size_t) t.count++] = s;
            t.normalise (buf->numSamples);
            c->slices = t;
        }
        if (c->slices.isEmpty())
            c->slices = grid ? Analysis::gridSlices (buf->numSamples, c->bars, gridPerBar)
                             : TransientDetector::detect (*buf, sens, std::min (kMaxSlices, c->bars * 16));
        if (c->slices.isEmpty()) c->slices = SliceTable::equal (buf->numSamples, 16);

        c->badges = Analysis::classify (*buf, c->slices);
        if (kv.count ("badges"))
        {
            const auto& s = kv["badges"];
            for (int k = 0; k < std::min ((int) s.size(), kMaxSlices); ++k) c->badges[(size_t) k] = badgeFromLetter (s[(size_t) k]);
        }
        return c;
    }

    const char* Session::statusText (Status s)
    {
        switch (s)
        {
            case Status::empty:      return "EMPTY";
            case Status::loading:    return "LOADING";
            case Status::ready:      return "READY";
            case Status::missing:    return "FILE MISSING";
            case Status::unreadable: return "CAN'T READ FILE";
        }
        return "";
    }

    // =========================================================================================
    Session::Session()
    {
        decoder = [] (const std::string& p, AudioFile::Status& st) { return AudioFile::load (p, st); };
        worker = std::thread ([this] { workerLoop(); });
        loadDemo (0);
    }

    Session::~Session()
    {
        { std::lock_guard<std::mutex> l (qMutex); quit = true; }
        qCv.notify_all();
        if (worker.joinable()) worker.join();
    }

    void Session::prepare (double sampleRate, int maxBlock)
    {
        std::lock_guard<std::recursive_mutex> l (mutex);
        sr = sampleRate;
        eng.prepare (sampleRate, maxBlock);
        for (int i = 0; i < kSlots; ++i) { eng.publishContent (i, slots[(size_t) i].content); publish (i); }
    }

    void Session::setDecoder (Decoder d) { std::lock_guard<std::mutex> l (qMutex); decoder = std::move (d); }
    void Session::setDataFolder (const std::string& f) { std::lock_guard<std::recursive_mutex> l (mutex); dataFolder = f; }

    void Session::process (float* left, float* right, int n, const MidiMsg* midi, int numMidi,
                           const Engine::Host& h, const Engine::Settings& s) noexcept
    {
        eng.process (left, right, n, midi, numMidi, h, s);
    }

    // ---- worker -------------------------------------------------------------------------------
    void Session::workerLoop()
    {
        for (;;)
        {
            std::optional<Job> job;
            std::optional<ScanJob> scan;
            Decoder dec;
            {
                std::unique_lock<std::mutex> l (qMutex);
                qCv.wait (l, [this] { return quit || ! jobs.empty() || ! scanJobs.empty(); });
                if (quit) return;
                if (! jobs.empty()) { job = jobs.front(); jobs.pop_front(); }
                else { scan = scanJobs.front(); scanJobs.pop_front(); }
                dec = decoder;
            }
            if (job)
            {
                AudioFile::Status st = AudioFile::Status::unreadable;
                auto buf = dec (job->path, st);
                ContentPtr c;
                if (buf != nullptr)
                {
                    if (buf->overviewMin.empty()) buf->buildOverview();
                    if (buf->path.empty()) buf->path = job->path;
                    if (buf->name.empty()) buf->name = AudioFile::baseName (job->path);
                    c = analyseBreak (buf, job->grid, job->sens, job->gpb, job->restore ? &*job->restore : nullptr);
                }
                std::lock_guard<std::mutex> l (qMutex);
                done.push_back ({ job->slot, job->path, c, st, job->restore });
            }
            else if (scan)
            {
                std::vector<BrowserEntry> found, dirs;
                std::error_code ec;
                auto addFile = [&] (const fs::path& path) -> bool
                {
                    const auto p = path.string();
                    std::string ext = path.extension().string();
                    for (auto& ch : ext) ch = (char) std::tolower ((unsigned char) ch);
                    if (ext != ".wav" && ext != ".aif" && ext != ".aiff" && ext != ".flac" && ext != ".mp3"
                        && ext != ".rx2" && ext != ".rex") return false;
                    BrowserEntry e;
                    e.path = p;
                    e.name = AudioFile::baseName (p);
                    const auto info = AudioFile::readInfo (p);
                    e.seconds = info.ok && info.sampleRate > 0 ? (double) info.frames / info.sampleRate : 0.0;
                    const auto t = Analysis::estimateTempo (e.seconds > 0 ? e.seconds : 60.0 / 170.0 * 8.0, e.name);
                    e.bpm = e.seconds > 0 ? t.bpm : 0.0;
                    e.bars = e.seconds > 0 ? t.bars : 0;
                    if (info.bpm > 1.0) { e.bpm = info.bpm; e.bars = info.bars; }   // REX: from the file
                    found.push_back (e);
                    return true;
                };
                if (scan->flat)
                {
                    for (fs::directory_iterator it (scan->folder, fs::directory_options::skip_permission_denied, ec), end;
                         ! ec && it != end; it.increment (ec))
                    {
                        const auto name = it->path().filename().string();
                        if (name.empty() || name[0] == '.') continue;          // hidden / system
                        std::error_code e2;
                        if (it->is_directory (e2))
                        {
                            BrowserEntry d;
                            d.path = it->path().string();
                            d.name = name;
                            d.isDir = true;
                            dirs.push_back (d);
                        }
                        else if (it->is_regular_file (e2)) addFile (it->path());
                        if (found.size() + dirs.size() >= 2000) break;
                    }
                    std::sort (dirs.begin(), dirs.end(), [] (const BrowserEntry& a, const BrowserEntry& b)
                    {
                        std::string x = a.name, y = b.name;
                        for (auto& c : x) c = (char) std::tolower ((unsigned char) c);
                        for (auto& c : y) c = (char) std::tolower ((unsigned char) c);
                        return x < y;
                    });
                }
                else if (fs::is_directory (scan->folder, ec))
                {
                    for (auto it = fs::recursive_directory_iterator (scan->folder, fs::directory_options::skip_permission_denied, ec);
                         it != fs::recursive_directory_iterator(); it.increment (ec))
                    {
                        if (ec) break;
                        if (it.depth() > 3) { it.disable_recursion_pending(); continue; }
                        if (! it->is_regular_file (ec)) continue;
                        addFile (it->path());
                        if (found.size() >= 2000) break;
                    }
                }
                std::sort (found.begin(), found.end(), [] (const BrowserEntry& a, const BrowserEntry& b)
                           { return a.bpm != b.bpm ? a.bpm < b.bpm : a.name < b.name; });
                if (scan->flat)
                {
                    std::vector<BrowserEntry> all;
                    auto clean = [] (std::string p) { while (p.size() > 1 && p.back() == '/') p.pop_back(); return p; };
                    const std::string here = clean (scan->folder), top = clean (scan->top);
                    const auto parent = fs::path (here).parent_path().string();
                    if ((top.empty() || here != top) && ! parent.empty() && parent != here)
                    {
                        BrowserEntry up;
                        up.path = parent;
                        up.name = "..";
                        up.isDir = up.isUp = true;
                        all.push_back (up);
                    }
                    all.insert (all.end(), dirs.begin(), dirs.end());
                    all.insert (all.end(), found.begin(), found.end());
                    found = std::move (all);
                }
                {
                    std::lock_guard<std::recursive_mutex> l (mutex);
                    browser = std::move (found);
                    browserDir = scan->folder;
                }
                scanning = false;
                ++uiVersion;
            }
        }
    }

    void Session::setMuteGroup (int slot, int group)
    {
        if (slot < 0 || slot >= kSlots) return;
        std::lock_guard<std::recursive_mutex> l (mutex);
        slots[(size_t) slot].muteGroup = clampv (group, 0, kMuteGroups - 1);
        publish (slot);
    }

    int Session::muteGroup (int slot) const
    {
        return slot >= 0 && slot < kSlots ? slots[(size_t) slot].muteGroup : 0;
    }

    bool Session::audition (const std::string& path)
    {
        Decoder dec;
        { std::lock_guard<std::mutex> q (qMutex); dec = decoder; }
        AudioFile::Status st = AudioFile::Status::unreadable;
        auto b = dec ? dec (path, st) : nullptr;
        std::lock_guard<std::recursive_mutex> l (mutex);
        if (b == nullptr) { auditionStop(); return false; }
        auditionKeep.push_back (b);
        auditionPath = path;
        eng.publishAudition (std::move (b));
        return true;
    }

    void Session::auditionStop()
    {
        std::lock_guard<std::recursive_mutex> l (mutex);
        auditionPath.clear();
        eng.publishAudition (nullptr);
    }

    void Session::poll()
    {
        {
            // auditions the audio thread has let go of (the newest one stays until it is replaced)
            std::lock_guard<std::recursive_mutex> lk (mutex);
            if (auditionKeep.size() > 1)
                auditionKeep.erase (std::remove_if (auditionKeep.begin(), auditionKeep.end() - 1,
                                                    [] (const SamplePtr& p) { return p.use_count() <= 1; }),
                                    auditionKeep.end() - 1);
            if (auditionPath.empty() && ! auditionKeep.empty() && auditionKeep.back().use_count() <= 1) auditionKeep.clear();
        }
        std::deque<Done> ready;
        { std::lock_guard<std::mutex> l (qMutex); ready.swap (done); }
        std::lock_guard<std::recursive_mutex> lk (mutex);
        for (auto& d : ready)
        {
            --pending;
            auto& s = slots[(size_t) d.slot];
            if (s.file != d.path) continue;   // superseded by a newer load
            if (d.content != nullptr) installContent (d.slot, d.content, d.path, false, d.restore ? &*d.restore : nullptr);
            else
            {
                s.status = d.status == AudioFile::Status::missing ? Status::missing : Status::unreadable;
                changed (d.slot);
            }
        }

        // pattern memory notes (G#3-B4) arrive from the audio thread
        const int req = eng.memoryRequest.exchange (-1);
        if (req >= 0)
        {
            const int si = req / kMemories, m = req % kMemories;
            auto& s = slots[(size_t) si];
            if (s.memories[(size_t) m].has_value())
            {
                s.current = *s.memories[(size_t) m];
                s.showOriginal = false;
                pushHistory (s, s.current);
                publish (si);
            }
        }

        // a break note on the MPC / keyboard moves the selection: keep the selected slice valid
        if (const auto& c = slots[(size_t) selectedSlot()].content; c != nullptr && selSlice >= c->numSlices()) selSlice = 0;

        keepAlive.erase (std::remove_if (keepAlive.begin(), keepAlive.end(), [] (const ContentPtr& p) { return p.use_count() <= 1; }),
                         keepAlive.end());
    }

    bool Session::isBusy() const { return pending.load() > 0 || scanning.load(); }

    void Session::waitUntilIdle (int timeoutMs)
    {
        const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds (timeoutMs);
        while (isBusy() && std::chrono::steady_clock::now() < end)
        {
            poll();
            std::this_thread::sleep_for (std::chrono::milliseconds (5));
        }
        poll();
    }

    // ---- slots ----------------------------------------------------------------------------------
    void Session::selectSlot (int i)
    {
        std::lock_guard<std::recursive_mutex> l (mutex);
        eng.selectedSlot = clampv (i, 0, kSlots - 1);
        selSlice = 0;
        ++uiVersion;
    }

    void Session::selectSlice (int k, bool audition)
    {
        std::lock_guard<std::recursive_mutex> l (mutex);
        const auto& s = slots[(size_t) selectedSlot()];
        if (s.content == nullptr) return;
        selSlice = clampv (k, 0, s.content->numSlices() - 1);
        if (audition) eng.previewSlice = selSlice;
        ++uiVersion;
    }

    void Session::loadDemo (int slot)
    {
        std::lock_guard<std::recursive_mutex> l (mutex);
        installContent (slot, analyseBreak (DemoBreak::make (44100.0, 170.0), false, 0.62f, 16, nullptr), "", true, nullptr);
    }

    void Session::loadFile (int slot, const std::string& path)
    {
        if (slot < 0 || slot >= kSlots) return;
        std::lock_guard<std::recursive_mutex> l (mutex);
        auto& s = slots[(size_t) slot];
        s.status = Status::loading;
        s.file = path;
        s.demo = false;
        changed (slot);
        ++pending;
        { std::lock_guard<std::mutex> q (qMutex); jobs.push_back ({ slot, path, std::nullopt, s.gridSlicing, s.sensitivity, s.gridPerBar }); }
        qCv.notify_one();
    }

    void Session::clearSlot (int slot)
    {
        if (slot < 0 || slot >= kSlots) return;
        std::lock_guard<std::recursive_mutex> l (mutex);
        slots[(size_t) slot] = Slot();
        eng.publishContent (slot, nullptr);
        publish (slot);
    }

    void Session::setSlicing (int slot, bool grid, float sens, int gridPerBar)
    {
        std::lock_guard<std::recursive_mutex> l (mutex);
        if (slot < 0 || slot >= kSlots) return;
        auto& s = slots[(size_t) slot];
        s.gridSlicing = grid; s.sensitivity = clampv (sens, 0.05f, 0.95f); s.gridPerBar = gridPerBar;
        publish (slot);
    }

    void Session::reslice (int slot, bool grid, float sens, int gridPerBar)
    {
        std::lock_guard<std::recursive_mutex> l (mutex);
        auto& s = slots[(size_t) slot];
        s.gridSlicing = grid; s.sensitivity = sens; s.gridPerBar = gridPerBar;
        if (s.content == nullptr) { changed (slot); return; }
        auto fresh = analyseBreak (s.content->buffer, grid, sens, gridPerBar, nullptr, false);
        auto c = std::const_pointer_cast<SlotContent> (fresh);
        c->bpm = s.content->bpm; c->bars = s.content->bars;
        installContent (slot, c, s.file, s.demo, nullptr);
    }

    void Session::installContent (int slot, ContentPtr c, const std::string& path, bool demo, const std::string* restore)
    {
        auto& s = slots[(size_t) slot];
        const bool grid = s.gridSlicing; const float sens = s.sensitivity; const int gpb = s.gridPerBar; const int grp = s.muteGroup;
        s = Slot();
        s.gridSlicing = grid; s.sensitivity = sens; s.gridPerBar = gpb; s.muteGroup = grp;
        s.content = c;
        keep (c);
        s.file = path;
        s.demo = demo;
        s.status = Status::ready;
        s.original = Analysis::originalPattern (*c);
        s.current = s.original;

        if (restore != nullptr)
        {
            auto kv = parseBlock (*restore);
            if (auto p = patternFromString (kv["pattern"])) s.current = *p;
            s.showOriginal = kv["showOriginal"] == "1";
            auto ps = split (kv["sliceParams"], ';');
            for (int k = 0; k < std::min ((int) ps.size(), kMaxSlices); ++k) s.program.slice[(size_t) k] = sliceParamsFromString (ps[(size_t) k]);
            if (kv.count ("all"))
            {
                const auto a = sliceParamsFromString (kv["all"]);
                s.program.all = { a.pitch, a.gate, a.stretch, a.reverse, a.stutter, a.trans, a.xfadeMs };
            }
            for (int m = 0; m < kMemories; ++m)
                if (kv.count ("mem" + std::to_string (m)))
                    if (auto p = patternFromString (kv["mem" + std::to_string (m)])) s.memories[(size_t) m] = *p;
            if (kv.count ("grid")) s.gridSlicing = kv["grid"] == "1";
            if (kv.count ("sens")) s.sensitivity = (float) std::atof (kv["sens"].c_str());
            if (kv.count ("gridPerBar")) s.gridPerBar = std::atoi (kv["gridPerBar"].c_str());
            if (kv.count ("group")) s.muteGroup = clampv (std::atoi (kv["group"].c_str()), 0, kMuteGroups - 1);
            if (kv.count ("loopBpm")) s.loopBpm = std::atof (kv["loopBpm"].c_str());
        }
        pushHistory (s, s.current);
        if (slot == selectedSlot()) selSlice = 0;
        eng.publishContent (slot, c);
        publish (slot);
    }

    void Session::publish (int slot)
    {
        auto& s = slots[(size_t) slot];
        s.program.pattern = s.showOriginal ? s.original : s.current;
        s.program.muteGroup = s.muteGroup;
        eng.publishProgram (slot, s.program);
        changed (slot);
    }

    void Session::changed (int slot) { ++slots[(size_t) slot].version; ++uiVersion; }

    void Session::pushHistory (Slot& s, const Pattern& p)
    {
        if (s.historyPos >= 0 && s.historyPos < (int) s.history.size() - 1) s.history.resize ((size_t) s.historyPos + 1);
        s.history.push_back (p);
        if (s.history.size() > 32) s.history.erase (s.history.begin());
        s.historyPos = (int) s.history.size() - 1;
    }

    ContentPtr Session::withBadges (const SlotContent& c, const std::array<Badge, kMaxSlices>& b) const
    {
        auto n = std::make_shared<SlotContent> (c);
        n->badges = b;
        return n;
    }

    ContentPtr Session::withSlices (const SlotContent& c, const SliceTable& t) const
    {
        auto n = std::make_shared<SlotContent> (c);
        n->slices = t;
        n->slices.normalise (c.buffer->numSamples);
        n->badges = Analysis::classify (*c.buffer, n->slices);
        return n;
    }

    // ---- SAMPLE tab ------------------------------------------------------------------------------
    float Session::getSliceValue (SliceField f, bool all) const
    {
        std::lock_guard<std::recursive_mutex> l (mutex);
        const auto& s = slots[(size_t) selectedSlot()];
        if (all)
        {
            const auto& a = s.program.all;
            switch (f)
            {
                case SliceField::pitch: return a.pitch;     case SliceField::gate: return a.gate;
                case SliceField::stretch: return a.stretch; case SliceField::reverse: return a.reverse ? 1.0f : 0.0f;
                case SliceField::stutter: return (float) a.stutter; case SliceField::trans: return a.trans;
                case SliceField::xfade: return a.xfadeMs;   case SliceField::level: return 1.0f;
            }
        }
        const auto& p = s.program.slice[(size_t) clampv (selSlice, 0, kMaxSlices - 1)];
        switch (f)
        {
            case SliceField::pitch: return p.pitch;     case SliceField::gate: return p.gate;
            case SliceField::stretch: return p.stretch; case SliceField::reverse: return p.reverse ? 1.0f : 0.0f;
            case SliceField::stutter: return (float) p.stutter; case SliceField::trans: return p.trans;
            case SliceField::xfade: return p.xfadeMs;   case SliceField::level: return p.level;
        }
        return 0.0f;
    }

    void Session::setSliceValue (SliceField f, float v, bool all)
    {
        std::lock_guard<std::recursive_mutex> l (mutex);
        const int slot = selectedSlot();
        auto& s = slots[(size_t) slot];
        auto apply = [&] (auto& p)
        {
            switch (f)
            {
                case SliceField::pitch:   p.pitch = clampv (v, -24.0f, 24.0f); break;
                case SliceField::gate:    p.gate = clampv (v, 0.05f, 1.0f); break;
                case SliceField::stretch: p.stretch = clampv (v, 0.5f, 4.0f); break;
                case SliceField::reverse: p.reverse = v > 0.5f; break;
                case SliceField::stutter: p.stutter = clampv ((int) std::lround (v), 0, 8); break;
                case SliceField::trans:   p.trans = clampv (v, 0.0f, 1.0f); break;
                case SliceField::xfade:   p.xfadeMs = clampv (v, 0.5f, 20.0f); break;
                case SliceField::level:   break;
            }
        };
        if (all) apply (s.program.all);
        else
        {
            auto& p = s.program.slice[(size_t) clampv (selSlice, 0, kMaxSlices - 1)];
            apply (p);
            if (f == SliceField::level) p.level = clampv (v, 0.0f, 2.0f);
        }
        publish (slot);
    }

    void Session::toggleMute (int k)
    {
        std::lock_guard<std::recursive_mutex> l (mutex);
        const int slot = selectedSlot();
        auto& p = slots[(size_t) slot].program.slice[(size_t) clampv (k, 0, kMaxSlices - 1)];
        p.mute = ! p.mute;
        publish (slot);
    }

    void Session::setBadge (int k, Badge b)
    {
        std::lock_guard<std::recursive_mutex> l (mutex);
        const int slot = selectedSlot();
        auto& s = slots[(size_t) slot];
        if (s.content == nullptr) return;
        auto badges = s.content->badges;
        badges[(size_t) clampv (k, 0, kMaxSlices - 1)] = b;
        s.content = withBadges (*s.content, badges);
        keep (s.content);
        eng.publishContent (slot, s.content);
        publish (slot);
    }

    void Session::moveMarker (int marker, int position)
    {
        std::lock_guard<std::recursive_mutex> l (mutex);
        const int slot = selectedSlot();
        auto& s = slots[(size_t) slot];
        if (s.content == nullptr || marker < 0 || marker >= s.content->slices.count) return;
        auto t = s.content->slices;
        t.starts[(size_t) marker] = position;
        s.content = withSlices (*s.content, t);
        keep (s.content);
        s.original = Analysis::originalPattern (*s.content);
        eng.publishContent (slot, s.content);
        publish (slot);
    }

    int Session::snapToHit (int position, int radius) const
    {
        std::lock_guard<std::recursive_mutex> l (mutex);
        const auto& s = slots[(size_t) selectedSlot()];
        if (s.content == nullptr) return position;
        const auto& b = *s.content->buffer;
        const int n = b.numSamples;
        constexpr int win = 128, hop = 8;
        const int a0 = std::max (win, position - radius), a1 = std::min (n - win, position + radius);
        if (a1 <= a0) return position;
        auto level = [&] (int from, int to)
        {
            double e = 0.0;
            for (int i = from; i < to; i += 2)
                for (int c = 0; c < b.channels; ++c) e += std::abs (b.data (c)[i]);
            return e;
        };
        // where the next 128 samples are loudest against the 128 before (a ratio, so quiet ghost notes and hats count
        // as well as loud kicks), nearer the touch preferred a little
        double peak = 0.0;
        for (int i = a0; i < a1; i += hop) peak = std::max (peak, level (i, i + win));
        int best = -1;
        double bestScore = 1.6;                    // the level must at least jump by this much to be a hit
        for (int i = a0; i < a1; i += hop)
        {
            const double after = level (i, i + win);
            if (after < 0.05 * peak) continue;
            const double ratio = after / (level (i - win, i) + 1.0e-3 * win);
            const double score = ratio * (1.0 - 0.3 * std::abs (i - position) / (double) std::max (1, radius));
            if (score > bestScore) { bestScore = score; best = i; }
        }
        if (best < 0) return position;
        // the ratio peaks a little after the true start: walk back while the level still rises
        while (best - hop > a0 && level (best - hop, best - hop + win) > 0.9 * level (best, best + win)
               && level (best - hop - win, best - hop) < level (best - win, best)) best -= hop;
        // a click-free start: the zero crossing just before it (within 2 ms)
        const float* x = b.data (0);
        const int back = std::max (1, (int) (b.sampleRate * 0.002));
        for (int i = best; i > std::max (1, best - back); --i)
            if ((x[i - 1] <= 0.0f) != (x[i] <= 0.0f)) return i;
        return best;
    }

    void Session::addMarker (int position)
    {
        std::lock_guard<std::recursive_mutex> l (mutex);
        const int slot = selectedSlot();
        auto& s = slots[(size_t) slot];
        if (s.content == nullptr || s.content->slices.count >= kMaxSlices) return;
        auto t = s.content->slices;
        t.starts[(size_t) t.count++] = position;
        auto c = withSlices (*s.content, t);
        const int newIndex = c->slices.sliceAt (position);
        s.content = c;
        keep (c);
        remapPattern (s.current, newIndex, +1);
        for (int k = kMaxSlices - 1; k > newIndex; --k) s.program.slice[(size_t) k] = s.program.slice[(size_t) k - 1];
        s.program.slice[(size_t) newIndex] = {};
        s.original = Analysis::originalPattern (*c);
        eng.publishContent (slot, c);
        publish (slot);
    }

    void Session::deleteMarker (int marker)
    {
        std::lock_guard<std::recursive_mutex> l (mutex);
        const int slot = selectedSlot();
        auto& s = slots[(size_t) slot];
        if (s.content == nullptr || marker <= 0 || marker >= s.content->slices.count) return;
        auto t = s.content->slices;
        for (int k = marker; k < t.count - 1; ++k) t.starts[(size_t) k] = t.starts[(size_t) k + 1];
        --t.count;
        s.content = withSlices (*s.content, t);
        keep (s.content);
        remapPattern (s.current, marker, -1);
        for (int k = marker; k < kMaxSlices - 1; ++k) s.program.slice[(size_t) k] = s.program.slice[(size_t) k + 1];
        s.original = Analysis::originalPattern (*s.content);
        selSlice = std::min (selSlice, s.content->numSlices() - 1);
        eng.publishContent (slot, s.content);
        publish (slot);
    }

    // ---- dice / history / SEQ ----------------------------------------------------------------------
    void Session::rollPattern (Style style, float amount, bool layers)
    {
        std::lock_guard<std::recursive_mutex> l (mutex);
        const int slot = selectedSlot();
        auto& s = slots[(size_t) slot];
        if (s.content == nullptr) return;
        Generator::Input in;
        in.content = s.content.get();
        in.original = s.original;
        in.current = s.current;
        in.style = style;
        in.amount = amount;
        in.layers = layers;
        in.seed = (int64_t) std::chrono::steady_clock::now().time_since_epoch().count();
        auto p = Generator::pattern (in);
        p.fx = s.current.fx;   // the FX lane belongs to the FX dice
        s.current = p;
        s.showOriginal = false;
        pushHistory (s, s.current);
        publish (slot);
    }

    void Session::rollFx (float amount)
    {
        std::lock_guard<std::recursive_mutex> l (mutex);
        const int slot = selectedSlot();
        auto& s = slots[(size_t) slot];
        if (s.content == nullptr) return;
        s.current = Generator::fx (s.current, amount, (int64_t) std::chrono::steady_clock::now().time_since_epoch().count());
        s.showOriginal = false;
        pushHistory (s, s.current);
        publish (slot);
    }

    void Session::historyBack()
    {
        std::lock_guard<std::recursive_mutex> l (mutex);
        const int slot = selectedSlot();
        auto& s = slots[(size_t) slot];
        if (s.historyPos <= 0) return;
        s.current = s.history[(size_t) --s.historyPos];
        s.showOriginal = false;
        publish (slot);
    }

    void Session::historyForward()
    {
        std::lock_guard<std::recursive_mutex> l (mutex);
        const int slot = selectedSlot();
        auto& s = slots[(size_t) slot];
        if (s.historyPos >= (int) s.history.size() - 1) return;
        s.current = s.history[(size_t) ++s.historyPos];
        s.showOriginal = false;
        publish (slot);
    }

    void Session::setShowOriginal (bool b)
    {
        std::lock_guard<std::recursive_mutex> l (mutex);
        slots[(size_t) selectedSlot()].showOriginal = b;
        publish (selectedSlot());
    }

    void Session::setPatternLength (int bars)
    {
        std::lock_guard<std::recursive_mutex> l (mutex);
        const int slot = selectedSlot();
        auto& s = slots[(size_t) slot];
        const int steps = clampv (bars * 16, 16, kMaxSteps);
        for (int i = s.current.numSteps; i < steps; ++i)
        {
            const int src = i % std::max (1, s.current.numSteps);
            s.current.slice[(size_t) i] = s.current.slice[(size_t) src];
            s.current.fx[(size_t) i] = s.current.fx[(size_t) src];
            s.current.locked[(size_t) i] = false;
        }
        s.current.numSteps = steps;
        s.showOriginal = false;
        pushHistory (s, s.current);
        publish (slot);
    }

    void Session::toggleStep (int step, Badge lane)
    {
        std::lock_guard<std::recursive_mutex> l (mutex);
        const int slot = selectedSlot();
        auto& s = slots[(size_t) slot];
        if (s.content == nullptr || step < 0 || step >= s.current.numSteps) return;
        auto& v = s.current.slice[(size_t) step];
        if (v >= 0 && s.content->badges[(size_t) v] == lane) v = -1;
        else
        {
            Rng r ((int64_t) step * 7919 + (int64_t) lane);
            const int k = Generator::pickSlice (*s.content, lane, r);
            if (k >= 0) v = (int16_t) k;
        }
        s.showOriginal = false;
        publish (slot);
    }

    void Session::setStepSlice (int step, int slice)
    {
        std::lock_guard<std::recursive_mutex> l (mutex);
        const int slot = selectedSlot();
        auto& s = slots[(size_t) slot];
        if (s.content == nullptr || step < 0 || step >= s.current.numSteps) return;
        s.current.slice[(size_t) step] = (int16_t) clampv (slice, -1, s.content->numSlices() - 1);
        s.showOriginal = false;
        publish (slot);
    }

    void Session::swapStep (int step)
    {
        std::lock_guard<std::recursive_mutex> l (mutex);
        const int slot = selectedSlot();
        auto& s = slots[(size_t) slot];
        if (s.content == nullptr || step < 0 || step >= s.current.numSteps) return;
        auto& v = s.current.slice[(size_t) step];
        if (v >= 0) v = (int16_t) Generator::nextWithSameBadge (*s.content, v);
        s.showOriginal = false;
        publish (slot);
    }

    void Session::toggleLock (int step)
    {
        std::lock_guard<std::recursive_mutex> l (mutex);
        const int slot = selectedSlot();
        auto& s = slots[(size_t) slot];
        if (step < 0 || step >= s.current.numSteps) return;
        s.current.locked[(size_t) step] = ! s.current.locked[(size_t) step];
        publish (slot);
    }

    void Session::setFxStep (int step, FxType t, int rate)
    {
        std::lock_guard<std::recursive_mutex> l (mutex);
        const int slot = selectedSlot();
        auto& s = slots[(size_t) slot];
        if (step < 0 || step >= s.current.numSteps) return;
        s.current.fx[(size_t) step] = { t, (uint8_t) clampv (rate, 8, 64) };
        s.showOriginal = false;
        publish (slot);
    }

    void Session::cycleFxRate (int step)
    {
        std::lock_guard<std::recursive_mutex> l (mutex);
        const int slot = selectedSlot();
        auto& s = slots[(size_t) slot];
        if (step < 0 || step >= s.current.numSteps) return;
        auto& e = s.current.fx[(size_t) step];
        if (e.type == FxType::none) return;
        e.rate = (uint8_t) (e.rate >= 64 ? 8 : e.rate * 2);
        publish (slot);
    }

    void Session::saveMemory (int m)
    {
        if (m < 0 || m >= kMemories) return;
        std::lock_guard<std::recursive_mutex> l (mutex);
        slots[(size_t) selectedSlot()].memories[(size_t) m] = slots[(size_t) selectedSlot()].current;
        changed (selectedSlot());
    }

    void Session::saveOriginalToMemory (int m)
    {
        // the break as recorded (FX lane empty) into memory m: recall it with the memory's key like any other
        if (m < 0 || m >= kMemories) return;
        std::lock_guard<std::recursive_mutex> l (mutex);
        auto& s = slots[(size_t) selectedSlot()];
        if (s.content == nullptr) return;
        Pattern p = s.original;
        for (auto& f : p.fx) f = FxEvent{};
        p.locked.fill (false);
        s.memories[(size_t) m] = p;
        changed (selectedSlot());
    }

    void Session::clearMemory (int m)
    {
        if (m < 0 || m >= kMemories) return;
        std::lock_guard<std::recursive_mutex> l (mutex);
        slots[(size_t) selectedSlot()].memories[(size_t) m].reset();
        changed (selectedSlot());
    }

    void Session::recallMemory (int m)
    {
        if (m < 0 || m >= kMemories) return;
        std::lock_guard<std::recursive_mutex> l (mutex);
        const int slot = selectedSlot();
        auto& s = slots[(size_t) slot];
        if (! s.memories[(size_t) m].has_value()) return;
        s.current = *s.memories[(size_t) m];
        s.showOriginal = false;
        pushHistory (s, s.current);
        publish (slot);
    }

    void Session::clearPattern()
    {
        std::lock_guard<std::recursive_mutex> l (mutex);
        const int slot = selectedSlot();
        auto& s = slots[(size_t) slot];
        const int n = s.current.numSteps;
        s.current = Pattern();
        s.current.numSteps = n;
        s.showOriginal = false;
        pushHistory (s, s.current);
        publish (slot);
    }

    void Session::resetToOriginal()
    {
        // the pattern back to the break as recorded (one hit per slice, in order) with an empty FX lane
        std::lock_guard<std::recursive_mutex> l (mutex);
        const int slot = selectedSlot();
        auto& s = slots[(size_t) slot];
        if (s.content == nullptr) return;
        s.current = s.original;
        for (auto& f : s.current.fx) f = FxEvent{};
        s.current.locked.fill (false);
        s.showOriginal = false;
        pushHistory (s, s.current);
        publish (slot);
    }

    // ---- browser -----------------------------------------------------------------------------------
    void Session::scanFolder (const std::string& folder)
    {
        scanning = true;
        { std::lock_guard<std::mutex> q (qMutex); scanJobs.push_back ({ folder, {}, false }); }
        qCv.notify_one();
    }

    void Session::listFolder (const std::string& folder, const std::string& top)
    {
        scanning = true;
        { std::lock_guard<std::mutex> q (qMutex); scanJobs.push_back ({ folder, top, true }); }
        qCv.notify_one();
    }

    std::vector<Session::BrowserEntry> Session::browserEntries() const { std::lock_guard<std::recursive_mutex> l (mutex); return browser; }
    std::string Session::browserFolder() const { std::lock_guard<std::recursive_mutex> l (mutex); return browserDir; }

    // ---- renders ------------------------------------------------------------------------------------
    bool Session::akaizeSlot (int slot, double targetBpm, double stretchPercent, int cycleLength)
    {
        AkaizeRender r;
        r.sync = targetBpm > 1.0;
        r.targetBpm = targetBpm;
        r.stretchPercent = stretchPercent;
        r.cycleLength = cycleLength;
        r.cycleFreq = akaizer::linkedFrequency (clampv (cycleLength, 20, 2000));
        return akaizeSlot (slot, r);
    }

    void Session::setLoopBpm (int slot, double bpm)
    {
        if (slot < 0 || slot >= kSlots) return;
        std::lock_guard<std::recursive_mutex> l (mutex);
        slots[(size_t) slot].loopBpm = bpm > 1.0 ? clampv (bpm, 20.0, 400.0) : 0.0;
        changed (slot);
    }

    double Session::loopBpm (int slot) const
    {
        if (slot < 0 || slot >= kSlots) return 0.0;
        const auto& s = slots[(size_t) slot];
        if (s.loopBpm > 1.0) return s.loopBpm;
        return s.content != nullptr ? s.content->bpm : 0.0;
    }

    bool Session::loopBpmSet (int slot) const { return slot >= 0 && slot < kSlots && slots[(size_t) slot].loopBpm > 1.0; }

    bool Session::akaizeSlot (int slot, const AkaizeRender& set)
    {
        std::lock_guard<std::recursive_mutex> l (mutex);
        auto& s = slots[(size_t) clampv (slot, 0, kSlots - 1)];
        if (s.content == nullptr) return false;
        const auto& src = *s.content->buffer;
        const double loop = loopBpm (slot);
        double stretchPercent = set.sync && set.targetBpm > 1.0 && loop > 1.0 ? loop / set.targetBpm * 100.0 : set.stretchPercent;
        stretchPercent = clampv (stretchPercent, 25.0, 2000.0);

        // Akaizer works on interleaved 16-bit, exactly as the original program did
        const int chans = src.channels;
        std::vector<int16_t> pcm ((size_t) src.numSamples * (size_t) chans);
        for (int i = 0; i < src.numSamples; ++i)
            for (int c = 0; c < chans; ++c)
                pcm[(size_t) (i * chans + c)] = (int16_t) clampv ((int) std::lround (src.ch[c][(size_t) i] * 32767.0f), -32768, 32767);

        akaizer::Settings st;
        st.stretchPercent = stretchPercent;
        st.cycleLength = clampv (set.cycleLength, 20, 2000);
        st.cycleFrequency = clampv (set.cycleFreq, 20, 2050);
        const auto r = akaizer::render (pcm.data(), pcm.size(), chans, src.sampleRate, st, clampv (set.pitch, -24.0, 24.0));
        if (r.samples.empty()) return false;

        auto out = std::make_shared<SampleData>();
        const int frames = (int) (r.samples.size() / (size_t) chans);
        out->allocate (chans, frames);
        out->sampleRate = src.sampleRate;
        for (int i = 0; i < frames; ++i)
            for (int c = 0; c < chans; ++c) out->ch[c][(size_t) i] = r.samples[(size_t) (i * chans + c)] / 32768.0f;
        const double newBpm = loop > 1.0 ? loop * 100.0 / stretchPercent : 0.0;
        char tag[48];
        std::snprintf (tag, sizeof tag, " AKAI %d", (int) std::lround (newBpm > 1.0 ? newBpm : stretchPercent));
        out->name = s.content->name + tag;
        out->buildOverview();

        // Renders/<name>_akaized.wav: 16-bit, the original's own WAV layout (never over an earlier render)
        std::string path;
        if (! dataFolder.empty())
        {
            std::error_code ec;
            fs::create_directories (fs::path (dataFolder) / "Renders", ec);
            std::string safe;
            for (char c : s.content->name) safe += (std::isalnum ((unsigned char) c) || c == ' ' || c == '-' || c == '_') ? c : '_';
            auto candidate = [&] (int n) {
                return (fs::path (dataFolder) / "Renders" / (safe + "_akaized" + (n > 1 ? " " + std::to_string (n) : std::string()) + ".wav")).string();
            };
            int n = 1;
            while (fs::exists (candidate (n), ec) && n < 999) ++n;
            path = candidate (n);
            const auto bytes = akaizer::makeWav (r.samples, chans, (int) std::lround (src.sampleRate));
            std::ofstream f (path, std::ios::binary);
            if (f && f.write ((const char*) bytes.data(), (std::streamsize) bytes.size())) out->path = path;
            else path.clear();
        }

        // Same hits, moved to where they land after the stretch; badges kept
        const double ratio = (double) frames / (double) std::max (1, src.numSamples);
        auto c = std::make_shared<SlotContent> (*s.content);
        c->buffer = out;
        c->name = out->name;
        for (int k = 0; k < c->slices.count; ++k) c->slices.starts[(size_t) k] = (int) std::lround (c->slices.starts[(size_t) k] * ratio);
        c->slices.normalise (frames);
        if (newBpm > 1.0) c->bpm = newBpm;

        // keep the slot's settings and pattern: install, then put them back
        const auto program = s.program;
        const auto current = s.current;
        const auto memories = s.memories;
        installContent (slot, c, path, false, nullptr);
        auto& t = slots[(size_t) slot];
        t.program.slice = program.slice;
        t.program.all = program.all;
        t.current = current;
        t.memories = memories;
        t.loopBpm = 0.0;     // the render's tempo is now the break's own
        pushHistory (t, t.current);
        publish (slot);
        return true;
    }

    std::shared_ptr<SampleData> Session::bounce (const Engine::Settings& settings, double bpm, double sampleRate)
    {
        std::lock_guard<std::recursive_mutex> l (mutex);
        const int slot = selectedSlot();
        const auto& s = slots[(size_t) slot];
        if (s.content == nullptr) return nullptr;
        const int steps = s.program.pattern.numSteps;
        const int total = (int) std::ceil (steps / 4.0 * 60.0 / bpm * sampleRate / std::max (0.25, settings.speed));

        auto e = std::make_unique<Engine>();
        e->prepare (sampleRate, 512);
        e->publishContent (slot, s.content);
        e->publishProgram (slot, s.program);
        e->selectedSlot = slot;

        auto out = std::make_shared<SampleData>();
        out->allocate (2, total);
        out->sampleRate = sampleRate;
        out->name = s.content->name;
        Engine::Host host;
        host.playing = true; host.hasInfo = true; host.bpm = bpm;
        for (int pos = 0; pos < total; pos += 512)
        {
            const int n = std::min (512, total - pos);
            host.ppq = pos * bpm / 60.0 / sampleRate;
            e->process (out->ch[0].data() + pos, out->ch[1].data() + pos, n, nullptr, 0, host, settings);
        }
        out->buildOverview();
        return out;
    }

    // ---- state -------------------------------------------------------------------------------------
    std::string Session::saveState() const
    {
        std::lock_guard<std::recursive_mutex> l (mutex);
        std::ostringstream o;
        o << "CHOPPAGE 1\n";
        o << "selected " << selectedSlot() << "\n";
        for (int i = 0; i < kSlots; ++i)
        {
            const auto& s = slots[(size_t) i];
            if (s.status == Status::empty) continue;
            o << "[slot " << i << "]\n";
            o << "demo " << (s.demo ? 1 : 0) << "\n";
            o << "path " << s.file << "\n";
            o << "grid " << (s.gridSlicing ? 1 : 0) << "\nsens " << s.sensitivity << "\ngridPerBar " << s.gridPerBar << "\n";
            o << "group " << s.muteGroup << "\n";
            if (s.loopBpm > 1.0) o << "loopBpm " << s.loopBpm << "\n";
            if (s.content != nullptr)
            {
                o << "bpm " << s.content->bpm << "\nbars " << s.content->bars << "\n";
                o << "slices " << s.content->slices.toString() << "\n";
                o << "badges ";
                for (int k = 0; k < s.content->numSlices(); ++k) o << badgeLetter (s.content->badges[(size_t) k]);
                o << "\nsliceParams ";
                for (int k = 0; k < s.content->numSlices(); ++k) o << (k ? ";" : "") << sliceParamsToString (s.program.slice[(size_t) k]);
                o << "\n";
            }
            SliceParams all;
            all.pitch = s.program.all.pitch; all.gate = s.program.all.gate; all.stretch = s.program.all.stretch;
            all.reverse = s.program.all.reverse; all.stutter = s.program.all.stutter; all.trans = s.program.all.trans;
            all.xfadeMs = s.program.all.xfadeMs;
            o << "all " << sliceParamsToString (all) << "\n";
            o << "pattern " << patternToString (s.current) << "\n";
            o << "showOriginal " << (s.showOriginal ? 1 : 0) << "\n";
            for (int m = 0; m < kMemories; ++m)
                if (s.memories[(size_t) m].has_value()) o << "mem" << m << " " << patternToString (*s.memories[(size_t) m]) << "\n";
        }
        return o.str();
    }

    void Session::loadState (const std::string& text)
    {
        if (text.rfind ("CHOPPAGE", 0) != 0) return;
        std::lock_guard<std::recursive_mutex> l (mutex);
        for (int i = 0; i < kSlots; ++i) clearSlot (i);

        int selected = 0;
        std::map<int, std::string> blocks;
        int cur = -1;
        std::stringstream ss (text);
        std::string line;
        while (std::getline (ss, line))
        {
            if (line.rfind ("[slot ", 0) == 0) { cur = clampv (std::atoi (line.c_str() + 6), 0, kSlots - 1); continue; }
            if (cur < 0) { if (line.rfind ("selected ", 0) == 0) selected = std::atoi (line.c_str() + 9); continue; }
            blocks[cur] += line + "\n";
        }

        for (auto& [i, block] : blocks)
        {
            auto kv = parseBlock (block);
            auto& s = slots[(size_t) i];
            s.gridSlicing = kv["grid"] == "1";
            if (kv.count ("sens")) s.sensitivity = (float) std::atof (kv["sens"].c_str());
            if (kv.count ("gridPerBar")) s.gridPerBar = std::atoi (kv["gridPerBar"].c_str());
            if (kv.count ("group")) s.muteGroup = clampv (std::atoi (kv["group"].c_str()), 0, kMuteGroups - 1);
            if (kv["demo"] == "1")
            {
                installContent (i, analyseBreak (DemoBreak::make (44100.0, 170.0), false, 0.62f, 16, &block), "", true, &block);
                continue;
            }
            s.status = Status::loading;
            s.file = kv["path"];
            {
                // a preset or project from another machine (or folder): the same file name in the breaks folder
                std::error_code ec;
                if (! s.file.empty() && ! std::filesystem::exists (s.file, ec) && ! browserDir.empty())
                {
                    const auto alt = std::filesystem::path (browserDir) / std::filesystem::path (s.file).filename();
                    if (std::filesystem::exists (alt, ec)) s.file = alt.string();
                }
            }
            ++pending;
            { std::lock_guard<std::mutex> q (qMutex); jobs.push_back ({ i, s.file, block, s.gridSlicing, s.sensitivity, s.gridPerBar }); }
            qCv.notify_one();
        }
        selectSlot (selected);
    }
}
