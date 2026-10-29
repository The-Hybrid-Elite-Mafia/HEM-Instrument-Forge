// HEM Instrument Forge - application entry.

#include "UI/MainComponent.h"

namespace hf
{
void createAppMutex();

/** Forwards MIDI from every enabled input device to the engine. JUCE calls this from each device's
    own MIDI thread; the spin lock keeps the engine's single-producer MIDI queue single-producer.
    (MIDI threads only - the audio thread never takes this lock.) */
class MidiForwarder final : public juce::MidiInputCallback
{
public:
    explicit MidiForwarder (ForgeEngine& e) : engine (e) {}
    void handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage& m) override
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        engine.liveMidi (m);
    }
private:
    ForgeEngine& engine;
    juce::SpinLock lock;
};

class ForgeApp final : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override    { return "HEM Instrument Forge"; }
    const juce::String getApplicationVersion() override { return HEM_VERSION_STRING; }
    bool moreThanOneInstanceAllowed() override          { return false; }

    void initialise (const juce::String& commandLine) override
    {
        createAppMutex();
        juce::PropertiesFile::Options o;
        o.applicationName = "Instrument Forge";
        o.folderName = "Hybrid Elite Mafia";
        o.filenameSuffix = ".settings";
        o.osxLibrarySubFolder = "Application Support";
        settings = std::make_unique<juce::PropertiesFile> (o);

        auto saved = settings->getXmlValue ("audioDevice");
        const auto err = devices.initialise (0, 2, saved.get(), true);
        player.setProcessor (&engine);
        devices.addAudioCallback (&player);
        // every MIDI input the user turns on in Audio and MIDI Settings plays the selected track
        for (auto& d : juce::MidiInput::getAvailableDevices())
            if (saved == nullptr && ! devices.isMidiInputDeviceEnabled (d.identifier))
                devices.setMidiInputDeviceEnabled (d.identifier, true);
        devices.addMidiInputDeviceCallback ({}, &midi);
        devices.addChangeListener (&deviceSaver);

        window = std::make_unique<MainWindow> (engine, devices, *settings);
        if (err.isNotEmpty())
            juce::AlertWindow::showAsync (juce::MessageBoxOptions().withTitle ("Sound card").withMessage ("The audio device could not start: " + err
                                              + "\n\nOpen File > Audio and MIDI Settings to pick one.").withButton ("OK"), nullptr);
        openFromCommandLine (commandLine);
    }

    void shutdown() override
    {
        window.reset();
        devices.removeChangeListener (&deviceSaver);
        devices.removeMidiInputDeviceCallback ({}, &midi);
        devices.removeAudioCallback (&player);
        player.setProcessor (nullptr);
        devices.closeAudioDevice();
        if (settings != nullptr) settings->saveIfNeeded();
    }

    void systemRequestedQuit() override
    {
        if (window == nullptr) { quit(); return; }
        window->content->checkSaveThen ([] { juce::JUCEApplication::getInstance()->quit(); });
    }

    void anotherInstanceStarted (const juce::String& commandLine) override
    {
        if (window != nullptr) window->toFront (true);
        openFromCommandLine (commandLine);
    }

private:
    void openFromCommandLine (const juce::String& commandLine)
    {
        const auto arg = commandLine.trim().unquoted();
        if (arg.isEmpty() || window == nullptr) return;
        const juce::File f (arg);
        if (f.existsAsFile() && f.hasFileExtension (session::kExtension))
            window->content->checkSaveThen ([this, f] { if (window != nullptr) window->content->openSession (f); });
    }

    struct DeviceSaver final : juce::ChangeListener
    {
        DeviceSaver (juce::AudioDeviceManager& d, std::unique_ptr<juce::PropertiesFile>& s) : devices (d), settings (s) {}
        void changeListenerCallback (juce::ChangeBroadcaster*) override
        {
            if (settings == nullptr) return;
            if (auto xml = devices.createStateXml()) settings->setValue ("audioDevice", xml.get());
            settings->saveIfNeeded();
        }
        juce::AudioDeviceManager& devices;
        std::unique_ptr<juce::PropertiesFile>& settings;
    };

    struct MainWindow final : juce::DocumentWindow
    {
        MainWindow (ForgeEngine& e, juce::AudioDeviceManager& d, juce::PropertiesFile& s)
            : DocumentWindow ("HEM Instrument Forge", ui::colours::navy, DocumentWindow::allButtons)
        {
            setUsingNativeTitleBar (true);
            content = new ui::MainComponent (e, &d, s);
            setContentOwned (content, true);
            setResizable (true, false);
            constrainer.setFixedAspectRatio (16.0 / 9.0);
            constrainer.setMinimumSize (1120, 630);
            setConstrainer (&constrainer);
            if (auto* disp = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay())
            {
                const auto area = disp->userArea;
                int w = juce::jmin (1600, area.getWidth() * 92 / 100);
                int h = w * 9 / 16;
                if (h > area.getHeight() * 92 / 100) { h = area.getHeight() * 92 / 100; w = h * 16 / 9; }
                centreWithSize (w, h);
            }
            else
                centreWithSize (1600, 900);
            setVisible (true);
        }
        void closeButtonPressed() override { juce::JUCEApplication::getInstance()->systemRequestedQuit(); }
        ui::MainComponent* content = nullptr;
        juce::ComponentBoundsConstrainer constrainer;
    };

    std::unique_ptr<juce::PropertiesFile> settings;
    ForgeEngine engine;
    juce::AudioDeviceManager devices;
    juce::AudioProcessorPlayer player;
    MidiForwarder midi { engine };
    DeviceSaver deviceSaver { devices, settings };
    std::unique_ptr<MainWindow> window;
};
} // namespace hf

START_JUCE_APPLICATION (hf::ForgeApp)
