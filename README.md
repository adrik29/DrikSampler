# DrikSampler

Sampler VST3 para o Reaper (Windows). Arraste um som, veja a onda, toque no teclado.

## Baixar

Em **Releases**, à direita desta página, fica sempre a última compilação:
`DrikSampler-Windows.zip`. As instruções de instalação estão no `LEIA-ME-instalacao.txt` dentro do zip.

## O que a versão 0.1 faz

- Carrega um arquivo de áudio (WAV, AIFF, FLAC, MP3) por arrastar e soltar ou pelo botão **Abrir...**
- Mostra a onda
- Descobre a nota original do som, pelo nome do arquivo (`Rhodes_C4.wav`) ou pela análise do áudio
- Toca o som em qualquer nota, pelo teclado MIDI, pelo piano roll ou pelo teclado da tela
- Envelope ADSR, filtro passa-baixa com ressonância, afinação e volume
- O projeto do Reaper guarda o som carregado e a posição dos botões

## O que ainda não faz

- Vários samples no mesmo instrumento (multisample)
- Marcadores de início, fim e loop na onda
- Importar SFZ
- Banco de instrumentos salvos

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
