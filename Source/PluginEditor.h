#pragma once

#include "PluginProcessor.h"

//==============================================================================
/** Desenho da onda, com os marcadores de inicio e fim que se arrastam com o mouse. */
class WaveformView final : public juce::Component,
                           private juce::ChangeListener,
                           private juce::Timer
{
public:
    WaveformView (juce::AudioFormatManager&, juce::AudioProcessorValueTreeState&);
    ~WaveformView() override;

    void setFile (const juce::File&);
    void setDragHighlight (bool);

    void paint (juce::Graphics&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

private:
    enum class Marker { none, start, end };

    void changeListenerCallback (juce::ChangeBroadcaster*) override   { repaint(); }
    void timerCallback() override;

    juce::Rectangle<float> waveArea() const;
    float xFor (float normalised) const;
    Marker markerAt (float x) const;
    void setMarker (Marker, float normalised);
    bool hasSound() const   { return thumbnail.getTotalLength() > 0.0; }

    juce::AudioProcessorValueTreeState& state;
    juce::RangedAudioParameter* startParam = nullptr;
    juce::RangedAudioParameter* endParam = nullptr;
    std::atomic<float>* loopValue = nullptr;

    juce::AudioThumbnailCache cache { 4 };
    juce::AudioThumbnail thumbnail;
    bool highlight = false;
    Marker dragging = Marker::none;
    float lastStart = -1.0f, lastEnd = -1.0f, lastLoop = -1.0f;
};

//==============================================================================
/** Mapa do instrumento multisample: que sample cobre que teclas, alinhado com o teclado da tela. */
class MapView final : public juce::Component
{
public:
    std::function<juce::Rectangle<float> (int)> keyRectangle;     // posicao de cada tecla no teclado de baixo

    void setInfo (const InstrumentInfo& i)   { info = i; repaint(); }
    void setDragHighlight (bool h)           { highlight = h; repaint(); }
    void paint (juce::Graphics&) override;

private:
    InstrumentInfo info;
    bool highlight = false;
};

/** Teclado da tela em que Z e X mudam a oitava do teclado do computador. */
class OctaveKeyboard final : public juce::MidiKeyboardComponent
{
public:
    explicit OctaveKeyboard (juce::MidiKeyboardState& s)
        : juce::MidiKeyboardComponent (s, horizontalKeyboard), state (s)
    {
        setKeyPressBaseOctave (octave);
    }

    int getOctave() const   { return octave; }
    std::function<void()> onOctaveChange;

    bool keyPressed (const juce::KeyPress& key) override
    {
        const auto c = juce::CharacterFunctions::toLowerCase (key.getTextCharacter());
        if (c == 'z' || c == 'x')
        {
            const int next = juce::jlimit (1, 9, octave + (c == 'x' ? 1 : -1));
            if (next != octave)
            {
                state.allNotesOff (1);
                octave = next;
                setKeyPressBaseOctave (octave);
                if (onOctaveChange) onOctaveChange();
            }
            return true;
        }
        return juce::MidiKeyboardComponent::keyPressed (key);
    }

private:
    juce::MidiKeyboardState& state;
    int octave = 5;
};

//==============================================================================
/** Um botao giratorio com o nome embaixo. */
class Knob final : public juce::Component
{
public:
    Knob (juce::AudioProcessorValueTreeState&, const juce::String& paramID, const juce::String& title);
    void resized() override;
    const juce::String id;

private:
    juce::Slider slider;
    juce::Label label;
    juce::AudioProcessorValueTreeState::SliderAttachment attachment;
};

//==============================================================================
class DrikSamplerEditor final : public juce::AudioProcessorEditor,
                                public juce::FileDragAndDropTarget,
                                private juce::ChangeListener
{
public:
    explicit DrikSamplerEditor (DrikSamplerProcessor&);
    ~DrikSamplerEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    bool isInterestedInFileDrag (const juce::StringArray&) override   { return true; }
    void fileDragEnter (const juce::StringArray&, int, int) override  { waveform.setDragHighlight (true);  mapView.setDragHighlight (true); }
    void fileDragExit (const juce::StringArray&) override             { waveform.setDragHighlight (false); mapView.setDragHighlight (false); }
    void filesDropped (const juce::StringArray&, int, int) override;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override   { refreshFromProcessor(); }
    void refreshFromProcessor();
    void openFile();
    void tryLoad (const juce::Array<juce::File>&);
    void showOctave();
    void rebuildList();
    void chooseFromList (int itemId);
    void stepList (int delta);
    void askNameAndSave();
    void showFolderMenu();
    void chooseFolder (bool forSamples);

    DrikSamplerProcessor& processor;

    juce::Label title, versionLabel, octaveLabel;
    juce::ComboBox programBox;
    MapView mapView;
    bool refreshing = false;
    int chosenIndex = -1;          // item da lista que o usuario escolheu por ultimo (-1 = veio de fora da lista)
    juce::TextButton openButton, saveButton { "Salvar..." }, folderButton { "Pastas..." }, prevButton { "<" }, nextButton { ">" };
    juce::ComboBox instrumentBox;
    juce::Array<juce::File> favorites;
    std::vector<DrikSamplerProcessor::LibraryEntry> library;
    juce::TooltipWindow tooltips { this, 600 };
    juce::ToggleButton loopButton;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> loopAttachment;
    WaveformView waveform;
    juce::OwnedArray<Knob> knobs;
    OctaveKeyboard keyboard;
    std::unique_ptr<juce::FileChooser> chooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DrikSamplerEditor)
};
