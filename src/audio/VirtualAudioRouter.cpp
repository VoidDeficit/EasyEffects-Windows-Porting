#include "VirtualAudioRouter.h"
#include "WindowsAudioEndpoints.h"

namespace eeval {

namespace {

constexpr const char* kWasapiTypeName     = "Windows Audio";
constexpr const char* kKeyMode            = "virtualAudio.mode";
constexpr const char* kKeyPhysicalOutput  = "virtualAudio.physicalOutput";
constexpr const char* kKeyPhysicalInput   = "virtualAudio.physicalInput";
constexpr const char* kKeyRestoreRender   = "virtualAudio.restoreRenderId";
constexpr const char* kKeyRestoreCapture  = "virtualAudio.restoreCaptureId";

// WASAPI friendly names look like "EasyEffects Speakers (EasyEffects Virtual Audio)".
// Note "EasyEffects Speakers" is a prefix of "EasyEffects Speakers Monitor", so
// require either an exact match or the " (" separator.
bool endpointMatches(const juce::String& deviceName, const juce::String& endpointName)
{
    return deviceName == endpointName || deviceName.startsWith(endpointName + " (");
}

} // namespace

VirtualAudioRouter::VirtualAudioRouter(juce::AudioDeviceManager& dm, juce::PropertySet* props)
    : deviceManager(dm), settings(props)
{
    if (settings != nullptr) {
        physicalOutput = settings->getValue(kKeyPhysicalOutput);
        physicalInput  = settings->getValue(kKeyPhysicalInput);
    }
}

VirtualAudioRouter::~VirtualAudioRouter()
{
    // Give the user their normal Windows defaults back when the app closes.
    // The mode itself stays saved so it is re-applied on the next launch.
    restoreWindowsDefaults();
    if (settings != nullptr)
        flushSettings();
}

juce::String VirtualAudioRouter::getModeName(Mode m)
{
    switch (m) {
        case Mode::SystemAudio: return "Process system audio";
        case Mode::Microphone:  return "Process microphone";
        case Mode::Off:         break;
    }
    return "Off";
}

bool VirtualAudioRouter::isVirtualDeviceName(const juce::String& name)
{
    for (auto* ep : { speakersName, speakersMonitorName, micFeedName, microphoneName })
        if (endpointMatches(name, ep))
            return true;
    return false;
}

juce::AudioIODeviceType* VirtualAudioRouter::getWasapiType()
{
    for (auto* type : deviceManager.getAvailableDeviceTypes()) {
        if (type->getTypeName() == kWasapiTypeName) {
            type->scanForDevices();
            return type;
        }
    }
    return nullptr;
}

juce::String VirtualAudioRouter::findDevice(bool isInput, const juce::String& endpointName)
{
    if (auto* type = getWasapiType())
        for (auto& name : type->getDeviceNames(isInput))
            if (endpointMatches(name, endpointName))
                return name;
    return {};
}

bool VirtualAudioRouter::isDriverInstalled()
{
    return findDevice(false, speakersName).isNotEmpty()
        && findDevice(true,  speakersMonitorName).isNotEmpty()
        && findDevice(false, micFeedName).isNotEmpty()
        && findDevice(true,  microphoneName).isNotEmpty();
}

juce::StringArray VirtualAudioRouter::getPhysicalOutputs()
{
    juce::StringArray result;
    if (auto* type = getWasapiType())
        for (auto& name : type->getDeviceNames(false))
            if (!isVirtualDeviceName(name))
                result.add(name);
    return result;
}

juce::StringArray VirtualAudioRouter::getPhysicalInputs()
{
    juce::StringArray result;
    if (auto* type = getWasapiType())
        for (auto& name : type->getDeviceNames(true))
            if (!isVirtualDeviceName(name))
                result.add(name);
    return result;
}

juce::String VirtualAudioRouter::pickDefaultPhysical(bool isInput)
{
    const auto candidates = isInput ? getPhysicalInputs() : getPhysicalOutputs();

    // Prefer whatever Windows currently uses as default (the user's real device).
    if (auto def = winaudio::getDefaultEndpoint(isInput ? winaudio::Flow::Capture : winaudio::Flow::Render)) {
        const auto defName = juce::String::fromUTF8(def->name.c_str());
        if (candidates.contains(defName))
            return defName;
    }
    return candidates.isEmpty() ? juce::String() : candidates[0];
}

juce::String VirtualAudioRouter::applyDeviceSetup(const juce::String& input, const juce::String& output)
{
    if (deviceManager.getCurrentAudioDeviceType() != kWasapiTypeName)
        deviceManager.setCurrentAudioDeviceType(kWasapiTypeName, true);

    auto setup = deviceManager.getAudioDeviceSetup();
    setup.inputDeviceName = input;
    setup.outputDeviceName = output;
    setup.useDefaultInputChannels = true;
    setup.useDefaultOutputChannels = true;
    setup.sampleRate = 48000.0;  // the virtual endpoints run at 48 kHz

    return deviceManager.setAudioDeviceSetup(setup, true);
}

void VirtualAudioRouter::switchWindowsDefault(bool capture, const juce::String& endpointName)
{
    const auto flow = capture ? winaudio::Flow::Capture : winaudio::Flow::Render;
    const char* restoreKey = capture ? kKeyRestoreCapture : kKeyRestoreRender;

    if (auto current = winaudio::getDefaultEndpoint(flow)) {
        const auto currentName = juce::String::fromUTF8(current->name.c_str());
        if (!isVirtualDeviceName(currentName) && settings != nullptr)
            settings->setValue(restoreKey, juce::String::fromUTF8(current->id.c_str()));
    }

    for (const auto& ep : winaudio::listEndpoints(flow)) {
        if (endpointMatches(juce::String::fromUTF8(ep.name.c_str()), endpointName)) {
            winaudio::setDefaultEndpoint(ep.id);
            break;
        }
    }

    if (settings != nullptr)
        flushSettings();
}

void VirtualAudioRouter::restoreWindowsDefaults()
{
    if (settings == nullptr)
        return;

    for (bool capture : { false, true }) {
        const char* key = capture ? kKeyRestoreCapture : kKeyRestoreRender;
        const auto savedId = settings->getValue(key);
        if (savedId.isEmpty())
            continue;

        const auto flow = capture ? winaudio::Flow::Capture : winaudio::Flow::Render;

        // Only restore if the default is still one of ours: if the user picked
        // something else in the meantime, respect that.
        bool defaultIsOurs = false;
        if (auto current = winaudio::getDefaultEndpoint(flow))
            defaultIsOurs = isVirtualDeviceName(juce::String::fromUTF8(current->name.c_str()));

        if (defaultIsOurs) {
            const auto id = savedId.toStdString();
            for (const auto& ep : winaudio::listEndpoints(flow)) {
                if (ep.id == id) {
                    winaudio::setDefaultEndpoint(id);
                    break;
                }
            }
        }

        settings->removeValue(key);
    }

    flushSettings();
}

void VirtualAudioRouter::flushSettings()
{
    // Persist immediately so a crash can't lose the IDs needed to restore the
    // user's Windows default devices.
    if (auto* file = dynamic_cast<juce::PropertiesFile*>(settings))
        file->saveIfNeeded();
}

void VirtualAudioRouter::saveSettings()
{
    if (settings == nullptr)
        return;

    settings->setValue(kKeyMode, (int)mode);
    settings->setValue(kKeyPhysicalOutput, physicalOutput);
    settings->setValue(kKeyPhysicalInput, physicalInput);
    flushSettings();
}

juce::String VirtualAudioRouter::setMode(Mode newMode)
{
    if (newMode != Mode::Off && !isDriverInstalled())
        return "The EasyEffects Virtual Audio driver is not installed.\n"
               "Run EEVirtualAudioSetup.exe (next to EasyEffects.exe) to install it.";

    // Undo whatever the previous mode changed before applying the new one, so
    // pickDefaultPhysical() sees the user's real default device.
    restoreWindowsDefaults();

    juce::String error;

    switch (newMode) {
        case Mode::Off:
            break;

        case Mode::SystemAudio: {
            if (physicalOutput.isEmpty() || !getPhysicalOutputs().contains(physicalOutput))
                physicalOutput = pickDefaultPhysical(false);
            if (physicalOutput.isEmpty()) {
                error = "No physical output device (speakers/headphones) was found.";
                break;
            }
            error = applyDeviceSetup(findDevice(true, speakersMonitorName), physicalOutput);
            if (error.isEmpty())
                switchWindowsDefault(false, speakersName);
            break;
        }

        case Mode::Microphone: {
            if (physicalInput.isEmpty() || !getPhysicalInputs().contains(physicalInput))
                physicalInput = pickDefaultPhysical(true);
            if (physicalInput.isEmpty()) {
                error = "No physical microphone was found.";
                break;
            }
            error = applyDeviceSetup(physicalInput, findDevice(false, micFeedName));
            if (error.isEmpty())
                switchWindowsDefault(true, microphoneName);
            break;
        }
    }

    mode = error.isEmpty() ? newMode : Mode::Off;
    saveSettings();
    return error;
}

juce::String VirtualAudioRouter::restoreSavedMode()
{
    const int saved = settings != nullptr ? settings->getIntValue(kKeyMode, 0) : 0;
    const auto savedMode = (saved == 1) ? Mode::SystemAudio
                         : (saved == 2) ? Mode::Microphone
                                        : Mode::Off;

    if (savedMode == Mode::Off) {
        // Clean up after a crash that left Windows pointing at our endpoints.
        restoreWindowsDefaults();
        return {};
    }
    return setMode(savedMode);
}

juce::String VirtualAudioRouter::setPhysicalOutput(const juce::String& name)
{
    physicalOutput = name;
    saveSettings();
    return mode == Mode::SystemAudio ? setMode(mode) : juce::String();
}

juce::String VirtualAudioRouter::setPhysicalInput(const juce::String& name)
{
    physicalInput = name;
    saveSettings();
    return mode == Mode::Microphone ? setMode(mode) : juce::String();
}

} // namespace eeval
