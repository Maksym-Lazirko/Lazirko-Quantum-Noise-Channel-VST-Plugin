#pragma once

#include <JuceHeader.h>
#include <complex>
#include <vector>

class LazirkoAudioProcessor : public juce::AudioProcessor
{
public:
    LazirkoAudioProcessor();
    ~LazirkoAudioProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

   #ifndef JucePlugin_PreferredChannelConfigurations
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
   #endif

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    const juce::String getName() const override;
    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int index, const juce::String& newName) override;

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState& getAPVTS() { return parameters; }

private:
    using Complex = std::complex<double>;

    std::vector<Complex> quantumState;
    int lastBlockSize = 0;

    juce::AudioProcessorValueTreeState parameters;

    std::atomic<float>* dephasingParam = nullptr;
    std::atomic<float>* dampingParam   = nullptr;
    std::atomic<float>* mixParam       = nullptr;

    void ensureQuantumStateSize (int numSamples);
    void encodeBlockToQuantumState (const float* input, int numSamples);
    void applyQuantumChannel();
    void decodeQuantumStateToBlock (float* output, int numSamples, float dryWetMix);

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LazirkoAudioProcessor)
};
