// EasyEffects' own standalone application (JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP).
//
// Compared with JUCE's default StandaloneFilterApp it adds:
//   - a notification-area (tray) icon with a menu: open, turn the Output /
//     Input effects on or off, quit;
//   - closing or minimising the window keeps EasyEffects running in the tray
//     (Settings > System tray), so the effects keep working;
//   - a single instance: opening EasyEffects again shows the running window;
//   - installed copies hand over to the "\EasyEffects\Launch" scheduled task
//     to run with administrator rights without a UAC prompt;
//   - "--tray" (used by the sign-in task) starts hidden in the tray.

#include <JuceHeader.h>

#if JucePlugin_Build_Standalone && JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP

#include <juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h>
#include "Updater.h"
#include "SingleInstance.h"
#include "../PluginEditor.h"
#include "../audio/WindowsAudioEndpoints.h"
#include "BinaryData.h"

namespace eeval {
namespace app {

static juce::Image renderAppIcon(int size)
{
    const auto src = juce::ImageCache::getFromMemory(BinaryData::icon_png, BinaryData::icon_pngSize);
    if (!src.isValid() || src.getWidth() == size)
        return src;
    return src.rescaled(size, size, juce::Graphics::highResamplingQuality);
}

class MainWindow;

// --- Tray icon -----------------------------------------------------------------

class TrayIcon : public juce::SystemTrayIconComponent {
public:
    explicit TrayIcon(MainWindow& w) : window(w)
    {
        const auto icon = renderAppIcon(32);
        setIconImage(icon, icon);
        setIconTooltip("EasyEffects");
    }

    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;

private:
    void showMenu();
    MainWindow& window;
};

// --- Main window -----------------------------------------------------------------

class MainWindow : public juce::StandaloneFilterWindow {
public:
    using StandaloneFilterWindow::StandaloneFilterWindow;

    juce::PropertySet* settings() { return pluginHolder != nullptr ? pluginHolder->settings.get() : nullptr; }

    bool closeToTray()    { auto* s = settings(); return s == nullptr || s->getBoolValue("tray.closeToTray", true); }
    bool minimiseToTray() { auto* s = settings(); return s == nullptr || s->getBoolValue("tray.minimiseToTray", true); }

    EasyEffectsAudioProcessorEditor* editor()
    {
        if (pluginHolder == nullptr || pluginHolder->processor == nullptr) return nullptr;
        return dynamic_cast<EasyEffectsAudioProcessorEditor*>(pluginHolder->processor->getActiveEditor());
    }

    void showFromTray()
    {
        setVisible(true);
        if (isMinimised()) setMinimised(false);
        toFront(true);
        juce::Process::makeForegroundProcess();
    }

    void hideToTray()
    {
        pluginHolder->savePluginState();
        setVisible(false);

        // Tell the user once where EasyEffects went.
        if (auto* s = settings(); s != nullptr && !s->getBoolValue("tray.hintShown", false) && tray != nullptr) {
            s->setValue("tray.hintShown", true);
            tray->showInfoBubble("EasyEffects is still running",
                                 "Your effects stay on. Open EasyEffects or quit it from this icon.");
        }
    }

    void closeButtonPressed() override
    {
        if (closeToTray() && tray != nullptr)
            hideToTray();
        else
            StandaloneFilterWindow::closeButtonPressed();   // saves and quits
    }

    void minimisationStateChanged(bool isNowMinimised) override
    {
        StandaloneFilterWindow::minimisationStateChanged(isNowMinimised);
        if (isNowMinimised && minimiseToTray() && tray != nullptr) {
            juce::Component::SafePointer<MainWindow> safe(this);
            juce::MessageManager::callAsync([safe] {
                if (safe == nullptr) return;
                safe->setMinimised(false);   // restore later at the normal size
                safe->hideToTray();
            });
        }
    }

    TrayIcon* tray = nullptr;
};

void TrayIcon::mouseDown(const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
        showMenu();
    else
        window.showFromTray();
}

void TrayIcon::mouseDoubleClick(const juce::MouseEvent&)
{
    window.showFromTray();
}

void TrayIcon::showMenu()
{
    auto* ed = window.editor();

    juce::PopupMenu m;
    m.addSectionHeader("EasyEffects " + currentVersion());
    m.addItem(1, "Open EasyEffects");
    m.addSeparator();
    if (ed != nullptr) {
        if (ed->hasOutputPipeline())
            m.addItem(2, "Output effects (system audio)", true, !ed->isPipelineBypassed(true));
        m.addItem(3, ed->hasOutputPipeline() ? "Input effects (microphone)" : "Effects", true,
                  !ed->isPipelineBypassed(false));
        m.addSeparator();
    }
    m.addItem(9, "Quit EasyEffects");

    // A tray menu needs the app in the foreground to close when clicking elsewhere.
    juce::Process::makeForegroundProcess();
    juce::Component::SafePointer<MainWindow> safe(&window);
    m.showMenuAsync(juce::PopupMenu::Options().withMousePosition(), [safe](int r) {
        if (safe == nullptr || r <= 0) return;
        auto* editor = safe->editor();
        switch (r) {
            case 1: safe->showFromTray(); break;
            case 2: if (editor) editor->setPipelineBypassed(true,  !editor->isPipelineBypassed(true));  break;
            case 3: if (editor) editor->setPipelineBypassed(false, !editor->isPipelineBypassed(false)); break;
            case 9: juce::JUCEApplicationBase::getInstance()->systemRequestedQuit(); break;
            default: break;
        }
    });
}

// --- Application ------------------------------------------------------------------

class EasyEffectsApp : public juce::JUCEApplication {
public:
    EasyEffectsApp()
    {
        juce::PropertiesFile::Options options;
        options.applicationName     = JucePlugin_Name;
        options.filenameSuffix      = ".settings";
        options.osxLibrarySubFolder = "Application Support";
        options.folderName          = "";
        appProperties.setStorageParameters(options);
    }

    const juce::String getApplicationName() override    { return JucePlugin_Name; }
    const juce::String getApplicationVersion() override { return JucePlugin_VersionString; }

    // One copy at a time is enforced by SingleInstance (works between
    // administrator and normal copies, unlike JUCE's own check).
    bool moreThanOneInstanceAllowed() override { return true; }
    void anotherInstanceStarted(const juce::String&) override {}

    void initialise(const juce::String& commandLine) override
    {
        const bool fromSignInTask = commandLine.contains("--tray");

        // Already running: bring that window forward, say so, and leave
        // (silently when started by the sign-in task).
        if (!acquireSingleInstance()) {
            if (!fromSignInTask)
                showAlreadyRunningMessage(requestShowRunningInstance());
            quit();
            return;
        }

        // Installed copy opened normally: start the elevated Launch task (no
        // UAC prompt) and leave; the task's copy takes the lock.
        if (isInstalledCopy() && !eeval::winaudio::isProcessElevated()) {
            releaseSingleInstance();
            if (startElevatedViaTask()) {
                quit();
                return;
            }
            acquireSingleInstance();   // no task: carry on without admin rights
        }

        // Another start of EasyEffects asks this copy to show its window.
        listenForShowRequests([] {
            juce::MessageManager::callAsync([] {
                if (auto* app = dynamic_cast<EasyEffectsApp*>(juce::JUCEApplicationBase::getInstance()))
                    if (app->mainWindow != nullptr)
                        app->mainWindow->showFromTray();
            });
        });

        if (juce::Desktop::getInstance().getDisplays().displays.isEmpty())
            return;

        mainWindow = std::make_unique<MainWindow>(getApplicationName(),
            juce::LookAndFeel::getDefaultLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId),
            std::make_unique<juce::StandalonePluginHolder>(appProperties.getUserSettings(), false,
                                                           juce::String{}, nullptr,
                                                           juce::Array<juce::StandalonePluginHolder::PluginInOuts>{},
                                                           false));

        tray = std::make_unique<TrayIcon>(*mainWindow);
        mainWindow->tray = tray.get();
        mainWindow->setIcon(renderAppIcon(128));

        // Start hidden in the tray for "--tray" (sign-in task) or when
        // Settings > System tray > Start minimized is on. Opening EasyEffects
        // again, or clicking the tray icon, shows the window.
        auto* settings = appProperties.getUserSettings();
        const bool startHidden = commandLine.contains("--tray")
                              || (settings != nullptr && settings->getBoolValue("tray.startMinimized", false));
        if (!startHidden)
            mainWindow->setVisible(true);
    }

    void shutdown() override
    {
        stopListeningForShowRequests();
        if (mainWindow != nullptr) mainWindow->tray = nullptr;
        tray = nullptr;
        mainWindow = nullptr;
        appProperties.saveIfNeeded();
    }

    void systemRequestedQuit() override
    {
        if (mainWindow != nullptr)
            mainWindow->pluginHolder->savePluginState();

        if (juce::ModalComponentManager::getInstance()->cancelAllModalComponents()) {
            juce::Timer::callAfterDelay(100, [] {
                if (auto* app = juce::JUCEApplicationBase::getInstance())
                    app->systemRequestedQuit();
            });
        } else {
            quit();
        }
    }

private:
    juce::ApplicationProperties appProperties;
    std::unique_ptr<MainWindow> mainWindow;
    std::unique_ptr<TrayIcon> tray;
};

} // namespace app
} // namespace eeval

juce::JUCEApplicationBase* juce_CreateApplication()
{
    return new eeval::app::EasyEffectsApp();
}

#endif
