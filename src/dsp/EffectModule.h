#pragma once

#include <JuceHeader.h>
#include <string>

namespace eeval {

class EffectModule {
public:
    virtual ~EffectModule() = default;

    virtual void prepare(const juce::dsp::ProcessSpec& spec) = 0;
    virtual void reset() = 0;
    
    // Subclasses implement their core DSP here (called only when not bypassed)
    virtual void processInternal(juce::AudioBuffer<float>& buffer) = 0;

    // Updates internal state from the APVTS
    virtual void updateParameters(juce::AudioProcessorValueTreeState& apvts) = 0;

    // Returns the module ID used for parameter naming: "<moduleId>.<param>"
    virtual const std::string& getModuleId() const = 0;

    // Returns a human-readable display name
    virtual const std::string& getName() const = 0;

    // Optional state persistence for non-float parameters (e.g. file paths)
    virtual juce::var getState() { return juce::var(); }
    virtual void setState(const juce::var& state) { juce::ignoreUnused(state); }

    // Returns the latency in samples (if any)
    virtual double getLatencySamples() const { return 0.0; }

    // For dynamics processors (0.0 to 1.0, where 1.0 is full reduction).
    // By default measured from the block peaks before/after processInternal().
    virtual float getGainReduction() const { return measuredReduction.load(); }

    // For Voice Activity Detection (0.0 to 1.0)
    virtual float getVADProbability() const { return 0.0f; }

    // Peak level (dBFS) of the last block before / after the effect, for the
    // dynamics graph. Measured for every effect, whatever engine (Windows
    // Audio or ASIO) runs the chain.
    virtual float getInputLevel() const { return inputLevel.load(); }
    virtual float getOutputLevel() const { return outputLevel.load(); }

    // Main process entry point — handles bypass and dry/wet mix
    void process(juce::AudioBuffer<float>& buffer)
    {
        if (bypassed) return;

        const int n = buffer.getNumSamples();
        const float inMag = n > 0 ? buffer.getMagnitude(0, n) : 0.0f;
        processWithMix(buffer);
        const float outMag = n > 0 ? buffer.getMagnitude(0, n) : 0.0f;

        inputLevel.store(juce::Decibels::gainToDecibels(inMag, -100.0f));
        outputLevel.store(juce::Decibels::gainToDecibels(outMag, -100.0f));
        measuredReduction.store(inMag > 0.0001f ? juce::jlimit(0.0f, 1.0f, 1.0f - outMag / inMag) : 0.0f);
    }

private:
    void processWithMix(juce::AudioBuffer<float>& buffer)
    {
        if (dryWetMix >= 1.0f) {
            // Fully wet — no need to copy
            processInternal(buffer);
        } else if (dryWetMix <= 0.0f) {
            // Fully dry — skip processing entirely
            return;
        } else {
            // Blend: copy dry, process wet, mix
            dryBuffer.makeCopyOf(buffer, true);
            processInternal(buffer);

            for (int ch = 0; ch < buffer.getNumChannels(); ++ch) {
                auto* wet = buffer.getWritePointer(ch);
                const auto* dry = dryBuffer.getReadPointer(ch);
                for (int i = 0; i < buffer.getNumSamples(); ++i) {
                    wet[i] = dry[i] * (1.0f - dryWetMix) + wet[i] * dryWetMix;
                }
            }
        }
    }

public:
    void setBypassed(bool bypass) { bypassed = bypass; }
    bool isBypassed() const { return bypassed; }

    void setDryWetMix(float mix) { dryWetMix = juce::jlimit(0.0f, 1.0f, mix); }
    float getDryWetMix() const { return dryWetMix; }

    // --- Per-effect input/output gain and level meters (set/driven by EffectChain) ---

    // Linear gains applied around process() by EffectChain.
    void setSlotGains(float inLinear, float outLinear) { slotInGain = inLinear; slotOutGain = outLinear; }
    float getSlotInGain() const  { return slotInGain; }
    float getSlotOutGain() const { return slotOutGain; }

    // Peak levels of the last block in dBFS, per channel (0 = L, 1 = R). Read by the UI.
    void setMeterIn(int ch, float db)  { if (ch >= 0 && ch < 2) meterInDb[ch].store(db); }
    void setMeterOut(int ch, float db) { if (ch >= 0 && ch < 2) meterOutDb[ch].store(db); }
    float getMeterInDb(int ch) const   { return (ch >= 0 && ch < 2) ? meterInDb[ch].load() : -120.0f; }
    float getMeterOutDb(int ch) const  { return (ch >= 0 && ch < 2) ? meterOutDb[ch].load() : -120.0f; }

    // Slot-level identifier (e.g., "slot3")
    void setSlotPrefix(const std::string& prefix) { slotPrefix = prefix; }
    const std::string& getSlotPrefix() const { return slotPrefix; }

    // Effect-level parameter prefix (e.g., "slot3.compressor")
    void setParamPrefix(const std::string& prefix) { slotParamPrefix = prefix; }
    const std::string& getParamPrefix() const { return slotParamPrefix; }

    // Helper: builds a parameter ID string for effect-specific parameters
    // "slot3.compressor.threshold"
    std::string paramId(const std::string& paramName) const {
        if (!slotParamPrefix.empty())
            return slotParamPrefix + "." + paramName;
        return getModuleId() + "." + paramName;
    }

    // Helper: builds a parameter ID string for common slot parameters
    // "slot3.mix", "slot3.bypass"
    std::string slotParamId(const std::string& paramName) const {
        if (!slotPrefix.empty())
            return slotPrefix + "." + paramName;
        return paramName;
    }

protected:
    bool bypassed = false;
    float dryWetMix = 1.0f; // 1.0 = fully wet (effect applied)
    std::string slotPrefix;      // e.g., "slot3"
    std::string slotParamPrefix; // e.g., "slot3.compressor"
    std::atomic<float> inputLevel { -100.0f };
    std::atomic<float> outputLevel { -100.0f };
    std::atomic<float> measuredReduction { 0.0f };

    float slotInGain = 1.0f;
    float slotOutGain = 1.0f;
    std::atomic<float> meterInDb[2]  { { -120.0f }, { -120.0f } };
    std::atomic<float> meterOutDb[2] { { -120.0f }, { -120.0f } };

    // Pre-allocated in prepare() for dry/wet blending
    juce::AudioBuffer<float> dryBuffer;

    // Call in subclass prepare() to set up the dry buffer
    void prepareBuffers(const juce::dsp::ProcessSpec& spec) {
        dryBuffer.setSize((int)spec.numChannels, (int)spec.maximumBlockSize);
    }
};

} // namespace eeval
