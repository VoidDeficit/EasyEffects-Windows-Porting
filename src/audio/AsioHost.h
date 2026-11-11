#pragma once

#include <JuceHeader.h>
#include "../PluginProcessor.h"
#include "WindowsAudioInput.h"
#include "WindowsAudioOutput.h"

namespace eeval {

/**
 * Hosts one ASIO driver (typically Synchronous Audio Router on top of the real
 * interface's ASIO driver) and runs BOTH EasyEffects pipelines inside its
 * single callback, each on its own pair of channels:
 *
 *   Output pipeline:  in-pair (e.g. a SAR "Players" endpoint) -> effects -> out-pair (speakers)
 *   Input pipeline:   in-pair (e.g. mic on the interface)     -> effects -> out-pair (a SAR "Discord Mic")
 *
 * ASIO drivers are single-client and SAR's endpoints only exist while a host
 * has it open — so EasyEffects itself is that host, no REAPER needed.
 *
 * Each side of a pipeline can also use Windows Audio (WASAPI) instead of an
 * ASIO channel pair:
 *  - input  = WindowsAudioInput  (e.g. a Windows-only microphone feeding an
 *    ASIO output pair, such as the virtual mic of a custom ASIO driver);
 *  - output = WindowsAudioOutput (e.g. an ASIO input pair, through the effects,
 *    out to the real speakers/headphones Windows knows about).
 *
 * Routes are stored by channel *name*, so they survive SAR endpoints being
 * added/removed (which shifts channel indices).
 */
class AsioHost : private juce::AudioIODeviceCallback,
                 private juce::AudioIODeviceType::Listener
{
public:
    enum PipelineIndex { outputPipeline = 0, inputPipeline = 1 };

    /** Where a pipeline gets its input from. */
    enum class InputSource { AsioPair = 0, WindowsAudio = 1 };
    /** Where a pipeline sends its output to. */
    enum class OutputTarget { AsioPair = 0, WindowsAudio = 1 };

    AsioHost(EasyEffectsAudioProcessor& outputProc, EasyEffectsAudioProcessor& inputProc,
             juce::PropertySet* settings);
    ~AsioHost() override;

    /** Installed ASIO drivers (e.g. "Synchronous Audio Router", "FLOW ASIO Driver"). */
    juce::StringArray getDriverNames();

    /** Opens + starts the driver with every channel. Returns an error message or {}. */
    juce::String open(const juce::String& driverName);
    void close();
    bool isOpen() const noexcept { return device != nullptr; }
    juce::String getDriverName() const { return driverName; }
    double getSampleRate() const;
    int getBufferSize() const;

    /** The driver's own panel (for SAR: hardware interface + endpoints). Reopens afterwards. */
    void showControlPanel();

    // Stereo channel pairs: pair i = channels 2i and 2i+1.
    juce::StringArray getPairNames(bool input) const;
    int  getRoute(int pipeline, bool input) const;          // pair index, -1 = none
    void setRoute(int pipeline, bool input, int pairIndex); // saves too
    juce::String getRouteName(int pipeline, bool input) const;

    /** Pair index whose FIRST channel is named exactly `channelName` (e.g.
        "EasyEffects Speakers 1"), or -1. */
    int findPairByChannelName(bool input, const juce::String& channelName) const;

    /** Windows Audio (WASAPI) capture devices usable as a pipeline input. */
    static juce::StringArray getWindowsInputNames();

    InputSource getInputSource(int pipeline) const;
    juce::String getWindowsInputName(int pipeline) const;

    /** Picks the input source of a pipeline (ASIO pair or Windows Audio mic).
        While the driver runs the capture device is started/stopped immediately.
        Returns an error message or {}. */
    juce::String setInputSource(int pipeline, InputSource source,
                                const juce::String& windowsDevice = {});

    /** Source shown in the UI: the ASIO pair name, or "Windows Audio: <mic>". */
    juce::String getSourceName(int pipeline) const;

    /** Windows Audio (WASAPI) playback devices usable as a pipeline sink. */
    static juce::StringArray getWindowsOutputNames();

    OutputTarget getOutputTarget(int pipeline) const;
    juce::String getWindowsOutputName(int pipeline) const;

    /** Picks the output target of a pipeline (ASIO pair or Windows device).
        While the driver runs the playback device is started/stopped immediately.
        Returns an error message or {}. */
    juce::String setOutputTarget(int pipeline, OutputTarget target,
                                 const juce::String& windowsDevice = {});

    /** Sink shown in the UI: the ASIO pair name, or "Windows: <device>". */
    juce::String getSinkName(int pipeline) const;

    /** True when the pipeline's WASAPI capture/playback device is really open. */
    bool isWindowsInputRunning(int pipeline) const;
    bool isWindowsOutputRunning(int pipeline) const;

    /** Called on the message thread when channels / state change (repaint UI). */
    std::function<void()> onChange;

private:
    // AudioIODeviceCallback
    void audioDeviceIOCallbackWithContext(const float* const* inputs, int numInputs,
                                          float* const* outputs, int numOutputs, int numSamples,
                                          const juce::AudioIODeviceCallbackContext&) override;
    void audioDeviceAboutToStart(juce::AudioIODevice*) override;
    void audioDeviceStopped() override;
    void audioDeviceError(const juce::String& message) override;

    // AudioIODeviceType::Listener (driver reset, e.g. SAR endpoints changed)
    void audioDeviceListChanged() override;

    void resolveRoutesFromNames();
    void saveRoutes();
    static juce::String key(int pipeline, bool input);
    static juce::String sourceKey(int pipeline);
    static juce::String windowsDeviceKey(int pipeline);
    static juce::String sinkKey(int pipeline);
    static juce::String windowsSinkKey(int pipeline);

    void loadInputSources();
    void loadOutputTargets();
    juce::String startWindowsInputs(double sampleRate);
    juce::String startWindowsInput(int pipeline, double sampleRate);
    void stopWindowsInput(int pipeline);
    void stopWindowsInputs();
    juce::String startWindowsOutputs(double sampleRate);
    juce::String startWindowsOutput(int pipeline, double sampleRate);
    void stopWindowsOutput(int pipeline);
    void stopWindowsOutputs();

    EasyEffectsAudioProcessor* procs[2];
    juce::PropertySet* settings;

    std::unique_ptr<juce::AudioIODeviceType> type;
    std::unique_ptr<juce::AudioIODevice> device;
    juce::String driverName;
    juce::StringArray inNames, outNames;

    std::atomic<int> routes[2][2];   // [pipeline][0 = input, 1 = output]
    std::atomic<int> inputSource[2];  // InputSource, per pipeline
    std::atomic<int> outputTarget[2]; // OutputTarget, per pipeline
    juce::String windowsInputName[2];
    juce::String windowsOutputName[2];
    std::unique_ptr<WindowsAudioInput> windowsInput[2];
    std::unique_ptr<WindowsAudioOutput> windowsOutput[2];
    juce::AudioBuffer<float> work[2];
    juce::MidiBuffer midi;
    bool reopening = false;

    // Deferred (callAsync) work checks this so it never touches a destroyed host.
    std::shared_ptr<std::atomic<bool>> alive = std::make_shared<std::atomic<bool>>(true);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AsioHost)
};

} // namespace eeval
