#include "PluginProcessor.h"
#include "PluginEditor.h"

LazirkoAudioProcessor::LazirkoAudioProcessor()
#ifndef JucePlugin_PreferredChannelConfigurations
    : AudioProcessor (BusesProperties()
                      #if ! JucePlugin_IsMidiEffect
                       #if ! JucePlugin_IsSynth
                        .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                       #endif
                        .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                      #endif
                      ),
      parameters (*this, nullptr, "PARAMETERS", createParameterLayout())
#else
    : parameters (*this, nullptr, "PARAMETERS", createParameterLayout())
#endif
{
    dephasingParam = parameters.getRawParameterValue ("DEPHASE");
    dampingParam   = parameters.getRawParameterValue ("DAMPING");
    mixParam       = parameters.getRawParameterValue ("MIX");
}

LazirkoAudioProcessor::~LazirkoAudioProcessor() {}

const juce::String LazirkoAudioProcessor::getName() const { return JucePlugin_Name; }
bool LazirkoAudioProcessor::acceptsMidi() const
{
   #if JucePlugin_WantsMidiInput
    return true;
   #else
    return false;
   #endif
}

bool LazirkoAudioProcessor::producesMidi() const
{
   #if JucePlugin_ProducesMidiOutput
    return true;
   #else
    return false;
   #endif
}

bool LazirkoAudioProcessor::isMidiEffect() const
{
   #if JucePlugin_IsMidiEffect
    return true;
   #else
    return false;
   #endif
}

double LazirkoAudioProcessor::getTailLengthSeconds() const { return 0.0; }
int LazirkoAudioProcessor::getNumPrograms() { return 1; }
int LazirkoAudioProcessor::getCurrentProgram() { return 0; }
void LazirkoAudioProcessor::setCurrentProgram (int) {}
const juce::String LazirkoAudioProcessor::getProgramName (int) { return {}; }
void LazirkoAudioProcessor::changeProgramName (int, const juce::String&) {}

void LazirkoAudioProcessor::prepareToPlay (double, int samplesPerBlock)
{
    ensureQuantumStateSize (samplesPerBlock);
}

void LazirkoAudioProcessor::releaseResources() {}

#ifndef JucePlugin_PreferredChannelConfigurations
bool LazirkoAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
   #if JucePlugin_IsMidiEffect
    juce::ignoreUnused (layouts);
    return true;
   #else
    auto mainOut = layouts.getMainOutputChannelSet();
    if (mainOut != juce::AudioChannelSet::mono() && mainOut != juce::AudioChannelSet::stereo())
        return false;

   #if ! JucePlugin_IsSynth
    if (mainOut != layouts.getMainInputChannelSet())
        return false;
   #endif

    return true;
   #endif
}
#endif

void LazirkoAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    juce::ignoreUnused (midiMessages);

    auto totalNumInputChannels  = getTotalNumInputChannels();
    auto totalNumOutputChannels = getTotalNumOutputChannels();

    for (auto i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
        buffer.clear (i, 0, buffer.getNumSamples());

    const int numSamples = buffer.getNumSamples();
    ensureQuantumStateSize (numSamples);

    // Stack-allocated temp buffer for mono processing
    juce::HeapBlock<float> monoTemp (static_cast<size_t> (numSamples));
    monoTemp.clear (numSamples);

    // Mix down to mono
    if (totalNumInputChannels == 1)
    {
        std::memcpy (monoTemp.getData(), buffer.getReadPointer (0), sizeof (float) * (size_t) numSamples);
    }
    else if (totalNumInputChannels >= 2)
    {
        auto* left  = buffer.getReadPointer (0);
        auto* right = buffer.getReadPointer (1);
        for (int n = 0; n < numSamples; ++n)
            monoTemp[n] = 0.5f * (left[n] + right[n]);
    }

    // Quantum processing pipeline
    encodeBlockToQuantumState (monoTemp.getData(), numSamples);
    applyQuantumChannel();
    decodeQuantumStateToBlock (monoTemp.getData(), numSamples, mixParam->load());

    // Write processed mono back to all outputs
    for (int ch = 0; ch < totalNumOutputChannels; ++ch)
        buffer.copyFrom (ch, 0, monoTemp.getData(), numSamples);
}

bool LazirkoAudioProcessor::hasEditor() const { return true; }
juce::AudioProcessorEditor* LazirkoAudioProcessor::createEditor()
{
    return new LazirkoAudioProcessorEditor (*this);
}

void LazirkoAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = parameters.copyState();
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void LazirkoAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xmlState = getXmlFromBinary (data, sizeInBytes))
        if (xmlState->hasTagName (parameters.state.getType()))
            parameters.replaceState (juce::ValueTree::fromXml (*xmlState));
}

juce::AudioProcessorValueTreeState::ParameterLayout LazirkoAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "DEPHASE", "Dephasing",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.01f, 1.0f),
        0.2f));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "DAMPING", "Amplitude Damping",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.01f, 1.0f),
        0.1f));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        "MIX", "Dry/Wet",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.01f),
        0.5f));

    return { params.begin(), params.end() };
}

// Quantum simulation internals

void LazirkoAudioProcessor::ensureQuantumStateSize (int numSamples)
{
    if (numSamples != lastBlockSize || (int) quantumState.size() != numSamples)
    {
        lastBlockSize = numSamples;
        quantumState.assign ((size_t) numSamples, Complex (0.0, 0.0));
    }
}

void LazirkoAudioProcessor::encodeBlockToQuantumState (const float* input, int numSamples)
{
    double norm2 = 0.0;
    for (int n = 0; n < numSamples; ++n)
    {
        const double x = static_cast<double> (input[n]);
        quantumState[(size_t) n] = Complex (x, 0.0);
        norm2 += x * x;
    }

    if (norm2 > 1e-12)
    {
        const double invNorm = 1.0 / std::sqrt (norm2);
        for (auto& amp : quantumState)
            amp *= invNorm;
    }
}

void LazirkoAudioProcessor::applyQuantumChannel()
{
    const double pDephase = juce::jlimit (0.0, 1.0, (double) dephasingParam->load());
    const double pDamp    = juce::jlimit (0.0, 1.0, (double) dampingParam->load());

    // Dephasing channel: mix amplitude with its magnitude (destroy phase)
    if (pDephase > 1e-6)
    {
        for (auto& amp : quantumState)
        {
            const double mag = std::abs (amp);
            amp = (1.0 - pDephase) * amp + pDephase * Complex (mag, 0.0);
        }
    }

    // Amplitude damping: nonlinear soft compression
    if (pDamp > 1e-6)
    {
        const double alpha = 1.0 + 4.0 * pDamp;
        for (auto& amp : quantumState)
        {
            const double mag = std::abs (amp);
            if (mag > 1e-12)
            {
                const double compressedMag = mag / (1.0 + alpha * mag * mag);
                amp *= (compressedMag / mag);
            }
        }
    }

    // Renormalise after channel
    double norm2 = 0.0;
    for (const auto& amp : quantumState)
        norm2 += std::norm (amp);

    if (norm2 > 1e-12)
    {
        const double invNorm = 1.0 / std::sqrt (norm2);
        for (auto& amp : quantumState)
            amp *= invNorm;
    }
}

void LazirkoAudioProcessor::decodeQuantumStateToBlock (float* output, int numSamples, float dryWetMix)
{
    const double mix = juce::jlimit (0.0, 1.0, (double) dryWetMix);

    for (int n = 0; n < numSamples; ++n)
    {
        const double wet  = quantumState[(size_t) n].real();
        const double dry  = static_cast<double> (output[n]);
        const double out  = (1.0 - mix) * dry + mix * wet;
        output[n] = static_cast<float> (juce::jlimit (-1.0, 1.0, out));
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new LazirkoAudioProcessor();
}
