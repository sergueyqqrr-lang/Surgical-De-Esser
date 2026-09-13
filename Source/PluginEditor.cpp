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
    ProLookAndFeel::drawInsetScreen (g, r, 6.0f);

    float grDb = proc.currentGainReductionDb.load();
    float maxShow = 24.0f;
    float frac = juce::jlimit (0.0f, 1.0f, grDb / maxShow);

    auto barArea = r.reduced (5.0f);
    auto barHeight = barArea.getHeight() * frac;
    auto bar = barArea.removeFromBottom (barHeight);

    juce::ColourGradient barGrad (Palette::red.brighter (0.3f), bar.getX(), bar.getY(),
                                   Palette::red.darker (0.3f), bar.getX(), bar.getBottom(), false);
    g.setGradientFill (barGrad);
    g.fillRoundedRectangle (bar, 3.0f);
    g.setColour (Palette::red.withAlpha (0.6f));
    g.drawRoundedRectangle (bar, 3.0f, 0.8f);

    g.setColour (Palette::text.withAlpha (0.85f));
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

float SpectrumAnalyzer::xToFreq (float x, float width) const
{
    constexpr float minFreq = 20.0f, maxFreq = 20000.0f;
    auto logMin = std::log10 (minFreq);
    auto logMax = std::log10 (maxFreq);
    auto frac = juce::jlimit (0.0f, 1.0f, width > 0.0f ? x / width : 0.0f);
    auto logF = logMin + frac * (logMax - logMin);
    return std::pow (10.0f, logF);
}

SpectrumAnalyzer::EdgeTarget nearestEdge (float mouseX, float sx1, float sx2, float radius)
{
    auto dLow = std::abs (mouseX - sx1);
    auto dHigh = std::abs (mouseX - sx2);
    if (dLow <= radius && dLow <= dHigh) return SpectrumAnalyzer::EdgeTarget::low;
    if (dHigh <= radius) return SpectrumAnalyzer::EdgeTarget::high;
    return SpectrumAnalyzer::EdgeTarget::none;
}

void SpectrumAnalyzer::mouseMove (const juce::MouseEvent& e)
{
    auto width = (float) getWidth();
    auto sx1 = freqToX (proc.apvts.getRawParameterValue ("xoverLow")->load(), width);
    auto sx2 = freqToX (proc.apvts.getRawParameterValue ("xoverHigh")->load(), width);

    auto newHover = nearestEdge (e.position.x, sx1, sx2, grabRadiusPx);
    if (newHover != hovering)
    {
        hovering = newHover;
        setMouseCursor (hovering == EdgeTarget::none
                            ? juce::MouseCursor::NormalCursor
                            : juce::MouseCursor::LeftRightResizeCursor);
        repaint();
    }
}

void SpectrumAnalyzer::mouseExit (const juce::MouseEvent&)
{
    hovering = EdgeTarget::none;
    setMouseCursor (juce::MouseCursor::NormalCursor);
    repaint();
}

void SpectrumAnalyzer::mouseDown (const juce::MouseEvent& e)
{
    auto width = (float) getWidth();
    auto sx1 = freqToX (proc.apvts.getRawParameterValue ("xoverLow")->load(), width);
    auto sx2 = freqToX (proc.apvts.getRawParameterValue ("xoverHigh")->load(), width);

    dragging = nearestEdge (e.position.x, sx1, sx2, grabRadiusPx);

    if (dragging == EdgeTarget::low)
    {
        if (auto* param = proc.apvts.getParameter ("xoverLow"))
            param->beginChangeGesture();
    }
    else if (dragging == EdgeTarget::high)
    {
        if (auto* param = proc.apvts.getParameter ("xoverHigh"))
            param->beginChangeGesture();
    }
}

void SpectrumAnalyzer::mouseDrag (const juce::MouseEvent& e)
{
    if (dragging == EdgeTarget::none)
        return;

    auto width = (float) getWidth();
    auto freq = xToFreq (e.position.x, width);

    if (dragging == EdgeTarget::low)
    {
        auto xHigh = proc.apvts.getRawParameterValue ("xoverHigh")->load();
        freq = juce::jlimit (1000.0f, xHigh - 200.0f, freq);
        if (auto* param = proc.apvts.getParameter ("xoverLow"))
            param->setValueNotifyingHost (param->convertTo0to1 (freq));
    }
    else if (dragging == EdgeTarget::high)
    {
        auto xLow = proc.apvts.getRawParameterValue ("xoverLow")->load();
        freq = juce::jlimit (xLow + 200.0f, 16000.0f, freq);
        if (auto* param = proc.apvts.getParameter ("xoverHigh"))
            param->setValueNotifyingHost (param->convertTo0to1 (freq));
    }
}

void SpectrumAnalyzer::mouseUp (const juce::MouseEvent&)
{
    if (dragging == EdgeTarget::low)
    {
        if (auto* param = proc.apvts.getParameter ("xoverLow"))
            param->endChangeGesture();
    }
    else if (dragging == EdgeTarget::high)
    {
        if (auto* param = proc.apvts.getParameter ("xoverHigh"))
            param->endChangeGesture();
    }
    dragging = EdgeTarget::none;
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
    ProLookAndFeel::drawInsetScreen (g, bounds, 8.0f);
    bounds = bounds.reduced (3.0f); // queda dentro del bisel dibujado por drawInsetScreen

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

    // Línea "Inicio eses": se resalta al pasar el mouse cerca o al arrastrarla
    bool lowActive = (hovering == EdgeTarget::low || dragging == EdgeTarget::low);
    g.setColour (Palette::cyan.withAlpha (lowActive ? 0.9f : 0.4f));
    g.drawVerticalLine ((int) sx1, bounds.getY(), bounds.getBottom());
    if (lowActive)
        g.fillRoundedRectangle (sx1 - 1.5f, bounds.getY(), 3.0f, bounds.getHeight(), 1.5f);

    // Línea "Fin eses"
    bool highActive = (hovering == EdgeTarget::high || dragging == EdgeTarget::high);
    g.setColour (Palette::cyan.withAlpha (highActive ? 0.9f : 0.4f));
    g.drawVerticalLine ((int) sx2, bounds.getY(), bounds.getBottom());
    if (highActive)
        g.fillRoundedRectangle (sx2 - 1.5f, bounds.getY(), 3.0f, bounds.getHeight(), 1.5f);

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
    g.setColour (Palette::cyan.withAlpha (0.25f));
    g.strokePath (curve, juce::PathStrokeType (4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour (Palette::cyan);
    g.strokePath (curve, juce::PathStrokeType (1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // Barra de reducción de ganancia activa, dentro de la banda resaltada
    auto grDb = proc.currentGainReductionDb.load();
    if (grDb > 0.1f)
    {
        auto grFrac = juce::jlimit (0.0f, 1.0f, grDb / 24.0f);
        g.setColour (Palette::red.withAlpha (0.55f));
        g.fillRect (juce::Rectangle<float> (sx1, bounds.getY(), sx2 - sx1, grFrac * 14.0f));
    }
}

// ============================================================================
// EQ de voces
// ============================================================================
VocalEqBandControl::VocalEqBandControl (SurgicalDeEsserAudioProcessor& proc, int bandIndex)
{
    const auto& d = kVocalEqBands[bandIndex];

    nameLabel.setText (d.name, juce::dontSendNotification);
    nameLabel.setJustificationType (juce::Justification::centred);
    nameLabel.setColour (juce::Label::textColourId, Palette::text);
    nameLabel.setFont (juce::Font (12.0f, juce::Font::bold));
    addAndMakeVisible (nameLabel);

    auto setupKnob = [this] (juce::Slider& s)
    {
        s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 56, 15);
        s.setColour (juce::Slider::rotarySliderFillColourId, Palette::cyan);
        addAndMakeVisible (s);
    };
    setupKnob (freqSlider);
    setupKnob (gainSlider);

    freqAttach = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        proc.apvts, SurgicalDeEsserAudioProcessor::eqFreqParamId (bandIndex), freqSlider);
    gainAttach = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        proc.apvts, SurgicalDeEsserAudioProcessor::eqGainParamId (bandIndex), gainSlider);
}

void VocalEqBandControl::paint (juce::Graphics& g)
{
    ProLookAndFeel::drawRaisedPanel (g, getLocalBounds().toFloat().reduced (2.0f), 6.0f);
}

void VocalEqBandControl::resized()
{
    auto r = getLocalBounds().reduced (6);
    nameLabel.setBounds (r.removeFromTop (16));
    auto knobs = r;
    freqSlider.setBounds (knobs.removeFromLeft (knobs.getWidth() / 2));
    gainSlider.setBounds (knobs);
}

VocalEqPanel::VocalEqPanel (SurgicalDeEsserAudioProcessor& proc)
{
    for (int i = 0; i < NUM_VOCAL_EQ_BANDS; ++i)
    {
        auto* band = new VocalEqBandControl (proc, i);
        bands.add (band);
        addAndMakeVisible (band);
    }

    eqBypassButton.setColour (juce::ToggleButton::textColourId, Palette::text);
    eqBypassButton.setColour (juce::ToggleButton::tickColourId, Palette::red);
    addAndMakeVisible (eqBypassButton);

    eqBypassAttach = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        proc.apvts, "eqBypass", eqBypassButton);
}

void VocalEqPanel::paint (juce::Graphics&) {}

void VocalEqPanel::resized()
{
    auto r = getLocalBounds();
    eqBypassButton.setBounds (r.removeFromBottom (26).removeFromLeft (140));
    r.removeFromBottom (6);

    auto bandWidth = r.getWidth() / bands.size();
    for (int i = 0; i < bands.size(); ++i)
        bands[i]->setBounds (i * bandWidth, r.getY(), bandWidth - 6, r.getHeight());
}

SurgicalDeEsserAudioProcessorEditor::SurgicalDeEsserAudioProcessorEditor (SurgicalDeEsserAudioProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p), grMeter (p), spectrum (p), vocalEqPanel (p)
{
    setLookAndFeel (&proLookAndFeel);

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

    eqToggleButton.setColour (juce::TextButton::buttonColourId, Palette::panel);
    eqToggleButton.setColour (juce::TextButton::textColourOffId, Palette::cyan);
    addAndMakeVisible (eqToggleButton);
    addChildComponent (vocalEqPanel); // se hace visible solo cuando está expandido
    eqToggleButton.onClick = [this]
    {
        eqExpanded = ! eqExpanded;
        updateEqPanelVisibility();
    };

    updateEqPanelVisibility(); // fija el texto del botón y el tamaño inicial (plegado)
}

SurgicalDeEsserAudioProcessorEditor::~SurgicalDeEsserAudioProcessorEditor()
{
    setLookAndFeel (nullptr);
}

void SurgicalDeEsserAudioProcessorEditor::updateEqPanelVisibility()
{
    vocalEqPanel.setVisible (eqExpanded);
    eqToggleButton.setButtonText (eqExpanded ? juce::String (juce::CharPointer_UTF8 ("VOCAL EQ  \xe2\x96\xb4  (ocultar)"))
                                              : juce::String (juce::CharPointer_UTF8 ("VOCAL EQ  \xe2\x96\xbe  (mostrar)")));

    constexpr int baseHeight = 560;
    constexpr int eqHeaderHeight = 36;
    constexpr int eqPanelHeight = 180;
    setSize (760, baseHeight + eqHeaderHeight + (eqExpanded ? eqPanelHeight : 0));
}

void SurgicalDeEsserAudioProcessorEditor::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();

    // Fondo con degradado sutil (más claro arriba, más oscuro abajo) para dar profundidad
    juce::ColourGradient bgGrad (Palette::bg.brighter (0.06f), bounds.getX(), bounds.getY(),
                                  Palette::bg.darker (0.35f), bounds.getX(), bounds.getBottom(), false);
    g.setGradientFill (bgGrad);
    g.fillAll();

    // Viñeta: oscurece las esquinas para que el centro (los controles) resalte más
    juce::ColourGradient vignette (juce::Colours::transparentBlack, bounds.getCentreX(), bounds.getCentreY(),
                                    juce::Colours::black.withAlpha (0.45f), bounds.getX(), bounds.getY(), true);
    vignette.isRadial = true;
    g.setGradientFill (vignette);
    g.fillAll();

    // Placa elevada detrás de las perillas (efecto chasis de hardware)
    if (! knobsPanelBounds.isEmpty())
        ProLookAndFeel::drawRaisedPanel (g, knobsPanelBounds.toFloat(), 10.0f);
}

void SurgicalDeEsserAudioProcessorEditor::resized()
{
    auto full = getLocalBounds();
    auto mainArea = full.removeFromTop (560);

    auto r = mainArea.reduced (14);
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
    knobsPanelBounds = r;
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

    // --- Sección plegable del EQ de voces (debajo del área principal fija) ---
    auto eqHeaderArea = full.removeFromTop (36).reduced (14, 4);
    eqToggleButton.setBounds (eqHeaderArea);

    if (eqExpanded)
        vocalEqPanel.setBounds (full.reduced (14, 4));
}
