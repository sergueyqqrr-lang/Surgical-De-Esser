#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>

// ============================================================================
// Surgical De-Esser
//
// Arquitectura split-band: la señal se separa en 3 bandas con crossovers
// Linkwitz-Riley (4to orden, magnitud perfectamente plana al sumar).
// Solo la banda media (rango de eses) recibe reducción de ganancia dinámica,
// y solo cuando hay sibilancia real por encima del umbral. Graves y agudos
// pasan siempre intactos, bit-transparentes.
// ============================================================================

struct ChannelBandFilters
{
    ChannelBandFilters() = default;

    // juce::dsp::IIR::Filter no se puede copiar, solo mover.
    ChannelBandFilters (const ChannelBandFilters&) = delete;
    ChannelBandFilters& operator= (const ChannelBandFilters&) = delete;
    ChannelBandFilters (ChannelBandFilters&&) = default;
    ChannelBandFilters& operator= (ChannelBandFilters&&) = default;

    // Cascada 2x Butterworth (Q=0.7071) = Linkwitz-Riley 4to orden
    juce::dsp::IIR::Filter<float> lowLP1, lowLP2;     // -> banda grave (< xoverLow)
    juce::dsp::IIR::Filter<float> lowHP1, lowHP2;     // -> todo lo que está por encima de xoverLow
    juce::dsp::IIR::Filter<float> highLP1, highLP2;   // (aplicado a "aboveLow") -> banda de eses
    juce::dsp::IIR::Filter<float> highHP1, highHP2;   // (aplicado a "aboveLow") -> banda aguda (> xoverHigh)

    void reset()
    {
        lowLP1.reset(); lowLP2.reset();
        lowHP1.reset(); lowHP2.reset();
        highLP1.reset(); highLP2.reset();
        highHP1.reset(); highHP2.reset();
    }
};

// ============================================================================
// EQ de voces (panel plegable en la interfaz).
//
// IMPORTANTE: este EQ se procesa ANTES del split-band del de-esser
// (ver processBlock). Así, si subes agudos aquí y eso genera más sibilancia,
// el de-esser (que corre después) la vuelve a detectar y controlar
// automáticamente — nunca "revive" eses que ya habían sido cortadas.
// ============================================================================

constexpr int NUM_VOCAL_EQ_BANDS = 6;

enum class VocalEqBandType { Bell, HighPass, HighShelf };

struct VocalEqBandDefaults
{
    float freqHz;
    float gainDb;
    float q;
    VocalEqBandType type;
    const char* name;
};

static constexpr VocalEqBandDefaults kVocalEqBands[NUM_VOCAL_EQ_BANDS] = {
    { 100.0f,   0.0f, 0.7f, VocalEqBandType::HighPass,  "Rumble"    },
    { 250.0f,   0.0f, 1.0f, VocalEqBandType::Bell,      "Warmth"    },
    { 500.0f,   0.0f, 1.0f, VocalEqBandType::Bell,      "Mud"       },
    { 3000.0f,  0.0f, 1.0f, VocalEqBandType::Bell,      "Presence"  },
    { 6000.0f,  0.0f, 1.0f, VocalEqBandType::Bell,      "Harshness" },
    { 11000.0f, 0.0f, 0.7f, VocalEqBandType::HighShelf, "Air"       }
};

class SurgicalDeEsserAudioProcessor : public juce::AudioProcessor
{
public:
    SurgicalDeEsserAudioProcessor();
    ~SurgicalDeEsserAudioProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Surgical De-Esser"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;

    // Medidor de reducción de ganancia para mostrar en la UI (dB, positivo = cuánto se está cortando)
    std::atomic<float> currentGainReductionDb { 0.0f };

    // ---- Analizador de espectro (FFT) para la interfaz ----
    static constexpr int fftOrder = 11;
    static constexpr int fftSize = 1 << fftOrder;   // 2048
    static constexpr int scopeSize = fftSize / 2;   // bins de magnitud útiles

    bool isNextFFTBlockReady() const noexcept { return nextFFTBlockReady.load(); }
    void computeNextSpectrumFrame();
    const std::array<float, scopeSize>& getScopeData() const noexcept { return scopeData; }

    static juce::String eqFreqParamId (int band) { return "eq" + juce::String (band) + "_freq"; }
    static juce::String eqGainParamId (int band) { return "eq" + juce::String (band) + "_gain"; }
    static juce::String eqBandOnParamId (int band) { return "eq" + juce::String (band) + "_on"; }

    // Respuesta acumulada del EQ de voces (en dB) para una frecuencia dada,
    // respetando EQ Bypass y el on/off de cada banda. Usado por el analizador
    // de espectro para dibujar la curva del EQ superpuesta.
    float getEqResponseDb (float frequency) const;

private:
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    void updateCrossoverCoefficients();
    void updateVocalEqFilters();
    void pushNextSampleIntoFifo (float sample) noexcept;
    juce::dsp::IIR::Coefficients<float>::Ptr computeBandCoefficients (int bandIndex) const;

    std::vector<ChannelBandFilters> channelFilters;

    // EQ de voces: 6 bandas en serie, procesadas ANTES del split-band (ver arriba).
    std::array<juce::dsp::ProcessorDuplicator<juce::dsp::IIR::Filter<float>,
                                               juce::dsp::IIR::Coefficients<float>>, NUM_VOCAL_EQ_BANDS> vocalEqBands;

    // Envolvente / compresor de la banda de eses
    float envelopeLinked = 0.0f;               // usado si Stereo Link = ON
    std::vector<float> envelopePerChannel;     // usado si Stereo Link = OFF

    double currentSampleRate = 44100.0;
    float lastXoverLow = -1.0f, lastXoverHigh = -1.0f;

    // FFT: se alimenta en el hilo de audio (pushNextSampleIntoFifo) y se
    // calcula en el hilo de mensajes (computeNextSpectrumFrame, llamado
    // desde un Timer en el editor) para no cargar el hilo de audio con la FFT.
    juce::dsp::FFT forwardFFT { fftOrder };
    juce::dsp::WindowingFunction<float> windowFn { (size_t) fftSize, juce::dsp::WindowingFunction<float>::hann };
    std::array<float, fftSize> fifo {};
    std::array<float, fftSize * 2> fftData {};
    int fifoIndex = 0;
    std::atomic<bool> nextFFTBlockReady { false };
    std::array<float, scopeSize> scopeData {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SurgicalDeEsserAudioProcessor)
};
