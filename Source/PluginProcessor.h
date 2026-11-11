#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_dsp/juce_dsp.h>

//==============================================================================
/** Um sample do instrumento, com a faixa de teclas em que ele toca.
    No modo de um sample so (single), ele vale para o teclado todo e quem manda na
    nota original, nos marcadores e no loop sao os botoes da tela. No modo multisample,
    cada zona traz esses dados consigo (vindos do SFZ ou do mapeamento automatico). */
struct DrikSound final : public juce::SynthesiserSound
{
    std::shared_ptr<juce::AudioBuffer<float>> data;     // pode ser dividido entre zonas
    int length = 0;
    double sourceRate = 44100.0;

    bool single = true;
    int loKey = 0, hiKey = 127, loVel = 0, hiVel = 127, root = 60;
    double tuneCents = 0.0, pitchRandomCents = 0.0;
    float gain = 1.0f;
    int offset = 0, end = 0;                // trecho tocado, em samples (end = um alem do ultimo)
    int loopMode = 0;                       // 0 sem loop, 1 continuo, 2 enquanto a tecla esta presa, 3 toca ate o fim
    int loopStart = 0, loopEnd = 0;
    int seqLength = 1, seqPosition = 1;
    float loRand = 0.0f, hiRand = 1.0f;

    bool appliesToNote (int note) override   { return single || (note >= loKey && note <= hiKey); }
    bool appliesToChannel (int) override     { return true; }
};

/** Ponteiros para os valores dos parametros, lidos pela thread de audio. */
struct VoiceParams
{
    std::atomic<float>* attack = nullptr;
    std::atomic<float>* decay = nullptr;
    std::atomic<float>* sustain = nullptr;
    std::atomic<float>* release = nullptr;
    std::atomic<float>* gainDb = nullptr;
    std::atomic<float>* tune = nullptr;
    std::atomic<float>* root = nullptr;
    std::atomic<float>* cutoff = nullptr;
    std::atomic<float>* resonance = nullptr;
    std::atomic<float>* velSens = nullptr;
    std::atomic<float>* start = nullptr;      // 0..1 do comprimento do som
    std::atomic<float>* end = nullptr;        // 0..1
    std::atomic<float>* loop = nullptr;       // 0 ou 1
    std::atomic<float>* xfadeMs = nullptr;    // emenda do loop
};

//==============================================================================
class DrikVoice final : public juce::SynthesiserVoice
{
public:
    explicit DrikVoice (const VoiceParams& p) : params (p) {}

    bool canPlaySound (juce::SynthesiserSound* s) override   { return dynamic_cast<DrikSound*> (s) != nullptr; }

    void setCurrentPlaybackSampleRate (double newRate) override;
    void startNote (int midiNote, float velocity, juce::SynthesiserSound*, int) override;
    void stopNote (float, bool allowTailOff) override;
    void pitchWheelMoved (int newValue) override;
    void controllerMoved (int, int) override {}
    void renderNextBlock (juce::AudioBuffer<float>&, int startSample, int numSamples) override;

private:
    void updateRatio();

    const VoiceParams& params;
    juce::ADSR adsr;
    juce::dsp::StateVariableTPTFilter<float> filter;

    double position = 0.0, ratio = 1.0, baseRatio = 1.0, bendSemis = 0.0;
    bool released = false;
    juce::Random random;
    float velocityGain = 1.0f;
    int note = 60;
};

//==============================================================================
/** Sintetizador que escolhe as zonas pela tecla, pela forca e pelo rodizio (round robin). */
class SamplerEngine final : public juce::Synthesiser
{
public:
    void noteOn (int channel, int note, float velocity) override;

private:
    juce::Random random;
    int sequence = 0;
};

/** Descricao de uma zona, antes de o audio ser carregado. */
struct ZoneDesc
{
    juce::File file;
    int loKey = 0, hiKey = 127, loVel = 0, hiVel = 127, root = 60;
    double tuneCents = 0.0, pitchRandomCents = 0.0, volumeDb = 0.0;
    juce::int64 offset = 0, end = -1;
    int loopMode = 0;
    juce::int64 loopStart = -1, loopEnd = -1;
    int seqLength = 1, seqPosition = 1;
    float loRand = 0.0f, hiRand = 1.0f;
};

/** O que a tela precisa saber sobre o instrumento carregado. */
struct InstrumentInfo
{
    enum Mode { empty, single, sfz, files };
    Mode mode = empty;
    juce::String name, message;
    juce::File file;                         // o audio (single) ou o .sfz
    juce::StringArray programs;              // sons dentro do SFZ, quando ha mais de um
    int program = 0;
    struct Zone { int lo, hi, root; };
    std::vector<Zone> zones;
};

//==============================================================================
class DrikSamplerProcessor final : public juce::AudioProcessor,
                                   public juce::ChangeBroadcaster
{
public:
    DrikSamplerProcessor();
    ~DrikSamplerProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override                          { return true; }

    const juce::String getName() const override              { return "DrikSampler"; }
    bool acceptsMidi() const override                        { return true; }
    bool producesMidi() const override                       { return false; }
    bool isMidiEffect() const override                       { return false; }
    double getTailLengthSeconds() const override             { return 5.0; }

    int getNumPrograms() override                            { return 1; }
    int getCurrentProgram() override                         { return 0; }
    void setCurrentProgram (int) override                    {}
    const juce::String getProgramName (int) override         { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    /** Carrega um arquivo de audio como instrumento de um sample so. Com fresh = true (som novo,
        escolhido pelo usuario) tambem corta o silencio das pontas e descobre a nota original;
        com false (projeto reaberto) respeita os valores salvos. */
    bool loadFile (const juce::File&, bool fresh);

    /** Abre um instrumento SFZ. program = indice na lista de sons do arquivo (-1 = o padrao). */
    bool loadSfz (const juce::File&, int program, bool fresh);

    /** Monta um instrumento com varios arquivos de audio, mapeando cada um pela sua nota. */
    bool loadFiles (const juce::Array<juce::File>&);

    /** Abre o que for solto na janela: um audio, um .sfz, varios audios ou uma pasta. */
    bool loadAnything (const juce::Array<juce::File>&);

    InstrumentInfo getInfo() const;
    juce::File getCurrentFile() const;

    juce::AudioProcessorValueTreeState apvts;
    juce::MidiKeyboardState keyboardState;
    juce::AudioFormatManager formatManager;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    /** Carrega o audio das zonas e troca o instrumento. Devolve quantas zonas entraram. */
    int installZones (const std::vector<ZoneDesc>&, juce::String& problem, std::vector<InstrumentInfo::Zone>& installed);
    void setInfo (const InstrumentInfo&);
    void setParam (const char* id, float value);

    SamplerEngine synth;
    std::vector<ZoneDesc> fileZones;          // so no modo 'files', para salvar no projeto
    InstrumentInfo info;
    VoiceParams voiceParams;

    juce::CriticalSection fileLock;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DrikSamplerProcessor)
};
