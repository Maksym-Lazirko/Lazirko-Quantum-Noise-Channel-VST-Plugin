#pragma once

#include <JuceHeader.h>
#include <complex>
#include <vector>
#include <cmath>

class LazirkoAudioProcessor : public juce::AudioProcessor
{
public:
    LazirkoAudioProcessor();
    ~LazirkoAudioProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

#ifndef JucePlugin_PreferredChannelConfigurations
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
#endif

    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;
    const juce::String getName() const override;
    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;
    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram(int index) override;
    const juce::String getProgramName(int index) override;
    void changeProgramName(int index, const juce::String& newName) override;
    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState& getAPVTS() { return parameters; }

    enum ProcessingMode
    {
        Mono = 1,
        LeftRight = 2,
        MidSide = 3,
        TransientSustain = 4
    };

private:
    using Complex = std::complex<double>;

    // Pre-allocated buffers
    std::vector<Complex> quantumStateA;
    std::vector<Complex> quantumStateB;
    std::vector<float> dryBufferA;
    std::vector<float> dryBufferB;
    std::vector<float> wetBufferA;
    std::vector<float> wetBufferB;

    int lastBlockSize = 0;

    juce::AudioProcessorValueTreeState parameters;
    std::atomic<float>* dephasingParam = nullptr;
    std::atomic<float>* dampingParam = nullptr;
    std::atomic<float>* mixParam = nullptr;
    std::atomic<float>* autoGainParam = nullptr;
    std::atomic<float>* modeParam = nullptr;

    // Parameter smoothing
    juce::SmoothedValue<float> smoothedDephasing;
    juce::SmoothedValue<float> smoothedDamping;
    juce::SmoothedValue<float> smoothedMix;
    juce::SmoothedValue<float> smoothedGainCompensation;

    // RMS measurement
    float inputRMS = 0.0f;
    float outputRMS = 0.0f;

    // Simple IIR filters for T/S mode
    struct SimpleFilter
    {
        float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f;
        float a1 = 0.0f, a2 = 0.0f;
        float x1 = 0.0f, x2 = 0.0f;
        float y1 = 0.0f, y2 = 0.0f;

        void setCoefficients(float _b0, float _b1, float _b2, float _a1, float _a2)
        {
            b0 = _b0; b1 = _b1; b2 = _b2;
            a1 = _a1; a2 = _a2;
        }

        void reset()
        {
            x1 = x2 = y1 = y2 = 0.0f;
        }

        float process(float input)
        {
            float output = b0 * input + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
            x2 = x1; x1 = input;
            y2 = y1; y1 = output;
            return output;
        }
    };

    SimpleFilter transientFilterHP_L, transientFilterHP_R;
    SimpleFilter transientFilterLP_L, transientFilterLP_R;

    // Random generator for dephasing
    juce::Random randomGen;

    void ensureQuantumStateSize(int numSamples);

    // Quantum processing with numSamples parameter
    void applyQuantumChannel(std::vector<Complex>& state,
        float dephase, float damp, int numSamples);

    void processMonoMode(juce::AudioBuffer<float>& buffer, int numSamples);
    void processLeftRightMode(juce::AudioBuffer<float>& buffer, int numSamples);
    void processMidSideMode(juce::AudioBuffer<float>& buffer, int numSamples);
    void processTransientSustainMode(juce::AudioBuffer<float>& buffer, int numSamples);

    float calculateRMS(const float* data, int numSamples);
    void setupFilters(double sampleRate, float cutoffFreq);
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LazirkoAudioProcessor)
};