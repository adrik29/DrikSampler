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
WaveformView::WaveformView (juce::AudioFormatManager& fm, juce::AudioProcessorValueTreeState& s)
    : state (s), thumbnail (512, fm, cache)
{
    startParam = state.getParameter ("start");
    endParam   = state.getParameter ("end");
    loopValue  = state.getRawParameterValue ("loop");

    thumbnail.addChangeListener (this);
    startTimerHz (30);
}

WaveformView::~WaveformView()
{
    stopTimer();
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

void WaveformView::timerCallback()
{
    // Redesenha quando os marcadores mudam por fora (automacao, projeto reaberto, som novo).
    const float s = startParam->getValue(), e = endParam->getValue(), l = loopValue->load();
    if (s != lastStart || e != lastEnd || l != lastLoop)
    {
        lastStart = s; lastEnd = e; lastLoop = l;
        repaint();
    }
}

juce::Rectangle<float> WaveformView::waveArea() const
{
    return getLocalBounds().toFloat().reduced (10.0f, 6.0f);
}

float WaveformView::xFor (float normalised) const
{
    const auto area = waveArea();
    return area.getX() + normalised * area.getWidth();
}

WaveformView::Marker WaveformView::markerAt (float x) const
{
    const float ds = std::abs (x - xFor (startParam->getValue()));
    const float de = std::abs (x - xFor (endParam->getValue()));
    const float reach = 9.0f;

    if (ds > reach && de > reach)
        return Marker::none;

    // Com os dois juntos, escolhe pelo lado em que o mouse esta.
    if (std::abs (ds - de) < 1.0f)
        return x < xFor (startParam->getValue()) ? Marker::start : Marker::end;

    return ds < de ? Marker::start : Marker::end;
}

void WaveformView::setMarker (Marker m, float normalised)
{
    const float minGap = 0.002f;

    if (m == Marker::start)
        startParam->setValueNotifyingHost (juce::jlimit (0.0f, juce::jmax (0.0f, endParam->getValue() - minGap), normalised));
    else if (m == Marker::end)
        endParam->setValueNotifyingHost (juce::jlimit (juce::jmin (1.0f, startParam->getValue() + minGap), 1.0f, normalised));
}

void WaveformView::mouseMove (const juce::MouseEvent& e)
{
    setMouseCursor (hasSound() && markerAt ((float) e.x) != Marker::none ? juce::MouseCursor::LeftRightResizeCursor
                                                                         : juce::MouseCursor::NormalCursor);
}

void WaveformView::mouseDown (const juce::MouseEvent& e)
{
    if (! hasSound())
        return;

    dragging = markerAt ((float) e.x);

    if (dragging == Marker::start) startParam->beginChangeGesture();
    if (dragging == Marker::end)   endParam->beginChangeGesture();

    mouseDrag (e);
}

void WaveformView::mouseDrag (const juce::MouseEvent& e)
{
    if (dragging == Marker::none)
        return;

    const auto area = waveArea();
    setMarker (dragging, ((float) e.x - area.getX()) / area.getWidth());
}

void WaveformView::mouseUp (const juce::MouseEvent&)
{
    if (dragging == Marker::start) startParam->endChangeGesture();
    if (dragging == Marker::end)   endParam->endChangeGesture();
    dragging = Marker::none;
}

void WaveformView::mouseDoubleClick (const juce::MouseEvent&)
{
    // Dois cliques: volta a usar o som inteiro.
    if (! hasSound())
        return;

    startParam->setValueNotifyingHost (0.0f);
    endParam->setValueNotifyingHost (1.0f);
}

void WaveformView::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();

    g.setColour (panelColour);
    g.fillRoundedRectangle (bounds, 6.0f);

    if (hasSound())
    {
        const auto area = waveArea();
        const float xs = xFor (startParam->getValue());
        const float xe = xFor (endParam->getValue());
        const bool looping = loopValue->load() > 0.5f;

        if (looping)
        {
            g.setColour (accentColour.withAlpha (0.08f));
            g.fillRect (juce::Rectangle<float> (xs, area.getY(), xe - xs, area.getHeight()));
        }

        g.setColour (accentColour);
        thumbnail.drawChannels (g, area.toNearestInt(), 0.0, thumbnail.getTotalLength(), 1.0f);

        // Escurece o que fica de fora.
        g.setColour (bgColour.withAlpha (0.72f));
        g.fillRect (juce::Rectangle<float> (area.getX(), area.getY(), xs - area.getX(), area.getHeight()));
        g.fillRect (juce::Rectangle<float> (xe, area.getY(), area.getRight() - xe, area.getHeight()));

        auto drawMarker = [&] (float x, bool isStart, const juce::String& text)
        {
            g.setColour (textColour);
            g.fillRect (juce::Rectangle<float> (x - 0.75f, area.getY(), 1.5f, area.getHeight()));

            const float w = 44.0f, h = 16.0f;
            const juce::Rectangle<float> tab (isStart ? x : x - w, area.getY(), w, h);
            g.fillRoundedRectangle (tab, 3.0f);
            g.setColour (bgColour);
            g.setFont (12.0f);
            g.drawText (text, tab, juce::Justification::centred);
        };

        drawMarker (xs, true,  utf8 ("in\xc3\xad" "cio"));
        drawMarker (xe, false, looping ? juce::String ("loop") : juce::String ("fim"));
    }
    else
    {
        g.setColour (dimColour);
        g.setFont (17.0f);
        g.drawFittedText (utf8 ("Arraste um arquivo de \xc3\xa1" "udio para c\xc3\xa1"),
                          getLocalBounds(), juce::Justification::centred, 1);
    }

    g.setColour (highlight ? accentColour : panelColour.brighter (0.25f));
    g.drawRoundedRectangle (bounds.reduced (0.75f), 6.0f, highlight ? 2.0f : 1.0f);
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
      waveform (p.formatManager, p.apvts),
      keyboard (p.keyboardState, juce::MidiKeyboardComponent::horizontalKeyboard)
{
    title.setText ("DrikSampler", juce::dontSendNotification);
    title.setFont (juce::FontOptions (22.0f, juce::Font::bold));
    title.setColour (juce::Label::textColourId, accentColour);
    addAndMakeVisible (title);

    // Versao e data da compilacao, para saber de relance qual copia esta aberta.
    versionLabel.setText (juce::String ("v") + DRIK_VERSION + "  (" + juce::Time::getCompilationDate().formatted ("%d/%m/%Y") + ")",
                          juce::dontSendNotification);
    versionLabel.setFont (juce::FontOptions (13.0f));
    versionLabel.setColour (juce::Label::textColourId, dimColour);
    addAndMakeVisible (versionLabel);

    fileLabel.setColour (juce::Label::textColourId, textColour);
    fileLabel.setFont (juce::FontOptions (15.0f));
    fileLabel.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (fileLabel);

    openButton.setButtonText ("Abrir...");
    openButton.setColour (juce::TextButton::buttonColourId, panelColour);
    openButton.setColour (juce::TextButton::textColourOffId, textColour);
    openButton.onClick = [this] { openFile(); };
    addAndMakeVisible (openButton);

    loopButton.setButtonText ("Loop");
    loopButton.setColour (juce::ToggleButton::textColourId, textColour);
    loopButton.setColour (juce::ToggleButton::tickColourId, accentColour);
    loopButton.setColour (juce::ToggleButton::tickDisabledColourId, dimColour);
    loopAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (processor.apvts, "loop", loopButton);
    addAndMakeVisible (loopButton);

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
        { "xfade",     "Emenda" },
        { "velsens",   "Sensibilidade" },
        { "gain",      "Volume" } };

    for (auto& d : defs)
        addAndMakeVisible (knobs.add (new Knob (processor.apvts, d.id, utf8 (d.title))));

    keyboard.setAvailableRange (12, 120);
    keyboard.setOctaveForMiddleC (4);
    keyboard.setKeyWidth (14.0f);
    // Clique do mouse sempre com forca total (o padrao do JUCE varia a forca conforme a altura do clique na tecla).
    keyboard.setVelocity (1.0f, false);
    keyboard.setLowestVisibleKey (24);
    keyboard.setColour (juce::MidiKeyboardComponent::keyDownOverlayColourId, accentColour);
    keyboard.setColour (juce::MidiKeyboardComponent::mouseOverKeyOverlayColourId, accentColour.withAlpha (0.4f));
    addAndMakeVisible (keyboard);

    processor.addChangeListener (this);
    refreshFromProcessor();

    setSize (980, 560);
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
    title.setBounds (top.removeFromLeft (140));
    versionLabel.setBounds (top.removeFromLeft (190).withTrimmedTop (5));
    openButton.setBounds (top.removeFromRight (90).reduced (0, 3));
    top.removeFromRight (10);
    loopButton.setBounds (top.removeFromRight (74));
    top.removeFromRight (10);
    fileLabel.setBounds (top);

    area.removeFromTop (10);
    keyboard.setBounds (area.removeFromBottom (84));
    keyboard.setKeyWidth ((float) keyboard.getWidth() / 64.0f);   // 64 teclas brancas de C0 a C9: ocupa a largura toda
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
