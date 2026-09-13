#include "PluginProcessor.h"
#include "PluginEditor.h"

SurgicalDeEsserAudioProcessor::SurgicalDeEsserAudioProcessor()
    : AudioProcessor (BusesProperties()
                        .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                        .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMETERS", createParameterLayout())
{
}

SurgicalDeEsserAudioProcessor::~SurgicalDeEsserAudioProcessor() {}

juce::AudioProcessorValueTreeState::ParameterLayout SurgicalDeEsserAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    params.push_back (std::make_unique<juce::AudioParameterFloat>(
        "xoverLow", "Inicio banda eses",
        juce::NormalisableRange<float> (1000.0f, 8000.0f, 1.0f, 0.4f), 3500.0f));

    params.push_back (std::make_unique<juce::AudioParameterFloat>(
        "xoverHigh", "Fin banda eses",
        juce::NormalisableRange<float> (4000.0f, 16000.0f, 1.0f, 0.4f), 9000.0f));

    params.push_back (std::make_unique<juce::AudioParameterFloat>(
        "threshold", "Threshold",
        juce::NormalisableRange<float> (-60.0f, 0.0f, 0.1f), -24.0f));

    params.push_back (std::make_unique<juce::AudioParameterFloat>(
        "ratio", "Ratio",
        juce::NormalisableRange<float> (1.0f, 20.0f, 0.1f, 0.5f), 4.0f));

    params.push_back (std::make_unique<juce::AudioParameterFloat>(
        "attack", "Attack",
        juce::NormalisableRange<float> (0.1f, 30.0f, 0.01f, 0.4f), 1.0f));

    params.push_back (std::make_unique<juce::AudioParameterFloat>(
        "release", "Release",
        juce::NormalisableRange<float> (10.0f, 300.0f, 0.1f, 0.4f), 60.0f));

    params.push_back (std::make_unique<juce::AudioParameterFloat>(
        "maxReduction", "Max Reduction",
        juce::NormalisableRange<float> (0.0f, 24.0f, 0.1f), 12.0f));

    params.push_back (std::make_unique<juce::AudioParameterFloat>(
        "output", "Output Gain",
        juce::NormalisableRange<float> (-12.0f, 12.0f, 0.01f), 0.0f));

    params.push_back (std::make_unique<juce::AudioParameterBool>("stereoLink", "Stereo Link", true));
    params.push_back (std::make_unique<juce::AudioParameterBool>("listen", "Listen (Solo eses)", false));
    params.push_back (std::make_unique<juce::AudioParameterBool>("bypass", "Bypass", false));

    // --- EQ de voces (procesado antes del de-esser) ---
    for (int i = 0; i < NUM_VOCAL_EQ_BANDS; ++i)
    {
        const auto& d = kVocalEqBands[i];
        params.push_back (std::make_unique<juce::AudioParameterFloat>(
            eqFreqParamId (i), juce::String (d.name) + " Freq",
            juce::NormalisableRange<float> (20.0f, 20000.0f, 0.1f, 0.3f), d.freqHz));

        params.push_back (std::make_unique<juce::AudioParameterFloat>(
            eqGainParamId (i), juce::String (d.name) + " Gain",
            juce::NormalisableRange<float> (-18.0f, 18.0f, 0.01f), d.gainDb));
    }
    params.push_back (std::make_unique<juce::AudioParameterBool>("eqBypass", "EQ Bypass", false));

    return { params.begin(), params.end() };
}

void SurgicalDeEsserAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    juce::ignoreUnused (samplesPerBlock);
    currentSampleRate = sampleRate;

    auto numChannels = getTotalNumOutputChannels();

    // ChannelBandFilters no se puede copiar (contiene juce::dsp::IIR::Filter),
    // así que se construye cada elemento en el propio vector en vez de copiarlo.
    channelFilters.clear();
    channelFilters.reserve ((size_t) numChannels);
    for (int i = 0; i < numChannels; ++i)
        channelFilters.emplace_back();

    envelopePerChannel.assign ((size_t) numChannels, 0.0f);
    envelopeLinked = 0.0f;

    for (auto& cf : channelFilters)
        cf.reset();

    juce::dsp::ProcessSpec spec;
    spec.sampleRate = sampleRate;
    spec.maximumBlockSize = (juce::uint32) samplesPerBlock;
    spec.numChannels = (juce::uint32) numChannels;
    for (auto& band : vocalEqBands)
        band.prepare (spec);

    lastXoverLow = -1.0f;
    lastXoverHigh = -1.0f;
    updateCrossoverCoefficients();
    updateVocalEqFilters();
}

void SurgicalDeEsserAudioProcessor::releaseResources() {}

bool SurgicalDeEsserAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono()
        && layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;
    return layouts.getMainOutputChannelSet() == layouts.getMainInputChannelSet();
}

void SurgicalDeEsserAudioProcessor::updateVocalEqFilters()
{
    for (int i = 0; i < NUM_VOCAL_EQ_BANDS; ++i)
    {
        const auto& d = kVocalEqBands[i];
        auto freq = apvts.getRawParameterValue (eqFreqParamId (i))->load();
        auto gain = apvts.getRawParameterValue (eqGainParamId (i))->load();
        freq = juce::jlimit (20.0f, (float) (currentSampleRate * 0.45), freq);
        auto gainLinear = juce::Decibels::decibelsToGain (gain);

        juce::dsp::IIR::Coefficients<float>::Ptr coeffs;
        switch (d.type)
        {
            case VocalEqBandType::HighPass:
                coeffs = juce::dsp::IIR::Coefficients<float>::makeHighPass (currentSampleRate, freq, d.q);
                break;
            case VocalEqBandType::HighShelf:
                coeffs = juce::dsp::IIR::Coefficients<float>::makeHighShelf (currentSampleRate, freq, d.q, gainLinear);
                break;
            case VocalEqBandType::Bell:
            default:
                coeffs = juce::dsp::IIR::Coefficients<float>::makePeakFilter (currentSampleRate, freq, d.q, gainLinear);
                break;
        }
        *vocalEqBands[(size_t) i].state = *coeffs;
    }
}

void SurgicalDeEsserAudioProcessor::updateCrossoverCoefficients()
{
    auto xLow  = apvts.getRawParameterValue ("xoverLow")->load();
    auto xHigh = apvts.getRawParameterValue ("xoverHigh")->load();

    // Asegura que High siempre esté por encima de Low (evita crossovers inválidos)
    xHigh = juce::jmax (xHigh, xLow + 200.0f);

    if (juce::approximatelyEqual (xLow, lastXoverLow) && juce::approximatelyEqual (xHigh, lastXoverHigh))
        return;

    lastXoverLow = xLow;
    lastXoverHigh = xHigh;

    constexpr float butterworthQ = 0.70710678f;

    auto lpLowCoeffs = juce::dsp::IIR::Coefficients<float>::makeLowPass (currentSampleRate, xLow, butterworthQ);
    auto hpLowCoeffs = juce::dsp::IIR::Coefficients<float>::makeHighPass (currentSampleRate, xLow, butterworthQ);
    auto lpHighCoeffs = juce::dsp::IIR::Coefficients<float>::makeLowPass (currentSampleRate, xHigh, butterworthQ);
    auto hpHighCoeffs = juce::dsp::IIR::Coefficients<float>::makeHighPass (currentSampleRate, xHigh, butterworthQ);

    for (auto& cf : channelFilters)
    {
        *cf.lowLP1.coefficients = *lpLowCoeffs;
        *cf.lowLP2.coefficients = *lpLowCoeffs;
        *cf.lowHP1.coefficients = *hpLowCoeffs;
        *cf.lowHP2.coefficients = *hpLowCoeffs;
        *cf.highLP1.coefficients = *lpHighCoeffs;
        *cf.highLP2.coefficients = *lpHighCoeffs;
        *cf.highHP1.coefficients = *hpHighCoeffs;
        *cf.highHP2.coefficients = *hpHighCoeffs;
    }
}

void SurgicalDeEsserAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    updateCrossoverCoefficients();
    updateVocalEqFilters();

    const int numChannels = buffer.getNumChannels();
    const int numSamples = buffer.getNumSamples();

    const bool bypassed = apvts.getRawParameterValue ("bypass")->load() > 0.5f;
    if (bypassed)
    {
        // Bypass total: ni el EQ ni el de-esser tocan la señal (dry real, para comparar A/B).
        currentGainReductionDb.store (0.0f);
        return;
    }

    // --- EQ de voces: se procesa PRIMERO, antes del split-band del de-esser. ---
    // Así, cualquier sibilancia que el EQ genere al subir agudos queda "aguas
    // abajo" del de-esser, que la detecta y controla igual que cualquier otra ese.
    const bool eqBypassed = apvts.getRawParameterValue ("eqBypass")->load() > 0.5f;
    if (! eqBypassed)
    {
        juce::dsp::AudioBlock<float> eqBlock (buffer);
        juce::dsp::ProcessContextReplacing<float> eqContext (eqBlock);
        for (auto& band : vocalEqBands)
            band.process (eqContext);
    }

    const bool stereoLink = apvts.getRawParameterValue ("stereoLink")->load() > 0.5f;
    const bool listenMode = apvts.getRawParameterValue ("listen")->load() > 0.5f;

    const float thresholdDb   = apvts.getRawParameterValue ("threshold")->load();
    const float ratio         = juce::jmax (1.0f, apvts.getRawParameterValue ("ratio")->load());
    const float attackMs      = apvts.getRawParameterValue ("attack")->load();
    const float releaseMs     = apvts.getRawParameterValue ("release")->load();
    const float maxReductionDb= apvts.getRawParameterValue ("maxReduction")->load();
    const float outputGainLin = juce::Decibels::decibelsToGain (apvts.getRawParameterValue ("output")->load());

    const float attackCoeff  = std::exp (-1.0f / (0.001f * attackMs  * (float) currentSampleRate));
    const float releaseCoeff = std::exp (-1.0f / (0.001f * releaseMs * (float) currentSampleRate));

    // Buffers temporales para las 3 bandas de cada canal
    std::array<float, 8> low{}, mid{}, high{}; // hasta 8 canales soportados

    float maxGrThisBlock = 0.0f;

    for (int n = 0; n < numSamples; ++n)
    {
        // --- 1) Separar bandas por canal ---
        for (int ch = 0; ch < numChannels; ++ch)
        {
            auto& cf = channelFilters[(size_t) ch];
            float x = buffer.getSample (ch, n);

            float lowBand = cf.lowLP2.processSample (cf.lowLP1.processSample (x));
            float aboveLow = cf.lowHP2.processSample (cf.lowHP1.processSample (x));
            float midBand = cf.highLP2.processSample (cf.highLP1.processSample (aboveLow));
            float highBand = cf.highHP2.processSample (cf.highHP1.processSample (aboveLow));

            low[(size_t) ch] = lowBand;
            mid[(size_t) ch] = midBand;
            high[(size_t) ch] = highBand;
        }

        // --- 2) Detección + reducción de ganancia, SOLO sobre la banda de eses ---
        float gainReductionDb = 0.0f;

        if (stereoLink)
        {
            float linkLevel = 0.0f;
            for (int ch = 0; ch < numChannels; ++ch)
                linkLevel = juce::jmax (linkLevel, std::abs (mid[(size_t) ch]));

            float coeff = (linkLevel > envelopeLinked) ? attackCoeff : releaseCoeff;
            envelopeLinked = coeff * envelopeLinked + (1.0f - coeff) * linkLevel;

            float levelDb = juce::Decibels::gainToDecibels (envelopeLinked, -100.0f);
            if (levelDb > thresholdDb)
                gainReductionDb = (levelDb - thresholdDb) * (1.0f - 1.0f / ratio);
            gainReductionDb = juce::jmin (gainReductionDb, maxReductionDb);

            float gainLin = juce::Decibels::decibelsToGain (-gainReductionDb);
            for (int ch = 0; ch < numChannels; ++ch)
                mid[(size_t) ch] *= gainLin;

            maxGrThisBlock = juce::jmax (maxGrThisBlock, gainReductionDb);
        }
        else
        {
            for (int ch = 0; ch < numChannels; ++ch)
            {
                float level = std::abs (mid[(size_t) ch]);
                float& env = envelopePerChannel[(size_t) ch];
                float coeff = (level > env) ? attackCoeff : releaseCoeff;
                env = coeff * env + (1.0f - coeff) * level;

                float levelDb = juce::Decibels::gainToDecibels (env, -100.0f);
                float chGrDb = 0.0f;
                if (levelDb > thresholdDb)
                    chGrDb = (levelDb - thresholdDb) * (1.0f - 1.0f / ratio);
                chGrDb = juce::jmin (chGrDb, maxReductionDb);

                mid[(size_t) ch] *= juce::Decibels::decibelsToGain (-chGrDb);
                maxGrThisBlock = juce::jmax (maxGrThisBlock, chGrDb);
            }
        }

        // --- 3) Recombinar bandas (graves y agudos intactos siempre) ---
        float monoSumForScope = 0.0f;
        for (int ch = 0; ch < numChannels; ++ch)
        {
            float sample = listenMode
                ? mid[(size_t) ch]
                : (low[(size_t) ch] + mid[(size_t) ch] + high[(size_t) ch]);

            sample *= outputGainLin;
            buffer.setSample (ch, n, sample);
            monoSumForScope += sample;
        }

        pushNextSampleIntoFifo (numChannels > 0 ? monoSumForScope / (float) numChannels : 0.0f);
    }

    currentGainReductionDb.store (maxGrThisBlock);
}

void SurgicalDeEsserAudioProcessor::pushNextSampleIntoFifo (float sample) noexcept
{
    if (fifoIndex == fftSize)
    {
        if (! nextFFTBlockReady.load())
        {
            std::fill (fftData.begin(), fftData.end(), 0.0f);
            std::copy (fifo.begin(), fifo.end(), fftData.begin());
            nextFFTBlockReady.store (true);
        }
        fifoIndex = 0;
    }
    fifo[(size_t) fifoIndex++] = sample;
}

void SurgicalDeEsserAudioProcessor::computeNextSpectrumFrame()
{
    windowFn.multiplyWithWindowingTable (fftData.data(), fftSize);
    forwardFFT.performFrequencyOnlyForwardTransform (fftData.data());

    constexpr float minDb = -100.0f;
    constexpr float maxDb = 0.0f;

    for (int i = 0; i < scopeSize; ++i)
    {
        auto magnitude = fftData[(size_t) i];
        auto levelDb = juce::Decibels::gainToDecibels (magnitude, minDb);
        auto level01 = juce::jmap (levelDb, minDb, maxDb, 0.0f, 1.0f);
        scopeData[(size_t) i] = juce::jlimit (0.0f, 1.0f, level01);
    }

    nextFFTBlockReady.store (false);
}

juce::AudioProcessorEditor* SurgicalDeEsserAudioProcessor::createEditor()
{
    return new SurgicalDeEsserAudioProcessorEditor (*this);
}

void SurgicalDeEsserAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml (state.createXml());
    copyXmlToBinary (*xml, destData);
}

void SurgicalDeEsserAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml (getXmlFromBinary (data, sizeInBytes));
    if (xml != nullptr && xml->hasTagName (apvts.state.getType()))
        apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SurgicalDeEsserAudioProcessor();
}
