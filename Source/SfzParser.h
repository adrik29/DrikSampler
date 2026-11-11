#pragma once
// Leitor de arquivos SFZ, sem dependencia do JUCE (da pra testar isolado).
//
// Entende o subconjunto que as bibliotecas de samples comuns usam:
//   cabecalhos <control> <global> <master> <group> <region>, com heranca de opcodes;
//   #include e #define; default_path; notas por numero ou por nome (C4 = 60);
//   programas escolhidos por keyswitch (sw_last / sw_label).
// O que nao entende, ignora sem reclamar.
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace drik
{

struct SfzRegion
{
    std::string samplePath;         // caminho completo, com barras normais
    int loKey = 0, hiKey = 127, root = 60;
    int loVel = 0, hiVel = 127;
    double tuneCents = 0.0, pitchRandomCents = 0.0, volumeDb = 0.0;
    long offset = 0, end = -1;      // end = ultimo sample tocado; -1 = ate o fim
    int loopMode = 0;               // 0 sem loop, 1 loop continuo, 2 loop enquanto a tecla esta presa, 3 toca ate o fim
    long loopStart = -1, loopEnd = -1;
    int seqLength = 1, seqPosition = 1;
    double loRand = 0.0, hiRand = 1.0;
    int program = -1;               // valor de sw_last; -1 = vale para todos os programas
    std::string programLabel;
};

struct SfzInstrument
{
    std::vector<SfzRegion> regions;
    std::vector<std::pair<int, std::string>> programs;   // (sw_last, nome), em ordem
    int defaultProgram = -1;
    bool hasAttack = false, hasRelease = false, hasCutoff = false;
    double attack = 0.0, release = 0.0, cutoff = 0.0;
    std::string error;
};

namespace sfzdetail
{
    using Opcodes = std::map<std::string, std::string>;

    inline std::string trim (const std::string& s)
    {
        size_t a = 0, b = s.size();
        while (a < b && std::isspace ((unsigned char) s[a])) ++a;
        while (b > a && std::isspace ((unsigned char) s[b - 1])) --b;
        return s.substr (a, b - a);
    }

    inline std::string slashes (std::string s)
    {
        std::replace (s.begin(), s.end(), '\\', '/');
        return s;
    }

    inline std::string dirOf (const std::string& path)
    {
        const auto p = slashes (path);
        const auto i = p.find_last_of ('/');
        return i == std::string::npos ? std::string() : p.substr (0, i + 1);
    }

    inline bool isAbsolute (const std::string& p)
    {
        return ! p.empty() && (p[0] == '/' || (p.size() > 1 && p[1] == ':'));
    }

    /** Numero MIDI a partir de "60" ou de um nome de nota ("c4", "F#2", "Bb3"). -1 se nao for nota. */
    inline int parseKey (const std::string& text)
    {
        const auto s = trim (text);
        if (s.empty()) return -1;

        if (std::isdigit ((unsigned char) s[0]) || (s[0] == '-' && s.size() > 1))
            return std::atoi (s.c_str());

        int semis;
        switch (std::tolower ((unsigned char) s[0]))
        {
            case 'c': semis = 0;  break;   case 'd': semis = 2;  break;   case 'e': semis = 4;  break;
            case 'f': semis = 5;  break;   case 'g': semis = 7;  break;   case 'a': semis = 9;  break;
            case 'b': semis = 11; break;   default: return -1;
        }
        size_t i = 1;
        if (i < s.size() && s[i] == '#') { ++semis; ++i; }
        else if (i < s.size() && (s[i] == 'b' || s[i] == 'B') && i + 1 < s.size()) { --semis; ++i; }
        if (i >= s.size()) return -1;
        return (std::atoi (s.c_str() + i) + 1) * 12 + semis;
    }

    inline bool isNameChar (char c)   { return std::isalnum ((unsigned char) c) || c == '_'; }

    /** Le um arquivo, resolvendo #include e #define, e devolve as linhas sem comentarios. */
    inline bool readLines (const std::string& path, const std::string& rootDir, std::vector<std::string>& out,
                           std::vector<std::pair<std::string, std::string>>& defines, int depth)
    {
        if (depth > 8) return false;
        std::ifstream in (path, std::ios::binary);
        if (! in) return false;

        std::string line;
        while (std::getline (in, line))
        {
            if (! line.empty() && line.back() == '\r') line.pop_back();
            if (line.size() >= 3 && (unsigned char) line[0] == 0xEF) line = line.substr (3);      // BOM

            const auto comment = line.find ("//");
            if (comment != std::string::npos) line = line.substr (0, comment);

            for (const auto& d : defines)                                // nomes mais longos primeiro
                for (size_t pos = line.find (d.first); pos != std::string::npos; pos = line.find (d.first, pos + d.second.size()))
                    line.replace (pos, d.first.size(), d.second);

            const auto t = trim (line);
            if (t.rfind ("#include", 0) == 0)
            {
                const auto a = t.find ('"'), b = t.rfind ('"');
                if (a != std::string::npos && b > a)
                {
                    const auto inc = slashes (t.substr (a + 1, b - a - 1));
                    readLines (isAbsolute (inc) ? inc : rootDir + inc, rootDir, out, defines, depth + 1);
                }
                continue;
            }
            if (t.rfind ("#define", 0) == 0)
            {
                std::istringstream ss (t.substr (7));
                std::string name, value;
                ss >> name;
                std::getline (ss, value);
                if (! name.empty() && name[0] == '$')
                {
                    defines.emplace_back (name, trim (value));
                    std::sort (defines.begin(), defines.end(), [] (const auto& x, const auto& y) { return x.first.size() > y.first.size(); });
                }
                continue;
            }
            if (! t.empty()) out.push_back (line);
        }
        return true;
    }

    inline double num (const Opcodes& o, const char* key, double fallback)
    {
        const auto it = o.find (key);
        return it == o.end() || trim (it->second).empty() ? fallback : std::atof (it->second.c_str());
    }

    inline bool has (const Opcodes& o, const char* key)   { return o.find (key) != o.end(); }
}

/** Le um arquivo .sfz. Em caso de falha, devolve instrumento vazio com 'error' preenchido. */
inline SfzInstrument parseSfz (const std::string& sfzPath)
{
    using namespace sfzdetail;
    SfzInstrument inst;
    const std::string rootDir = dirOf (sfzPath);

    std::vector<std::string> lines;
    std::vector<std::pair<std::string, std::string>> defines;
    if (! readLines (slashes (sfzPath), rootDir, lines, defines, 0))
    {
        inst.error = "nao consegui abrir o arquivo";
        return inst;
    }

    Opcodes control, global, master, group, region;
    Opcodes* current = nullptr;
    bool inRegion = false;
    int noteOffset = 0;

    auto finishRegion = [&]
    {
        if (! inRegion) return;
        inRegion = false;

        Opcodes o = global;
        for (const auto& kv : master) o[kv.first] = kv.second;
        for (const auto& kv : group)  o[kv.first] = kv.second;
        for (const auto& kv : region) o[kv.first] = kv.second;

        const auto sampleIt = o.find ("sample");
        if (sampleIt == o.end()) return;
        const auto sample = slashes (trim (sampleIt->second));
        if (sample.empty() || sample[0] == '*') return;                       // geradores (*sine etc.)
        if (has (o, "trigger") && trim (o["trigger"]) != "attack") return;    // samples de soltura: fora por enquanto

        SfzRegion r;
        std::string base = control.count ("default_path") ? slashes (trim (control["default_path"])) : std::string();
        if (! base.empty() && base.back() != '/') base += '/';
        r.samplePath = isAbsolute (sample) ? sample : (isAbsolute (base) ? base + sample : rootDir + base + sample);

        if (has (o, "key"))
        {
            const int k = parseKey (o["key"]);
            if (k >= 0) r.loKey = r.hiKey = r.root = k;
        }
        if (has (o, "lokey"))            { const int k = parseKey (o["lokey"]); if (k >= 0) r.loKey = k; }
        if (has (o, "hikey"))            { const int k = parseKey (o["hikey"]); if (k >= 0) r.hiKey = k; }
        if (has (o, "pitch_keycenter"))  { const int k = parseKey (o["pitch_keycenter"]); if (k >= 0) r.root = k; }
        r.loKey = std::clamp (r.loKey + noteOffset, 0, 127);
        r.hiKey = std::clamp (r.hiKey + noteOffset, 0, 127);
        r.root  = std::clamp (r.root + noteOffset, 0, 127);
        if (r.hiKey < r.loKey) return;

        r.loVel = (int) num (o, "lovel", 0);
        r.hiVel = (int) num (o, "hivel", 127);
        r.tuneCents = num (o, "tune", 0) + num (o, "pitch", 0) + 100.0 * num (o, "transpose", 0);
        r.pitchRandomCents = num (o, "pitch_random", 0);
        r.volumeDb = num (o, "volume", 0);
        r.offset = (long) num (o, "offset", 0);
        r.end = (long) num (o, "end", -1);
        if (r.end == 0) r.end = -1;
        r.loopStart = (long) num (o, "loop_start", -1);
        r.loopEnd = (long) num (o, "loop_end", -1);
        r.seqLength = std::max (1, (int) num (o, "seq_length", 1));
        r.seqPosition = std::max (1, (int) num (o, "seq_position", 1));
        r.loRand = num (o, "lorand", 0.0);
        r.hiRand = num (o, "hirand", 1.0);

        const auto mode = has (o, "loop_mode") ? trim (o["loop_mode"]) : std::string();
        if (mode == "loop_continuous")      r.loopMode = 1;
        else if (mode == "loop_sustain")    r.loopMode = 2;
        else if (mode == "one_shot")        r.loopMode = 3;
        if (r.loopMode == 1 || r.loopMode == 2)
            if (r.loopEnd <= r.loopStart || r.loopStart < 0)
                r.loopMode = 0;                                                // loop sem pontos validos: toca reto

        if (has (o, "sw_last"))
        {
            r.program = parseKey (o["sw_last"]);
            r.programLabel = has (o, "sw_label") ? trim (o["sw_label"]) : std::string();
            bool known = false;
            for (auto& p : inst.programs) known = known || p.first == r.program;
            if (! known)
                inst.programs.emplace_back (r.program, r.programLabel.empty() ? "Som " + std::to_string (inst.programs.size() + 1) : r.programLabel);
        }
        if (has (o, "sw_default") && inst.defaultProgram < 0)
            inst.defaultProgram = parseKey (o["sw_default"]);

        if (! inst.hasAttack && has (o, "ampeg_attack"))    { inst.hasAttack = true;  inst.attack = num (o, "ampeg_attack", 0); }
        if (! inst.hasRelease && has (o, "ampeg_release"))  { inst.hasRelease = true; inst.release = num (o, "ampeg_release", 0); }
        if (! inst.hasCutoff && has (o, "cutoff"))
        {
            const auto type = has (o, "fil_type") ? trim (o["fil_type"]) : std::string ("lpf_2p");
            if (type.rfind ("lpf", 0) == 0) { inst.hasCutoff = true; inst.cutoff = num (o, "cutoff", 20000); }
        }

        inst.regions.push_back (std::move (r));
    };

    for (const auto& line : lines)
    {
        // Localiza cabecalhos <nome> e opcodes nome=valor; o valor vai ate o proximo deles.
        struct Token { size_t start, valueStart; std::string name; bool header; };
        std::vector<Token> tokens;

        for (size_t i = 0; i < line.size(); ++i)
        {
            if (line[i] == '<')
            {
                const auto close = line.find ('>', i);
                if (close == std::string::npos) break;
                tokens.push_back ({ i, close + 1, line.substr (i + 1, close - i - 1), true });
                i = close;
            }
            else if (line[i] == '=' && i > 0 && isNameChar (line[i - 1]))
            {
                size_t a = i;
                while (a > 0 && isNameChar (line[a - 1])) --a;
                const bool boundary = a == 0 || std::isspace ((unsigned char) line[a - 1]) || line[a - 1] == '>';
                if (boundary && ! std::isdigit ((unsigned char) line[a]))
                    tokens.push_back ({ a, i + 1, line.substr (a, i - a), false });
            }
        }

        for (size_t t = 0; t < tokens.size(); ++t)
        {
            const auto& tok = tokens[t];
            if (tok.header)
            {
                finishRegion();
                if (tok.name == "region")       { region.clear(); current = &region; inRegion = true; }
                else if (tok.name == "group")   { group.clear(); current = &group; }
                else if (tok.name == "master")  { master.clear(); group.clear(); current = &master; }
                else if (tok.name == "global")  { global.clear(); master.clear(); group.clear(); current = &global; }
                else if (tok.name == "control") { current = &control; }
                else                            { current = nullptr; }      // <curve>, <effect>, <midi>...
                continue;
            }

            if (current == nullptr) continue;
            const size_t stop = t + 1 < tokens.size() ? tokens[t + 1].start : line.size();
            const auto value = trim (line.substr (tok.valueStart, stop - tok.valueStart));
            (*current)[tok.name] = value;

            if (current == &control && tok.name == "note_offset")   noteOffset = std::atoi (value.c_str());
            if (current == &control && tok.name == "octave_offset") noteOffset = 12 * std::atoi (value.c_str());
        }
    }
    finishRegion();

    std::sort (inst.programs.begin(), inst.programs.end());
    if (inst.regions.empty() && inst.error.empty())
        inst.error = "o arquivo nao tem nenhuma regiao com sample";
    return inst;
}

} // namespace drik
