#pragma once

#include "PluginProcessor.h"

//==============================================================================
/** Desenho da onda do som carregado. */
class WaveformView final : public juce::Component,
                           private juce::ChangeListener
{
public:
    explicit WaveformView (juce::AudioFormatManager&);
    ~WaveformView() override;

    void setFile (const juce::File&);
    void setDragHighlight (bool);
    void paint (juce::Graphics&) override;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override   { repaint(); }

    juce::AudioThumbnailCache cache { 4 };
    juce::AudioThumbnail thumbnail;
    bool highlight = false;
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

    juce::Label title, fileLabel;
    juce::TextButton openButton;
    WaveformView waveform;
    juce::OwnedArray<Knob> knobs;
    juce::MidiKeyboardComponent keyboard;
    std::unique_ptr<juce::FileChooser> chooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DrikSamplerEditor)
};
