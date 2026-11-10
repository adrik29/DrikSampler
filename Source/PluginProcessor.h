#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_dsp/juce_dsp.h>

//==============================================================================
/** O som carregado: o audio inteiro na memoria. */
struct DrikSound final : public juce::SynthesiserSound
{
    juce::AudioBuffer<float> data;
    int length = 0;
    double sourceRate = 44100.0;

    bool appliesToNote (int) override      { return true; }
    bool appliesToChannel (int) override   { return true; }
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
    float velocityGain = 1.0f;
    int note = 60;
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

    /** Carrega um arquivo de audio. Com detectRoot, descobre a nota original
        pelo nome do arquivo ou, se nao houver, pela analise do som. */
    bool loadFile (const juce::File&, bool detectRoot);
    juce::File getCurrentFile() const;

    juce::AudioProcessorValueTreeState apvts;
    juce::MidiKeyboardState keyboardState;
    juce::AudioFormatManager formatManager;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    juce::Synthesiser synth;
    VoiceParams voiceParams;

    juce::CriticalSection fileLock;
    juce::File currentFile;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DrikSamplerProcessor)
};
