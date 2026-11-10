// Teste do motor sem interface: carrega um som gerado, toca notas, confere altura, silencio e estado salvo.
#include "../Source/PluginProcessor.h"
#include "../Source/PitchDetect.h"
#include <cstdio>

static int fails = 0;
static void check (bool ok, const char* what)
{
    std::printf ("%s  %s\n", ok ? "ok   " : "FALHA", what);
    if (! ok) ++fails;
}

static juce::File writeTone (const juce::String& name, double hz, double sr, int channels, double silenceBefore = 0.0, double silenceAfter = 0.0)
{
    auto file = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile (name);
    file.deleteFile();

    const int lead = (int) (sr * silenceBefore), body = (int) (sr * 3.0), trail = (int) (sr * silenceAfter);
    juce::AudioBuffer<float> buf (channels, lead + body + trail);
    buf.clear();
    for (int c = 0; c < channels; ++c)
        for (int i = 0; i < body; ++i)
            buf.setSample (c, lead + i, 0.5f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * hz * i / sr));

    juce::WavAudioFormat wav;
    std::unique_ptr<juce::OutputStream> out (file.createOutputStream());
    auto writer = wav.createWriterFor (out, juce::AudioFormatWriterOptions().withSampleRate (sr)
                                                                             .withNumChannels (channels)
                                                                             .withBitsPerSample (16));
    writer->writeFromAudioSampleBuffer (buf, 0, buf.getNumSamples());
    writer.reset();
    return file;
}

static std::vector<float> render (DrikSamplerProcessor& p, juce::MidiBuffer first, double seconds, double sr, int block)
{
    std::vector<float> out;
    juce::AudioBuffer<float> buf (2, block);
    for (int done = 0; done < (int) (seconds * sr); done += block)
    {
        p.processBlock (buf, first);
        first.clear();
        for (int i = 0; i < block; ++i)
            out.push_back (0.5f * (buf.getSample (0, i) + buf.getSample (1, i)));
    }
    return out;
}

static float peakOf (const std::vector<float>& v, size_t from = 0)
{
    float pk = 0.0f;
    for (size_t i = from; i < v.size(); ++i) pk = std::max (pk, std::fabs (v[i]));
    return pk;
}

int main()
{
    juce::ScopedJuceInitialiser_GUI init;
    const double sr = 48000.0;
    const int block = 512;

    for (int channels = 1; channels <= 2; ++channels)
    {
        std::printf ("--- arquivo com %d canal(is), 44.1 kHz, tocado a 48 kHz\n", channels);
        DrikSamplerProcessor p;
        p.setPlayConfigDetails (0, 2, sr, block);
        p.prepareToPlay (sr, block);

        check (render (p, {}, 0.1, sr, block).size() > 0 && peakOf (render (p, {}, 0.1, sr, block)) == 0.0f,
               "sem som carregado: silencio, sem travar");

        auto file = writeTone ("drik_teste.wav", 220.0, 44100.0, channels);   // La 220 Hz = nota 57
        check (p.loadFile (file, true), "carrega o WAV");
        check ((int) p.apvts.getRawParameterValue ("root")->load() == 57, "detecta a nota original (A3 = 57)");

        juce::MidiBuffer on;
        on.addEvent (juce::MidiMessage::noteOn (1, 69, 1.0f), 0);
        auto a = render (p, on, 0.5, sr, block);
        check (peakOf (a) > 0.2f, "nota tocada produz som");
        check (drik::hzToMidiNote (drik::detectPitchHz (a.data(), (int) a.size(), sr)) == 69, "nota 69 soa como A4 (440 Hz)");

        juce::MidiBuffer off;
        off.addEvent (juce::MidiMessage::noteOff (1, 69), 0);
        auto tail = render (p, off, 1.0, sr, block);
        check (peakOf (tail, tail.size() / 2) == 0.0f, "depois do release, silencio");

        juce::MidiBuffer low;
        low.addEvent (juce::MidiMessage::noteOn (1, 45, 1.0f), 0);
        auto b = render (p, low, 0.5, sr, block);
        check (drik::hzToMidiNote (drik::detectPitchHz (b.data(), (int) b.size(), sr)) == 45, "nota 45 soa como A2 (110 Hz)");
        auto longTail = render (p, {}, 8.0, sr, block);
        check (peakOf (longTail, longTail.size() - 4800) == 0.0f, "nota presa termina sozinha no fim do sample");

        // Sensibilidade a forca da tecla.
        auto playSoft = [&] (float sens)
        {
            p.apvts.getParameter ("velsens")->setValueNotifyingHost (sens);
            juce::MidiBuffer soft, stop;
            soft.addEvent (juce::MidiMessage::noteOn (1, 57, 0.2f), 0);
            auto v = render (p, soft, 0.3, sr, block);
            stop.addEvent (juce::MidiMessage::noteOff (1, 57), 0);
            render (p, stop, 1.0, sr, block);
            return peakOf (v);
        };
        const float softFull = playSoft (1.0f), softNone = playSoft (0.0f), softHalf = playSoft (0.5f);
        check (softFull > 0.07f && softFull < 0.13f, "sensibilidade 100%: tecla fraca (20%) sai baixa");
        check (softNone > 0.45f, "sensibilidade 0%: tecla fraca sai no volume cheio");
        check (softHalf > softFull * 2.0f && softHalf < softNone * 0.8f, "sensibilidade 50%: fica no meio");
        p.apvts.getParameter ("velsens")->setValueNotifyingHost (1.0f);

        juce::MidiBuffer chord;
        for (int n = 40; n < 80; ++n) chord.addEvent (juce::MidiMessage::noteOn (1, n, 1.0f), 0);
        auto c = render (p, chord, 0.3, sr, block);
        bool finite = true;
        for (float v : c) finite = finite && std::isfinite (v);
        check (finite, "40 notas de uma vez (mais que as 24 vozes): sem NaN");

        // Estado salvo e restaurado, como o Reaper faz ao reabrir o projeto.
        p.apvts.getParameter ("tune")->setValueNotifyingHost (p.apvts.getParameter ("tune")->convertTo0to1 (7.0f));
        juce::MemoryBlock state;
        p.getStateInformation (state);

        DrikSamplerProcessor q;
        q.setPlayConfigDetails (0, 2, sr, block);
        q.prepareToPlay (sr, block);
        q.setStateInformation (state.getData(), (int) state.getSize());
        check (q.getCurrentFile() == file, "estado restaurado: mesmo arquivo");
        check ((int) q.apvts.getRawParameterValue ("root")->load() == 57
               && std::abs (q.apvts.getRawParameterValue ("tune")->load() - 7.0f) < 0.01f, "estado restaurado: mesmos botoes");
        auto d = render (q, on, 0.5, sr, block);
        check (drik::hzToMidiNote (drik::detectPitchHz (d.data(), (int) d.size(), sr)) == 76, "afinacao +7 semitons: A4 vira E5");

        // ---- marcadores, loop e corte de silencio (v0.2)
        {
            auto setP = [&] (const char* id, float v) { auto* prm = p.apvts.getParameter (id); prm->setValueNotifyingHost (prm->convertTo0to1 (v)); };
            auto getP = [&] (const char* id) { return p.apvts.getRawParameterValue (id)->load(); };
            auto maxStep = [] (const std::vector<float>& v, size_t from)
            {
                float m = 0.0f;
                for (size_t i = from + 1; i < v.size(); ++i) m = std::max (m, std::fabs (v[i] - v[i - 1]));
                return m;
            };
            juce::MidiBuffer rootOn, rootOff;
            rootOn.addEvent (juce::MidiMessage::noteOn (1, 57, 1.0f), 0);
            rootOff.addEvent (juce::MidiMessage::noteOff (1, 57), 0);
            setP ("tune", 0.0f);

            // Cala o acorde do teste anterior antes de medir qualquer coisa.
            juce::MidiBuffer quiet;
            quiet.addEvent (juce::MidiMessage::allNotesOff (1), 0);
            render (p, quiet, 1.0, sr, block);
            check (peakOf (render (p, {}, 0.1, sr, block)) == 0.0f, "all notes off silencia tudo");

            check (getP ("start") < 0.01f && getP ("end") > 0.99f, "som sem silencio: marcadores nas pontas");

            // Trecho curto entre marcadores, sem loop: a nota dura so esse trecho (10% de 3 s = 0,3 s).
            setP ("start", 0.5f); setP ("end", 0.6f); setP ("loop", 0.0f);
            auto cut = render (p, rootOn, 1.0, sr, block);
            check (peakOf (cut, 0) > 0.3f && peakOf (cut, (size_t) (0.25 * sr)) > 0.3f, "marcadores: toca o trecho");
            check (peakOf (cut, (size_t) (0.36 * sr)) == 0.0f, "marcadores: para no marcador de fim");
            check (maxStep (cut, 0) < 0.05f, "marcadores: fim sem estalo");
            render (p, rootOff, 0.5, sr, block);

            // Mesmo trecho com loop: continua soando enquanto a tecla esta presa, sem estalo na emenda.
            setP ("loop", 1.0f);
            for (float xf : { 0.0f, 30.0f, 400.0f })
            {
                setP ("xfade", xf);
                auto looped = render (p, rootOn, 4.0, sr, block);
                check (peakOf (looped, (size_t) (3.5 * sr)) > 0.3f, "loop: segue soando depois de 3,5 s");
                bool finiteLoop = true;
                for (float v : looped) finiteLoop = finiteLoop && std::isfinite (v);
                check (finiteLoop && peakOf (looped) < 0.8f, "loop: sem NaN e sem estouro de volume");
                if (xf > 0.0f)
                    check (maxStep (looped, (size_t) (0.1 * sr)) < 0.06f, "loop: emenda sem estalo");
                check (drik::hzToMidiNote (drik::detectPitchHz (looped.data() + (int) (2.0 * sr), (int) sr, sr)) == 57, "loop: continua afinado");
                auto rel = render (p, rootOff, 1.5, sr, block);
                check (peakOf (rel, rel.size() / 2) == 0.0f, "loop: solta a tecla e silencia");
            }

            // Mexer nos marcadores com a nota soando nao pode travar nem estourar.
            auto live = render (p, rootOn, 0.2, sr, block);
            setP ("end", 0.52f);  auto l1 = render (p, {}, 0.2, sr, block);
            setP ("start", 0.1f); auto l2 = render (p, {}, 0.2, sr, block);
            setP ("end", 1.0f);   auto l3 = render (p, {}, 0.2, sr, block);
            setP ("start", 0.999f); auto l4 = render (p, {}, 0.2, sr, block);
            bool liveOk = true;
            for (auto* v : { &live, &l1, &l2, &l3, &l4 }) for (float x : *v) liveOk = liveOk && std::isfinite (x) && std::fabs (x) < 1.0f;
            check (liveOk, "arrastar marcadores com a nota soando: sem NaN");
            render (p, rootOff, 1.0, sr, block);
            setP ("loop", 0.0f); setP ("xfade", 30.0f);

            // Arquivo com silencio nas pontas: marcadores encostam no som; estado salvo nao e recortado de novo.
            auto padded = writeTone ("drik_pontas.wav", 220.0, 44100.0, channels, 0.5, 1.0);   // 0,5 + 3 + 1 = 4,5 s
            check (p.loadFile (padded, true), "carrega arquivo com silencio nas pontas");
            check (std::abs (getP ("start") - 0.5f / 4.5f) < 0.004f && std::abs (getP ("end") - 3.5f / 4.5f) < 0.006f,
                   "corta o silencio das pontas");
            check ((int) getP ("root") == 57, "nota original detectada no som comprido (A3)");
            auto imm = render (p, rootOn, 0.05, sr, block);
            check (peakOf (imm) > 0.3f, "depois do corte a nota soa na hora, sem esperar o silencio");
            render (p, rootOff, 0.5, sr, block);

            setP ("start", 0.3f); setP ("end", 0.7f); setP ("loop", 1.0f);
            juce::MemoryBlock st;
            p.getStateInformation (st);
            DrikSamplerProcessor r;
            r.setPlayConfigDetails (0, 2, sr, block);
            r.prepareToPlay (sr, block);
            r.setStateInformation (st.getData(), (int) st.getSize());
            check (std::abs (r.apvts.getRawParameterValue ("start")->load() - 0.3f) < 0.001f
                   && std::abs (r.apvts.getRawParameterValue ("end")->load() - 0.7f) < 0.001f
                   && r.apvts.getRawParameterValue ("loop")->load() > 0.5f, "projeto reaberto: marcadores e loop como foram salvos");
            padded.deleteFile();
            setP ("loop", 0.0f);
        }

        check (! p.loadFile (juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("nao_existe.wav"), true),
               "arquivo inexistente: recusa sem travar");
        file.deleteFile();
    }

    std::printf (fails == 0 ? "\ntudo certo\n" : "\n%d falhas\n", fails);
    return fails;
}
