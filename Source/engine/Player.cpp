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

    constexpr uint8_t C_G = cmdLetter ('G'), C_H = cmdLetter ('H'), C_K = cmdLetter ('K'), C_L = cmdLetter ('L'),
                      C_M = cmdLetter ('M'), C_N = cmdLetter ('N'), C_O = cmdLetter ('O'), C_P = cmdLetter ('P'),
                      C_Q = cmdLetter ('Q'), C_R = cmdLetter ('R'), C_S = cmdLetter ('S'), C_T = cmdLetter ('T'),
                      C_U = cmdLetter ('U'), C_W = cmdLetter ('W'), C_X = cmdLetter ('X'), C_Z = cmdLetter ('Z');

    float cutoffFromParam (uint8_t p) { return (float) (20.0 * std::pow (1000.0, p / 255.0)); }

    // advance one sample; returns false when a release has finished
    bool stepEnv (int& stage, float& level, float relStep, const Envelope& e, double sr)
    {
        switch (stage)
        {
            case 1:
                level += 1.0f / (float) std::max (1.0, (double) std::max (0.0005f, e.attack) * sr);
                if (level >= 1.0f) { level = 1.0f; stage = 2; }
                break;
            case 2:
                level -= (1.0f - e.sustain) / (float) std::max (1.0, (double) std::max (0.0005f, e.decay) * sr);
                if (level <= e.sustain) { level = e.sustain; stage = 3; }
                break;
            case 3: level = e.sustain; break;
            case 4:
                level -= relStep;
                if (level <= 0.0f) { level = 0.0f; stage = 0; return false; }
                break;
            default: break;
        }
        return true;
    }
}

Player::Player()
{
    resetVoices();
    for (auto& j : uiJamPos) j = -1;
}

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
    rng = 0x9E3779B9u;
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
    rng = 0x9E3779B9u;
    mode = Mode::Block;
}

void Player::stop()
{
    mode = Mode::Stopped;
    for (int i = 0; i < kMaxTracks; ++i) { voices[i].active = false; voices[i].microDelay = -1; }
}

// ---------------------------------------------------------------------------
void Player::jamNoteOn (int note, int instrIndex, int velocity127, int start, int end, bool loopRegion)
{
    if (song == nullptr || instrIndex < 0 || instrIndex >= kMaxInstruments) return;
    const auto& in = song->instruments[(size_t) instrIndex];
    if (! in.hasData()) return;

    const int slot = jamNext;
    Voice& v = voices[kMaxTracks + slot];
    jamNext = (jamNext + 1) % kJamVoices;
    v = Voice{};
    v.instr = instrIndex;
    v.finetune = in.finetune;
    v.note = std::clamp (note + in.transpose, 1, (int) kNoteMax);
    v.period = v.outPeriod = periodFor (v.note, v.finetune);
    v.volume = v.outVolume = std::clamp (in.volume * velocity127 / 127, 0, 64);
    v.jamNote = note;
    const int len = (int) in.data.size();
    if (start >= 0 && start < len)
    {
        v.regionStart = start;
        v.regionEnd = (end > start && end <= len) ? end : len;
        v.regionLoop = loopRegion && v.regionEnd - v.regionStart >= 4;
        v.pos = start;
    }
    v.active = true;
    startShaping (v);
    uiJamInstr[slot] = instrIndex;
}

void Player::jamNoteOff (int note)
{
    for (int i = kMaxTracks; i < kVoices; ++i)
        if (voices[i].jamNote == note) { releaseVoice (voices[i]); voices[i].jamNote = -1; }
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

double Player::lineSamples() const { return samplesPerTick() * speed; }

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

    if (tick == 0 && firstTickOfRow) processRow();
    else for (int ch = 0; ch < kMaxTracks; ++ch) tickEffects (ch);

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
        if (ch < song->numTracks)
        {
            const Cell& c = b.at (line, ch);
            uint8_t chance = 0;
            if (c.has (C_K, chance) && (nextRandom() % 255u) >= chance)
                continue;   // Kxx: this step doesn't play this time
            processCell (ch, c);
        }
        else voices[ch].active = false;
    }
    scheduleEarlyNotes();
    uiSeqPos = seqPos; uiBlock = block; uiLine = line;
    uiSpeed = speed; uiTempo = tempo; uiLed = ledOn;
}

// M81-MFF on the next line: start that note early, inside this line
void Player::scheduleEarlyNotes()
{
    const Block& b = song->blocks[(size_t) block];
    const int next = line + 1;
    if (next >= b.lines) return;   // not across block boundaries
    for (int ch = 0; ch < std::min (song->numTracks, kMaxTracks); ++ch)
    {
        const Cell& c = b.at (next, ch);
        uint8_t m = 0;
        if (! c.has (C_M, m) || m <= 0x80) continue;
        if (c.note == 0 || c.note > kNoteMax) continue;
        Voice& v = voices[ch];
        const double early = std::min (1.0, (m - 0x80) / 128.0);
        v.microCell = c;
        v.microDelay = (long long) std::max (0.0, lineSamples() * (1.0 - early));
        v.earlyDone = true;
    }
}

void Player::setInstrument (Voice& v, int instrNumber)
{
    if (instrNumber <= 0 || instrNumber > kMaxInstruments) return;
    const auto& in = song->instruments[(size_t) instrNumber - 1];
    v.instr = instrNumber - 1;
    v.volume = in.volume;
    v.finetune = in.finetune;
}

void Player::triggerNote (Voice& v, const Cell& cell, int)
{
    if (v.instr < 0) { v.active = false; return; }
    const auto& in = song->instruments[(size_t) v.instr];
    if (! in.hasData()) { v.active = false; return; }
    const double len = (double) in.data.size();

    int note = (int) cell.note + in.transpose;
    uint8_t p = 0;
    // reset per-note state
    v.regionStart = v.regionEnd = -1; v.regionLoop = false;
    v.reverse = false; v.pitchMul = 1.0;
    v.glideTicks = 0; v.gateTicks = -1; v.rollEvery = 0;
    v.fltType = -1; v.fltRes = -1.0f; v.drive = 0.0f; v.crushBits = 0;

    if (cell.has (C_X, p))
    {
        const int what = p >> 4, amt = p & 15;
        const int r = amt ? (int) (nextRandom() % (uint32_t) (amt * 2 + 1)) - amt : 0;
        if (what == 0) note += r;
    }
    v.note = std::clamp (note, 1, (int) kNoteMax);
    v.period = v.outPeriod = v.targetPeriod = periodFor (v.note, v.finetune);
    v.pos = 0;

    if (cell.cmd == 0x9 || cell.cmd2 == 0x9)
    {
        const uint8_t off = cell.cmd == 0x9 ? cell.param : cell.param2;
        if (off) v.offsetMem = off;
        v.pos = v.offsetMem * 256.0;
        if (v.pos >= len)
        {
            if (in.isLooped()) v.pos = in.loopStart;
            else { v.active = false; return; }
        }
    }
    if (cell.has (C_P, p)) v.pos = std::floor (p / 256.0 * len);
    if (cell.has (C_S, p))
    {
        const int sl = p & 15;
        v.regionStart = std::floor (sl * len / 16.0);
        v.regionEnd = std::floor ((sl + 1) * len / 16.0);
        v.pos = v.regionStart;
    }
    if (cell.has (C_X, p) && (p >> 4) == 3 && (p & 15))
        v.pos = std::floor ((nextRandom() % 1000u) / 1000.0 * (p & 15) / 16.0 * len);
    if (cell.has (C_U, p) && (p & 1))
    {
        // play backwards from the end of the slice/sample (an offset counts from the end)
        v.reverse = true;
        const double hi = v.regionEnd > 0 ? v.regionEnd : len;
        const double lo = v.regionStart > 0 ? v.regionStart : 0.0;
        v.pos = std::clamp (hi - 1.0 - (v.pos - lo), lo, hi - 1.0);
    }
    if (v.vibWave < 4)  v.vibPos = 0;
    if (v.tremWave < 4) v.tremPos = 0;
    v.active = true;
    startShaping (v);
}

// ---------------------------------------------------------------------------
void Player::startShaping (Voice& v)
{
    v.ampEnv = {}; v.fltEnv = {};
    v.ampEnv.stage = 1; v.fltEnv.stage = 1;
    v.svf = {};
}

void Player::releaseVoice (Voice& v)
{
    if (! v.active || v.instr < 0 || song == nullptr) { v.active = false; return; }
    const auto& in = song->instruments[(size_t) v.instr];
    if (! in.volEnv.on) { v.active = false; return; }   // classic tracker: OFF cuts
    const double sr = sampleRate;
    auto rel = [sr] (Voice::EnvState& e, float seconds)
    {
        if (e.stage == 0) return;
        e.stage = 4;
        e.relStep = e.level / (float) std::max (1.0, (double) std::max (0.001f, seconds) * sr);
    };
    rel (v.ampEnv, in.volEnv.release);
    rel (v.fltEnv, in.filterEnv.release);
}

void Player::shapeVoice (Voice& v, float& l, float& r)
{
    const auto& in = song->instruments[(size_t) v.instr];
    if (in.volEnv.on)
    {
        if (! stepEnv (v.ampEnv.stage, v.ampEnv.level, v.ampEnv.relStep, in.volEnv, sampleRate))
            v.active = false;
        l *= v.ampEnv.level; r *= v.ampEnv.level;
    }

    const FilterType type = v.fltType >= 0 ? (FilterType) v.fltType : in.filter;
    if (type != FilterType::Off)
    {
        stepEnv (v.fltEnv.stage, v.fltEnv.level, v.fltEnv.relStep, in.filterEnv, sampleRate);
        auto& f = v.svf;
        if (f.count-- <= 0)
        {
            f.count = 15;
            const float baseCut = v.fltType >= 0 ? v.fltCutoff : in.cutoff;
            const float res = v.fltRes >= 0.0f ? v.fltRes : in.resonance;
            const double oct = in.filterEnvAmount * v.fltEnv.level + in.keyTrack * (v.note - 37) / 12.0;
            const double fc = std::clamp ((double) baseCut * std::pow (2.0, oct), 20.0, sampleRate * 0.45);
            const double g = std::tan (kPi * fc / sampleRate);
            f.k = (float) (2.0 - 1.94 * std::clamp ((double) res, 0.0, 1.0));
            f.a1 = (float) (1.0 / (1.0 + g * (g + f.k)));
            f.a2 = (float) (g * f.a1);
            f.a3 = (float) (g * f.a2);
        }
        auto run = [&f, type] (float x, float& ic1, float& ic2)
        {
            const float v3 = x - ic2;
            const float v1 = f.a1 * ic1 + f.a2 * v3;
            const float v2 = ic2 + f.a2 * ic1 + f.a3 * v3;
            ic1 = 2.0f * v1 - ic1;
            ic2 = 2.0f * v2 - ic2;
            switch (type)
            {
                case FilterType::HighPass: return x - f.k * v1 - v2;
                case FilterType::BandPass: return v1;
                default:                   return v2;
            }
        };
        l = run (l, f.ic1L, f.ic2L);
        r = run (r, f.ic1R, f.ic2R);
    }

    if (v.drive > 0.0f)
    {
        const float k = 1.0f + 15.0f * v.drive;
        const float norm = 1.0f / std::tanh (k);
        l = std::tanh (l * k) * norm;
        r = std::tanh (r * k) * norm;
    }
    if (v.crushBits > 0 && v.crushBits < 16)
    {
        const float q = (float) (1 << (v.crushBits - 1));
        l = std::round (l * q) / q;
        r = std::round (r * q) / q;
    }
}

bool Player::fixedPan (int ch, float& pos) const
{
    const Voice& v = voices[ch];
    auto toPos = [] (int p) { return std::clamp ((float) p / 127.5f - 1.0f, -1.0f, 1.0f); };
    if (v.pan >= 0) { pos = toPos (v.pan); return true; }                                   // 8xx / E8x / X2y
    if (ch < kMaxTracks && song->trackPan[ch] >= 0) { pos = toPos (song->trackPan[ch]); return true; }
    if (v.instr >= 0 && ! song->instruments[(size_t) v.instr].amigaPan) { pos = toPos (song->instruments[(size_t) v.instr].pan); return true; }
    return false;
}

// ---------------------------------------------------------------------------
void Player::processCell (int ch, const Cell& cell)
{
    Voice& v = voices[ch];
    v.cmd = cell.cmd; v.param = cell.param;
    v.cmd2 = cell.cmd2; v.param2 = cell.param2;
    v.delayTick = -1;
    uint8_t p = 0;

    const bool edDelay = (cell.cmd == 0xE && (cell.param >> 4) == 0xD && (cell.param & 15) > 0)
                      || (cell.cmd2 == 0xE && (cell.param2 >> 4) == 0xD && (cell.param2 & 15) > 0);
    const bool hasNotePart = cell.note != 0 || cell.instr != 0;
    const bool early = v.earlyDone;        // the note was already started early (M81-MFF)
    v.earlyDone = false;
    const bool late = cell.has (C_M, p) && p > 0 && p < 0x80 && cell.note > 0 && cell.note <= kNoteMax;

    if (early)
    {
        // note/instrument already handled
    }
    else if (late)
    {
        v.microCell = cell;
        v.microDelay = (long long) (lineSamples() * (p / 128.0));
    }
    else if (edDelay && hasNotePart)
    {
        v.delayTick = (cell.cmd == 0xE ? cell.param : cell.param2) & 15;
        v.delayedCell = cell;
    }
    else
    {
        setInstrument (v, cell.instr);
        if (cell.note == kNoteOff) releaseVoice (v);
        else if (cell.note > 0 && cell.note <= kNoteMax)
        {
            uint8_t g = 0;
            const bool porta = (cell.cmd == 0x3 || cell.cmd == 0x5 || cell.cmd2 == 0x3 || cell.cmd2 == 0x5);
            const bool glide = cell.has (C_G, g);
            if ((porta || glide) && v.active && v.instr >= 0)
            {
                const auto& in = song->instruments[(size_t) v.instr];
                v.targetPeriod = periodFor (std::clamp ((int) cell.note + in.transpose, 1, (int) kNoteMax), v.finetune);
                v.note = std::clamp ((int) cell.note + in.transpose, 1, (int) kNoteMax);
                if (glide)
                {
                    v.glideTicks = std::max (1, (int) g);
                    v.glideStep = (v.targetPeriod - v.period) / v.glideTicks;
                }
            }
            else triggerNote (v, cell, ch);
        }
    }

    applyRowCommand (v, cell.cmd, cell.param, cell);
    if (mode == Mode::Stopped) return;
    applyRowCommand (v, cell.cmd2, cell.param2, cell);
    v.outPeriod = v.period;
    v.outVolume = v.volume;
}

void Player::applyRowCommand (Voice& v, uint8_t cmd, uint8_t param, const Cell& cell)
{
    const int x = param >> 4, y = param & 15;
    if (cmd == 0 && param == 0) return;
    switch (cmd)
    {
        case 0x3: if (param) v.portaSpeed = param; break;
        case 0x4: if (x) v.vibSpeed = (uint8_t) x; if (y) v.vibDepth = (uint8_t) y; break;
        case 0x7: if (x) v.tremSpeed = (uint8_t) x; if (y) v.tremDepth = (uint8_t) y; break;
        case 0x8: v.pan = param; break;
        case 0xB: jumpPos = param; break;
        case 0xC: v.volume = std::min ((int) param, 64); break;
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
            if (param == 0) { stop(); ++songWraps; return; }
            if (param < 0x20) speed = param; else tempo = param;
            break;
        default: break;
    }

    if (cmd < 16) return;
    if (cmd == C_H || cmd == C_L || cmd == C_W)
    {
        v.fltType = (int) (cmd == C_H ? FilterType::HighPass : cmd == C_L ? FilterType::LowPass : FilterType::BandPass);
        v.fltCutoff = cutoffFromParam (param);
    }
    else if (cmd == C_Q) { v.fltRes = param / 255.0f; if (v.fltType < 0 && v.instr >= 0 && song->instruments[(size_t) v.instr].filter == FilterType::Off) { v.fltType = (int) FilterType::LowPass; v.fltCutoff = 20000.0f; } }
    else if (cmd == C_N) v.gateTicks = param ? param : -1;
    else if (cmd == C_O) v.drive = param / 255.0f;
    else if (cmd == C_Z) v.crushBits = (param > 0 && param < 16) ? param : 0;
    else if (cmd == C_T) v.pitchMul = std::pow (2.0, ((int) param - 128) / 1200.0);
    else if (cmd == C_R) { v.rollEvery = std::max (1, x); v.rollVolStep = y == 0 || y == 8 ? 0 : y < 8 ? -y * 4 : (y - 8) * 4; }
    else if (cmd == C_U && cell.note == 0) v.reverse = (param & 1) != 0;   // flip a note that's already playing
    else if (cmd == C_X)
    {
        const int amt = y;
        const int r = amt ? (int) (nextRandom() % (uint32_t) (amt * 2 + 1)) - amt : 0;
        if (x == 1) v.volume = std::clamp (v.volume + r * 4, 0, 64);
        else if (x == 2) v.pan = std::clamp (128 + r * 16, 0, 255);
    }
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

void Player::tickCommand (Voice& v, uint8_t cmd, uint8_t param, double& vib, int& trem, int& arp)
{
    const int x = param >> 4, y = param & 15;
    auto volSlide = [&] { if (x) v.volume = std::min (64, v.volume + x); else v.volume = std::max (0, v.volume - y); };
    auto porta = [&]
    {
        if (v.targetPeriod <= 0) return;
        if (v.period < v.targetPeriod) v.period = std::min (v.targetPeriod, v.period + v.portaSpeed);
        else                           v.period = std::max (v.targetPeriod, v.period - v.portaSpeed);
    };
    switch (cmd)
    {
        case 0x0: if (param) arp = (tick % 3 == 1) ? x : (tick % 3 == 2 ? y : 0); break;
        case 0x1: v.period = std::max (kMinPeriod, v.period - param); break;
        case 0x2: v.period = std::min (kMaxPeriod, v.period + param); break;
        case 0x3: porta(); break;
        case 0x4: vib = vibratoDelta (v.vibPos, v.vibDepth, v.vibWave, 7); v.vibPos = (uint8_t) ((v.vibPos + v.vibSpeed) & 63); break;
        case 0x5: porta(); volSlide(); break;
        case 0x6: vib = vibratoDelta (v.vibPos, v.vibDepth, v.vibWave, 7); v.vibPos = (uint8_t) ((v.vibPos + v.vibSpeed) & 63); volSlide(); break;
        case 0x7: trem = (int) vibratoDelta (v.tremPos, v.tremDepth, v.tremWave, 6); v.tremPos = (uint8_t) ((v.tremPos + v.tremSpeed) & 63); break;
        case 0xA: volSlide(); break;
        case 0xE:
            if (x == 0x9 && y > 0 && tick % y == 0 && v.instr >= 0 && song->instruments[(size_t) v.instr].hasData()) { v.pos = v.regionStart > 0 ? v.regionStart : 0; v.active = true; }
            if (x == 0xC && tick == y) v.volume = 0;
            break;
        default: break;
    }
}

void Player::tickEffects (int ch)
{
    Voice& v = voices[ch];

    if (v.delayTick >= 0 && tick == v.delayTick)
    {
        v.delayTick = -1;
        Cell c = v.delayedCell; c.cmd = 0; c.param = 0;
        setInstrument (v, c.instr);
        if (c.note == kNoteOff) releaseVoice (v);
        else if (c.note > 0) triggerNote (v, c, ch);
    }

    double vib = 0; int trem = 0; int arpSemis = 0;
    tickCommand (v, v.cmd, v.param, vib, trem, arpSemis);
    tickCommand (v, v.cmd2, v.param2, vib, trem, arpSemis);

    // G: glide to the new note over N ticks
    if (v.glideTicks > 0)
    {
        v.period = std::clamp (v.period + v.glideStep, kMinPeriod, kMaxPeriod);
        if (--v.glideTicks == 0) v.period = v.targetPeriod;
    }
    // N: release after N ticks
    if (v.gateTicks > 0 && --v.gateTicks == 0) { releaseVoice (v); v.gateTicks = -1; }
    // R: roll / retrigger
    if (v.rollEvery > 0 && tick > 0 && tick % v.rollEvery == 0 && v.instr >= 0 && song->instruments[(size_t) v.instr].hasData())
    {
        const double len = (double) song->instruments[(size_t) v.instr].data.size();
        v.pos = v.reverse ? (v.regionEnd > 0 ? v.regionEnd : len) - 1.0 : (v.regionStart > 0 ? v.regionStart : 0.0);
        v.volume = std::clamp (v.volume + v.rollVolStep, 0, 64);
        v.active = true;
        startShaping (v);
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
    // an early-scheduled note only counts for the line it was meant for
    if (loopJumpLine >= 0 || jumpPos >= 0 || breakLine >= 0)
        for (auto& v : voices) if (v.earlyDone) { v.earlyDone = false; }
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
    const int last = (int) d.size() - 1;
    const bool eight = options.eightBit;

    // playback window: a slice/selection region, or the instrument's loop
    bool looped = in.isLooped();
    double loopStart = in.loopStart, end = looped ? (double) (in.loopStart + in.loopLength) : (double) d.size();
    double begin = 0.0;
    if (v.regionStart >= 0 && v.regionEnd > v.regionStart)
    {
        begin = v.regionStart;
        end = std::min ((double) d.size(), v.regionEnd);
        looped = v.regionLoop;
        loopStart = begin;
    }
    const double loopLen = end - loopStart;

    auto conv = [eight] (int s) -> float
    {
        if (eight) s = (s >> 8) * 256;
        return (float) s / 32768.0f;
    };

    float L = 0.0f, R = 0.0f;
    if (v.reverse)
    {
        // backwards: step down through the sample, stop at the start
        const size_t idx = (size_t) std::clamp ((int) std::floor (v.pos), 0, last);
        L = conv (d[idx]);
        R = stereo ? conv (dr[idx]) : L;
        v.pos -= inc;
        if (v.pos < begin) { if (looped && loopLen >= 4) v.pos = end - 1.0; else v.active = false; }
    }
    else
    {
        auto wrap = [&] (double& p) -> bool
        {
            if (p < end) return true;
            if (! looped || loopLen < 1.0) return false;
            while (p >= end) p -= loopLen;
            return true;
        };
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
            if (n >= (int) end) n = looped ? (int) loopStart : i;
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
            // micro-moved notes (Mxx) start part-way through a line
            for (int ch = 0; ch < kMaxTracks; ++ch)
            {
                Voice& v = voices[ch];
                if (v.microDelay >= 0 && v.microDelay-- == 0)
                {
                    Cell c = v.microCell;
                    setInstrument (v, c.instr);
                    if (c.note == kNoteOff) releaseVoice (v);
                    else if (c.note > 0) triggerNote (v, c, ch);
                    v.microDelay = -1;
                }
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
                const double rateMul = v.instr >= 0 ? song->instruments[(size_t) v.instr].rate / kAmigaC3Rate : 1.0;
                const double inc = kPaulaClock / std::max (kMinPeriod, v.outPeriod) / sampleRate * rateMul * v.pitchMul;
                float sl, sr;
                renderVoice (v, inc, sl, sr);
                if (v.active || sl != 0.0f || sr != 0.0f)
                    if (v.instr >= 0) shapeVoice (v, sl, sr);
                if (ch < kMaxTracks)
                {
                    if (mute[ch].load (std::memory_order_relaxed) || ch >= nt) continue;
                    peaks[ch] = std::max ({ peaks[ch], std::abs (sl), std::abs (sr) });
                }
                valL[ch] = sl; valR[ch] = sr;
                st[ch] = v.instr >= 0 && song->instruments[(size_t) v.instr].isStereo();
            }
        }

        // Pans: 8xx in the pattern, then the track's pan, then the instrument's
        // pan; otherwise mono voices follow the Amiga L-R-R-L layout scaled by
        // SEP and stereo samples play centred with their own stereo image.
        float fixedPos[kVoices] = {};
        bool  isFixed[kVoices] = {};
        for (int ch = 0; ch < kVoices; ++ch)
            if (voices[ch].active || valL[ch] != 0.0f) isFixed[ch] = song != nullptr && fixedPan (ch, fixedPos[ch]);

        auto addVoice = [&] (int ch, float scale, float sidePos, float& L, float& R)
        {
            const float p = isFixed[ch] ? fixedPos[ch] : (st[ch] ? 0.0f : sidePos);
            if (st[ch])
            {
                L += valL[ch] * scale * 0.7f * std::min (1.0f, 1.0f - p);
                R += valR[ch] * scale * 0.7f * std::min (1.0f, 1.0f + p);
            }
            else addPanned (valL[ch] * scale, p, L, R);
        };

        float L = 0, R = 0;
        if (nt > 4)
        {
            for (int p = 0; p < 4; ++p)
            {
                const int a = p * 2, b = p * 2 + 1;
                if (! isFixed[a] && ! isFixed[b] && ! st[a] && ! st[b])
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
            const float p = isFixed[j] ? fixedPos[j] : 0.0f;
            if (st[j]) { L += valL[j] * 0.5f * std::min (1.0f, 1.0f - p); R += valR[j] * 0.5f * std::min (1.0f, 1.0f + p); }
            else addPanned (valL[j] * 0.7f, p, L, R);
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
    for (int j = 0; j < kJamVoices; ++j)
        uiJamPos[j].store (voices[kMaxTracks + j].active ? (int) voices[kMaxTracks + j].pos : -1, std::memory_order_relaxed);
    float pkL = 0, pkR = 0;
    for (int i = 0; i < numSamples; ++i) { pkL = std::max (pkL, std::abs (left[i])); pkR = std::max (pkR, std::abs (right[i])); }
    uiOutL.store (pkL, std::memory_order_relaxed);
    uiOutR.store (pkR, std::memory_order_relaxed);
    uiLed = ledOn;
    uiSpeed = speed; uiTempo = tempo;
}

} // namespace prv
