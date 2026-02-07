#include "PluginEditor.h"

LazirkoAudioProcessorEditor::LazirkoAudioProcessorEditor(LazirkoAudioProcessor& p)
    : AudioProcessorEditor(&p), processor(p)
{
    auto setupSlider = [](juce::Slider& s)
    {
            s.setSliderStyle(juce::Slider::Rotary);
            s.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 60, 20);
    };

    setupSlider(dephaseSlider);
    setupSlider(dampingSlider);
    setupSlider(mixSlider);

    addAndMakeVisible(dephaseSlider);
    addAndMakeVisible(dampingSlider);
    addAndMakeVisible(mixSlider);

    dephaseLabel.setText("Dephase", juce::dontSendNotification);
    dephaseLabel.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(dephaseLabel);

    dampingLabel.setText("Damping", juce::dontSendNotification);
    dampingLabel.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(dampingLabel);

    mixLabel.setText("Mix", juce::dontSendNotification);
    mixLabel.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(mixLabel);

    autoGainButton.setButtonText("AUTO GAIN");
    autoGainButton.setClickingTogglesState(true);
    addAndMakeVisible(autoGainButton);

    auto& apvts = processor.getAPVTS();

    dephaseAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        apvts, "DEPHASE", dephaseSlider);
    dampingAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        apvts, "DAMPING", dampingSlider);
    mixAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        apvts, "MIX", mixSlider);
    autoGainAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        apvts, "AUTOGAIN", autoGainButton);

    modeSelector.addItem("Mono", 1);
    modeSelector.addItem("L/R", 2);
    modeSelector.addItem("M/S", 3);
    modeSelector.addItem("Transient/Sustain", 4);
    modeSelector.setSelectedId(1);
    addAndMakeVisible(modeSelector);

    modeLabel.setText("Mode", juce::dontSendNotification);
    modeLabel.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(modeLabel);

    modeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        apvts, "MODE", modeSelector);

    setSize(420, 260);
}

LazirkoAudioProcessorEditor::~LazirkoAudioProcessorEditor() = default;

void LazirkoAudioProcessorEditor::paint(juce::Graphics& g)
{
    g.fillAll(getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId));
    g.setColour(juce::Colours::white);
    g.setFont(juce::FontOptions(16.0f));
    g.drawFittedText("Quantum Noise Channel", getLocalBounds().removeFromTop(30),
                      juce::Justification::centred, 1);
}

void LazirkoAudioProcessorEditor::resized()
{
    auto r = getLocalBounds().reduced(12);
    r.removeFromTop(30);

    auto row = r.removeFromTop(100);
    int colWidth = row.getWidth() / 3;

    auto dephaseArea = row.removeFromLeft(colWidth).reduced(8);
    dephaseSlider.setBounds(dephaseArea);
    dephaseLabel.setBounds(dephaseArea.getX(), dephaseArea.getBottom() + 4,
                            dephaseArea.getWidth(), 20);

    auto dampingArea = row.removeFromLeft(colWidth).reduced(8);
    dampingSlider.setBounds(dampingArea);
    dampingLabel.setBounds(dampingArea.getX(), dampingArea.getBottom() + 4,
                            dampingArea.getWidth(), 20);

    auto mixArea = row.removeFromLeft(colWidth).reduced(8);
    mixSlider.setBounds(mixArea);
    mixLabel.setBounds(mixArea.getX(), mixArea.getBottom() + 4,
                        mixArea.getWidth(), 20);

    r.removeFromTop(10);
    auto buttonArea = r.removeFromTop(30);
    autoGainButton.setBounds(buttonArea.withSizeKeepingCentre(120, 28));

    r.removeFromTop(8);
    auto modeRow = r.removeFromTop(30);
    auto modeLabelArea = modeRow.removeFromLeft(60);
    modeLabel.setBounds(modeLabelArea);
    modeSelector.setBounds(modeRow.reduced(8, 2));
}
