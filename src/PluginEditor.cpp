#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "ui/ConvolverEditor.h"
#include "audio/WindowsAudioEndpoints.h"
#include "audio/SarAutoSetup.h"

// Elevation can't change while the process runs; ask Windows once.
static bool isElevatedCached()
{
    static const bool elevated = eeval::winaudio::isProcessElevated();
    return elevated;
}

// Access JUCE standalone holder for device management
#if JucePlugin_Build_Standalone
 #include <juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h>
#endif

EasyEffectsAudioProcessorEditor::EasyEffectsAudioProcessorEditor(EasyEffectsAudioProcessor& p)
    : AudioProcessorEditor(&p), hostProcessor(p), activeProc(&p)
{
    setLookAndFeel(&customTheme);
    setResizable(true, true);
    setResizeLimits(880, 560, 3000, 2000);
    setSize(1080, 700);

    activeSlots = proc().getActiveSlots();

    // --- Toolbar: left tabs ---------------------------------------------
    for (auto* b : { &effectsTab, &presetBtn, &bypassBtn, &monitorBtn, &virtualBtn })
        b->getProperties().set("flat", true);   // EasyEffects toolbar buttons are flat

    effectsTab.setToggleState(true, juce::dontSendNotification);   // the only page
    effectsTab.onClick = [this] { effectsTab.setToggleState(true, juce::dontSendNotification); };
    addAndMakeVisible(effectsTab);

    presetBtn.onClick = [this] { showPresetMenu(); };
    addAndMakeVisible(presetBtn);

    // --- Toolbar: centre Output | Input pipeline switcher (standalone) ---
    for (auto* t : { &outputTab, &inputTab }) {
        t->getProperties().set("flat", true);
        t->setRadioGroupId(4711);
        addChildComponent(*t);   // shown once the Output pipeline exists
    }
    outputTab.setTooltip("Effects for what your apps play (system audio)");
    inputTab.setTooltip("Effects for your microphone");
    outputTab.onClick = [this] { switchPipeline(Pipeline::Output); };
    inputTab.onClick  = [this] { switchPipeline(Pipeline::Input); };

    // --- Toolbar: right-hand actions --------------------------------------
    bypassBtn.setClickingTogglesState(true);
    bypassBtn.setColour(juce::TextButton::buttonOnColourId, eeval::theme::accentDanger);
    bypassBtn.setTooltip("Bypass every effect in this pipeline (A/B comparison)");
    bypassBtn.onClick = [this] {
        proc().setGlobalBypass(bypassBtn.getToggleState());
    };
    addAndMakeVisible(bypassBtn);

    monitorBtn.setClickingTogglesState(true);
    monitorBtn.setColour(juce::TextButton::buttonOnColourId, eeval::theme::accentSuccess);
    monitorBtn.setTooltip("Temporarily send the output to your monitor device");
    monitorBtn.onClick = [this] { toggleMonitor(); };
    addAndMakeVisible(monitorBtn);

    // --- Toolbar: right --------------------------------------------------
    menuBtn.onClick = [this] { showMainMenu(); };
    addAndMakeVisible(menuBtn);

    // --- Sidebar ---------------------------------------------------------
    addEffectBtn.onClick = [this] { showAddEffectMenu(); };
    addAndMakeVisible(addEffectBtn);

    // Virtual Audio button - routes through the built-in EasyEffects virtual devices
#if JucePlugin_Build_Standalone
    if (juce::JUCEApplicationBase::isStandaloneApp()) {
        addAndMakeVisible(virtualBtn);
        virtualBtn.setColour(juce::TextButton::buttonOnColourId, eeval::theme::accentPrimary);
        virtualBtn.onClick = [this] { showVirtualAudioMenu(); };

        // The standalone window isn't on the desktop yet while the editor is
        // being constructed, so look up the plugin holder on the next message loop.
        juce::Component::SafePointer<EasyEffectsAudioProcessorEditor> safeThis(this);
        juce::MessageManager::callAsync([safeThis] {
            if (safeThis != nullptr) {
                safeThis->initVirtualRouter();
                safeThis->initPipelines();
            }
        });
    }
#endif

    // Load saved monitor device from settings
#if JucePlugin_Build_Standalone
    if (auto* holder = juce::StandalonePluginHolder::getInstance()) {
        if (auto* props = holder->settings.get())
            monitorDeviceName = props->getValue("monitorDevice", "");
    }
#endif

    // Preset name label (right side of the toolbar)
    presetNameLabel.setFont(juce::Font(juce::FontOptions(13.0f)));
    presetNameLabel.setColour(juce::Label::textColourId, eeval::theme::textSecondary);
    presetNameLabel.setJustificationType(juce::Justification::centredRight);
    addAndMakeVisible(presetNameLabel);

    // FFT Analyzer
    fftAnalyzer = std::make_unique<eeval::ui::SpectrumAnalyzerEditor>(proc());
    addAndMakeVisible(fftAnalyzer.get());

    // Sidebar effect list
    moduleList.setModel(this);
    moduleList.setRowHeight(38);
    moduleList.setColour(juce::ListBox::backgroundColourId, eeval::theme::bgSurface);
    addAndMakeVisible(moduleList);

    // Viewport
    viewport.setScrollBarsShown(true, false);
    viewport.setScrollBarThickness(10);
    addAndMakeVisible(viewport);

    // Select first row
    int lastSelected = proc().getSelectedEditorIndex();
    if (lastSelected >= 0 && lastSelected < (int)activeSlots.size())
        moduleList.selectRow(lastSelected, false, true);
    else if (!activeSlots.empty())
        moduleList.selectRow(0, false, true);

    rebuildEditorView();
    resized();   // setSize() above ran before the child components existed
    startTimerHz(10);
}

EasyEffectsAudioProcessorEditor::~EasyEffectsAudioProcessorEditor()
{
    // If monitoring, restore original device before closing
    if (isMonitoring)
        toggleMonitor();

    // Restores the user's Windows default devices (mode stays saved for next launch)
    virtualRouter.reset();

    stopTimer();
    // Drop every UI object that points into a pipeline before the pipelines go.
    viewport.setViewedComponent(nullptr, false);
    currentEditor.reset();
    moduleList.setModel(nullptr);
    fftAnalyzer.reset();
    activeProc = &hostProcessor;
    juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("EasyEffects").getChildFile("asio.log")
        .appendText(juce::Time::getCurrentTime().toString(true, true, true, true) + "  editor shutting down\n");
    if (asioEnabled())
        restoreWindowsDefaultsFromSar();   // before SAR's devices disappear
    asioHost.reset();         // stop the ASIO callback before the processors it drives go away
                              // ("asio.driver" stays saved, so ASIO resumes next launch)
    outputPipeline.reset();   // saves its chain + devices
    setLookAndFeel(nullptr);
}

// --- Output / Input pipelines --------------------------------------------

void EasyEffectsAudioProcessorEditor::initPipelines()
{
#if JucePlugin_Build_Standalone
    if (outputPipeline != nullptr)
        return;

    auto* holder = juce::StandalonePluginHolder::getInstance();
    if (holder == nullptr)
        return;

    outputPipeline = std::make_unique<eeval::SecondaryPipeline>(
        holder->settings.get(), "outputPipeline",
        holder->deviceManager.getAudioDeviceSetup().outputDeviceName);

    outputTab.setVisible(true);
    inputTab.setVisible(true);

    // ASIO engine: one driver (e.g. Synchronous Audio Router) for both pipelines.
    asioHost = std::make_unique<eeval::AsioHost>(outputPipeline->processor(), hostProcessor,
                                                 holder->settings.get());
    {
        juce::Component::SafePointer<EasyEffectsAudioProcessorEditor> safeThis(this);
        asioHost->onChange = [safeThis] { if (safeThis != nullptr) safeThis->repaint(); };
    }

    // Reopen the tab that was shown last time (Input on first run, where the
    // existing chain lives).
    Pipeline initial = Pipeline::Input;
    if (auto* props = holder->settings.get())
        if (props->getValue("ui.activePipeline", "input") == "output")
            initial = Pipeline::Output;

    switchPipeline(initial);

    if (auto* props = holder->settings.get()) {
        const auto savedDriver = props->getValue("asio.driver");
        if (savedDriver.isNotEmpty())
            enableAsio(savedDriver);
    }
#endif
}

// --- ASIO engine ----------------------------------------------------------

void EasyEffectsAudioProcessorEditor::enableAsio(const juce::String& driver)
{
#if JucePlugin_Build_Standalone
    auto* holder = juce::StandalonePluginHolder::getInstance();
    if (asioHost == nullptr || outputPipeline == nullptr || holder == nullptr)
        return;

    // Monitoring changes Windows Audio devices that are about to close.
    if (isMonitoring)
        toggleMonitor();

    // ASIO drivers are single-client and now run both pipelines themselves.
    holder->deviceManager.closeAudioDevice();
    outputPipeline->deviceManager().closeAudioDevice();

    asioDriver = driver;
    if (auto* props = holder->settings.get())
        props->setValue("asio.driver", driver);

    monitorBtn.setEnabled(false);
    virtualBtn.setEnabled(false);
    monitorBtn.setTooltip("Not available with ASIO: route a pair to your monitor outputs instead");

    // Synchronous Audio Router: write its config (hardware + EasyEffects'
    // endpoints) BEFORE opening it — SAR reads the file when the driver loads.
    eeval::sar::Result sarSetup;
    const bool isSar = eeval::sar::isSarDriver(driver);
    if (isSar) {
        juce::String preferred;
        if (auto* props = holder->settings.get())
            preferred = props->getValue("sar.hardware");
        sarSetup = eeval::sar::ensureConfig(asioHost->getDriverNames(), preferred);
        if (sarSetup.error.isEmpty() && sarSetup.hardware.isNotEmpty())
            if (auto* props = holder->settings.get())
                props->setValue("sar.hardware", sarSetup.hardware);
    }

    // Newly created endpoints must be wired up on the first run where SAR
    // actually starts (often the elevated relaunch), so remember that.
    if (isSar && sarSetup.endpointsAdded)
        if (auto* props = holder->settings.get())
            props->setValue("sar.pendingAutoRoute", true);

    const auto err = asioHost->open(driver);
    const bool needsAdmin = driverNeedsElevation(driver) && !isElevatedCached();
    const bool driverRunning = asioHost->isOpen()
        && (err.isEmpty() || err.startsWith("The ASIO driver is running"));

    if (isSar && driverRunning && sarSetup.error.isEmpty()) {
        bool force = false;
        if (auto* props = holder->settings.get()) {
            force = props->getBoolValue("sar.pendingAutoRoute", false);
            props->removeValue("sar.pendingAutoRoute");
        }
        // New endpoints: wire both tabs to them. Otherwise only fill empty sides.
        autoRouteSar(force);
        if (!needsAdmin)
            sarDefaultsCountdown = 80;   // SAR adds the Windows devices a moment after starting
    }
    if (sarSetup.error.isNotEmpty())
        juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,
                                               "Synchronous Audio Router", sarSetup.error);

    if (needsAdmin) {
        // SAR runs without admin rights, but it only creates its Windows
        // playback/recording devices for an elevated host.
        juce::Component::SafePointer<EasyEffectsAudioProcessorEditor> safeThis(this);
        juce::AlertWindow::showAsync(juce::MessageBoxOptions()
            .withIconType(juce::MessageBoxIconType::InfoIcon)
            .withTitle("Administrator rights needed")
            .withMessage("Synchronous Audio Router only creates its Windows playback and recording "
                         "devices when EasyEffects runs as administrator.\n\n"
                         "Restart EasyEffects as administrator now? Your settings are saved first.")
            .withButton("Restart as administrator")
            .withButton("Not now"),
            [safeThis](int result) {
                if (result == 1 && safeThis != nullptr)
                    safeThis->restartElevated();
            });
    } else if (err.isNotEmpty()) {
        if (asioHost->isOpen())
            juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, "ASIO",
                "\"" + driver + "\" is running, but a Windows Audio device could not be started:\n\n" + err +
                "\n\nPick another device under the Input tab's \"Input device\" row or the "
                "Output tab's \"Output device\" row.");
        else
            juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, "ASIO",
                "Couldn't start \"" + driver + "\":\n" + err +
                "\n\nIf this is Synchronous Audio Router, open its control panel "
                "(menu > ASIO control panel) and choose your audio interface (e.g. FLOW ASIO Driver) as the hardware.");
    }
    repaint();
#else
    juce::ignoreUnused(driver);
#endif
}

void EasyEffectsAudioProcessorEditor::autoRouteSar(bool force)
{
    if (asioHost == nullptr || !asioHost->isOpen())
        return;

    using Host = eeval::AsioHost;
    const int outPipe = Host::outputPipeline, inPipe = Host::inputPipeline;
    const int speakers = asioHost->findPairByChannelName(true,  juce::String(eeval::sar::speakersEndpoint) + " 1");
    const int mic      = asioHost->findPairByChannelName(false, juce::String(eeval::sar::microphoneEndpoint) + " 1");
    const bool hasHwIn  = !asioHost->getPairNames(true).isEmpty();
    const bool hasHwOut = !asioHost->getPairNames(false).isEmpty();

    // Output tab:  EasyEffects Speakers (what apps play) -> effects -> interface outputs (pair 0)
    if (speakers >= 0 && (force || (asioHost->getInputSource(outPipe) == Host::InputSource::AsioPair
                                    && asioHost->getRoute(outPipe, true) < 0))) {
        asioHost->setInputSource(outPipe, Host::InputSource::AsioPair);
        asioHost->setRoute(outPipe, true, speakers);
    }
    if (hasHwOut && (force || (asioHost->getOutputTarget(outPipe) == Host::OutputTarget::AsioPair
                               && asioHost->getRoute(outPipe, false) < 0))) {
        asioHost->setOutputTarget(outPipe, Host::OutputTarget::AsioPair);
        asioHost->setRoute(outPipe, false, 0);   // the interface's first outputs (main/headphones)
    }

    // Input tab:  interface inputs (pair 0, the mic) -> effects -> EasyEffects Microphone
    if (hasHwIn && (force || (asioHost->getInputSource(inPipe) == Host::InputSource::AsioPair
                              && asioHost->getRoute(inPipe, true) < 0))) {
        asioHost->setInputSource(inPipe, Host::InputSource::AsioPair);
        asioHost->setRoute(inPipe, true, 0);
    }
    if (mic >= 0 && (force || (asioHost->getOutputTarget(inPipe) == Host::OutputTarget::AsioPair
                               && asioHost->getRoute(inPipe, false) < 0))) {
        asioHost->setOutputTarget(inPipe, Host::OutputTarget::AsioPair);
        asioHost->setRoute(inPipe, false, mic);
    }
    repaint();
}

void EasyEffectsAudioProcessorEditor::trySwitchWindowsDefaultsToSar()
{
#if JucePlugin_Build_Standalone
    auto* holder = juce::StandalonePluginHolder::getInstance();
    auto* props = holder != nullptr ? holder->settings.get() : nullptr;
    if (props == nullptr) { sarDefaultsCountdown = 0; return; }

    using namespace eeval::winaudio;
    bool done = true;

    auto switchTo = [props, &done](Flow flow, const char* endpointName, const char* prevKey) {
        const juce::String ours(endpointName);
        auto isOurs = [&ours](const std::string& n) { return juce::String::fromUTF8(n.c_str()).startsWith(ours); };

        auto current = getDefaultEndpoint(flow);
        if (current && isOurs(current->name))
            return;   // already ours

        for (const auto& ep : listEndpoints(flow)) {
            if (!isOurs(ep.name)) continue;
            // Remember the user's device so it can be restored later.
            if (current && !props->containsKey(prevKey))
                props->setValue(prevKey, juce::String::fromUTF8(current->id.c_str()));
            setDefaultEndpoint(ep.id);
            return;
        }
        done = false;   // SAR hasn't created this device yet: try again on the next tick
    };

    switchTo(Flow::Render,  eeval::sar::speakersEndpoint,   "sar.prevRenderId");
    switchTo(Flow::Capture, eeval::sar::microphoneEndpoint, "sar.prevCaptureId");

    if (done || --sarDefaultsCountdown <= 0)
        sarDefaultsCountdown = 0;
#endif
}

void EasyEffectsAudioProcessorEditor::restoreWindowsDefaultsFromSar()
{
#if JucePlugin_Build_Standalone
    auto* holder = juce::StandalonePluginHolder::getInstance();
    auto* props = holder != nullptr ? holder->settings.get() : nullptr;
    if (props == nullptr) return;

    using namespace eeval::winaudio;
    auto restore = [props](Flow flow, const char* endpointName, const char* prevKey) {
        const auto prev = props->getValue(prevKey);
        if (prev.isEmpty()) return;
        // Only if the default is still ours — respect a choice made meanwhile.
        auto current = getDefaultEndpoint(flow);
        if (!current || juce::String::fromUTF8(current->name.c_str()).startsWith(endpointName))
            setDefaultEndpoint(prev.toStdString());
        props->removeValue(prevKey);
    };
    restore(Flow::Render,  eeval::sar::speakersEndpoint,   "sar.prevRenderId");
    restore(Flow::Capture, eeval::sar::microphoneEndpoint, "sar.prevCaptureId");
    if (auto* f = dynamic_cast<juce::PropertiesFile*>(props))
        f->saveIfNeeded();
#endif
}

void EasyEffectsAudioProcessorEditor::restartElevated()
{
#if JucePlugin_Build_Standalone
    // Persist everything before the elevated copy starts and reads the settings.
    if (outputPipeline != nullptr)
        outputPipeline->save();
    if (auto* holder = juce::StandalonePluginHolder::getInstance()) {
        holder->savePluginState();
        if (auto* props = dynamic_cast<juce::PropertiesFile*>(holder->settings.get()))
            props->saveIfNeeded();
    }

    // ASIO drivers are single-client: free it so the new instance can open it.
    if (asioHost != nullptr)
        asioHost->close();

    if (!eeval::winaudio::relaunchSelfElevated()) {
        // Declined or failed: carry on without admin rights.
        if (asioHost != nullptr && asioDriver.isNotEmpty())
            asioHost->open(asioDriver);
        repaint();
        return;
    }

    juce::JUCEApplicationBase::quit();
#endif
}

void EasyEffectsAudioProcessorEditor::disableAsio()
{
#if JucePlugin_Build_Standalone
    auto* holder = juce::StandalonePluginHolder::getInstance();
    // While SAR's devices still exist (and are still the default), hand the
    // Windows defaults back to the user's previous devices.
    restoreWindowsDefaultsFromSar();
    sarDefaultsCountdown = 0;
    if (asioHost != nullptr)
        asioHost->close();
    asioDriver.clear();

    if (holder != nullptr) {
        if (auto* props = holder->settings.get())
            props->setValue("asio.driver", juce::String());
        holder->deviceManager.restartLastAudioDevice();
    }
    if (outputPipeline != nullptr)
        outputPipeline->deviceManager().restartLastAudioDevice();

    monitorBtn.setEnabled(true);
    virtualBtn.setEnabled(true);
    monitorBtn.setTooltip("Temporarily send the output to your monitor device");
    repaint();
#endif
}

void EasyEffectsAudioProcessorEditor::showPairMenu(bool inputSide)
{
    using InputSource  = eeval::AsioHost::InputSource;
    using OutputTarget = eeval::AsioHost::OutputTarget;
    if (asioHost == nullptr) return;

    const int pipe = activePipelineIndex();
    const auto pairs = asioHost->getPairNames(inputSide);
    const int current = asioHost->getRoute(pipe, inputSide);
    const bool windowsSource = inputSide && asioHost->getInputSource(pipe) == InputSource::WindowsAudio;
    const bool windowsSink = !inputSide && asioHost->getOutputTarget(pipe) == OutputTarget::WindowsAudio;
    const bool windowsSelected = windowsSource || windowsSink;

    // The microphone chain (Input pipeline) can take its source from Windows
    // Audio; the playback chain (Output pipeline) can send its result to a
    // Windows Audio output device.
    const bool offerWindowsInput  = inputSide && pipe == eeval::AsioHost::inputPipeline;
    const bool offerWindowsOutput = !inputSide && pipe == eeval::AsioHost::outputPipeline;

    const auto windowsInputs = offerWindowsInput ? asioHost->getWindowsInputNames()
                                                 : juce::StringArray();
    const auto windowsOutputs = offerWindowsOutput ? asioHost->getWindowsOutputNames()
                                                   : juce::StringArray();

    juce::PopupMenu menu;
    menu.addSectionHeader(juce::String(inputSide ? "Input" : "Output") + " channels - " + asioDriver);
    menu.addItem(1000, "None", true, !windowsSelected && current < 0);
    for (int i = 0; i < pairs.size(); ++i)
        menu.addItem(i + 1, pairs[i], true, !windowsSelected && i == current);
    if (pairs.isEmpty())
        menu.addItem(-1, asioHost->isOpen() ? "(driver has no channels)" : "(driver not running)", false, false);

    if (offerWindowsInput) {
        menu.addSeparator();
        menu.addSectionHeader("Windows Audio microphone");
        if (windowsInputs.isEmpty())
            menu.addItem(-1, "(no Windows Audio inputs found)", false, false);
        for (int i = 0; i < windowsInputs.size(); ++i)
            menu.addItem(3000 + i, windowsInputs[i], true,
                         windowsSource && windowsInputs[i] == asioHost->getWindowsInputName(pipe));
    }

    if (offerWindowsOutput) {
        menu.addSeparator();
        menu.addSectionHeader("Windows Audio output device");
        if (windowsOutputs.isEmpty())
            menu.addItem(-1, "(no Windows Audio outputs found)", false, false);
        for (int i = 0; i < windowsOutputs.size(); ++i)
            menu.addItem(4000 + i, windowsOutputs[i], true,
                         windowsSink && windowsOutputs[i] == asioHost->getWindowsOutputName(pipe));
    }

    menu.addSeparator();
    menu.addItem(2000, "Open " + asioDriver + " control panel...");

    juce::Component::SafePointer<EasyEffectsAudioProcessorEditor> safeThis(this);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetScreenArea(
                           localAreaToGlobal(inputSide ? sourceRowArea : sinkRowArea)),
        [safeThis, pipe, inputSide, windowsInputs, windowsOutputs](int r) {
            if (safeThis == nullptr || safeThis->asioHost == nullptr || r <= 0) return;
            auto& host = *safeThis->asioHost;

            juce::String error;
            juce::String title = "Windows Audio";

            if (r == 2000) {
                host.showControlPanel();
                return;
            }

            if (r >= 4000 && r - 4000 < windowsOutputs.size()) {
                error = host.setOutputTarget(pipe, OutputTarget::WindowsAudio,
                                             windowsOutputs[r - 4000]);
                title = "Windows Audio output";
            } else if (r >= 3000 && r - 3000 < windowsInputs.size()) {
                error = host.setInputSource(pipe, InputSource::WindowsAudio,
                                            windowsInputs[r - 3000]);
                title = "Windows Audio input";
            } else if (inputSide) {
                host.setInputSource(pipe, InputSource::AsioPair);
                host.setRoute(pipe, true, r == 1000 ? -1 : r - 1);
            } else {
                host.setOutputTarget(pipe, OutputTarget::AsioPair);
                host.setRoute(pipe, false, r == 1000 ? -1 : r - 1);
            }

            if (error.isNotEmpty())
                juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,
                                                       title, error);
            safeThis->repaint();
            safeThis->repaint(safeThis->sourceRowArea);
            safeThis->repaint(safeThis->sinkRowArea);
        });
}

void EasyEffectsAudioProcessorEditor::switchPipeline(Pipeline p)
{
    EasyEffectsAudioProcessor* target = &hostProcessor;
    if (p == Pipeline::Output) {
        if (outputPipeline == nullptr) return;
        target = &outputPipeline->processor();
    }

    outputTab.setToggleState(p == Pipeline::Output, juce::dontSendNotification);
    inputTab.setToggleState(p == Pipeline::Input, juce::dontSendNotification);

    if (target == activeProc && p == activePipeline && fftAnalyzer != nullptr && currentEditor != nullptr) {
        repaint();
        return;
    }

    // Tear down everything bound to the old pipeline's processor.
    viewport.setViewedComponent(nullptr, false);
    currentEditor.reset();
    fftAnalyzer.reset();

    activePipeline = p;
    activeProc = target;

    fftAnalyzer = std::make_unique<eeval::ui::SpectrumAnalyzerEditor>(proc());
    addAndMakeVisible(fftAnalyzer.get());

    bypassBtn.setToggleState(proc().getGlobalBypass(), juce::dontSendNotification);

#if JucePlugin_Build_Standalone
    if (auto* holder = juce::StandalonePluginHolder::getInstance())
        if (auto* props = holder->settings.get())
            props->setValue("ui.activePipeline", p == Pipeline::Output ? "output" : "input");
#endif

    refreshSidebar();   // rebuilds rows + effect page for the new chain
    resized();
    repaint();
}

juce::AudioDeviceManager* EasyEffectsAudioProcessorEditor::activeDeviceManager() const
{
    if (asioEnabled())
        return nullptr;   // devices are ASIO channel pairs, not Windows Audio devices
    if (activePipeline == Pipeline::Output && outputPipeline != nullptr)
        return &outputPipeline->deviceManager();
#if JucePlugin_Build_Standalone
    if (auto* holder = juce::StandalonePluginHolder::getInstance())
        return &holder->deviceManager;
#endif
    return nullptr;
}

void EasyEffectsAudioProcessorEditor::showDeviceMenu(bool inputSide)
{
    if (asioEnabled()) { showPairMenu(inputSide); return; }

    auto* dm = activeDeviceManager();
    if (dm == nullptr) return;
    auto* type = dm->getCurrentDeviceTypeObject();
    if (type == nullptr) return;

    type->scanForDevices();
    const auto names = type->getDeviceNames(inputSide);
    const auto setup = dm->getAudioDeviceSetup();
    const auto current = inputSide ? setup.inputDeviceName : setup.outputDeviceName;

    juce::PopupMenu menu;
    menu.addSectionHeader(inputSide ? "Input device" : "Output device");
    if (inputSide)
        menu.addItem(1000, "None", true, current.isEmpty());
    for (int i = 0; i < names.size(); ++i)
        menu.addItem(i + 1, names[i], true, names[i] == current);
    menu.addSeparator();
    menu.addItem(2000, "Audio settings...");

    juce::Component::SafePointer<EasyEffectsAudioProcessorEditor> safeThis(this);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetScreenArea(
                           localAreaToGlobal(inputSide ? sourceRowArea : sinkRowArea)),
        [safeThis, dm, names, inputSide](int r) {
            if (safeThis == nullptr || r <= 0) return;
            if (r == 2000) { safeThis->showAudioSettings(); return; }

            auto s = dm->getAudioDeviceSetup();
            const juce::String chosen = (r == 1000) ? juce::String() : names[r - 1];
            if (inputSide) { s.inputDeviceName = chosen;  s.useDefaultInputChannels = true; }
            else           { s.outputDeviceName = chosen; s.useDefaultOutputChannels = true; }

            const auto err = dm->setAudioDeviceSetup(s, true);
            if (err.isNotEmpty())
                juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,
                                                       "Audio device", err);
            if (safeThis->outputPipeline != nullptr)
                safeThis->outputPipeline->save();
            safeThis->repaint();
        });
}

void EasyEffectsAudioProcessorEditor::showAudioSettings()
{
    if (asioEnabled()) { asioHost->showControlPanel(); return; }   // rate/buffer live in the driver

    auto* dm = activeDeviceManager();
    if (dm == nullptr) return;

    auto* selector = new juce::AudioDeviceSelectorComponent(*dm, 0, 2, 0, 2, false, false, true, false);
    selector->setSize(520, 440);

    juce::DialogWindow::LaunchOptions o;
    o.content.setOwned(selector);
    o.dialogTitle = (activePipeline == Pipeline::Output && hasPipelineTabs())
                        ? "Output pipeline - audio settings" : "Input pipeline - audio settings";
    o.dialogBackgroundColour = eeval::theme::bgBase;
    o.escapeKeyTriggersCloseButton = true;
    o.useNativeTitleBar = true;
    o.resizable = false;
    o.launchAsync();
}

void EasyEffectsAudioProcessorEditor::mouseDown(const juce::MouseEvent& e)
{
    if (sourceRowArea.contains(e.getPosition()))
        showDeviceMenu(true);
    else if (sinkRowArea.contains(e.getPosition()))
        showDeviceMenu(false);
}

void EasyEffectsAudioProcessorEditor::mouseMove(const juce::MouseEvent& e)
{
    const int hot = sourceRowArea.contains(e.getPosition()) ? 1
                  : sinkRowArea.contains(e.getPosition())   ? 2 : 0;
    if (hot != hoveredEndRow) {
        hoveredEndRow = hot;
        setMouseCursor(hot != 0 ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
        repaint(sourceRowArea);
        repaint(sinkRowArea);
    }
}

void EasyEffectsAudioProcessorEditor::mouseExit(const juce::MouseEvent&)
{
    if (hoveredEndRow != 0) {
        hoveredEndRow = 0;
        setMouseCursor(juce::MouseCursor::NormalCursor);
        repaint(sourceRowArea);
        repaint(sinkRowArea);
    }
}

void EasyEffectsAudioProcessorEditor::timerCallback()
{
    // Update preset name label
    auto& pm = proc().getPresetManager();
    juce::String label;
    if (pm.hasActivePreset()) {
        label = juce::String(pm.getActivePresetName());
        if (pm.isDirty()) label += " *";
    } else {
        label = "(No Preset)";
    }
    presetNameLabel.setText(label, juce::dontSendNotification);

    // Sync bypass button
    bypassBtn.setToggleState(proc().getGlobalBypass(), juce::dontSendNotification);

#if JucePlugin_Build_Standalone
    // While ASIO runs both pipelines, the Windows Audio devices must stay closed
    // (e.g. if something re-opened one via Options > Audio Settings).
    if (asioEnabled()) {
        if (auto* holder = juce::StandalonePluginHolder::getInstance())
            if (holder->deviceManager.getCurrentAudioDevice() != nullptr)
                holder->deviceManager.closeAudioDevice();
        if (outputPipeline != nullptr && outputPipeline->deviceManager().getCurrentAudioDevice() != nullptr)
            outputPipeline->deviceManager().closeAudioDevice();
    }
#endif

    // Make EasyEffects' SAR devices the Windows defaults once SAR has created them.
    if (sarDefaultsCountdown > 0)
        trySwitchWindowsDefaultsToSar();

    // Persist the Output pipeline every ~10 s (it's also saved on exit).
    if (outputPipeline != nullptr && ++saveCountdown >= 100) {
        saveCountdown = 0;
        outputPipeline->save();
    }

    // Rows read their bypass state live; device names can change in Audio Settings.
    moduleList.repaint();
    repaint(sourceRowArea);
    repaint(sinkRowArea);
    repaint(footerArea);
}

juce::String EasyEffectsAudioProcessorEditor::currentInputDeviceName() const
{
    if (asioEnabled())
        return asioHost->getSourceName(activePipelineIndex());
    if (auto* dm = activeDeviceManager()) {
        auto name = dm->getAudioDeviceSetup().inputDeviceName;
        return name.isNotEmpty() ? name : juce::String("None");
    }
    return "Host input";
}

juce::String EasyEffectsAudioProcessorEditor::currentOutputDeviceName() const
{
    if (asioEnabled())
        return asioHost->getSinkName(activePipelineIndex());
    if (auto* dm = activeDeviceManager()) {
        auto name = dm->getAudioDeviceSetup().outputDeviceName;
        return name.isNotEmpty() ? name : juce::String("None");
    }
    return "Host output";
}

void EasyEffectsAudioProcessorEditor::paint(juce::Graphics& g)
{
    using namespace eeval;
    g.fillAll(theme::bgBase);

    // --- Toolbar ---------------------------------------------------------
    g.setColour(theme::bgToolbar);
    g.fillRect(0, 0, getWidth(), headerHeight);
    g.setColour(theme::borderSubtle);
    g.fillRect(0, headerHeight - 1, getWidth(), 1);

    // Segmented group frames: centre "Output | Input", right "Bypass | Monitor | Virtual Audio"
    auto drawGroupFrame = [&g](juce::Rectangle<int> area) {
        if (area.isEmpty()) return;
        auto r = area.toFloat().expanded(2.0f, 2.0f);
        g.setColour(theme::bgSurface);
        g.fillRoundedRectangle(r, theme::radius + 1.0f);
        g.setColour(theme::borderSubtle);
        g.drawRoundedRectangle(r.reduced(0.5f), theme::radius + 1.0f, 1.0f);
    };
    if (hasPipelineTabs())
        drawGroupFrame(pipelineGroupArea);
    drawGroupFrame(segmentGroupArea);

    // --- Sidebar ---------------------------------------------------------
    g.setColour(theme::bgSurface);
    g.fillRect(sidebarArea);
    g.setColour(theme::borderSubtle);
    g.fillRect(sidebarArea.getRight(), sidebarArea.getY(), 1, sidebarArea.getHeight());

    // Source / sink ends of the chain. Clickable: they open a device menu.
    const bool clickable = activeDeviceManager() != nullptr || asioEnabled();
    auto drawEndRow = [&g, clickable](juce::Rectangle<int> r, const juce::String& title,
                                      const juce::String& device, bool hovered) {
        if (r.isEmpty()) return;

        if (hovered && clickable) {
            g.setColour(theme::bgOverlay);
            g.fillRoundedRectangle(r.reduced(6, 3).toFloat(), theme::radius);
        }

        auto row = r.reduced(14, 0);
        if (clickable) {
            // small ▾ hinting that the device can be changed
            juce::Path chev;
            const float x = (float)row.getRight() - 6.0f, y = (float)row.getCentreY();
            chev.startNewSubPath(x - 4.0f, y - 2.0f); chev.lineTo(x, y + 2.0f); chev.lineTo(x + 4.0f, y - 2.0f);
            g.setColour(hovered ? theme::textPrimary : theme::textSecondary);
            g.strokePath(chev, juce::PathStrokeType(1.4f));
            row.removeFromRight(16);
        }

        juce::Path arrow;
        const float cx = (float)row.getX() + 8.0f, cy = (float)row.getCentreY();
        arrow.startNewSubPath(cx, cy - 6.0f); arrow.lineTo(cx, cy + 5.0f);
        arrow.startNewSubPath(cx - 4.0f, cy + 1.0f); arrow.lineTo(cx, cy + 5.0f); arrow.lineTo(cx + 4.0f, cy + 1.0f);
        g.setColour(theme::accentPrimary);
        g.strokePath(arrow, juce::PathStrokeType(1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        row.removeFromLeft(24);

        g.setColour(theme::textPrimary);
        g.setFont(juce::Font(juce::FontOptions(14.0f, juce::Font::bold)));
        const int titleW = juce::jmin(row.getWidth(),
            juce::roundToInt(juce::GlyphArrangement::getStringWidth(g.getCurrentFont(), title)) + 8);
        g.drawText(title, row.removeFromLeft(titleW), juce::Justification::centredLeft, false);

        g.setColour(theme::textSecondary);
        g.setFont(juce::Font(juce::FontOptions(12.0f)));
        g.drawText(device, row, juce::Justification::centredLeft, true);
    };

    // Same wording as EasyEffects: the Output pipeline is fed by the players
    // (apps) and ends in the output device; the Input pipeline starts at the
    // input device and ends in the recorders (Discord, OBS, ...).
    const bool outputTabActive = hasPipelineTabs() && activePipeline == Pipeline::Output;
    drawEndRow(sourceRowArea, outputTabActive ? "Players" : "Input device", currentInputDeviceName(), hoveredEndRow == 1);
    drawEndRow(sinkRowArea, outputTabActive ? "Output device" : (hasPipelineTabs() ? "Recorders" : "Output device"),
               currentOutputDeviceName(), hoveredEndRow == 2);

    g.setColour(theme::borderSubtle);
    if (!sourceRowArea.isEmpty()) g.fillRect(sourceRowArea.getX() + 10, sourceRowArea.getBottom() - 1, sourceRowArea.getWidth() - 20, 1);
    if (!sinkRowArea.isEmpty())   g.fillRect(sinkRowArea.getX() + 10, sinkRowArea.getY(), sinkRowArea.getWidth() - 20, 1);

    // --- Empty state ----------------------------------------------------
    if (currentEditor == nullptr && !contentArea.isEmpty()) {
        g.setColour(theme::textSecondary);
        g.setFont(juce::Font(juce::FontOptions(16.0f)));
        g.drawText("No effects yet", contentArea.withTrimmedBottom(24), juce::Justification::centred, false);
        g.setFont(juce::Font(juce::FontOptions(13.0f)));
        g.drawText("Use \"+ Add effect\" to build your chain", contentArea.withTrimmedTop(28),
                   juce::Justification::centred, false);
    }

    // --- Status bar -----------------------------------------------------
    g.setColour(theme::bgToolbar);
    g.fillRect(footerArea);
    g.setColour(theme::borderSubtle);
    g.fillRect(footerArea.getX(), footerArea.getY(), footerArea.getWidth(), 1);

    double sr = proc().getSampleRate();
    if (auto* dm = activeDeviceManager())
        if (auto* device = dm->getCurrentAudioDevice())
            sr = device->getCurrentSampleRate();
    if (asioEnabled() && asioHost->getSampleRate() > 0.0)
        sr = asioHost->getSampleRate();

    const auto dot   = juce::String::fromUTF8("\xc2\xb7");      // ·
    const auto arrow = juce::String::fromUTF8("\xe2\x86\x92");  // →
    juce::String status;
    if (hasPipelineTabs())
        status << (activePipeline == Pipeline::Output ? "Output" : "Input") << "   " << dot << "   ";
    if (asioEnabled()) {
        status << "ASIO: " << asioDriver;
        if (asioHost->isOpen() && asioHost->getBufferSize() > 0)
            status << " (" << asioHost->getBufferSize() << " samples)";
        else
            status << " (not running)";
        if (driverNeedsElevation(asioDriver) && !isElevatedCached())
            status << " - not admin, SAR devices unavailable";

        const int pipe = activePipelineIndex();
        if (asioHost->getInputSource(pipe) == eeval::AsioHost::InputSource::WindowsAudio)
            status << "   " << dot << "   mic "
                   << (asioHost->isWindowsInputRunning(pipe) ? "on" : "off");
        if (asioHost->getOutputTarget(pipe) == eeval::AsioHost::OutputTarget::WindowsAudio)
            status << "   " << dot << "   Windows out "
                   << (asioHost->isWindowsOutputRunning(pipe) ? "on" : "off");

        status << "   " << dot << "   ";
    }
    status << juce::String(sr / 1000.0, 1) << " kHz   " << dot << "   "
           << currentInputDeviceName() << "  " << arrow << "  " << currentOutputDeviceName();
    if (isMonitoring)
        status << "   " << dot << "   Monitoring: " << monitorDeviceName;

    // Both pipelines through one VB-Cable = your mic plays in the Output pipeline.
    bool sharedCable = false;
#if JucePlugin_Build_Standalone
    if (outputPipeline != nullptr && !asioEnabled())
        if (auto* holder = juce::StandalonePluginHolder::getInstance())
            sharedCable = eeval::SecondaryPipeline::sharesCableWith(
                outputPipeline->deviceManager().getAudioDeviceSetup().inputDeviceName,
                holder->deviceManager.getAudioDeviceSetup().outputDeviceName);
#endif
    if (sharedCable)
        status = "Warning: both pipelines use the same VB-Cable, so your microphone also plays in Output. "
                 "Change \"Players\" on the Output tab.";

    g.setColour(sharedCable ? theme::accentWarning
                            : (isMonitoring ? theme::accentSuccess : theme::textSecondary));
    g.setFont(juce::Font(juce::FontOptions(12.0f)));
    auto statusArea = footerArea.reduced(12, 0);
    statusArea.removeFromRight(presetNameLabel.getWidth() + 8);
    g.drawText(status, statusArea, juce::Justification::centredLeft, true);
}

void EasyEffectsAudioProcessorEditor::resized()
{
    auto area = getLocalBounds();

    // --- Toolbar ---------------------------------------------------------
    auto header = area.removeFromTop(headerHeight).reduced(8, 7);

    effectsTab.setBounds(header.removeFromLeft(84));
    header.removeFromLeft(4);
    presetBtn.setBounds(header.removeFromLeft(84));

    menuBtn.setBounds(header.removeFromRight(36));
    header.removeFromRight(10);

    // Right: Bypass | Monitor | Virtual Audio
    // (isVisible() isn't usable here: setSize() runs resized() before the button is added)
    const bool standalone = juce::JUCEApplicationBase::isStandaloneApp();
    const int groupWidth = 80 + 80 + (standalone ? 118 : 0);
    segmentGroupArea = header.removeFromRight(groupWidth).withSizeKeepingCentre(groupWidth, header.getHeight() - 4);
    {
        auto g = segmentGroupArea;
        bypassBtn.setBounds(g.removeFromLeft(80));
        monitorBtn.setBounds(g.removeFromLeft(80));
        if (standalone)
            virtualBtn.setBounds(g);
    }

    // Centre: Output | Input (EasyEffects' pipeline switcher), centred in the window
    {
        const int w = 84 * 2;
        auto centred = juce::Rectangle<int>(0, header.getY(), getWidth(), header.getHeight())
                           .withSizeKeepingCentre(w, header.getHeight() - 4);
        // keep clear of the left tabs / right group on narrow windows
        if (centred.getX() < header.getX() + 8)          centred.setX(header.getX() + 8);
        if (centred.getRight() > header.getRight() - 8)  centred.setX(header.getRight() - 8 - w);
        pipelineGroupArea = centred;
        auto g = pipelineGroupArea;
        outputTab.setBounds(g.removeFromLeft(84));
        inputTab.setBounds(g);
    }

    // --- Status bar (preset name on the right) ---------------------------
    footerArea = area.removeFromBottom(footerHeight);
    presetNameLabel.setBounds(footerArea.reduced(12, 1).removeFromRight(260));

    // --- Spectrum (full width) -------------------------------------------
    // Always reserve the space, even on the first call (before the analyzer exists).
    auto spectrumArea = area.removeFromTop(fftHeight);
    if (fftAnalyzer)
        fftAnalyzer->setBounds(spectrumArea);

    // --- Sidebar ---------------------------------------------------------
    sidebarArea = area.removeFromLeft(sidebarWidth);
    auto side = sidebarArea;
    addEffectBtn.setBounds(side.removeFromTop(sidebarHeaderHeight).reduced(10, 7));
    sourceRowArea = side.removeFromTop(endRowHeight);
    sinkRowArea = side.removeFromBottom(endRowHeight);
    moduleList.setBounds(side.reduced(0, 4));

    area.removeFromLeft(1); // separator line

    // --- Main content ----------------------------------------------------
    contentArea = area;
    viewport.setBounds(area);
    layoutCurrentEditor();
}

void EasyEffectsAudioProcessorEditor::layoutCurrentEditor()
{
    if (currentEditor == nullptr)
        return;

    const int viewH = viewport.getHeight();
    const int wanted = juce::jmax(currentEditor->getPreferredHeight(), 420);
    const bool scrolls = wanted > viewH;
    const int width = viewport.getWidth() - (scrolls ? viewport.getScrollBarThickness() : 0);
    currentEditor->setSize(juce::jmax(0, width), juce::jmax(viewH, wanted));
}

// --- Monitor Toggle ---

void EasyEffectsAudioProcessorEditor::toggleMonitor()
{
#if JucePlugin_Build_Standalone
    // Monitor the pipeline that's on screen, and always restore the one we changed.
    auto* dmPtr = isMonitoring ? monitoredDeviceManager : activeDeviceManager();
    if (dmPtr == nullptr) {
        monitorBtn.setToggleState(false, juce::dontSendNotification);
        isMonitoring = false;
        return;
    }
    auto& dm = *dmPtr;

    if (!isMonitoring) {
        // Check if monitor device is configured
        if (monitorDeviceName.isEmpty()) {
            // Prompt user to choose a monitor device
            juce::PopupMenu menu;
            menu.addSectionHeader("Choose Monitor Output Device:");

            auto* currentDevice = dm.getCurrentAudioDevice();
            auto* deviceType = currentDevice ? dm.getCurrentDeviceTypeObject() : nullptr;
            
            juce::StringArray outputDevices;
            if (deviceType)
                outputDevices = deviceType->getDeviceNames(false);

            for (int i = 0; i < outputDevices.size(); ++i)
                menu.addItem(i + 1, outputDevices[i]);

            menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&monitorBtn),
                [this, outputDevices](int result) {
                    if (result > 0) {
                        monitorDeviceName = outputDevices[result - 1];
                        // Save to settings
                    #if JucePlugin_Build_Standalone
                        if (auto* h = juce::StandalonePluginHolder::getInstance())
                            if (auto* props = h->settings.get())
                                props->setValue("monitorDevice", monitorDeviceName);
                    #endif
                        // Now actually toggle
                        monitorBtn.setToggleState(true, juce::dontSendNotification);
                        toggleMonitor();
                    } else {
                        monitorBtn.setToggleState(false, juce::dontSendNotification);
                    }
                });
            return;
        }

        // Save current output device
        auto config = dm.getAudioDeviceSetup();
        savedOutputDevice = config.outputDeviceName;

        // Switch to monitor device
        config.outputDeviceName = monitorDeviceName;
        dm.setAudioDeviceSetup(config, true);

        monitoredDeviceManager = dmPtr;
        isMonitoring = true;
    } else {
        // Restore original output device
        auto config = dm.getAudioDeviceSetup();
        config.outputDeviceName = savedOutputDevice;
        dm.setAudioDeviceSetup(config, true);

        monitoredDeviceManager = nullptr;
        isMonitoring = false;
    }
    repaint();
#endif
}

// --- Virtual Audio Routing ---

void EasyEffectsAudioProcessorEditor::initVirtualRouter()
{
#if JucePlugin_Build_Standalone
    if (virtualRouter != nullptr)
        return;

    auto* holder = juce::StandalonePluginHolder::getInstance();
    if (holder == nullptr)
        return;

    virtualRouter = std::make_unique<eeval::VirtualAudioRouter>(holder->deviceManager, holder->settings.get());

    const auto error = virtualRouter->restoreSavedMode();
    if (virtualRouter->getMode() != eeval::VirtualAudioRouter::Mode::Off)
        holder->getMuteInputValue().setValue(false);  // virtual routes can't feed back

    if (error.isNotEmpty())
        juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,
                                               "Virtual Audio", error);

    updateVirtualButton();
#endif
}

void EasyEffectsAudioProcessorEditor::applyVirtualMode(eeval::VirtualAudioRouter::Mode mode)
{
#if JucePlugin_Build_Standalone
    if (virtualRouter == nullptr)
        return;

    const auto error = virtualRouter->setMode(mode);

    if (error.isEmpty() && mode != eeval::VirtualAudioRouter::Mode::Off)
        if (auto* holder = juce::StandalonePluginHolder::getInstance())
            holder->getMuteInputValue().setValue(false);

    if (error.isNotEmpty())
        juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,
                                               "Virtual Audio", error);

    updateVirtualButton();
#else
    juce::ignoreUnused(mode);
#endif
}

void EasyEffectsAudioProcessorEditor::updateVirtualButton()
{
    using Mode = eeval::VirtualAudioRouter::Mode;
    const auto mode = virtualRouter != nullptr ? virtualRouter->getMode() : Mode::Off;

    virtualBtn.setToggleState(mode != Mode::Off, juce::dontSendNotification);
    virtualBtn.setButtonText(mode == Mode::SystemAudio ? "Virtual: System"
                           : mode == Mode::Microphone  ? "Virtual: Mic"
                                                       : "Virtual Audio");
    repaint();
}

void EasyEffectsAudioProcessorEditor::showVirtualAudioMenu()
{
#if JucePlugin_Build_Standalone
    if (virtualRouter == nullptr)
        initVirtualRouter();
    if (virtualRouter == nullptr)
        return;

    using Mode = eeval::VirtualAudioRouter::Mode;
    const bool installed = virtualRouter->isDriverInstalled();
    const auto mode = virtualRouter->getMode();

    juce::PopupMenu menu;
    menu.addSectionHeader(installed ? "EasyEffects Virtual Audio"
                                    : "Virtual Audio driver not installed");

    menu.addItem(1, eeval::VirtualAudioRouter::getModeName(Mode::Off), true, mode == Mode::Off);
    menu.addItem(2, eeval::VirtualAudioRouter::getModeName(Mode::SystemAudio), installed, mode == Mode::SystemAudio);
    menu.addItem(3, eeval::VirtualAudioRouter::getModeName(Mode::Microphone), installed, mode == Mode::Microphone);
    menu.addSeparator();

    const auto outputs = virtualRouter->getPhysicalOutputs();
    juce::PopupMenu outputMenu;
    for (int i = 0; i < outputs.size(); ++i)
        outputMenu.addItem(100 + i, outputs[i], true, outputs[i] == virtualRouter->getPhysicalOutput());
    menu.addSubMenu("Speakers / headphones (system mode)", outputMenu, outputs.size() > 0);

    const auto inputs = virtualRouter->getPhysicalInputs();
    juce::PopupMenu inputMenu;
    for (int i = 0; i < inputs.size(); ++i)
        inputMenu.addItem(300 + i, inputs[i], true, inputs[i] == virtualRouter->getPhysicalInput());
    menu.addSubMenu("Microphone (mic mode)", inputMenu, inputs.size() > 0);

    menu.addSeparator();

    // EEVirtualAudioSetup.exe ships next to EasyEffects.exe in release builds.
    const auto setupExe = juce::File::getSpecialLocation(juce::File::currentExecutableFile)
                              .getSiblingFile("EEVirtualAudioSetup.exe");
    if (setupExe.existsAsFile()) {
        if (installed)
            menu.addItem(6, "Uninstall virtual audio driver...");
        else
            menu.addItem(5, "Install virtual audio driver...");
    }
    menu.addItem(4, installed ? "How it works..." : "How to install the driver...");

    juce::Component::SafePointer<EasyEffectsAudioProcessorEditor> safeThis(this);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&virtualBtn),
        [safeThis, outputs, inputs, setupExe](int result) {
            if (safeThis == nullptr || safeThis->virtualRouter == nullptr || result <= 0)
                return;

            auto& self = *safeThis;
            juce::String error;

            if (result == 1)      self.applyVirtualMode(Mode::Off);
            else if (result == 2) self.applyVirtualMode(Mode::SystemAudio);
            else if (result == 3) self.applyVirtualMode(Mode::Microphone);
            else if (result == 4) {
                juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::InfoIcon, "Virtual Audio",
                    juce::String("EasyEffects ships its own virtual audio driver, so no VB-Cable is needed.\n\n")
                    + "Process system audio:\n"
                      "  Windows output -> EasyEffects Speakers -> effects -> your speakers/headphones\n\n"
                    + "Process microphone:\n"
                      "  your mic -> effects -> EasyEffects Microphone (select it in Discord/OBS/Zoom)\n\n"
                    + "Install: run EEVirtualAudioSetup.exe (next to EasyEffects.exe), or use the\n"
                      "Install item in this menu. Windows will ask for administrator permission.\n"
                      "Your previous Windows default devices are restored when you turn this off or close the app.");
            }
            else if (result == 5 || result == 6) {
                // Turn routing off before the driver goes away.
                if (result == 6)
                    self.applyVirtualMode(Mode::Off);
                // The setup exe's manifest requests elevation, so Windows shows a UAC prompt.
                if (!setupExe.startAsProcess(result == 6 ? "/uninstall" : ""))
                    error = "Could not start " + setupExe.getFullPathName();
            }
            else if (result >= 300 && result - 300 < inputs.size())
                error = self.virtualRouter->setPhysicalInput(inputs[result - 300]);
            else if (result >= 100 && result - 100 < outputs.size())
                error = self.virtualRouter->setPhysicalOutput(outputs[result - 100]);

            if (error.isNotEmpty())
                juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, "Virtual Audio", error);

            self.updateVirtualButton();
        });
#endif
}

// --- ListBoxModel ---

int EasyEffectsAudioProcessorEditor::getNumRows() {
    return (int)activeSlots.size();
}

void EasyEffectsAudioProcessorEditor::paintListBoxItem(int, juce::Graphics&, int, int, bool) {}

juce::Component* EasyEffectsAudioProcessorEditor::refreshComponentForRow(int rowNumber, bool isRowSelected, juce::Component* existing) {
    delete existing;

    if (rowNumber < 0 || rowNumber >= (int)activeSlots.size())
        return nullptr;

    const auto& slot = activeSlots[(size_t)rowNumber];

    auto* rowComp = new eeval::ui::SidebarRowCustomComponent(
        proc().parameters,
        slot.slotIndex,
        slot.typeId,
        slot.displayName,
        rowNumber,
        [this](int slotIdx) {
            proc().removeEffect(slotIdx);
            refreshSidebar();
        },
        [this](int slotIdx) {
            proc().moveEffect(slotIdx, -1);
            refreshSidebar();
        },
        [this](int slotIdx) {
            proc().moveEffect(slotIdx, 1);
            refreshSidebar();
        },
        [this](int slotIdx, int delta) {
            // Called from the row's mouseUp; rebuild the list on the next message
            // so the row isn't deleted while it is still handling the event.
            juce::Component::SafePointer<EasyEffectsAudioProcessorEditor> safeThis(this);
            juce::MessageManager::callAsync([safeThis, slotIdx, delta] {
                if (safeThis != nullptr)
                    safeThis->moveEffectBy(slotIdx, delta);
            });
        }
    );

    rowComp->update(isRowSelected);
    return rowComp;
}

void EasyEffectsAudioProcessorEditor::moveEffectBy(int slotIndex, int delta)
{
    // Slots are kept compact (row == slot index), so walk the effect one
    // position at a time in the requested direction.
    const int dir = delta < 0 ? -1 : 1;
    int idx = slotIndex;
    for (int step = 0; step < std::abs(delta); ++step) {
        const int target = idx + dir;
        if (target < 0 || target >= (int)activeSlots.size())
            break;
        proc().moveEffect(idx, dir);
        idx = target;
    }
    proc().setSelectedEditorIndex(idx);
    refreshSidebar();
}

void EasyEffectsAudioProcessorEditor::listBoxItemClicked(int row, const juce::MouseEvent&) {
    proc().setSelectedEditorIndex(row);
    rebuildEditorView();
}

void EasyEffectsAudioProcessorEditor::selectedRowsChanged(int lastRowSelected) {
    if (lastRowSelected >= 0 && lastRowSelected < (int)activeSlots.size()) {
        proc().setSelectedEditorIndex(lastRowSelected);
        rebuildEditorView();
    }
}

void EasyEffectsAudioProcessorEditor::refreshSidebar() {
    activeSlots = proc().getActiveSlots();
    moduleList.updateContent();
    moduleList.repaint();

    int sel = proc().getSelectedEditorIndex();
    if (sel >= (int)activeSlots.size())
        sel = juce::jmax(0, (int)activeSlots.size() - 1);
    proc().setSelectedEditorIndex(sel);
    if (!activeSlots.empty())
        moduleList.selectRow(sel, false, true);

    rebuildEditorView();
}

void EasyEffectsAudioProcessorEditor::rebuildEditorView() {
    int index = proc().getSelectedEditorIndex();
    if (index < 0 || index >= (int)activeSlots.size()) {
        viewport.setViewedComponent(nullptr, false);
        currentEditor.reset();
        repaint();
        return;
    }

    const auto& slot = activeSlots[(size_t)index];

    viewport.setViewedComponent(nullptr, false);

    std::unique_ptr<juce::Component> inner;
    if (slot.typeId == "eq") {
        inner = std::make_unique<eeval::ui::VisualEqualizerEditor>(
            proc(), proc().parameters, slot.slotIndex);
    } else if (slot.typeId == "convolver") {
        inner = std::make_unique<eeval::ui::ConvolverEditor>(
            proc(), slot.slotIndex);
    } else {
        inner = std::make_unique<eeval::ui::GenericModuleEditor>(
            proc().parameters, slot.slotIndex, slot.typeId, slot.displayName);
    }

    // Every effect page gets the EasyEffects Input/Output strip on top.
    currentEditor = std::make_unique<eeval::ui::EffectPage>(proc(), slot.slotIndex, std::move(inner));

    viewport.setViewedComponent(currentEditor.get(), false);
    // The page's sliders were built before being attached to this window, so their
    // text boxes / spin buttons came from the default look-and-feel. Re-send it now
    // that the page inherits our theme, so they get rebuilt with the Breeze styling.
    currentEditor->sendLookAndFeelChange();
    layoutCurrentEditor();
    repaint();
}

void EasyEffectsAudioProcessorEditor::showPresetMenu() {
    auto& pm = proc().getPresetManager();
    juce::PopupMenu menu;

    menu.addItem(1, "Save" + juce::String(pm.hasActivePreset() ? " (" + pm.getActivePresetName() + ")" : ""));
    menu.addItem(2, "Save As...");
    menu.addSeparator();

    auto list = pm.getGlobalPresetList();
    juce::PopupMenu loadMenu;
    for (int i = 0; i < list.size(); ++i)
        loadMenu.addItem(100 + i, list[i]);
    menu.addSubMenu("Load", loadMenu, list.size() > 0);

    juce::PopupMenu deleteMenu;
    for (int i = 0; i < list.size(); ++i)
        deleteMenu.addItem(200 + i, list[i]);
    menu.addSubMenu("Delete", deleteMenu, list.size() > 0);

    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&presetBtn),
        [this, &pm, list](int result) {
            if (result == 1) {
                if (pm.hasActivePreset()) {
                    pm.saveGlobalPreset(pm.getActivePresetName());
                } else {
                    auto* aw = new juce::AlertWindow("Save Preset", "Enter preset name:", juce::MessageBoxIconType::NoIcon);
                    aw->addTextEditor("name", "", "Name:");
                    aw->addButton("Save", 1, juce::KeyPress(juce::KeyPress::returnKey));
                    aw->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
                    aw->enterModalState(true, juce::ModalCallbackFunction::create([this, aw](int res) {
                        if (res == 1) {
                            auto name = aw->getTextEditorContents("name").toStdString();
                            if (!name.empty())
                                proc().getPresetManager().saveGlobalPreset(name);
                        }
                        delete aw;
                    }));
                }
            } else if (result == 2) {
                auto* aw = new juce::AlertWindow("Save Preset As", "Enter new preset name:", juce::MessageBoxIconType::NoIcon);
                aw->addTextEditor("name", "", "Name:");
                aw->addButton("Save", 1, juce::KeyPress(juce::KeyPress::returnKey));
                aw->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
                aw->enterModalState(true, juce::ModalCallbackFunction::create([this, aw](int res) {
                    if (res == 1) {
                        auto name = aw->getTextEditorContents("name").toStdString();
                        if (!name.empty())
                            proc().getPresetManager().saveGlobalPreset(name);
                    }
                    delete aw;
                }));
            } else if (result >= 200 && result < 300) {
                auto name = list[result - 200].toStdString();
                auto opts = juce::MessageBoxOptions()
                    .withIconType(juce::MessageBoxIconType::WarningIcon)
                    .withTitle("Delete Preset")
                    .withMessage("Delete \"" + juce::String(name) + "\"?")
                    .withButton("Delete")
                    .withButton("Cancel");
                juce::AlertWindow::showAsync(opts, [this, name](int r) {
                    if (r == 1)
                        proc().getPresetManager().deleteGlobalPreset(name);
                });
            } else if (result >= 100 && result < 200) {
                pm.loadGlobalPreset(list[result - 100].toStdString());
                refreshSidebar();
            }
        });
}

// --- main menu (top right, like EasyEffects' hamburger menu) ---

void EasyEffectsAudioProcessorEditor::showMainMenu()
{
    juce::PopupMenu menu;
#if JucePlugin_Build_Standalone
    const bool standalone = juce::StandalonePluginHolder::getInstance() != nullptr;
#else
    const bool standalone = false;
#endif
    // Audio engine: Windows Audio (one device per pipeline) or an ASIO driver
    // such as Synchronous Audio Router (one driver runs both pipelines).
    juce::StringArray asioDrivers;
    if (asioHost != nullptr)
        asioDrivers = asioHost->getDriverNames();
    juce::PopupMenu engineMenu;
    engineMenu.addItem(10, "Windows Audio", asioHost != nullptr, !asioEnabled());
    if (!asioDrivers.isEmpty()) {
        engineMenu.addSectionHeader("ASIO");
        for (int i = 0; i < asioDrivers.size(); ++i)
            engineMenu.addItem(100 + i, asioDrivers[i], true, asioEnabled() && asioDrivers[i] == asioDriver);
    }

    menu.addSubMenu("Audio engine", engineMenu, standalone && asioHost != nullptr);
    if (asioEnabled())
    {
        menu.addItem(4, "ASIO control panel (" + asioDriver + ")...");
        if (eeval::sar::isSarDriver(asioDriver))
            menu.addItem(6, "Connect both tabs to the EasyEffects devices");
    }
    else
        menu.addItem(1, "Audio settings...", standalone);
    menu.addItem(2, "Monitor device...", standalone && !asioEnabled());
    menu.addSeparator();
    menu.addItem(3, "About Easy Effects");

    juce::Component::SafePointer<EasyEffectsAudioProcessorEditor> safeThis(this);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&menuBtn),
        [safeThis, asioDrivers](int result) {
            if (safeThis == nullptr) return;
            if (result == 10) {
                if (safeThis->asioEnabled()) safeThis->disableAsio();
            } else if (result >= 100 && result - 100 < asioDrivers.size()) {
                safeThis->enableAsio(asioDrivers[result - 100]);
            } else if (result == 4) {
                if (safeThis->asioHost != nullptr) safeThis->asioHost->showControlPanel();
            } else if (result == 6) {
                safeThis->autoRouteSar(true);
                safeThis->sarDefaultsCountdown = 80;
            } else if (result == 1) {
                safeThis->showAudioSettings();   // for the pipeline on screen
            } else if (result == 2) {
                safeThis->chooseMonitorDevice();
            } else if (result == 3) {
                juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::InfoIcon, "Easy Effects for Windows",
                    "A Windows port of EasyEffects: a real-time chain of audio effects for your\n"
                    "system audio or microphone.\n\n"
                    "Inspired by EasyEffects by Wellington Wallace. Licensed under GPLv3.");
            }
        });
}

void EasyEffectsAudioProcessorEditor::chooseMonitorDevice()
{
#if JucePlugin_Build_Standalone
    auto* holder = juce::StandalonePluginHolder::getInstance();
    if (!holder) return;

    juce::PopupMenu deviceMenu;
    deviceMenu.addSectionHeader("Monitor output device");
    auto* deviceType = holder->deviceManager.getCurrentDeviceTypeObject();
    juce::StringArray outputDevices;
    if (deviceType)
        outputDevices = deviceType->getDeviceNames(false);

    for (int i = 0; i < outputDevices.size(); ++i)
        deviceMenu.addItem(i + 1, outputDevices[i], true, outputDevices[i] == monitorDeviceName);

    juce::Component::SafePointer<EasyEffectsAudioProcessorEditor> safeThis(this);
    deviceMenu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&menuBtn),
        [safeThis, outputDevices](int r) {
            if (safeThis == nullptr || r <= 0) return;
            safeThis->monitorDeviceName = outputDevices[r - 1];
            if (auto* h = juce::StandalonePluginHolder::getInstance())
                if (auto* props = h->settings.get())
                    props->setValue("monitorDevice", safeThis->monitorDeviceName);
        });
#endif
}

void EasyEffectsAudioProcessorEditor::showAddEffectMenu() {
    juce::PopupMenu menu;
    const auto& types = eeval::EffectRegistry::getEffectTypes();

    for (int i = 0; i < (int)types.size(); ++i)
        menu.addItem(i + 1, types[(size_t)i].displayName);

    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&addEffectBtn),
        [this](int result) {
            if (result > 0) {
                const auto& types = eeval::EffectRegistry::getEffectTypes();
                int idx = result - 1;
                if (idx >= 0 && idx < (int)types.size()) {
                    int slotIdx = proc().addEffect(types[(size_t)idx].typeId);
                    if (slotIdx >= 0) {
                        refreshSidebar();
                        int newRow = (int)activeSlots.size() - 1;
                        proc().setSelectedEditorIndex(newRow);
                        moduleList.selectRow(newRow, false, true);
                        rebuildEditorView();
                    }
                }
            }
        });
}
