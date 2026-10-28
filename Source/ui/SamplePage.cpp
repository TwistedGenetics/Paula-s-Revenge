// Paula's Revenge - SAMPLE page
#include "SamplePage.h"
#include "../Prefs.h"

namespace prv::ui
{
const int SamplePage::kAmigaRates[5] = { 0, 8363, 16726, 22050, 28867 };

namespace
{
    juce::String hex2 (int v) { return juce::String::toHexString (v).toUpperCase().paddedLeft ('0', 2); }
    juce::String mmss (double secs)
    {
        const int t = (int) std::floor (std::max (0.0, secs) * 10.0);
        return juce::String (t / 600) + ":" + juce::String ((t / 10) % 60).paddedLeft ('0', 2) + "." + juce::String (t % 10);
    }
    float dbToGain (float db) { return db <= -59.9f ? 0.0f : std::pow (10.0f, db / 20.0f); }
    float toFrac (float lin) { return juce::jlimit (0.0f, 1.0f, (juce::Decibels::gainToDecibels (lin, -60.0f) + 60.0f) / 60.0f); }
}

SamplePage::SamplePage (PaulaProcessor& p) : proc (p)
{
    setWantsKeyboardFocus (true);
    setOpaque (true);
    Theme::markDarkPage (*this);

    closeBtn.onClick = [this] { if (onClose) onClose(); };
    settingsBtn.onClick = [this] { if (onAudioSettings) onAudioSettings(); else status ("AUDIO/MIDI SETTINGS: IN THE APP MENU"); };
    inputBtn.onClick = [this] { setSource (PaulaProcessor::SourceInput); };
    resampleBtn.onClick = [this] { setSource (PaulaProcessor::SourceResample); };
    monitorBtn.onClick = [this] { setMonitor (monitorBtn.getToggleState()); };
    stereoBtn.onClick = [this] { setChannels (RecChannels::Stereo); };
    monoBtn.onClick = [this] { setChannels (RecChannels::Mono); };
    leftBtn.onClick = [this] { setChannels (RecChannels::Left); };
    rightBtn.onClick = [this] { setChannels (RecChannels::Right); };
    manualBtn.onClick = [this] { setTrigger (RecTrigger::Manual); };
    thresholdBtn.onClick = [this] { setTrigger (RecTrigger::Threshold); };
    playbackBtn.onClick = [this] { setTrigger (RecTrigger::WithPlayback); };
    recordBtn.onClick = [this] { record(); };
    stopBtn.onClick = [this] { stop(); };
    keepBtn.onClick = [this] { keep(); };
    againBtn.onClick = [this] { again(); };
    trimBtn.onClick = [this] { savePrefs(); };
    normBtn.onClick = [this] { savePrefs(); };
    cleanBtn.onClick = [this] { setModel (SamplerModel::Clean); };
    amigaBtn.onClick = [this] { setModel (SamplerModel::Amiga); };
    s950Btn.onClick = [this] { setModel (SamplerModel::S950); };
    bits8Btn.onClick = [this] { setBits (8); };
    bits12Btn.onClick = [this] { setBits (12); };
    driveSlider.labelChars = 12; driveSlider.valueChars = 8;
    driveSlider.format = [] (float v) { return "+" + juce::String (v * 24.0f, 1) + " DB"; };
    driveSlider.onChange = [this] (float v) { setDriveDb (v * 24.0f); };
    // INPUT GAIN: -24..+24 dB (the middle = 0 dB); THRESHOLD: -60..0 dB
    gainSlider.labelChars = 12; gainSlider.valueChars = 9;
    gainSlider.format = [] (float v) { const float db = v * 48.0f - 24.0f; return (db > 0.05f ? "+" : "") + juce::String (db, 1) + " DB"; };
    gainSlider.onChange = [this] (float v) { setGainDb (v * 48.0f - 24.0f); };
    threshSlider.labelChars = 12; threshSlider.valueChars = 9;
    threshSlider.format = [] (float v) { return juce::String (v * 60.0f - 60.0f, 1) + " DB"; };
    threshSlider.onChange = [this] (float v) { setThresholdDb (v * 60.0f - 60.0f); };

    for (juce::Component* c : std::initializer_list<juce::Component*> { &closeBtn, &settingsBtn, &inputBtn, &resampleBtn, &monitorBtn, &stereoBtn, &monoBtn, &leftBtn, &rightBtn,
                                &manualBtn, &thresholdBtn, &playbackBtn, &recordBtn, &stopBtn, &keepBtn, &againBtn, &trimBtn, &normBtn,
                                &gainSlider, &threshSlider, &cleanBtn, &amigaBtn, &s950Btn, &bits8Btn, &bits12Btn, &driveSlider })
        addAndMakeVisible (c);
    loadPrefs();
    makeRateButtons();
    refreshButtons();
}

SamplePage::~SamplePage() = default;

// ---------------------------------------------------------------- settings
void SamplePage::loadPrefs()
{
    auto& pf = prv::prefs();
    proc.recSource = juce::jlimit (0, 1, pf.getIntValue ("sampleSource", 0));
    channels = (RecChannels) juce::jlimit (0, 3, pf.getIntValue ("sampleChannels", 0));
    trigger = (RecTrigger) juce::jlimit (0, 2, pf.getIntValue ("sampleTrigger", 0));
    amigaIndex = juce::jlimit (0, 4, pf.getIntValue ("sampleAmigaRate", 0));
    thresholdDb = juce::jlimit (-60.0f, 0.0f, (float) pf.getDoubleValue ("sampleThresholdDb", -30.0));
    gainDb = juce::jlimit (-24.0f, 24.0f, (float) pf.getDoubleValue ("sampleGainDb", 0.0));
    trimBtn.setToggleState (pf.getBoolValue ("sampleTrim", true), juce::dontSendNotification);
    normBtn.setToggleState (pf.getBoolValue ("sampleNormalise", true), juce::dontSendNotification);
    proc.inputGain = dbToGain (gainDb);
    proc.monitorInput = false;   // (always off to start with: feedback)
    samplerModel = (SamplerModel) juce::jlimit (0, 2, pf.getIntValue ("samplerModel", 0));
    bits = pf.getIntValue ("samplerBits", 8) == 12 ? 12 : 8;
    amigaRateIdx = juce::jlimit (0, 3, pf.getIntValue ("samplerAmigaRate", 1));
    s950RateIdx = juce::jlimit (0, 7, pf.getIntValue ("samplerS950Rate", 3));
    driveDb = juce::jlimit (0.0f, 24.0f, (float) pf.getDoubleValue ("samplerDriveDb", 0.0));
    driveSlider.setValue (driveDb / 24.0f);
    applyVintage();
    gainSlider.setValue ((gainDb + 24.0f) / 48.0f);
    threshSlider.setValue ((thresholdDb + 60.0f) / 60.0f);
}

void SamplePage::savePrefs()
{
    auto& pf = prv::prefs();
    pf.setValue ("sampleSource", proc.recSource.load());
    pf.setValue ("sampleChannels", (int) channels);
    pf.setValue ("sampleTrigger", (int) trigger);
    pf.setValue ("sampleAmigaRate", amigaIndex);
    pf.setValue ("sampleThresholdDb", (double) thresholdDb);
    pf.setValue ("sampleGainDb", (double) gainDb);
    pf.setValue ("sampleTrim", trimBtn.getToggleState());
    pf.setValue ("sampleNormalise", normBtn.getToggleState());
    pf.setValue ("samplerModel", (int) samplerModel);
    pf.setValue ("samplerBits", bits);
    pf.setValue ("samplerAmigaRate", amigaRateIdx);
    pf.setValue ("samplerS950Rate", s950RateIdx);
    pf.setValue ("samplerDriveDb", (double) driveDb);
}

VintageSettings SamplePage::vintageSettings() const
{
    VintageSettings v;
    v.model = samplerModel;
    v.bits = bits;
    v.rate = samplerModel == SamplerModel::Amiga ? kAmigaSampleRates[amigaRateIdx] : samplerModel == SamplerModel::S950 ? kS950SampleRates[s950RateIdx] : 0.0;
    v.driveDb = driveDb;
    return v;
}

void SamplePage::applyVintage() { proc.setVintage (vintageSettings()); }

void SamplePage::makeRateButtons()
{
    // CLEAN: AMIGA RATE (resampled as it's kept); AMIGA / S950: the sampler's own rates
    for (auto& b : rateBtns) removeChildComponent (b.get());
    rateBtns.clear();
    std::vector<juce::String> names;
    if (samplerModel == SamplerModel::Clean) for (int i = 0; i < 5; ++i) names.push_back (i == 0 ? juce::String ("NONE") : juce::String (kAmigaRates[i]));
    else if (samplerModel == SamplerModel::Amiga) for (double r : kAmigaSampleRates) names.push_back (juce::String ((int) r));
    else for (double r : kS950SampleRates) names.push_back (r == 7500 ? juce::String ("7.5K") : r == 22050 ? juce::String ("22K") : juce::String ((int) (r / 1000)) + "K");   // (8 of them: short)
    for (int i = 0; i < (int) names.size(); ++i)
    {
        auto b = std::make_unique<PixButton> (names[(size_t) i], true);
        b->onClick = [this, i] { setRateIndex (i); };
        addAndMakeVisible (*b);
        rateBtns.push_back (std::move (b));
    }
    if (getWidth() > 0) resized();
}

bool SamplePage::busy() const
{
    const auto rs = proc.recorder.state();
    return rs == TakeRecorder::State::Armed || rs == TakeRecorder::State::Recording;
}

void SamplePage::setModel (SamplerModel m)
{
    if (busy()) { status ("STOP RECORDING FIRST"); refreshButtons(); return; }
    samplerModel = m;
    if (m == SamplerModel::Amiga) bits = 8;
    if (m == SamplerModel::S950) bits = 12;
    savePrefs(); applyVintage(); makeRateButtons(); refreshButtons();
    status (m == SamplerModel::Amiga ? "AMIGA SAMPLER: 8-BIT, NO ANTI-ALIAS FILTER - THE GRAIN AND THE ALIASING (MONITOR TO HEAR IT AS YOU SAMPLE)"
          : m == SamplerModel::S950 ? "AKAI S950: 12-BIT, ITS FILTER FOLLOWS THE RATE - PUSH INPUT DRIVE TO OVERLOAD IT LIKE THE CLASSIC RECORDS"
                                    : "CLEAN: THE INTERFACE AS IT IS (THE DEFAULT)");
}

void SamplePage::setBits (int b)
{
    if (busy()) { status ("STOP RECORDING FIRST"); refreshButtons(); return; }
    bits = b == 12 ? 12 : 8;
    savePrefs(); applyVintage(); refreshButtons();
    status (juce::String (bits) + "-BIT");
}

void SamplePage::setRateIndex (int i)
{
    if (samplerModel == SamplerModel::Clean) { setAmigaRate (i); return; }
    if (busy()) { status ("STOP RECORDING FIRST"); refreshButtons(); return; }
    if (samplerModel == SamplerModel::Amiga) amigaRateIdx = juce::jlimit (0, 3, i);
    else s950RateIdx = juce::jlimit (0, 7, i);
    savePrefs(); applyVintage(); refreshButtons();
    status ("SAMPLING AT " + juce::String ((int) vintageSettings().rate) + " HZ");
}

void SamplePage::setDriveDb (float db)
{
    driveDb = juce::jlimit (0.0f, 24.0f, db);
    savePrefs();
    proc.withLock ([&] { proc.vintage.setDrive (driveDb); });
}

void SamplePage::setSource (int s)
{
    proc.recSource = juce::jlimit (0, 1, s);
    savePrefs(); refreshButtons();
    status (s == PaulaProcessor::SourceResample ? "RESAMPLE: RECORDS EVERYTHING THE APP PLAYS - PLAY THE SONG OR JAM, THEN RECORD"
                                                : "INPUT: RECORDS THE AUDIO INTERFACE / MIC / LINE IN");
}
void SamplePage::setChannels (RecChannels c) { channels = c; savePrefs(); refreshButtons(); }
void SamplePage::setTrigger (RecTrigger t)
{
    trigger = t; savePrefs(); refreshButtons();
    status (t == RecTrigger::Threshold ? "THRESHOLD: RECORD ARMS IT; IT STARTS WHEN THE SOUND GOES OVER THE LINE ON THE METER"
          : t == RecTrigger::WithPlayback ? "WITH PLAYBACK: RECORD ARMS IT; IT RECORDS FROM PLAY TO STOP"
                                          : "MANUAL: RECORD STARTS STRAIGHT AWAY");
}
void SamplePage::setMonitor (bool on)
{
    proc.monitorInput = on;
    refreshButtons();
    status (on ? "MONITOR ON: YOU HEAR THE INPUT - USE HEADPHONES WITH A MIC (FEEDBACK)" : "MONITOR OFF");
}
void SamplePage::setAmigaRate (int i) { amigaIndex = juce::jlimit (0, 4, i); savePrefs(); refreshButtons(); }
void SamplePage::setThresholdDb (float db) { thresholdDb = juce::jlimit (-60.0f, 0.0f, db); savePrefs(); repaint (meterArea); }
void SamplePage::setGainDb (float db) { gainDb = juce::jlimit (-24.0f, 24.0f, db); proc.inputGain = dbToGain (gainDb); savePrefs(); }

void SamplePage::refreshButtons()
{
    const int src = proc.recSource.load();
    inputBtn.setToggleState (src == PaulaProcessor::SourceInput, juce::dontSendNotification);
    resampleBtn.setToggleState (src == PaulaProcessor::SourceResample, juce::dontSendNotification);
    monitorBtn.setToggleState (proc.monitorInput.load(), juce::dontSendNotification);
    stereoBtn.setToggleState (channels == RecChannels::Stereo, juce::dontSendNotification);
    monoBtn.setToggleState (channels == RecChannels::Mono, juce::dontSendNotification);
    leftBtn.setToggleState (channels == RecChannels::Left, juce::dontSendNotification);
    rightBtn.setToggleState (channels == RecChannels::Right, juce::dontSendNotification);
    manualBtn.setToggleState (trigger == RecTrigger::Manual, juce::dontSendNotification);
    thresholdBtn.setToggleState (trigger == RecTrigger::Threshold, juce::dontSendNotification);
    playbackBtn.setToggleState (trigger == RecTrigger::WithPlayback, juce::dontSendNotification);
    const int rateSel = samplerModel == SamplerModel::Clean ? amigaIndex : samplerModel == SamplerModel::Amiga ? amigaRateIdx : s950RateIdx;
    for (int i = 0; i < (int) rateBtns.size(); ++i) rateBtns[(size_t) i]->setToggleState (i == rateSel, juce::dontSendNotification);
    cleanBtn.setToggleState (samplerModel == SamplerModel::Clean, juce::dontSendNotification);
    amigaBtn.setToggleState (samplerModel == SamplerModel::Amiga, juce::dontSendNotification);
    s950Btn.setToggleState (samplerModel == SamplerModel::S950, juce::dontSendNotification);
    bits8Btn.setToggleState (samplerModel != SamplerModel::Clean && bits == 8, juce::dontSendNotification);
    bits12Btn.setToggleState (samplerModel != SamplerModel::Clean && bits == 12, juce::dontSendNotification);
    bits8Btn.setEnabled (samplerModel != SamplerModel::Clean);
    bits12Btn.setEnabled (samplerModel != SamplerModel::Clean);
    driveSlider.setEnabled (samplerModel != SamplerModel::Clean);
    const auto rs = proc.recorder.state();
    const bool busy = rs == TakeRecorder::State::Armed || rs == TakeRecorder::State::Recording;
    recordBtn.setToggleState (busy, juce::dontSendNotification);
    recordBtn.setButtonText (rs == TakeRecorder::State::Armed ? "ARMED" : rs == TakeRecorder::State::Recording ? "REC" : "RECORD");
    stopBtn.setEnabled (busy);
    keepBtn.setEnabled (hasTake());
    againBtn.setEnabled (hasTake());
    threshSlider.setEnabled (trigger == RecTrigger::Threshold);
    gainSlider.setEnabled (src == PaulaProcessor::SourceInput);
    repaint();
}

// ---------------------------------------------------------------- recording
void SamplePage::opened()
{
    refreshButtons();
    grabKeyboardFocus();
    const int ins = proc.editorState.instrument;
    if (proc.recSource.load() == PaulaProcessor::SourceInput && proc.inputChannels.load() == 0 && proc.audioRunning.load())
        status ("NO AUDIO INPUT IS OPEN - CHOOSE ONE IN AUDIO/MIDI SETTINGS, OR RECORD WITH RESAMPLE");
    else status ("SAMPLE INTO INSTRUMENT " + hex2 (ins + 1) + ": RECORD (R) - THEN KEEP (ENTER) OR AGAIN");
}

void SamplePage::record()
{
    const auto rs = proc.recorder.state();
    if (rs == TakeRecorder::State::Armed || rs == TakeRecorder::State::Recording) { stop(); return; }
    takeL.clear(); takeR.clear(); takeHitLimit = false;
    applyVintage();   // (the device's rate may have changed)
    const double rate = proc.vintage.active() ? proc.vintage.outRate() : (proc.audioRate() > 0 ? proc.audioRate() : 44100.0);
    proc.withLock ([&] { proc.recorder.arm (rate, kMaxSeconds, trigger, dbToGain (thresholdDb)); });
    refreshButtons();
    if (proc.recSource.load() == PaulaProcessor::SourceInput && proc.inputChannels.load() == 0 && proc.audioRunning.load())
        status ("RECORDING - BUT NO AUDIO INPUT IS OPEN: CHOOSE ONE IN AUDIO/MIDI SETTINGS");
    else status (trigger == RecTrigger::Threshold ? "ARMED: WAITING FOR THE SOUND TO GO OVER THE THRESHOLD (STOP CANCELS)"
               : trigger == RecTrigger::WithPlayback ? "ARMED: PLAY THE SONG OR BLOCK - IT RECORDS UNTIL YOU STOP PLAYING"
                                                     : "RECORDING - RECORD AGAIN OR STOP TO END IT");
}

void SamplePage::stop()
{
    const bool wasArmed = proc.recorder.state() == TakeRecorder::State::Armed;
    proc.withLock ([&] { proc.recorder.stop(); });
    if (wasArmed) status ("CANCELLED - NOTHING RECORDED");
    tick();
    refreshButtons();
}

void SamplePage::tick()
{
    // the meter: what the recorder hears (the input after INPUT GAIN, or the app's output)
    const float pl = proc.recorder.takePeak (0), pr = proc.recorder.takePeak (1);
    meterL = std::max (pl, meterL * 0.80f);
    meterR = std::max (pr, meterR * 0.80f);
    // peak hold: 1.5 s, then it falls back to the level
    if (pl >= holdL || pr >= holdR) holdCount = 45;
    holdL = std::max (holdL, pl); holdR = std::max (holdR, pr);
    if (--holdCount <= 0) { holdL = meterL; holdR = meterR; holdCount = 0; }
    if (proc.recorder.takeClip()) clipCount = 60;
    else if (clipCount > 0) --clipCount;

    const auto rs = proc.recorder.state();
    if (rs == TakeRecorder::State::Recording)
    {
        proc.recorder.recentFrames (live, std::max (64, waveArea.getWidth()) * 64);
    }
    else if (rs == TakeRecorder::State::Done)
    {
        // the take: out of the recorder (a swap, under the lock), shown as a waveform
        takeRate = proc.recorder.sampleRate();
        takeHitLimit = proc.recorder.hitLimit();
        proc.withLock ([&] { proc.recorder.takeAway (takeL, takeR); });
        live.clear();
        refreshButtons();
        if (takeL.empty()) status ("NOTHING RECORDED");
        else status ("TAKE: " + mmss (takeSeconds()) + (takeHitLimit ? " - THE 5 MINUTE LIMIT" : juce::String())
                     + " - KEEP (ENTER) PUTS IT IN INSTRUMENT " + hex2 (proc.editorState.instrument + 1) + ", AGAIN THROWS IT AWAY");
        if (onChanged) onChanged();
    }
    if (rs != TakeRecorder::State::Idle || recordBtn.getToggleState()) refreshButtons();
    repaint (meterArea.getUnion (clockArea).getUnion (waveArea));
}

bool SamplePage::keep()
{
    if (! hasTake()) return false;
    TakeOptions o;
    o.channels = channels;
    o.trimSilence = trimBtn.getToggleState();
    o.normalise = normBtn.getToggleState();
    o.amigaRate = samplerModel == SamplerModel::Clean ? kAmigaRates[amigaIndex] : 0.0;
    o.bits = samplerModel == SamplerModel::Clean ? 0 : bits;
    const int idx = proc.editorState.instrument;
    o.name = ("SAMPLE " + hex2 (idx + 1)).toStdString();
    Instrument in;
    if (! takeToInstrument (takeL, takeR, takeRate, o, in)) { status ("NOTHING TO KEEP - THE TAKE IS SILENT"); return false; }
    proc.history.instrument (idx, "SAMPLE");
    proc.withLock ([&] { proc.player.jamAllOff(); std::swap (proc.song.instruments[(size_t) idx], in); });
    const auto& now = proc.song.instruments[(size_t) idx];
    takeL.clear(); takeR.clear();
    refreshButtons();
    status ("KEPT IN INSTRUMENT " + hex2 (idx + 1) + ": " + mmss ((double) now.data.size() / now.rate) + (now.dataR.empty() ? ", MONO" : ", STEREO")
            + ", " + juce::String (juce::roundToInt (now.rate)) + " HZ - EDIT TO SLICE OR LOOP IT");
    if (onChanged) onChanged();
    return true;
}

void SamplePage::again()
{
    takeL.clear(); takeR.clear();
    refreshButtons();
    record();
}

juce::String SamplePage::clockText() const
{
    const auto rs = proc.recorder.state();
    const juce::String limit = " / " + juce::String ((int) kMaxSeconds / 60) + ":00";
    if (rs == TakeRecorder::State::Recording) return "REC " + mmss (proc.recorder.seconds()) + limit;
    if (rs == TakeRecorder::State::Armed) return "ARMED 0:00.0" + limit;
    if (hasTake()) return "TAKE " + mmss (takeSeconds());
    return "0:00.0" + limit;
}

juce::String SamplePage::stateText() const
{
    const int src = proc.recSource.load();
    const int ins = proc.inputChannels.load();
    if (src == PaulaProcessor::SourceResample) return "RECORDING WHAT THE APP PLAYS";
    if (! proc.audioRunning.load()) return "NO AUDIO DEVICE RUNNING:";
    if (ins == 0) return "NO INPUT OPEN - CHOOSE ONE:";
    return "INPUT: " + juce::String (ins) + " CH, " + juce::String (juce::roundToInt (proc.audioRate())) + " HZ";
}

// ---------------------------------------------------------------- drawing
void SamplePage::paint (juce::Graphics& g)
{
    const auto& pal = Theme::palFor (*this);
    const float px = Theme::px;
    g.fillAll (pal.panel);
    const int ins = proc.editorState.instrument;
    const juce::String name (proc.song.instruments[(size_t) ins].name);
    PixelFont::drawLeft (g, "SAMPLE INTO INSTRUMENT " + hex2 (ins + 1) + (name.isNotEmpty() ? ": " + name : juce::String()), titleArea.toFloat(), px, pal.ink);
    PixelFont::drawLeft (g, "SOURCE", sourceLabel.toFloat(), px, pal.ink);
    PixelFont::drawLeft (g, "CHANNELS", channelsLabel.toFloat(), px, pal.ink);
    PixelFont::drawLeft (g, "TRIGGER", triggerLabel.toFloat(), px, pal.ink);
    PixelFont::drawLeft (g, samplerModel == SamplerModel::Clean ? "AMIGA RATE" : "RATE (HZ)", rateLabel.toFloat(), px, pal.ink);
    PixelFont::drawLeft (g, "SAMPLER", samplerLabel.toFloat(), px, pal.ink);
    drawMeter (g, meterArea.toFloat());
    // the clock, twice the size
    const auto rs = proc.recorder.state();
    PixelFont::drawLeft (g, clockText(), clockArea.toFloat(), px * 1.75f,
                         rs == TakeRecorder::State::Recording ? Theme::skin.hot : pal.ink);
    drawWave (g, waveArea.toFloat());
    PixelFont::drawLeft (g, stateText(), infoArea.toFloat(), px, pal.faint);
}

void SamplePage::drawMeter (juce::Graphics& g, juce::Rectangle<float> area)
{
    const auto& pal = Theme::palFor (*this);
    const float px = Theme::px;
    const float labelW = px * PixelFont::kCellW * 2;
    const float clipW = px * PixelFont::kCellW * 5;
    const float barH = (area.getHeight() - px * 2) / 2.0f;
    const auto c = Theme::skin.track[3];
    int i = 0;
    for (auto [lvl, hold, nm] : { std::tuple<float, float, const char*> { meterL, holdL, "L" }, { meterR, holdR, "R" } })
    {
        auto row = juce::Rectangle<float> (area.getX(), area.getY() + (float) i++ * (barH + px * 2), area.getWidth(), barH);
        PixelFont::drawLeft (g, nm, row.removeFromLeft (labelW), px, pal.ink);
        auto clip = row.removeFromRight (clipW);
        row.removeFromRight (px * 2);
        g.setColour (Theme::skin.bg);
        g.fillRect (row);
        const float f = toFrac (lvl);
        g.setColour (f > 0.985f ? Theme::skin.hot : c);
        g.fillRect (row.withWidth (row.getWidth() * f));
        g.setColour (pal.ink);
        g.fillRect (row.getX() + row.getWidth() * toFrac (hold) - 1.0f, row.getY(), 2.0f, row.getHeight());
        // THRESHOLD: its level as a line
        if (trigger == RecTrigger::Threshold)
        {
            g.setColour (Theme::skin.hot);
            const float tx = row.getX() + row.getWidth() * ((thresholdDb + 60.0f) / 60.0f);
            g.fillRect (tx - px * 0.5f, row.getY() - px, px, row.getHeight() + px * 2);
        }
        Theme::bevel (g, row, false, std::max (1.0f, px * 0.5f), pal);
        // the clip light
        g.setColour (clipCount > 0 ? Theme::skin.hot : Theme::skin.bg);
        g.fillRect (clip);
        PixelFont::drawCentred (g, "CLIP", clip, px, clipCount > 0 ? juce::Colours::white : pal.faint);
    }
}

void SamplePage::drawWave (juce::Graphics& g, juce::Rectangle<float> area)
{
    const auto& pal = Theme::palFor (*this);
    const float px = Theme::px;
    g.setColour (Theme::skin.bg);
    g.fillRect (area);
    Theme::bevel (g, area, false, std::max (1.0f, px * 0.5f), pal);
    auto a = area.reduced (px * 2);
    const bool showLive = proc.recorder.state() == TakeRecorder::State::Recording && ! live.empty();
    const std::vector<float>* L = showLive ? &live : &takeL;
    const std::vector<float>* R = showLive ? nullptr : (takeR.size() == takeL.size() && ! takeR.empty() ? &takeR : nullptr);
    if (L->empty())
    {
        const auto rs = proc.recorder.state();
        PixelFont::drawCentred (g, rs == TakeRecorder::State::Armed ? "ARMED - WAITING..." : "THE TAKE SHOWS HERE", a, px, pal.faint);
        return;
    }
    const int w = std::max (1, (int) a.getWidth());
    const size_t n = L->size();
    g.setColour (showLive ? Theme::skin.hot : Theme::skin.text);
    const float mid = a.getCentreY(), half = a.getHeight() * 0.5f;
    for (int x = 0; x < w; ++x)
    {
        const size_t s0 = n * (size_t) x / (size_t) w, s1 = std::max (s0 + 1, n * (size_t) (x + 1) / (size_t) w);
        float lo = 0, hi = 0;
        for (size_t s = s0; s < s1 && s < n; ++s)
        {
            const float v = R != nullptr ? 0.5f * ((*L)[s] + (*R)[s]) : (*L)[s];
            lo = std::min (lo, v); hi = std::max (hi, v);
        }
        g.fillRect (a.getX() + (float) x, mid - hi * half, 1.0f, std::max (1.0f, (hi - lo) * half));
    }
}

void SamplePage::resized()
{
    const float px = Theme::px;
    const int rowH = (int) (px * 11), gap = (int) std::max (2.0f, px * 1.5f);
    auto charsW = [px] (int c) { return (int) (px * 6 * c + px * 4); };
    auto r = getLocalBounds().reduced (gap * 2);
    auto place = [&] (juce::Rectangle<int>& row, PixButton& b) { b.setBounds (row.removeFromLeft (charsW (b.getButtonText().length()))); row.removeFromLeft (gap); };

    auto head = r.removeFromTop (rowH);
    closeBtn.setBounds (head.removeFromRight (charsW (5))); head.removeFromRight (gap);
    titleArea = head;
    r.removeFromTop (gap * 2);

    const int labelW = charsW (10);
    auto row1 = r.removeFromTop (rowH); r.removeFromTop (gap);
    sourceLabel = row1.removeFromLeft (labelW);
    place (row1, inputBtn); place (row1, resampleBtn); row1.removeFromLeft (gap * 3); place (row1, monitorBtn);
    auto rowS = r.removeFromTop (rowH); r.removeFromTop (gap);
    samplerLabel = rowS.removeFromLeft (labelW);
    place (rowS, cleanBtn); place (rowS, amigaBtn); place (rowS, s950Btn); rowS.removeFromLeft (gap * 3); place (rowS, bits8Btn); place (rowS, bits12Btn);
    auto row2 = r.removeFromTop (rowH); r.removeFromTop (gap);
    channelsLabel = row2.removeFromLeft (labelW);
    place (row2, stereoBtn); place (row2, monoBtn); place (row2, leftBtn); place (row2, rightBtn);
    auto row3 = r.removeFromTop (rowH); r.removeFromTop (gap);
    triggerLabel = row3.removeFromLeft (labelW);
    place (row3, manualBtn); place (row3, thresholdBtn); place (row3, playbackBtn);
    gainSlider.setBounds (r.removeFromTop (rowH)); r.removeFromTop (gap);
    driveSlider.setBounds (r.removeFromTop (rowH)); r.removeFromTop (gap);
    threshSlider.setBounds (r.removeFromTop (rowH)); r.removeFromTop (gap * 2);

    meterArea = r.removeFromTop ((int) (px * 24)); r.removeFromTop (gap * 2);
    auto row5 = r.removeFromTop ((int) (px * 18)); r.removeFromTop (gap * 2);
    {
        auto b = row5.withSizeKeepingCentre (row5.getWidth(), rowH);
        recordBtn.setBounds (b.removeFromLeft (charsW (6))); b.removeFromLeft (gap);
        stopBtn.setBounds (b.removeFromLeft (charsW (4))); b.removeFromLeft (gap * 3);
        clockArea = row5.withLeft (b.getX());
    }

    infoArea = r.removeFromBottom (rowH); r.removeFromBottom (gap);
    settingsBtn.setBounds (infoArea.removeFromRight (charsW (21))); infoArea.removeFromRight (gap);
    auto row7 = r.removeFromBottom (rowH); r.removeFromBottom (gap);
    rateLabel = row7.removeFromLeft (labelW);
    for (auto& b : rateBtns) place (row7, *b);
    auto row6 = r.removeFromBottom (rowH); r.removeFromBottom (gap * 2);
    place (row6, keepBtn); place (row6, againBtn); row6.removeFromLeft (gap * 3); place (row6, trimBtn); place (row6, normBtn);
    waveArea = r;
}

bool SamplePage::keyPressed (const juce::KeyPress& k)
{
    const int code = k.getKeyCode();
    if (k.getModifiers().isCommandDown()) return false;
    if (code == juce::KeyPress::escapeKey) { if (onClose) onClose(); return true; }
    if (code == juce::KeyPress::returnKey) { keep(); return true; }
    if (code == juce::KeyPress::backspaceKey || code == juce::KeyPress::deleteKey) { if (hasTake()) again(); return true; }
    const auto ch = juce::CharacterFunctions::toLowerCase (k.getTextCharacter());
    if (ch == 'r') { record(); return true; }
    if (ch == 'm') { setMonitor (! proc.monitorInput.load()); return true; }
    return false;
}
} // namespace prv::ui
