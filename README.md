# DrikSampler

Sampler VST3 para o Reaper (Windows). Arraste um som, veja a onda, toque no teclado.

## Baixar

Em **Releases**, à direita desta página, fica sempre a última compilação:
`DrikSampler-Windows.zip`. As instruções de instalação estão no `LEIA-ME-instalacao.txt` dentro do zip.

## O que a versão atual faz

**Um som só**

- Carrega um arquivo de áudio (WAV, AIFF, FLAC, MP3) por arrastar e soltar ou pelo botão **Abrir...**
- Mostra a onda, com marcadores de **início** e **fim** que se arrastam com o mouse (dois cliques na onda voltam ao som inteiro)
- Ao carregar, encosta os marcadores onde o som realmente começa e termina
- **Loop** entre os marcadores enquanto a tecla está presa, com emenda suave ajustável
- Descobre a nota original do som, pelo nome do arquivo (`Rhodes_C4.wav`) ou pela análise do áudio

**Instrumento com vários samples (multisample)**

- Abre instrumentos no formato **SFZ**: arraste o arquivo `.sfz`, ou a pasta inteira da biblioteca
- Quando o SFZ traz vários sons no mesmo arquivo, aparece uma lista para escolher
- Arrastar **vários arquivos de áudio** de uma vez monta um instrumento: cada um vai para a tecla da sua nota
  e cobre as teclas vizinhas; sons sem altura (percussão) ficam um por tecla, a partir do C2
- No lugar da onda aparece o mapa do teclado, mostrando que sample cobre que teclas
- Entende camadas por força da tecla, rodízio entre samples da mesma nota e os loops de cada sample

**Em qualquer dos dois**

- Toca pelo teclado MIDI, pelo piano roll, pelo teclado da tela ou pelo teclado do computador
  (`A S D F G H J K`, com `Z` e `X` mudando a oitava)
- Envelope ADSR, filtro passa-baixa com ressonância, afinação e volume
- Sensibilidade à força da tecla (em 0%, toda nota sai no volume cheio)
- O projeto do Reaper guarda o instrumento carregado e a posição dos botões
- O número da versão aparece ao lado do título

## Bibliotecas

Qualquer biblioteca em SFZ simples deve abrir. Testado com duas coleções gratuitas de samples de mellotron:

- [Mellotron-SFZ](https://github.com/ExistentiaVirae/Mellotron-SFZ): 23 instrumentos, um arquivo `.sfz` por instrumento
- [NarwhalAudio/mellotron](https://github.com/NarwhalAudio/mellotron): 23 sons num arquivo `.sfz` só, escolhidos pela lista

As duas trazem as suas próprias condições de uso; leia o que acompanha cada uma. Os samples não fazem parte
deste repositório.

## O que ainda não faz

- Salvar como SFZ um instrumento montado com arquivos soltos
- Samples de soltura de tecla, efeitos e moduladores do SFZ (são ignorados)
- Zoom na onda
- Carregar do disco aos poucos: tudo vai para a memória (limite de 1,5 GB por instrumento)

## Compilar

O GitHub compila sozinho a cada alteração (`.github/workflows/build.yml`). Para compilar à mão:

```
cmake -B build
cmake --build build --config Release
```

Testes do motor de áudio e do leitor de SFZ, sem interface:

```
cmake -B build -DDRIK_BUILD_TESTS=ON
cmake --build build --target DrikEngineTest
```

Feito com [JUCE](https://juce.com) 8.
