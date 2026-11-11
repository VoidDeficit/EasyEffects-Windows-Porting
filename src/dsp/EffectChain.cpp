#include "EffectChain.h"
#include "EffectRegistry.h"

namespace eeval {

template <typename Setter>
void EffectChain::measurePeaks(const juce::AudioBuffer<float>& buffer, Setter&& set)
{
    const int numCh = buffer.getNumChannels();
    const int n = buffer.getNumSamples();
    for (int ch = 0; ch < 2; ++ch) {
        // Mono buffers feed both meters from channel 0.
        const int src = juce::jmin(ch, numCh - 1);
        float peak = 0.0f;
        if (src >= 0 && n > 0)
            peak = buffer.getMagnitude(src, 0, n);
        set(ch, juce::Decibels::gainToDecibels(peak, -120.0f));
    }
}

EffectChain::EffectChain() {
}

void EffectChain::prepare(const juce::dsp::ProcessSpec& spec) {
    std::lock_guard<std::mutex> lock(slotMutex);
    cachedSpec = spec;
    isPrepared = true;
    for (auto& slot : slots) {
        if (slot.module)
            slot.module->prepare(spec);
    }
}

void EffectChain::process(juce::AudioBuffer<float>& buffer) {
    // Audio thread: process all active slots in order
    // We use try_lock to avoid blocking the audio thread
    std::lock_guard<std::mutex> lock(slotMutex);
    for (auto& slot : slots) {
        auto* m = slot.module.get();
        if (m == nullptr)
            continue;

        if (m->isBypassed()) {
            // Bypassed: signal passes untouched (no gain), meters show it as-is.
            measurePeaks(buffer, [m](int ch, float db) { m->setMeterIn(ch, db); m->setMeterOut(ch, db); });
            continue;
        }

        if (m->getSlotInGain() != 1.0f)
            buffer.applyGain(m->getSlotInGain());
        measurePeaks(buffer, [m](int ch, float db) { m->setMeterIn(ch, db); });

        m->process(buffer);

        if (m->getSlotOutGain() != 1.0f)
            buffer.applyGain(m->getSlotOutGain());
        measurePeaks(buffer, [m](int ch, float db) { m->setMeterOut(ch, db); });
    }
}

void EffectChain::reset() {
    std::lock_guard<std::mutex> lock(slotMutex);
    for (auto& slot : slots) {
        if (slot.module)
            slot.module->reset();
    }
}

void EffectChain::updateParameters(juce::AudioProcessorValueTreeState& apvts) {
    std::lock_guard<std::mutex> lock(slotMutex);

    if (!gainParamsResolved) {
        for (int i = 0; i < MAX_SLOTS; ++i) {
            const auto sp = EffectRegistry::slotPrefix(i);
            inGainParams[(size_t)i]  = apvts.getRawParameterValue(sp + ".inGain");
            outGainParams[(size_t)i] = apvts.getRawParameterValue(sp + ".outGain");
            bypassParams[(size_t)i]  = apvts.getRawParameterValue(sp + ".bypass");
            mixParams[(size_t)i]     = apvts.getRawParameterValue(sp + ".mix");
        }
        gainParamsResolved = true;
    }

    for (int i = 0; i < MAX_SLOTS; ++i) {
        auto& slot = slots[(size_t)i];
        if (!slot.module)
            continue;

        slot.module->updateParameters(apvts);

        if (const auto* bp = bypassParams[(size_t)i])
            slot.module->setBypassed(bp->load() > 0.5f);
        if (const auto* mx = mixParams[(size_t)i])
            slot.module->setDryWetMix(mx->load() / 100.0f);

        const auto* in  = inGainParams[(size_t)i];
        const auto* out = outGainParams[(size_t)i];
        slot.module->setSlotGains(in  ? juce::Decibels::decibelsToGain(in->load())  : 1.0f,
                                  out ? juce::Decibels::decibelsToGain(out->load()) : 1.0f);
    }
}

// --- Slot management (UI thread) ---

void EffectChain::setSlotModule(int slotIndex, std::unique_ptr<EffectModule> module) {
    if (slotIndex < 0 || slotIndex >= MAX_SLOTS) return;

    std::lock_guard<std::mutex> lock(slotMutex);

    // Prepare the new module if we already have a cached spec
    if (module && isPrepared) {
        module->prepare(cachedSpec);
    }

    slots[(size_t)slotIndex].module = std::move(module);
}

void EffectChain::clearSlot(int slotIndex) {
    if (slotIndex < 0 || slotIndex >= MAX_SLOTS) return;

    std::lock_guard<std::mutex> lock(slotMutex);
    slots[(size_t)slotIndex].module.reset();
    slots[(size_t)slotIndex].typeId = "none";
}

void EffectChain::swapSlots(int indexA, int indexB) {
    if (indexA < 0 || indexA >= MAX_SLOTS) return;
    if (indexB < 0 || indexB >= MAX_SLOTS) return;
    if (indexA == indexB) return;

    std::lock_guard<std::mutex> lock(slotMutex);
    std::swap(slots[(size_t)indexA], slots[(size_t)indexB]);
}

EffectModule* EffectChain::getSlotModule(int slotIndex) const {
    if (slotIndex < 0 || slotIndex >= MAX_SLOTS) return nullptr;
    return slots[(size_t)slotIndex].module.get();
}

int EffectChain::getActiveSlotCount() const {
    int count = 0;
    for (const auto& slot : slots) {
        if (slot.module) ++count;
    }
    return count;
}

const std::string& EffectChain::getSlotTypeId(int slotIndex) const {
    static const std::string empty = "none";
    if (slotIndex < 0 || slotIndex >= MAX_SLOTS) return empty;
    return slots[(size_t)slotIndex].typeId;
}

void EffectChain::setSlotTypeId(int slotIndex, const std::string& typeId) {
    if (slotIndex < 0 || slotIndex >= MAX_SLOTS) return;
    std::lock_guard<std::mutex> lock(slotMutex);
    slots[(size_t)slotIndex].typeId = typeId;
}

// --- Legacy compatibility ---

void EffectChain::addModule(std::unique_ptr<EffectModule> module) {
    // Find first empty slot
    for (int i = 0; i < MAX_SLOTS; ++i) {
        if (!slots[(size_t)i].module) {
            slots[(size_t)i].module = std::move(module);
            return;
        }
    }
}

void EffectChain::clearModules() {
    std::lock_guard<std::mutex> lock(slotMutex);
    for (auto& slot : slots) {
        slot.module.reset();
        slot.typeId = "none";
    }
}

std::vector<std::string> EffectChain::getModuleNames() const {
    std::vector<std::string> names;
    for (const auto& slot : slots) {
        if (slot.module)
            names.push_back(slot.module->getName());
    }
    return names;
}

std::vector<std::string> EffectChain::getModuleIds() const {
    std::vector<std::string> ids;
    for (const auto& slot : slots) {
        if (slot.module)
            ids.push_back(slot.module->getModuleId());
    }
    return ids;
}

} // namespace eeval
