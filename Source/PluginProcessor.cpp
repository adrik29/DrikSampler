#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "PitchDetect.h"

//==============================================================================
void DrikVoice::setCurrentPlaybackSampleRate (double newRate)
{
    juce::SynthesiserVoice::setCurrentPlaybackSampleRate (newRate);

    if (newRate > 0.0)
    {
        adsr.setSampleRate (newRate);
        filter.prepare ({ newRate, 512, 2 });
        filter.setType (juce::dsp::StateVariableTPTFilterType::lowpass);
    }
}

void DrikVoice::updateRatio()
{
    ratio = baseRatio * std::pow (2.0, bendSemis / 12.0);
}

void DrikVoice::startNote (int midiNote, float velocity, juce::SynthesiserSound* s, int pitchWheel)
{
    auto* sound = dynamic_cast<DrikSound*> (s);
    const double sr = getSampleRate();

    if (sound == nullptr || sr <= 0.0)
    {
        clearCurrentNote();
        return;
    }

    note = midiNote;
    const double semis = (double) midiNote - (double) params.root->load() + (double) params.tune->load();
    baseRatio = std::pow (2.0, semis / 12.0) * sound->sourceRate / sr;
    bendSemis = ((double) pitchWheel - 8192.0) / 8192.0 * 2.0;
    updateRatio();

    position = 0.0;
    velocityGain = velocity;

    adsr.setParameters ({ params.attack->load(), params.decay->load(), params.sustain->load(), params.release->load() });
    adsr.reset();
    adsr.noteOn();
    filter.reset();
}

void DrikVoice::stopNote (float, bool allowTailOff)
{
    if (allowTailOff)
    {
        adsr.noteOff();
    }
    else
    {
        adsr.reset();
        clearCurrentNote();
    }
}

void DrikVoice::pitchWheelMoved (int newValue)
{
    bendSemis = ((double) newValue - 8192.0) / 8192.0 * 2.0;   // +/- 2 semitons
    updateRatio();
}

void DrikVoice::renderNextBlock (juce::AudioBuffer<float>& out, int startSample, int numSamples)
{
    auto* sound = dynamic_cast<DrikSound*> (getCurrentlyPlayingSound().get());
    if (sound == nullptr)
        return;

    const int length = sound->length;
    const int numCh = sound->data.getNumChannels();
    const float* inL = sound->data.getReadPointer (0);
    const float* inR = numCh > 1 ? sound->data.getReadPointer (1) : inL;

    float* outL = out.getWritePointer (0, startSample);
    float* outR = out.getNumChannels() > 1 ? out.getWritePointer (1, startSample) : nullptr;

    adsr.setParameters ({ params.attack->load(), params.decay->load(), params.sustain->load(), params.release->load() });

    const float gain = juce::Decibels::decibelsToGain (params.gainDb->load()) * velocityGain;
    const float maxCutoff = (float) (getSampleRate() * 0.45);
    filter.setCutoffFrequency (juce::jlimit (20.0f, maxCutoff, params.cutoff->load()));
    filter.setResonance (juce::jlimit (0.3f, 12.0f, params.resonance->load()));

    for (int i = 0; i < numSamples; ++i)
    {
        const int idx = (int) position;
        if (idx >= length - 1)
        {
            adsr.reset();
            clearCurrentNote();
            break;
        }

        const float frac = (float) (position - (double) idx);
        float l = inL[idx] + frac * (inL[idx + 1] - inL[idx]);
        float r = inR[idx] + frac * (inR[idx + 1] - inR[idx]);

        const float env = adsr.getNextSample() * gain;
        l = filter.processSample (0, l) * env;
        r = filter.processSample (1, r) * env;

        if (outR != nullptr)
        {
            outL[i] += l;
            outR[i] += r;
        }
        else
        {
            outL[i] += 0.5f * (l + r);
        }

        position += ratio;

        if (! adsr.isActive())
        {
            clearCurrentNote();
            break;
        }
    }

    filter.snapToZero();
}

//==============================================================================
static juce::String noteName (int n)
{
    return juce::MidiMessage::getMidiNoteName (n, true, true, 4);   // C4 = 60, como no Reaper
}

juce::AudioProcessorValueTreeState::ParameterLayout DrikSamplerProcessor::createLayout()
{
    using Range = juce::NormalisableRange<float>;
    using Float = juce::AudioParameterFloat;
    using Attr  = juce::AudioParameterFloatAttributes;

    auto timeRange = [] (float lo, float hi) { Range r (lo, hi, 0.0f); r.setSkewForCentre (0.3f); return r; };
    auto seconds = Attr().withStringFromValueFunction ([] (float v, int)
    {
        return v < 1.0f ? juce::String (juce::roundToInt (v * 1000.0f)) + " ms" : juce::String (v, 2) + " s";
    });

    Range cutoffRange (20.0f, 20000.0f, 0.0f);
    cutoffRange.setSkewForCentre (1000.0f);
    Range resRange (0.5f, 10.0f, 0.0f);
    resRange.setSkewForCentre (2.0f);

    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add (std::make_unique<Float> (juce::ParameterID { "attack", 1 },  "Attack",  timeRange (0.001f, 5.0f),  0.002f, seconds));
    layout.add (std::make_unique<Float> (juce::ParameterID { "decay", 1 },   "Decay",   timeRange (0.001f, 5.0f),  0.2f,   seconds));
    layout.add (std::make_unique<Float> (juce::ParameterID { "sustain", 1 }, "Sustain", Range (0.0f, 1.0f, 0.0f),  1.0f,
                                         Attr().withStringFromValueFunction ([] (float v, int) { return juce::String (juce::roundToInt (v * 100.0f)) + " %"; })));
    layout.add (std::make_unique<Float> (juce::ParameterID { "release", 1 }, "Release", timeRange (0.005f, 10.0f), 0.25f,  seconds));

    layout.add (std::make_unique<Float> (juce::ParameterID { "cutoff", 1 }, "Cutoff", cutoffRange, 20000.0f,
                                         Attr().withStringFromValueFunction ([] (float v, int)
                                         {
                                             return v < 1000.0f ? juce::String (juce::roundToInt (v)) + " Hz" : juce::String (v / 1000.0f, 1) + " kHz";
                                         })));
    layout.add (std::make_unique<Float> (juce::ParameterID { "resonance", 1 }, "Resonance", resRange, 0.707f,
                                         Attr().withStringFromValueFunction ([] (float v, int) { return juce::String (v, 2); })));

    layout.add (std::make_unique<Float> (juce::ParameterID { "tune", 1 }, "Tune", Range (-24.0f, 24.0f, 0.01f), 0.0f,
                                         Attr().withStringFromValueFunction ([] (float v, int) { return juce::String (v, 2) + " st"; })));
    layout.add (std::make_unique<Float> (juce::ParameterID { "gain", 1 }, "Gain", Range (-36.0f, 12.0f, 0.1f), 0.0f,
                                         Attr().withStringFromValueFunction ([] (float v, int) { return juce::String (v, 1) + " dB"; })));

    layout.add (std::make_unique<juce::AudioParameterInt> (juce::ParameterID { "root", 1 }, "Root Note", 0, 127, 60,
                                                           juce::AudioParameterIntAttributes()
                                                               .withStringFromValueFunction ([] (int v, int) { return noteName (v); })));
    return layout;
}

//==============================================================================
DrikSamplerProcessor::DrikSamplerProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "DrikSampler", createLayout())
{
    formatManager.registerBasicFormats();

    voiceParams.attack    = apvts.getRawParameterValue ("attack");
    voiceParams.decay     = apvts.getRawParameterValue ("decay");
    voiceParams.sustain   = apvts.getRawParameterValue ("sustain");
    voiceParams.release   = apvts.getRawParameterValue ("release");
    voiceParams.gainDb    = apvts.getRawParameterValue ("gain");
    voiceParams.tune      = apvts.getRawParameterValue ("tune");
    voiceParams.root      = apvts.getRawParameterValue ("root");
    voiceParams.cutoff    = apvts.getRawParameterValue ("cutoff");
    voiceParams.resonance = apvts.getRawParameterValue ("resonance");

    for (int i = 0; i < 24; ++i)
        synth.addVoice (new DrikVoice (voiceParams));
}

void DrikSamplerProcessor::prepareToPlay (double sampleRate, int)
{
    synth.setCurrentPlaybackSampleRate (sampleRate);
    keyboardState.reset();
}

bool DrikSamplerProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo();
}

void DrikSamplerProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();

    // Junta as notas do teclado da tela com as que vem do Reaper.
    keyboardState.processNextMidiBuffer (midi, 0, buffer.getNumSamples(), true);
    synth.renderNextBlock (buffer, midi, 0, buffer.getNumSamples());
}

//==============================================================================
bool DrikSamplerProcessor::loadFile (const juce::File& file, bool detectRoot)
{
    if (! file.existsAsFile())
        return false;

    std::unique_ptr<juce::AudioFormatReader> reader (formatManager.createReaderFor (file));
    if (reader == nullptr || reader->lengthInSamples < 2 || reader->sampleRate <= 0.0)
        return false;

    // Versao 1 carrega tudo na memoria; limita a 10 minutos por seguranca.
    const auto maxLength = (juce::int64) (reader->sampleRate * 600.0);
    const int length = (int) juce::jmin (reader->lengthInSamples, maxLength);
    const int numCh = juce::jlimit (1, 2, (int) reader->numChannels);

    auto* sound = new DrikSound();
    juce::SynthesiserSound::Ptr holder (sound);

    sound->data.setSize (numCh, length + 4);
    sound->data.clear();
    reader->read (&sound->data, 0, length, 0, true, numCh > 1);
    sound->length = length;
    sound->sourceRate = reader->sampleRate;

    if (detectRoot)
    {
        int root = drik::noteFromFilename (file.getFileNameWithoutExtension().toStdString());

        if (root < 0)
        {
            const int analysed = juce::jmin (length, (int) (reader->sampleRate * 20.0));
            std::vector<float> mono ((size_t) analysed);
            for (int i = 0; i < analysed; ++i)
            {
                float v = sound->data.getSample (0, i);
                if (numCh > 1)
                    v = 0.5f * (v + sound->data.getSample (1, i));
                mono[(size_t) i] = v;
            }
            root = drik::hzToMidiNote (drik::detectPitchHz (mono.data(), analysed, reader->sampleRate));
        }

        if (root >= 0)
            if (auto* p = apvts.getParameter ("root"))
                p->setValueNotifyingHost (p->convertTo0to1 ((float) root));
    }

    synth.clearSounds();
    synth.addSound (holder);

    {
        const juce::ScopedLock sl (fileLock);
        currentFile = file;
    }

    sendChangeMessage();
    return true;
}

juce::File DrikSamplerProcessor::getCurrentFile() const
{
    const juce::ScopedLock sl (fileLock);
    return currentFile;
}

//==============================================================================
void DrikSamplerProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    auto state = apvts.copyState();
    state.setProperty ("samplePath", getCurrentFile().getFullPathName(), nullptr);

    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, dest);
}

void DrikSamplerProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
    {
        auto state = juce::ValueTree::fromXml (*xml);
        if (! state.isValid())
            return;

        const auto path = state.getProperty ("samplePath").toString();
        apvts.replaceState (state);

        if (path.isNotEmpty() && juce::File::isAbsolutePath (path))
            loadFile (juce::File (path), false);
    }
}

juce::AudioProcessorEditor* DrikSamplerProcessor::createEditor()
{
    return new DrikSamplerEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new DrikSamplerProcessor();
}
