#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "ui/Theme.h"
#include "ui/GenericModuleEditor.h"
#include "ui/VisualEqualizerEditor.h"
#include "ui/SpectrumAnalyzerEditor.h"
#include "ui/SidebarRowCustomComponent.h"
#include "ui/EffectPage.h"
#include "ui/NavigationPane.h"
#include "ui/SettingsPage.h"
#include "dsp/EffectRegistry.h"
#include "audio/SecondaryPipeline.h"
#include "audio/AsioHost.h"
#include "app/Updater.h"

/**
 * Main window: EasyEffects' structure, reimagined as a Windows 11 app.
 *
 *  ┌──────────────┬──────────────────────────────────────────────────────────┐
 *  │ = EasyEffects│ Output                    [Preset v] [Bypass] [Monitor]  │
 *  │| Output      │ System audio · 48.0 kHz · Windows Audio                  │
 *  │  Input       │ ┌ spectrum ──────────────────────────────────────────┐  │
 *  │              │ └────────────────────────────────────────────────────┘  │
 *  │              │ ┌ Effects ────[+ Add]┐  Input [0.00 dB] [-17]  Output …  │
 *  │              │ │ Players  device  v │  ┌ Controls ──────┐ ┌ Dynamics ┐  │
 *  │              │ │|Equalizer  [on] .= │  │                │ │          │  │
 *  │              │ │ Limiter    [on] .= │  └────────────────┘ └──────────┘  │
 *  │  Devices     │ │ Output   device  v │                                   │
 *  │  Settings    │ └────────────────────┘                                   │
 *  └──────────────┴──────────────────────────────────────────────────────────┘
 *
 * Output / Input are EasyEffects' two pipelines. Devices replaces the Linux
 * app's PipeWire page (virtual audio, devices, audio engine) and Settings holds
 * appearance and about.
 */
class EasyEffectsAudioProcessorEditor : public juce::AudioProcessorEditor,
                                        private juce::ListBoxModel,
                                        private juce::Timer,
                                        private juce::DarkModeSettingListener
{
public:
    EasyEffectsAudioProcessorEditor(EasyEffectsAudioProcessor&);
    ~EasyEffectsAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;
    void timerCallback() override;

    // ListBoxModel
    int getNumRows() override;
    void paintListBoxItem(int rowNumber, juce::Graphics& g, int width, int height, bool rowIsSelected) override;
    juce::Component* refreshComponentForRow(int rowNumber, bool isRowSelected, juce::Component* existingComponentToUpdate) override;
    void listBoxItemClicked(int row, const juce::MouseEvent&) override;
    void selectedRowsChanged(int lastRowSelected) override;

    // Called by sidebar rows or externally to refresh
    void refreshSidebar();

    // Used by the tray menu (src/app/StandaloneApp.cpp).
    bool hasOutputPipeline() const { return outputPipeline != nullptr; }
    bool isPipelineBypassed(bool output) const;
    void setPipelineBypassed(bool output, bool bypassed);

private:
    enum class Pipeline { Output, Input };
    enum class Page { Output, Input, Devices, Settings };

    void rebuildEditorView();
    void layoutCurrentEditor();
    void showAddEffectMenu();
    void showPresetMenu();
    void chooseMonitorDevice(juce::Component* anchor);
    void toggleMonitor();
    void moveEffectBy(int slotIndex, int delta);
    void resetEffect(int slotIndex);   // defaults + rebuild the open page

    // --- Pages / navigation -------------------------------------------------
    void buildNavigation();
    void showPage(Page p);
    bool isPipelinePage() const { return currentPage == Page::Output || currentPage == Page::Input; }
    void buildDevicesPage();
    void buildSettingsPage();
    void layoutSettingsViewport();
    juce::String pageSubtitle() const;
    juce::String engineStatus() const;

    // --- Appearance (Settings > App theme) ----------------------------------
    juce::String themeSetting() const;
    void setThemeSetting(const juce::String& s);
    void applyAppTheme();
    void darkModeSettingChanged() override;
    juce::PropertySet* settings() const;

    juce::String currentInputDeviceName() const  { return inputNameFor(activePipeline); }
    juce::String currentOutputDeviceName() const { return outputNameFor(activePipeline); }
    juce::String inputNameFor(Pipeline p) const;
    juce::String outputNameFor(Pipeline p) const;

    // --- Output / Input pipelines (EasyEffects' two tabs) -----------------
    // Input  = the standalone holder's processor + devices (the original pipeline)
    // Output = eeval::SecondaryPipeline (standalone only)
    void initPipelines();
    void switchPipeline(Pipeline p);
    bool hasPipelineTabs() const { return outputPipeline != nullptr; }
    juce::AudioDeviceManager* activeDeviceManager() const { return deviceManagerFor(activePipeline); }
    juce::AudioDeviceManager* deviceManagerFor(Pipeline p) const;
    void showDeviceMenu(Pipeline p, bool inputSide, juce::Rectangle<int> screenArea);
    void showAudioSettings(Pipeline p);
    void showEngineMenu(juce::Component& anchor);
    void mouseDown(const juce::MouseEvent&) override;
    void mouseMove(const juce::MouseEvent&) override;
    void mouseExit(const juce::MouseEvent&) override;

    // The processor whose chain the UI currently shows / edits.
    EasyEffectsAudioProcessor& proc() const { return *activeProc; }

    // --- ASIO engine (e.g. Synchronous Audio Router) -----------------------
    // When enabled, one ASIO driver runs both pipelines (AsioHost) and the
    // per-pipeline Windows Audio devices are closed.
    bool asioEnabled() const { return asioHost != nullptr && asioDriver.isNotEmpty(); }
    void enableAsio(const juce::String& driver);
    void disableAsio();
    void restartElevated();   // save, release the driver, relaunch as admin, quit

    // Synchronous Audio Router automation: create EasyEffects' endpoints, wire
    // both tabs to them and make them the Windows defaults (restored on exit).
    void autoRouteSar(bool force);
    void trySwitchWindowsDefaultsToSar();   // retried from the timer until SAR's devices exist
    void restoreWindowsDefaultsFromSar(bool render = true, bool capture = true);
    int sarDefaultsCountdown = 0;
    static bool driverNeedsElevation(const juce::String& driver) {
        return driver.containsIgnoreCase("Synchronous Audio Router");
    }
    void showPairMenu(Pipeline p, bool inputSide, juce::Rectangle<int> screenArea);
    static int pipelineIndexFor(Pipeline p) {
        return p == Pipeline::Output ? eeval::AsioHost::outputPipeline
                                     : eeval::AsioHost::inputPipeline;
    }
    int activePipelineIndex() const { return pipelineIndexFor(activePipeline); }

    // Built-in virtual audio routing (standalone only)
    bool sarInstalled() const;
    bool sarRunning() const;
    bool virtualOptionEnabled(bool output) const;     // option saved in settings (default on)
    void setVirtualOption(bool output, bool enabled); // turn SAR + Windows default on/off
    void showVirtualAudioHelp();
    void updateVirtualButton();

    EasyEffectsAudioProcessor& hostProcessor;      // the Input pipeline (owned by JUCE)
    EasyEffectsAudioProcessor* activeProc = nullptr;
    Pipeline activePipeline = Pipeline::Input;
    Page currentPage = Page::Input;
    std::unique_ptr<eeval::SecondaryPipeline> outputPipeline;
    std::unique_ptr<eeval::AsioHost> asioHost;     // declared after the pipelines: destroyed first
    juce::String asioDriver;                       // non-empty = ASIO engine selected
    int saveCountdown = 0;
    juce::String themeFallback { "system" };       // plug-in builds have no settings file

    eeval::theme::CustomLookAndFeel customTheme;
    juce::TooltipWindow tooltipWindow { this, 700 };

    // Navigation pane
    eeval::ui::NavigationPane nav;
    bool navUserCompact = false;

    // Page header + command bar
    juce::TextButton presetBtn;
    juce::TextButton bypassBtn  { "Bypass" };
    juce::TextButton monitorBtn { "Monitor" };
    juce::Rectangle<int> layerArea, headerArea;

    // Spectrum (card under the header)
    std::unique_ptr<eeval::ui::SpectrumAnalyzerEditor> fftAnalyzer;
    juce::Rectangle<int> spectrumCard;

    // Effects card
    juce::TextButton addEffectBtn { "Add" };
    juce::ListBox moduleList;
    std::vector<EasyEffectsAudioProcessor::SlotInfo> activeSlots;
    juce::Rectangle<int> effectsCard, effectsHeader, sourceRowArea, sinkRowArea;
    int hoveredEndRow = 0;   // 0 none, 1 source, 2 sink (clickable device rows)

    // Effect page
    juce::Viewport viewport;
    std::unique_ptr<eeval::ui::EffectPage> currentEditor;
    juce::Rectangle<int> contentArea;

    // Devices / Settings pages
    juce::Viewport settingsViewport;
    std::unique_ptr<eeval::ui::SettingsPage> devicesPage, settingsPage;

    // Updates (declared after the pages: its callbacks refresh them)
    eeval::app::Updater updater;
    bool automaticUpdateCheck = false;   // prompt only for the start-up check
    bool autostartCached = false;        // "\EasyEffects\Autostart" task exists
    void promptForUpdate(const eeval::app::Updater::Release& r);

    // Monitor state
    bool isMonitoring = false;
    juce::String savedOutputDevice;  // original output device before monitor toggle
    juce::String monitorDeviceName;  // user-chosen monitor/playback device
    juce::AudioDeviceManager* monitoredDeviceManager = nullptr;  // pipeline being monitored

    // Layout constants
    static constexpr int pagePadding = 24;
    static constexpr int headerHeight = 76;
    static constexpr int fftHeight = 128;
    static constexpr int effectsCardWidth = 320;
    static constexpr int effectsHeaderHeight = 52;
    static constexpr int endRowHeight = 52;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EasyEffectsAudioProcessorEditor)
};
