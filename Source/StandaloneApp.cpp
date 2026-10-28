// Paula's Revenge - standalone app with a normal macOS window
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
    cmdCapture       = 0x2004
};

// the real audio system for FOLLOW MAC OUTPUT
class AppOutputSystem final : public prv::OutputSystem
{
public:
    AppOutputSystem (juce::StandalonePluginHolder& h) : holder (h) {}

    juce::String systemDefaultOutput() override { return prv::macDefaultOutputName(); }
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

        // FOLLOW MAC OUTPUT: headphones in, AirPods on... the sound goes there too
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
        window->setVisible (true);

       #if JUCE_MAC
        juce::PopupMenu appleExtras;
        appleExtras.addCommandItem (&commands, cmdAudioSettings);
        appleExtras.addCommandItem (&commands, cmdFollowOutput);
        juce::MenuBarModel::setMacMainMenu (this, &appleExtras);
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
    juce::StringArray getMenuBarNames() override { return { "View" }; }

    juce::PopupMenu getMenuForIndex (int, const juce::String&) override
    {
        juce::PopupMenu m;
        m.addCommandItem (&commands, cmdCapture);
        m.addSeparator();
        m.addCommandItem (&commands, cmdFullScreen);
        m.addCommandItem (&commands, cmdFollowOutput);
       #if ! JUCE_MAC
        m.addCommandItem (&commands, cmdAudioSettings);
        m.addSeparator();
        m.addCommandItem (&commands, juce::StandardApplicationCommandIDs::quit);
       #endif
        return m;
    }

    void menuItemSelected (int, int) override {}

    // ------------------------------------------------------------ commands
    void getAllCommands (juce::Array<juce::CommandID>& c) override
    {
        JUCEApplication::getAllCommands (c);   // includes Quit (Cmd+Q)
        c.addArray ({ cmdAudioSettings, cmdFullScreen, cmdFollowOutput, cmdCapture });
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
            info.addDefaultKeypress ('f', juce::ModifierKeys::commandModifier | juce::ModifierKeys::ctrlModifier);
        }
        else if (id == cmdCapture)
        {
            info.setInfo ("Capture What I Played", "Put the notes you just played into the pattern", "General", 0);
            info.addDefaultKeypress ('r', juce::ModifierKeys::commandModifier);
        }
        else if (id == cmdFollowOutput)
        {
            info.setInfo ("Follow Mac Output (Headphones)", "Play through whatever output the Mac switches to", "General", 0);
            info.setTicked (follower != nullptr && follower->enabled);
        }
        else JUCEApplication::getCommandInfo (id, info);
    }

    bool perform (const InvocationInfo& info) override
    {
        if (info.commandID == cmdAudioSettings) { if (holder != nullptr) holder->showAudioSettingsDialog(); return true; }
        if (info.commandID == cmdFullScreen)    { toggleFullScreen(); return true; }
        if (info.commandID == cmdCapture)       { if (auto* ed = editor()) ed->captureNow(); return true; }
        if (info.commandID == cmdFollowOutput && follower != nullptr)
        {
            const bool on = ! follower->enabled;
            appProperties.getUserSettings()->setValue ("followMacOutput", on);
            follower->setEnabled (on);
            if (auto* ed = editor()) ed->setStatus (on ? "FOLLOW MAC OUTPUT ON" : "FOLLOW MAC OUTPUT OFF - STAYING ON " + outputSystem->currentOutput());
            commands.commandStatusChanged();
            return true;
        }
        return JUCEApplication::perform (info);
    }

private:
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
    // macOS can switch its output at any time: look twice a second (a cheap CoreAudio query)
    void timerCallback() override { if (follower != nullptr) follower->check(); }

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
        if (dev->getOutputChannelNames().size() >= 2 && setup.outputChannels.countNumberOfSetBits() < 2)
        {
            setup.useDefaultOutputChannels = false;
            setup.outputChannels.clear();
            setup.outputChannels.setRange (0, 2, true);
            dm.setAudioDeviceSetup (setup, true);
            holder->saveAudioDeviceState();
        }
    }
};
} // namespace

juce::JUCEApplicationBase* juce_CreateApplication();
juce::JUCEApplicationBase* juce_CreateApplication() { return new PaulaApp(); }

#endif
