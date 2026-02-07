#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

class LazirkoAudioProcessorEditor : public juce::AudioProcessorEditor
{
public:
    explicit LazirkoAudioProcessorEditor(LazirkoAudioProcessor&);
    ~LazirkoAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    LazirkoAudioProcessor& processor;

    juce::Slider dephaseSlider;
    juce::Slider dampingSlider;
    juce::Slider mixSlider;

    juce::Label dephaseLabel;
    juce::Label dampingLabel;
    juce::Label mixLabel;

    juce::ToggleButton autoGainButton;
    juce::ComboBox modeSelector;
    juce::Label modeLabel;

    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> dephaseAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> dampingAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> mixAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> autoGainAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> modeAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LazirkoAudioProcessorEditor)
};
