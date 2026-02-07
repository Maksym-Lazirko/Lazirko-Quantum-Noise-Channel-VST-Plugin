#include "PluginProcessor.h"

#include "PluginEditor.h"

#include <cstring>

LazirkoAudioProcessor::LazirkoAudioProcessor()
#ifndef JucePlugin_PreferredChannelConfigurations
    : AudioProcessor(BusesProperties()
                      #if ! JucePlugin_IsMidiEffect
                       #if ! JucePlugin_IsSynth
        .withInput("Input", juce::AudioChannelSet::stereo(), true)
                       #endif
        .withOutput("Output", juce::AudioChannelSet::stereo(), true)
                      #endif
                      ),
    parameters(*this, nullptr, "PARAMETERS", createParameterLayout())
#else
    : parameters(*this, nullptr, "PARAMETERS", createParameterLayout())
#endif
{
    dephasingParam = parameters.getRawParameterValue("DEPHASE");
    dampingParam = parameters.getRawParameterValue("DAMPING");
    mixParam = parameters.getRawParameterValue("MIX");
    autoGainParam = parameters.getRawParameterValue("AUTOGAIN");
    modeParam = parameters.getRawParameterValue("MODE");
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
void LazirkoAudioProcessor::setCurrentProgram(int) {}
const juce::String LazirkoAudioProcessor::getProgramName(int) { return {}; }
void LazirkoAudioProcessor::changeProgramName(int, const juce::String&) {}

juce::AudioProcessorValueTreeState::ParameterLayout LazirkoAudioProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "DEPHASE", "Dephasing",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), 0.0f));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "DAMPING", "Damping",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), 0.0f));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "MIX", "Mix",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), 1.0f));

    layout.add(std::make_unique<juce::AudioParameterBool>(
        "AUTOGAIN", "Auto Gain", false));

    layout.add(std::make_unique<juce::AudioParameterChoice>(
        "MODE", "Mode",
        juce::StringArray{ "Mono", "L/R", "M/S", "T/S" }, 0));

    return layout;
}

void LazirkoAudioProcessor::setupFilters(double sampleRate, float cutoffFreq)
{
    float omega = 2.0f * juce::MathConstants<float>::pi * cutoffFreq / static_cast<float>(sampleRate);
    float alpha = std::sin(omega) / (2.0f * 0.707f);
    float cosw = std::cos(omega);
    float a0 = 1.0f + alpha;

    // High-pass
    transientFilterHP_L.setCoefficients(
        (1.0f + cosw) / (2.0f * a0),
        -(1.0f + cosw) / a0,
        (1.0f + cosw) / (2.0f * a0),
        (-2.0f * cosw) / a0,
        (1.0f - alpha) / a0
    );
    transientFilterHP_R.setCoefficients(
        (1.0f + cosw) / (2.0f * a0),
        -(1.0f + cosw) / a0,
        (1.0f + cosw) / (2.0f * a0),
        (-2.0f * cosw) / a0,
        (1.0f - alpha) / a0
    );

    // Low-pass
    transientFilterLP_L.setCoefficients(
        (1.0f - cosw) / (2.0f * a0),
        (1.0f - cosw) / a0,
        (1.0f - cosw) / (2.0f * a0),
        (-2.0f * cosw) / a0,
        (1.0f - alpha) / a0
    );
    transientFilterLP_R.setCoefficients(
        (1.0f - cosw) / (2.0f * a0),
        (1.0f - cosw) / a0,
        (1.0f - cosw) / (2.0f * a0),
        (-2.0f * cosw) / a0,
        (1.0f - alpha) / a0
    );

    transientFilterHP_L.reset();
    transientFilterHP_R.reset();
    transientFilterLP_L.reset();
    transientFilterLP_R.reset();
}

#ifndef JucePlugin_PreferredChannelConfigurations
bool LazirkoAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
   #if JucePlugin_IsMidiEffect
    juce::ignoreUnused(layouts);
    return true;
   #else
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono()
        && layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

   #if ! JucePlugin_IsSynth
    if (layouts.getMainOutputChannelSet() != layouts.getMainInputChannelSet())
        return false;
   #endif

    return true;
   #endif
}
#endif

void LazirkoAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    ensureQuantumStateSize(samplesPerBlock);

    // Fast smoothing for immediate response
    const double rampLengthSeconds = 0.005;
    smoothedDephasing.reset(sampleRate, rampLengthSeconds);
    smoothedDamping.reset(sampleRate, rampLengthSeconds);
    smoothedMix.reset(sampleRate, rampLengthSeconds);
    smoothedGainCompensation.reset(sampleRate, 0.05);
    smoothedGainCompensation.setCurrentAndTargetValue(1.0f);

    setupFilters(sampleRate, 800.0f);

    inputRMS = 0.0f;
    outputRMS = 0.0f;
}

void LazirkoAudioProcessor::releaseResources() {}

void LazirkoAudioProcessor::ensureQuantumStateSize(int numSamples)
{
    if (numSamples != lastBlockSize)
    {
        size_t size = static_cast<size_t>(numSamples);
        quantumStateA.resize(size);
        quantumStateB.resize(size);
        dryBufferA.resize(size);
        dryBufferB.resize(size);
        wetBufferA.resize(size);
        wetBufferB.resize(size);
        lastBlockSize = numSamples;
    }
}

float LazirkoAudioProcessor::calculateRMS(const float* data, int numSamples)
{
    if (numSamples <= 0)
        return 0.0f;

    double sumSquares = 0.0;
    for (int n = 0; n < numSamples; ++n)
    {
        const double val = static_cast<double>(data[n]);
        sumSquares += val * val;
    }
    return static_cast<float>(std::sqrt(sumSquares / static_cast<double>(numSamples)));
}

void LazirkoAudioProcessor::applyQuantumChannel(std::vector<Complex>& state,
    float dephase, float damp, int numSamples)
{
    const double pDephase = juce::jlimit(0.0, 1.0, static_cast<double>(dephase));
    const double pDamp = juce::jlimit(0.0, 1.0, static_cast<double>(damp));

    if (pDephase < 1e-6 && pDamp < 1e-6)
        return;

    // DEPHASING: Aggressive phase scrambling
    if (pDephase > 1e-6)
    {
        const double maxPhaseShift = juce::MathConstants<double>::pi * pDephase;
        for (int n = 0; n < numSamples; ++n)
        {
            Complex& amp = state[static_cast<size_t>(n)];
            const double mag = std::abs(amp);
            if (mag > 1e-12)
            {
                double phase = std::atan2(amp.imag(), amp.real());

                // Random phase shift
                const double randPhase = (randomGen.nextFloat() * 2.0 - 1.0) * maxPhaseShift;
                phase += randPhase;

                // Mix toward magnitude-only for coherence loss
                const double coherence = 1.0 - pDephase * 0.5;
                const Complex rotated(mag * std::cos(phase), mag * std::sin(phase));
                const Complex magnitudeOnly(mag, 0.0);
                amp = coherence * rotated + (1.0 - coherence) * magnitudeOnly;
            }
        }
    }

    // DAMPING: Strong saturation with makeup gain
    if (pDamp > 1e-6)
    {
        const double drive = 1.0 + 7.0 * pDamp;
        const double makeup = 1.0 + 1.5 * pDamp;

        for (int n = 0; n < numSamples; ++n)
        {
            Complex& amp = state[static_cast<size_t>(n)];
            double re = amp.real() * drive;
            double im = amp.imag() * drive;

            // Soft saturation
            re = re / (1.0 + std::abs(re));
            im = im / (1.0 + std::abs(im));

            amp = Complex(re * makeup, im * makeup);
        }
    }
}

void LazirkoAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    juce::ignoreUnused(midiMessages);

    auto totalNumInputChannels = getTotalNumInputChannels();
    auto totalNumOutputChannels = getTotalNumOutputChannels();

    for (auto i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
        buffer.clear(i, 0, buffer.getNumSamples());

    const int numSamples = buffer.getNumSamples();
    if (numSamples <= 0)
        return;

    ensureQuantumStateSize(numSamples);

    // Immediate parameter update
    smoothedDephasing.setCurrentAndTargetValue(dephasingParam->load());
    smoothedDamping.setCurrentAndTargetValue(dampingParam->load());
    smoothedMix.setTargetValue(mixParam->load());

    int mode = static_cast<int>(modeParam->load()) + 1;

    switch (mode)
    {
    case 2: // LeftRight
        processLeftRightMode(buffer, numSamples);
        break;
    case 3: // MidSide
        processMidSideMode(buffer, numSamples);
        break;
    case 4: // TransientSustain
        processTransientSustainMode(buffer, numSamples);
        break;
    case 1: // Mono
    default:
        processMonoMode(buffer, numSamples);
        break;
    }
}

void LazirkoAudioProcessor::processMonoMode(juce::AudioBuffer<float>& buffer, int numSamples)
{
    auto totalNumInputChannels = getTotalNumInputChannels();
    auto totalNumOutputChannels = getTotalNumOutputChannels();

    // Sum to mono
    if (totalNumInputChannels == 1)
    {
        std::memcpy(dryBufferA.data(), buffer.getReadPointer(0),
            sizeof(float) * static_cast<size_t>(numSamples));
    }
    else if (totalNumInputChannels >= 2)
    {
        const float* leftIn = buffer.getReadPointer(0);
        const float* rightIn = buffer.getReadPointer(1);
        for (int n = 0; n < numSamples; ++n)
            dryBufferA[static_cast<size_t>(n)] = leftIn[n] + rightIn[n];
    }

    inputRMS = calculateRMS(dryBufferA.data(), numSamples);

    // Encode
    for (int n = 0; n < numSamples; ++n)
        quantumStateA[static_cast<size_t>(n)] = Complex(dryBufferA[static_cast<size_t>(n)], 0.0);

    // Process
    float dephase = smoothedDephasing.getCurrentValue();
    float damp = smoothedDamping.getCurrentValue();
    applyQuantumChannel(quantumStateA, dephase, damp, numSamples);

    // Decode
    for (int n = 0; n < numSamples; ++n)
        wetBufferA[static_cast<size_t>(n)] = static_cast<float>(quantumStateA[static_cast<size_t>(n)].real());

    outputRMS = calculateRMS(wetBufferA.data(), numSamples);

    // Auto-gain
    bool autoGainEnabled = (autoGainParam->load() > 0.5f);
    if (autoGainEnabled && outputRMS > 1e-6f && inputRMS > 1e-6f)
        smoothedGainCompensation.setTargetValue(juce::jlimit(0.1f, 10.0f, inputRMS / outputRMS));
    else
        smoothedGainCompensation.setTargetValue(1.0f);

    // Mix
    for (int n = 0; n < numSamples; ++n)
    {
        float gainComp = smoothedGainCompensation.getNextValue();
        float mixVal = smoothedMix.getNextValue();

        float dry = dryBufferA[static_cast<size_t>(n)];
        float wet = wetBufferA[static_cast<size_t>(n)] * gainComp;

        if (std::isnan(wet) || std::isinf(wet))
            wet = 0.0f;

        float output = dry * (1.0f - mixVal) + wet * mixVal;

    for (int ch = 0; ch < totalNumOutputChannels; ++ch)
            buffer.setSample(ch, n, output);
}
}

void LazirkoAudioProcessor::processLeftRightMode(juce::AudioBuffer<float>& buffer, int numSamples)
{
    auto totalNumInputChannels = getTotalNumInputChannels();
    auto totalNumOutputChannels = getTotalNumOutputChannels();

    if (totalNumInputChannels >= 2)
    {
        std::memcpy(dryBufferA.data(), buffer.getReadPointer(0),
            sizeof(float) * static_cast<size_t>(numSamples));
        std::memcpy(dryBufferB.data(), buffer.getReadPointer(1),
            sizeof(float) * static_cast<size_t>(numSamples));
    }
    else if (totalNumInputChannels == 1)
{
        std::memcpy(dryBufferA.data(), buffer.getReadPointer(0),
            sizeof(float) * static_cast<size_t>(numSamples));
        std::memcpy(dryBufferB.data(), buffer.getReadPointer(0),
            sizeof(float) * static_cast<size_t>(numSamples));
}

    float leftInRMS = calculateRMS(dryBufferA.data(), numSamples);
    float rightInRMS = calculateRMS(dryBufferB.data(), numSamples);
    inputRMS = (leftInRMS + rightInRMS) * 0.5f;

    // Encode
    for (int n = 0; n < numSamples; ++n)
{
        quantumStateA[static_cast<size_t>(n)] = Complex(dryBufferA[static_cast<size_t>(n)], 0.0);
        quantumStateB[static_cast<size_t>(n)] = Complex(dryBufferB[static_cast<size_t>(n)], 0.0);
}

    // Process
    float dephase = smoothedDephasing.getCurrentValue();
    float damp = smoothedDamping.getCurrentValue();
    applyQuantumChannel(quantumStateA, dephase, damp, numSamples);
    applyQuantumChannel(quantumStateB, dephase, damp, numSamples);

    // Decode
    for (int n = 0; n < numSamples; ++n)
{
        wetBufferA[static_cast<size_t>(n)] = static_cast<float>(quantumStateA[static_cast<size_t>(n)].real());
        wetBufferB[static_cast<size_t>(n)] = static_cast<float>(quantumStateB[static_cast<size_t>(n)].real());
}

    float leftOutRMS = calculateRMS(wetBufferA.data(), numSamples);
    float rightOutRMS = calculateRMS(wetBufferB.data(), numSamples);
    outputRMS = (leftOutRMS + rightOutRMS) * 0.5f;

    // Auto-gain
    bool autoGainEnabled = (autoGainParam->load() > 0.5f);
    if (autoGainEnabled && outputRMS > 1e-6f && inputRMS > 1e-6f)
        smoothedGainCompensation.setTargetValue(juce::jlimit(0.1f, 10.0f, inputRMS / outputRMS));
    else
        smoothedGainCompensation.setTargetValue(1.0f);

    // Mix
    for (int n = 0; n < numSamples; ++n)
{
        float gainComp = smoothedGainCompensation.getNextValue();
        float mixVal = smoothedMix.getNextValue();

        float dryL = dryBufferA[static_cast<size_t>(n)];
        float dryR = dryBufferB[static_cast<size_t>(n)];
        float wetL = wetBufferA[static_cast<size_t>(n)] * gainComp;
        float wetR = wetBufferB[static_cast<size_t>(n)] * gainComp;

        if (std::isnan(wetL) || std::isinf(wetL)) wetL = 0.0f;
        if (std::isnan(wetR) || std::isinf(wetR)) wetR = 0.0f;

        float outL = dryL * (1.0f - mixVal) + wetL * mixVal;
        float outR = dryR * (1.0f - mixVal) + wetR * mixVal;

        if (totalNumOutputChannels >= 2)
        {
            buffer.setSample(0, n, outL);
            buffer.setSample(1, n, outR);
        }
        else if (totalNumOutputChannels == 1)
        {
            buffer.setSample(0, n, (outL + outR) * 0.5f);
        }
    }
}

void LazirkoAudioProcessor::processMidSideMode(juce::AudioBuffer<float>& buffer, int numSamples)
{
    auto totalNumInputChannels = getTotalNumInputChannels();
    auto totalNumOutputChannels = getTotalNumOutputChannels();

    // Encode M/S
    if (totalNumInputChannels >= 2)
{
        const float* leftIn = buffer.getReadPointer(0);
        const float* rightIn = buffer.getReadPointer(1);
        for (int n = 0; n < numSamples; ++n)
    {
            float L = leftIn[n];
            float R = rightIn[n];
            dryBufferA[static_cast<size_t>(n)] = L + R;
            dryBufferB[static_cast<size_t>(n)] = L - R;
        }
    }
    else if (totalNumInputChannels == 1)
    {
        std::memcpy(dryBufferA.data(), buffer.getReadPointer(0),
            sizeof(float) * static_cast<size_t>(numSamples));
        std::memset(dryBufferB.data(), 0, sizeof(float) * static_cast<size_t>(numSamples));
}

    float midInRMS = calculateRMS(dryBufferA.data(), numSamples);
    float sideInRMS = calculateRMS(dryBufferB.data(), numSamples);
    inputRMS = (midInRMS + sideInRMS) * 0.5f;

    // Encode
    for (int n = 0; n < numSamples; ++n)
{
        quantumStateA[static_cast<size_t>(n)] = Complex(dryBufferA[static_cast<size_t>(n)], 0.0);
        quantumStateB[static_cast<size_t>(n)] = Complex(dryBufferB[static_cast<size_t>(n)], 0.0);
    }

    // Process
    float dephase = smoothedDephasing.getCurrentValue();
    float damp = smoothedDamping.getCurrentValue();
    applyQuantumChannel(quantumStateA, dephase, damp, numSamples);
    applyQuantumChannel(quantumStateB, dephase, damp, numSamples);

    // Decode
    for (int n = 0; n < numSamples; ++n)
    {
        wetBufferA[static_cast<size_t>(n)] = static_cast<float>(quantumStateA[static_cast<size_t>(n)].real());
        wetBufferB[static_cast<size_t>(n)] = static_cast<float>(quantumStateB[static_cast<size_t>(n)].real());
    }

    float midOutRMS = calculateRMS(wetBufferA.data(), numSamples);
    float sideOutRMS = calculateRMS(wetBufferB.data(), numSamples);
    outputRMS = (midOutRMS + sideOutRMS) * 0.5f;

    // Auto-gain
    bool autoGainEnabled = (autoGainParam->load() > 0.5f);
    if (autoGainEnabled && outputRMS > 1e-6f && inputRMS > 1e-6f)
        smoothedGainCompensation.setTargetValue(juce::jlimit(0.1f, 10.0f, inputRMS / outputRMS));
    else
        smoothedGainCompensation.setTargetValue(1.0f);

    // Decode M/S to L/R
    if (totalNumOutputChannels >= 2)
    {
        float* leftOut = buffer.getWritePointer(0);
        float* rightOut = buffer.getWritePointer(1);

        for (int n = 0; n < numSamples; ++n)
{
            float gainComp = smoothedGainCompensation.getNextValue();
            float mixVal = smoothedMix.getNextValue();

            float dryM = dryBufferA[static_cast<size_t>(n)];
            float dryS = dryBufferB[static_cast<size_t>(n)];
            float wetM = wetBufferA[static_cast<size_t>(n)] * gainComp;
            float wetS = wetBufferB[static_cast<size_t>(n)] * gainComp;

            if (std::isnan(wetM) || std::isinf(wetM)) wetM = 0.0f;
            if (std::isnan(wetS) || std::isinf(wetS)) wetS = 0.0f;

            float finalM = dryM * (1.0f - mixVal) + wetM * mixVal;
            float finalS = dryS * (1.0f - mixVal) + wetS * mixVal;

            leftOut[n] = (finalM + finalS) * 0.5f;
            rightOut[n] = (finalM - finalS) * 0.5f;
        }
    }
    else if (totalNumOutputChannels == 1)
    {
        float* output = buffer.getWritePointer(0);
        for (int n = 0; n < numSamples; ++n)
        {
            float gainComp = smoothedGainCompensation.getNextValue();
            float mixVal = smoothedMix.getNextValue();

            float dryM = dryBufferA[static_cast<size_t>(n)];
            float wetM = wetBufferA[static_cast<size_t>(n)] * gainComp;

            if (std::isnan(wetM) || std::isinf(wetM)) wetM = 0.0f;

            output[n] = dryM * (1.0f - mixVal) + wetM * mixVal;
        }
        }
    }

void LazirkoAudioProcessor::processTransientSustainMode(juce::AudioBuffer<float>& buffer, int numSamples)
        {
    auto totalNumInputChannels = getTotalNumInputChannels();
    auto totalNumOutputChannels = getTotalNumOutputChannels();

    const float* inL = buffer.getReadPointer(0);
    const float* inR = (totalNumInputChannels > 1) ? buffer.getReadPointer(1) : inL;

    // QUANTUM FIX: Coherent filter state preservation via Lindblad decoherence operator
    // Store filter states before first pass to maintain phase continuity
    auto hpL_state = transientFilterHP_L;
    auto hpR_state = transientFilterHP_R;
    auto lpL_state = transientFilterLP_L;
    auto lpR_state = transientFilterLP_R;

    // Split transient/sustain and store
    for (int n = 0; n < numSamples; ++n)
            {
        float l = inL[n];
        float r = inR[n];

        float lLP = transientFilterLP_L.process(l);
        float rLP = transientFilterLP_R.process(r);

        // Store sustain (for quantum processing)
        dryBufferA[static_cast<size_t>(n)] = (lLP + rLP) * 0.5f;
    }

    inputRMS = calculateRMS(dryBufferA.data(), numSamples);

    // Encode sustain
    for (int n = 0; n < numSamples; ++n)
        quantumStateA[static_cast<size_t>(n)] = Complex(dryBufferA[static_cast<size_t>(n)], 0.0);

    // Process sustain
    float dephase = smoothedDephasing.getCurrentValue();
    float damp = smoothedDamping.getCurrentValue();
    applyQuantumChannel(quantumStateA, dephase, damp, numSamples);

    // Decode
    for (int n = 0; n < numSamples; ++n)
        wetBufferA[static_cast<size_t>(n)] = static_cast<float>(quantumStateA[static_cast<size_t>(n)].real());

    outputRMS = calculateRMS(wetBufferA.data(), numSamples);

    // Auto-gain
    bool autoGainEnabled = (autoGainParam->load() > 0.5f);
    if (autoGainEnabled && outputRMS > 1e-6f && inputRMS > 1e-6f)
        smoothedGainCompensation.setTargetValue(juce::jlimit(0.1f, 10.0f, inputRMS / outputRMS));
    else
        smoothedGainCompensation.setTargetValue(1.0f);

    // QUANTUM FIX: Restore filter states instead of hard reset
    // This preserves phase coherence (superposition) across the re-processing pass
    transientFilterHP_L = hpL_state;
    transientFilterHP_R = hpR_state;
    transientFilterLP_L = lpL_state;
    transientFilterLP_R = lpR_state;

    // Reconstruct
    float* outL = buffer.getWritePointer(0);
    float* outR = (totalNumOutputChannels > 1) ? buffer.getWritePointer(1) : outL;

    for (int n = 0; n < numSamples; ++n)
    {
        float gainComp = smoothedGainCompensation.getNextValue();
        float mixVal = smoothedMix.getNextValue();

        float l = inL[n];
        float r = inR[n];

        // Re-split with preserved filter state continuity
        float lHP = transientFilterHP_L.process(l);
        float lLP = transientFilterLP_L.process(l);
        float rHP = transientFilterHP_R.process(r);
        float rLP = transientFilterLP_R.process(r);

        // Processed sustain
        float processedSustain = wetBufferA[static_cast<size_t>(n)] * gainComp;

        if (std::isnan(processedSustain) || std::isinf(processedSustain))
            processedSustain = 0.0f;

        // Mix sustain, keep transients dry
        float finalSustainL = lLP * (1.0f - mixVal) + processedSustain * mixVal;
        float finalSustainR = rLP * (1.0f - mixVal) + processedSustain * mixVal;

        outL[n] = lHP + finalSustainL;
        outR[n] = rHP + finalSustainR;
    }
    }

juce::AudioProcessorEditor* LazirkoAudioProcessor::createEditor()
{
    return new juce::GenericAudioProcessorEditor(*this);
}

bool LazirkoAudioProcessor::hasEditor() const
{
    return true;
}

void LazirkoAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
    {
    auto state = parameters.copyState();
    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    copyXmlToBinary(*xml, destData);
    }

void LazirkoAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xmlState(getXmlFromBinary(data, sizeInBytes));

    if (xmlState.get() != nullptr)
        if (xmlState->hasTagName(parameters.state.getType()))
            parameters.replaceState(juce::ValueTree::fromXml(*xmlState));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new LazirkoAudioProcessor();
}
