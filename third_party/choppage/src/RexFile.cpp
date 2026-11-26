// REX2 loops (.rx2 / .rex from ReCycle): decoded with VelociLoops (public domain, third_party/velociloops), the
// slices put back on their places in the loop, and the file's slice points and length in bars handed to the
// analysis so CHOPPAGE uses ReCycle's chops instead of finding its own.
#include "choppage/AudioFile.h"
#include "choppage/Model.h"
#include "../third_party/velociloops/velociloops.cpp"
#include <algorithm>
#include <cmath>

namespace bc::AudioFile
{
    bool isRexFile (const std::string& path)
    {
        auto dot = path.find_last_of ('.');
        if (dot == std::string::npos) return false;
        std::string e = path.substr (dot + 1);
        for (auto& c : e) c = (char) std::tolower ((unsigned char) c);
        return e == "rx2" || e == "rex";
    }

    std::shared_ptr<SampleData> loadRex (const std::string& path, Status& status, double maxSeconds)
    {
        VLError err = VL_OK;
        VLFile f = vl_open (path.c_str(), &err);
        if (f == nullptr) { status = err == VL_ERROR_FILE_NOT_FOUND ? Status::missing : Status::unreadable; return nullptr; }
        struct Closer { VLFile f; ~Closer() { vl_close (f); } } closer { f };

        VLFileInfo info {};
        if (vl_get_info (f, &info) != VL_OK || info.sample_rate <= 0 || info.slice_count <= 0) { status = Status::unreadable; return nullptr; }
        int loop0 = std::max (0, info.loop_start), loop1 = info.loop_end > info.loop_start ? info.loop_end : info.total_frames;
        loop1 = std::min (loop1, info.total_frames);
        if (loop1 - loop0 < 64) { loop0 = 0; loop1 = info.total_frames; }   // (readRexInfo below takes the same span)
        const int frames = loop1 - loop0;
        if (frames <= 0) { status = Status::empty; return nullptr; }
        if (frames > info.sample_rate * maxSeconds) { status = Status::tooLong; return nullptr; }

        std::vector<VLSliceInfo> slices;
        for (int i = 0; i < info.slice_count; ++i)
        {
            VLSliceInfo s {};
            if (vl_get_slice_info (f, i, &s) == VL_OK && s.sample_start - loop0 < frames && s.sample_start + s.sample_length > loop0)
                slices.push_back (s);
        }
        if (slices.empty()) { status = Status::empty; return nullptr; }
        std::sort (slices.begin(), slices.end(), [] (const VLSliceInfo& a, const VLSliceInfo& b) { return a.sample_start < b.sample_start; });

        auto d = std::make_shared<SampleData>();
        d->allocate (info.channels >= 2 ? 2 : 1, frames);
        d->sampleRate = info.sample_rate;
        d->path = path;
        d->name = baseName (path);

        // each slice, as ReCycle renders it, at its place in the loop (cut where the next one starts)
        std::vector<float> L, R;
        std::vector<int> starts;
        for (size_t k = 0; k < slices.size(); ++k)
        {
            const auto& s = slices[k];
            const int idx = (int) (&s - slices.data());
            (void) idx;
            int32_t index = -1;
            for (int i = 0; i < info.slice_count; ++i)
            {
                VLSliceInfo t {};
                if (vl_get_slice_info (f, i, &t) == VL_OK && t.sample_start == s.sample_start && t.ppq_pos == s.ppq_pos) { index = i; break; }
            }
            if (index < 0) continue;
            const int n = vl_get_slice_frame_count (f, index);
            if (n <= 0) continue;
            L.assign ((size_t) n, 0.0f); R.assign ((size_t) n, 0.0f);
            int32_t got = 0;
            if (vl_decode_slice (f, index, L.data(), R.data(), 0, n, &got) != VL_OK) continue;
            const int at = std::max (0, s.sample_start - loop0);
            const int end = k + 1 < slices.size() ? std::max (at, slices[k + 1].sample_start - loop0) : frames;
            const int count = std::min ({ (int) got, end - at, frames - at });
            for (int i = 0; i < count; ++i)
            {
                d->ch[0][(size_t) (at + i)] = L[(size_t) i];
                if (d->channels > 1) d->ch[1][(size_t) (at + i)] = R[(size_t) i];
            }
            starts.push_back (at);
        }
        if (starts.empty()) { status = Status::unreadable; return nullptr; }
        starts.erase (std::unique (starts.begin(), starts.end()), starts.end());
        if (starts.front() != 0) starts.insert (starts.begin(), 0);
        if ((int) starts.size() > kMaxSlices)
        {
            // keep an even spread of ReCycle's points
            std::vector<int> fewer;
            for (int k = 0; k < kMaxSlices; ++k) fewer.push_back (starts[(size_t) ((int64_t) k * (int64_t) starts.size() / kMaxSlices)]);
            starts = std::move (fewer);
        }
        d->fileMarkers = std::move (starts);
        // REX PPQ: 15360 a quarter note. The audio is at ReCycle's original tempo: the tempo is worked out from it.
        const double beats = std::max (1.0, info.ppq_length / 15360.0);
        d->fileBars = std::max (1, (int) std::lround (beats / 4.0));
        d->fileBpm = 60.0 * beats / std::max (1e-6, d->lengthSeconds());
        d->buildOverview();
        status = Status::ok;
        return d;
    }

    Info readRexInfo (const std::string& path)
    {
        Info i;
        VLError err = VL_OK;
        VLFile f = vl_open (path.c_str(), &err);
        if (f == nullptr) return i;
        VLFileInfo info {};
        if (vl_get_info (f, &info) == VL_OK && info.sample_rate > 0)
        {
            const int loop1 = info.loop_end > info.loop_start ? std::min (info.loop_end, info.total_frames) : info.total_frames;
            i.ok = true;
            i.channels = info.channels;
            i.sampleRate = info.sample_rate;
            i.frames = std::max (0, loop1 - std::max (0, info.loop_start));
            const double beats = std::max (1.0, info.ppq_length / 15360.0);
            i.bars = std::max (1, (int) std::lround (beats / 4.0));
            if (i.frames > 0) i.bpm = 60.0 * beats * info.sample_rate / (double) i.frames;
        }
        vl_close (f);
        return i;
    }
}
