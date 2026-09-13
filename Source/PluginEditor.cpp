#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace Palette
{
    const juce::Colour bg      (0xff14181d);
    const juce::Colour panel   (0xff1e252c);
    const juce::Colour cyan    (0xff4fd6c8);
    const juce::Colour red     (0xffe0524a);
    const juce::Colour text    (0xffe4ecef);
}

void GrMeter::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (Palette::panel);
    g.fillRoundedRectangle (r, 6.0f);

    float grDb = proc.currentGainReductionDb.load();
    float maxShow = 24.0f;
    float frac = juce::jlimit (0.0f, 1.0f, grDb / maxShow);

    auto barArea = r.reduced (4.0f);
    auto barHeight = barArea.getHeight() * frac;
    auto bar = barArea.removeFromBottom (barHeight);
    g.setColour (Palette::red);
    g.fillRoundedRectangle (bar, 3.0f);

    g.setColour (Palette::text);
    g.setFont (11.0f);
    g.drawText (juce::String (grDb, 1) + " dB", getLocalBounds(), juce::Justification::centredBottom);
}

float SpectrumAnalyzer::freqToX (float freqHz, float width) const
{
    constexpr float minFreq = 20.0f, maxFreq = 20000.0f;
    auto logMin = std::log10 (minFreq);
    auto logMax = std::log10 (maxFreq);
    auto logF = std::log10 (juce::jlimit (minFreq, maxFreq, freqHz));
    return width * (logF - logMin) / (logMax - logMin);
}

void SpectrumAnalyzer::timerCallback()
{
    // Si hay un frame de FFT nuevo, se calcula (esto solo actualiza los datos crudos).
    if (proc.isNextFFTBlockReady())
        proc.computeNextSpectrumFrame();

    // El suavizado corre en CADA tick del timer, no solo cuando hay datos nuevos,
    // para que la caída (fall-off) se vea como un movimiento continuo y no a saltos.
    const auto& target = proc.getScopeData();
    for (size_t i = 0; i < smoothedData.size(); ++i)
    {
        float coeff = (target[i] > smoothedData[i]) ? attackCoeff : releaseCoeff;
        smoothedData[i] = coeff * smoothedData[i] + (1.0f - coeff) * target[i];
    }

    repaint();
}

void SpectrumAnalyzer::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    g.setColour (Palette::panel);
    g.fillRoundedRectangle (bounds, 6.0f);

    auto sr = proc.getSampleRate();
    if (sr <= 0.0) sr = 44100.0;

    constexpr float minDb = -100.0f;
    constexpr float maxDb = 0.0f;
    auto dbToY = [&bounds, minDb, maxDb] (float db)
    {
        auto level01 = juce::jmap (db, minDb, maxDb, 0.0f, 1.0f);
        return bounds.getBottom() - level01 * bounds.getHeight();
    };

    // Líneas y etiquetas de dB (eje vertical)
    g.setFont (10.0f);
    for (float db = maxDb; db >= minDb; db -= 20.0f)
    {
        auto y = dbToY (db);
        g.setColour (Palette::text.withAlpha (db == 0.0f ? 0.0f : 0.10f));
        g.drawHorizontalLine ((int) y, bounds.getX(), bounds.getRight());

        g.setColour (Palette::text.withAlpha (0.4f));
        g.drawText (juce::String ((int) db), (int) bounds.getX() + 2, (int) y - 12, 40, 12, juce::Justification::left);
    }

    // Líneas de referencia de frecuencia (100Hz, 1kHz, 10kHz)
    g.setColour (Palette::text.withAlpha (0.15f));
    for (float f : { 100.0f, 1000.0f, 10000.0f })
    {
        auto x = bounds.getX() + freqToX (f, bounds.getWidth());
        g.drawVerticalLine ((int) x, bounds.getY(), bounds.getBottom());
    }
    g.setColour (Palette::text.withAlpha (0.35f));
    g.setFont (10.0f);
    g.drawText ("100", (int) (bounds.getX() + freqToX (100.0f, bounds.getWidth())) - 12, (int) bounds.getBottom() - 12, 30, 12, juce::Justification::left);
    g.drawText ("1k",  (int) (bounds.getX() + freqToX (1000.0f, bounds.getWidth())) - 10, (int) bounds.getBottom() - 12, 30, 12, juce::Justification::left);
    g.drawText ("10k", (int) (bounds.getX() + freqToX (10000.0f, bounds.getWidth())) - 12, (int) bounds.getBottom() - 12, 30, 12, juce::Justification::left);

    // Resalta la banda de eses (Inicio/Fin eses) igual que hacen Pro-Q/Pro-DS con la banda activa
    auto xLow = proc.apvts.getRawParameterValue ("xoverLow")->load();
    auto xHigh = proc.apvts.getRawParameterValue ("xoverHigh")->load();
    auto sx1 = bounds.getX() + freqToX (xLow, bounds.getWidth());
    auto sx2 = bounds.getX() + freqToX (xHigh, bounds.getWidth());
    g.setColour (Palette::cyan.withAlpha (0.10f));
    g.fillRect (juce::Rectangle<float> (sx1, bounds.getY(), sx2 - sx1, bounds.getHeight()));
    g.setColour (Palette::cyan.withAlpha (0.4f));
    g.drawVerticalLine ((int) sx1, bounds.getY(), bounds.getBottom());
    g.drawVerticalLine ((int) sx2, bounds.getY(), bounds.getBottom());

    // Curva del espectro en vivo (ya suavizada con ataque/caída, ver timerCallback)
    const auto& scope = smoothedData;
    juce::Path curve;
    bool started = false;
    for (int i = 1; i < (int) scope.size(); ++i)
    {
        auto freq = (float) i * (float) sr / (float) SurgicalDeEsserAudioProcessor::fftSize;
        if (freq < 20.0f || freq > 20000.0f)
            continue;

        auto x = bounds.getX() + freqToX (freq, bounds.getWidth());
        auto y = bounds.getBottom() - scope[(size_t) i] * bounds.getHeight();

        if (! started) { curve.startNewSubPath (x, y); started = true; }
        else curve.lineTo (x, y);
    }
    g.setColour (Palette::cyan);
    g.strokePath (curve, juce::PathStrokeType (1.5f));

    // Barra de reducción de ganancia activa, dentro de la banda resaltada
    auto grDb = proc.currentGainReductionDb.load();
    if (grDb > 0.1f)
    {
        auto grFrac = juce::jlimit (0.0f, 1.0f, grDb / 24.0f);
        g.setColour (Palette::red.withAlpha (0.55f));
        g.fillRect (juce::Rectangle<float> (sx1, bounds.getY(), sx2 - sx1, grFrac * 14.0f));
    }
}

SurgicalDeEsserAudioProcessorEditor::SurgicalDeEsserAudioProcessorEditor (SurgicalDeEsserAudioProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p), grMeter (p), spectrum (p)
{
    title.setText ("SURGICAL DE-ESSER", juce::dontSendNotification);
    title.setFont (juce::Font (20.0f, juce::Font::bold));
    title.setColour (juce::Label::textColourId, Palette::cyan);
    addAndMakeVisible (title);
    addAndMakeVisible (grMeter);
    addAndMakeVisible (spectrum);

    auto setupKnob = [this] (juce::Slider& s, juce::Label& l, const juce::String& text)
    {
        l.setText (text, juce::dontSendNotification);
        l.setJustificationType (juce::Justification::centred);
        l.setColour (juce::Label::textColourId, Palette::text);
        addAndMakeVisible (l);

        s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 70, 16);
        s.setColour (juce::Slider::rotarySliderFillColourId, Palette::cyan);
        addAndMakeVisible (s);
    };

    setupKnob (xoverLowSlider, xoverLowLabel, "INICIO ESES (Hz)");
    setupKnob (xoverHighSlider, xoverHighLabel, "FIN ESES (Hz)");
    setupKnob (thresholdSlider, thresholdLabel, "THRESHOLD");
    setupKnob (ratioSlider, ratioLabel, "RATIO");
    setupKnob (attackSlider, attackLabel, "ATTACK");
    setupKnob (releaseSlider, releaseLabel, "RELEASE");
    setupKnob (maxReductionSlider, maxReductionLabel, "MAX CUT (dB)");
    setupKnob (outputSlider, outputLabel, "OUTPUT");

    for (auto* btn : { &stereoLinkButton, &listenButton, &bypassButton })
    {
        btn->setColour (juce::ToggleButton::textColourId, Palette::text);
        btn->setColour (juce::ToggleButton::tickColourId, Palette::cyan);
        addAndMakeVisible (btn);
    }

    auto& apvts = p.apvts;
    xoverLowAttach = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (apvts, "xoverLow", xoverLowSlider);
    xoverHighAttach = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (apvts, "xoverHigh", xoverHighSlider);
    thresholdAttach = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (apvts, "threshold", thresholdSlider);
    ratioAttach = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (apvts, "ratio", ratioSlider);
    attackAttach = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (apvts, "attack", attackSlider);
    releaseAttach = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (apvts, "release", releaseSlider);
    maxReductionAttach = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (apvts, "maxReduction", maxReductionSlider);
    outputAttach = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (apvts, "output", outputSlider);

    stereoLinkAttach = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (apvts, "stereoLink", stereoLinkButton);
    listenAttach = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (apvts, "listen", listenButton);
    bypassAttach = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (apvts, "bypass", bypassButton);

    setSize (760, 560);
}

void SurgicalDeEsserAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (Palette::bg);
}

void SurgicalDeEsserAudioProcessorEditor::resized()
{
    auto r = getLocalBounds().reduced (14);
    auto topRow = r.removeFromTop (30);
    title.setBounds (topRow.removeFromLeft (300));
    grMeter.setBounds (topRow.removeFromRight (50));

    r.removeFromTop (10);

    spectrum.setBounds (r.removeFromTop (200));

    r.removeFromTop (14);

    auto toggles = r.removeFromBottom (30);
    auto tw = toggles.getWidth() / 3;
    stereoLinkButton.setBounds (toggles.removeFromLeft (tw));
    listenButton.setBounds (toggles.removeFromLeft (tw));
    bypassButton.setBounds (toggles);

    r.removeFromBottom (10);

    const int numKnobs = 8;
    auto knobWidth = r.getWidth() / numKnobs;

    auto layoutKnob = [&r, knobWidth] (juce::Label& l, juce::Slider& s)
    {
        auto col = r.removeFromLeft (knobWidth);
        l.setBounds (col.removeFromTop (16));
        s.setBounds (col.reduced (4));
    };

    layoutKnob (xoverLowLabel, xoverLowSlider);
    layoutKnob (xoverHighLabel, xoverHighSlider);
    layoutKnob (thresholdLabel, thresholdSlider);
    layoutKnob (ratioLabel, ratioSlider);
    layoutKnob (attackLabel, attackSlider);
    layoutKnob (releaseLabel, releaseSlider);
    layoutKnob (maxReductionLabel, maxReductionSlider);
    layoutKnob (outputLabel, outputSlider);
}
