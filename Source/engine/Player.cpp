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
                      C_U = cmdLetter ('U'), C_W = cmdLetter ('W'), C_X = cmdLetter ('X'), C_Z = cmdLetter ('Z'),
                      C_I = cmdLetter ('I'), C_V = cmdLetter ('V'), C_J = cmdLetter ('J'), C_Y = cmdLetter ('Y');

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
    for (auto& t : uiTrackInstr) t = -1;
    for (auto& t : synthTrack) t = 99;
    for (auto& o : instOnOv) o = -1;
    for (auto& d : sampleAlign) d.setMax (MixerEngine::kMaxSlotDelay);
}

// ---------------------------------------------------------------------------
// plug-in instruments
bool Player::isSynthVoice (const Voice& v) const
{
    return song != nullptr && v.instr >= 0 && v.instr < kMaxInstruments && song->instruments[(size_t) v.instr].isPlugin();
}

int Player::synthOutputTrack (int i) const
{
    if (song == nullptr || i < 0 || i >= kMaxInstruments) return 0;
    const int nt = std::clamp (song->numTracks, 1, kMaxTracks);
    const int o = song->instruments[(size_t) i].pluginOutput;
    if (o >= 0) return std::min (o, nt - 1);
    return synthTrack[i] < nt ? synthTrack[i] : 0;   // AUTO: the lowest track that has played it
}

bool Player::instrumentIsOn (int i) const
{
    if (song == nullptr || i < 0 || i >= kMaxInstruments) return false;
    return instOnOv[i] >= 0 ? instOnOv[i] == 1 : ! song->instruments[(size_t) i].switchedOff;
}

void Player::setInstrumentOn (int i, int state)
{
    if (i < 0 || i >= kMaxInstruments) return;
    instOnOv[i] = std::clamp (state, -1, 1);
    if (instrumentIsOn (i)) return;
    // switched off: its notes stop now (samples fade out over 5 ms, so there's no click)
    for (auto& v : voices)
        if (v.active && v.instr == i && v.fadeOut == 0) { v.fadeLen = v.fadeOut = std::max (1, (int) (sampleRate * 0.005)); }
    for (auto& v : voices) if (v.synthNote >= 0 && v.synthInstr == i) synthRelease (v);
    for (auto& j : jamSynth) if (j.note >= 0 && j.instr == i) { if (auto* s = rack.at (i)) s->noteOff (j.midi, chunkPos); j = JamSynth {}; }
    if (auto* s = rack.at (i)) s->allNotesOff (chunkPos);
}

void Player::resetSwitches()
{
    // play / stop start again from the BYPASS and ON buttons
    for (auto& o : instOnOv) o = -1;
    mixerEngine.clearOnOff();
}

void Player::synthRelease (Voice& v)
{
    if (v.synthNote < 0) return;
    if (auto* s = rack.at (v.synthInstr))
    {
        s->noteOff (v.synthNote, chunkPos);
        for (int& c : v.synthChord) if (c >= 0) s->noteOff (c, chunkPos);
    }
    v.synthNote = -1;
    v.synthChord[0] = v.synthChord[1] = -1;
}

namespace
{
    // 0xy on a line: the chord's two extra notes (x and y semitones up)
    bool chordOf (const Cell& c, int& x, int& y)
    {
        uint8_t p = 0;
        if (c.cmd == 0 && c.param != 0) p = c.param;
        else if (c.cmd2 == 0 && c.param2 != 0) p = c.param2;
        if (p == 0) return false;
        x = p >> 4; y = p & 15;
        return true;
    }
}

void Player::synthTrigger (Voice& v, const Cell& cell, int ch)
{
    const auto& in = song->instruments[(size_t) v.instr];
    synthRelease (v);   // one note at a time on a track, as with samples
    uint8_t p = 0;
    int note = (int) cell.note + in.transpose;
    if (cell.has (C_I, p) && p != 0) note += std::clamp ((int) p - 128, -48, 48);
    v.note = std::clamp (note, 1, (int) kNoteMax);
    v.period = v.outPeriod = v.targetPeriod = periodFor (v.note, v.finetune);
    v.glideTicks = 0; v.gateTicks = -1; v.rollEvery = 0; v.pitchMul = 1.0;
    if (v.vibWave < 4)  v.vibPos = 0;
    if (v.tremWave < 4) v.tremPos = 0;
    // Cxx on the note's own line sets the velocity (C40 = 127)
    int vol = v.volume;
    if (cell.cmd == 0xC) vol = std::min ((int) cell.param, 64);
    else if (cell.cmd2 == 0xC) vol = std::min ((int) cell.param2, 64);
    v.synthVel = std::clamp ((int) std::lround (vol * 127.0 / 64.0), 1, 127);
    v.active = false;   // (no sample to play)
    auto* s = rack.at (v.instr);
    if (s == nullptr || (ch < kMaxTracks && mute[ch].load (std::memory_order_relaxed)) || ! instrumentIsOn (v.instr)) return;
    if (v.lastBend != 8192) { s->pitchBend (8192, chunkPos); v.lastBend = 8192; }
    v.synthNote = midiNoteFor (v.note);
    v.synthInstr = v.instr;
    s->noteOn (v.synthNote, v.synthVel, chunkPos);
    // CHORD: 0xy plays the note with x and y semitones above it, held
    int x = 0, y = 0;
    if (in.chord && chordOf (chordSource != nullptr ? *chordSource : cell, x, y))
    {
        int k = 0;
        for (int iv : { x, y })
            if (iv > 0 && v.synthNote + iv <= 127 && k < 2)
            {
                v.synthChord[k] = v.synthNote + iv;
                s->noteOn (v.synthChord[k++], v.synthVel, chunkPos);
            }
        v.chordLine = true;
    }
    if (ch < kMaxTracks) synthTrack[v.instr] = std::min (synthTrack[v.instr], ch);
    synthBend (v);
}

void Player::synthRetrigger (Voice& v, int velocityChange)
{
    if (v.synthNote < 0) return;
    auto* s = rack.at (v.synthInstr);
    if (s == nullptr) return;
    v.synthVel = std::clamp (v.synthVel + velocityChange, 1, 127);
    s->noteOff (v.synthNote, chunkPos);
    for (int c : v.synthChord) if (c >= 0) s->noteOff (c, chunkPos);
    s->noteOn (v.synthNote, v.synthVel, chunkPos);
    for (int c : v.synthChord) if (c >= 0) s->noteOn (c, v.synthVel, chunkPos);
}

void Player::synthBend (Voice& v)
{
    // slides, vibrato, arpeggio and finetune reach a plug-in as pitch bend
    if (v.synthNote < 0) return;
    auto* s = rack.at (v.synthInstr);
    if (s == nullptr) return;
    const int range = std::max (1, song->instruments[(size_t) v.synthInstr].bendRange);
    const double semis = 12.0 * std::log2 (periodFor (v.synthNote - 23, 0) / std::max (kMinPeriod, v.outPeriod)) + 1200.0 * std::log2 (v.pitchMul) / 100.0;
    const int bend = std::clamp ((int) std::lround (8192.0 + semis / range * 8191.0), 0, 16383);
    if (bend != v.lastBend) { s->pitchBend (bend, chunkPos); v.lastBend = bend; }
}

void Player::synthAllOff()
{
    for (auto& v : voices) synthRelease (v);
    for (auto& j : jamSynth) if (j.note >= 0) { if (auto* s = rack.at (j.instr)) s->noteOff (j.midi, chunkPos); j = JamSynth {}; }
    for (int i = 0; i < kMaxInstruments; ++i) if (auto* s = rack.at (i)) s->allNotesOff (chunkPos);
}

// ---------------------------------------------------------------------------
// automation knobs
void Player::applyLane (int kind, int a, int b, float v)
{
    if (song == nullptr) return;
    if (kind == 0)
    {
        const auto& t = song->mixer.trackAuto[a][b];
        if (! t.valid()) return;
        if (t.target < kAutoInstrumentBase) mixerEngine.setAutomation (t.target, t.param, v);
        else if (t.param == kAutoOnOff) setInstrumentOn (t.target - kAutoInstrumentBase, autoValueOn (v) ? 1 : 0);
        else if (auto* s = rack.at (t.target - kAutoInstrumentBase)) s->setParameterAt (t.param, v, chunkPos);
    }
    else
    {
        const auto& t = song->instruments[(size_t) a].knobs[b];
        if (t.param == kAutoOnOff) setInstrumentOn (a, autoValueOn (v) ? 1 : 0);
        else if (t.param >= 0) if (auto* s = rack.at (a)) s->setParameterAt (t.param, v, chunkPos);
    }
}

void Player::setLane (Lane& l, float value, int glide, int kind, int a, int b)
{
    knobSetThisRow = true;
    if (glide > 0 && l.cur >= 0.0f)
    {
        l.target = value; l.ticks = glide; l.step = (value - l.cur) / (float) glide;
        return;
    }
    l.cur = l.target = value; l.ticks = 0;
    applyLane (kind, a, b, value);
}

void Player::advanceAutomation()
{
    auto run = [this] (Lane& l, int kind, int a, int b)
    {
        if (l.ticks <= 0) return;
        l.cur = --l.ticks == 0 ? l.target : l.cur + l.step;
        applyLane (kind, a, b, l.cur);
    };
    for (int t = 0; t < kMaxTracks; ++t) for (int k = 0; k < kTrackKnobs; ++k) run (trackLanes[t][k], 0, t, k);
    for (int i = 0; i < kMaxInstruments; ++i) for (int k = 0; k < kInstrumentKnobs; ++k) run (instLanes[i][k], 1, i, k);
}

void Player::setSampleRate (double sr)
{
    sampleRate = sr > 0 ? sr : 44100.0;
    updateFilters();
    mixerEngine.prepare (sampleRate);
    rack.prepare (sampleRate, MixerEngine::kMaxBlock);
    lpL.reset(); lpR.reset(); hpL.reset(); hpR.reset(); ledL.reset(); ledR.reset();
    for (auto& b : akL) b.reset();
    for (auto& b : akR) b.reset();
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

    // Akai: a Butterworth low-pass built from 2-pole sections
    //  S950: the voice filter (6th-order Butterworth switched-capacitor, MF6 type) fully open
    //  S1000: the DAC's steep reconstruction filter (8th order) just below 20 kHz
    akStages = 0;
    if (options.isAkai())
    {
        static const double q6[] = { 0.5176, 0.7071, 1.9319 };
        static const double q8[] = { 0.5098, 0.6013, 0.9000, 2.5629 };
        const bool s950 = options.model == PaulaOptions::Model::S950;
        const double fc = std::min (s950 ? kS950Cutoff : kS1000Cutoff, nyq);
        akStages = s950 ? 3 : 4;
        for (int k = 0; k < akStages; ++k) { akL[k].setLowpass (fc, s950 ? q6[k] : q8[k], sampleRate); akR[k] = akL[k]; }
        hpL.setLowpass (s950 ? 12.0 : 4.0, sampleRate); hpR = hpL;   // output coupling
    }
}

void Player::resetVoices()
{
    for (auto& v : voices) v = Voice{};
}

double Player::positionInBeats (int pos, int ln) const
{
    // 4 lines a beat, from the start of the song
    long long lines = ln;
    if (song != nullptr)
        for (int i = 0; i < pos && i < (int) song->sequence.size(); ++i) lines += song->blocks[(size_t) song->safeBlock (song->sequence[(size_t) i])].lines;
    return (double) lines / 4.0;
}

void Player::sendClock (int kind, int offset)
{
    for (int i = 0; i < kMaxInstruments; ++i)
        if (clockOwner[i]) if (auto* s = rack.at (i)) s->clock (kind, ppq, offset);
}

void Player::playSong (int pos, int startLine)
{
    if (song == nullptr || song->sequence.empty()) return;
    if (mode != Mode::Stopped) sendClock (2, chunkPos);
    synthAllOff();
    resetSwitches();
    for (auto& t : synthTrack) t = 99;
    for (auto& g : pendingGlide) g = 0;
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
    uiSeqPos = seqPos; uiBlock = block; uiLine = line;
    ppq = positionInBeats (seqPos, line);
    clockStartPending = true;
}

void Player::playBlock (int b, int startLine)
{
    if (song == nullptr || song->blocks.empty()) return;
    if (mode != Mode::Stopped) sendClock (2, chunkPos);
    synthAllOff();
    resetSwitches();
    for (auto& t : synthTrack) t = 99;
    for (auto& g : pendingGlide) g = 0;
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
    uiBlock = block; uiLine = line;
    ppq = line / 4.0;
    clockStartPending = true;
}

bool Player::switchBlock (int b)
{
    if (mode != Mode::Block || song == nullptr || song->blocks.empty()) return false;
    b = song->safeBlock (b);
    if (b == block) return false;
    // carry on from the same line and tick, so the groove doesn't stop
    block = b;
    const int lines = song->blocks[(size_t) block].lines;
    if (line >= lines) line %= lines;
    breakLine = jumpPos = loopJumpLine = -1;
    for (auto& v : voices) v.earlyDone = false;
    uiBlock = block; uiLine = line;
    return true;
}

void Player::stop()
{
    if (mode != Mode::Stopped && ! clockStartPending) sendClock (2, chunkPos);
    clockStartPending = false;
    synthAllOff();
    resetSwitches();
    mode = Mode::Stopped;
    for (int i = 0; i < kMaxTracks; ++i) { voices[i].active = false; voices[i].microDelay = -1; }
}

// ---------------------------------------------------------------------------
void Player::jamNoteOn (int note, int instrIndex, int velocity127, int start, int end, bool loopRegion)
{
    if (song == nullptr || instrIndex < 0 || instrIndex >= kMaxInstruments) return;
    const auto& in = song->instruments[(size_t) instrIndex];
    if (! instrumentIsOn (instrIndex)) return;   // switched off
    if (in.isPlugin())
    {
        // a plug-in instrument plays the note itself
        auto* s = rack.at (instrIndex);
        if (s == nullptr) return;
        for (auto& j : jamSynth)
            if (j.note < 0)
            {
                j.note = note; j.instr = instrIndex; j.midi = midiNoteFor (note + in.transpose);
                s->noteOn (j.midi, std::clamp (velocity127, 1, 127), chunkPos);
                return;
            }
        return;
    }
    if (! in.hasData()) return;

    const int slot = jamNext;
    Voice& v = voices[kMaxTracks + slot];
    jamNext = (jamNext + 1) % kJamVoices;
    v = Voice{};
    v.instr = instrIndex;
    v.finetune = in.finetune;
    v.note = std::clamp (note + in.transpose, 1, (int) kNoteMax);
    v.period = v.outPeriod = periodFor (v.note, v.finetune);
    v.volume = v.outVolume = std::clamp (64 * velocity127 / 127, 0, 64);   // the instrument's VOL is applied as a level
    v.jamNote = note;
    const int len = (int) in.data.size();
    if (start < 0 && in.sliceKeys && in.numSlices() > 0)
    {
        // slice to keys: C-3 = slice 1, C#3 = slice 2... each at its own pitch
        int a = 0, b = 0;
        if (! in.sliceRange (note - 37, a, b)) { v.active = false; return; }
        v.note = std::clamp (37 + in.transpose, 1, (int) kNoteMax);
        v.period = v.outPeriod = periodFor (v.note, v.finetune);
        v.regionStart = a; v.regionEnd = b; v.pos = a;
    }
    else if (start >= 0 && start < len)
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

void Player::jamNoteFrom (int note, int instrIndex, int pos)
{
    jamNoteOn (note, instrIndex, 127, -1, -1, false);
    if (song == nullptr || instrIndex < 0 || instrIndex >= kMaxInstruments) return;
    const auto& in = song->instruments[(size_t) instrIndex];
    const int slot = (jamNext + kJamVoices - 1) % kJamVoices;
    Voice& v = voices[kMaxTracks + slot];
    if (! v.active || v.instr != instrIndex) return;
    v.regionStart = v.regionEnd = -1;
    v.note = std::clamp (note + in.transpose, 1, (int) kNoteMax);
    v.period = v.outPeriod = periodFor (v.note, v.finetune);
    v.pos = std::clamp ((double) pos, 0.0, std::max (0.0, (double) in.data.size() - 1.0));
}

void Player::jamNoteOff (int note)
{
    for (auto& j : jamSynth)
        if (j.note == note) { if (auto* s = rack.at (j.instr)) s->noteOff (j.midi, chunkPos); j = JamSynth {}; }
    for (int i = kMaxTracks; i < kVoices; ++i)
        if (voices[i].jamNote == note) { releaseVoice (voices[i]); voices[i].jamNote = -1; }
}

void Player::jamAllOff()
{
    for (auto& j : jamSynth) if (j.note >= 0) { if (auto* s = rack.at (j.instr)) s->noteOff (j.midi, chunkPos); j = JamSynth {}; }
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
    advanceAutomation();

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
    const int next = (inLineLoop() && line == lpTo) ? lpFrom : line + 1;
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
    v.volume = 64;   // note volume (Cxx etc.); the instrument's VOL scales it at the output
    v.finetune = in.finetune;
}

void Player::triggerNote (Voice& v, const Cell& cell, int ch)
{
    if (v.instr < 0) { v.active = false; return; }
    if (isSynthVoice (v)) { synthTrigger (v, cell, ch); return; }
    const auto& in = song->instruments[(size_t) v.instr];
    if (! in.hasData()) { v.active = false; return; }
    if (! instrumentIsOn (v.instr))
    {
        // switched off: no new note (what's sounding fades out)
        if (v.active && v.fadeOut == 0) v.fadeLen = v.fadeOut = std::max (1, (int) (sampleRate * 0.005));
        return;
    }
    v.fadeOut = 0;
    const double len = (double) in.data.size();

    int note = (int) cell.note + in.transpose;
    uint8_t p = 0;
    // reset per-note state
    v.regionStart = v.regionEnd = -1; v.regionLoop = false;
    v.reverse = false; v.pingBack = false; v.pitchMul = 1.0;

    // slice to keys: the note picks the slice (C-3 = slice 1) and plays it at its own pitch
    int keySlice = -1;
    if (in.sliceKeys && in.numSlices() > 0)
    {
        keySlice = (int) cell.note - 37;
        int a = 0, b = 0;
        if (! in.sliceRange (keySlice, a, b)) { v.active = false; return; }
        note = 37 + in.transpose;
    }
    // Ixx: transpose just this note (I80 = none, I8C = +12, I74 = -12)
    if (cell.has (C_I, p) && p != 0) note += std::clamp ((int) p - 128, -48, 48);
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
    if (keySlice >= 0)
    {
        int a = 0, b = 0;
        in.sliceRange (keySlice, a, b);
        v.regionStart = a; v.regionEnd = b; v.pos = a;
    }
    else if (cell.has (C_S, p))
    {
        int a = 0, b = 0;
        if (in.numSlices() > 0)
        {
            // the instrument's own slice points (S00 = slice 1)
            if (! in.sliceRange (p % in.numSlices(), a, b)) { a = 0; b = (int) len; }
            v.regionStart = a; v.regionEnd = b;
        }
        else
        {
            const int sl = p & 15;
            v.regionStart = std::floor (sl * len / 16.0);
            v.regionEnd = std::floor ((sl + 1) * len / 16.0);
        }
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
    synthRelease (v);
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
    // Yxx: the next knob change on this track glides over xx ticks (this line or a later one)
    uint8_t yp = 0;
    const bool hasY = cell.has (C_Y, yp);
    rowGlide = hasY ? yp : pendingGlide[ch];
    knobSetThisRow = false;
    v.chordLine = false;
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
            if ((porta || glide) && (v.active || v.synthNote >= 0) && v.instr >= 0)
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
    synthBend (v);
    if (knobSetThisRow) pendingGlide[ch] = 0;
    else if (hasY) pendingGlide[ch] = yp;
}

void Player::applyRowCommand (Voice& v, uint8_t cmd, uint8_t param, const Cell& cell)
{
    const int x = param >> 4, y = param & 15;
    if (cmd == 0 && param == 0) return;
    const int ch = (int) (&v - voices);
    // automation: the track's knobs, and a plug-in instrument's own knobs / mod wheel
    if (ch >= 0 && ch < kMaxTracks && (cmd == C_V || cmd == C_J))
    {
        const int k = cmd == C_V ? 0 : 1;
        setLane (trackLanes[ch][k], param / 255.0f, rowGlide, 0, ch, k);
        return;
    }
    if (isSynthVoice (v) && (cmd == C_L || cmd == C_Q || cmd == C_H || cmd == C_W || cmd == C_O))
    {
        if (cmd == C_O) { if (auto* s = rack.at (v.instr)) s->controller (1, param >> 1, chunkPos); return; }
        const int k = cmd == C_L ? 0 : cmd == C_Q ? 1 : cmd == C_H ? 2 : 3;
        if (ch >= 0 && ch < kMaxTracks) setLane (instLanes[v.instr][k], param / 255.0f, rowGlide, 1, v.instr, k);
        return;
    }
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
        case 0x0: if (param && ! v.chordLine) arp = (tick % 3 == 1) ? x : (tick % 3 == 2 ? y : 0); break;   // (CHORD: held instead)
        case 0x1: v.period = std::max (kMinPeriod, v.period - param); break;
        case 0x2: v.period = std::min (kMaxPeriod, v.period + param); break;
        case 0x3: porta(); break;
        case 0x4: vib = vibratoDelta (v.vibPos, v.vibDepth, v.vibWave, 7); v.vibPos = (uint8_t) ((v.vibPos + v.vibSpeed) & 63); break;
        case 0x5: porta(); volSlide(); break;
        case 0x6: vib = vibratoDelta (v.vibPos, v.vibDepth, v.vibWave, 7); v.vibPos = (uint8_t) ((v.vibPos + v.vibSpeed) & 63); volSlide(); break;
        case 0x7: trem = (int) vibratoDelta (v.tremPos, v.tremDepth, v.tremWave, 6); v.tremPos = (uint8_t) ((v.tremPos + v.tremSpeed) & 63); break;
        case 0xA: volSlide(); break;
        case 0xE:
            if (x == 0x9 && y > 0 && tick % y == 0 && v.synthNote >= 0) synthRetrigger (v, 0);
            if (x == 0x9 && y > 0 && tick % y == 0 && v.instr >= 0 && song->instruments[(size_t) v.instr].hasData()) { v.pos = v.regionStart > 0 ? v.regionStart : 0; v.active = true; }
            if (x == 0xC && tick == y) { v.volume = 0; synthRelease (v); }
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
        else if (c.note > 0) { chordSource = &v.delayedCell; triggerNote (v, c, ch); chordSource = nullptr; }
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
    if (v.rollEvery > 0 && tick > 0 && tick % v.rollEvery == 0 && v.synthNote >= 0)
        synthRetrigger (v, v.rollVolStep * 2);
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
    synthBend (v);
}

void Player::setLineLoop (int b, int from, int to)
{
    if (b < 0 || song == nullptr || b >= (int) song->blocks.size()) { lpBlock = -1; return; }
    const int lines = song->blocks[(size_t) b].lines;
    lpFrom = std::clamp (std::min (from, to), 0, lines - 1);
    lpTo = std::clamp (std::max (from, to), 0, lines - 1);
    lpBlock = b;
}

int Player::nextSongPos (int prev, int wanted)
{
    const int seqLen = (int) song->sequence.size();
    if (songLoopFrom < 0 || songLoopFrom >= seqLen) return wanted;
    const int to = std::min (songLoopTo, seqLen - 1);
    const bool wasInside = prev >= songLoopFrom && prev <= to;
    const bool staysInside = wanted >= songLoopFrom && wanted <= to;
    if (wasInside && staysInside) return wanted;
    return songLoopFrom;   // end of the loop, or outside it: go (back) to its start
}

void Player::advanceLine()
{
    if (rowDelay > 0) { rowDelayCount = rowDelay; rowDelay = 0; }
    if (rowDelayCount > 0) { --rowDelayCount; firstTickOfRow = false; return; }
    firstTickOfRow = true;

    const int lines = song->blocks[(size_t) block].lines;
    const int seqLen = (int) song->sequence.size();

    if (inLineLoop() && loopJumpLine < 0)
    {
        // the marked loop wins over block ends and pattern jumps
        line = line >= std::min (lpTo, lines - 1) ? lpFrom : line + 1;
        breakLine = jumpPos = -1;
        uiSeqPos = seqPos; uiBlock = block; uiLine = line;
        return;
    }
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
            if (seqPos <= prev && songLoopFrom < 0) ++songWraps;
            seqPos = nextSongPos (prev, seqPos);
            block = song->safeBlock (song->sequence[(size_t) seqPos]);
        }
        line = breakLine >= 0 ? std::clamp (breakLine, 0, song->blocks[(size_t) block].lines - 1) : 0;
    }
    else if (++line >= lines)
    {
        line = 0;
        if (mode == Mode::Song)
        {
            const int prev = seqPos;
            if (++seqPos >= seqLen) { seqPos = 0; if (songLoopFrom < 0) ++songWraps; }
            seqPos = nextSongPos (prev, seqPos);
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
    const int drop = options.dropBits();

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
    const bool ping = looped && in.pingPong && v.regionStart < 0 && loopLen >= 2.0;

    auto conv = [drop] (int s) -> float
    {
        if (drop > 0) s = (s >> drop) * (1 << drop);   // 8-bit Paula / 12-bit S950
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
        if (v.pingBack && v.pos < loopStart)
        {
            // ping-pong: bounce off the loop start and go forwards again
            v.pos = std::min (end - 1.0, loopStart + (loopStart - v.pos));
            v.reverse = v.pingBack = false;
        }
        else if (v.pos < begin) { if (looped && loopLen >= 4) v.pos = end - 1.0; else v.active = false; }
    }
    else
    {
        bool bounced = false;
        auto wrap = [&] (double& p) -> bool
        {
            if (p < end) return true;
            if (! looped || loopLen < 1.0) return false;
            if (ping)
            {
                // ping-pong: bounce off the loop end and play backwards
                p = std::max (loopStart, end - 1.0 - (p - end));
                v.reverse = v.pingBack = true;
                bounced = true;
                return true;
            }
            while (p >= end) p -= loopLen;
            return true;
        };
        if (options.stepped())
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
                if (bounced) break;
            }
            const double used = std::max (1e-9, inc - remaining);   // less than inc if a ping-pong bounce cut it short
            L = (float) (accL / used);
            R = stereo ? (float) (accR / used) : L;
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
    // note volume (Cxx, slides, tremolo) times the instrument's level
    float g = (float) v.outVolume / 64.0f * (float) std::clamp (in.volume, 0, 64) / 64.0f;
    if (v.fadeOut > 0)
    {
        g *= (float) v.fadeOut / (float) v.fadeLen;
        if (--v.fadeOut == 0) v.active = false;
    }
    outL = L * g;
    outR = R * g;
}

void Player::render (float* left, float* right, int numSamples)
{
    // in blocks the mixer can take
    for (int done = 0; done < numSamples;)
    {
        const int n = std::min (numSamples - done, MixerEngine::kMaxBlock);
        renderChunk (left + done, right + done, n);
        done += n;
    }
}

void Player::outputStage (float& L, float& R, bool led)
{
    if (akStages > 0)
    {
        for (int k = 0; k < akStages; ++k) { L = akL[k].process (L); R = akR[k].process (R); }
        L = hpL.hp (L);
        R = hpR.hp (R);
    }
    else if (options.model != PaulaOptions::Model::Clean)
    {
        L = hpL.hp (lpL.lp (L));
        R = hpR.hp (lpR.lp (R));
    }
    if (led && ! options.isAkai())
    {
        L = ledL.process (L);
        R = ledR.process (R);
    }
    const float g = options.master;
    L = softClip (L * g);
    R = softClip (R * g);
}

void Player::updateSynths (int numSamples)
{
    (void) numSamples;
    if (song == nullptr) return;
    // settings, which instrument sends each device's clock, latencies
    int synthLat[kMaxTracks] {};
    int lead = 0;
    for (int i = 0; i < kMaxInstruments; ++i)
    {
        clockOwner[i] = false;
        auto* s = rack.at (i);
        if (s == nullptr) continue;
        const auto& in = song->instruments[(size_t) i];
        s->update (in);
        if (in.isMidiOut())
        {
            lead = std::max (lead, -in.midiDelayMs);
            if (in.midiClock)
            {
                bool first = true;
                for (int j = 0; j < i && first; ++j)
                {
                    const auto& o = song->instruments[(size_t) j];
                    if (rack.at (j) != nullptr && o.isMidiOut() && o.midiClock && o.midiDevice == in.midiDevice) first = false;
                }
                clockOwner[i] = first;
            }
        }
        else if (instrumentIsOn (i))
        {
            const int t = synthOutputTrack (i);
            if (t >= 0 && t < kMaxTracks) synthLat[t] = std::max (synthLat[t], std::clamp (s->latency(), 0, MixerEngine::kMaxSlotDelay));
        }
    }
    for (int t = 0; t < kMaxTracks; ++t) { sampleDelay[t] = synthLat[t]; mixerEngine.setInputLatency (t, synthLat[t]); }
    mixerEngine.setExtraDelay ((int) std::lround (lead * sampleRate / 1000.0));
}

void Player::renderChunk (float* left, float* right, int numSamples)
{
    if (options.model != lastModel) updateFilters();
    if (rack.any()) updateSynths (numSamples);
    else { for (auto& d : sampleDelay) d = 0; for (int t = 0; t < kMaxTracks; ++t) mixerEngine.setInputLatency (t, 0); mixerEngine.setExtraDelay (0); }
    bool anyClock = false;
    for (bool o : clockOwner) anyClock = anyClock || o;
    if (clockStartPending && mode != Mode::Stopped)
    {
        clockStartPending = false;
        if (anyClock) sendClock (1, 0);
        nextClock = (long long) std::ceil (ppq * 24.0 - 1e-9);
    }
    // with a mixer in use each track goes to its own buffer first
    // (plug-in instruments always go through the mixer: they play into a track's strip)
    const bool mixActive = song != nullptr && (forceMixPath || ! song->mixer.isFlat() || rack.any());

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
        chunkPos = i;
        if (mode != Mode::Stopped && song != nullptr)
        {
            tickRemain -= 1.0;
            if (tickRemain <= 0.0)
            {
                doTick();
                tickRemain += samplesPerTick();
            }
            // MIDI clock: 24 a beat (4 lines a beat)
            if (mode != Mode::Stopped)
            {
                const double beatBpm = (tempoOverride > 0 ? tempoOverride : (double) tempo) * 6.0 / std::max (1, speed);
                ppq += beatBpm / 60.0 / sampleRate;
                if (anyClock) while (ppq * 24.0 >= (double) nextClock) { sendClock (0, i); ++nextClock; }
                else nextClock = (long long) std::ceil (ppq * 24.0 - 1e-9);
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

        // preview (jam) notes
        float jl = 0, jr = 0;
        for (int j = kMaxTracks; j < kVoices; ++j)
        {
            const float p = isFixed[j] ? fixedPos[j] : 0.0f;
            if (st[j]) { jl += valL[j] * 0.5f * std::min (1.0f, 1.0f - p); jr += valR[j] * 0.5f * std::min (1.0f, 1.0f + p); }
            else addPanned (valL[j] * 0.7f, p, jl, jr);
        }

        if (mixActive)
        {
            // each track on its own (paired tracks in 8-track songs share the level they'd have had)
            const float scale = nt > 4 ? 0.5f : 1.0f;
            for (int t = 0; t < kMaxTracks; ++t)
            {
                float tl = 0, tr = 0;
                if (t < nt)
                {
                    addVoice (t, scale, sidePan (nt > 4 ? t / 2 : t), tl, tr);
                    if (nt > 4 && options.crunches()) { tl = quant8 (tl); tr = quant8 (tr); }
                }
                trkL[t][i] = tl; trkR[t][i] = tr;
            }
            jamL[i] = jl; jamR[i] = jr;
            ledBuf[i] = ledOn;
            continue;
        }

        float L = 0, R = 0;
        if (nt > 4)
        {
            for (int p = 0; p < 4; ++p)
            {
                const int a = p * 2, b = p * 2 + 1;
                if (! isFixed[a] && ! isFixed[b] && ! st[a] && ! st[b])
                {
                    float s = (valL[a] + valL[b]) * 0.5f;
                    if (options.crunches()) s = quant8 (s);
                    addPanned (s, sidePan (p), L, R);
                }
                else
                {
                    float pl = 0, pr = 0;
                    addVoice (a, 0.5f, sidePan (p), pl, pr);
                    addVoice (b, 0.5f, sidePan (p), pl, pr);
                    if (options.crunches()) { pl = quant8 (pl); pr = quant8 (pr); }
                    L += pl; R += pr;
                }
            }
        }
        else
        {
            for (int t = 0; t < nt; ++t) addVoice (t, 1.0f, sidePan (t), L, R);
        }
        L += jl; R += jr;
        outputStage (L, R, ledOn);
        left[i] = L; right[i] = R;
    }

    chunkPos = 0;
    // samples on a track wait for its plug-in instrument's latency
    if (mixActive)
        for (int t = 0; t < nt; ++t)
            if (sampleDelay[t] > 0 || sampleAlign[t].maxDelay() > 0) sampleAlign[t].process (trkL[t], trkR[t], numSamples, sampleDelay[t]);
    if (mixActive && rack.any())
    {
        // plug-in instruments: each into its track's strip, at the instrument's VOL
        for (int ins = 0; ins < kMaxInstruments; ++ins)
        {
            auto* s = rack.at (ins);
            if (s == nullptr) continue;
            s->render (synthL, synthR, numSamples);
            const int t = synthOutputTrack (ins);
            if (! instrumentIsOn (ins)) continue;   // (still run, so it's ready when switched back on)
            if (t >= nt || mute[t].load (std::memory_order_relaxed)) continue;
            const float g = (float) std::clamp (song->instruments[(size_t) ins].volume, 0, 64) / 64.0f;
            float pk = 0;
            for (int i = 0; i < numSamples; ++i)
            {
                const float l = synthL[i] * g, r = synthR[i] * g;
                trkL[t][i] += l; trkR[t][i] += r;
                pk = std::max ({ pk, std::abs (l), std::abs (r) });
            }
            peaks[t] = std::max (peaks[t], pk);
        }
    }
    if (mixActive)
    {
        float* tl[kMaxTracks]; float* tr[kMaxTracks];
        for (int t = 0; t < kMaxTracks; ++t) { tl[t] = trkL[t]; tr[t] = trkR[t]; }
        // the song's beat tempo (4 lines a beat) for synced delays
        const double bpm = (tempoOverride > 0 ? tempoOverride : (double) tempo) * 6.0 / std::max (1, speed);
        mixerEngine.process (song->mixer, tl, tr, nt, jamL, jamR, left, right, numSamples, bpm);
        for (int i = 0; i < numSamples; ++i) outputStage (left[i], right[i], ledBuf[i]);
        if (stemTap != nullptr)
        {
            for (int t = 0; t < nt; ++t) stemTap->add (t, trkL[t], trkR[t], numSamples);
            for (int r = 0; r < kReturns; ++r) stemTap->add (kMaxTracks + r, mixerEngine.returnLeft (r), mixerEngine.returnRight (r), numSamples);
        }
    }
    if (stemTap != nullptr) stemTap->add (kStems - 1, left, right, numSamples);

    for (int t = 0; t < kMaxTracks; ++t)
    {
        uiLevel[t].store (peaks[t], std::memory_order_relaxed);
        uiTrackInstr[t].store (voices[t].instr, std::memory_order_relaxed);
    }
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
