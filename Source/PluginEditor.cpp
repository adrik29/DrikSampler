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
        g.drawFittedText (utf8 ("Arraste para c\xc3\xa1 um som, v\xc3\xa1rios sons, uma pasta ou um instrumento .sfz"),
                          getLocalBounds(), juce::Justification::centred, 1);
    }

    g.setColour (highlight ? accentColour : panelColour.brighter (0.25f));
    g.drawRoundedRectangle (bounds.reduced (0.75f), 6.0f, highlight ? 2.0f : 1.0f);
}

//==============================================================================
void MapView::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    g.setColour (panelColour);
    g.fillRoundedRectangle (bounds, 6.0f);

    // Faixas unicas de teclas (camadas de forca e rodizio caem na mesma faixa).
    std::vector<InstrumentInfo::Zone> ranges;
    int lowest = 127, highest = 0;
    for (const auto& z : info.zones)
    {
        lowest = juce::jmin (lowest, z.lo);
        highest = juce::jmax (highest, z.hi);
        bool known = false;
        for (const auto& r : ranges) known = known || (r.lo == z.lo && r.hi == z.hi);
        if (! known) ranges.push_back (z);
    }
    std::sort (ranges.begin(), ranges.end(), [] (const auto& a, const auto& b) { return a.lo < b.lo; });

    auto name = [] (int n) { return juce::MidiMessage::getMidiNoteName (n, true, true, 4); };

    g.setColour (textColour);
    g.setFont (juce::FontOptions (20.0f, juce::Font::bold));
    g.drawText (info.name, getLocalBounds().reduced (16, 12).removeFromTop (26), juce::Justification::centredLeft);

    juce::String summary;
    if (! info.zones.empty())
        summary << (int) info.zones.size() << " samples   |   teclas " << name (lowest) << " a " << name (highest);
    if (info.zones.size() != ranges.size())
        summary << "   |   " << (int) ranges.size() << " faixas";
    g.setColour (dimColour);
    g.setFont (14.0f);
    g.drawText (summary, getLocalBounds().reduced (16, 12).withTrimmedTop (30).removeFromTop (20), juce::Justification::centredLeft);

    if (info.message.isNotEmpty())
    {
        g.setColour (accentColour);
        g.drawText (info.message, getLocalBounds().reduced (16, 12).withTrimmedTop (52).removeFromTop (20), juce::Justification::centredLeft);
    }

    if (keyRectangle != nullptr && ! ranges.empty())
    {
        const float top = bounds.getBottom() - 74.0f, height = 46.0f;
        const float offsetX = 0.0f;

        g.setColour (dimColour);
        g.setFont (12.5f);
        g.drawText (utf8 ("cada bloco \xc3\xa9 um sample; o tra\xc3\xa7o claro marca a nota em que ele foi gravado"),
                    juce::Rectangle<float> (16.0f, top - 22.0f, bounds.getWidth() - 32.0f, 18.0f), juce::Justification::centredLeft);

        for (size_t i = 0; i < ranges.size(); ++i)
        {
            const auto a = keyRectangle (ranges[i].lo), b = keyRectangle (ranges[i].hi);
            const float x1 = juce::jmin (a.getX(), b.getX()) + offsetX;
            const float x2 = juce::jmax (a.getRight(), b.getRight()) + offsetX;
            if (x2 <= x1) continue;

            g.setColour (i % 2 == 0 ? accentColour : accentColour.darker (0.45f));
            g.fillRoundedRectangle (juce::Rectangle<float> (x1 + 1.0f, top, juce::jmax (2.0f, x2 - x1 - 2.0f), height), 2.0f);

            const auto r = keyRectangle (ranges[i].root);
            if (ranges[i].root >= ranges[i].lo && ranges[i].root <= ranges[i].hi && x2 - x1 > 14.0f)
            {
                g.setColour (textColour);
                g.fillRect (juce::Rectangle<float> (r.getCentreX() - 1.0f, top + height - 12.0f, 2.0f, 12.0f));
            }
        }

        g.setColour (dimColour);
        g.setFont (12.0f);
        g.drawText (utf8 ("fora dos blocos n\xc3\xa3" "o h\xc3\xa1 som"), juce::Rectangle<float> (16.0f, top + height + 4.0f, 300.0f, 16.0f),
                    juce::Justification::centredLeft);
    }

    g.setColour (highlight ? accentColour : panelColour.brighter (0.25f));
    g.drawRoundedRectangle (bounds.reduced (0.75f), 6.0f, highlight ? 2.0f : 1.0f);
}

//==============================================================================
Knob::Knob (juce::AudioProcessorValueTreeState& state, const juce::String& paramID, const juce::String& titleText)
    : id (paramID), attachment (state, paramID, slider)
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
      keyboard (p.keyboardState)
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

    // Lista de instrumentos: os favoritos salvos e tudo o que existe na pasta dos samples.
    instrumentBox.setColour (juce::ComboBox::backgroundColourId, panelColour);
    instrumentBox.setColour (juce::ComboBox::textColourId, textColour);
    instrumentBox.setColour (juce::ComboBox::outlineColourId, accentColour.withAlpha (0.6f));
    instrumentBox.setColour (juce::ComboBox::arrowColourId, accentColour);
    instrumentBox.setTextWhenNothingSelected (utf8 ("nenhum som carregado"));
    instrumentBox.setTooltip (utf8 ("Meus instrumentos: os que voc\xc3\xaa salvou, com a regulagem dos bot\xc3\xb5" "es. "
                                    "Depois, tudo o que existe na pasta dos samples."));
    instrumentBox.onChange = [this] { if (! refreshing) chooseFromList (instrumentBox.getSelectedId()); };
    addAndMakeVisible (instrumentBox);

    openButton.setButtonText ("Abrir...");
    openButton.onClick = [this] { openFile(); };
    saveButton.onClick = [this] { askNameAndSave(); };
    saveButton.setTooltip (utf8 ("Guarda o instrumento que est\xc3\xa1 carregado junto com a posi\xc3\xa7\xc3\xa3" "o de todos os bot\xc3\xb5" "es, com o nome que voc\xc3\xaa der."));
    folderButton.onClick = [this] { showFolderMenu(); };
    folderButton.setTooltip (utf8 ("Escolhe a pasta onde ficam os samples e a pasta onde ficam os instrumentos salvos."));
    prevButton.onClick = [this] { stepList (-1); };
    nextButton.onClick = [this] { stepList (1); };

    for (auto* b : { &openButton, &saveButton, &folderButton, &prevButton, &nextButton })
    {
        b->setColour (juce::TextButton::buttonColourId, panelColour);
        b->setColour (juce::TextButton::textColourOffId, textColour);
        addAndMakeVisible (*b);
    }

    loopButton.setButtonText ("Loop");
    loopButton.setColour (juce::ToggleButton::textColourId, textColour);
    loopButton.setColour (juce::ToggleButton::tickColourId, accentColour);
    loopButton.setColour (juce::ToggleButton::tickDisabledColourId, dimColour);
    loopAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (processor.apvts, "loop", loopButton);
    addAndMakeVisible (loopButton);

    addAndMakeVisible (waveform);
    addChildComponent (mapView);
    mapView.keyRectangle = [this] (int note) { return keyboard.getRectangleForKey (note); };

    // Sons dentro de um SFZ que traz varios (escolhidos por keyswitch no arquivo).
    programBox.setColour (juce::ComboBox::backgroundColourId, panelColour);
    programBox.setColour (juce::ComboBox::textColourId, textColour);
    programBox.setColour (juce::ComboBox::outlineColourId, accentColour.withAlpha (0.6f));
    programBox.setColour (juce::ComboBox::arrowColourId, accentColour);
    programBox.onChange = [this]
    {
        if (refreshing) return;
        const auto i = processor.getInfo();
        const int chosen = programBox.getSelectedId() - 1;
        if (i.mode == InstrumentInfo::sfz && chosen >= 0 && chosen != i.program)
            processor.loadSfz (i.file, chosen, false);
    };
    addChildComponent (programBox);

    octaveLabel.setFont (juce::FontOptions (13.0f));
    octaveLabel.setColour (juce::Label::textColourId, dimColour);
    addAndMakeVisible (octaveLabel);
    keyboard.onOctaveChange = [this] { showOctave(); };
    showOctave();

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
    rebuildList();

    setSize (980, 580);
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
    versionLabel.setBounds (top.removeFromLeft (150).withTrimmedTop (5));
    openButton.setBounds (top.removeFromRight (80).reduced (0, 3));
    top.removeFromRight (6);
    folderButton.setBounds (top.removeFromRight (80).reduced (0, 3));
    top.removeFromRight (6);
    saveButton.setBounds (top.removeFromRight (80).reduced (0, 3));
    top.removeFromRight (12);
    nextButton.setBounds (top.removeFromRight (30).reduced (0, 3));
    top.removeFromRight (4);
    prevButton.setBounds (top.removeFromLeft (30).reduced (0, 3));
    top.removeFromLeft (4);
    instrumentBox.setBounds (top.reduced (0, 3));

    area.removeFromTop (10);
    octaveLabel.setBounds (area.removeFromBottom (18));
    keyboard.setBounds (area.removeFromBottom (84));
    keyboard.setKeyWidth ((float) keyboard.getWidth() / 64.0f);   // 64 teclas brancas de C0 a C9: ocupa a largura toda
    area.removeFromBottom (12);

    auto knobRow = area.removeFromBottom (108);
    area.removeFromBottom (12);
    waveform.setBounds (area);
    mapView.setBounds (area);
    programBox.setBounds (area.getRight() - 296, area.getY() + 12, 280, 26);
    loopButton.setBounds (area.getRight() - 84, area.getY() + 8, 74, 24);
    loopButton.toFront (false);

    const int w = knobRow.getWidth() / juce::jmax (1, knobs.size());
    for (auto* k : knobs)
        k->setBounds (knobRow.removeFromLeft (w));
}

//==============================================================================
void DrikSamplerEditor::showOctave()
{
    octaveLabel.setText (utf8 ("Teclado do computador: clique no teclado acima; A S D F G H J K tocam, com A = D\xc3\xb3 ") + "C"
                         + juce::String (keyboard.getOctave() - 1) + utf8 ("; Z e X mudam a oitava"), juce::dontSendNotification);
}

void DrikSamplerEditor::refreshFromProcessor()
{
    const auto i = processor.getInfo();
    const bool multi = i.mode == InstrumentInfo::sfz || i.mode == InstrumentInfo::files;

    waveform.setVisible (! multi);
    mapView.setVisible (multi);
    loopButton.setVisible (! multi);

    if (multi)
        mapView.setInfo (i);
    else
        waveform.setFile (i.file);

    // Num multisample, nota original e loop vem de cada sample: esses botoes deixam de valer.
    for (auto* k : knobs)
        if (k->id == "root" || k->id == "xfade")
        {
            k->setEnabled (! multi);
            k->setAlpha (multi ? 0.35f : 1.0f);
        }

    refreshing = true;
    programBox.clear (juce::dontSendNotification);
    for (int p = 0; p < i.programs.size(); ++p)
        programBox.addItem (i.programs[p], p + 1);
    programBox.setSelectedId (i.program + 1, juce::dontSendNotification);
    programBox.setVisible (multi && i.programs.size() > 1);
    refreshing = false;

    // A lista mostra o nome do instrumento em uso; se for um dos itens, fica selecionado.
    refreshing = true;
    juce::String text = i.mode == InstrumentInfo::empty ? juce::String() : i.name;
    if (! multi && i.message.isNotEmpty())
        text = i.message;

    int match = -1;
    for (int k = 0; k < instrumentBox.getNumItems(); ++k)
        if (instrumentBox.getItemText (k) == text)
        {
            match = k;
            break;
        }

    const bool showingMessage = ! multi && i.message.isNotEmpty();

    if (match >= 0)
        instrumentBox.setSelectedItemIndex (match, juce::dontSendNotification);
    else if (chosenIndex >= 0 && chosenIndex < instrumentBox.getNumItems() && ! showingMessage)
        instrumentBox.setSelectedItemIndex (chosenIndex, juce::dontSendNotification);
    else
    {
        instrumentBox.setSelectedId (0, juce::dontSendNotification);
        instrumentBox.setText (text, juce::dontSendNotification);
    }
    refreshing = false;
}

//==============================================================================
// Itens da lista: 1..N sao os favoritos; de 10001 em diante, as entradas da biblioteca.
static constexpr int libraryIdBase = 10001;

void DrikSamplerEditor::rebuildList()
{
    refreshing = true;
    instrumentBox.clear (juce::dontSendNotification);

    favorites = DrikSamplerProcessor::favoriteFiles();
    if (! favorites.isEmpty())
    {
        instrumentBox.addSectionHeading ("Meus instrumentos");
        for (int i = 0; i < favorites.size(); ++i)
            instrumentBox.addItem (favorites[i].getFileNameWithoutExtension(), i + 1);
    }

    library = processor.scanLibrary();
    juce::String group;
    for (int i = 0; i < (int) library.size(); ++i)
    {
        if (group != library[(size_t) i].group)
        {
            group = library[(size_t) i].group;
            instrumentBox.addSectionHeading (group);
        }
        instrumentBox.addItem (library[(size_t) i].label, libraryIdBase + i);
    }

    refreshing = false;
    refreshFromProcessor();
}

void DrikSamplerEditor::chooseFromList (int itemId)
{
    chosenIndex = instrumentBox.getSelectedItemIndex();

    if (itemId >= libraryIdBase)
    {
        const int i = itemId - libraryIdBase;
        if (i < (int) library.size())
            tryLoad (library[(size_t) i].files);
    }
    else if (itemId >= 1 && itemId <= favorites.size())
    {
        setMouseCursor (juce::MouseCursor::WaitCursor);
        processor.loadFavorite (favorites[itemId - 1]);
        setMouseCursor (juce::MouseCursor::NormalCursor);
    }
}

void DrikSamplerEditor::stepList (int delta)
{
    const int count = instrumentBox.getNumItems();
    if (count == 0)
        return;

    const int current = instrumentBox.getSelectedItemIndex() >= 0 ? instrumentBox.getSelectedItemIndex() : chosenIndex;
    const int next = current < 0 ? (delta > 0 ? 0 : count - 1) : (current + delta + count) % count;
    instrumentBox.setSelectedItemIndex (next, juce::sendNotificationSync);
}

void DrikSamplerEditor::askNameAndSave()
{
    const auto info = processor.getInfo();
    if (info.mode == InstrumentInfo::empty)
    {
        instrumentBox.setText (utf8 ("carregue um som antes de salvar"), juce::dontSendNotification);
        return;
    }

    auto* w = new juce::AlertWindow ("Salvar instrumento", utf8 ("Nome do instrumento (guarda os samples e a regulagem dos bot\xc3\xb5" "es):"),
                                     juce::MessageBoxIconType::NoIcon, this);
    w->addTextEditor ("name", info.name.upToLastOccurrenceOf (".", false, false).isNotEmpty() ? info.name.upToLastOccurrenceOf (".", false, false) : info.name);
    w->addButton ("Salvar", 1, juce::KeyPress (juce::KeyPress::returnKey));
    w->addButton ("Cancelar", 0, juce::KeyPress (juce::KeyPress::escapeKey));

    juce::Component::SafePointer<DrikSamplerEditor> safe (this);
    w->enterModalState (true, juce::ModalCallbackFunction::create ([safe, w] (int result)
    {
        if (safe == nullptr || result != 1)
            return;

        const auto name = juce::File::createLegalFileName (w->getTextEditorContents ("name").trim());
        if (name.isEmpty())
            return;

        const auto file = DrikSamplerProcessor::getFavoritesFolder().getChildFile (name + ".drikinst");
        if (safe->processor.saveFavorite (file))
        {
            safe->chosenIndex = -1;
            safe->rebuildList();
        }
        else
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Salvar instrumento",
                                                    utf8 ("N\xc3\xa3" "o consegui gravar em:\n") + file.getFullPathName());
    }), true);
}

void DrikSamplerEditor::showFolderMenu()
{
    const auto root = DrikSamplerProcessor::getSamplesRoot();
    juce::PopupMenu menu;
    menu.addSectionHeader ("Pasta dos samples");
    menu.addItem (1, root != juce::File() ? root.getFullPathName() + "   (trocar...)" : utf8 ("ainda n\xc3\xa3" "o escolhida   (escolher...)"));
    menu.addSectionHeader ("Pasta dos instrumentos salvos");
    menu.addItem (2, DrikSamplerProcessor::getFavoritesFolder().getFullPathName() + "   (trocar...)");
    menu.addSeparator();
    menu.addItem (3, "Atualizar a lista");

    juce::Component::SafePointer<DrikSamplerEditor> safe (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (folderButton), [safe] (int choice)
    {
        if (safe == nullptr) return;
        if (choice == 1) safe->chooseFolder (true);
        if (choice == 2) safe->chooseFolder (false);
        if (choice == 3) safe->rebuildList();
    });
}

void DrikSamplerEditor::chooseFolder (bool forSamples)
{
    auto start = forSamples ? DrikSamplerProcessor::getSamplesRoot() : DrikSamplerProcessor::getFavoritesFolder();
    if (! start.isDirectory())
        start = juce::File::getSpecialLocation (juce::File::userHomeDirectory);

    chooser = std::make_unique<juce::FileChooser> (forSamples ? utf8 ("Escolha a pasta onde ficam todos os samples")
                                                              : utf8 ("Escolha a pasta para os instrumentos salvos"), start);

    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                          [this, forSamples] (const juce::FileChooser& fc)
                          {
                              const auto folder = fc.getResult();
                              if (! folder.isDirectory())
                                  return;

                              if (forSamples) DrikSamplerProcessor::setSamplesRoot (folder);
                              else            DrikSamplerProcessor::setFavoritesFolder (folder);
                              rebuildList();
                          });
}

void DrikSamplerEditor::tryLoad (const juce::Array<juce::File>& files)
{
    setMouseCursor (juce::MouseCursor::WaitCursor);
    const bool ok = processor.loadAnything (files);
    setMouseCursor (juce::MouseCursor::NormalCursor);

    if (! ok && processor.getInfo().message.isEmpty() && ! files.isEmpty())
        instrumentBox.setText (utf8 ("N\xc3\xa3" "o consegui abrir: ") + files.getFirst().getFileName(), juce::dontSendNotification);
}

void DrikSamplerEditor::filesDropped (const juce::StringArray& files, int, int)
{
    waveform.setDragHighlight (false);
    mapView.setDragHighlight (false);

    juce::Array<juce::File> list;
    for (const auto& f : files)
        list.add (juce::File (f));
    chosenIndex = -1;
    tryLoad (list);
}

void DrikSamplerEditor::openFile()
{
    auto start = processor.getCurrentFile();
    if (! start.exists())
        start = juce::File::getSpecialLocation (juce::File::userMusicDirectory);

    chooser = std::make_unique<juce::FileChooser> (utf8 ("Escolha um som, v\xc3\xa1rios sons ou um instrumento SFZ"), start,
                                                   "*.sfz;" + processor.formatManager.getWildcardForAllFormats());

    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles
                              | juce::FileBrowserComponent::canSelectMultipleItems,
                          [this] (const juce::FileChooser& fc)
                          {
                              const auto chosen = fc.getResults();
                              if (! chosen.isEmpty())
                              {
                                  chosenIndex = -1;
                                  tryLoad (chosen);
                              }
                          });
}
