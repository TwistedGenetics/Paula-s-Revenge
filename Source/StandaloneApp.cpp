// Paula's Revenge - standalone app with a normal macOS window
//
// Replaces JUCE's default standalone wrapper so the app gets the standard
// title bar: red = quit, yellow = minimise, green = full screen. It also adds
// an app menu (Audio/MIDI Settings, Full Screen, Quit) and makes sure both
// stereo output channels are switched on.
#include <JuceHeader.h>

#if JucePlugin_Build_Standalone && JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP

#include <juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h>

#if JUCE_MAC
 void prvToggleNativeFullScreen (void* nsView);   // Source/mac/FullScreen.mm
#endif

namespace
{
enum CommandIDs
{
    cmdAudioSettings = 0x2001,
    cmdFullScreen    = 0x2002
};

class PaulaApp final : public juce::JUCEApplication,
                       public juce::MenuBarModel
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
    bool moreThanOneInstanceAllowed() override          { return false; }

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
        holder = std::make_unique<juce::StandalonePluginHolder> (appProperties.getUserSettings(), false, juce::String(), nullptr,
                                                                 juce::Array<juce::StandalonePluginHolder::PluginInOuts>(), false);
        ensureStereoOutputs();

        commands.registerAllCommandsForTarget (this);
        window = std::make_unique<MainWindow> (getApplicationName(), *holder->processor, appProperties.getUserSettings());
        window->addKeyListener (commands.getKeyMappings());
        window->setVisible (true);

       #if JUCE_MAC
        juce::PopupMenu appleExtras;
        appleExtras.addCommandItem (&commands, cmdAudioSettings);
        juce::MenuBarModel::setMacMainMenu (this, &appleExtras);
       #endif
        setApplicationCommandManagerToWatch (&commands);
    }

    void shutdown() override
    {
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

    void anotherInstanceStarted (const juce::String&) override
    {
        if (window != nullptr) window->toFront (true);
    }

    // --------------------------------------------------------------- menus
    juce::StringArray getMenuBarNames() override { return { "View" }; }

    juce::PopupMenu getMenuForIndex (int, const juce::String&) override
    {
        juce::PopupMenu m;
        m.addCommandItem (&commands, cmdFullScreen);
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
        c.addArray ({ cmdAudioSettings, cmdFullScreen });
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
        else JUCEApplication::getCommandInfo (id, info);
    }

    bool perform (const InvocationInfo& info) override
    {
        if (info.commandID == cmdAudioSettings) { if (holder != nullptr) holder->showAudioSettingsDialog(); return true; }
        if (info.commandID == cmdFullScreen)    { toggleFullScreen(); return true; }
        return JUCEApplication::perform (info);
    }

private:
    juce::ApplicationProperties appProperties;
    std::unique_ptr<juce::StandalonePluginHolder> holder;
    std::unique_ptr<MainWindow> window;
    juce::ApplicationCommandManager commands;

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
