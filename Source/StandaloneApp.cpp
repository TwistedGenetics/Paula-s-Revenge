// Paula's Revenge - standalone app with a normal window (macOS and Windows)
//
// Replaces JUCE's default standalone wrapper so the app gets the standard
// title bar: red = quit, yellow = minimise, green = full screen. It also adds
// an app menu (Audio/MIDI Settings, Full Screen, Quit) and makes sure both
// stereo output channels are switched on.
#include <JuceHeader.h>

#if JucePlugin_Build_Standalone && JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP

#include <juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h>
#include "PluginHost.h"
#include "PluginEditor.h"
#include "OutputFollower.h"

#if JUCE_MAC
 void prvToggleNativeFullScreen (void* nsView);   // Source/mac/FullScreen.mm
#endif

namespace
{
enum CommandIDs
{
    cmdAudioSettings = 0x2001,
    cmdFullScreen    = 0x2002,
    cmdFollowOutput  = 0x2003,
    cmdCapture       = 0x2004,
    cmdSample        = 0x2005,
    cmdNew           = 0x2010,
    cmdLoad          = 0x2011,
    cmdSave          = 0x2012,
    cmdSaveAs        = 0x2013,
    cmdSaveMod       = 0x2014,
    cmdRenderWav     = 0x2015,
    cmdExportSamples = 0x2016,
    cmdNoteOffs      = 0x2020,
    cmdFullVelocity  = 0x2021
};

// the real audio system for FOLLOW SYSTEM OUTPUT
class AppOutputSystem final : public prv::OutputSystem
{
public:
    AppOutputSystem (juce::StandalonePluginHolder& h) : holder (h) {}

    juce::String systemDefaultOutput() override { return prv::systemDefaultOutputName(); }
    juce::String currentOutput() override
    {
        auto* dev = holder.deviceManager.getCurrentAudioDevice();
        return dev != nullptr ? holder.deviceManager.getAudioDeviceSetup().outputDeviceName : juce::String();
    }
    juce::StringArray outputs() override
    {
        auto* type = holder.deviceManager.getCurrentDeviceTypeObject();
        return type != nullptr ? type->getDeviceNames (false) : juce::StringArray();
    }
    bool switchTo (const juce::String& name) override
    {
        auto& dm = holder.deviceManager;
        auto* proc = dynamic_cast<PaulaProcessor*> (holder.processor.get());
        const bool running = dm.getCurrentAudioDevice() != nullptr && dm.getCurrentAudioDevice()->isPlaying();
        // fade out (10 ms), switch, fade back in: no clicks
        if (proc != nullptr && running) { proc->outputGainTarget = 0.0f; juce::Thread::sleep (25); }
        auto setup = dm.getAudioDeviceSetup();
        setup.outputDeviceName = name;
        setup.useDefaultOutputChannels = false;
        setup.outputChannels.clear();
        setup.outputChannels.setRange (0, 2, true);   // stereo 1+2 (a mono device just gets its one)
        auto err = dm.setAudioDeviceSetup (setup, true);
        if (err.isNotEmpty())
        {
            // its own sample rate / buffer size, if ours aren't possible
            setup.sampleRate = 0; setup.bufferSize = 0;
            err = dm.setAudioDeviceSetup (setup, true);
        }
        if (proc != nullptr) proc->outputGainTarget = 1.0f;
        if (err.isNotEmpty() || dm.getCurrentAudioDevice() == nullptr) return false;
        holder.saveAudioDeviceState();
        return true;
    }

private:
    juce::StandalonePluginHolder& holder;
};

class PaulaApp final : public juce::JUCEApplication,
                       public juce::MenuBarModel,
                       private juce::ChangeListener,
                       private juce::Timer
{
public:
    PaulaApp()
    {
        juce::PropertiesFile::Options o;
        o.applicationName     = juce::CharPointer_UTF8 (JucePlugin_Name);
        o.filenameSuffix      = ".settings";
        o.osxLibrarySubFolder = "Application Support";
       #if JUCE_LINUX || JUCE_BSD
        o.folderName          = "~/.config";
       #endif
        appProperties.setStorageParameters (o);   // same file as before, so audio settings carry over
    }

    const juce::String getApplicationName() override    { return juce::CharPointer_UTF8 (JucePlugin_Name); }
    const juce::String getApplicationVersion() override { return JucePlugin_VersionString; }
    // (a plug-in scan runs in a second copy of the app)
    bool moreThanOneInstanceAllowed() override          { return prv::PluginHost::isScanChild (getCommandLineParameterArray()); }

    // ---------------------------------------------------------------------
    class MainWindow final : public juce::DocumentWindow
    {
    public:
        MainWindow (const juce::String& name, juce::AudioProcessor& processor, juce::PropertySet* settings)
            : DocumentWindow (name, juce::Colours::black, DocumentWindow::allButtons), props (settings)
        {
            setUsingNativeTitleBar (true);
            setContentOwned (processor.createEditorIfNeeded(), true);
            setResizable (true, false);
            if (auto* ed = getContentComponent())
                if (auto* ce = dynamic_cast<juce::AudioProcessorEditor*> (ed))
                    if (auto* c = ce->getConstrainer())
                        setResizeLimits (c->getMinimumWidth(), c->getMinimumHeight(), 8000, 5000);

            const auto saved = props != nullptr ? props->getValue ("windowBounds") : juce::String();
            if (saved.isNotEmpty()) restoreWindowStateFromString (saved);
            else centreWithSize (getWidth(), getHeight());
        }

        ~MainWindow() override
        {
            if (props != nullptr) props->setValue ("windowBounds", getWindowStateAsString());
            clearContentComponent();   // editor goes before the processor
        }

        void closeButtonPressed() override
        {
            // the red button quits the whole app
            juce::JUCEApplication::getInstance()->systemRequestedQuit();
        }

    private:
        juce::PropertySet* props;
    };

    // ---------------------------------------------------------------------
    void initialise (const juce::String&) override
    {
        if (prv::PluginHost::isScanChild (getCommandLineParameterArray()))
        {
            // looking at one plug-in for the main app, then quit (no window, no audio)
           #if JUCE_MAC
            juce::Process::setDockIconVisible (false);
           #endif
            juce::MessageManager::callAsync ([] { prv::PluginHost::runScanChild (juce::JUCEApplicationBase::getCommandLineParameterArray()); juce::JUCEApplicationBase::quit(); });
            return;
        }
        holder = std::make_unique<juce::StandalonePluginHolder> (appProperties.getUserSettings(), false, juce::String(), nullptr,
                                                                 juce::Array<juce::StandalonePluginHolder::PluginInOuts>(), false);
        ensureStereoOutputs();
        // SAMPLE records the input: JUCE's standalone mutes it by default (feedback); the SAMPLE
        // page has its own MONITOR switch, off to start with
        holder->getMuteInputValue().setValue (false);

        // FOLLOW SYSTEM OUTPUT: headphones in, AirPods on... the sound goes there too
        outputSystem = std::make_unique<AppOutputSystem> (*holder);
        follower = std::make_unique<prv::OutputFollower> (*outputSystem);
        follower->enabled = appProperties.getUserSettings()->getBoolValue ("followMacOutput", true);
        follower->onStatus = [this] (const juce::String& m) { if (auto* ed = editor()) ed->setStatus (m); };
        follower->onEnabledChanged = [this] (bool on)
        {
            appProperties.getUserSettings()->setValue ("followMacOutput", on);
            commands.commandStatusChanged();
        };
        follower->check();
        holder->deviceManager.addChangeListener (this);
        startTimer (500);

        commands.registerAllCommandsForTarget (this);
        window = std::make_unique<MainWindow> (getApplicationName(), *holder->processor, appProperties.getUserSettings());
        window->addKeyListener (commands.getKeyMappings());
        if (auto* ed = editor()) ed->openAudioSettings = [this] { if (holder != nullptr) holder->showAudioSettingsDialog(); };
        window->setVisible (true);

       #if JUCE_MAC
        juce::PopupMenu appleExtras;
        appleExtras.addCommandItem (&commands, cmdAudioSettings);
        appleExtras.addCommandItem (&commands, cmdFollowOutput);
        juce::MenuBarModel::setMacMainMenu (this, &appleExtras);
       #else
        // a menu bar in the window, in big type like the rest of the app
        window->setLookAndFeel (&menuLook);
        window->setMenuBar (this, 30);
       #endif
        setApplicationCommandManagerToWatch (&commands);
    }

    void shutdown() override
    {
        stopTimer();
        if (holder != nullptr) holder->deviceManager.removeChangeListener (this);
        follower = nullptr;
        outputSystem = nullptr;
       #if JUCE_MAC
        juce::MenuBarModel::setMacMainMenu (nullptr);
       #else
        if (window != nullptr) { window->setMenuBar (nullptr); window->setLookAndFeel (nullptr); }
       #endif
        if (holder != nullptr) holder->savePluginState();
        window = nullptr;
        holder = nullptr;
        appProperties.saveIfNeeded();
    }

    void systemRequestedQuit() override
    {
        if (holder != nullptr) holder->savePluginState();
        if (juce::ModalComponentManager::getInstance()->cancelAllModalComponents())
        {
            juce::Timer::callAfterDelay (100, [] { if (auto* app = JUCEApplicationBase::getInstance()) app->systemRequestedQuit(); });
            return;
        }
        quit();
    }

    void anotherInstanceStarted (const juce::String& cmd) override
    {
        if (cmd.contains ("--prv-scan")) return;
        if (window != nullptr) window->toFront (true);
    }

    // --------------------------------------------------------------- menus
    // File + View: on the Mac in the menu bar at the top of the screen (the app menu keeps
    // Audio/MIDI Settings, Follow, Quit too), on a PC in the window
    juce::StringArray getMenuBarNames() override { return { "File", "View" }; }

    juce::PopupMenu getMenuForIndex (int, const juce::String& name) override
    {
        juce::PopupMenu m;
        if (name == "File")
        {
            m.addCommandItem (&commands, cmdNew);
            m.addCommandItem (&commands, cmdLoad);
            m.addCommandItem (&commands, cmdSave);
            m.addCommandItem (&commands, cmdSaveAs);
            m.addSeparator();
            m.addCommandItem (&commands, cmdSaveMod);
            m.addCommandItem (&commands, cmdRenderWav);
            m.addCommandItem (&commands, cmdExportSamples);
            m.addSeparator();
            m.addCommandItem (&commands, cmdAudioSettings);
           #if ! JUCE_MAC
            m.addCommandItem (&commands, cmdFollowOutput);
            m.addSeparator();
            m.addCommandItem (&commands, juce::StandardApplicationCommandIDs::quit);
           #endif
            return m;
        }
        m.addCommandItem (&commands, cmdCapture);
        m.addCommandItem (&commands, cmdSample);
        m.addSeparator();
        m.addCommandItem (&commands, cmdNoteOffs);
        m.addCommandItem (&commands, cmdFullVelocity);
        m.addSeparator();
        m.addCommandItem (&commands, cmdFullScreen);
       #if JUCE_MAC
        m.addCommandItem (&commands, cmdFollowOutput);
       #endif
        return m;
    }

    void menuItemSelected (int, int) override {}

    // ------------------------------------------------------------ commands
    void getAllCommands (juce::Array<juce::CommandID>& c) override
    {
        JUCEApplication::getAllCommands (c);   // includes Quit (Cmd+Q)
        c.addArray ({ cmdAudioSettings, cmdFullScreen, cmdFollowOutput, cmdCapture, cmdSample,
                      cmdNew, cmdLoad, cmdSave, cmdSaveAs, cmdSaveMod, cmdRenderWav, cmdExportSamples, cmdNoteOffs, cmdFullVelocity });
    }

    void getCommandInfo (juce::CommandID id, juce::ApplicationCommandInfo& info) override
    {
        if (id == cmdAudioSettings)
        {
            info.setInfo ("Audio/MIDI Settings...", "Choose the audio and MIDI devices", "General", 0);
            info.addDefaultKeypress (',', juce::ModifierKeys::commandModifier);
        }
        else if (id == cmdFullScreen)
        {
            info.setInfo ("Toggle Full Screen", "Switch the window in and out of full screen", "View", 0);
           #if JUCE_MAC
            info.addDefaultKeypress ('f', juce::ModifierKeys::commandModifier | juce::ModifierKeys::ctrlModifier);
           #else
            info.addDefaultKeypress (juce::KeyPress::F11Key, juce::ModifierKeys());   // (Ctrl+F is FOLLOW)
           #endif
        }
        else if (id == cmdCapture)
        {
            info.setInfo ("Capture What I Played", "Put the notes you just played into the pattern", "General", 0);
            info.addDefaultKeypress ('r', juce::ModifierKeys::commandModifier);
        }
        else if (id == cmdSample)
        {
            info.setInfo ("Sample Audio...", "Record audio (the input, or the app's own output) into the instrument", "General", 0);
            info.addDefaultKeypress ('4', juce::ModifierKeys::commandModifier);
        }
        else if (id == cmdNew)    info.setInfo ("New", "Start a new song", "File", 0);
        else if (id == cmdLoad)
        {
            info.setInfo ("Load...", "Load a song or a MOD", "File", 0);
            info.addDefaultKeypress ('o', juce::ModifierKeys::commandModifier);
        }
        else if (id == cmdSave)
        {
            info.setInfo ("Save", "Save the song", "File", 0);
            info.addDefaultKeypress ('s', juce::ModifierKeys::commandModifier);
        }
        else if (id == cmdSaveAs)
        {
            info.setInfo ("Save As...", "Save the song under a new name", "File", 0);
            info.addDefaultKeypress ('s', juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier);
        }
        else if (id == cmdSaveMod)       info.setInfo ("Save MOD...", "Save the song as a ProTracker MOD", "File", 0);
        else if (id == cmdRenderWav)     info.setInfo ("Render WAV...", "Render the song (or each track) to WAV", "File", 0);
        else if (id == cmdExportSamples) info.setInfo ("Export Samples...", "Write every sample instrument as a WAV", "File", 0);
        else if (id == cmdNoteOffs)
        {
            info.setInfo ("Record Note-Offs", "Letting go of a key writes OFF (plug-in / MIDI-out instruments)", "View", 0);
            info.setTicked (prv::ui::PatternEditor::recordNoteOffs());
        }
        else if (id == cmdFullVelocity)
        {
            info.setInfo ("Full Velocity", "Ignore the keyboard's velocity: every note at 127", "View", 0);
            auto* proc = holder != nullptr ? dynamic_cast<PaulaProcessor*> (holder->processor.get()) : nullptr;
            info.setTicked (proc != nullptr && proc->fullVelocity.load());
        }
        else if (id == cmdFollowOutput)
        {
            info.setInfo ("Follow System Output (Headphones)", "Play through whatever output the computer switches to", "General", 0);
            info.setTicked (follower != nullptr && follower->enabled);
        }
        else JUCEApplication::getCommandInfo (id, info);
    }

    bool perform (const InvocationInfo& info) override
    {
        if (info.commandID == cmdAudioSettings) { if (holder != nullptr) holder->showAudioSettingsDialog(); return true; }
        if (info.commandID == cmdFullScreen)    { toggleFullScreen(); return true; }
        if (info.commandID == cmdCapture)       { if (auto* ed = editor()) ed->captureNow(); return true; }
        if (info.commandID == cmdSample)        { if (auto* ed = editor()) ed->toggleSample(); return true; }
        if (auto* ed = editor())
        {
            switch (info.commandID)
            {
                case cmdNew:           ed->newSong(); return true;
                case cmdLoad:          ed->loadSong(); return true;
                case cmdSave:          ed->saveSong(); return true;
                case cmdSaveAs:        ed->saveSongAs(); return true;
                case cmdSaveMod:       ed->exportMod(); return true;
                case cmdRenderWav:     ed->exportWav(); return true;
                case cmdExportSamples: ed->exportSamples(); return true;
                case cmdNoteOffs:      ed->setNoteOffs (! prv::ui::PatternEditor::recordNoteOffs()); commands.commandStatusChanged(); return true;
                case cmdFullVelocity:  ed->setFullVelocity (! ed->fullVelocityOn()); commands.commandStatusChanged(); return true;
                default: break;
            }
        }
        if (info.commandID == cmdFollowOutput && follower != nullptr)
        {
            const bool on = ! follower->enabled;
            appProperties.getUserSettings()->setValue ("followMacOutput", on);
            follower->setEnabled (on);
            if (auto* ed = editor()) ed->setStatus (on ? "FOLLOW SYSTEM OUTPUT ON" : "FOLLOW SYSTEM OUTPUT OFF - STAYING ON " + outputSystem->currentOutput().toUpperCase());
            commands.commandStatusChanged();
            return true;
        }
        return JUCEApplication::perform (info);
    }

private:
    // the menu bar and its menus in a large, clear font (a PC: the menus are in the window)
    struct MenuLook final : public juce::LookAndFeel_V4
    {
        MenuLook()
        {
            setColour (juce::PopupMenu::backgroundColourId, juce::Colours::white);
            setColour (juce::PopupMenu::textColourId, juce::Colours::black);
            setColour (juce::PopupMenu::highlightedBackgroundColourId, juce::Colour (0xff1a1a1a));
            setColour (juce::PopupMenu::highlightedTextColourId, juce::Colours::white);
        }
        juce::Font getMenuBarFont (juce::MenuBarComponent&, int, const juce::String&) override { return juce::Font (juce::FontOptions (20.0f, juce::Font::bold)); }
        juce::Font getPopupMenuFont() override { return juce::Font (juce::FontOptions (20.0f)); }
        void drawMenuBarBackground (juce::Graphics& g, int w, int h, bool, juce::MenuBarComponent&) override
        {
            g.fillAll (juce::Colours::white);
            g.setColour (juce::Colours::black);
            g.fillRect (0, h - 1, w, 1);
        }
        void drawMenuBarItem (juce::Graphics& g, int w, int h, int, const juce::String& text, bool over, bool open, bool, juce::MenuBarComponent& bar) override
        {
            if (over || open) { g.fillAll (juce::Colour (0xff1a1a1a)); g.setColour (juce::Colours::white); }
            else g.setColour (juce::Colours::black);
            g.setFont (getMenuBarFont (bar, 0, text));
            g.drawFittedText (text, 0, 0, w, h, juce::Justification::centred, 1);
        }
    };
    MenuLook menuLook;
    juce::ApplicationProperties appProperties;
    std::unique_ptr<juce::StandalonePluginHolder> holder;
    std::unique_ptr<MainWindow> window;
    juce::ApplicationCommandManager commands;
    std::unique_ptr<AppOutputSystem> outputSystem;
    std::unique_ptr<prv::OutputFollower> follower;

    PaulaEditor* editor() const
    {
        return window != nullptr ? dynamic_cast<PaulaEditor*> (window->getContentComponent()) : nullptr;
    }
    // the device changed (the settings window, a device going away...)
    void changeListenerCallback (juce::ChangeBroadcaster*) override
    {
        if (follower == nullptr) return;
        follower->deviceChanged();
        juce::MessageManager::callAsync ([this] { if (follower != nullptr) follower->check(); });
    }
    // the system can switch its output at any time: look twice a second (a cheap query)
    void timerCallback() override
    {
        if (follower != nullptr) follower->check();
        // the window's title: the app and the song (its file's name), * when not saved
        if (window != nullptr && holder != nullptr)
            if (auto* proc = dynamic_cast<PaulaProcessor*> (holder->processor.get()))
            {
                const juce::String t = getApplicationName() + " - " + juce::String (proc->song.name) + (proc->history.isDirty() ? " *" : "");
                if (window->getName() != t) window->setName (t);
            }
    }

    void toggleFullScreen()
    {
        if (window == nullptr) return;
       #if JUCE_MAC
        if (auto* peer = window->getPeer()) { prvToggleNativeFullScreen (peer->getNativeHandle()); return; }
       #endif
        window->setFullScreen (! window->isFullScreen());
    }

    // An old setup could have only one output channel switched on, which puts
    // everything in one ear. Make sure the first two outputs are active unless
    // the user has picked a different pair.
    void ensureStereoOutputs()
    {
        auto& dm = holder->deviceManager;
        auto* dev = dm.getCurrentAudioDevice();
        if (dev == nullptr) return;
        auto setup = dm.getAudioDeviceSetup();
        bool change = false;
        const int outs = dev->getOutputChannelNames().size(), ins = dev->getInputChannelNames().size();
        if (outs >= 2 && setup.outputChannels.countNumberOfSetBits() < 2)
        {
            setup.useDefaultOutputChannels = false;
            setup.outputChannels.clear();
            setup.outputChannels.setRange (0, 2, true);
            change = true;
        }
        // CV out (an ES-9): with more than 2 outputs, all of them on (up to 16), in order, so
        // "output 5" in the app is the interface's output 5; the same for its inputs (AUTO-TUNE)
        if (outs > 2)
        {
            const int want = std::min (outs, prv::kCvOutputs);
            if (setup.outputChannels.countNumberOfSetBits() != want || setup.outputChannels.findNextSetBit (0) != 0)
            {
                setup.useDefaultOutputChannels = false;
                setup.outputChannels.clear();
                setup.outputChannels.setRange (0, want, true);
                change = true;
            }
        }
        if (ins > 2)
        {
            const int want = std::min (ins, prv::kCvOutputs);
            if (setup.inputChannels.countNumberOfSetBits() != want || setup.inputChannels.findNextSetBit (0) != 0)
            {
                setup.useDefaultInputChannels = false;
                setup.inputChannels.clear();
                setup.inputChannels.setRange (0, want, true);
                change = true;
            }
        }
        if (change)
        {
            dm.setAudioDeviceSetup (setup, true);
            holder->saveAudioDeviceState();
        }
    }
};
} // namespace

juce::JUCEApplicationBase* juce_CreateApplication();
juce::JUCEApplicationBase* juce_CreateApplication() { return new PaulaApp(); }

#endif
