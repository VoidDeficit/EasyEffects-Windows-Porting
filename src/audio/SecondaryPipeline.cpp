#include "SecondaryPipeline.h"

namespace eeval {

SecondaryPipeline::SecondaryPipeline(juce::PropertySet* s, const juce::String& k)
    : settings(s), key(k)
{
    proc = std::make_unique<EasyEffectsAudioProcessor>();

    // Restore the saved chain, or start empty (EasyEffects' default).
    const auto saved = settings != nullptr ? settings->getValue(key + ".state") : juce::String();
    juce::MemoryBlock state;
    if (saved.isNotEmpty() && state.fromBase64Encoding(saved) && state.getSize() > 0) {
        proc->setStateInformation(state.getData(), (int)state.getSize());
    } else {
        while (!proc->getActiveSlots().empty())
            proc->removeEffect(0);
    }

    player.setProcessor(proc.get());
    openDevices();
    devices.addAudioCallback(&player);
}

SecondaryPipeline::~SecondaryPipeline()
{
    save();
    devices.removeAudioCallback(&player);
    player.setProcessor(nullptr);
    devices.closeAudioDevice();
}

void SecondaryPipeline::openDevices()
{
    std::unique_ptr<juce::XmlElement> savedXml;
    if (settings != nullptr)
        savedXml = settings->getXmlValue(key + ".audio");

    if (savedXml != nullptr) {
        devices.initialise(2, 2, savedXml.get(), true);
        return;
    }

    // First run: no input (never fall back to the default microphone — mic ->
    // speakers would feed back) and the default output. The source is picked
    // from the sidebar, or comes from Synchronous Audio Router in ASIO mode.
    devices.initialise(0, 2, nullptr, true);
}

void SecondaryPipeline::save()
{
    if (settings == nullptr || proc == nullptr)
        return;

    juce::MemoryBlock state;
    proc->getStateInformation(state);
    settings->setValue(key + ".state", state.toBase64Encoding());

    if (auto xml = devices.createStateXml())
        settings->setValue(key + ".audio", xml.get());

    if (auto* file = dynamic_cast<juce::PropertiesFile*>(settings))
        file->saveIfNeeded();
}

} // namespace eeval
