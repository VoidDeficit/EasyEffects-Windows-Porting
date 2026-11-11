#include "AsioHost.h"

namespace eeval {

// Small trace of ASIO open/close steps in %APPDATA%\EasyEffects\asio.log, so a
// driver that blocks (e.g. on shutdown) can be pinpointed after the fact.
static void asioTrace(const juce::String& msg)
{
    auto f = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                 .getChildFile("EasyEffects").getChildFile("asio.log");
    if (f.getSize() > 256 * 1024)
        f.deleteFile();
    f.appendText(juce::Time::getCurrentTime().toString(true, true, true, true) + "  " + msg + "\n");
}

AsioHost::AsioHost(EasyEffectsAudioProcessor& outputProc, EasyEffectsAudioProcessor& inputProc,
                   juce::PropertySet* s)
    : settings(s)
{
    procs[outputPipeline] = &outputProc;
    procs[inputPipeline]  = &inputProc;

    for (auto& p : routes)
        for (auto& r : p)
            r.store(-1);

    for (auto& source : inputSource)
        source.store((int) InputSource::AsioPair);
    for (auto& target : outputTarget)
        target.store((int) OutputTarget::AsioPair);

    loadInputSources();
    loadOutputTargets();

#if JUCE_ASIO
    type.reset(juce::AudioIODeviceType::createAudioIODeviceType_ASIO());
    if (type != nullptr) {
        type->addListener(this);
        type->scanForDevices();
    }
#endif
}

AsioHost::~AsioHost()
{
    alive->store(false);
    close();
    if (type != nullptr)
        type->removeListener(this);
}

juce::StringArray AsioHost::getDriverNames()
{
    if (type == nullptr) return {};
    type->scanForDevices();
    return type->getDeviceNames(false);
}

juce::String AsioHost::open(const juce::String& name)
{
    close();
    if (type == nullptr)
        return "ASIO support is not available in this build.";

    asioTrace("open " + name + "...");
    device.reset(type->createDevice(name, name));
    if (device == nullptr) {
        asioTrace("open: could not load driver");
        return "Could not load the ASIO driver \"" + name + "\".";
    }

    driverName = name;
    inNames  = device->getInputChannelNames();
    outNames = device->getOutputChannelNames();

    juce::BigInteger inMask, outMask;
    inMask.setRange(0, inNames.size(), true);
    outMask.setRange(0, outNames.size(), true);

    // Candidate rates: the driver's current rate first (no clock change), then
    // the common ones. SAR can report a rate its hardware (e.g. a FLOW 8 that
    // is clocked at 44.1 kHz) can't start at, so fall back instead of failing.
    juce::Array<double> candidates;
    if (device->getCurrentSampleRate() > 0.0)
        candidates.addIfNotAlreadyThere(device->getCurrentSampleRate());
    for (double r : { 48000.0, 44100.0, 96000.0, 88200.0 })
        candidates.addIfNotAlreadyThere(r);
    for (double r : device->getAvailableSampleRates())
        candidates.addIfNotAlreadyThere(r);

    juce::String err;
    for (double sr : candidates) {
        err = device->open(inMask, outMask, sr, device->getDefaultBufferSize());
        asioTrace("open: " + juce::String(inNames.size()) + " in / " + juce::String(outNames.size())
                  + " out channels, " + juce::String(sr) + " Hz -> " + (err.isEmpty() ? juce::String("ok") : err));
        if (err.isEmpty())
            break;
        device->close();
    }

    if (err.isNotEmpty()) {
        // Keep the device object: SAR can still show its control panel so the
        // user can pick the hardware interface and fix the problem.
        return err;
    }

    resolveRoutesFromNames();

    // The rate the driver actually opened at (after the fallback loop above).
    const double sr = device->getCurrentSampleRate();

    // The pipelines always run stereo in this engine. Doing this on the message
    // thread guarantees a proper 2-channel DSP setup even if the standalone
    // holder's device was closed (which may leave a zero-channel play config).
    for (auto* proc : procs)
        proc->setPlayConfigDetails(2, 2, sr, device->getDefaultBufferSize());

    // Start the Windows Audio devices while the ASIO driver is up but before
    // its callback runs, so the FIFOs are already primed.
    const auto inputError  = startWindowsInputs(device->getCurrentSampleRate());
    const auto outputError = startWindowsOutputs(device->getCurrentSampleRate());
    device->start(this);

    if (settings != nullptr)
        settings->setValue("asio.driver", driverName);
    if (onChange) onChange();

    // The driver runs even if a Windows Audio device failed; report that
    // problem without pretending the whole ASIO engine failed.
    juce::String warning;
    if (inputError.isNotEmpty())
        warning << "Could not open the Windows Audio input:\n" << inputError;
    if (outputError.isNotEmpty())
        warning << (warning.isNotEmpty() ? "\n\n" : "") << "Could not open the Windows Audio output:\n" << outputError;
    if (warning.isNotEmpty())
        return "The ASIO driver is running, but a Windows Audio device could not be opened:\n\n" + warning;
    return {};
}

void AsioHost::close()
{
    if (device != nullptr) {
        asioTrace("close " + driverName + ": stop...");
        device->stop();
        // The ASIO callback is stopped now: safe to tear the Windows devices down.
        stopWindowsInputs();
        stopWindowsOutputs();
        asioTrace("close: stopped, closing...");
        device->close();
        asioTrace("close: closed, releasing driver...");
        device.reset();
        asioTrace("close: driver released");
    } else {
        stopWindowsInputs();
        stopWindowsOutputs();
    }
    inNames.clear();
    outNames.clear();
}

double AsioHost::getSampleRate() const
{
    return device != nullptr ? device->getCurrentSampleRate() : 0.0;
}

int AsioHost::getBufferSize() const
{
    return device != nullptr ? device->getCurrentBufferSizeSamples() : 0;
}

void AsioHost::showControlPanel()
{
    if (device == nullptr && driverName.isNotEmpty())
        open(driverName);
    if (device == nullptr)
        return;

    device->showControlPanel();

    // Endpoints/hardware may have changed: reopen with the new channel list.
    const auto name = driverName;
    auto token = alive;
    juce::MessageManager::callAsync([this, name, token] {
        if (token->load())
            open(name);
    });
}

juce::StringArray AsioHost::getPairNames(bool input) const
{
    const auto& names = input ? inNames : outNames;
    juce::StringArray pairs;
    for (int i = 0; i < names.size(); i += 2) {
        if (i + 1 < names.size())
            pairs.add(names[i] + "  +  " + names[i + 1]);
        else
            pairs.add(names[i]);
    }
    return pairs;
}

int AsioHost::getRoute(int pipeline, bool input) const
{
    return routes[pipeline][input ? 0 : 1].load();
}

juce::String AsioHost::getRouteName(int pipeline, bool input) const
{
    const int pair = getRoute(pipeline, input);
    const auto pairs = getPairNames(input);
    return (pair >= 0 && pair < pairs.size()) ? pairs[pair] : juce::String("None");
}

void AsioHost::setRoute(int pipeline, bool input, int pairIndex)
{
    routes[pipeline][input ? 0 : 1].store(pairIndex);
    saveRoutes();
    if (onChange) onChange();
}

int AsioHost::findPairByChannelName(bool input, const juce::String& channelName) const
{
    const auto& names = input ? inNames : outNames;
    for (int i = 0; i < names.size(); i += 2)
        if (names[i] == channelName)
            return i / 2;
    return -1;
}

juce::String AsioHost::key(int pipeline, bool input)
{
    return juce::String("asio.") + (pipeline == outputPipeline ? "output" : "input")
         + (input ? ".in" : ".out");
}

juce::String AsioHost::sourceKey(int pipeline)
{
    return juce::String("asio.") + (pipeline == outputPipeline ? "output" : "input") + ".inSource";
}

juce::String AsioHost::windowsDeviceKey(int pipeline)
{
    return juce::String("asio.") + (pipeline == outputPipeline ? "output" : "input") + ".winIn";
}

juce::String AsioHost::sinkKey(int pipeline)
{
    return juce::String("asio.") + (pipeline == outputPipeline ? "output" : "input") + ".outTarget";
}

juce::String AsioHost::windowsSinkKey(int pipeline)
{
    return juce::String("asio.") + (pipeline == outputPipeline ? "output" : "input") + ".winOut";
}

juce::StringArray AsioHost::getWindowsInputNames()
{
    return WindowsAudioInput::getDeviceNames();
}

AsioHost::InputSource AsioHost::getInputSource(int pipeline) const
{
    if (pipeline < 0 || pipeline > 1)
        return InputSource::AsioPair;
    return (InputSource) inputSource[pipeline].load();
}

juce::String AsioHost::getWindowsInputName(int pipeline) const
{
    return (pipeline >= 0 && pipeline <= 1) ? windowsInputName[pipeline] : juce::String();
}

juce::String AsioHost::getSourceName(int pipeline) const
{
    if (getInputSource(pipeline) == InputSource::WindowsAudio)
        return windowsInputName[pipeline].isNotEmpty()
                   ? "Windows: " + windowsInputName[pipeline]
                   : juce::String("Windows Audio (no device)");

    return getRouteName(pipeline, true);
}

juce::StringArray AsioHost::getWindowsOutputNames()
{
    return WindowsAudioOutput::getDeviceNames();
}

AsioHost::OutputTarget AsioHost::getOutputTarget(int pipeline) const
{
    if (pipeline < 0 || pipeline > 1)
        return OutputTarget::AsioPair;
    return (OutputTarget) outputTarget[pipeline].load();
}

juce::String AsioHost::getWindowsOutputName(int pipeline) const
{
    return (pipeline >= 0 && pipeline <= 1) ? windowsOutputName[pipeline] : juce::String();
}

juce::String AsioHost::getSinkName(int pipeline) const
{
    if (getOutputTarget(pipeline) == OutputTarget::WindowsAudio)
        return windowsOutputName[pipeline].isNotEmpty()
                   ? "Windows: " + windowsOutputName[pipeline]
                   : juce::String("Windows Audio (no device)");

    return getRouteName(pipeline, false);
}

bool AsioHost::isWindowsInputRunning(int pipeline) const
{
    return pipeline >= 0 && pipeline <= 1
        && windowsInput[pipeline] != nullptr
        && windowsInput[pipeline]->isRunning();
}

bool AsioHost::isWindowsOutputRunning(int pipeline) const
{
    return pipeline >= 0 && pipeline <= 1
        && windowsOutput[pipeline] != nullptr
        && windowsOutput[pipeline]->isRunning();
}

juce::String AsioHost::setOutputTarget(int pipeline, OutputTarget target,
                                       const juce::String& windowsDevice)
{
    if (pipeline < 0 || pipeline > 1)
        return {};

    windowsOutputName[pipeline] = target == OutputTarget::WindowsAudio ? windowsDevice
                                                                      : juce::String();
    outputTarget[pipeline].store((int) target);

    juce::String error;
    if (device != nullptr) {
        if (target == OutputTarget::WindowsAudio)
            error = startWindowsOutput(pipeline, getSampleRate());
        else
            stopWindowsOutput(pipeline);
    }

    saveRoutes();
    if (onChange) onChange();
    return error;
}

juce::String AsioHost::setInputSource(int pipeline, InputSource source,
                                      const juce::String& windowsDevice)
{
    if (pipeline < 0 || pipeline > 1)
        return {};

    windowsInputName[pipeline] = source == InputSource::WindowsAudio ? windowsDevice
                                                                     : juce::String();
    inputSource[pipeline].store((int) source);

    juce::String error;
    if (device != nullptr) {
        if (source == InputSource::WindowsAudio)
            error = startWindowsInput(pipeline, getSampleRate());
        else
            stopWindowsInput(pipeline);
    }

    saveRoutes();
    if (onChange) onChange();
    return error;
}

void AsioHost::loadInputSources()
{
    for (int p = 0; p < 2; ++p) {
        windowsInputName[p].clear();
        if (settings != nullptr) {
            if (settings->getValue(sourceKey(p)) == "windows")
                inputSource[p].store((int) InputSource::WindowsAudio);
            windowsInputName[p] = settings->getValue(windowsDeviceKey(p));
        }
    }
}

void AsioHost::loadOutputTargets()
{
    for (int p = 0; p < 2; ++p) {
        windowsOutputName[p].clear();
        if (settings != nullptr) {
            if (settings->getValue(sinkKey(p)) == "windows")
                outputTarget[p].store((int) OutputTarget::WindowsAudio);
            windowsOutputName[p] = settings->getValue(windowsSinkKey(p));
        }
    }
}

juce::String AsioHost::startWindowsInputs(double sampleRate)
{
    juce::String firstError;
    for (int p = 0; p < 2; ++p) {
        if (getInputSource(p) != InputSource::WindowsAudio)
            continue;
        const auto error = startWindowsInput(p, sampleRate);
        if (firstError.isEmpty())
            firstError = error;
    }
    return firstError;
}

juce::String AsioHost::startWindowsInput(int pipeline, double sampleRate)
{
    stopWindowsInput(pipeline);

    if (getInputSource(pipeline) != InputSource::WindowsAudio)
        return {};

    if (windowsInputName[pipeline].isEmpty())
        return "No Windows Audio input device is selected for the "
             + juce::String(pipeline == inputPipeline ? "Input" : "Output") + " pipeline.";

    if (sampleRate <= 0.0)
        sampleRate = 48000.0;

    if (windowsInput[pipeline] == nullptr)
        windowsInput[pipeline] = std::make_unique<WindowsAudioInput>();

    const auto error = windowsInput[pipeline]->start(windowsInputName[pipeline], sampleRate);
    if (error.isNotEmpty()) {
        asioTrace("windows input for pipeline " + juce::String(pipeline) + " failed: " + error);
        stopWindowsInput(pipeline);
    }
    return error;
}

void AsioHost::stopWindowsInput(int pipeline)
{
    if (pipeline >= 0 && pipeline <= 1 && windowsInput[pipeline] != nullptr)
        windowsInput[pipeline]->stop();
}

void AsioHost::stopWindowsInputs()
{
    for (int p = 0; p < 2; ++p)
        stopWindowsInput(p);
}

juce::String AsioHost::startWindowsOutputs(double sampleRate)
{
    juce::String firstError;
    for (int p = 0; p < 2; ++p) {
        if (getOutputTarget(p) != OutputTarget::WindowsAudio)
            continue;
        const auto error = startWindowsOutput(p, sampleRate);
        if (firstError.isEmpty())
            firstError = error;
    }
    return firstError;
}

juce::String AsioHost::startWindowsOutput(int pipeline, double sampleRate)
{
    stopWindowsOutput(pipeline);

    if (getOutputTarget(pipeline) != OutputTarget::WindowsAudio)
        return {};

    if (windowsOutputName[pipeline].isEmpty())
        return "No Windows Audio output device is selected for the "
             + juce::String(pipeline == inputPipeline ? "Input" : "Output") + " pipeline.";

    if (sampleRate <= 0.0)
        sampleRate = 48000.0;

    if (windowsOutput[pipeline] == nullptr)
        windowsOutput[pipeline] = std::make_unique<WindowsAudioOutput>();

    const auto error = windowsOutput[pipeline]->start(windowsOutputName[pipeline], sampleRate);
    if (error.isNotEmpty()) {
        asioTrace("windows output for pipeline " + juce::String(pipeline) + " failed: " + error);
        stopWindowsOutput(pipeline);
    }
    return error;
}

void AsioHost::stopWindowsOutput(int pipeline)
{
    if (pipeline >= 0 && pipeline <= 1 && windowsOutput[pipeline] != nullptr)
        windowsOutput[pipeline]->stop();
}

void AsioHost::stopWindowsOutputs()
{
    for (int p = 0; p < 2; ++p)
        stopWindowsOutput(p);
}

void AsioHost::saveRoutes()
{
    if (settings == nullptr) return;
    for (int p = 0; p < 2; ++p) {
        for (bool input : { true, false }) {
            const int pair = getRoute(p, input);
            const auto& names = input ? inNames : outNames;
            // Store the first channel's name ("" = none)
            settings->setValue(key(p, input), (pair >= 0 && pair * 2 < names.size()) ? names[pair * 2] : juce::String());
        }
        settings->setValue(sourceKey(p), getInputSource(p) == InputSource::WindowsAudio ? "windows" : "asio");
        settings->setValue(windowsDeviceKey(p), windowsInputName[p]);
        settings->setValue(sinkKey(p), getOutputTarget(p) == OutputTarget::WindowsAudio ? "windows" : "asio");
        settings->setValue(windowsSinkKey(p), windowsOutputName[p]);
    }
    if (auto* f = dynamic_cast<juce::PropertiesFile*>(settings))
        f->saveIfNeeded();
}

void AsioHost::resolveRoutesFromNames()
{
    for (int p = 0; p < 2; ++p) {
        for (bool input : { true, false }) {
            const auto& names = input ? inNames : outNames;
            int pair = -1;

            if (settings != nullptr && settings->containsKey(key(p, input))) {
                const auto stored = settings->getValue(key(p, input));
                const int idx = stored.isNotEmpty() ? names.indexOf(stored) : -1;
                pair = idx >= 0 ? idx / 2 : -1;
            } else {
                // First use — safe defaults (nothing can feed back):
                //   Output pipeline: no input yet, plays to the first outputs (speakers)
                //   Input pipeline:  first inputs (mic), no output until a SAR endpoint is chosen
                if (p == outputPipeline && !input && names.size() > 0) pair = 0;
                if (p == inputPipeline  &&  input && names.size() > 0) pair = 0;
            }
            routes[p][input ? 0 : 1].store(pair);
        }
    }
}

// --- audio thread ------------------------------------------------------------

void AsioHost::audioDeviceAboutToStart(juce::AudioIODevice* d)
{
    const double sr = d->getCurrentSampleRate();
    const int bs = d->getCurrentBufferSizeSamples();

    for (int p = 0; p < 2; ++p) {
        auto* proc = procs[p];
        const juce::ScopedLock sl(proc->getCallbackLock());
        proc->setRateAndBufferSizeDetails(sr, bs);
        proc->prepareToPlay(sr, bs);
        work[p].setSize(2, juce::jmax(bs, 1), false, true, false);
    }
    midi.ensureSize(256);
}

void AsioHost::audioDeviceStopped()
{
    for (auto* proc : procs) {
        const juce::ScopedLock sl(proc->getCallbackLock());
        proc->releaseResources();
    }
}

void AsioHost::audioDeviceError(const juce::String&)
{
}

void AsioHost::audioDeviceIOCallbackWithContext(const float* const* inputs, int numInputs,
                                                float* const* outputs, int numOutputs, int numSamples,
                                                const juce::AudioIODeviceCallbackContext&)
{
    for (int ch = 0; ch < numOutputs; ++ch)
        if (outputs[ch] != nullptr)
            juce::FloatVectorOperations::clear(outputs[ch], numSamples);

    for (int p = 0; p < 2; ++p) {
        const int inPair  = routes[p][0].load();
        const int outPair = routes[p][1].load();
        const bool windowsSource = getInputSource(p) == InputSource::WindowsAudio;
        const bool windowsSink   = getOutputTarget(p) == OutputTarget::WindowsAudio;

        // A Windows Audio source/sink is independent of the ASIO channel pairs,
        // so a pipeline with "no ASIO pair" must still run.
        if (! windowsSource && ! windowsSink && inPair < 0 && outPair < 0)
            continue;

        auto& buf = work[p];
        if (numSamples > buf.getNumSamples())
            buf.setSize(2, numSamples, false, false, true);   // shouldn't happen; driver gave a bigger block

        if (windowsSource) {
            // Microphone captured through WASAPI by WindowsAudioInput.
            if (windowsInput[p] != nullptr && windowsInput[p]->isRunning())
                windowsInput[p]->readInto(buf, numSamples);
            else
                buf.clear();
        } else {
            // Gather the input pair (mono source feeds both sides)
            for (int c = 0; c < 2; ++c) {
                int src = inPair >= 0 ? inPair * 2 + c : -1;
                if (src >= numInputs) src = (inPair >= 0 && inPair * 2 < numInputs) ? inPair * 2 : -1;
                if (src >= 0 && inputs[src] != nullptr)
                    buf.copyFrom(c, 0, inputs[src], numSamples);
                else
                    buf.clear(c, 0, numSamples);
            }
        }

        juce::AudioBuffer<float> view(buf.getArrayOfWritePointers(), 2, numSamples);
        {
            auto* proc = procs[p];
            const juce::ScopedLock sl(proc->getCallbackLock());
            if (!proc->isSuspended()) {
                midi.clear();
                proc->processBlock(view, midi);
            }
        }

        if (windowsSink) {
            // Playback through WASAPI (e.g. ASIO input -> effects -> speakers).
            if (windowsOutput[p] != nullptr && windowsOutput[p]->isRunning())
                windowsOutput[p]->writeFrom(view, numSamples);
        } else if (outPair >= 0) {
            // Mix into the ASIO output pair (two pipelines may share outputs)
            for (int c = 0; c < 2; ++c) {
                const int dst = outPair * 2 + c;
                if (dst < numOutputs && outputs[dst] != nullptr)
                    juce::FloatVectorOperations::add(outputs[dst], view.getReadPointer(c), numSamples);
            }
        }
    }
}

// --- driver reset --------------------------------------------------------------

void AsioHost::audioDeviceListChanged()
{
    // JUCE reopened the driver with the old channel set; reopen with all channels
    // so new SAR endpoints show up, and re-resolve routes by name.
    if (reopening || driverName.isEmpty())
        return;

    auto token = alive;
    juce::MessageManager::callAsync([this, token] {
        if (!token->load() || reopening || driverName.isEmpty()) return;
        reopening = true;
        open(driverName);
        reopening = false;
    });
}

} // namespace eeval
