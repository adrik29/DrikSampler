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
/** Um botao giratorio com o nome embaixo. */
class Knob final : public juce::Component
{
public:
    Knob (juce::AudioProcessorValueTreeState&, const juce::String& paramID, const juce::String& title);
    void resized() override;

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
    void fileDragEnter (const juce::StringArray&, int, int) override  { waveform.setDragHighlight (true); }
    void fileDragExit (const juce::StringArray&) override             { waveform.setDragHighlight (false); }
    void filesDropped (const juce::StringArray&, int, int) override;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override   { refreshFromProcessor(); }
    void refreshFromProcessor();
    void openFile();
    void tryLoad (const juce::File&);

    DrikSamplerProcessor& processor;

    juce::Label title, versionLabel, fileLabel;
    juce::TextButton openButton;
    juce::ToggleButton loopButton;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> loopAttachment;
    WaveformView waveform;
    juce::OwnedArray<Knob> knobs;
    juce::MidiKeyboardComponent keyboard;
    std::unique_ptr<juce::FileChooser> chooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DrikSamplerEditor)
};
