#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "ui/Theme.h"
#include "ui/GenericModuleEditor.h"
#include "ui/VisualEqualizerEditor.h"
#include "ui/SpectrumAnalyzerEditor.h"
#include "ui/SidebarRowCustomComponent.h"
#include "ui/EffectPage.h"
#include "dsp/EffectRegistry.h"
#include "audio/SecondaryPipeline.h"
#include "audio/AsioHost.h"

/**
 * Main window, laid out like EasyEffects 8:
 *
 *  [Effects][Presets]     [ Bypass | Monitor | Virtual Audio ]     preset  [≡]
 *  ▂▃▅▇▅▃▂▃▅▇▆▄▂ spectrum (20 … 10.7k Hz) ▂▃▅▇▅▃▂▃▅▇▆▄▂
 *  [+ Add effect]        |  Input [0.00 dB] [-17][-17]  Output [0.00 dB] [-19][-19]
 *  ↓ Input  <device>     |  ┌ Controls ───────────┐ ┌ Dynamics ──┐
 *  ↓ Effect 1     ⋮ ⇅    |  │ label     ───●──── │ │            │
 *  ↓ Effect 2     ⋮ ⇅    |  └─────────────────────┘ └────────────┘
 *  ↓ Output device
 *  48.0 kHz · in → out
 */
class EasyEffectsAudioProcessorEditor : public juce::AudioProcessorEditor,
                                        private juce::ListBoxModel,
                                        private juce::Timer
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

private:
    void rebuildEditorView();
    void layoutCurrentEditor();
    void showAddEffectMenu();
    void showPresetMenu();
    void showMainMenu();
    void chooseMonitorDevice();
    void toggleMonitor();
    void moveEffectBy(int slotIndex, int delta);

    juce::String currentInputDeviceName() const;
    juce::String currentOutputDeviceName() const;

    // --- Output / Input pipelines (EasyEffects' two tabs) -----------------
    // Input  = the standalone holder's processor + devices (the original pipeline)
    // Output = eeval::SecondaryPipeline (standalone only)
    enum class Pipeline { Output, Input };
    void initPipelines();
    void switchPipeline(Pipeline p);
    bool hasPipelineTabs() const { return outputPipeline != nullptr; }
    juce::AudioDeviceManager* activeDeviceManager() const;
    void showDeviceMenu(bool inputSide);
    void showAudioSettings();
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
    void showPairMenu(bool inputSide);
    int  activePipelineIndex() const {
        return activePipeline == Pipeline::Output ? eeval::AsioHost::outputPipeline
                                                  : eeval::AsioHost::inputPipeline;
    }

    // Built-in virtual audio routing (standalone only)
    bool virtualOptionEnabled(bool output) const;     // option saved in settings (default on)
    void setVirtualOption(bool output, bool enabled); // turn SAR + Windows default on/off
    void showVirtualAudioMenu();
    void updateVirtualButton();

    // button drawn with three lines (no icon font needed)
    class MenuButton : public juce::Button {
    public:
        MenuButton() : juce::Button("Menu") {}
        void paintButton(juce::Graphics& g, bool over, bool down) override {
            auto r = getLocalBounds().toFloat().reduced(0.5f);
            if (over || down) {
                g.setColour(down ? eeval::theme::accentPrimary.withAlpha(0.35f) : eeval::theme::bgOverlay);
                g.fillRoundedRectangle(r, eeval::theme::radius);
                g.setColour(eeval::theme::accentPrimary);
                g.drawRoundedRectangle(r, eeval::theme::radius, 1.0f);
            }
            g.setColour(eeval::theme::textPrimary);
            const float cx = r.getCentreX(), cy = r.getCentreY();
            for (int i = -1; i <= 1; ++i)
                g.fillRoundedRectangle(cx - 7.0f, cy + (float)i * 5.0f - 0.75f, 14.0f, 1.5f, 0.75f);
        }
    };

    EasyEffectsAudioProcessor& hostProcessor;      // the Input pipeline (owned by JUCE)
    EasyEffectsAudioProcessor* activeProc = nullptr;
    Pipeline activePipeline = Pipeline::Input;
    std::unique_ptr<eeval::SecondaryPipeline> outputPipeline;
    std::unique_ptr<eeval::AsioHost> asioHost;     // declared after the pipelines: destroyed first
    juce::String asioDriver;                       // non-empty = ASIO engine selected
    int saveCountdown = 0;

    eeval::theme::CustomLookAndFeel customTheme;

    // Toolbar
    juce::TextButton effectsTab  { "Effects" };
    juce::TextButton presetBtn   { "Presets" };
    juce::TextButton outputTab   { "Output" };
    juce::TextButton inputTab    { "Input" };
    juce::Rectangle<int> pipelineGroupArea;
    juce::TextButton bypassBtn   { "Bypass" };
    juce::TextButton monitorBtn  { "Monitor" };
    juce::TextButton virtualBtn  { "Virtual Audio" };
    MenuButton menuBtn;
    juce::Label presetNameLabel;
    juce::Rectangle<int> segmentGroupArea;

    // Spectrum (full width, under the toolbar)
    std::unique_ptr<eeval::ui::SpectrumAnalyzerEditor> fftAnalyzer;

    // Sidebar
    juce::TextButton addEffectBtn { "+ Add effect" };
    juce::ListBox moduleList;
    std::vector<EasyEffectsAudioProcessor::SlotInfo> activeSlots;
    juce::Rectangle<int> sidebarArea, sourceRowArea, sinkRowArea;
    int hoveredEndRow = 0;   // 0 none, 1 source, 2 sink (clickable device rows)

    // Main Content
    juce::Viewport viewport;
    std::unique_ptr<eeval::ui::EffectPage> currentEditor;
    juce::Rectangle<int> contentArea, footerArea;

    // Monitor state
    bool isMonitoring = false;
    juce::String savedOutputDevice;  // original output device before monitor toggle
    juce::String monitorDeviceName;  // user-chosen monitor/playback device
    juce::AudioDeviceManager* monitoredDeviceManager = nullptr;  // pipeline being monitored

    // Layout constants
    static constexpr int headerHeight = 46;
    static constexpr int fftHeight = 120;
    static constexpr int footerHeight = 26;
    static constexpr int sidebarWidth = 220;
    static constexpr int sidebarHeaderHeight = 44;
    static constexpr int endRowHeight = 34;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EasyEffectsAudioProcessorEditor)
};
