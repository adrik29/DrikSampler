// Teste do leitor de SFZ:  g++ -std=c++17 -O2 tests/test_sfz.cpp -o t && ./t [arquivo.sfz ...]
#include "../Source/SfzParser.h"
#include <cstdio>
#include <sys/stat.h>

static int fails = 0;
static void check (bool ok, const char* what) { if (! ok) { ++fails; std::printf ("FALHA  %s\n", what); } }

static void write (const std::string& path, const std::string& text) { std::ofstream (path, std::ios::binary) << text; }

int main (int argc, char** argv)
{
    const std::string dir = "/tmp/drik_sfz_test/";
    mkdir (dir.c_str(), 0755);

    // Estilo 1: uma regiao por tecla, numeros, barras invertidas, '#' no nome do arquivo.
    write (dir + "a.sfz",
           "<group>\r\nloop_mode=no_loop\r\nampeg_attack=0.001\r\nfil_type=lpf_2p\r\ncutoff=19913\r\n\r\n"
           "<region>\r\nsample=samples\\G#2.wav\r\nlokey=44 hikey=44\r\npitch_keycenter=44\r\noffset=0\r\nend=170926\r\nloop_start=0\r\nloop_end=0\r\n"
           "<region>\r\nsample=samples\\A2.wav\r\nlokey=45 hikey=46\r\npitch_keycenter=45\r\n");
    auto a = drik::parseSfz (dir + "a.sfz");
    check (a.error.empty() && a.regions.size() == 2, "estilo 1: duas regioes");
    if (a.regions.size() == 2)
    {
        check (a.regions[0].samplePath == dir + "samples/G#2.wav", "estilo 1: caminho com # e barra invertida");
        check (a.regions[0].loKey == 44 && a.regions[0].hiKey == 44 && a.regions[0].root == 44, "estilo 1: teclas");
        check (a.regions[0].end == 170926 && a.regions[0].loopMode == 0, "estilo 1: fim e sem loop");
        check (a.regions[1].loKey == 45 && a.regions[1].hiKey == 46, "estilo 1: faixa de duas teclas");
    }
    check (a.hasAttack && a.attack == 0.001 && a.hasCutoff && a.cutoff == 19913, "estilo 1: ataque e corte do grupo");
    check (a.programs.empty(), "estilo 1: sem programas");

    // Estilo 2: #include, default_path com espacos e '&', notas por nome, programas por keyswitch.
    write (dir + "map.sfz", "<region> key=G2 sample=G2.wav\n<region> key=c#3 sample=C#3.wav // comentario\n");
    write (dir + "b.sfz",
           "<global>\namp_velcurve_1=1\nsw_lokey=0 sw_hikey=24 sw_default=1\npitch=-15\npitch_random=30\n\n"
           "// Metais\n<control>\ndefault_path=./Samples/Brass/Tenor & Alto Sax/\n\n<group>\nsw_last=0 sw_label=Tenor & Alto Sax\n#include \"map.sfz\"\n\n"
           "<control>\ndefault_path=./Samples/Voices/8 Choir/\n\n<group>\nsw_last=1 sw_label=8 Choir\n#include \"map.sfz\"\n");
    auto b = drik::parseSfz (dir + "b.sfz");
    check (b.error.empty() && b.regions.size() == 4, "estilo 2: quatro regioes via include");
    check (b.programs.size() == 2 && b.programs[0].second == "Tenor & Alto Sax" && b.programs[1].second == "8 Choir", "estilo 2: dois programas com nome");
    check (b.defaultProgram == 1, "estilo 2: programa padrao");
    if (b.regions.size() == 4)
    {
        check (b.regions[0].samplePath == dir + "./Samples/Brass/Tenor & Alto Sax/G2.wav", "estilo 2: default_path com espacos");
        check (b.regions[0].root == 43 && b.regions[1].root == 49, "estilo 2: notas por nome (G2 = 43, C#3 = 49)");
        check (b.regions[0].program == 0 && b.regions[3].program == 1, "estilo 2: regioes nos programas certos");
        check (b.regions[3].samplePath == dir + "./Samples/Voices/8 Choir/C#3.wav", "estilo 2: default_path trocado no meio");
        check (b.regions[0].tuneCents == -15 && b.regions[0].pitchRandomCents == 30, "estilo 2: afinacao herdada do global");
    }

    // Estilo 3: #define, velocidade, loop, transpose, round robin, release ignorado, master.
    write (dir + "c.sfz",
           "#define $EXT flac\n#define $ROOT 60\n<master> volume=-6\n<group> lovel=1 hivel=64 loop_mode=loop_continuous\n"
           "<region> sample=p.$EXT lokey=c3 hikey=b4 pitch_keycenter=$ROOT loop_start=1000 loop_end=5000 transpose=12 tune=7\n"
           "<group> lovel=65 hivel=127 seq_length=2\n<region> sample=f1.$EXT seq_position=1 <region> sample=f2.$EXT seq_position=2 loop_mode=one_shot\n"
           "<group> trigger=release\n<region> sample=rel.$EXT\n<region> sample=*sine\n");
    auto c = drik::parseSfz (dir + "c.sfz");
    check (c.regions.size() == 3, "estilo 3: tres regioes (soltura e gerador ficam de fora)");
    if (c.regions.size() == 3)
    {
        check (c.regions[0].samplePath == dir + "p.flac" && c.regions[0].root == 60, "estilo 3: #define");
        check (c.regions[0].loKey == 48 && c.regions[0].hiKey == 71, "estilo 3: faixa por nome");
        check (c.regions[0].loVel == 1 && c.regions[0].hiVel == 64 && c.regions[1].loVel == 65, "estilo 3: camadas de velocidade");
        check (c.regions[0].loopMode == 1 && c.regions[0].loopStart == 1000 && c.regions[0].loopEnd == 5000, "estilo 3: loop");
        check (c.regions[0].tuneCents == 1207 && c.regions[0].volumeDb == -6, "estilo 3: transpose, tune e volume do master");
        check (c.regions[1].seqLength == 2 && c.regions[2].seqPosition == 2 && c.regions[2].loopMode == 3, "estilo 3: round robin e one_shot na mesma linha");
    }

    check (! drik::parseSfz (dir + "nao_existe.sfz").error.empty(), "arquivo inexistente: erro, sem travar");
    write (dir + "vazio.sfz", "// nada\n<group> volume=3\n");
    check (! drik::parseSfz (dir + "vazio.sfz").error.empty(), "arquivo sem regioes: erro");

    // Arquivos reais passados na linha de comando: resumo.
    for (int i = 1; i < argc; ++i)
    {
        auto r = drik::parseSfz (argv[i]);
        int lo = 127, hi = 0, missing = 0;
        for (auto& z : r.regions)
        {
            lo = std::min (lo, z.loKey); hi = std::max (hi, z.hiKey);
            if (! std::ifstream (z.samplePath)) ++missing;
        }
        std::printf ("%-52s %4zu regioes  teclas %d-%d  programas %zu  arquivos ausentes %d  %s\n",
                     argv[i] + std::max<long> (0, (long) std::string (argv[i]).size() - 52), r.regions.size(), lo, hi, r.programs.size(), missing, r.error.c_str());
    }

    std::printf (fails == 0 ? "tudo certo\n" : "%d falhas\n", fails);
    return fails;
}
