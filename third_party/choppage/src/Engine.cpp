#include "choppage/Engine.h"
#include <cmath>

namespace bc
{
    void Engine::prepare (double sampleRate, int block)
    {
        sr = sampleRate > 0.0 ? sampleRate : 44100.0;
        maxBlock = std::max (kChunk, block);

        for (auto& p : players)
            for (auto& v : p.voices) v.prepare (sr);
        perf.prepare (sr, kChunk);
        chain.prepare (sr, kChunk);
        reset();
    }

    void Engine::reset()
    {
        for (auto& p : players)
        {
            for (auto& v : p.voices) v.kill();
            const int slot = p.activeSlot;
            p = Player();   // voices are re-prepared below
            p.activeSlot = slot;
            for (auto& v : p.voices) v.prepare (sr);
        }
        perf.reset();
        chain.reset();
        internalPpq = 0.0;
        wasHostPlaying = false;
        autoPadLeft.fill (0);
        midiPad.fill (false);
        currentStep = -1;
        playingSlot = -1;
        for (auto& a : slotStep) a = -1;
        for (auto& a : slotSlice) a = -1;
    }

    void Engine::publishContent (int slot, ContentPtr c)
    {
        if (slot < 0 || slot >= kSlots) return;
        lock.lock();
        pendingContent[(size_t) slot] = std::move (c);
        pendingVersion.fetch_add (1);
        lock.unlock();
    }

    void Engine::publishProgram (int slot, const SlotProgram& p)
    {
        if (slot < 0 || slot >= kSlots) return;
        lock.lock();
        pendingProgram[(size_t) slot] = p;
        pendingVersion.fetch_add (1);
        lock.unlock();
    }

    void Engine::publishAudition (SamplePtr b)
    {
        lock.lock();
        pendingAudition = std::move (b);
        auditionVersion.fetch_add (1);
        lock.unlock();
    }

    void Engine::pullPublished() noexcept
    {
        const auto av = auditionVersion.load();
        if (av != auditionSeen && lock.tryLock())
        {
            audition = pendingAudition;
            auditionSeen = av;
            auditionPos = 0.0;
            auditionFade = 0.0f;
            lock.unlock();
        }
        const auto v = pendingVersion.load();
        if (v == seenVersion) return;
        if (! lock.tryLock()) return;   // next block
        // Ptr copies only bump counts; the message thread keeps every content alive,
        // so dropping the old one here never frees memory on the audio thread.
        content = pendingContent;
        program = pendingProgram;
        seenVersion = v;
        lock.unlock();
    }

    // ------------------------------------------------------------------------------------
    void Engine::playSlice (Player& pl, int slice, int atSample, int gateSamples, const Settings& s) noexcept
    {
        const auto& c = content[(size_t) pl.activeSlot];
        if (c == nullptr || c->buffer == nullptr) return;
        const auto& prog = program[(size_t) pl.activeSlot];
        slice = clampv (slice, 0, c->numSlices() - 1);
        const auto& sp = prog.slice[(size_t) slice];
        const auto& all = prog.all;
        if (sp.mute) return;

        SliceVoice::Start st;
        st.content = c;
        st.region = c->region (slice);

        double rate = c->buffer->sampleRate / sr;
        if (s.fit && ! s.keepPitch && ! s.midiTrigger)
        {
            const double hostBpm = lastHostBpm;
            rate *= hostBpm / std::max (20.0, c->bpm * s.tempoSourceMul);
        }
        rate *= std::pow (2.0, (sp.pitch + all.pitch) / 12.0);
        if (halfTape) rate *= 0.5;   // HALF-TIME 1/2 TAPE: as a tape at half speed
        st.rate = rate;
        st.reverse = sp.reverse != all.reverse;
        st.stutter = sp.stutter > 1 ? sp.stutter : all.stutter;
        st.stretch = clampv ((double) sp.stretch * (double) all.stretch, 0.25, 8.0);
        st.cycleLength = s.slCycleLength;
        st.cycleFreq = s.slCycleFreq;
        st.inTime = s.pads.akInTime;
        st.grainPitch = s.pads.akPitch;
        st.trans = clampv (sp.trans + (all.trans - 0.5f), 0.0f, 1.0f);
        const float gate = clampv (sp.gate * all.gate, 0.02f, 1.0f);
        st.gateSamples = gateSamples > 0 ? std::max (8, (int) (gateSamples * gate)) : (1 << 30);
        const float xf = std::max (sp.xfadeMs, all.xfadeMs);
        st.fadeSamples = std::max (8, (int) (xf * 0.001f * (float) sr));
        st.level = sp.level;

        // Mono per group: whatever this group was playing fades out over this slice's crossfade.
        renderVoices (renderedUpTo, atSample);
        for (auto& v : pl.voices) v.release (st.fadeSamples);
        auto& v = pl.voices[(size_t) pl.nextVoice];
        pl.nextVoice = (pl.nextVoice + 1) % (int) pl.voices.size();
        v.start (st);
        pl.slice = slice;
    }

    bool Engine::condition (Player& pl, int c, bool fxRow, int64_t cycle, const Settings& s) noexcept
    {
        if (c <= cond::always || c >= cond::kCount) return true;
        const int8_t same = fxRow ? pl.lastFxResult : pl.lastNoteResult;
        const int8_t other = fxRow ? pl.lastNoteResult : pl.lastFxResult;
        switch (c)
        {
            case cond::fill:     break;
            case cond::notFill:  break;
            case cond::pre:      return same == 1;                 // (no condition before: false, as on Elektron's)
            case cond::notPre:   return same == 0;
            case cond::nei:      return other == 1;
            case cond::notNei:   return other == 0;
            default: break;
        }
        bool r = true;
        if (c == cond::fill) r = s.fill;
        else if (c == cond::notFill) r = ! s.fill;
        else if (c == cond::first || c == cond::notFirst)
        {
            const bool firstTime = cycle == pl.firstCycle;
            r = c == cond::first ? firstTime : ! firstTime;
        }
        else if (cond::isPercent (c))
        {
            condRandom ^= condRandom << 13; condRandom ^= condRandom >> 17; condRandom ^= condRandom << 5;
            r = (int) (condRandom % 100u) < (int) cond::percents()[c - cond::percentBase];
        }
        else if (cond::isAB (c))
        {
            int a = 1, b = 2;
            cond::abOf (c, a, b);
            const int64_t k = cycle - pl.firstCycle;              // 0 = the first time round
            r = ((k % b) + b) % b == a - 1;
        }
        (fxRow ? pl.lastFxResult : pl.lastNoteResult) = r ? 1 : 0;
        return r;
    }

    void Engine::triggerStep (Player& pl, int64_t globalStep, int atSample, const Settings& s, double samplesPerStep) noexcept
    {
        const auto& prog = program[(size_t) pl.activeSlot];
        const auto& pat = prog.pattern;
        const int n = clampv (pat.numSteps, 1, kMaxSteps);
        const int step = (int) (((globalStep % n) + n) % n);
        const int64_t cycle = (globalStep - step) / n;               // how many times round the pattern
        if (pl.firstCycle == std::numeric_limits<int64_t>::min()) pl.firstCycle = cycle;
        pl.step = step;

        // FX lane: an event holds its effect for one step (tape stop: a beat) - if its FX PROB says so
        const auto& e = pat.fx[(size_t) step];
        if (e.type != FxType::none && condition (pl, pat.fxCond[(size_t) step], true, cycle, s))
        {
            const int len = (int) (samplesPerStep * (e.type == FxType::tapeStop ? 4.0 : 1.0));
            switch (e.type)
            {
                case FxType::stutter:  autoPadLeft[PerfFx::stutter] = len; autoStutterDiv = e.rate; break;
                case FxType::reverse:  autoPadLeft[PerfFx::reverse] = len; break;
                case FxType::stretch:  autoPadLeft[PerfFx::akaize] = len; break;
                case FxType::tapeStop: autoPadLeft[PerfFx::tapeStop] = len; break;
                case FxType::roll:     autoPadLeft[PerfFx::roll] = len; autoRollDiv = e.rate; break;   // (the step's RATE is the roll's)
                case FxType::none: break;
            }
        }

        const int slice = pat.slice[(size_t) step];
        if (slice < 0) return;
        if (! condition (pl, pat.noteCond[(size_t) step], false, cycle, s)) return;   // NOTE PROB: not this time
        // Gate: until the next hit in the pattern
        int gapSteps = 1;
        while (gapSteps < n && pat.slice[(size_t) ((step + gapSteps) % n)] < 0) ++gapSteps;
        playSlice (pl, slice, atSample, (int) (gapSteps * samplesPerStep), s);
    }

    void Engine::renderVoices (int from, int to) noexcept
    {
        if (to <= from) return;
        float* out[2] = { mix[0], mix[1] };
        for (auto& p : players)
            for (auto& v : p.voices)
                if (v.isActive()) v.render (out, 2, from, to - from);
        renderedUpTo = to;
    }

    void Engine::handleMidi (const MidiMsg& m, const Settings& s) noexcept
    {
        const int note = m.data[1];
        if (m.isNoteOn())
        {
            lastNote = note;
            const auto use = noteUse (note, s.noteMap);
            if (use.role == NoteRole::breakSlot)
            {
                const int slot = use.index;
                const int g = groupOf (slot);
                auto& p = players[(size_t) g];
                const bool sameAgain = slot == p.activeSlot && p.latched && p.heldBreakNote < 0;
                selectedSlot = slot;
                lastPlayer = g;
                p.heldBreakNote = note;
                if (s.gateMode == 0 && sameAgain) { p.latched = false; return; }   // LATCH: same key again stops
                p.latched = true;
                p.oneShot = s.gateMode == 2;
                p.oneShotArm = p.oneShot;    // the end is worked out from the pattern on the next step
                if (s.breakMode == 2 && p.wasRunning && slot != p.activeSlot) { p.pendingSlot = slot; return; }
                p.activeSlot = slot;
                p.startStepOffset = 0;
                if (s.velToSlice)
                {
                    const int n = program[(size_t) slot].pattern.numSteps;
                    p.startStepOffset = (m.data[2] * 16 / 128) * n / 16;
                }
                if (s.breakMode == 0 || s.velToSlice) { p.restartNow = true; p.restartFromNote = true; }
                else if (! p.wasRunning) p.restartNow = true;
            }
            else if (use.role == NoteRole::fxPad)
                midiPad[(size_t) use.index] = true;
            else if (use.role == NoteRole::slice)
                pendingSliceNote = use.index;
            else if (use.role == NoteRole::memory)
                memoryRequest = players[(size_t) lastPlayer].activeSlot * kMemories + use.index;
        }
        else if (m.isNoteOff())
        {
            const auto use = noteUse (note, s.noteMap);
            if (use.role == NoteRole::fxPad)
                midiPad[(size_t) use.index] = false;
            else
                for (auto& p : players)
                    if (note == p.heldBreakNote)
                    {
                        p.heldBreakNote = -1;
                        if (s.gateMode == 1) p.latched = false;   // HOLD; ONE SHOT ignores the note-off
                    }
        }
        else if (m.isAllOff())
        {
            midiPad.fill (false);
            for (auto& p : players)
            {
                p.heldBreakNote = -1;
                if (s.gateMode == 1) p.latched = false;
            }
        }
    }

    // ------------------------------------------------------------------------------------
    void Engine::process (float* outL, float* outR, int total, const MidiMsg* midi, int numMidi, const Host& host,
                          const Settings& sIn) noexcept
    {
        pullPublished();
        std::fill (outL, outL + total, 0.0f);
        std::fill (outR, outR + total, 0.0f);

        Settings s = sIn;
        const auto& sel = content[(size_t) clampv (players[(size_t) lastPlayer].activeSlot, 0, kSlots - 1)];
        const double bpm = host.hasInfo && host.bpm > 1.0 ? host.bpm
                         : (sel != nullptr ? sel->bpm * s.tempoSourceMul : 120.0);
        lastHostBpm = bpm;
        const double ppqPerSample = bpm / 60.0 / sr;
        s.pads.samplesPerBeat = sr * 60.0 / bpm;

        // The UI picked a different break: its group follows it (as a break note would, without starting it).
        const int ui = selectedSlot.load();
        const int uiGroup = ui >= 0 && ui < kSlots ? groupOf (ui) : 0;
        if (ui >= 0 && ui < kSlots)
        {
            auto& p = players[(size_t) uiGroup];
            if (ui != p.activeSlot && p.pendingSlot < 0 && p.heldBreakNote < 0)
            {
                p.activeSlot = ui;
                p.restartNow = s.breakMode == 0;
                if (p.oneShot) p.oneShotArm = true;
            }
        }

        if (! host.playing && wasHostPlaying)
            for (auto& p : players) { p.latched = false; p.oneShot = false; }    // host stop ends every latch
        if (host.playing && ! wasHostPlaying)
        {
            for (auto& p : players) p.restartNow = true;
            internalPpq = host.ppq;
        }
        wasHostPlaying = host.playing;

        int midiIt = 0;
        for (int c0 = 0; c0 < total; c0 += kChunk)
        {
            const int n = std::min (kChunk, total - c0);
            std::fill (mix[0], mix[0] + kChunk, 0.0f);
            std::fill (mix[1], mix[1] + kChunk, 0.0f);
            renderedUpTo = 0;

            for (int i = 0; i < n; ++i)
            {
                const int abs = c0 + i;
                while (midiIt < numMidi && midi[midiIt].offset <= abs)
                    handleMidi (midi[midiIt++], s);

                const double ppqNow = host.playing ? host.ppq + abs * ppqPerSample : internalPpq;
                const bool halfTime = s.pads.quantSteps > 0.0 ? padOn[PerfFx::halfTime]   // (quantised like the other pads)
                                                              : midiPad[PerfFx::halfTime] || uiPad[PerfFx::halfTime].load();
                static const double kHalf[] = { 0.5, 0.75, 2.0 / 3.0, 0.25, 0.5 };   // HALF-TIME MODE (as halfTimeFactor)
                const double speed = s.speed * (halfTime ? kHalf[clampv (s.halfMode, 0, 4)] : 1.0);
                halfTape = halfTime && s.halfMode == 4;
                const bool followHost = host.playing && ! halfTime;

                bool anyRunning = false;
                for (auto& p : players) anyRunning = anyRunning || p.wasRunning;

                for (int gi = 0; gi < kMuteGroups; ++gi)
                {
                    auto& p = players[(size_t) gi];
                    // A break plays only once its key has triggered it (LATCH: until the same key again, another
                    // break in its group or the host stopping; HOLD: while held; ONE SHOT: to the pattern's end) or
                    // the panel's PLAY is on (the selected break's group). The host's tempo and position time it,
                    // but the transport running doesn't start it on its own.
                    const bool running = ! s.midiTrigger && content[(size_t) p.activeSlot] != nullptr
                                         && (p.latched || (uiPlay.load() && gi == uiGroup));

                    // QUANT switch on the next beat line
                    if (p.pendingSlot >= 0)
                    {
                        const double beatPos = ppqNow - std::floor (ppqNow);
                        if (! running || beatPos < ppqPerSample) { p.activeSlot = p.pendingSlot; p.pendingSlot = -1; }
                    }

                    if (running)
                    {
                        const auto& pat = program[(size_t) p.activeSlot].pattern;
                        const int steps = clampv (pat.numSteps, 1, kMaxSteps);
                        if (! p.wasRunning || p.restartNow)
                        {
                            // BAR SYNC / QUANT: lock to the bar grid (the host's, or the internal clock's while
                            // another group is playing, so groups stay together); RESTART (or a note that asks): step 0
                            const bool grid = s.breakMode != 0 && ! p.restartFromNote && (host.playing || anyRunning);
                            p.restartFromNote = false;
                            p.stepPos = grid ? ppqNow * 4.0 * speed : (double) p.startStepOffset;
                            p.syncOffset = p.stepPos - ppqNow * 4.0 * speed;
                            p.lastStepGlobal = std::numeric_limits<int64_t>::min();
                            p.firstCycle = std::numeric_limits<int64_t>::min();   // (1ST: from here)
                            p.lastNoteResult = p.lastFxResult = -1;
                            p.restartNow = false;
                        }
                        else if (followHost)
                        {
                            // Follow the song position every sample: loop braces and relocates stay locked.
                            // (After HALF-TIME this also snaps back to where the grid is.)
                            p.stepPos = ppqNow * 4.0 * speed + p.syncOffset;
                        }

                        // which step (swung) are we in?
                        const double whole = std::floor (p.stepPos);
                        const double frac = p.stepPos - whole;
                        auto g = (int64_t) whole;
                        const double swingAt = (double) (s.swing - 0.5f) * 2.0;   // 0 .. 0.5 of a step
                        if ((g & 1) != 0 && frac < swingAt) --g;
                        bool stopNow = false;
                        if (g != p.lastStepGlobal)
                        {
                            p.lastStepGlobal = g;
                            const int idx = (int) (((g % steps) + steps) % steps);
                            if (p.oneShotArm) { p.oneShotEnd = g - idx + steps; p.oneShotArm = false; }   // this pattern's end
                            if (p.oneShot && g >= p.oneShotEnd) stopNow = true;
                            else
                            {
                                const double samplesPerStep = sr * 60.0 / bpm / 4.0 / speed;
                                triggerStep (p, g, i, s, samplesPerStep);
                            }
                        }
                        if (stopNow)
                        {
                            // ONE SHOT reached the end of the pattern
                            p.latched = false;
                            p.oneShot = false;
                            renderVoices (renderedUpTo, i);
                            for (auto& v : p.voices) v.release ((int) (0.005 * sr));
                            p.step = -1;
                            p.wasRunning = false;
                            continue;
                        }
                        if (! followHost) p.stepPos += ppqPerSample * 4.0 * speed;
                    }
                    else if (p.wasRunning)
                    {
                        renderVoices (renderedUpTo, i);
                        for (auto& v : p.voices) v.release ((int) (0.005 * sr));
                        p.step = -1;
                    }
                    p.wasRunning = running;
                }

                // Slice notes (on the group played last) / a slice picked on screen (on the selected break's group):
                // one-shots over the top
                int one = pendingSliceNote, group = lastPlayer;
                const bool fromPad = one >= 0;
                pendingSliceNote = -1;
                if (one < 0) { one = previewSlice.exchange (-1); group = uiGroup; }
                if (one >= 0)
                {
                    auto& p = players[(size_t) group];
                    if (content[(size_t) p.activeSlot] != nullptr)
                    {
                        playSlice (p, one, i, 0, s);
                        if (fromPad) sliceHit.store (p.activeSlot * kMaxSlices + one, std::memory_order_relaxed);
                    }
                }

                if (! host.playing) internalPpq += ppqPerSample;
            }
            renderVoices (renderedUpTo, n);

            // FX pads: MIDI, UI and the FX lane
            std::array<bool, kFxPads> held {}, pressed {};
            const double ppqHere0 = host.playing ? host.ppq + c0 * ppqPerSample : internalPpq;
            {
                // PAD QUANTISE: a press waits for the next grid line (it starts in the chunk the line falls in or
                // just after, so STUTTER's loop starts on the line); letting go ends it on the next line; a tap
                // shorter than the grid still plays one grid's worth. The FX lane is on the grid already.
                const double q = s.pads.quantSteps;
                const double pos16 = ppqHere0 * 4.0, chunk16 = n * ppqPerSample * 4.0;
                const bool onLine = q > 0.0 && (pos16 - std::floor (pos16 / q) * q) < chunk16;
                for (int p = 0; p < kFxPads; ++p)
                {
                    const size_t i = (size_t) p;
                    const bool raw = midiPad[i] || uiPad[i].load();
                    pressed[i] = raw;
                    if (q <= 0.0) { padOn[i] = raw; padArmed[i] = padStopArmed[i] = padTap[i] = false; }
                    else
                    {
                        if (raw && ! padWas[i]) { if (padOn[i]) padStopArmed[i] = false; else { padArmed[i] = true; padTap[i] = false; } }
                        if (! raw && padWas[i]) { if (padOn[i]) padStopArmed[i] = true; else if (padArmed[i]) padTap[i] = true; }
                        if (padOn[i] && padStopArmed[i] && onLine) { padOn[i] = false; padStopArmed[i] = false; }
                        else if (padArmed[i] && onLine)
                        {
                            padOn[i] = true; padArmed[i] = false;
                            if (padTap[i] || ! raw) { padStopArmed[i] = true; padTap[i] = false; }
                        }
                    }
                    padWas[i] = raw;
                    const bool lane = autoPadLeft[i] > 0;
                    held[i] = padOn[i] || lane;
                    if (lane) autoPadLeft[i] -= n;
                }
            }
            PerfFx::Settings ps = s.pads;
            if (autoPadLeft[PerfFx::stutter] > 0 && ! midiPad[PerfFx::stutter] && ! uiPad[PerfFx::stutter].load())
                ps.stutterDiv = autoStutterDiv;
            if (autoPadLeft[PerfFx::roll] > 0 && ! midiPad[PerfFx::roll] && ! uiPad[PerfFx::roll].load())
                ps.rollDiv = autoRollDiv;
            const double samplesPerStep = sr * 60.0 / bpm / 4.0;
            const double ppqHere = host.playing ? host.ppq + c0 * ppqPerSample : internalPpq;
            ps.samplesSinceGrid = (ppqHere * 4.0 - std::floor (ppqHere * 4.0)) * samplesPerStep;
            perf.process (mix[0], mix[1], n, held, ps);

            SoundChain::Settings cs = s.sound;
            cs.dropHeld = held[PerfFx::s950Drop];
            chain.process (mix[0], mix[1], n, cs);

            for (int p = 0; p < kFxPads; ++p)
            {
                padLit[(size_t) p] = held[(size_t) p] || pressed[(size_t) p] || perf.isAudible (p);
                padActive[(size_t) p] = held[(size_t) p];
            }

            // what plays where, for the screens
            {
                std::array<int, kSlots> st, sl;
                st.fill (-1); sl.fill (-1);
                int recent = -1;
                for (int gi = 0; gi < kMuteGroups; ++gi)
                {
                    const auto& p = players[(size_t) gi];
                    if (! p.wasRunning) continue;
                    st[(size_t) p.activeSlot] = p.step;
                    sl[(size_t) p.activeSlot] = p.slice;
                    if (gi == lastPlayer || recent < 0) recent = gi;
                }
                for (int k = 0; k < kSlots; ++k) { slotStep[(size_t) k] = st[(size_t) k]; slotSlice[(size_t) k] = sl[(size_t) k]; }
                if (recent >= 0)
                {
                    const auto& p = players[(size_t) recent];
                    playingSlot = p.activeSlot; currentStep = p.step; currentSlice = p.slice;
                }
                else { playingSlot = -1; currentStep = -1; }
            }

            // browser audition: dry, over everything, looped at the file's own speed
            if (audition != nullptr && audition->numSamples > 1)
            {
                const auto& b = *audition;
                const double step = b.sampleRate / sr;
                const float* L = b.ch[0].data();
                const float* R = b.channels > 1 ? b.ch[1].data() : L;
                for (int k = 0; k < n; ++k)
                {
                    const int i0 = (int) auditionPos;
                    const int i1 = i0 + 1 < b.numSamples ? i0 + 1 : 0;
                    const float t = (float) (auditionPos - i0);
                    auditionFade = std::min (1.0f, auditionFade + 1.0f / 256.0f);
                    const float g = 0.8f * auditionFade;
                    mix[0][k] += g * (L[i0] + (L[i1] - L[i0]) * t);
                    mix[1][k] += g * (R[i0] + (R[i1] - R[i0]) * t);
                    auditionPos += step;
                    if (auditionPos >= b.numSamples) auditionPos -= b.numSamples;
                }
            }

            std::copy (mix[0], mix[0] + n, outL + c0);
            std::copy (mix[1], mix[1] + n, outR + c0);
        }
    }
}
