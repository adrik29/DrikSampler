// Teste isolado da deteccao de nota:  g++ -std=c++17 -O2 tests/test_pitch.cpp -o t && ./t
#include "../Source/PitchDetect.h"
#include <cstdio>

static std::vector<float> tone (double hz, double sr, int kind)
{
    std::vector<float> v ((size_t) (sr * 1.5));
    for (size_t i = 0; i < v.size(); ++i)
    {
        const double t = (double) i / sr, ph = std::fmod (hz * t, 1.0);
        double y = 0.0;
        if (kind == 0) y = std::sin (2 * M_PI * hz * t);
        if (kind == 1) y = 2.0 * ph - 1.0;                                   // serra
        if (kind == 2) y = 0.2 * std::sin (2 * M_PI * hz * t)                // fundamental fraca
                         + 0.6 * std::sin (2 * M_PI * 2 * hz * t)
                         + 0.4 * std::sin (2 * M_PI * 3 * hz * t);
        v[i] = (float) (y * std::exp (-1.5 * t));                            // decaimento
    }
    return v;
}

int main()
{
    int fails = 0;
    for (double sr : { 44100.0, 48000.0, 96000.0 })
        for (int kind = 0; kind < 3; ++kind)
            for (int note : { 28, 36, 45, 57, 60, 69, 76, 84, 93 })
            {
                const double hz = 440.0 * std::pow (2.0, (note - 69) / 12.0);
                auto v = tone (hz, sr, kind);
                const int got = drik::hzToMidiNote (drik::detectPitchHz (v.data(), (int) v.size(), sr));
                if (got != note) { ++fails; std::printf ("FALHA sr=%.0f tipo=%d nota=%d obtido=%d\n", sr, kind, note, got); }
            }

    // Ruido nao deve virar nota.
    std::vector<float> noise (66150);
    unsigned s = 12345;
    for (auto& x : noise) { s = s * 1664525u + 1013904223u; x = (float) ((s >> 8) / 8388608.0 - 1.0); }
    if (drik::detectPitchHz (noise.data(), (int) noise.size(), 44100.0) != 0.0) { ++fails; std::printf ("FALHA ruido\n"); }

    // Som comprido de uma nota so: confia. Trecho com notas mudando: nao confia.
    {
        const double sr = 44100.0;
        std::vector<float> steady ((size_t) (sr * 8.0)), tune ((size_t) (sr * 8.0)), padded;
        const int melody[] = { 45, 52, 48, 57, 50, 43, 55, 47 };
        for (size_t i = 0; i < steady.size(); ++i)
        {
            const double t = (double) i / sr;
            steady[i] = (float) (0.5 * std::sin (2 * M_PI * 196.0 * t) * std::exp (-0.4 * t));
            const int n1 = melody[(int) t % 8], n2 = melody[((int) t + 3) % 8] + 12;
            tune[i] = (float) (0.3 * std::sin (2 * M_PI * 440.0 * std::pow (2.0, (n1 - 69) / 12.0) * t)
                             + 0.3 * std::sin (2 * M_PI * 440.0 * std::pow (2.0, (n2 - 69) / 12.0) * t));
        }
        if (drik::detectNote (steady.data(), (int) steady.size(), sr) != 55) { ++fails; std::printf ("FALHA nota longa estavel\n"); }
        if (drik::detectNote (tune.data(), (int) tune.size(), sr) != -1)     { ++fails; std::printf ("FALHA trecho de musica deveria dar -1, deu %d\n", drik::detectNote (tune.data(), (int) tune.size(), sr)); }

        padded.assign ((size_t) (sr * 0.5), 0.0f);
        padded.insert (padded.end(), steady.begin(), steady.begin() + (long) sr);
        padded.insert (padded.end(), (size_t) (sr * 0.25), 0.0f);
        int a = 0, b = 0;
        const bool ok = drik::findSoundBounds (padded.data(), (int) padded.size(), sr, a, b);
        if (! ok || std::abs (a - (int) (sr * 0.5)) > 200 || std::abs (b - (int) (sr * 1.5)) > 700)
        { ++fails; std::printf ("FALHA corte de silencio: %d..%d\n", a, b); }
    }

    struct { const char* name; int want; } names[] = {
        { "Rhodes_C4", 60 }, { "strings F#2", 42 }, { "pad-Bb3", 58 }, { "A0", 21 }, { "moog_c-1", 0 },
        { "Solina Strings", -1 }, { "take12", -1 }, { "Bass_E1_v2", 28 }, { "DX7 EPiano", -1 }, { "Juno_G#5_loud", 80 } };
    for (auto& t : names)
        if (drik::noteFromFilename (t.name) != t.want)
        { ++fails; std::printf ("FALHA nome '%s' obtido=%d esperado=%d\n", t.name, drik::noteFromFilename (t.name), t.want); }

    std::printf (fails == 0 ? "tudo certo\n" : "%d falhas\n", fails);
    return fails;
}
