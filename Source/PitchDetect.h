#pragma once
// Deteccao de nota sem dependencia do JUCE (da pra testar isolado).
#include <algorithm>
#include <cctype>
#include <cmath>
#include <string>
#include <vector>

namespace drik
{

/** Estima a frequencia fundamental (Hz) de um trecho mono pelo metodo YIN.
    Devolve 0 se o som nao tiver altura definida (ruido, percussao). */
inline double detectPitchHz (const float* x, int n, double sampleRate)
{
    if (x == nullptr || n < 2048 || sampleRate <= 0.0)
        return 0.0;

    const double minHz = 30.0, maxHz = 2000.0;
    const int tauMax = (int) (sampleRate / minHz);
    const int tauMin = std::max (2, (int) (sampleRate / maxHz));

    // Pula o ataque: comeca um pouco depois do pico de amplitude.
    int peak = 0;
    float peakVal = 0.0f;
    for (int i = 0; i < n; ++i)
    {
        const float a = std::fabs (x[i]);
        if (a > peakVal) { peakVal = a; peak = i; }
    }
    if (peakVal < 1.0e-4f)
        return 0.0;

    const int window = std::min (4096, n - tauMax - 1);
    if (window < 512)
        return 0.0;

    int start = peak + (int) (0.05 * sampleRate);
    if (start + window + tauMax >= n)
        start = std::max (0, (n - window - tauMax - 1) / 2);

    const float* s = x + start;

    std::vector<double> d ((size_t) tauMax + 1, 0.0);
    for (int tau = 1; tau <= tauMax; ++tau)
    {
        double sum = 0.0;
        for (int j = 0; j < window; ++j)
        {
            const double diff = (double) s[j] - (double) s[j + tau];
            sum += diff * diff;
        }
        d[(size_t) tau] = sum;
    }

    // Diferenca normalizada pela media acumulada.
    std::vector<double> cmnd ((size_t) tauMax + 1, 1.0);
    double running = 0.0;
    for (int tau = 1; tau <= tauMax; ++tau)
    {
        running += d[(size_t) tau];
        cmnd[(size_t) tau] = running > 0.0 ? d[(size_t) tau] * tau / running : 1.0;
    }

    const double threshold = 0.15;
    int best = -1;
    for (int tau = tauMin; tau < tauMax; ++tau)
    {
        if (cmnd[(size_t) tau] < threshold)
        {
            while (tau + 1 < tauMax && cmnd[(size_t) tau + 1] < cmnd[(size_t) tau])
                ++tau;
            best = tau;
            break;
        }
    }

    if (best < 0)
    {
        // Nada passou do limiar: aceita o minimo global so se for razoavel.
        int argmin = tauMin;
        for (int tau = tauMin; tau < tauMax; ++tau)
            if (cmnd[(size_t) tau] < cmnd[(size_t) argmin])
                argmin = tau;
        if (cmnd[(size_t) argmin] > 0.35)
            return 0.0;
        best = argmin;
    }

    // Interpolacao parabolica para refinar o periodo.
    double tauRefined = (double) best;
    if (best > 1 && best < tauMax)
    {
        const double a = cmnd[(size_t) best - 1], b = cmnd[(size_t) best], c = cmnd[(size_t) best + 1];
        const double denom = a - 2.0 * b + c;
        if (std::fabs (denom) > 1.0e-12)
            tauRefined += 0.5 * (a - c) / denom;
    }

    return tauRefined > 0.0 ? sampleRate / tauRefined : 0.0;
}

inline int hzToMidiNote (double hz)
{
    if (hz <= 0.0)
        return -1;
    const int note = (int) std::lround (69.0 + 12.0 * std::log2 (hz / 440.0));
    return (note >= 0 && note <= 127) ? note : -1;
}

/** Procura um nome de nota no nome do arquivo ("Rhodes_F#2.wav", "strings Bb3").
    Convencao: C4 = 60 (a mesma do Reaper). Devolve -1 se nao achar. */
inline int noteFromFilename (const std::string& name)
{
    auto isAlnum = [] (char c) { return std::isalnum ((unsigned char) c) != 0; };
    auto isDigit = [] (char c) { return std::isdigit ((unsigned char) c) != 0; };

    int found = -1;
    const int n = (int) name.size();

    for (int i = 0; i < n; ++i)
    {
        if (i > 0 && isAlnum (name[(size_t) i - 1]))
            continue;

        int semis;
        switch (std::toupper ((unsigned char) name[(size_t) i]))
        {
            case 'C': semis = 0;  break;
            case 'D': semis = 2;  break;
            case 'E': semis = 4;  break;
            case 'F': semis = 5;  break;
            case 'G': semis = 7;  break;
            case 'A': semis = 9;  break;
            case 'B': semis = 11; break;
            default:  continue;
        }

        int j = i + 1;
        if (j < n && name[(size_t) j] == '#')
        {
            ++semis; ++j;
        }
        else if (j + 1 < n && name[(size_t) j] == 'b'
                 && (isDigit (name[(size_t) j + 1]) || name[(size_t) j + 1] == '-'))
        {
            --semis; ++j;
        }

        bool negative = false;
        if (j + 1 < n && name[(size_t) j] == '-' && isDigit (name[(size_t) j + 1]))
        {
            negative = true; ++j;
        }

        if (j >= n || ! isDigit (name[(size_t) j]))
            continue;

        const int octave = (name[(size_t) j] - '0') * (negative ? -1 : 1);
        ++j;

        if (j < n && isAlnum (name[(size_t) j]))
            continue;

        const int note = (octave + 1) * 12 + semis;
        if (note >= 0 && note <= 127)
            found = note;
    }

    return found;
}

} // namespace drik
