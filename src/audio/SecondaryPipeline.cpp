#include "SecondaryPipeline.h"

namespace eeval {

SecondaryPipeline::SecondaryPipeline(juce::PropertySet* s, const juce::String& k, const juce::String& other)
    : settings(s), key(k), otherOutput(other)
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

    // First run. Never fall back to the default microphone here: microphone ->
    // speakers would feed back. Listen to the VB-Cable output if it exists
    // (the usual "system audio" route), otherwise open no input at all.
    // (Enumerating device types doesn't open any device.)
    juce::String cableInput, speakers;
    for (auto* type : devices.getAvailableDeviceTypes()) {
        if (type->getTypeName() != "Windows Audio")
            continue;
        type->scanForDevices();
        for (const auto& name : type->getDeviceNames(true))
            if (name.startsWithIgnoreCase("CABLE Output")) { cableInput = name; break; }

        // The Output pipeline ends on real speakers/headphones: use the Windows
        // default unless that is a VB-Cable (common when routing system audio).
        const auto outs = type->getDeviceNames(false);
        const int def = type->getDefaultDeviceIndex(false);
        if (outs[def].isNotEmpty() && !outs[def].startsWithIgnoreCase("CABLE"))
            speakers = outs[def];
        else
            for (const auto& name : outs)
                if (!name.startsWithIgnoreCase("CABLE")) { speakers = name; break; }
    }

    // If the Input pipeline already writes into that cable, listening to it here
    // would play the processed microphone back through the speakers. Start with
    // no input instead; the user can pick one from the sidebar.
    if (sharesCableWith(cableInput, otherOutput))
        cableInput.clear();

    juce::AudioDeviceManager::AudioDeviceSetup setup;
    setup.inputDeviceName = cableInput;   // empty = no input
    setup.outputDeviceName = speakers;    // empty = system default
    setup.useDefaultInputChannels = true;
    setup.useDefaultOutputChannels = true;
    devices.initialise(cableInput.isNotEmpty() ? 2 : 0, 2, nullptr, true, {}, &setup);
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
