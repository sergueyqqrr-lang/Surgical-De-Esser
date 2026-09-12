#pragma once
#include "PluginProcessor.h"

class GrMeter : public juce::Component, private juce::Timer
{
public:
    explicit GrMeter (SurgicalDeEsserAudioProcessor& p) : proc (p) { startTimerHz (30); }
    void paint (juce::Graphics& g) override;
private:
    void timerCallback() override { repaint(); }
    SurgicalDeEsserAudioProcessor& proc;
};

class SpectrumAnalyzer : public juce::Component, private juce::Timer
{
public:
    explicit SpectrumAnalyzer (SurgicalDeEsserAudioProcessor& p) : proc (p) { startTimerHz (30); }
    void paint (juce::Graphics& g) override;

private:
    void timerCallback() override;
    float freqToX (float freqHz, float width) const;

    SurgicalDeEsserAudioProcessor& proc;

    // Suavizado tipo medidor de picos: sube rápido (ataque), baja lento (fall-off),
    // igual que hacen los analizadores de FabFilter en vez de mostrar el valor crudo.
    std::array<float, SurgicalDeEsserAudioProcessor::scopeSize> smoothedData {};
    static constexpr float attackCoeff  = 0.35f; // más bajo = sube más rápido
    static constexpr float releaseCoeff = 0.90f; // más alto = cae más lento
};

class SurgicalDeEsserAudioProcessorEditor : public juce::AudioProcessorEditor
{
public:
    explicit SurgicalDeEsserAudioProcessorEditor (SurgicalDeEsserAudioProcessor&);

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    SurgicalDeEsserAudioProcessor& audioProcessor;

    juce::Label title;
    GrMeter grMeter;
    SpectrumAnalyzer spectrum;

    juce::Label xoverLowLabel, xoverHighLabel, thresholdLabel, ratioLabel,
                attackLabel, releaseLabel, maxReductionLabel, outputLabel;

    juce::Slider xoverLowSlider, xoverHighSlider, thresholdSlider, ratioSlider,
                 attackSlider, releaseSlider, maxReductionSlider, outputSlider;

    juce::ToggleButton stereoLinkButton { "Stereo Link" };
    juce::ToggleButton listenButton { "Listen (solo eses)" };
    juce::ToggleButton bypassButton { "Bypass" };

    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>
        xoverLowAttach, xoverHighAttach, thresholdAttach, ratioAttach,
        attackAttach, releaseAttach, maxReductionAttach, outputAttach;

    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>
        stereoLinkAttach, listenAttach, bypassAttach;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SurgicalDeEsserAudioProcessorEditor)
};
