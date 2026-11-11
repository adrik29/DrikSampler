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

    // ================= multisample e SFZ (v0.3) =================
    {
        std::printf ("--- multisample e SFZ\n");
        auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("drik_multi");
        dir.deleteRecursively();
        dir.getChildFile ("samples").createDirectory();

        // Tres samples: Do 3 (130,8 Hz), Do 4 (261,6 Hz), Do 5 (523,3 Hz), cada um gravado na sua altura.
        auto tone = [&] (const juce::String& name, int midi)
        {
            auto f = writeTone (name, 440.0 * std::pow (2.0, (midi - 69) / 12.0), 44100.0, 1);
            auto dest = dir.getChildFile ("samples").getChildFile (name);
            f.moveFileTo (dest);
            return dest;
        };
        tone ("C3.wav", 48); tone ("C4.wav", 60); tone ("C5.wav", 72);
        tone ("forte_C4.wav", 61);                       // de proposito meio tom acima, para distinguir a camada

        dir.getChildFile ("teste.sfz").replaceWithText (
            "<group> ampeg_attack=0.004 ampeg_release=0.08\r\n"
            "<region> sample=samples\\C3.wav lokey=43 hikey=53 pitch_keycenter=48\r\n"
            "<region> sample=samples\\C4.wav lokey=54 hikey=65 pitch_keycenter=60 hivel=90\r\n"
            "<region> sample=samples\\forte_C4.wav lokey=54 hikey=65 pitch_keycenter=60 lovel=91\r\n"
            "<region> sample=samples\\C5.wav lokey=66 hikey=77 pitch_keycenter=72 tune=100\r\n"
            "<region> sample=samples\\nao_existe.wav key=20\r\n");

        DrikSamplerProcessor p;
        p.setPlayConfigDetails (0, 2, sr, block);
        p.prepareToPlay (sr, block);
        auto play = [&] (DrikSamplerProcessor& proc, int note, float vel, double seconds = 0.5)
        {
            juce::MidiBuffer on, off;
            on.addEvent (juce::MidiMessage::noteOn (1, note, vel), 0);
            auto v = render (proc, on, seconds, sr, block);
            off.addEvent (juce::MidiMessage::noteOff (1, note), 0);
            render (proc, off, 1.0, sr, block);
            return v;
        };
        auto pitch = [&] (const std::vector<float>& v) { return drik::hzToMidiNote (drik::detectPitchHz (v.data(), (int) v.size(), sr)); };

        check (p.loadSfz (dir.getChildFile ("teste.sfz"), -1, true), "abre o SFZ");
        auto info = p.getInfo();
        check (info.mode == InstrumentInfo::sfz && info.zones.size() == 4, "SFZ: 4 zonas carregadas (a de arquivo ausente fica de fora)");
        check (info.message.contains ("1 arquivo"), "SFZ: avisa do arquivo ausente");
        check (std::abs (p.apvts.getRawParameterValue ("release")->load() - 0.08f) < 0.001f, "SFZ: release do arquivo vai para o botao");

        check (pitch (play (p, 48, 0.5f)) == 48, "zona 1: tecla na nota original");
        check (pitch (play (p, 52, 0.5f)) == 52, "zona 1: tecla acima da original, mesma zona");
        check (pitch (play (p, 60, 0.5f)) == 60, "zona 2: camada fraca");
        check (pitch (play (p, 60, 1.0f)) == 61, "zona 2: camada forte toca o outro sample");
        check (pitch (play (p, 57, 0.5f)) == 57, "zona 2: abaixo da original");
        check (pitch (play (p, 72, 0.5f)) == 73, "zona 3: tune=100 sobe um semitom");
        check (peakOf (play (p, 30, 1.0f)) == 0.0f && peakOf (play (p, 90, 1.0f)) == 0.0f, "fora das zonas: silencio");
        check (peakOf (play (p, 60, 0.5f, 5.0), (size_t) (4.0 * sr)) == 0.0f, "nota presa acaba no fim do sample (sem loop)");

        // Os botoes de um sample so nao podem interferir no multisample.
        p.apvts.getParameter ("root")->setValueNotifyingHost (0.1f);
        p.apvts.getParameter ("start")->setValueNotifyingHost (0.9f);
        check (pitch (play (p, 48, 0.5f)) == 48 && peakOf (play (p, 48, 0.5f)) > 0.1f, "nota original e marcadores da tela nao afetam o multisample");
        p.apvts.getParameter ("start")->setValueNotifyingHost (0.0f);

        // Acorde atravessando as tres zonas.
        juce::MidiBuffer chord;
        for (int n : { 48, 55, 60, 64, 72 }) chord.addEvent (juce::MidiMessage::noteOn (1, n, 0.6f), 0);
        auto c = render (p, chord, 0.5, sr, block);
        bool fin = true; for (float v : c) fin = fin && std::isfinite (v);
        check (fin && peakOf (c) > 0.2f, "acorde atravessando zonas");
        juce::MidiBuffer quiet; quiet.addEvent (juce::MidiMessage::allNotesOff (1), 0);
        render (p, quiet, 1.0, sr, block);

        // Projeto salvo e reaberto.
        juce::MemoryBlock st;
        p.getStateInformation (st);
        DrikSamplerProcessor q;
        q.setPlayConfigDetails (0, 2, sr, block);
        q.prepareToPlay (sr, block);
        q.setStateInformation (st.getData(), (int) st.getSize());
        check (q.getInfo().mode == InstrumentInfo::sfz && q.getInfo().zones.size() == 4 && pitch (play (q, 52, 0.5f)) == 52, "projeto reaberto: SFZ de volta");

        // Loop dentro da zona e programas por keyswitch.
        dir.getChildFile ("b.sfz").replaceWithText (
            "<global> sw_default=1\n"
            "<group> sw_last=0 sw_label=Grave\n<region> sample=samples/C3.wav key=c3 lokey=0 hikey=127 loop_mode=loop_continuous loop_start=4410 loop_end=22049\n"
            "<group> sw_last=1 sw_label=Agudo\n<region> sample=samples/C5.wav key=c5 lokey=0 hikey=127 loop_mode=one_shot\n");
        check (p.loadSfz (dir.getChildFile ("b.sfz"), -1, true), "abre SFZ com dois sons");
        info = p.getInfo();
        check (info.programs.size() == 2 && info.programs[1] == "Agudo" && info.program == 1, "lista de sons e som padrao");
        check (pitch (play (p, 72, 0.8f)) == 72, "som padrao (Agudo)");
        {
            juce::MidiBuffer on, off;
            on.addEvent (juce::MidiMessage::noteOn (1, 72, 0.8f), 0);
            render (p, on, 0.1, sr, block);
            off.addEvent (juce::MidiMessage::noteOff (1, 72), 0);
            auto after = render (p, off, 1.0, sr, block);
            check (peakOf (after, (size_t) (0.6 * sr)) > 0.1f, "one_shot: continua tocando depois de soltar a tecla");
            render (p, quiet, 3.5, sr, block);
        }
        check (p.loadSfz (dir.getChildFile ("b.sfz"), 0, false), "troca para o outro som");
        auto looped = play (p, 48, 0.8f, 6.0);
        check (peakOf (looped, (size_t) (5.0 * sr)) > 0.1f && pitch (looped) == 48, "loop da zona: nota presa segue soando alem do fim do sample");

        // Varios arquivos soltos: mapeamento automatico pela nota do nome.
        juce::Array<juce::File> drop;
        for (auto n : { "C3.wav", "C4.wav", "C5.wav" }) drop.add (dir.getChildFile ("samples").getChildFile (n));
        check (p.loadAnything (drop), "tres arquivos soltos viram um instrumento");
        info = p.getInfo();
        check (info.mode == InstrumentInfo::files && info.zones.size() == 3, "tres zonas");
        if (info.zones.size() == 3)
            check (info.zones[0].lo == 0 && info.zones[0].hi == 54 && info.zones[1].lo == 55 && info.zones[1].hi == 66 && info.zones[2].hi == 127,
                   "faixas divididas a meio caminho entre as notas");
        check (pitch (play (p, 50, 0.7f)) == 50 && pitch (play (p, 62, 0.7f)) == 62 && pitch (play (p, 79, 0.7f)) == 79, "afinado no teclado todo");
        p.getStateInformation (st);
        DrikSamplerProcessor r2;
        r2.setPlayConfigDetails (0, 2, sr, block);
        r2.prepareToPlay (sr, block);
        r2.setStateInformation (st.getData(), (int) st.getSize());
        check (r2.getInfo().mode == InstrumentInfo::files && pitch (play (r2, 62, 0.7f)) == 62, "projeto reaberto: instrumento de arquivos soltos de volta");

        // A pasta inteira: acha o .sfz e abre por ele.
        juce::Array<juce::File> folder; folder.add (dir);
        check (p.loadAnything (folder) && p.getInfo().mode == InstrumentInfo::sfz, "pasta solta: abre pelo .sfz que estiver dentro");

        // Um arquivo so continua sendo o modo antigo.
        juce::Array<juce::File> one; one.add (dir.getChildFile ("samples").getChildFile ("C4.wav"));
        check (p.loadAnything (one) && p.getInfo().mode == InstrumentInfo::single
               && (int) p.apvts.getRawParameterValue ("root")->load() == 60 && pitch (play (p, 67, 0.7f)) == 67, "um arquivo: modo de um sample, como antes");

        // SFZ invalido nao derruba o instrumento atual.
        dir.getChildFile ("ruim.sfz").replaceWithText ("isto nao e um sfz");
        check (! p.loadSfz (dir.getChildFile ("ruim.sfz"), -1, true) && p.getInfo().mode == InstrumentInfo::single
               && p.getInfo().message.isNotEmpty() && pitch (play (p, 67, 0.7f)) == 67, "SFZ invalido: avisa e mantem o instrumento anterior");
        dir.deleteRecursively();
    }

    // ================= favoritos, pasta-mae e biblioteca (v0.4) =================
    {
        std::printf ("--- favoritos e biblioteca\n");
        auto base = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("drik_fav");
        base.deleteRecursively();
        auto rootA = base.getChildFile ("SamplesA"), rootB = base.getChildFile ("SamplesB"), favs = base.getChildFile ("Favoritos");
        auto put = [&] (const juce::File& folder, const juce::String& name, int midi)
        {
            folder.createDirectory();
            auto f = writeTone (name, 440.0 * std::pow (2.0, (midi - 69) / 12.0), 44100.0, 1);
            f.moveFileTo (folder.getChildFile (name));
        };
        // Biblioteca com .sfz, pasta de samples com nome de nota, e sons avulsos.
        put (rootA.getChildFile ("Mellotron/Cordas/samples"), "C3.wav", 48);
        put (rootA.getChildFile ("Mellotron/Cordas/samples"), "C4.wav", 60);
        rootA.getChildFile ("Mellotron/Cordas/Cordas.sfz").replaceWithText ("<region> sample=samples/C3.wav key=c3 lokey=0 hikey=54\n<region> sample=samples/C4.wav key=c4 lokey=55 hikey=127\n");
        put (rootA.getChildFile ("Nord/Coro"), "Coro C3.wav", 48);
        put (rootA.getChildFile ("Nord/Coro"), "Coro G3.wav", 55);
        put (rootA.getChildFile ("Nord/Coro"), "Coro C4.wav", 60);
        put (rootA.getChildFile ("Bichos"), "javali.wav", 50);
        put (rootA.getChildFile ("Bichos"), "cabrito.wav", 64);

        const auto oldRoot = DrikSamplerProcessor::getSamplesRoot();
        const auto oldFavs = DrikSamplerProcessor::getFavoritesFolder();
        DrikSamplerProcessor::setSamplesRoot (rootA);
        DrikSamplerProcessor::setFavoritesFolder (favs);
        check (DrikSamplerProcessor::getSamplesRoot() == rootA && DrikSamplerProcessor::getFavoritesFolder() == favs, "pastas escolhidas ficam guardadas");

        DrikSamplerProcessor p;
        p.setPlayConfigDetails (0, 2, sr, block);
        p.prepareToPlay (sr, block);
        auto play = [&] (DrikSamplerProcessor& proc, int note)
        {
            juce::MidiBuffer on, off;
            on.addEvent (juce::MidiMessage::noteOn (1, note, 0.8f), 0);
            auto v = render (proc, on, 0.5, sr, block);
            off.addEvent (juce::MidiMessage::allNotesOff (1), 0);
            render (proc, off, 1.0, sr, block);
            return drik::hzToMidiNote (drik::detectPitchHz (v.data(), (int) v.size(), sr));
        };

        const auto lib = p.scanLibrary();
        juce::StringArray labels;
        for (auto& e : lib) labels.add (e.label + " [" + e.group + "] x" + juce::String (e.files.size()));
        check (lib.size() == 4, "biblioteca: 4 entradas (1 sfz, 1 pasta-instrumento, 2 sons avulsos)");
        check (labels.contains ("Mellotron/Cordas/Cordas [Mellotron] x1"), "biblioteca: o .sfz aparece uma vez, sem listar os samples dele");
        check (labels.contains ("Nord/Coro [Nord] x3"), "biblioteca: pasta com nomes de nota vira um instrumento so");
        check (labels.contains ("Bichos/javali [Bichos] x1") && labels.contains ("Bichos/cabrito [Bichos] x1"), "biblioteca: sons avulsos ficam um por entrada");
        if (lib.size() != 4) std::printf ("   achei: %s\n", labels.joinIntoString (" | ").toRawUTF8());

        // Favorito de um SFZ com regulagem propria.
        check (p.loadSfz (rootA.getChildFile ("Mellotron/Cordas/Cordas.sfz"), -1, true), "abre o sfz da biblioteca");
        auto setP = [&] (DrikSamplerProcessor& proc, const char* id, float v) { auto* prm = proc.apvts.getParameter (id); prm->setValueNotifyingHost (prm->convertTo0to1 (v)); };
        setP (p, "release", 1.5f); setP (p, "cutoff", 3000.0f); setP (p, "tune", 2.0f); setP (p, "velsens", 0.3f);
        check (p.saveFavorite (favs.getChildFile ("Cordas do meu jeito.drikinst")), "salva o favorito");
        check (p.getInfo().name == "Cordas do meu jeito", "o instrumento passa a ter o nome do favorito");
        check (favs.getChildFile ("Cordas do meu jeito.drikinst").loadFileAsString().contains ("$ROOT/Mellotron/Cordas/Cordas.sfz"), "o favorito guarda o caminho a partir da pasta-mae");

        // Favorito de pasta-instrumento e de som avulso.
        for (auto& e : lib) if (e.label == "Nord/Coro") check (p.loadAnything (e.files) && p.getInfo().mode == InstrumentInfo::files, "abre a pasta-instrumento");
        check (play (p, 57) == 59, "pasta-instrumento toca afinada (com os +2 semitons que estao no botao)");
        check (p.saveFavorite (favs.getChildFile ("Coro.drikinst")), "salva favorito de pasta");
        for (auto& e : lib) if (e.label == "Bichos/javali") p.loadAnything (e.files);
        setP (p, "attack", 0.5f);
        check (p.getInfo().mode == InstrumentInfo::single && p.saveFavorite (favs.getChildFile ("Javali lento.drikinst")), "salva favorito de som avulso");
        check (DrikSamplerProcessor::favoriteFiles().size() == 3, "tres favoritos na pasta");

        // Outra janela do sampler, com outra sensibilidade, carrega o favorito.
        DrikSamplerProcessor q;
        q.setPlayConfigDetails (0, 2, sr, block);
        q.prepareToPlay (sr, block);
        setP (q, "velsens", 0.9f);
        check (q.loadFavorite (favs.getChildFile ("Cordas do meu jeito.drikinst")), "carrega o favorito");
        check (q.getInfo().mode == InstrumentInfo::sfz && q.getInfo().name == "Cordas do meu jeito", "favorito: instrumento e nome");
        check (std::abs (q.apvts.getRawParameterValue ("release")->load() - 1.5f) < 0.01f
               && std::abs (q.apvts.getRawParameterValue ("cutoff")->load() - 3000.0f) < 1.0f, "favorito: regulagem dos botoes de volta");
        check (std::abs (q.apvts.getRawParameterValue ("velsens")->load() - 0.9f) < 0.01f, "favorito nao mexe na sensibilidade");
        check (play (q, 60) == 62, "favorito toca com a afinacao salva (+2 semitons)");

        // A pasta dos samples muda de lugar: basta apontar a nova.
        check (rootA.moveFileTo (rootB), "move a pasta dos samples");
        check (! q.loadFavorite (favs.getChildFile ("Coro.drikinst")) && q.getInfo().message.isNotEmpty(), "pasta sumiu: avisa, sem travar");
        check (q.getInfo().mode == InstrumentInfo::sfz, "pasta sumiu: o instrumento anterior continua carregado");
        DrikSamplerProcessor::setSamplesRoot (rootB);
        check (q.loadFavorite (favs.getChildFile ("Coro.drikinst")) && q.getInfo().mode == InstrumentInfo::files && play (q, 57) == 59,
               "pasta nova apontada: o favorito de pasta volta, com a afinacao que foi salva nele");
        check (q.loadFavorite (favs.getChildFile ("Javali lento.drikinst")) && q.getInfo().mode == InstrumentInfo::single
               && std::abs (q.apvts.getRawParameterValue ("attack")->load() - 0.5f) < 0.01f, "favorito de som avulso volta com a regulagem");
        check (q.loadFavorite (favs.getChildFile ("Cordas do meu jeito.drikinst")) && play (q, 60) == 62, "favorito do sfz volta na pasta nova");

        // Projeto do Reaper tambem sobrevive a mudanca de pasta.
        juce::MemoryBlock st;
        q.getStateInformation (st);
        check (rootB.moveFileTo (rootA), "move de volta");
        DrikSamplerProcessor::setSamplesRoot (rootA);
        DrikSamplerProcessor r3;
        r3.setPlayConfigDetails (0, 2, sr, block);
        r3.prepareToPlay (sr, block);
        r3.setStateInformation (st.getData(), (int) st.getSize());
        check (r3.getInfo().mode == InstrumentInfo::sfz && play (r3, 60) == 62, "projeto salvo reabre depois de a pasta dos samples mudar de lugar");

        check (! p.loadFavorite (favs.getChildFile ("nao existe.drikinst")), "favorito inexistente: recusa");
        favs.getChildFile ("lixo.drikinst").replaceWithText ("nada");
        check (! p.loadFavorite (favs.getChildFile ("lixo.drikinst")), "favorito corrompido: recusa");

        DrikSamplerProcessor::setSamplesRoot (oldRoot);
        DrikSamplerProcessor::setFavoritesFolder (oldFavs);
        base.deleteRecursively();
    }

    // ================= bibliotecas reais, se estiverem no disco =================
    for (const char* path : { "/home/claude/libs/Mellotron-SFZ/Mellotron/String Section/String Section.sfz",
                              "/home/claude/libs/Mellotron-SFZ/Mellotron/Combined Choir/Combined Choir.sfz" })
    {
        juce::File f (path);
        if (! f.existsAsFile()) continue;
        DrikSamplerProcessor p;
        p.setPlayConfigDetails (0, 2, sr, block);
        p.prepareToPlay (sr, block);
        const bool ok = p.loadSfz (f, -1, true);
        const auto info = p.getInfo();
        int right = 0, near = 0, total = 0; float quietest = 10.0f, loudest = 0.0f; double longest = 0.0;
        for (int n = 43; ok && n <= 77; ++n)
        {
            juce::MidiBuffer on, off;
            on.addEvent (juce::MidiMessage::noteOn (1, n, 1.0f), 0);
            auto v = render (p, on, 9.0, sr, block);
            off.addEvent (juce::MidiMessage::noteOff (1, n), 0);
            render (p, off, 0.5, sr, block);
            const double hz = drik::detectPitchHz (v.data() + (int) (0.3 * sr), (int) (1.5 * sr), sr);
            const double semis = hz > 0 ? 69.0 + 12.0 * std::log2 (hz / 440.0) : -1.0;
            ++total;
            if (std::abs (semis - n) < 0.5) ++right;
            if (std::abs (semis - n) < 0.5 || std::abs (std::fmod (std::abs (semis - n), 12.0)) < 0.5 || std::abs (std::fmod (std::abs (semis - n), 12.0) - 12.0) < 0.5) ++near;
            const float pk = peakOf (v);
            quietest = std::min (quietest, pk); loudest = std::max (loudest, pk);
            size_t last = 0; for (size_t k = 0; k < v.size(); ++k) if (std::fabs (v[k]) > 0.001f) last = k;
            longest = std::max (longest, (double) last / sr);
        }
        std::printf ("%s: %s, %d zonas, %s | afinacao certa em %d de %d teclas (%d contando oitavas) | pico %.2f a %.2f | nota mais longa %.1f s\n",
                     f.getFileNameWithoutExtension().toRawUTF8(), ok ? "abriu" : "NAO ABRIU", (int) info.zones.size(), info.message.toRawUTF8(),
                     right, total, near, quietest, loudest, longest);
        check (ok && info.zones.size() == 35 && quietest > 0.01f && near >= 30, "biblioteca real abre e toca afinada");
    }

    std::printf (fails == 0 ? "\ntudo certo\n" : "\n%d falhas\n", fails);
    return fails;
}
