#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "ui/ConvolverEditor.h"
#include "ui/WindowsChrome.h"
#include "audio/WindowsAudioEndpoints.h"
#include "audio/SarAutoSetup.h"

namespace theme = eeval::theme;
namespace icon  = eeval::theme::icon;

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

static juce::String middleDot() { return juce::String::fromUTF8("  \xc2\xb7  "); }

EasyEffectsAudioProcessorEditor::EasyEffectsAudioProcessorEditor(EasyEffectsAudioProcessor& p)
    : AudioProcessorEditor(&p), hostProcessor(p), activeProc(&p)
{
    eeval::ui::chrome::applyTheme(themeSetting());
    customTheme.refreshColours();
    setLookAndFeel(&customTheme);
    // Popup menus and dialogs live in their own windows and use the default look-and-feel.
    juce::LookAndFeel::setDefaultLookAndFeel(&customTheme);
    juce::Desktop::getInstance().addDarkModeSettingListener(this);

    setResizable(true, true);
    setResizeLimits(900, 600, 3200, 2200);

    activeSlots = proc().getActiveSlots();

    // --- Navigation pane ---------------------------------------------------
    nav.onSelect = [this](const juce::String& id) {
        if      (id == "output")   showPage(Page::Output);
        else if (id == "input")    showPage(Page::Input);
        else if (id == "devices")  showPage(Page::Devices);
        else if (id == "settings") showPage(Page::Settings);
    };
    nav.onToggleCompact = [this] {
        navUserCompact = !navUserCompact;
        if (auto* s = settings()) s->setValue("ui.navCompact", navUserCompact);
        resized();
    };
    addAndMakeVisible(nav);
    buildNavigation();

    // --- Command bar --------------------------------------------------------
    presetBtn.getProperties().set("dropdown", true);
    presetBtn.getProperties().set("icon", (int)icon::library);
    presetBtn.setTooltip("Save, load or delete presets for this pipeline");
    presetBtn.onClick = [this] { showPresetMenu(); };
    addAndMakeVisible(presetBtn);

    bypassBtn.getProperties().set("icon", (int)icon::power);
    bypassBtn.setClickingTogglesState(true);
    bypassBtn.setTooltip("Bypass every effect in this pipeline (A/B comparison)");
    bypassBtn.onClick = [this] { proc().setGlobalBypass(bypassBtn.getToggleState()); };
    addAndMakeVisible(bypassBtn);

    monitorBtn.getProperties().set("icon", (int)icon::headphones);
    monitorBtn.setClickingTogglesState(true);
    monitorBtn.setTooltip("Temporarily send the output to your monitor device");
    monitorBtn.onClick = [this] { toggleMonitor(); };
    if (juce::JUCEApplicationBase::isStandaloneApp())
        addAndMakeVisible(monitorBtn);

    // --- Effects card -------------------------------------------------------
    addEffectBtn.getProperties().set("accent", true);
    addEffectBtn.getProperties().set("icon", (int)icon::add);
    addEffectBtn.setTooltip("Add an effect to the end of the chain");
    addEffectBtn.onClick = [this] { showAddEffectMenu(); };
    addAndMakeVisible(addEffectBtn);

    moduleList.setModel(this);
    moduleList.setRowHeight(44);
    moduleList.setColour(juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
    addAndMakeVisible(moduleList);

    // --- Effect page / settings pages --------------------------------------
    viewport.setScrollBarsShown(true, false);
    viewport.setScrollBarThickness(12);
    addAndMakeVisible(viewport);

    settingsViewport.setScrollBarsShown(true, false);
    settingsViewport.setScrollBarThickness(12);
    addChildComponent(settingsViewport);

    // FFT Analyzer
    fftAnalyzer = std::make_unique<eeval::ui::SpectrumAnalyzerEditor>(proc());
    addAndMakeVisible(fftAnalyzer.get());

    // Select first row
    int lastSelected = proc().getSelectedEditorIndex();
    if (lastSelected >= 0 && lastSelected < (int)activeSlots.size())
        moduleList.selectRow(lastSelected, false, true);
    else if (!activeSlots.empty())
        moduleList.selectRow(0, false, true);

    rebuildEditorView();

#if JucePlugin_Build_Standalone
    if (juce::JUCEApplicationBase::isStandaloneApp()) {
        // The standalone window isn't on the desktop yet while the editor is
        // being constructed, so look up the plugin holder on the next message loop.
        juce::Component::SafePointer<EasyEffectsAudioProcessorEditor> safeThis(this);
        juce::MessageManager::callAsync([safeThis] {
            if (safeThis == nullptr) return;
            auto& self = *safeThis;
            if (auto* s = self.settings()) {
                self.monitorDeviceName = s->getValue("monitorDevice", "");
                self.navUserCompact = s->getBoolValue("ui.navCompact", false);
            }
            self.applyAppTheme();          // saved theme + native title bar
            self.initPipelines();
            self.updateVirtualButton();
            self.resized();
        });
    }
#endif

    setSize(1200, 780);
    startTimerHz(10);
}

EasyEffectsAudioProcessorEditor::~EasyEffectsAudioProcessorEditor()
{
    juce::Desktop::getInstance().removeDarkModeSettingListener(this);

    // If monitoring, restore original device before closing
    if (isMonitoring)
        toggleMonitor();

    stopTimer();
    // Drop every UI object that points into a pipeline before the pipelines go.
    settingsViewport.setViewedComponent(nullptr, false);
    devicesPage.reset();
    settingsPage.reset();
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
    juce::LookAndFeel::setDefaultLookAndFeel(nullptr);
    setLookAndFeel(nullptr);
}

// --- Settings / appearance ------------------------------------------------

juce::PropertySet* EasyEffectsAudioProcessorEditor::settings() const
{
#if JucePlugin_Build_Standalone
    if (juce::JUCEApplicationBase::isStandaloneApp())
        if (auto* holder = juce::StandalonePluginHolder::getInstance())
            return holder->settings.get();
#endif
    return nullptr;
}

juce::String EasyEffectsAudioProcessorEditor::themeSetting() const
{
    if (auto* s = settings())
        return s->getValue("ui.theme", "system");
    return themeFallback;
}

void EasyEffectsAudioProcessorEditor::setThemeSetting(const juce::String& v)
{
    if (auto* s = settings()) s->setValue("ui.theme", v);
    themeFallback = v;
    applyAppTheme();
}

void EasyEffectsAudioProcessorEditor::applyAppTheme()
{
    eeval::ui::chrome::applyTheme(themeSetting());
    customTheme.refreshColours();
    sendLookAndFeelChange();
    eeval::ui::chrome::styleTopLevelWindow(*this);

    // Editors read some colours when they are built: rebuild the visible one.
    rebuildEditorView();
    moduleList.updateContent();
    moduleList.repaint();
    if (devicesPage)  devicesPage->refresh();
    if (settingsPage) settingsPage->refresh();
    repaint();
}

void EasyEffectsAudioProcessorEditor::darkModeSettingChanged()
{
    if (themeSetting() == "system")
        applyAppTheme();
}

// --- Navigation -------------------------------------------------------------

void EasyEffectsAudioProcessorEditor::buildNavigation()
{
    std::vector<eeval::ui::NavigationPane::Item> items;
    if (hasPipelineTabs()) {
        items.push_back({ "output", "Output", icon::volume, false, "Effects for what your apps play (system audio)" });
        items.push_back({ "input",  "Input",  icon::microphone, false, "Effects for your microphone" });
        items.push_back({ "devices", "Devices", icon::devices, true, "Virtual audio, devices and audio engine" });
    } else {
        items.push_back({ "input", "Effects", icon::equalizer, false, "The effect chain" });
    }
    items.push_back({ "settings", "Settings", icon::settings, true, "Appearance and about" });
    nav.setItems(std::move(items));

    switch (currentPage) {
        case Page::Output:   nav.setSelected("output");   break;
        case Page::Input:    nav.setSelected("input");    break;
        case Page::Devices:  nav.setSelected("devices");  break;
        case Page::Settings: nav.setSelected("settings"); break;
    }
}

void EasyEffectsAudioProcessorEditor::showPage(Page p)
{
    if (p == Page::Output && !hasPipelineTabs()) p = Page::Input;
    if (p == Page::Devices && !hasPipelineTabs()) p = Page::Settings;

    currentPage = p;
    if (p == Page::Output) switchPipeline(Pipeline::Output);
    if (p == Page::Input)  switchPipeline(Pipeline::Input);

    buildNavigation();   // updates the selection

    const bool pipe = isPipelinePage();
    for (juce::Component* c : { (juce::Component*)&presetBtn, (juce::Component*)&bypassBtn,
                                (juce::Component*)&addEffectBtn, (juce::Component*)&moduleList,
                                (juce::Component*)&viewport })
        c->setVisible(pipe);
    monitorBtn.setVisible(pipe && juce::JUCEApplicationBase::isStandaloneApp());
    if (fftAnalyzer) fftAnalyzer->setVisible(pipe);

    settingsViewport.setVisible(!pipe);
    if (!pipe) {
        if (p == Page::Devices && devicesPage == nullptr)  buildDevicesPage();
        if (p == Page::Settings && settingsPage == nullptr) buildSettingsPage();
        auto* page = p == Page::Devices ? devicesPage.get() : settingsPage.get();
        if (settingsViewport.getViewedComponent() != page) {
            settingsViewport.setViewedComponent(page, false);
            settingsViewport.setViewPosition(0, 0);
        }
        if (page) {
            page->sendLookAndFeelChange();
            page->refresh();
        }
    }

    if (auto* s = settings())
        s->setValue("ui.page", p == Page::Output ? "output" : p == Page::Input ? "input"
                             : p == Page::Devices ? "devices" : "settings");
    resized();
    repaint();
}

void EasyEffectsAudioProcessorEditor::layoutSettingsViewport()
{
    if (auto* page = dynamic_cast<eeval::ui::SettingsPage*>(settingsViewport.getViewedComponent())) {
        const int viewH = settingsViewport.getHeight();
        const int wanted = page->getPreferredHeight();
        const int w = settingsViewport.getWidth() - (wanted > viewH ? settingsViewport.getScrollBarThickness() : 0);
        page->setSize(juce::jmax(0, w), juce::jmax(viewH, wanted));
    }
}

// --- Devices page (replaces the Linux app's PipeWire page) -----------------

bool EasyEffectsAudioProcessorEditor::sarInstalled() const
{
    // Enumerating ASIO drivers reads the registry: cache it for a few seconds.
    static juce::uint32 lastCheck = 0;
    static bool installed = false;
    const auto now = juce::Time::getMillisecondCounter();
    if (asioHost != nullptr && (lastCheck == 0 || now - lastCheck > 3000)) {
        installed = asioHost->getDriverNames().contains(eeval::sar::driverName);
        lastCheck = now;
    }
    return installed;
}

bool EasyEffectsAudioProcessorEditor::sarRunning() const
{
    return asioEnabled() && eeval::sar::isSarDriver(asioDriver) && asioHost->isOpen();
}

void EasyEffectsAudioProcessorEditor::buildDevicesPage()
{
    using Card = eeval::ui::SettingsCard;
    devicesPage = std::make_unique<eeval::ui::SettingsPage>(
        "Devices", "Where EasyEffects takes its audio from and sends it to");
    auto& page = *devicesPage;
    auto* self = this;

    // --- Virtual audio (Synchronous Audio Router) ---------------------------
    if (asioHost != nullptr) {
        page.addSection("Virtual audio");

        page.addCard(icon::volume, "Process system audio", [] {
                return juce::String("Apps play to EasyEffects Speakers, then through the Output effects to your speakers");
            })
            .withToggle([self] { return self->sarRunning() && self->virtualOptionEnabled(true); },
                        [self](bool on) { self->setVirtualOption(true, on); },
                        [self] { return self->sarInstalled(); });

        page.addCard(icon::microphone, "Process microphone", [] {
                return juce::String("Your microphone goes through the Input effects to EasyEffects Microphone");
            })
            .withToggle([self] { return self->sarRunning() && self->virtualOptionEnabled(false); },
                        [self](bool on) { self->setVirtualOption(false, on); },
                        [self] { return self->sarInstalled(); });

        page.addCard(icon::plug, "Synchronous Audio Router", [self] {
                if (!self->sarInstalled())
                    return juce::String("Not installed. It is free, signed and works with Secure Boot on.");
                if (!self->sarRunning())
                    return juce::String("Installed, not in use");
                juce::String s = "Running";
                if (self->asioHost->getBufferSize() > 0)
                    s << middleDot() << self->asioHost->getBufferSize() << " samples";
                if (driverNeedsElevation(self->asioDriver) && !isElevatedCached())
                    s << middleDot() << "needs administrator rights to create its Windows devices";
                return s;
            })
            .withButton([self] {
                    if (!self->sarInstalled()) return juce::String("Download");
                    if (self->sarRunning() && !isElevatedCached()) return juce::String("Restart as administrator");
                    if (self->sarRunning()) return juce::String("Reconnect devices");
                    return juce::String("How it works");
                },
                [self](juce::Component&) {
                    if (!self->sarInstalled()) {
                        juce::URL("https://github.com/eiz/SynchronousAudioRouter/releases").launchInDefaultBrowser();
                    } else if (self->sarRunning() && !isElevatedCached()) {
                        self->restartElevated();
                    } else if (self->sarRunning()) {
                        self->autoRouteSar(true);
                        self->sarDefaultsCountdown = 80;
                    } else {
                        self->showVirtualAudioHelp();
                    }
                    self->updateVirtualButton();
                });

        page.addCard(icon::power, "Turn virtual audio off", [] {
                return juce::String("Go back to Windows Audio and restore your previous default devices");
            })
            .withButton([] { return juce::String("Turn off"); },
                        [self](juce::Component&) { self->disableAsio(); },
                        false, false, [self] { return self->sarRunning(); });
    }

    // --- Devices of each pipeline -------------------------------------------
    auto deviceCard = [&page, self](Pipeline p, bool inputSide, juce::juce_wchar glyph,
                                    const juce::String& title, const juce::String& desc) {
        page.addCard(glyph, title, [desc] { return desc; })
            .withButton([self, p, inputSide] {
                    return inputSide ? self->inputNameFor(p) : self->outputNameFor(p);
                },
                [self, p, inputSide](juce::Component& anchor) {
                    self->showDeviceMenu(p, inputSide, anchor.getScreenBounds());
                },
                true);
    };

    page.addSection("Output");
    deviceCard(Pipeline::Output, true,  icon::apps,     "Players",       "What the Output effects process: the audio your apps play");
    deviceCard(Pipeline::Output, false, icon::speakers, "Output device", "Your speakers or headphones");

    page.addSection("Input");
    deviceCard(Pipeline::Input, true,  icon::microphone, "Input device", "Your microphone");
    deviceCard(Pipeline::Input, false, icon::apps,       "Recorders",    "Where the processed microphone goes (pick it in Discord, OBS, Zoom)");

    // --- Audio engine -------------------------------------------------------
    page.addSection("Audio engine");
    page.addCard(icon::sliders, "Audio engine", [] {
            return juce::String("Windows Audio opens one device per pipeline; an ASIO driver runs both pipelines");
        })
        .withButton([self] { return self->asioEnabled() ? self->asioDriver : juce::String("Windows Audio"); },
                    [self](juce::Component& anchor) { self->showEngineMenu(anchor); },
                    true);

    page.addCard(icon::settings, "Sample rate and buffer size", [self] {
            return self->asioEnabled() ? juce::String("Set in the ASIO driver's control panel")
                                       : juce::String("Audio settings of each pipeline");
        })
        .withButton([self] { return self->asioEnabled() ? juce::String("Open control panel")
                                                         : juce::String("Open"); },
                    [self](juce::Component& anchor) {
                        if (self->asioEnabled()) { self->asioHost->showControlPanel(); return; }
                        juce::PopupMenu m;
                        m.addItem(1, "Output pipeline...");
                        m.addItem(2, "Input pipeline...");
                        juce::Component::SafePointer<EasyEffectsAudioProcessorEditor> safe(self);
                        m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&anchor), [safe](int r) {
                            if (safe == nullptr || r <= 0) return;
                            safe->showAudioSettings(r == 1 ? Pipeline::Output : Pipeline::Input);
                        });
                    });

    page.addCard(icon::headphones, "Monitor device", [self] {
            return self->asioEnabled() ? juce::String("Not available with ASIO: route a pair to your monitor outputs instead")
                                       : juce::String("Where the Monitor button sends the sound");
        })
        .withButton([self] { return self->monitorDeviceName.isNotEmpty() ? self->monitorDeviceName
                                                                          : juce::String("Choose a device"); },
                    [self](juce::Component& anchor) { self->chooseMonitorDevice(&anchor); },
                    true, false, [self] { return !self->asioEnabled(); });

}

// --- Settings page -----------------------------------------------------------

void EasyEffectsAudioProcessorEditor::buildSettingsPage()
{
    settingsPage = std::make_unique<eeval::ui::SettingsPage>("Settings");
    auto& page = *settingsPage;
    auto* self = this;

    page.addSection("Appearance");
    page.addCard(icon::colour, "App theme", [] {
            return juce::String("Follow Windows, or always use the light or dark theme");
        })
        .withButton([self] {
                const auto t = self->themeSetting();
                return t == "light" ? juce::String("Light") : t == "dark" ? juce::String("Dark")
                                                                         : juce::String("Use system setting");
            },
            [self](juce::Component& anchor) {
                const auto t = self->themeSetting();
                juce::PopupMenu m;
                m.addItem(1, "Light", true, t == "light");
                m.addItem(2, "Dark", true, t == "dark");
                m.addItem(3, "Use system setting", true, t != "light" && t != "dark");
                juce::Component::SafePointer<EasyEffectsAudioProcessorEditor> safe(self);
                m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&anchor), [safe](int r) {
                    if (safe == nullptr || r <= 0) return;
                    safe->setThemeSetting(r == 1 ? "light" : r == 2 ? "dark" : "system");
                });
            },
            true);

    page.addCard(icon::nav, "Compact navigation", [] {
            return juce::String("Show only icons in the navigation pane");
        })
        .withToggle([self] { return self->navUserCompact; },
                    [self](bool on) {
                        self->navUserCompact = on;
                        if (auto* s = self->settings()) s->setValue("ui.navCompact", on);
                        self->resized();
                    });

    page.addSection("About");
    page.addCard(icon::info, "EasyEffects for Windows", [] {
            return juce::String("A Windows port of EasyEffects by Wellington Wallace. Licensed under GPLv3.");
        })
        .withButton([] { return juce::String("Original project"); },
                    [](juce::Component&) { juce::URL("https://github.com/wwmm/easyeffects").launchInDefaultBrowser(); });

    page.addCard(icon::openLink, "Synchronous Audio Router", [] {
            return juce::String("Free virtual audio devices used by Devices > Virtual audio");
        })
        .withButton([] { return juce::String("Open website"); },
                    [](juce::Component&) {
                        juce::URL("https://github.com/eiz/SynchronousAudioRouter").launchInDefaultBrowser();
                    });
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

    outputPipeline = std::make_unique<eeval::SecondaryPipeline>(holder->settings.get(), "outputPipeline");

    // ASIO engine: one driver (e.g. Synchronous Audio Router) for both pipelines.
    asioHost = std::make_unique<eeval::AsioHost>(outputPipeline->processor(), hostProcessor,
                                                 holder->settings.get());
    {
        juce::Component::SafePointer<EasyEffectsAudioProcessorEditor> safeThis(this);
        asioHost->onChange = [safeThis] { if (safeThis != nullptr) safeThis->repaint(); };
    }

    // Reopen the page that was shown last time (Input on first run, where the
    // existing chain lives).
    Page initial = Page::Input;
    Pipeline initialPipeline = Pipeline::Input;
    if (auto* props = holder->settings.get()) {
        if (props->getValue("ui.activePipeline", "input") == "output")
            initialPipeline = Pipeline::Output;
        const auto page = props->getValue("ui.page", initialPipeline == Pipeline::Output ? "output" : "input");
        initial = page == "output" ? Page::Output : page == "devices" ? Page::Devices
                : page == "settings" ? Page::Settings : Page::Input;
    }

    switchPipeline(initialPipeline);
    showPage(initial);

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
                "\n\nPick another device under Devices > Input device or Devices > Output device.");
        else
            juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, "ASIO",
                "Couldn't start \"" + driver + "\":\n" + err +
                "\n\nIf this is Synchronous Audio Router, open its control panel "
                "(Devices > Sample rate and buffer size) and choose your audio interface "
                "(e.g. FLOW ASIO Driver) as the hardware.");
    }
    updateVirtualButton();
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

        // getDefaultEndpoint() only reports the console role, so even when that
        // is already ours, set all roles (console, multimedia, communications):
        // Discord/Zoom often follow the *communications* default.
        auto current = getDefaultEndpoint(flow);
        const bool alreadyOurs = current && isOurs(current->name);

        for (const auto& ep : listEndpoints(flow)) {
            if (!isOurs(ep.name)) continue;
            // Remember the user's device so it can be restored later.
            if (current && !alreadyOurs && !props->containsKey(prevKey))
                props->setValue(prevKey, juce::String::fromUTF8(current->id.c_str()));
            setDefaultEndpoint(ep.id);
            return;
        }
        done = false;   // SAR hasn't created this device yet: try again on the next tick
    };

    // Only for the options switched on in Devices > Virtual audio.
    if (virtualOptionEnabled(true))
        switchTo(Flow::Render,  eeval::sar::speakersEndpoint,   "sar.prevRenderId");
    if (virtualOptionEnabled(false))
        switchTo(Flow::Capture, eeval::sar::microphoneEndpoint, "sar.prevCaptureId");

    if (done || --sarDefaultsCountdown <= 0)
        sarDefaultsCountdown = 0;
#endif
}

void EasyEffectsAudioProcessorEditor::restoreWindowsDefaultsFromSar(bool render, bool capture)
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
    if (render)  restore(Flow::Render,  eeval::sar::speakersEndpoint,   "sar.prevRenderId");
    if (capture) restore(Flow::Capture, eeval::sar::microphoneEndpoint, "sar.prevCaptureId");
    if (auto* f = dynamic_cast<juce::PropertiesFile*>(props))
        f->saveIfNeeded();
#else
    juce::ignoreUnused(render, capture);
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
    monitorBtn.setTooltip("Temporarily send the output to your monitor device");
    updateVirtualButton();
#endif
}

void EasyEffectsAudioProcessorEditor::showEngineMenu(juce::Component& anchor)
{
    if (asioHost == nullptr) return;

    // Audio engine: Windows Audio (one device per pipeline) or an ASIO driver
    // such as Synchronous Audio Router (one driver runs both pipelines).
    const auto asioDrivers = asioHost->getDriverNames();
    juce::PopupMenu menu;
    menu.addItem(10, "Windows Audio", true, !asioEnabled());
    if (!asioDrivers.isEmpty()) {
        menu.addSectionHeader("ASIO");
        for (int i = 0; i < asioDrivers.size(); ++i)
            menu.addItem(100 + i, asioDrivers[i], true, asioEnabled() && asioDrivers[i] == asioDriver);
    }

    juce::Component::SafePointer<EasyEffectsAudioProcessorEditor> safeThis(this);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&anchor),
        [safeThis, asioDrivers](int result) {
            if (safeThis == nullptr) return;
            if (result == 10) {
                if (safeThis->asioEnabled()) safeThis->disableAsio();
            } else if (result >= 100 && result - 100 < asioDrivers.size()) {
                safeThis->enableAsio(asioDrivers[result - 100]);
            }
            safeThis->updateVirtualButton();
        });
}

void EasyEffectsAudioProcessorEditor::showPairMenu(Pipeline p, bool inputSide, juce::Rectangle<int> screenArea)
{
    using InputSource  = eeval::AsioHost::InputSource;
    using OutputTarget = eeval::AsioHost::OutputTarget;
    if (asioHost == nullptr) return;

    const int pipe = pipelineIndexFor(p);
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
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetScreenArea(screenArea),
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
            if (safeThis->devicesPage) safeThis->devicesPage->refresh();
            safeThis->repaint();
        });
}

void EasyEffectsAudioProcessorEditor::switchPipeline(Pipeline p)
{
    EasyEffectsAudioProcessor* target = &hostProcessor;
    if (p == Pipeline::Output) {
        if (outputPipeline == nullptr) return;
        target = &outputPipeline->processor();
    }

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
    addChildComponent(fftAnalyzer.get());
    fftAnalyzer->setVisible(isPipelinePage());

    bypassBtn.setToggleState(proc().getGlobalBypass(), juce::dontSendNotification);

    if (auto* props = settings())
        props->setValue("ui.activePipeline", p == Pipeline::Output ? "output" : "input");

    refreshSidebar();   // rebuilds rows + effect page for the new chain
    resized();
    repaint();
}

juce::AudioDeviceManager* EasyEffectsAudioProcessorEditor::deviceManagerFor(Pipeline p) const
{
    if (asioEnabled())
        return nullptr;   // devices are ASIO channel pairs, not Windows Audio devices
    if (p == Pipeline::Output && outputPipeline != nullptr)
        return &outputPipeline->deviceManager();
#if JucePlugin_Build_Standalone
    if (p == Pipeline::Input)
        if (auto* holder = juce::StandalonePluginHolder::getInstance())
            return &holder->deviceManager;
#endif
    return nullptr;
}

void EasyEffectsAudioProcessorEditor::showDeviceMenu(Pipeline p, bool inputSide, juce::Rectangle<int> screenArea)
{
    if (asioEnabled()) { showPairMenu(p, inputSide, screenArea); return; }

    auto* dm = deviceManagerFor(p);
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
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetScreenArea(screenArea),
        [safeThis, dm, names, inputSide, p](int r) {
            if (safeThis == nullptr || r <= 0) return;
            if (r == 2000) { safeThis->showAudioSettings(p); return; }

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
            if (safeThis->devicesPage) safeThis->devicesPage->refresh();
            safeThis->repaint();
        });
}

void EasyEffectsAudioProcessorEditor::showAudioSettings(Pipeline p)
{
    if (asioEnabled()) { asioHost->showControlPanel(); return; }   // rate/buffer live in the driver

    auto* dm = deviceManagerFor(p);
    if (dm == nullptr) return;

    auto* selector = new juce::AudioDeviceSelectorComponent(*dm, 0, 2, 0, 2, false, false, true, false);
    selector->setSize(520, 440);

    juce::DialogWindow::LaunchOptions o;
    o.content.setOwned(selector);
    o.dialogTitle = (p == Pipeline::Output && hasPipelineTabs())
                        ? "Output pipeline - audio settings" : "Input pipeline - audio settings";
    o.dialogBackgroundColour = theme::bgBase;
    o.escapeKeyTriggersCloseButton = true;
    o.useNativeTitleBar = true;
    o.resizable = false;
    o.launchAsync();
}

void EasyEffectsAudioProcessorEditor::mouseDown(const juce::MouseEvent& e)
{
    if (!isPipelinePage()) return;
    if (sourceRowArea.contains(e.getPosition()))
        showDeviceMenu(activePipeline, true, localAreaToGlobal(sourceRowArea));
    else if (sinkRowArea.contains(e.getPosition()))
        showDeviceMenu(activePipeline, false, localAreaToGlobal(sinkRowArea));
}

void EasyEffectsAudioProcessorEditor::mouseMove(const juce::MouseEvent& e)
{
    const bool clickable = isPipelinePage() && (activeDeviceManager() != nullptr || asioEnabled());
    const int hot = !clickable ? 0
                  : sourceRowArea.contains(e.getPosition()) ? 1
                  : sinkRowArea.contains(e.getPosition())   ? 2 : 0;
    if (hot != hoveredEndRow) {
        hoveredEndRow = hot;
        repaint(sourceRowArea);
        repaint(sinkRowArea);
    }
}

void EasyEffectsAudioProcessorEditor::mouseExit(const juce::MouseEvent&)
{
    if (hoveredEndRow != 0) {
        hoveredEndRow = 0;
        repaint(sourceRowArea);
        repaint(sinkRowArea);
    }
}

void EasyEffectsAudioProcessorEditor::timerCallback()
{
    // Preset drop-down shows the active preset
    auto& pm = proc().getPresetManager();
    juce::String label = pm.hasActivePreset() ? juce::String(pm.getActivePresetName()) : juce::String("No preset");
    if (pm.hasActivePreset() && pm.isDirty()) label += " *";
    if (presetBtn.getButtonText() != label)
        presetBtn.setButtonText(label);

    // Sync bypass button
    bypassBtn.setToggleState(proc().getGlobalBypass(), juce::dontSendNotification);

#if JucePlugin_Build_Standalone
    // While ASIO runs both pipelines, the Windows Audio devices must stay closed
    // (e.g. if something re-opened one via the audio settings dialog).
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

    if (isPipelinePage()) {
        // Rows read their bypass state live; device names can change in the audio settings.
        moduleList.repaint();
        repaint(sourceRowArea);
        repaint(sinkRowArea);
        repaint(headerArea);
    } else if (currentPage == Page::Devices && devicesPage != nullptr && saveCountdown % 5 == 0) {
        devicesPage->refresh();
    }
}

juce::String EasyEffectsAudioProcessorEditor::inputNameFor(Pipeline p) const
{
    if (asioEnabled())
        return asioHost->getSourceName(pipelineIndexFor(p));
    if (auto* dm = deviceManagerFor(p)) {
        auto name = dm->getAudioDeviceSetup().inputDeviceName;
        return name.isNotEmpty() ? name : juce::String("None");
    }
    return "Host input";
}

juce::String EasyEffectsAudioProcessorEditor::outputNameFor(Pipeline p) const
{
    if (asioEnabled())
        return asioHost->getSinkName(pipelineIndexFor(p));
    if (auto* dm = deviceManagerFor(p)) {
        auto name = dm->getAudioDeviceSetup().outputDeviceName;
        return name.isNotEmpty() ? name : juce::String("None");
    }
    return "Host output";
}

juce::String EasyEffectsAudioProcessorEditor::engineStatus() const
{
    double sr = proc().getSampleRate();
    if (auto* dm = activeDeviceManager())
        if (auto* device = dm->getCurrentAudioDevice())
            sr = device->getCurrentSampleRate();
    if (asioEnabled() && asioHost->getSampleRate() > 0.0)
        sr = asioHost->getSampleRate();

    juce::String s;
    if (sr > 0.0)
        s << juce::String(sr / 1000.0, 1) << " kHz" << middleDot();

    if (asioEnabled()) {
        s << "ASIO: " << asioDriver;
        if (asioHost->isOpen() && asioHost->getBufferSize() > 0)
            s << " (" << asioHost->getBufferSize() << " samples)";
        else
            s << " (not running)";
        if (driverNeedsElevation(asioDriver) && !isElevatedCached())
            s << middleDot() << "not administrator: virtual devices unavailable";
    } else if (juce::JUCEApplicationBase::isStandaloneApp()) {
        s << "Windows Audio";
    } else {
        s << "Plug-in";
    }

    if (isMonitoring)
        s << middleDot() << "Monitoring on " << monitorDeviceName;
    return s;
}

juce::String EasyEffectsAudioProcessorEditor::pageSubtitle() const
{
    juce::String what;
    if (!hasPipelineTabs())
        what = juce::JUCEApplicationBase::isStandaloneApp() ? "Your audio" : "Audio from the host";
    else
        what = activePipeline == Pipeline::Output ? "System audio: what your apps play" : "Your microphone";
    return what + middleDot() + engineStatus();
}

void EasyEffectsAudioProcessorEditor::paint(juce::Graphics& g)
{
    g.fillAll(theme::windowBg);

    // --- Content layer (rounded top-left corner, like WinUI NavigationView) ---
    {
        auto l = layerArea.toFloat();
        juce::Path layer;
        layer.addRoundedRectangle(l.getX(), l.getY(), l.getWidth() + theme::cardRadius, l.getHeight() + theme::cardRadius,
                                  theme::cardRadius, theme::cardRadius, true, false, false, false);
        g.setColour(theme::bgBase);
        g.fillPath(layer);
        g.setColour(theme::borderSubtle);
        g.strokePath(layer, juce::PathStrokeType(1.0f));
    }

    if (!isPipelinePage())
        return;

    // --- Page header -----------------------------------------------------------
    {
        auto h = headerArea;
        const juce::String title = !hasPipelineTabs() ? "Effects"
                                 : activePipeline == Pipeline::Output ? "Output" : "Input";
        g.setColour(theme::textPrimary);
        g.setFont(theme::title());
        g.drawText(title, h.removeFromTop(40), juce::Justification::centredLeft, true);
        g.setColour(isMonitoring ? theme::accentText : theme::textSecondary);
        g.setFont(theme::body());
        g.drawText(pageSubtitle(), h.removeFromTop(22), juce::Justification::centredLeft, true);
    }

    // --- Spectrum card -----------------------------------------------------
    theme::fillCard(g, spectrumCard.toFloat());

    // --- Effects card --------------------------------------------------------
    theme::fillCard(g, effectsCard.toFloat());
    {
        auto h = effectsHeader.reduced(16, 0);
        g.setColour(theme::textPrimary);
        g.setFont(theme::bodyStrong());
        g.drawText("Effects", h, juce::Justification::centredLeft, false);

        int active = 0;
        for (auto& s : activeSlots) {
            auto* prm = proc().parameters.getRawParameterValue("slot" + std::to_string(s.slotIndex) + ".bypass");
            if (prm == nullptr || prm->load() < 0.5f) ++active;
        }
        g.setColour(theme::textTertiary);
        g.setFont(theme::caption());
        const int labelW = juce::roundToInt(juce::GlyphArrangement::getStringWidth(theme::bodyStrong(), "Effects")) + 10;
        if (!activeSlots.empty())
            g.drawText(juce::String(active) + " of " + juce::String((int)activeSlots.size()) + " on",
                       h.withTrimmedLeft(labelW), juce::Justification::centredLeft, false);
    }

    // Source / sink ends of the chain. Clickable: they open a device menu.
    const bool clickable = activeDeviceManager() != nullptr || asioEnabled();
    auto drawEndRow = [&g, clickable](juce::Rectangle<int> r, juce::juce_wchar glyph, const juce::String& title,
                                      const juce::String& device, bool hovered) {
        if (r.isEmpty()) return;
        if (hovered && clickable) {
            g.setColour(theme::subtleFill);
            g.fillRoundedRectangle(r.reduced(4, 4).toFloat(), theme::radius);
        }
        auto row = r.reduced(16, 0);
        theme::drawIcon(g, glyph, row.removeFromLeft(20).toFloat(), theme::textSecondary, 16.0f);
        row.removeFromLeft(14);
        if (clickable) {
            theme::drawIcon(g, icon::chevronDown, row.removeFromRight(16).toFloat(),
                            hovered ? theme::textPrimary : theme::textSecondary, 10.0f);
            row.removeFromRight(8);
        }
        auto block = row.withSizeKeepingCentre(row.getWidth(), 36);
        g.setColour(theme::textSecondary);
        g.setFont(theme::caption());
        g.drawText(title, block.removeFromTop(16), juce::Justification::centredLeft, true);
        g.setColour(theme::textPrimary);
        g.setFont(theme::body());
        g.drawText(device, block, juce::Justification::centredLeft, true);
    };

    // Same wording as EasyEffects: the Output pipeline is fed by the players
    // (apps) and ends in the output device; the Input pipeline starts at the
    // input device and ends in the recorders (Discord, OBS, ...).
    const bool outputTabActive = hasPipelineTabs() && activePipeline == Pipeline::Output;
    drawEndRow(sourceRowArea, outputTabActive ? icon::apps : icon::microphone,
               outputTabActive ? "Players" : "Input device", currentInputDeviceName(), hoveredEndRow == 1);
    drawEndRow(sinkRowArea, outputTabActive ? icon::speakers : icon::apps,
               outputTabActive ? "Output device" : (hasPipelineTabs() ? "Recorders" : "Output device"),
               currentOutputDeviceName(), hoveredEndRow == 2);

    g.setColour(theme::divider);
    if (!sourceRowArea.isEmpty()) g.fillRect(sourceRowArea.getX() + 1, sourceRowArea.getBottom(), sourceRowArea.getWidth() - 2, 1);
    if (!sinkRowArea.isEmpty())   g.fillRect(sinkRowArea.getX() + 1, sinkRowArea.getY() - 1, sinkRowArea.getWidth() - 2, 1);
    if (!effectsHeader.isEmpty()) g.fillRect(effectsHeader.getX() + 1, effectsHeader.getBottom(), effectsHeader.getWidth() - 2, 1);

    // --- Empty state -----------------------------------------------------------
    if (currentEditor == nullptr && !contentArea.isEmpty()) {
        auto c = contentArea.withSizeKeepingCentre(juce::jmin(contentArea.getWidth(), 420), 140);
        theme::drawIcon(g, icon::equalizer, c.removeFromTop(56).toFloat(), theme::textTertiary, 40.0f);
        g.setColour(theme::textPrimary);
        g.setFont(theme::subtitle());
        g.drawText("No effects yet", c.removeFromTop(32), juce::Justification::centred, false);
        g.setColour(theme::textSecondary);
        g.setFont(theme::body());
        g.drawText("Select Add to build your chain", c.removeFromTop(22), juce::Justification::centred, false);
    }
}

void EasyEffectsAudioProcessorEditor::resized()
{
    auto area = getLocalBounds();

    // --- Navigation pane -------------------------------------------------------
    const bool compact = navUserCompact || getWidth() < 1000;
    nav.setCompact(compact);
    nav.setBounds(area.removeFromLeft(compact ? eeval::ui::NavigationPane::compactWidth
                                              : eeval::ui::NavigationPane::expandedWidth));
    layerArea = area;

    // --- Devices / Settings pages ------------------------------------------
    settingsViewport.setBounds(layerArea.withTrimmedLeft(1).withTrimmedTop(1));
    layoutSettingsViewport();

    // --- Pipeline page ----------------------------------------------------------
    auto page = layerArea.reduced(pagePadding, 0);
    page.removeFromTop(16);
    page.removeFromBottom(pagePadding);

    headerArea = page.removeFromTop(headerHeight - 14);
    {
        auto bar = headerArea.withHeight(40).withSizeKeepingCentre(headerArea.getWidth(), 32);
        bar = bar.withY(headerArea.getY() + 4);
        if (monitorBtn.isVisible() || juce::JUCEApplicationBase::isStandaloneApp()) {
            monitorBtn.setBounds(bar.removeFromRight(124));
            bar.removeFromRight(8);
        }
        bypassBtn.setBounds(bar.removeFromRight(118));
        bar.removeFromRight(8);
        presetBtn.setBounds(bar.removeFromRight(juce::jlimit(140, 240, bar.getWidth() - 160)));
    }
    page.removeFromTop(14);

    spectrumCard = page.removeFromTop(fftHeight);
    if (fftAnalyzer)
        fftAnalyzer->setBounds(spectrumCard.reduced(10, 8));
    page.removeFromTop(16);

    effectsCard = page.removeFromLeft(juce::jmin(effectsCardWidth, page.getWidth() / 3 + 40));
    {
        auto inner = effectsCard.reduced(1);
        effectsHeader = inner.removeFromTop(effectsHeaderHeight);
        addEffectBtn.setBounds(effectsHeader.reduced(12, 0).removeFromRight(84).withSizeKeepingCentre(84, 32));
        sourceRowArea = inner.removeFromTop(endRowHeight);
        sinkRowArea = inner.removeFromBottom(endRowHeight);
        moduleList.setBounds(inner.reduced(0, 4));
    }

    page.removeFromLeft(6);   // effect editors carry their own padding
    contentArea = page;
    viewport.setBounds(page.withTrimmedTop(-10));
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
            menu.addSectionHeader("Choose a monitor output device");

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
                        if (auto* props = settings())
                            props->setValue("monitorDevice", monitorDeviceName);
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
    monitorBtn.setToggleState(isMonitoring, juce::dontSendNotification);
    repaint();
#endif
}

// --- Virtual Audio (Synchronous Audio Router) ------------------------------
//
// Devices > Virtual audio: EasyEffects' own Windows devices, provided by
// Synchronous Audio Router (SAR) while EasyEffects hosts it.
//   Process system audio  -> Windows default playback  = "EasyEffects Speakers"
//   Process microphone    -> Windows default recording = "EasyEffects Microphone"
// Turning an option off hands that default back to the user's previous device.

bool EasyEffectsAudioProcessorEditor::virtualOptionEnabled(bool output) const
{
    if (auto* props = settings())
        return props->getBoolValue(output ? "virtual.output" : "virtual.input", true);
    juce::ignoreUnused(output);
    return false;
}

void EasyEffectsAudioProcessorEditor::setVirtualOption(bool output, bool enabled)
{
#if JucePlugin_Build_Standalone
    auto* props = settings();
    if (props == nullptr || asioHost == nullptr)
        return;
    props->setValue(output ? "virtual.output" : "virtual.input", enabled);

    if (enabled) {
        // Needs SAR as the engine; switching to it also creates + wires the devices.
        if (!(asioEnabled() && eeval::sar::isSarDriver(asioDriver)))
            enableAsio(eeval::sar::driverName);
        else
            sarDefaultsCountdown = 80;   // (re)apply the Windows default for this option
    } else {
        restoreWindowsDefaultsFromSar(output, !output);
    }
    updateVirtualButton();
#else
    juce::ignoreUnused(output, enabled);
#endif
}

void EasyEffectsAudioProcessorEditor::updateVirtualButton()
{
    if (devicesPage) devicesPage->refresh();
    repaint();
}

void EasyEffectsAudioProcessorEditor::showVirtualAudioHelp()
{
    juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::InfoIcon, "Virtual audio",
        "EasyEffects creates its own Windows devices through Synchronous Audio Router (SAR):\n\n"
        "Process system audio:\n"
        "  apps -> EasyEffects Speakers -> Output effects -> your speakers/headphones\n\n"
        "Process microphone:\n"
        "  your mic -> Input effects -> EasyEffects Microphone (pick it in Discord/OBS/Zoom)\n\n"
        "The devices exist while EasyEffects is running. SAR needs EasyEffects to run as\n"
        "administrator (it offers to restart that way). Your previous Windows default devices\n"
        "are restored when you turn an option off or close EasyEffects.");
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
        },
        [this](int slotIdx) { resetEffect(slotIdx); }
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
    {
        const int slotIdx = slot.slotIndex;
        currentEditor->setOnReset([this, slotIdx] { resetEffect(slotIdx); });
    }

    viewport.setViewedComponent(currentEditor.get(), false);
    // The page's sliders were built before being attached to this window, so their
    // text boxes / spin buttons came from the default look-and-feel. Re-send it now
    // that the page inherits our theme, so they get rebuilt with the Fluent styling.
    currentEditor->sendLookAndFeelChange();
    layoutCurrentEditor();
    repaint();
}

void EasyEffectsAudioProcessorEditor::showPresetMenu() {
    auto& pm = proc().getPresetManager();
    juce::PopupMenu menu;

    menu.addItem(1, "Save" + juce::String(pm.hasActivePreset() ? " (" + pm.getActivePresetName() + ")" : ""));
    menu.addItem(2, "Save as...");
    menu.addSeparator();

    auto list = pm.getGlobalPresetList();
    if (!list.isEmpty())
        menu.addSectionHeader("Load");
    for (int i = 0; i < list.size(); ++i)
        menu.addItem(100 + i, list[i], true, pm.hasActivePreset() && list[i] == juce::String(pm.getActivePresetName()));

    juce::PopupMenu deleteMenu;
    for (int i = 0; i < list.size(); ++i)
        deleteMenu.addItem(200 + i, list[i]);
    menu.addSeparator();
    menu.addSubMenu("Delete", deleteMenu, list.size() > 0);

    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&presetBtn),
        [this, &pm, list](int result) {
            auto askName = [this](const juce::String& title) {
                auto* aw = new juce::AlertWindow(title, "Enter a name for the preset:", juce::MessageBoxIconType::NoIcon);
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
            };

            if (result == 1) {
                if (pm.hasActivePreset())
                    pm.saveGlobalPreset(pm.getActivePresetName());
                else
                    askName("Save preset");
            } else if (result == 2) {
                askName("Save preset as");
            } else if (result >= 200 && result < 300) {
                auto name = list[result - 200].toStdString();
                auto opts = juce::MessageBoxOptions()
                    .withIconType(juce::MessageBoxIconType::WarningIcon)
                    .withTitle("Delete preset")
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

void EasyEffectsAudioProcessorEditor::chooseMonitorDevice(juce::Component* anchor)
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
    deviceMenu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(anchor != nullptr ? anchor : &monitorBtn),
        [safeThis, outputDevices](int r) {
            if (safeThis == nullptr || r <= 0) return;
            safeThis->monitorDeviceName = outputDevices[r - 1];
            if (auto* props = safeThis->settings())
                props->setValue("monitorDevice", safeThis->monitorDeviceName);
            if (safeThis->devicesPage) safeThis->devicesPage->refresh();
        });
#else
    juce::ignoreUnused(anchor);
#endif
}

void EasyEffectsAudioProcessorEditor::showAddEffectMenu() {
    juce::PopupMenu menu;
    const auto& types = eeval::EffectRegistry::getEffectTypes();

    // Alphabetical, like the Linux app's effect picker.
    std::vector<int> order((size_t)types.size());
    for (int i = 0; i < (int)types.size(); ++i) order[(size_t)i] = i;
    std::sort(order.begin(), order.end(), [&types](int a, int b) {
        return juce::String(types[(size_t)a].displayName).compareNatural(types[(size_t)b].displayName) < 0;
    });

    menu.addSectionHeader("Add effect");
    for (int i : order)
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

void EasyEffectsAudioProcessorEditor::resetEffect(int slotIndex)
{
    proc().resetEffect(slotIndex);

    // Rebuild the open page on the next message: the request may come from a
    // button inside it, and editors such as the EQ cache node positions.
    juce::Component::SafePointer<EasyEffectsAudioProcessorEditor> safeThis(this);
    juce::MessageManager::callAsync([safeThis] {
        if (safeThis != nullptr) {
            safeThis->rebuildEditorView();
            safeThis->moduleList.repaint();
        }
    });
}
