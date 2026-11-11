#include "ConvolverEditor.h"
#include "../dsp/ConvolverModule.h"

namespace eeval {
namespace ui {

ConvolverEditor::ConvolverEditor(EasyEffectsAudioProcessor& proc, int slotIdx)
    : processor(proc), slotIndex(slotIdx)
{
    // Embed the generic editor for the Mix/Presets UI
    genericEditor = std::make_unique<GenericModuleEditor>(
        processor.parameters, slotIndex, "convolver", "Convolver");
    addAndMakeVisible(genericEditor.get());

    addAndMakeVisible(loadBtn);
    addAndMakeVisible(clearBtn);
    
    fileLabel.setFont(juce::Font(14.0f));
    fileLabel.setColour(juce::Label::textColourId, eeval::theme::textPrimary);
    fileLabel.setText("No IR Loaded", juce::dontSendNotification);
    addAndMakeVisible(fileLabel);

    loadBtn.onClick = [this]() {
        fileChooser = std::make_unique<juce::FileChooser>("Select an Impulse Response (.wav)", juce::File{}, "*.wav");
        
        auto chooserFlags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;
        fileChooser->launchAsync(chooserFlags, [this](const juce::FileChooser& fc) {
            auto file = fc.getResult();
            if (file.existsAsFile()) {
                if (auto* mod = processor.getChain().getSlotModule(slotIndex)) {
                    if (auto* conv = dynamic_cast<ConvolverModule*>(mod)) {
                        conv->loadImpulseResponse(file);
                    }
                }
            }
        });
    };

    clearBtn.onClick = [this]() {
        if (auto* mod = processor.getChain().getSlotModule(slotIndex)) {
            if (auto* conv = dynamic_cast<ConvolverModule*>(mod)) {
                conv->loadImpulseResponse(juce::File()); // empty file clears it
            }
        }
    };

    getProperties().set("preferredHeight",
        irCardHeight + (int)genericEditor->getProperties().getWithDefault("preferredHeight", 400));

    startTimerHz(10);
}

void ConvolverEditor::timerCallback() {
    // Update label with loaded IR name
    if (auto* mod = processor.getChain().getSlotModule(slotIndex)) {
        if (auto* conv = dynamic_cast<ConvolverModule*>(mod)) {
            std::string name = conv->getLoadedIRName();
            if (name.empty()) {
                fileLabel.setText("No IR Loaded", juce::dontSendNotification);
            } else {
                fileLabel.setText("Loaded: " + juce::String(name), juce::dontSendNotification);
            }
        }
    }
}

void ConvolverEditor::paint(juce::Graphics& g) {
    g.fillAll(eeval::theme::bgBase);
    eeval::theme::drawCard(g, irCard, "Impulse response");
}

void ConvolverEditor::resized() {
    auto area = getLocalBounds();

    irCard = area.removeFromTop(irCardHeight).reduced(14, 0).withTrimmedTop(10);

    // Mirror theme::drawCard's content area
    auto content = irCard.reduced(12, 8);
    content.removeFromTop(26 + 4);
    auto row = content.removeFromTop(30);

    loadBtn.setBounds(row.removeFromLeft(130));
    row.removeFromLeft(8);
    clearBtn.setBounds(row.removeFromLeft(80));
    row.removeFromLeft(14);
    fileLabel.setBounds(row);

    if (genericEditor) {
        genericEditor->setBounds(area);
    }
}

} // namespace ui
} // namespace eeval
