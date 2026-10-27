// Paula's Revenge - replayer and Paula-style output stage
#include "Player.h"

namespace prv
{
namespace
{
    constexpr double kMinPeriod = 14.0;
    constexpr double kMaxPeriod = 7000.0;

    const uint8_t kSine[32] = { 0, 24, 49, 74, 97, 120, 141, 161, 180, 197, 212, 224, 235, 244, 250, 253,
                                255, 253, 250, 244, 235, 224, 212, 197, 180, 161, 141, 120, 97, 74, 49, 24 };

    // transparent below 0.8, smoothly limits towards 1.0 above
    inline float softClip (float x)
    {
        const float a = std::abs (x);
        if (a <= 0.8f) return x;
        const float over = (a - 0.8f) / 0.2f;
        return std::copysign (0.8f + 0.2f * over / (1.0f + over), x);
    }

    inline float quant8 (float s) { return std::round (std::clamp (s, -1.0f, 1.0f) * 127.0f) / 127.0f; }
}

Player::Player() { resetVoices(); }

void Player::setSampleRate (double sr)
{
    sampleRate = sr > 0 ? sr : 44100.0;
    updateFilters();
    lpL.reset(); lpR.reset(); hpL.reset(); hpR.reset(); ledL.reset(); ledR.reset();
}

void Player::updateFilters()
{
    lastModel = options.model;
    const double nyq = sampleRate * 0.45;
    // A500: fixed RC lowpass ~4.4 kHz. A1200: ~34 kHz (effectively open).
    const double lpHz = options.model == PaulaOptions::Model::A500 ? 4420.0 : 34000.0;
    lpL.setLowpass (std::min (lpHz, nyq), sampleRate); lpR = lpL;
    hpL.setLowpass (5.2, sampleRate); hpR = hpL;
    // "LED" filter: 2-pole Butterworth-ish lowpass ~3.1 kHz, Q 0.66
    ledL.setLowpass (3091.0, 0.660, sampleRate); ledR = ledL;
}

void Player::resetVoices()
{
    for (auto& v : voices) v = Voice{};
}

void Player::playSong (int pos, int startLine)
{
    if (song == nullptr || song->sequence.empty()) return;
    for (int i = 0; i < kMaxTracks; ++i) voices[i] = Voice{};
    seqPos = std::clamp (pos, 0, (int) song->sequence.size() - 1);
    block  = song->safeBlock (song->sequence[(size_t) seqPos]);
    line   = std::clamp (startLine, 0, song->blocks[(size_t) block].lines - 1);
    tick = 0; tickRemain = 0;
    speed = std::clamp (song->speed, 1, 31);
    tempo = std::clamp (song->tempo, 32, 255);
    ledOn = song->ledFilterAtStart;
    breakLine = jumpPos = loopJumpLine = -1; rowDelayCount = 0; firstTickOfRow = true;
    mode = Mode::Song;
}

void Player::playBlock (int b, int startLine)
{
    if (song == nullptr || song->blocks.empty()) return;
    for (int i = 0; i < kMaxTracks; ++i) voices[i] = Voice{};
    block = song->safeBlock (b);
    line  = std::clamp (startLine, 0, song->blocks[(size_t) block].lines - 1);
    tick = 0; tickRemain = 0;
    speed = std::clamp (song->speed, 1, 31);
    tempo = std::clamp (song->tempo, 32, 255);
    ledOn = song->ledFilterAtStart;
    breakLine = jumpPos = loopJumpLine = -1; rowDelayCount = 0; firstTickOfRow = true;
    mode = Mode::Block;
}

void Player::stop()
{
    mode = Mode::Stopped;
    for (int i = 0; i < kMaxTracks; ++i) voices[i].active = false;
}

// ---------------------------------------------------------------------------
void Player::jamNoteOn (int note, int instrIndex, int velocity127)
{
    if (song == nullptr || instrIndex < 0 || instrIndex >= kMaxInstruments) return;
    const auto& in = song->instruments[(size_t) instrIndex];
    if (! in.hasData()) return;

    Voice& v = voices[kMaxTracks + jamNext];
    jamNext = (jamNext + 1) % kJamVoices;
    v = Voice{};
    v.instr = instrIndex;
    v.finetune = in.finetune;
    v.note = std::clamp (note + in.transpose, 1, (int) kNoteMax);
    v.period = v.outPeriod = periodFor (v.note, v.finetune);
    v.volume = v.outVolume = std::clamp (in.volume * velocity127 / 127, 0, 64);
    v.jamNote = note;
    v.active = true;
}

void Player::jamNoteOff (int note)
{
    for (int i = kMaxTracks; i < kVoices; ++i)
        if (voices[i].jamNote == note) { voices[i].active = false; voices[i].jamNote = -1; }
}

void Player::jamAllOff()
{
    for (int i = kMaxTracks; i < kVoices; ++i) { voices[i].active = false; voices[i].jamNote = -1; }
}

// ---------------------------------------------------------------------------
double Player::samplesPerTick() const
{
    const double bpm = tempoOverride > 0 ? tempoOverride : (double) tempo;
    return sampleRate * 2.5 / std::max (1.0, bpm);
}

void Player::doTick()
{
    if (song == nullptr) return;

    // keep positions valid if the song was edited while playing
    if (mode == Mode::Song)
    {
        if (song->sequence.empty()) { stop(); return; }
        if (seqPos >= (int) song->sequence.size()) seqPos = 0;
        block = song->safeBlock (song->sequence[(size_t) seqPos]);
    }
    block = song->safeBlock (block);
    if (line >= song->blocks[(size_t) block].lines) line = 0;

    if (tick == 0)
    {
        if (firstTickOfRow) processRow();
        else for (int ch = 0; ch < kMaxTracks; ++ch) tickEffects (ch);
    }
    else
    {
        for (int ch = 0; ch < kMaxTracks; ++ch) tickEffects (ch);
    }

    if (mode == Mode::Stopped) return;

    if (++tick >= speed)
    {
        tick = 0;
        advanceLine();
    }
}

void Player::processRow()
{
    breakLine = jumpPos = loopJumpLine = -1;
    const Block& b = song->blocks[(size_t) block];
    for (int ch = 0; ch < kMaxTracks; ++ch)
    {
        if (ch < song->numTracks) processCell (ch, b.at (line, ch));
        else voices[ch].active = false;
    }
    uiSeqPos = seqPos; uiBlock = block; uiLine = line;
    uiSpeed = speed; uiTempo = tempo; uiLed = ledOn;
}

void Player::triggerNote (Voice& v, const Cell& cell, int)
{
    if (v.instr < 0) { v.active = false; return; }
    const auto& in = song->instruments[(size_t) v.instr];
    if (! in.hasData()) { v.active = false; return; }

    v.note = std::clamp ((int) cell.note + in.transpose, 1, (int) kNoteMax);
    v.period = v.outPeriod = v.targetPeriod = periodFor (v.note, v.finetune);
    v.pos = 0;
    if (cell.cmd == 0x9)
    {
        if (cell.param) v.offsetMem = cell.param;
        v.pos = v.offsetMem * 256.0;
        if (v.pos >= (double) in.data.size())
        {
            if (in.isLooped()) v.pos = in.loopStart;
            else { v.active = false; return; }
        }
    }
    if (v.vibWave < 4)  v.vibPos = 0;
    if (v.tremWave < 4) v.tremPos = 0;
    v.active = true;
}

void Player::processCell (int ch, const Cell& cell)
{
    Voice& v = voices[ch];
    v.cmd = cell.cmd; v.param = cell.param;
    const int x = cell.param >> 4, y = cell.param & 15;
    v.delayTick = -1;

    const bool delayed = (cell.cmd == 0xE && x == 0xD && y > 0 && (cell.note != 0 || cell.instr != 0));
    if (delayed)
    {
        v.delayTick = y; v.delayedCell = cell;
    }
    else
    {
        if (cell.instr > 0 && cell.instr <= kMaxInstruments)
        {
            const auto& in = song->instruments[(size_t) cell.instr - 1];
            v.instr = cell.instr - 1;
            v.volume = in.volume;
            v.finetune = in.finetune;
        }
        if (cell.note == kNoteOff)
        {
            v.active = false;
        }
        else if (cell.note > 0 && cell.note <= kNoteMax)
        {
            if ((cell.cmd == 0x3 || cell.cmd == 0x5) && v.active && v.instr >= 0)
            {
                const auto& in = song->instruments[(size_t) v.instr];
                v.targetPeriod = periodFor (std::clamp ((int) cell.note + in.transpose, 1, (int) kNoteMax), v.finetune);
            }
            else
            {
                triggerNote (v, cell, ch);
            }
        }
    }

    switch (cell.cmd)
    {
        case 0x3: if (cell.param) v.portaSpeed = cell.param; break;
        case 0x4: if (x) v.vibSpeed = (uint8_t) x; if (y) v.vibDepth = (uint8_t) y; break;
        case 0x7: if (x) v.tremSpeed = (uint8_t) x; if (y) v.tremDepth = (uint8_t) y; break;
        case 0x8: v.pan = cell.param; break;
        case 0xB: jumpPos = cell.param; break;
        case 0xC: v.volume = std::min ((int) cell.param, 64); break;
        case 0xD: breakLine = x * 10 + y; break;
        case 0xE:
            switch (x)
            {
                case 0x0: ledOn = (y == 0); break;
                case 0x1: v.period = std::max (kMinPeriod, v.period - y); break;
                case 0x2: v.period = std::min (kMaxPeriod, v.period + y); break;
                case 0x4: v.vibWave = (uint8_t) y; break;
                case 0x5: v.finetune = y > 7 ? y - 16 : y; break;
                case 0x6:
                    if (y == 0) v.loopRow = line;
                    else if (v.loopCount == 0) { v.loopCount = y; loopJumpLine = v.loopRow; }
                    else if (--v.loopCount > 0) loopJumpLine = v.loopRow;
                    break;
                case 0x7: v.tremWave = (uint8_t) y; break;
                case 0x8: v.pan = y * 17; break;
                case 0xA: v.volume = std::min (64, v.volume + y); break;
                case 0xB: v.volume = std::max (0, v.volume - y); break;
                case 0xC: if (y == 0) v.volume = 0; break;
                case 0xE: rowDelay = y; break;
                default: break;
            }
            break;
        case 0xF:
            if (cell.param == 0) { stop(); ++songWraps; return; }
            if (cell.param < 0x20) speed = cell.param; else tempo = cell.param;
            break;
        default: break;
    }
    v.outPeriod = v.period;
    v.outVolume = v.volume;
}

double Player::vibratoDelta (uint8_t pos, uint8_t depth, uint8_t wave, int shift) const
{
    const int idx = pos & 31;
    int val = 0;
    switch (wave & 3)
    {
        case 0: val = kSine[idx]; break;
        case 1: val = (pos & 32) ? 255 - idx * 8 : idx * 8; break;
        default: val = 255; break;
    }
    double d = (double) (val * depth >> shift);
    return (pos & 32) ? -d : d;
}

void Player::tickEffects (int ch)
{
    Voice& v = voices[ch];
    const int x = v.param >> 4, y = v.param & 15;

    if (v.delayTick >= 0 && tick == v.delayTick)
    {
        v.delayTick = -1;
        Cell c = v.delayedCell; c.cmd = 0; c.param = 0;
        if (c.instr > 0 && c.instr <= kMaxInstruments)
        {
            v.instr = c.instr - 1;
            v.volume = song->instruments[(size_t) v.instr].volume;
            v.finetune = song->instruments[(size_t) v.instr].finetune;
        }
        if (c.note == kNoteOff) v.active = false;
        else if (c.note > 0) triggerNote (v, c, ch);
    }

    auto volSlide = [&] { if (x) v.volume = std::min (64, v.volume + x); else v.volume = std::max (0, v.volume - y); };
    auto porta = [&]
    {
        if (v.targetPeriod <= 0) return;
        if (v.period < v.targetPeriod) v.period = std::min (v.targetPeriod, v.period + v.portaSpeed);
        else                           v.period = std::max (v.targetPeriod, v.period - v.portaSpeed);
    };

    double vib = 0; int trem = 0; int arpSemis = 0;
    switch (v.cmd)
    {
        case 0x0: if (v.param) arpSemis = (tick % 3 == 1) ? x : (tick % 3 == 2 ? y : 0); break;
        case 0x1: v.period = std::max (kMinPeriod, v.period - v.param); break;
        case 0x2: v.period = std::min (kMaxPeriod, v.period + v.param); break;
        case 0x3: porta(); break;
        case 0x4: vib = vibratoDelta (v.vibPos, v.vibDepth, v.vibWave, 7); v.vibPos = (uint8_t) ((v.vibPos + v.vibSpeed) & 63); break;
        case 0x5: porta(); volSlide(); break;
        case 0x6: vib = vibratoDelta (v.vibPos, v.vibDepth, v.vibWave, 7); v.vibPos = (uint8_t) ((v.vibPos + v.vibSpeed) & 63); volSlide(); break;
        case 0x7: trem = (int) vibratoDelta (v.tremPos, v.tremDepth, v.tremWave, 6); v.tremPos = (uint8_t) ((v.tremPos + v.tremSpeed) & 63); break;
        case 0xA: volSlide(); break;
        case 0xE:
            if (x == 0x9 && y > 0 && tick % y == 0 && v.instr >= 0 && song->instruments[(size_t) v.instr].hasData()) { v.pos = 0; v.active = true; }
            if (x == 0xC && tick == y) v.volume = 0;
            break;
        default: break;
    }

    v.outPeriod = std::clamp (v.period + vib, kMinPeriod, kMaxPeriod);
    if (arpSemis) v.outPeriod = std::max (kMinPeriod, v.period * std::pow (2.0, -arpSemis / 12.0));
    v.outVolume = std::clamp (v.volume + trem, 0, 64);
}

void Player::advanceLine()
{
    if (rowDelay > 0) { rowDelayCount = rowDelay; rowDelay = 0; }
    if (rowDelayCount > 0) { --rowDelayCount; firstTickOfRow = false; return; }
    firstTickOfRow = true;

    const int lines = song->blocks[(size_t) block].lines;
    const int seqLen = (int) song->sequence.size();

    if (loopJumpLine >= 0)
    {
        line = std::clamp (loopJumpLine, 0, lines - 1);
    }
    else if (jumpPos >= 0 || breakLine >= 0)
    {
        if (mode == Mode::Song)
        {
            const int prev = seqPos;
            seqPos = jumpPos >= 0 ? jumpPos : seqPos + 1;
            if (seqPos >= seqLen || seqPos < 0) seqPos = 0;
            if (seqPos <= prev) ++songWraps;
            block = song->safeBlock (song->sequence[(size_t) seqPos]);
        }
        line = breakLine >= 0 ? std::clamp (breakLine, 0, song->blocks[(size_t) block].lines - 1) : 0;
    }
    else if (++line >= lines)
    {
        line = 0;
        if (mode == Mode::Song)
        {
            if (++seqPos >= seqLen) { seqPos = 0; ++songWraps; }
            block = song->safeBlock (song->sequence[(size_t) seqPos]);
        }
    }
    breakLine = jumpPos = loopJumpLine = -1;
    uiSeqPos = seqPos; uiBlock = block; uiLine = line;
}

// ---------------------------------------------------------------------------
void Player::renderVoice (Voice& v, double inc, float& outL, float& outR)
{
    outL = outR = 0.0f;
    if (! v.active || v.instr < 0 || song == nullptr) return;
    const auto& in = song->instruments[(size_t) v.instr];
    const auto& d = in.data;
    if (d.empty()) { v.active = false; return; }
    const bool stereo = in.isStereo();
    const auto& dr = stereo ? in.dataR : in.data;

    const bool looped = in.isLooped();
    const double end = looped ? (double) (in.loopStart + in.loopLength) : (double) d.size();
    const bool eight = options.eightBit;
    const int last = (int) d.size() - 1;

    auto conv = [eight] (int s) -> float
    {
        if (eight) s = (s >> 8) * 256;
        return (float) s / 32768.0f;
    };
    auto wrap = [&] (double& p) -> bool
    {
        if (p < end) return true;
        if (! looped) return false;
        while (p >= end) p -= in.loopLength;
        return true;
    };

    float L = 0.0f, R = 0.0f;
    if (options.zeroOrderHold)
    {
        // Integrate the stepped (sample-and-hold) waveform across this output
        // sample: keeps Paula's stepped character but tames host-rate aliasing.
        double remaining = inc, accL = 0.0, accR = 0.0, p = v.pos;
        bool alive = true;
        int guard = 0;
        while (remaining > 1e-12 && guard++ < 4096)
        {
            const double fl = std::floor (p);
            const double step = std::min (fl + 1.0 - p, remaining);
            const size_t idx = (size_t) std::clamp ((int) fl, 0, last);
            accL += conv (d[idx]) * step;
            if (stereo) accR += conv (dr[idx]) * step;
            p += step; remaining -= step;
            if (! wrap (p)) { alive = false; break; }
        }
        L = (float) (accL / inc);
        R = stereo ? (float) (accR / inc) : L;
        v.pos = p;
        if (! alive) v.active = false;
    }
    else
    {
        const int i = std::clamp ((int) v.pos, 0, last);
        const float frac = (float) (v.pos - std::floor (v.pos));
        int n = i + 1;
        if (n >= (int) end) n = looped ? in.loopStart : i;
        n = std::clamp (n, 0, last);
        const float a = conv (d[(size_t) i]), b = conv (d[(size_t) n]);
        L = a + (b - a) * frac;
        if (stereo)
        {
            const float ar = conv (dr[(size_t) i]), br = conv (dr[(size_t) n]);
            R = ar + (br - ar) * frac;
        }
        else R = L;
        v.pos += inc;
        if (! wrap (v.pos)) v.active = false;
    }
    const float g = (float) v.outVolume / 64.0f;
    outL = L * g;
    outR = R * g;
}

void Player::render (float* left, float* right, int numSamples)
{
    if (options.model != lastModel) updateFilters();

    float peaks[kMaxTracks] = {};
    const int nt = song ? std::clamp (song->numTracks, 1, kMaxTracks) : 0;
    const float sep = std::clamp (options.stereoSep, 0.0f, 1.0f);

    auto addPanned = [&] (float s, float pos, float& L, float& R)
    {
        L += s * (1.0f - pos) * 0.5f;
        R += s * (1.0f + pos) * 0.5f;
    };
    auto sidePan = [&] (int hw) { return ((hw == 0 || hw == 3) ? -1.0f : 1.0f) * sep; };
    auto overridePan = [&] (const Voice& v) { return ((float) v.pan / 127.5f - 1.0f) * sep; };

    for (int i = 0; i < numSamples; ++i)
    {
        if (mode != Mode::Stopped && song != nullptr)
        {
            tickRemain -= 1.0;
            if (tickRemain <= 0.0)
            {
                doTick();
                tickRemain += samplesPerTick();
            }
        }

        float valL[kVoices] = {}, valR[kVoices] = {};
        bool  st[kVoices] = {};
        if (song != nullptr)
        {
            for (int ch = 0; ch < kVoices; ++ch)
            {
                Voice& v = voices[ch];
                if (! v.active) continue;
                const double inc = kPaulaClock / std::max (kMinPeriod, v.outPeriod) / sampleRate;
                float sl, sr;
                renderVoice (v, inc, sl, sr);
                if (ch < kMaxTracks)
                {
                    if (mute[ch].load (std::memory_order_relaxed) || ch >= nt) continue;
                    peaks[ch] = std::max ({ peaks[ch], std::abs (sl), std::abs (sr) });
                }
                valL[ch] = sl; valR[ch] = sr;
                st[ch] = v.instr >= 0 && song->instruments[(size_t) v.instr].isStereo();
            }
        }

        // Mono voices follow Amiga panning (or 8xx). Stereo samples keep their
        // own stereo image, centred unless the track is panned with 8xx.
        auto addVoice = [&] (int ch, float scale, float sidePos, float& L, float& R)
        {
            const Voice& v = voices[ch];
            if (st[ch])
            {
                const float p = v.pan >= 0 ? overridePan (v) : 0.0f;
                L += valL[ch] * scale * 0.7f * std::min (1.0f, 1.0f - p);
                R += valR[ch] * scale * 0.7f * std::min (1.0f, 1.0f + p);
            }
            else addPanned (valL[ch] * scale, v.pan >= 0 ? overridePan (v) : sidePos, L, R);
        };

        float L = 0, R = 0;
        if (nt > 4)
        {
            for (int p = 0; p < 4; ++p)
            {
                const int a = p * 2, b = p * 2 + 1;
                if (voices[a].pan < 0 && voices[b].pan < 0 && ! st[a] && ! st[b])
                {
                    float s = (valL[a] + valL[b]) * 0.5f;
                    if (options.crunch8ch && options.eightBit) s = quant8 (s);
                    addPanned (s, sidePan (p), L, R);
                }
                else
                {
                    float pl = 0, pr = 0;
                    addVoice (a, 0.5f, sidePan (p), pl, pr);
                    addVoice (b, 0.5f, sidePan (p), pl, pr);
                    if (options.crunch8ch && options.eightBit) { pl = quant8 (pl); pr = quant8 (pr); }
                    L += pl; R += pr;
                }
            }
        }
        else
        {
            for (int t = 0; t < nt; ++t) addVoice (t, 1.0f, sidePan (t), L, R);
        }
        for (int j = kMaxTracks; j < kVoices; ++j)
        {
            if (st[j]) { L += valL[j] * 0.5f; R += valR[j] * 0.5f; }
            else addPanned (valL[j] * 0.7f, 0.0f, L, R);
        }

        // analogue output stage
        if (options.model != PaulaOptions::Model::Clean)
        {
            L = hpL.hp (lpL.lp (L));
            R = hpR.hp (lpR.lp (R));
        }
        if (ledOn)
        {
            L = ledL.process (L);
            R = ledR.process (R);
        }

        const float g = options.master;
        left[i]  = softClip (L * g);
        right[i] = softClip (R * g);
    }

    for (int t = 0; t < kMaxTracks; ++t)
        uiLevel[t].store (peaks[t], std::memory_order_relaxed);
    uiLed = ledOn;
    uiSpeed = speed; uiTempo = tempo;
}

} // namespace prv
