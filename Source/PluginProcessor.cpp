#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "PitchDetect.h"
#include "SfzParser.h"
#include <map>
#include <set>

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
    released = false;

    // Um sample so: a nota original vem do botao. Multisample: vem da zona.
    double semis = (double) midiNote + (double) params.tune->load();
    if (sound->single)
        semis -= (double) params.root->load();
    else
        semis += -(double) sound->root + (sound->tuneCents + random.nextDouble() * sound->pitchRandomCents) / 100.0;

    baseRatio = std::pow (2.0, semis / 12.0) * sound->sourceRate / sr;
    bendSemis = ((double) pitchWheel - 8192.0) / 8192.0 * 2.0;
    updateRatio();

    position = sound->single ? (double) juce::jlimit (0.0f, 1.0f, params.start->load()) * (double) sound->length
                             : (double) sound->offset;
    // Sensibilidade: 100% = a forca da tecla manda no volume; 0% = toda nota sai no volume cheio.
    const float sens = juce::jlimit (0.0f, 1.0f, params.velSens->load());
    velocityGain = (1.0f - sens + sens * velocity) * sound->gain;

    adsr.setParameters ({ params.attack->load(), params.decay->load(), params.sustain->load(), params.release->load() });
    adsr.reset();
    adsr.noteOn();
    filter.reset();
}

void DrikVoice::stopNote (float, bool allowTailOff)
{
    released = true;

    if (allowTailOff)
    {
        // Zona marcada para tocar ate o fim (one_shot) ignora a soltura da tecla.
        auto* sound = dynamic_cast<DrikSound*> (getCurrentlyPlayingSound().get());
        if (sound == nullptr || sound->single || sound->loopMode != 3)
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
    const int numCh = sound->data->getNumChannels();
    const float* inL = sound->data->getReadPointer (0);
    const float* inR = numCh > 1 ? sound->data->getReadPointer (1) : inL;

    float* outL = out.getWritePointer (0, startSample);
    float* outR = out.getNumChannels() > 1 ? out.getWritePointer (1, startSample) : nullptr;

    adsr.setParameters ({ params.attack->load(), params.decay->load(), params.sustain->load(), params.release->load() });

    const float gain = juce::Decibels::decibelsToGain (params.gainDb->load()) * velocityGain;
    const float maxCutoff = (float) (getSampleRate() * 0.45);
    filter.setCutoffFrequency (juce::jlimit (20.0f, maxCutoff, params.cutoff->load()));
    filter.setResonance (juce::jlimit (0.3f, 12.0f, params.resonance->load()));

    double startS, endS, xfade = 0.0;
    bool looping;

    if (sound->single)
    {
        // Trecho tocado: entre os marcadores de inicio e fim (lidos a cada bloco, entao
        // arrastar um marcador com a nota soando ja vale na hora).
        startS  = (double) juce::jlimit (0.0f, 1.0f, params.start->load()) * (double) length;
        endS    = juce::jlimit (startS + 2.0, (double) length, (double) params.end->load() * (double) length);
        looping = params.loop->load() > 0.5f && endS - startS > 8.0;

        // Emenda do loop: nos ultimos X samples antes do fim, o som vai dando lugar ao comeco.
        if (looping)
            xfade = juce::jlimit (0.0, (endS - startS) * 0.5, (double) params.xfadeMs->load() * 0.001 * sound->sourceRate);
    }
    else
    {
        // Multisample: o trecho e o loop vem da zona; os pontos de loop do autor sao respeitados como estao.
        looping = (sound->loopMode == 1 || (sound->loopMode == 2 && ! released)) && sound->loopEnd - sound->loopStart > 8;
        startS  = looping ? (double) sound->loopStart : (double) sound->offset;
        endS    = looping ? (double) sound->loopEnd : (double) sound->end;
    }

    const double fadeOut = 64.0;   // sem loop: desce rapido no fim para nao estalar

    auto read = [inL, inR] (double pos, float& l, float& r)
    {
        const int idx = (int) pos;
        const float frac = (float) (pos - (double) idx);
        l = inL[idx] + frac * (inL[idx + 1] - inL[idx]);
        r = inR[idx] + frac * (inR[idx + 1] - inR[idx]);
    };

    for (int i = 0; i < numSamples; ++i)
    {
        if (position >= endS)
        {
            if (! looping)
            {
                adsr.reset();
                clearCurrentNote();
                break;
            }

            const double span = juce::jmax (1.0, endS - startS - xfade);
            position = startS + xfade + std::fmod (position - endS, span);
        }

        float l, r;
        read (position, l, r);

        if (looping)
        {
            if (xfade >= 1.0 && position > endS - xfade)
            {
                const double t = (position - (endS - xfade)) / xfade;
                float l2, r2;
                read (startS + t * xfade, l2, r2);
                const float gOld = (float) std::cos (t * juce::MathConstants<double>::halfPi);
                const float gNew = (float) std::sin (t * juce::MathConstants<double>::halfPi);
                l = l * gOld + l2 * gNew;
                r = r * gOld + r2 * gNew;
            }
        }
        else if (position > endS - fadeOut)
        {
            const float g = (float) juce::jmax (0.0, (endS - position) / fadeOut);
            l *= g;
            r *= g;
        }

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
void SamplerEngine::noteOn (int channel, int midiNote, float velocity)
{
    const juce::ScopedLock sl (lock);

    const int vel = juce::jlimit (1, 127, juce::roundToInt (velocity * 127.0f));
    const float dice = random.nextFloat();
    bool stoppedOld = false;

    for (auto* s : sounds)
    {
        auto* zone = static_cast<DrikSound*> (s);

        if (! zone->appliesToNote (midiNote) || ! zone->appliesToChannel (channel))
            continue;

        if (! zone->single)
        {
            if (vel < zone->loVel || vel > zone->hiVel)                         continue;   // camada de forca
            if (dice < zone->loRand || dice >= zone->hiRand)                    continue;   // variacao aleatoria
            if (zone->seqLength > 1 && sequence % zone->seqLength != zone->seqPosition - 1) continue;   // rodizio
        }

        // Se a mesma nota ainda esta soando, solta a anterior primeiro (uma vez so).
        if (! stoppedOld)
        {
            stoppedOld = true;
            for (auto* voice : voices)
                if (voice->getCurrentlyPlayingNote() == midiNote && voice->isPlayingChannel (channel))
                    stopVoice (voice, 1.0f, true);
        }

        startVoice (findFreeVoice (zone, channel, midiNote, isNoteStealingEnabled()), zone, channel, midiNote, velocity);
    }

    ++sequence;
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

    layout.add (std::make_unique<Float> (juce::ParameterID { "velsens", 1 }, "Velocity Sensitivity", Range (0.0f, 1.0f, 0.0f), 1.0f,
                                         Attr().withStringFromValueFunction ([] (float v, int) { return juce::String (juce::roundToInt (v * 100.0f)) + " %"; })));

    layout.add (std::make_unique<Float> (juce::ParameterID { "start", 1 }, "Sample Start", Range (0.0f, 1.0f, 0.0f), 0.0f));
    layout.add (std::make_unique<Float> (juce::ParameterID { "end", 1 },   "Sample End",   Range (0.0f, 1.0f, 0.0f), 1.0f));
    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "loop", 1 }, "Loop", false));

    Range xfadeRange (0.0f, 1000.0f, 0.0f);
    xfadeRange.setSkewForCentre (100.0f);
    layout.add (std::make_unique<Float> (juce::ParameterID { "xfade", 1 }, "Loop Crossfade", xfadeRange, 30.0f,
                                         Attr().withStringFromValueFunction ([] (float v, int) { return juce::String (juce::roundToInt (v)) + " ms"; })));

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
    voiceParams.velSens   = apvts.getRawParameterValue ("velsens");
    voiceParams.start     = apvts.getRawParameterValue ("start");
    voiceParams.end       = apvts.getRawParameterValue ("end");
    voiceParams.loop      = apvts.getRawParameterValue ("loop");
    voiceParams.xfadeMs   = apvts.getRawParameterValue ("xfade");

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
void DrikSamplerProcessor::setParam (const char* id, float value)
{
    if (auto* p = apvts.getParameter (id))
        p->setValueNotifyingHost (p->convertTo0to1 (value));
}

void DrikSamplerProcessor::setInfo (const InstrumentInfo& newInfo)
{
    {
        const juce::ScopedLock sl (fileLock);
        info = newInfo;
    }
    sendChangeMessage();
}

InstrumentInfo DrikSamplerProcessor::getInfo() const
{
    const juce::ScopedLock sl (fileLock);
    return info;
}

juce::File DrikSamplerProcessor::getCurrentFile() const
{
    const juce::ScopedLock sl (fileLock);
    return info.file;
}

//==============================================================================
bool DrikSamplerProcessor::loadFile (const juce::File& file, bool fresh)
{
    if (! file.existsAsFile())
        return false;

    std::unique_ptr<juce::AudioFormatReader> reader (formatManager.createReaderFor (file));
    if (reader == nullptr || reader->lengthInSamples < 2 || reader->sampleRate <= 0.0)
        return false;

    // Carrega tudo na memoria; limita a 10 minutos por seguranca.
    const auto maxLength = (juce::int64) (reader->sampleRate * 600.0);
    const int length = (int) juce::jmin (reader->lengthInSamples, maxLength);
    const int numCh = juce::jlimit (1, 2, (int) reader->numChannels);

    auto* sound = new DrikSound();
    juce::SynthesiserSound::Ptr holder (sound);

    sound->data = std::make_shared<juce::AudioBuffer<float>> (numCh, length + 4);
    sound->data->clear();
    reader->read (sound->data.get(), 0, length, 0, true, numCh > 1);
    sound->length = length;
    sound->end = length;
    sound->sourceRate = reader->sampleRate;
    sound->single = true;

    if (fresh)
    {
        std::vector<float> mono ((size_t) length);
        for (int i = 0; i < length; ++i)
        {
            float v = sound->data->getSample (0, i);
            if (numCh > 1)
                v = 0.5f * (v + sound->data->getSample (1, i));
            mono[(size_t) i] = v;
        }

        // 1. Marcadores: encostam onde o som realmente comeca e termina.
        int first = 0, last = length;
        if (! drik::findSoundBounds (mono.data(), length, reader->sampleRate, first, last))
        {
            first = 0;
            last = length;
        }
        setParam ("start", (float) first / (float) length);
        setParam ("end",   (float) last / (float) length);

        // 2. Nota original: nome do arquivo; senao, analise do som; senao, C4.
        int root = drik::noteFromFilename (file.getFileNameWithoutExtension().toStdString());

        if (root < 0)
        {
            const int analysed = juce::jmin (last - first, (int) (reader->sampleRate * 30.0));
            root = drik::detectNote (mono.data() + first, analysed, reader->sampleRate);
        }

        setParam ("root", (float) (root >= 0 ? root : 60));
    }

    synth.clearSounds();
    synth.addSound (holder);
    fileZones.clear();

    InstrumentInfo i;
    i.mode = InstrumentInfo::single;
    i.file = file;
    i.name = file.getFileName();
    setInfo (i);
    return true;
}

//==============================================================================
int DrikSamplerProcessor::installZones (const std::vector<ZoneDesc>& zones, juce::String& problem, std::vector<InstrumentInfo::Zone>& installed)
{
    installed.clear();
    std::vector<DrikSound*> withEnd;
    std::map<juce::String, std::pair<std::shared_ptr<juce::AudioBuffer<float>>, double>> cache;
    juce::Array<juce::SynthesiserSound::Ptr> loaded;
    juce::int64 bytes = 0;
    int missing = 0;
    const juce::int64 budget = (juce::int64) 1500 * 1024 * 1024;       // teto de memoria: 1,5 GB

    for (const auto& z : zones)
    {
        const auto key = z.file.getFullPathName();
        auto it = cache.find (key);

        if (it == cache.end())
        {
            std::unique_ptr<juce::AudioFormatReader> reader (formatManager.createReaderFor (z.file));
            if (reader == nullptr || reader->lengthInSamples < 2 || reader->sampleRate <= 0.0)
            {
                ++missing;
                continue;
            }

            const int length = (int) juce::jmin (reader->lengthInSamples, (juce::int64) (reader->sampleRate * 600.0));
            const int numCh = juce::jlimit (1, 2, (int) reader->numChannels);
            bytes += (juce::int64) length * numCh * 4;
            if (bytes > budget)
            {
                problem = juce::String::fromUTF8 ("instrumento grande demais para a mem\xc3\xb3ria; carreguei s\xc3\xb3 uma parte");
                break;
            }

            auto buffer = std::make_shared<juce::AudioBuffer<float>> (numCh, length + 4);
            buffer->clear();
            reader->read (buffer.get(), 0, length, 0, true, numCh > 1);
            it = cache.emplace (key, std::make_pair (buffer, reader->sampleRate)).first;
        }

        auto* sound = new DrikSound();
        sound->data = it->second.first;
        sound->sourceRate = it->second.second;
        sound->length = sound->data->getNumSamples() - 4;
        sound->single = false;
        sound->loKey = z.loKey;   sound->hiKey = z.hiKey;
        sound->loVel = z.loVel;   sound->hiVel = z.hiVel;
        sound->root = z.root;
        sound->tuneCents = z.tuneCents;
        sound->pitchRandomCents = z.pitchRandomCents;
        sound->gain = juce::Decibels::decibelsToGain ((float) z.volumeDb);
        sound->offset = (int) juce::jlimit ((juce::int64) 0, (juce::int64) juce::jmax (0, sound->length - 2), z.offset);
        sound->end = z.end > 0 ? (int) juce::jlimit ((juce::int64) sound->offset + 2, (juce::int64) sound->length, z.end + 1) : sound->length;
        sound->loopMode = z.loopMode;
        sound->loopStart = (int) juce::jlimit ((juce::int64) 0, (juce::int64) sound->length, z.loopStart);
        sound->loopEnd = (int) juce::jlimit ((juce::int64) 0, (juce::int64) sound->length, z.loopEnd + 1);
        if (sound->loopEnd - sound->loopStart <= 8 && (sound->loopMode == 1 || sound->loopMode == 2))
            sound->loopMode = 0;
        sound->seqLength = z.seqLength;   sound->seqPosition = z.seqPosition;
        sound->loRand = z.loRand;         sound->hiRand = z.hiRand;
        loaded.add (sound);
        installed.push_back ({ z.loKey, z.hiKey, z.root });
        if (z.end > 0)
            withEnd.push_back (sound);
    }

    // Defeito conhecido de alguns SFZ: o 'end' de todas as regioes cai na metade exata do sample
    // (conta feita como se o arquivo mono fosse estereo), e toda nota e cortada no meio.
    // Quando o padrao e inequivoco, toca o sample inteiro e avisa.
    if (withEnd.size() >= 4)
    {
        bool allHalf = true;
        for (auto* snd : withEnd)
        {
            const double ratio = (double) snd->end / (double) juce::jmax (1, snd->length);
            allHalf = allHalf && ratio > 0.47 && ratio < 0.53;
        }
        if (allHalf)
        {
            for (auto* snd : withEnd)
                snd->end = snd->length;
            if (problem.isEmpty())
                problem = juce::String::fromUTF8 ("o arquivo cortava cada nota na metade; estou tocando os samples inteiros");
        }
    }

    if (loaded.isEmpty())
    {
        if (problem.isEmpty())
            problem = juce::String::fromUTF8 ("n\xc3\xa3o achei nenhum dos arquivos de \xc3\xa1udio");
        return 0;
    }

    synth.allNotesOff (0, false);
    synth.clearSounds();
    for (auto& snd : loaded)
        synth.addSound (snd);

    if (missing > 0 && problem.isEmpty())
        problem = juce::String (missing) + juce::String::fromUTF8 (missing == 1 ? " arquivo n\xc3\xa3o encontrado" : " arquivos n\xc3\xa3o encontrados");

    return loaded.size();
}

bool DrikSamplerProcessor::loadSfz (const juce::File& file, int program, bool fresh)
{
    const auto parsed = drik::parseSfz (file.getFullPathName().toStdString());

    InstrumentInfo i;
    i.mode = InstrumentInfo::sfz;
    i.file = file;
    i.name = file.getFileNameWithoutExtension();

    if (! parsed.error.empty())
    {
        // Nao troca o instrumento atual; so avisa.
        auto current = getInfo();
        current.message = file.getFileName() + ": " + juce::String::fromUTF8 (parsed.error.c_str());
        setInfo (current);
        return false;
    }

    for (const auto& p : parsed.programs)
        i.programs.add (juce::String::fromUTF8 (p.second.c_str()));

    int programValue = -1;
    if (! parsed.programs.empty())
    {
        if (program < 0)
        {
            program = 0;
            for (size_t k = 0; k < parsed.programs.size(); ++k)
                if (parsed.programs[k].first == parsed.defaultProgram)
                    program = (int) k;
        }
        program = juce::jlimit (0, (int) parsed.programs.size() - 1, program);
        programValue = parsed.programs[(size_t) program].first;
        i.program = program;
    }

    std::vector<ZoneDesc> zones;
    for (const auto& r : parsed.regions)
    {
        if (r.program >= 0 && programValue >= 0 && r.program != programValue)
            continue;                                   // pertence a outro som do mesmo arquivo

        ZoneDesc z;
        z.file = juce::File (juce::String::fromUTF8 (r.samplePath.c_str()));
        z.loKey = r.loKey;  z.hiKey = r.hiKey;  z.loVel = r.loVel;  z.hiVel = r.hiVel;  z.root = r.root;
        z.tuneCents = r.tuneCents;  z.pitchRandomCents = r.pitchRandomCents;  z.volumeDb = r.volumeDb;
        z.offset = r.offset;  z.end = r.end;
        z.loopMode = r.loopMode;  z.loopStart = r.loopStart;  z.loopEnd = r.loopEnd;
        z.seqLength = r.seqLength;  z.seqPosition = r.seqPosition;
        z.loRand = (float) r.loRand;  z.hiRand = (float) r.hiRand;
        zones.push_back (z);
    }

    juce::String problem;
    const int count = installZones (zones, problem, i.zones);
    if (count == 0)
    {
        auto current = getInfo();
        current.message = file.getFileName() + ": " + problem;
        setInfo (current);
        return false;
    }

    if (fresh)
    {
        // O que o arquivo disser sobre envelope e filtro vai para os botoes; o resto fica como estava.
        if (parsed.hasAttack)   setParam ("attack", (float) juce::jmax (0.001, parsed.attack));
        if (parsed.hasRelease)  setParam ("release", (float) juce::jmax (0.005, parsed.release));
        if (parsed.hasCutoff)   setParam ("cutoff", (float) juce::jlimit (20.0, 20000.0, parsed.cutoff));
    }

    fileZones.clear();
    i.message = problem;
    setInfo (i);
    return true;
}

bool DrikSamplerProcessor::loadFiles (const juce::Array<juce::File>& files)
{
    // Descobre a nota de cada arquivo: primeiro pelo nome, depois pela analise do som.
    struct Item { juce::File file; int root; };
    std::vector<Item> pitched, unpitched;

    for (const auto& f : files)
    {
        int root = drik::noteFromFilename (f.getFileNameWithoutExtension().toStdString());

        if (root < 0)
        {
            std::unique_ptr<juce::AudioFormatReader> reader (formatManager.createReaderFor (f));
            if (reader == nullptr || reader->lengthInSamples < 2)
                continue;

            const int n = (int) juce::jmin (reader->lengthInSamples, (juce::int64) (reader->sampleRate * 20.0));
            juce::AudioBuffer<float> buf (1, n);
            reader->read (&buf, 0, n, 0, true, false);
            int first = 0, last = n;
            if (drik::findSoundBounds (buf.getReadPointer (0), n, reader->sampleRate, first, last))
                root = drik::detectNote (buf.getReadPointer (0) + first, last - first, reader->sampleRate);
        }

        (root >= 0 ? pitched : unpitched).push_back ({ f, root });
    }

    std::vector<ZoneDesc> zones;
    juce::String note;

    if (pitched.size() >= unpitched.size() && ! pitched.empty())
    {
        // Instrumento afinado: cada sample cobre das teclas a meio caminho do vizinho de baixo
        // ate a meio caminho do vizinho de cima; as pontas vao ate o fim do teclado.
        std::stable_sort (pitched.begin(), pitched.end(), [] (const Item& a, const Item& b) { return a.root < b.root; });

        std::vector<int> roots;
        for (const auto& it : pitched)
            if (roots.empty() || roots.back() != it.root)
                roots.push_back (it.root);

        for (size_t r = 0; r < roots.size(); ++r)
        {
            const int lo = r == 0 ? 0 : (roots[r - 1] + roots[r]) / 2 + 1;
            const int hi = r + 1 == roots.size() ? 127 : (roots[r] + roots[r + 1]) / 2;

            std::vector<juce::File> same;                // varios samples da mesma nota: rodizio
            for (const auto& it : pitched)
                if (it.root == roots[r])
                    same.push_back (it.file);

            for (size_t k = 0; k < same.size(); ++k)
            {
                ZoneDesc z;
                z.file = same[k];
                z.loKey = lo;  z.hiKey = hi;  z.root = roots[r];
                z.seqLength = (int) same.size();
                z.seqPosition = (int) k + 1;
                zones.push_back (z);
            }
        }

        if (! unpitched.empty())
            note = juce::String ((int) unpitched.size()) + juce::String::fromUTF8 (" sem nota definida ficaram de fora");
    }
    else
    {
        // Sons sem altura (percussao, efeitos): um por tecla, a partir do C2, cada um no seu tom natural.
        int key = 36;
        for (const auto& it : unpitched)
        {
            if (key > 127) break;
            ZoneDesc z;
            z.file = it.file;
            z.loKey = z.hiKey = z.root = key++;
            z.loopMode = 3;                              // toca ate o fim, como pad de bateria
            zones.push_back (z);
        }
    }

    juce::String problem;
    InstrumentInfo i;
    if (zones.empty() || installZones (zones, problem, i.zones) == 0)
    {
        auto current = getInfo();
        current.message = juce::String::fromUTF8 ("n\xc3\xa3o consegui montar um instrumento com esses arquivos");
        setInfo (current);
        return false;
    }

    fileZones = zones;

    i.mode = InstrumentInfo::files;
    i.name = files.getFirst().getParentDirectory().getFileName();
    i.message = problem.isNotEmpty() ? problem : note;
    setInfo (i);
    return true;
}

bool DrikSamplerProcessor::loadAnything (const juce::Array<juce::File>& dropped)
{
    juce::Array<juce::File> audio, sfzFiles;

    auto consider = [&] (const juce::File& f)
    {
        if (f.hasFileExtension ("sfz"))
            sfzFiles.add (f);
        else if (formatManager.findFormatForFileExtension (f.getFileExtension()) != nullptr)
            audio.add (f);
    };

    for (const auto& f : dropped)
    {
        if (f.isDirectory())
            for (const auto& child : f.findChildFiles (juce::File::findFiles, true))
                consider (child);
        else
            consider (f);
    }

    audio.sort();
    sfzFiles.sort();

    if (! sfzFiles.isEmpty())   return loadSfz (sfzFiles.getFirst(), -1, true);
    if (audio.size() == 1)      return loadFile (audio.getFirst(), true);
    if (audio.size() > 1)       return loadFiles (audio);
    return false;
}

//==============================================================================
// Ajustes do usuario, num arquivo proprio (compartilhado entre o programa avulso e o plugin).
static std::unique_ptr<juce::PropertiesFile> openSettings()
{
    juce::PropertiesFile::Options o;
    o.applicationName = "DrikSampler";
    o.filenameSuffix = ".settings";
    o.folderName = "DrikSampler";
    o.osxLibrarySubFolder = "Application Support";
    return std::make_unique<juce::PropertiesFile> (o);
}

juce::File DrikSamplerProcessor::getSamplesRoot()
{
    const auto path = openSettings()->getValue ("samplesRoot");
    return juce::File::isAbsolutePath (path) ? juce::File (path) : juce::File();
}

void DrikSamplerProcessor::setSamplesRoot (const juce::File& folder)
{
    auto settings = openSettings();
    settings->setValue ("samplesRoot", folder.getFullPathName());
    settings->saveIfNeeded();
}

juce::File DrikSamplerProcessor::getFavoritesFolder()
{
    const auto path = openSettings()->getValue ("favoritesFolder");
    if (juce::File::isAbsolutePath (path))
        return juce::File (path);

    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
               .getChildFile ("DrikSampler").getChildFile ("Instrumentos");
}

void DrikSamplerProcessor::setFavoritesFolder (const juce::File& folder)
{
    auto settings = openSettings();
    settings->setValue ("favoritesFolder", folder.getFullPathName());
    settings->saveIfNeeded();
}

juce::Array<juce::File> DrikSamplerProcessor::favoriteFiles()
{
    auto files = getFavoritesFolder().findChildFiles (juce::File::findFiles, false, "*.drikinst");
    files.sort();
    return files;
}

// Caminho dentro da pasta-mae vira "$ROOT/...": se a pasta mudar de lugar, basta apontar o novo.
juce::String DrikSamplerProcessor::toPortable (const juce::File& file)
{
    const auto root = getSamplesRoot();
    if (root != juce::File() && file.isAChildOf (root))
        return "$ROOT/" + file.getRelativePathFrom (root).replaceCharacter ('\\', '/');
    return file.getFullPathName();
}

juce::File DrikSamplerProcessor::fromPortable (const juce::String& text)
{
    if (text.startsWith ("$ROOT/"))
    {
        const auto root = getSamplesRoot();
        return root != juce::File() ? root.getChildFile (text.substring (6)) : juce::File();
    }
    return juce::File::isAbsolutePath (text) ? juce::File (text) : juce::File();
}

//==============================================================================
juce::ValueTree DrikSamplerProcessor::buildState() const
{
    auto state = const_cast<juce::AudioProcessorValueTreeState&> (apvts).copyState();
    const auto i = getInfo();

    state.removeChild (state.getChildWithName ("ZONES"), nullptr);
    state.setProperty ("mode", (int) i.mode, nullptr);
    state.setProperty ("samplePath", i.file != juce::File() ? toPortable (i.file) : juce::String(), nullptr);
    state.setProperty ("program", i.program, nullptr);
    state.setProperty ("instrumentName", i.name, nullptr);

    if (i.mode == InstrumentInfo::files)
    {
        juce::ValueTree zones ("ZONES");
        for (const auto& z : fileZones)
        {
            juce::ValueTree t ("ZONE");
            t.setProperty ("path", toPortable (z.file), nullptr);
            t.setProperty ("lo", z.loKey, nullptr);
            t.setProperty ("hi", z.hiKey, nullptr);
            t.setProperty ("root", z.root, nullptr);
            t.setProperty ("seqLength", z.seqLength, nullptr);
            t.setProperty ("seqPosition", z.seqPosition, nullptr);
            t.setProperty ("loopMode", z.loopMode, nullptr);
            zones.addChild (t, -1, nullptr);
        }
        state.addChild (zones, -1, nullptr);
    }

    return state;
}

bool DrikSamplerProcessor::applyState (juce::ValueTree state)
{
    if (! state.isValid())
        return false;

    const auto file = fromPortable (state.getProperty ("samplePath").toString());
    const int program = state.getProperty ("program", 0);
    const auto zonesTree = state.getChildWithName ("ZONES").createCopy();

    // Projetos salvos antes do multisample nao tem 'mode': sao sempre um sample so.
    const int mode = state.getProperty ("mode", (int) InstrumentInfo::single);
    const auto name = state.getProperty ("instrumentName").toString();

    state.removeChild (state.getChildWithName ("ZONES"), nullptr);
    apvts.replaceState (state);

    if (mode == InstrumentInfo::sfz)
        return file != juce::File() && loadSfz (file, program, false);

    if (mode == InstrumentInfo::files && zonesTree.isValid())
    {
        std::vector<ZoneDesc> zones;
        InstrumentInfo i;
        i.mode = InstrumentInfo::files;
        i.name = name;

        for (const auto& t : zonesTree)
        {
            ZoneDesc z;
            z.file = fromPortable (t.getProperty ("path").toString());
            z.loKey = t.getProperty ("lo", 0);
            z.hiKey = t.getProperty ("hi", 127);
            z.root = t.getProperty ("root", 60);
            z.seqLength = t.getProperty ("seqLength", 1);
            z.seqPosition = t.getProperty ("seqPosition", 1);
            z.loopMode = t.getProperty ("loopMode", 0);
            zones.push_back (z);
        }

        juce::String problem;
        if (installZones (zones, problem, i.zones) == 0)
            return false;

        fileZones = zones;
        i.message = problem;
        setInfo (i);
        return true;
    }

    return file != juce::File() && loadFile (file, false);
}

void DrikSamplerProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    if (auto xml = buildState().createXml())
        copyXmlToBinary (*xml, dest);
}

void DrikSamplerProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        applyState (juce::ValueTree::fromXml (*xml));
}

//==============================================================================
bool DrikSamplerProcessor::saveFavorite (const juce::File& file)
{
    if (getInfo().mode == InstrumentInfo::empty)
        return false;

    auto state = buildState();
    state.setProperty ("instrumentName", file.getFileNameWithoutExtension(), nullptr);

    auto xml = state.createXml();
    if (xml == nullptr || ! file.getParentDirectory().createDirectory() || ! xml->writeTo (file))
        return false;

    auto i = getInfo();
    i.name = file.getFileNameWithoutExtension();
    setInfo (i);
    return true;
}

bool DrikSamplerProcessor::loadFavorite (const juce::File& file)
{
    auto xml = juce::XmlDocument::parse (file);
    if (xml == nullptr)
        return false;

    auto state = juce::ValueTree::fromXml (*xml);
    if (! state.isValid() || ! state.hasProperty ("mode"))
        return false;

    // A sensibilidade depende do teclado de quem toca, nao do instrumento: o favorito nao mexe nela.
    const float sensitivity = apvts.getRawParameterValue ("velsens")->load();
    const bool ok = applyState (state);
    setParam ("velsens", sensitivity);

    auto i = getInfo();
    if (ok)
    {
        i.name = file.getFileNameWithoutExtension();
    }
    else
    {
        i.message = file.getFileNameWithoutExtension()
                    + juce::String::fromUTF8 (": n\xc3\xa3o achei os samples. Confira a pasta dos samples em Pastas...");
    }
    setInfo (i);
    return ok;
}

//==============================================================================
std::vector<DrikSamplerProcessor::LibraryEntry> DrikSamplerProcessor::scanLibrary() const
{
    std::vector<LibraryEntry> entries;
    const auto root = getSamplesRoot();
    if (! root.isDirectory())
        return entries;

    auto& fm = const_cast<juce::AudioFormatManager&> (formatManager);
    const auto all = root.findChildFiles (juce::File::findFiles, true);

    auto relative = [&root] (const juce::File& f)
    {
        return f.getRelativePathFrom (root).replaceCharacter ('\\', '/');
    };
    auto groupOf = [] (const juce::String& rel)
    {
        return rel.containsChar ('/') ? rel.upToFirstOccurrenceOf ("/", false, false) : juce::String ("Samples");
    };

    // Pastas que tem um .sfz: tudo o que estiver nelas ou abaixo delas ja e coberto pelo .sfz.
    std::set<juce::String> sfzFolders;
    for (const auto& f : all)
        if (f.hasFileExtension ("sfz"))
            sfzFolders.insert (f.getParentDirectory().getFullPathName());

    auto coveredBySfz = [&] (juce::File dir)
    {
        while (dir != root && dir.isAChildOf (root))
        {
            if (sfzFolders.count (dir.getFullPathName()) > 0)
                return true;
            dir = dir.getParentDirectory();
        }
        return false;
    };

    std::map<juce::String, juce::Array<juce::File>> audioByFolder;

    for (const auto& f : all)
    {
        if (f.hasFileExtension ("sfz"))
        {
            const auto rel = relative (f).upToLastOccurrenceOf (".", false, false);
            entries.push_back ({ rel, groupOf (rel), { f } });
        }
        else if (fm.findFormatForFileExtension (f.getFileExtension()) != nullptr && ! coveredBySfz (f.getParentDirectory()))
        {
            audioByFolder[f.getParentDirectory().getFullPathName()].add (f);
        }
    }

    for (auto& kv : audioByFolder)
    {
        auto& files = kv.second;
        files.sort();

        int named = 0;
        for (const auto& f : files)
            if (drik::noteFromFilename (f.getFileNameWithoutExtension().toStdString()) >= 0)
                ++named;

        const juce::File folder (kv.first);

        if (files.size() >= 2 && named * 10 >= files.size() * 6 && folder != root)
        {
            // Pasta de samples com nome de nota: e um instrumento so.
            const auto rel = relative (folder);
            entries.push_back ({ rel, groupOf (rel), files });
        }
        else
        {
            // Sons avulsos: cada arquivo e uma entrada.
            for (const auto& f : files)
            {
                const auto rel = relative (f).upToLastOccurrenceOf (".", false, false);
                entries.push_back ({ rel, groupOf (rel), { f } });
            }
        }

        if (entries.size() > 3000)
            break;
    }

    std::sort (entries.begin(), entries.end(), [] (const LibraryEntry& a, const LibraryEntry& b)
    {
        return a.label.compareNatural (b.label) < 0;
    });
    return entries;
}

juce::AudioProcessorEditor* DrikSamplerProcessor::createEditor()
{
    return new DrikSamplerEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new DrikSamplerProcessor();
}
