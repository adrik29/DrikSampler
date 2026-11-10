#include "PluginEditor.h"

namespace
{
    const juce::Colour bgColour     { 0xff16181d };
    const juce::Colour panelColour  { 0xff20242c };
    const juce::Colour accentColour { 0xffe8a33d };
    const juce::Colour textColour   { 0xffe6e6e6 };
    const juce::Colour dimColour    { 0xff8a909c };

    // Textos com acento: sempre em UTF-8 explicito, para nao depender do compilador.
    juce::String utf8 (const char* s)   { return juce::String::fromUTF8 (s); }
}

//==============================================================================
WaveformView::WaveformView (juce::AudioFormatManager& fm) : thumbnail (512, fm, cache)
{
    thumbnail.addChangeListener (this);
}

WaveformView::~WaveformView()
{
    thumbnail.removeChangeListener (this);
}

void WaveformView::setFile (const juce::File& f)
{
    if (f.existsAsFile())
        thumbnail.setSource (new juce::FileInputSource (f));
    else
        thumbnail.clear();

    repaint();
}

void WaveformView::setDragHighlight (bool shouldHighlight)
{
    highlight = shouldHighlight;
    repaint();
}

void WaveformView::paint (juce::Graphics& g)
{
    auto area = getLocalBounds().toFloat();

    g.setColour (panelColour);
    g.fillRoundedRectangle (area, 6.0f);

    if (thumbnail.getTotalLength() > 0.0)
    {
        g.setColour (accentColour);
        thumbnail.drawChannels (g, getLocalBounds().reduced (8, 6), 0.0, thumbnail.getTotalLength(), 1.0f);
    }
    else
    {
        g.setColour (dimColour);
        g.setFont (17.0f);
        g.drawFittedText (utf8 ("Arraste um arquivo de \xc3\xa1" "udio para c\xc3\xa1"),
                          getLocalBounds(), juce::Justification::centred, 1);
    }

    g.setColour (highlight ? accentColour : panelColour.brighter (0.25f));
    g.drawRoundedRectangle (area.reduced (0.75f), 6.0f, highlight ? 2.0f : 1.0f);
}

//==============================================================================
Knob::Knob (juce::AudioProcessorValueTreeState& state, const juce::String& paramID, const juce::String& titleText)
    : attachment (state, paramID, slider)
{
    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 78, 18);
    slider.setColour (juce::Slider::rotarySliderFillColourId, accentColour);
    slider.setColour (juce::Slider::rotarySliderOutlineColourId, panelColour.brighter (0.35f));
    slider.setColour (juce::Slider::thumbColourId, textColour);
    slider.setColour (juce::Slider::textBoxTextColourId, textColour);
    slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    addAndMakeVisible (slider);

    label.setText (titleText, juce::dontSendNotification);
    label.setJustificationType (juce::Justification::centred);
    label.setColour (juce::Label::textColourId, dimColour);
    label.setFont (juce::FontOptions (13.0f));
    addAndMakeVisible (label);
}

void Knob::resized()
{
    auto area = getLocalBounds();
    label.setBounds (area.removeFromBottom (18));
    slider.setBounds (area);
}

//==============================================================================
DrikSamplerEditor::DrikSamplerEditor (DrikSamplerProcessor& p)
    : AudioProcessorEditor (p),
      processor (p),
      waveform (p.formatManager),
      keyboard (p.keyboardState, juce::MidiKeyboardComponent::horizontalKeyboard)
{
    title.setText ("DrikSampler", juce::dontSendNotification);
    title.setFont (juce::FontOptions (22.0f, juce::Font::bold));
    title.setColour (juce::Label::textColourId, accentColour);
    addAndMakeVisible (title);

    fileLabel.setColour (juce::Label::textColourId, textColour);
    fileLabel.setFont (juce::FontOptions (15.0f));
    fileLabel.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (fileLabel);

    openButton.setButtonText ("Abrir...");
    openButton.setColour (juce::TextButton::buttonColourId, panelColour);
    openButton.setColour (juce::TextButton::textColourOffId, textColour);
    openButton.onClick = [this] { openFile(); };
    addAndMakeVisible (openButton);

    addAndMakeVisible (waveform);

    struct Def { const char* id; const char* title; };
    const Def defs[] = {
        { "root",      "Nota original" },
        { "tune",      "Afina\xc3\xa7\xc3\xa3" "o" },
        { "attack",    "Ataque" },
        { "decay",     "Decay" },
        { "sustain",   "Sustain" },
        { "release",   "Release" },
        { "cutoff",    "Filtro" },
        { "resonance", "Resson\xc3\xa2" "ncia" },
        { "gain",      "Volume" } };

    for (auto& d : defs)
        addAndMakeVisible (knobs.add (new Knob (processor.apvts, d.id, utf8 (d.title))));

    keyboard.setAvailableRange (12, 120);
    keyboard.setOctaveForMiddleC (4);
    keyboard.setKeyWidth (14.0f);
    keyboard.setLowestVisibleKey (24);
    keyboard.setColour (juce::MidiKeyboardComponent::keyDownOverlayColourId, accentColour);
    keyboard.setColour (juce::MidiKeyboardComponent::mouseOverKeyOverlayColourId, accentColour.withAlpha (0.4f));
    addAndMakeVisible (keyboard);

    processor.addChangeListener (this);
    refreshFromProcessor();

    setSize (900, 540);
}

DrikSamplerEditor::~DrikSamplerEditor()
{
    processor.removeChangeListener (this);
}

void DrikSamplerEditor::paint (juce::Graphics& g)
{
    g.fillAll (bgColour);
}

void DrikSamplerEditor::resized()
{
    auto area = getLocalBounds().reduced (14);

    auto top = area.removeFromTop (34);
    title.setBounds (top.removeFromLeft (170));
    openButton.setBounds (top.removeFromRight (90).reduced (0, 3));
    top.removeFromRight (10);
    fileLabel.setBounds (top);

    area.removeFromTop (10);
    keyboard.setBounds (area.removeFromBottom (84));
    area.removeFromBottom (12);

    auto knobRow = area.removeFromBottom (108);
    area.removeFromBottom (12);
    waveform.setBounds (area);

    const int w = knobRow.getWidth() / juce::jmax (1, knobs.size());
    for (auto* k : knobs)
        k->setBounds (knobRow.removeFromLeft (w));
}

//==============================================================================
void DrikSamplerEditor::refreshFromProcessor()
{
    const auto file = processor.getCurrentFile();
    waveform.setFile (file);
    fileLabel.setText (file.existsAsFile() ? file.getFileName() : utf8 ("nenhum som carregado"),
                       juce::dontSendNotification);
}

void DrikSamplerEditor::tryLoad (const juce::File& file)
{
    if (! processor.loadFile (file, true))
        fileLabel.setText (utf8 ("N\xc3\xa3" "o consegui abrir: ") + file.getFileName(), juce::dontSendNotification);
}

void DrikSamplerEditor::filesDropped (const juce::StringArray& files, int, int)
{
    waveform.setDragHighlight (false);

    if (! files.isEmpty())
        tryLoad (juce::File (files[0]));
}

void DrikSamplerEditor::openFile()
{
    auto start = processor.getCurrentFile();
    if (! start.existsAsFile())
        start = juce::File::getSpecialLocation (juce::File::userMusicDirectory);

    chooser = std::make_unique<juce::FileChooser> ("Escolha um som", start,
                                                   processor.formatManager.getWildcardForAllFormats());

    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this] (const juce::FileChooser& fc)
                          {
                              const auto f = fc.getResult();
                              if (f.existsAsFile())
                                  tryLoad (f);
                          });
}
