# DrikSampler

Sampler VST3 para o Reaper (Windows). Arraste um som, veja a onda, toque no teclado.

## Baixar

Em **Releases**, à direita desta página, fica sempre a última compilação:
`DrikSampler-Windows.zip`. As instruções de instalação estão no `LEIA-ME-instalacao.txt` dentro do zip.

## O que a versão atual faz

- Carrega um arquivo de áudio (WAV, AIFF, FLAC, MP3) por arrastar e soltar ou pelo botão **Abrir...**
- Mostra a onda, com marcadores de **início** e **fim** que se arrastam com o mouse (dois cliques na onda voltam ao som inteiro)
- Ao carregar, encosta os marcadores onde o som realmente começa e termina, deixando o silêncio de fora
- **Loop** entre os marcadores enquanto a tecla está presa, com emenda suave ajustável
- Descobre a nota original do som, pelo nome do arquivo (`Rhodes_C4.wav`) ou pela análise do áudio; em trecho de música, onde não existe uma nota só, fica em C4
- Toca o som em qualquer nota, pelo teclado MIDI, pelo piano roll ou pelo teclado da tela
- Envelope ADSR, filtro passa-baixa com ressonância, afinação e volume
- Sensibilidade à força da tecla (em 0%, toda nota sai no volume cheio)
- O projeto do Reaper guarda o som carregado, os marcadores e a posição dos botões
- O número da versão aparece ao lado do título

## O que ainda não faz

- Vários samples no mesmo instrumento (multisample)
- Importar SFZ
- Banco de instrumentos salvos
- Zoom na onda

## Compilar

O GitHub compila sozinho a cada alteração (`.github/workflows/build.yml`). Para compilar à mão:

```
cmake -B build
cmake --build build --config Release
```

Testes do motor de áudio, sem interface:

```
cmake -B build -DDRIK_BUILD_TESTS=ON
cmake --build build --target DrikEngineTest
```

Feito com [JUCE](https://juce.com) 8.
