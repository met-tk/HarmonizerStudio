#include "EventEditors.h"
#include "CubaseTheme.h"
#include "../core/ScaleDefinitions.h"
#include "../core/ChordClassifier.h"
#include "../core/Localization.h"
#include "../core/AppStateManager.h"
#include "../core/SectionLayout.h"
#include <algorithm>

namespace Harmonizer
{

static juce::String localizedUiText(const char* english, const char* chinese)
{
    return Localization::getInstance().getLanguage() == Language::English
        ? juce::String::fromUTF8(english)
        : juce::String::fromUTF8(chinese);
}

static juce::String localizedUiText(const char* english, const char8_t* chinese)
{
    return localizedUiText(english, reinterpret_cast<const char*>(chinese));
}

//==============================================================================
// 1. ChordEditorDialog
//==============================================================================
ChordEditorDialog::ChordEditorDialog(const ChordEvent& initialChord, std::function<void(const juce::String&, const juce::String&)> onConfirm)
    : confirmCallback(std::move(onConfirm))
{
    lblPreview.setFont(juce::FontOptions(22.0f).withStyle("bold"));
    lblPreview.setColour(juce::Label::textColourId, CubaseTheme::accentOrange());
    lblPreview.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(lblPreview);

    static const char* roots[] = { "C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B" };
    for (int i = 0; i < 12; ++i)
        rootCombo.addItem(roots[i], i + 1);

    const auto& qualities = ChordClassifier::getSupportedQualities();
    for (size_t i = 0; i < qualities.size(); ++i)
        qualityCombo.addItem(qualities[i].displayName, (int)i + 1);

    static const char* basses[] = {
        "Root (No Inversion)", "/3rd", "/5th", "/7th", "/9th",
        "/C", "/C#", "/D", "/Eb", "/E", "/F", "/F#", "/G", "/Ab", "/A", "/Bb", "/B"
    };
    for (int i = 0; i < 17; ++i)
        bassInversionCombo.addItem(basses[i], i + 1);

    rootCombo.setSelectedId(1);
    for (int i = 0; i < 12; ++i)
    {
        if (initialChord.chordName.startsWith(roots[i]))
        {
            rootCombo.setSelectedId(i + 1);
            break;
        }
    }

    qualityCombo.setSelectedId(1);
    for (size_t i = 0; i < qualities.size(); ++i)
    {
        if (initialChord.chordName.contains(qualities[i].id))
        {
            qualityCombo.setSelectedId((int)i + 1);
            break;
        }
    }

    bassInversionCombo.setSelectedId(1);
    if (initialChord.chordName.contains("/"))
    {
        juce::String slashPart = "/" + initialChord.chordName.fromLastOccurrenceOf("/", false, false);
        for (int i = 0; i < 17; ++i)
        {
            if (juce::String(basses[i]) == slashPart)
            {
                bassInversionCombo.setSelectedId(i + 1);
                break;
            }
        }
    }

    // Parse existing tension notes in name
    if (initialChord.chordName.contains("b9")) btnTension_b9.setToggleState(true, juce::dontSendNotification);
    if (initialChord.chordName.contains("#9")) btnTension_sharp9.setToggleState(true, juce::dontSendNotification);
    if (initialChord.chordName.contains("9") && !initialChord.chordName.contains("b9") && !initialChord.chordName.contains("#9"))
        btnTension_9.setToggleState(true, juce::dontSendNotification);
    if (initialChord.chordName.contains("#11")) btnTension_sharp11.setToggleState(true, juce::dontSendNotification);
    if (initialChord.chordName.contains("11") && !initialChord.chordName.contains("#11"))
        btnTension_11.setToggleState(true, juce::dontSendNotification);
    if (initialChord.chordName.contains("b13")) btnTension_b13.setToggleState(true, juce::dontSendNotification);
    if (initialChord.chordName.contains("13") && !initialChord.chordName.contains("b13"))
        btnTension_13.setToggleState(true, juce::dontSendNotification);
    if (initialChord.chordName.contains("alt")) btnTension_alt.setToggleState(true, juce::dontSendNotification);

    romanEditor.setText(initialChord.romanNumeral.isNotEmpty() ? initialChord.romanNumeral : "I");

    addAndMakeVisible(rootCombo);
    addAndMakeVisible(qualityCombo);
    addAndMakeVisible(bassInversionCombo);
    addAndMakeVisible(romanEditor);

    auto onTensionClick = [this]() { updatePreview(); };
    auto setupTensionBtn = [this, onTensionClick](juce::ToggleButton& btn) {
        btn.onClick = onTensionClick;
        addAndMakeVisible(btn);
    };
    setupTensionBtn(btnTension_9);
    setupTensionBtn(btnTension_b9);
    setupTensionBtn(btnTension_sharp9);
    setupTensionBtn(btnTension_11);
    setupTensionBtn(btnTension_sharp11);
    setupTensionBtn(btnTension_13);
    setupTensionBtn(btnTension_b13);
    setupTensionBtn(btnTension_alt);

    auto onChange = [this]() { updatePreview(); };
    rootCombo.onChange = onChange;
    qualityCombo.onChange = onChange;
    bassInversionCombo.onChange = onChange;

    addAndMakeVisible(btnOk);
    addAndMakeVisible(btnCancel);

    btnOk.setColour(juce::TextButton::buttonColourId, CubaseTheme::accentOrange());
    btnOk.setColour(juce::TextButton::textColourOffId, juce::Colours::black);

    btnOk.onClick = [this]() {
        if (confirmCallback)
            confirmCallback(lblPreview.getText(), romanEditor.getText().trim());
        if (auto* w = findParentComponentOfClass<juce::DialogWindow>())
            w->exitModalState(1);
    };

    btnCancel.onClick = [this]() {
        if (auto* w = findParentComponentOfClass<juce::DialogWindow>())
            w->exitModalState(0);
    };

    updatePreview();
    setSize(440, 320);
}

void ChordEditorDialog::updatePreview()
{
    juce::String root = rootCombo.getText();
    const auto& qualities = ChordClassifier::getSupportedQualities();
    int qIdx = qualityCombo.getSelectedId() - 1;
    juce::String qId = (qIdx >= 0 && qIdx < (int)qualities.size()) ? qualities[qIdx].id : "Maj";

    std::vector<juce::String> activeTensions;
    if (btnTension_b9.getToggleState()) activeTensions.push_back("b9");
    if (btnTension_9.getToggleState()) activeTensions.push_back("9");
    if (btnTension_sharp9.getToggleState()) activeTensions.push_back("#9");
    if (btnTension_11.getToggleState()) activeTensions.push_back("11");
    if (btnTension_sharp11.getToggleState()) activeTensions.push_back("#11");
    if (btnTension_b13.getToggleState()) activeTensions.push_back("b13");
    if (btnTension_13.getToggleState()) activeTensions.push_back("13");
    if (btnTension_alt.getToggleState()) activeTensions.push_back("alt");

    juce::String bass = "";
    if (bassInversionCombo.getSelectedId() > 1)
    {
        juce::String bText = bassInversionCombo.getText();
        if (bText.startsWith("/"))
            bass = bText.substring(1);
        else
            bass = bText;
    }

    juce::String fullChord = ChordClassifier::buildChordName(root, qId, activeTensions, bass);
    lblPreview.setText(fullChord, juce::dontSendNotification);

    int rootPc = rootCombo.getSelectedId() - 1;
    int bassPc = rootPc;
    if (bass.isNotEmpty())
    {
        static const char* noteNames[] = { "C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B" };
        for (int i = 0; i < 12; ++i)
            if (bass == noteNames[i]) { bassPc = i; break; }
    }
    juce::String roman = ChordClassifier::computeRomanNumeral(rootPc, bassPc, qId, 0, "Ionian (Natural Major)");
    romanEditor.setText(roman, juce::dontSendNotification);
}

void ChordEditorDialog::paint(juce::Graphics& g)
{
    g.fillAll(CubaseTheme::bgPanel());

    g.setColour(CubaseTheme::textSecondary());
    g.setFont(CubaseTheme::getFont(12.0f));
    g.drawText(juce::String::fromUTF8(u8"Root (根音):"), 20, 68, 90, 24, juce::Justification::centredLeft);
    g.drawText(juce::String::fromUTF8(u8"Quality (性质):"), 220, 68, 90, 24, juce::Justification::centredLeft);

    g.drawText(juce::String::fromUTF8(u8"Tensions (多选附加音):"), 20, 108, 140, 24, juce::Justification::centredLeft);

    g.drawText(juce::String::fromUTF8(u8"Bass / Inversion (转位低音):"), 20, 188, 160, 24, juce::Justification::centredLeft);
    g.drawText(juce::String::fromUTF8(u8"Roman Degree (级数):"), 20, 228, 140, 24, juce::Justification::centredLeft);
}

void ChordEditorDialog::resized()
{
    lblPreview.setBounds(20, 12, getWidth() - 40, 42);

    rootCombo.setBounds(115, 68, 95, 26);
    qualityCombo.setBounds(315, 68, 105, 26);

    // Tension Buttons 2x4 grid
    int tx = 20, ty = 134, tw = 48, th = 22;
    btnTension_b9.setBounds(tx, ty, tw, th);
    btnTension_9.setBounds(tx + 52, ty, tw, th);
    btnTension_sharp9.setBounds(tx + 104, ty, tw, th);
    btnTension_11.setBounds(tx + 156, ty, tw, th);

    btnTension_sharp11.setBounds(tx + 212, ty, tw + 4, th);
    btnTension_b13.setBounds(tx + 270, ty, tw + 4, th);
    btnTension_13.setBounds(tx + 328, ty, tw, th);
    btnTension_alt.setBounds(tx + 380, ty, tw, th);

    bassInversionCombo.setBounds(185, 188, 160, 26);
    romanEditor.setBounds(185, 228, 90, 26);

    btnCancel.setBounds(getWidth() - 170, getHeight() - 44, 70, 28);
    btnOk.setBounds(getWidth() - 90, getHeight() - 44, 70, 28);
}

void ChordEditorDialog::showModal(const ChordEvent& chord, std::function<void(const juce::String&, const juce::String&)> onConfirm)
{
    auto dialog = std::make_unique<ChordEditorDialog>(chord, onConfirm);
    juce::DialogWindow::LaunchOptions opt;
    opt.dialogTitle = "Edit Chord";
    opt.dialogBackgroundColour = CubaseTheme::bgPanel();
    opt.content.setOwned(dialog.release());
    opt.useNativeTitleBar = true;
    opt.resizable = false;
    opt.launchAsync();
}

//==============================================================================
// 2. ModeEditorDialog
//==============================================================================
namespace
{
struct ModeTonicOption { const char* name; int pitchClass; };
constexpr ModeTonicOption modeTonicOptions[] {
    { "C", 0 }, { "C#", 1 }, { "Db", 1 }, { "D", 2 },
    { "D#", 3 }, { "Eb", 3 }, { "E", 4 }, { "F", 5 },
    { "F#", 6 }, { "Gb", 6 }, { "G", 7 }, { "G#", 8 },
    { "Ab", 8 }, { "A", 9 }, { "A#", 10 }, { "Bb", 10 },
    { "B", 11 }
};
constexpr int modeTonicOptionCount = static_cast<int>(sizeof(modeTonicOptions)
                                                 / sizeof(modeTonicOptions[0]));
} // namespace

ModeEditorDialog::ModeEditorDialog(const ModeEvent& initialMode,
                                   RealtimeChangeFn onRealtimeChange,
                                   CancelFn onCancel,
                                   FinishFn onFinish)
    : realtimeCallback(std::move(onRealtimeChange)),
      cancelCallback(std::move(onCancel)),
      finishCallback(std::move(onFinish))
{
    nameEditor.setText(initialMode.name);
    addAndMakeVisible(nameEditor);

    for (int i = 0; i < modeTonicOptionCount; ++i)
        tonicCombo.addItem(modeTonicOptions[i].name, i + 1);
    const auto layout = ScaleDefinitions::computeLayout(initialMode.tonicPc,
        initialMode.scaleName, initialMode.tonic);
    const auto savedName = initialMode.tonic.trim();
    int tonicOptionId = 0;
    for (int i = 0; i < modeTonicOptionCount; ++i)
        if (layout.tonicPc == modeTonicOptions[i].pitchClass
            && layout.tonicName.equalsIgnoreCase(modeTonicOptions[i].name))
        {
            tonicOptionId = i + 1;
            break;
        }
    if (tonicOptionId == 0 && savedName.isNotEmpty()
        && layout.tonicName == savedName)
    {
        // Keep an existing valid spelling (for example B#) available while
        // editing, even when it is outside the common tonic menu.
        tonicOptionId = 100 + layout.tonicPc;
        tonicCombo.addItem(savedName, tonicOptionId);
    }
    if (tonicOptionId == 0)
    {
        const auto preferred = ScaleDefinitions::getPreferredTonicName(
            layout.tonicPc, initialMode.scaleName);
        for (int i = 0; i < modeTonicOptionCount; ++i)
            if (preferred == modeTonicOptions[i].name)
            {
                tonicOptionId = i + 1;
                break;
            }
    }
    tonicCombo.setSelectedId(tonicOptionId > 0 ? tonicOptionId : 1,
                             juce::dontSendNotification);
    addAndMakeVisible(tonicCombo);

    auto allScales = ScaleDefinitions::getAllScales();
    for (size_t i = 0; i < allScales.size(); ++i)
        scaleCombo.addItem(allScales[i].name, (int)i + 1);

    scaleCombo.setSelectedId(1, juce::dontSendNotification);
    for (size_t i = 0; i < allScales.size(); ++i)
    {
        if (allScales[i].name == initialMode.scaleName)
        {
            scaleCombo.setSelectedId((int)i + 1, juce::dontSendNotification);
            break;
        }
    }
    addAndMakeVisible(scaleCombo);

    colorCombo.addItem("Emerald Green", 1);
    colorCombo.addItem("Cyan Blue", 2);
    colorCombo.addItem("Amber Gold", 3);
    colorCombo.addItem("Purple", 4);
    colorCombo.addItem("Rose Pink", 5);

    int chosenColorId = 1;
    if (initialMode.color == juce::Colour(0xff06b6d4)) chosenColorId = 2;
    else if (initialMode.color == juce::Colour(0xfff59e0b)) chosenColorId = 3;
    else if (initialMode.color == juce::Colour(0xff8b5cf6)) chosenColorId = 4;
    else if (initialMode.color == juce::Colour(0xfff43f5e)) chosenColorId = 5;
    colorCombo.setSelectedId(chosenColorId, juce::dontSendNotification);
    addAndMakeVisible(colorCombo);

    addAndMakeVisible(btnCancel);
    btnCancel.setColour(juce::TextButton::buttonColourId, CubaseTheme::buttonBackground());
    btnCancel.setColour(juce::TextButton::textColourOffId, CubaseTheme::textPrimary());

    addAndMakeVisible(btnOk);
    btnOk.setColour(juce::TextButton::buttonColourId, CubaseTheme::accentCyan());
    btnOk.setColour(juce::TextButton::textColourOffId, juce::Colours::black);

    nameEditor.onTextChange = [this]() { applyRealtime(); };
    tonicCombo.onChange = [this]() { applyRealtime(); };
    scaleCombo.onChange = [this]() { applyRealtime(); };
    colorCombo.onChange = [this]() { applyRealtime(); };

    btnCancel.onClick = [this]() {
        isCancelled = true;
        if (cancelCallback)
            cancelCallback();
        if (auto* w = findParentComponentOfClass<juce::DialogWindow>())
            w->exitModalState(0);
    };

    btnOk.onClick = [this]() {
        applyRealtime();
        if (auto* w = findParentComponentOfClass<juce::DialogWindow>())
            w->exitModalState(1);
    };

    setSize(400, 240);
    setWantsKeyboardFocus(true);
}

ModeEditorDialog::~ModeEditorDialog()
{
    if (!isCancelled && finishCallback)
    {
        finishCallback();
    }
}

void ModeEditorDialog::applyRealtime()
{
    if (realtimeCallback)
    {
        juce::Colour c = juce::Colour(0xff10b981);
        if (colorCombo.getSelectedId() == 2) c = juce::Colour(0xff06b6d4);
        else if (colorCombo.getSelectedId() == 3) c = juce::Colour(0xfff59e0b);
        else if (colorCombo.getSelectedId() == 4) c = juce::Colour(0xff8b5cf6);
        else if (colorCombo.getSelectedId() == 5) c = juce::Colour(0xfff43f5e);

        const int selectedId = tonicCombo.getSelectedId();
        const int tonicPc = selectedId >= 1 && selectedId <= modeTonicOptionCount
            ? modeTonicOptions[selectedId - 1].pitchClass
            : selectedId >= 100 && selectedId < 112 ? selectedId - 100 : -1;
        if (tonicPc < 0) return;
        realtimeCallback(nameEditor.getText().trim(),
                         tonicCombo.getText(),
                         tonicPc,
                         scaleCombo.getText(),
                         c);
    }
}

bool ModeEditorDialog::keyPressed(const juce::KeyPress& key)
{
    if (key == juce::KeyPress::escapeKey)
    {
        btnCancel.triggerClick();
        return true;
    }
    if (key == juce::KeyPress::returnKey)
    {
        btnOk.triggerClick();
        return true;
    }
    return false;
}

void ModeEditorDialog::paint(juce::Graphics& g)
{
    g.fillAll(CubaseTheme::bgPanel());
    g.setColour(CubaseTheme::textSecondary());
    g.setFont(12.0f);
    g.drawText("Section Name:", 20, 20, 100, 26, juce::Justification::centredLeft);
    g.drawText("Tonic (Key):", 20, 60, 100, 26, juce::Justification::centredLeft);
    g.drawText("Scale / Mode:", 20, 100, 100, 26, juce::Justification::centredLeft);
    g.drawText("Track Color:", 20, 140, 100, 26, juce::Justification::centredLeft);
}

void ModeEditorDialog::resized()
{
    nameEditor.setBounds(130, 20, getWidth() - 150, 26);
    tonicCombo.setBounds(130, 60, 120, 26);
    scaleCombo.setBounds(130, 100, getWidth() - 150, 26);
    colorCombo.setBounds(130, 140, 140, 26);

    // Cancel 按键移到左下角最左侧
    btnCancel.setBounds(20, getHeight() - 42, 80, 28);

    // OK 按键放在右下角
    btnOk.setBounds(getWidth() - 90, getHeight() - 42, 70, 28);
}

void ModeEditorDialog::showModal(const ModeEvent& mode,
                                 RealtimeChangeFn onRealtimeChange,
                                 CancelFn onCancel,
                                 FinishFn onFinish)
{
    auto dialog = std::make_unique<ModeEditorDialog>(mode, onRealtimeChange, onCancel, onFinish);
    juce::DialogWindow::LaunchOptions opt;
    opt.dialogTitle = "Edit Tonality / Mode";
    opt.dialogBackgroundColour = CubaseTheme::bgPanel();
    opt.content.setOwned(dialog.release());
    opt.useNativeTitleBar = true;
    opt.resizable = false;
    opt.launchAsync();
}

//==============================================================================
// 3. MarkerEditorDialog
//==============================================================================
MarkerEditorDialog::MarkerEditorDialog(const MarkerEvent& initialMarker, std::function<void(const juce::String&, juce::Colour)> onConfirm)
    : confirmCallback(std::move(onConfirm))
{
    nameEditor.setText(initialMarker.name);
    addAndMakeVisible(nameEditor);

    colorCombo.addItem("Cyan Blue", 1);
    colorCombo.addItem("Magenta Rose", 2);
    colorCombo.addItem("Amber Orange", 3);
    colorCombo.addItem("Emerald Green", 4);
    colorCombo.addItem("Purple", 5);
    colorCombo.setSelectedId(1);
    addAndMakeVisible(colorCombo);

    addAndMakeVisible(btnOk);
    addAndMakeVisible(btnCancel);

    btnOk.setColour(juce::TextButton::buttonColourId, CubaseTheme::accentBlue());
    btnOk.setColour(juce::TextButton::textColourOffId, juce::Colours::black);

    btnOk.onClick = [this]() {
        if (confirmCallback)
        {
            juce::Colour c = juce::Colour(0xff0284c7);
            if (colorCombo.getSelectedId() == 2) c = juce::Colour(0xffec4899);
            else if (colorCombo.getSelectedId() == 3) c = juce::Colour(0xfff59e0b);
            else if (colorCombo.getSelectedId() == 4) c = juce::Colour(0xff10b981);
            else if (colorCombo.getSelectedId() == 5) c = juce::Colour(0xff8b5cf6);

            confirmCallback(nameEditor.getText().trim(), c);
        }
        if (auto* w = findParentComponentOfClass<juce::DialogWindow>())
            w->exitModalState(1);
    };

    btnCancel.onClick = [this]() {
        if (auto* w = findParentComponentOfClass<juce::DialogWindow>())
            w->exitModalState(0);
    };

    setSize(340, 160);
}

void MarkerEditorDialog::paint(juce::Graphics& g)
{
    g.fillAll(CubaseTheme::bgPanel());
    g.setColour(CubaseTheme::textSecondary());
    g.setFont(12.0f);
    g.drawText("Marker Label:", 20, 22, 100, 26, juce::Justification::centredLeft);
    g.drawText("Color Tag:", 20, 62, 100, 26, juce::Justification::centredLeft);
}

void MarkerEditorDialog::resized()
{
    nameEditor.setBounds(120, 22, getWidth() - 140, 26);
    colorCombo.setBounds(120, 62, 140, 26);

    btnCancel.setBounds(getWidth() - 170, getHeight() - 42, 70, 28);
    btnOk.setBounds(getWidth() - 90, getHeight() - 42, 70, 28);
}

void MarkerEditorDialog::showModal(const MarkerEvent& marker, std::function<void(const juce::String&, juce::Colour)> onConfirm)
{
    auto dialog = std::make_unique<MarkerEditorDialog>(marker, onConfirm);
    juce::DialogWindow::LaunchOptions opt;
    opt.dialogTitle = "Edit Marker";
    opt.dialogBackgroundColour = CubaseTheme::bgPanel();
    opt.content.setOwned(dialog.release());
    opt.useNativeTitleBar = true;
    opt.resizable = false;
    opt.launchAsync();
}

//==============================================================================
// 4. TimeSigEditorDialog
//==============================================================================
TimeSigEditorDialog::TimeSigEditorDialog(const TimeSignatureEvent& initialSig, std::function<void(int, int)> onConfirm)
    : confirmCallback(std::move(onConfirm))
{
    for (int n = 1; n <= 16; ++n)
        numCombo.addItem(juce::String(n), n);
    numCombo.setSelectedId(initialSig.numerator);
    addAndMakeVisible(numCombo);

    denCombo.addItem("2", 1);
    denCombo.addItem("4", 2);
    denCombo.addItem("8", 3);
    denCombo.addItem("16", 4);

    if (initialSig.denominator == 2) denCombo.setSelectedId(1);
    else if (initialSig.denominator == 8) denCombo.setSelectedId(3);
    else if (initialSig.denominator == 16) denCombo.setSelectedId(4);
    else denCombo.setSelectedId(2);

    addAndMakeVisible(denCombo);

    addAndMakeVisible(btnOk);
    addAndMakeVisible(btnCancel);

    btnOk.setColour(juce::TextButton::buttonColourId, CubaseTheme::accentOrange());
    btnOk.setColour(juce::TextButton::textColourOffId, juce::Colours::black);

    btnOk.onClick = [this]() {
        if (confirmCallback)
        {
            int num = numCombo.getSelectedId();
            int den = 4;
            if (denCombo.getSelectedId() == 1) den = 2;
            else if (denCombo.getSelectedId() == 3) den = 8;
            else if (denCombo.getSelectedId() == 4) den = 16;
            confirmCallback(num, den);
        }
        if (auto* w = findParentComponentOfClass<juce::DialogWindow>())
            w->exitModalState(1);
    };

    btnCancel.onClick = [this]() {
        if (auto* w = findParentComponentOfClass<juce::DialogWindow>())
            w->exitModalState(0);
    };

    setSize(300, 150);
}

void TimeSigEditorDialog::paint(juce::Graphics& g)
{
    g.fillAll(CubaseTheme::bgPanel());
    g.setColour(CubaseTheme::textSecondary());
    g.setFont(12.0f);
    g.drawText("Numerator (Beats):", 20, 20, 120, 26, juce::Justification::centredLeft);
    g.drawText("Denominator (Value):", 20, 56, 120, 26, juce::Justification::centredLeft);
}

void TimeSigEditorDialog::resized()
{
    numCombo.setBounds(150, 20, 110, 26);
    denCombo.setBounds(150, 56, 110, 26);

    btnCancel.setBounds(getWidth() - 170, getHeight() - 40, 70, 26);
    btnOk.setBounds(getWidth() - 90, getHeight() - 40, 70, 26);
}

void TimeSigEditorDialog::showModal(const TimeSignatureEvent& sig, std::function<void(int, int)> onConfirm)
{
    auto dialog = std::make_unique<TimeSigEditorDialog>(sig, onConfirm);
    juce::DialogWindow::LaunchOptions opt;
    opt.dialogTitle = "Edit Time Signature";
    opt.dialogBackgroundColour = CubaseTheme::bgPanel();
    opt.content.setOwned(dialog.release());
    opt.useNativeTitleBar = true;
    opt.resizable = false;
    opt.launchAsync();
}

//==============================================================================
// 4b. TempoEditorDialog
//==============================================================================
TempoEditorDialog::TempoEditorDialog(const TempoEvent& initialTempo, std::function<void(double, TempoTransitionType)> onConfirm)
    : confirmCallback(std::move(onConfirm))
{
    bpmSlider.setRange(20.0, 300.0, 0.1);
    bpmSlider.setValue(initialTempo.bpm);
    bpmSlider.setSliderStyle(juce::Slider::LinearBar);
    bpmSlider.setTextValueSuffix(" BPM");
    bpmSlider.setColour(juce::Slider::trackColourId, CubaseTheme::accentOrange().withAlpha(0.6f));
    bpmSlider.setColour(juce::Slider::textBoxOutlineColourId, CubaseTheme::borderSubtle());
    bpmSlider.setColour(juce::Slider::textBoxTextColourId, CubaseTheme::textPrimary());
    addAndMakeVisible(bpmSlider);

    transitionCombo.addItem(juce::String::fromUTF8((const char*)u8"Jump / 立即步进"), 1);
    transitionCombo.addItem(juce::String::fromUTF8((const char*)u8"Ramp / 线性渐变"), 2);
    transitionCombo.setSelectedId(initialTempo.type == TempoTransitionType::Ramp ? 2 : 1);
    addAndMakeVisible(transitionCombo);

    addAndMakeVisible(btnOk);
    addAndMakeVisible(btnCancel);

    btnOk.setColour(juce::TextButton::buttonColourId, CubaseTheme::accentOrange());
    btnOk.setColour(juce::TextButton::textColourOffId, juce::Colours::black);

    btnOk.onClick = [this]() {
        if (confirmCallback)
        {
            double bpm = bpmSlider.getValue();
            TempoTransitionType trans = (transitionCombo.getSelectedId() == 2) ? TempoTransitionType::Ramp : TempoTransitionType::Jump;
            confirmCallback(bpm, trans);
        }
        if (auto* w = findParentComponentOfClass<juce::DialogWindow>())
            w->exitModalState(1);
    };

    btnCancel.onClick = [this]() {
        if (auto* w = findParentComponentOfClass<juce::DialogWindow>())
            w->exitModalState(0);
    };

    setSize(320, 160);
}

void TempoEditorDialog::paint(juce::Graphics& g)
{
    g.fillAll(CubaseTheme::bgPanel());
    g.setColour(CubaseTheme::textSecondary());
    g.setFont(12.0f);
    g.drawText(juce::String::fromUTF8((const char*)u8"速度 (BPM):"), 20, 20, 120, 26, juce::Justification::centredLeft);
    g.drawText(juce::String::fromUTF8((const char*)u8"过渡类型 (Transition):"), 20, 56, 120, 26, juce::Justification::centredLeft);
}

void TempoEditorDialog::resized()
{
    bpmSlider.setBounds(150, 20, 150, 26);
    transitionCombo.setBounds(150, 56, 150, 26);

    btnCancel.setBounds(getWidth() - 170, getHeight() - 40, 70, 26);
    btnOk.setBounds(getWidth() - 90, getHeight() - 40, 70, 26);
}

void TempoEditorDialog::showModal(const TempoEvent& tempo, std::function<void(double, TempoTransitionType)> onConfirm)
{
    auto dialog = std::make_unique<TempoEditorDialog>(tempo, onConfirm);
    juce::DialogWindow::LaunchOptions opt;
    opt.dialogTitle = juce::String::fromUTF8((const char*)u8"编辑速度节点 (Edit Tempo)");
    opt.dialogBackgroundColour = CubaseTheme::bgPanel();
    opt.content.setOwned(dialog.release());
    opt.useNativeTitleBar = true;
    opt.resizable = false;
    opt.launchAsync();
}

//==============================================================================
//==============================================================================
// 5. SectionsManagerDialog
//==============================================================================
class SectionsManagerDialog::ModuleCard : public juce::Component
{
public:
    ModuleCard(SectionsManagerDialog& ownerIn, const SectionDockItem& item)
        : owner(ownerIn), itemId(item.id), name(item.name), position(item.position),
          hasSettings(item.id == 0 || item.id == 2 || item.id == 12
                      || item.id == kQuickChordSectionId)
    {
        setMouseCursor(juce::MouseCursor::DraggingHandCursor);
        if (hasSettings)
        {
            settingsButton.setButtonText(localizedUiText("Settings", u8"设置"));
            settingsButton.setTooltip(localizedUiText("Saved separately from the layout", u8"模块设置单独保存"));
            settingsButton.onClick = [this]() { owner.openModuleSettings(itemId); };
            addAndMakeVisible(settingsButton);
        }
    }

    void paint(juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat().reduced(1.0f);
        g.setColour(CubaseTheme::bgPanel());
        g.fillRoundedRectangle(bounds, 6.0f);
        if (hovered)
        {
            g.setColour(CubaseTheme::accentCyan().withAlpha(0.08f));
            g.fillRoundedRectangle(bounds, 6.0f);
        }
        g.setColour(hovered ? CubaseTheme::accentCyan() : CubaseTheme::borderSubtle());
        g.drawRoundedRectangle(bounds, 6.0f, 1.0f);
        g.setColour(position == SectionDockPosition::Hidden
                        ? CubaseTheme::accentOrange() : CubaseTheme::accentCyan());
        g.fillRoundedRectangle(1.0f, 5.0f, 3.0f, (float)getHeight() - 10.0f, 1.5f);

        g.setColour(CubaseTheme::textSecondary());
        for (int row = 0; row < 3; ++row)
            for (int col = 0; col < 2; ++col)
                g.fillEllipse(14.0f + col * 6.0f, 15.0f + row * 7.0f, 2.5f, 2.5f);

        g.setColour(CubaseTheme::textPrimary());
        g.setFont(CubaseTheme::getFont(11.5f, true));
        g.drawText(name, 33, 6, getWidth() - (hasSettings ? 119 : 43),
                   getHeight() - 12, juce::Justification::centredLeft, true);
    }

    void resized() override
    {
        if (hasSettings)
            settingsButton.setBounds(getWidth() - 82, 8, 72, getHeight() - 16);
    }

    void mouseDrag(const juce::MouseEvent& event) override
    {
        if (!dragStarted && event.getDistanceFromDragStart() > 5)
        {
            dragStarted = true;
            owner.startDragging(juce::var(itemId), this, juce::ScaledImage(),
                                false, nullptr, &event.source);
        }
    }

    void mouseEnter(const juce::MouseEvent&) override { hovered = true; repaint(); }
    void mouseExit(const juce::MouseEvent&) override { hovered = false; repaint(); }
    void mouseUp(const juce::MouseEvent&) override { dragStarted = false; }

    SectionsManagerDialog& owner;
    int itemId;

private:
    juce::String name;
    SectionDockPosition position;
    bool hasSettings;
    juce::TextButton settingsButton;
    bool dragStarted = false;
    bool hovered = false;
};

class SectionsManagerDialog::DockLane : public juce::Component,
                                        public juce::DragAndDropTarget
{
public:
    DockLane(SectionsManagerDialog& ownerIn, SectionDockPosition positionIn)
        : owner(ownerIn), position(positionIn) {}

    bool isInterestedInDragSource(const SourceDetails& details) override
    {
        auto* card = dynamic_cast<ModuleCard*>(details.sourceComponent.get());
        return card != nullptr && &card->owner == &owner;
    }

    void itemDragEnter(const SourceDetails& details) override { itemDragMove(details); }

    void itemDragMove(const SourceDetails& details) override
    {
        auto* card = dynamic_cast<ModuleCard*>(details.sourceComponent.get());
        if (card == nullptr) return;
        hovering = true;
        draggingItemId = card->itemId;
        insertionIndex = insertionForY(details.localPosition.y, card->itemId);
        repaint();
    }

    void itemDragExit(const SourceDetails&) override
    {
        hovering = false;
        insertionIndex = -1;
        draggingItemId = -1;
        repaint();
    }

    void itemDropped(const SourceDetails& details) override
    {
        auto* card = dynamic_cast<ModuleCard*>(details.sourceComponent.get());
        if (card == nullptr) return;
        const int index = insertionForY(details.localPosition.y, card->itemId);
        hovering = false;
        insertionIndex = -1;
        draggingItemId = -1;
        repaint();
        owner.moveItemToLane(card->itemId, position, index);
    }

    void paint(juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat().reduced(1.0f);
        g.setColour(hovering ? CubaseTheme::accentBlue().withAlpha(0.16f)
                             : CubaseTheme::bgDeep());
        g.fillRoundedRectangle(bounds, 7.0f);
        g.setColour(hovering ? CubaseTheme::accentCyan() : CubaseTheme::borderSubtle());
        g.drawRoundedRectangle(bounds, 7.0f, hovering ? 2.0f : 1.0f);

        const char* titleEn = position == SectionDockPosition::Left ? "Left sidebar"
                            : position == SectionDockPosition::Right ? "Right sidebar" : "Hidden";
        const char8_t* titleZh = position == SectionDockPosition::Left ? u8"左侧区域"
                              : position == SectionDockPosition::Right ? u8"右侧区域" : u8"隐藏";
        g.setColour(CubaseTheme::textPrimary());
        g.setFont(CubaseTheme::getFont(13.0f, true));
        g.drawText(localizedUiText(titleEn, titleZh), 14, 11, getWidth() - 62, 24,
                   juce::Justification::centredLeft);
        g.setColour(CubaseTheme::textSecondary());
        g.setFont(CubaseTheme::getFont(11.0f));
        g.drawText(juce::String(owner.cardCountInLane(position)), getWidth() - 40, 12, 25, 23,
                   juce::Justification::centredRight);
        g.setColour(CubaseTheme::borderSubtle());
        g.drawHorizontalLine(45, 12.0f, (float)getWidth() - 12.0f);

        if (owner.cardCountInLane(position) == 0 && !hovering)
        {
            g.setColour(CubaseTheme::textSecondary().withAlpha(0.68f));
            g.drawText(localizedUiText("Drag a module here", u8"将模块拖到这里"),
                       12, 74, getWidth() - 24, 30, juce::Justification::centred);
        }
        if (hovering && insertionIndex >= 0)
        {
            const int y = insertionLineY(insertionIndex, draggingItemId);
            g.setColour(CubaseTheme::accentCyan());
            g.fillRoundedRectangle(10.0f, (float)y, (float)getWidth() - 20.0f, 3.0f, 1.5f);
        }
    }

    SectionsManagerDialog& owner;
    SectionDockPosition position;

private:
    std::vector<const ModuleCard*> cardsExcept(int excludedItemId) const
    {
        std::vector<const ModuleCard*> result;
        for (int i = 0; i < getNumChildComponents(); ++i)
        {
            auto* card = dynamic_cast<ModuleCard*>(getChildComponent(i));
            if (card != nullptr && card->itemId != excludedItemId)
                result.push_back(card);
        }
        std::sort(result.begin(), result.end(), [](const auto* a, const auto* b) {
            return a->getY() < b->getY();
        });
        return result;
    }

    int insertionForY(int y, int excludingItemId) const
    {
        int index = 0;
        for (const auto* card : cardsExcept(excludingItemId))
        {
            if (y < card->getY() + card->getHeight() / 2)
                break;
            ++index;
        }
        return index;
    }

    int insertionLineY(int index, int excludingItemId) const
    {
        auto remaining = cardsExcept(excludingItemId);
        if (remaining.empty()) return 56;
        if (index < (int)remaining.size()) return remaining[(size_t)index]->getY() - 2;
        return remaining.back()->getBottom() + 6;
    }

    bool hovering = false;
    int insertionIndex = -1;
    int draggingItemId = -1;
};

SectionsManagerDialog::~SectionsManagerDialog() = default;

SectionsManagerDialog::SectionsManagerDialog(const ManagerData& initialData,
                                             std::function<void(const ManagerData&)> onApply)
    : applyCallback(std::move(onApply)), data(initialData),
      currentContextIndex(std::clamp(initialData.initialContextIndex, 0, 7))
{
    static const char* contextNamesEn[8] = {
        "Piano Roll", "Instrument Track", "Chord Track", "Mode Track",
        "Marker Track", "Time Signature", "Folder Track", "Audio Track"
    };
    static const char8_t* contextNamesZh[8] = {
        u8"钢琴卷帘窗", u8"乐器轨", u8"和弦轨", u8"调式轨",
        u8"标记轨", u8"拍号轨", u8"文件夹轨", u8"音频轨"
    };
    for (int i = 0; i < 8; ++i)
    {
        auto button = std::make_unique<juce::TextButton>(
            localizedUiText(contextNamesEn[i], contextNamesZh[i]));
        button->onClick = [this, i]() {
            if (currentContextIndex != i)
            {
                currentContextIndex = i;
                loadContextToUi(i);
            }
        };
        addAndMakeVisible(button.get());
        contextButtons[(size_t)i] = std::move(button);
    }

    static constexpr SectionDockPosition positions[3] = {
        SectionDockPosition::Left, SectionDockPosition::Right, SectionDockPosition::Hidden
    };
    for (int i = 0; i < 3; ++i)
    {
        lanes[(size_t)i] = std::make_unique<DockLane>(*this, positions[i]);
        addAndMakeVisible(lanes[(size_t)i].get());
    }

    chkLeftZone.setButtonText(localizedUiText("Show left sidebar", u8"显示左侧区域"));
    chkLeftZone.setToggleState(data.leftZoneVisible, juce::dontSendNotification);
    chkLeftZone.onClick = [this]() { data.leftZoneVisible = chkLeftZone.getToggleState(); };
    addAndMakeVisible(chkLeftZone);

    chkRightZone.setButtonText(localizedUiText("Show right sidebar", u8"显示右侧区域"));
    chkRightZone.setToggleState(data.rightZoneVisible, juce::dontSendNotification);
    chkRightZone.onClick = [this]() { data.rightZoneVisible = chkRightZone.getToggleState(); };
    addAndMakeVisible(chkRightZone);

    btnReset.setButtonText(localizedUiText("Restore current defaults", u8"恢复当前默认布局"));
    btnReset.setTooltip(localizedUiText("Restore positions and order in the current view", u8"恢复当前视图中的模块位置和顺序"));
    btnReset.onClick = [this]() {
        if (currentContextIndex >= 2)
        {
            for (auto& item : getItemsForContext(currentContextIndex))
                item.position = SectionDockPosition::Left;
            loadContextToUi(currentContextIndex);
            return;
        }
        auto& target = getItemsForContext(currentContextIndex);
        for (auto& item : target)
        {
            if (currentContextIndex == 0)
            {
                item.position = item.id == 0 ? SectionDockPosition::Left
                    : item.id == 3 ? SectionDockPosition::Hidden
                    : SectionDockPosition::Right;
            }
            else
                item.position = item.id == 4 || item.id == kReharmonizationSectionId
                    || item.id == kQuickChordSectionId
                    ? SectionDockPosition::Right : SectionDockPosition::Left;
        }
        auto rank = [](int id) {
            auto it = std::find(kDefaultSectionOrder.begin(), kDefaultSectionOrder.end(), id);
            return std::distance(kDefaultSectionOrder.begin(), it);
        };
        std::stable_sort(target.begin(), target.end(), [&](const auto& a, const auto& b) {
            return rank(a.id) < rank(b.id);
        });
        syncSharedModuleOrder();
        loadContextToUi(currentContextIndex);
    };
    addAndMakeVisible(btnReset);

    btnOk.setButtonText(localizedUiText("Save layout", u8"保存布局"));
    btnOk.setColour(juce::TextButton::buttonColourId, CubaseTheme::accentCyan());
    btnOk.setColour(juce::TextButton::textColourOffId, juce::Colours::black);
    btnOk.onClick = [this]() {
        if (applyCallback) applyCallback(data);
        if (auto* w = findParentComponentOfClass<juce::DialogWindow>())
            w->exitModalState(1);
    };
    addAndMakeVisible(btnOk);

    btnCancel.setButtonText(localizedUiText("Cancel layout", u8"取消布局"));
    btnCancel.onClick = [this]() {
        if (auto* w = findParentComponentOfClass<juce::DialogWindow>())
            w->exitModalState(0);
    };
    addAndMakeVisible(btnCancel);

    setSize(1120, 600);
    loadContextToUi(currentContextIndex);
}

std::vector<SectionDockItem>& SectionsManagerDialog::getItemsForContext(int idx)
{
    switch (idx)
    {
        case 0: return data.keyEditorItems;
        case 1: return data.instTrackItems;
        case 2: return data.chordTrackItems;
        case 3: return data.modeTrackItems;
        case 4: return data.markerTrackItems;
        case 5: return data.timeSigTrackItems;
        case 6: return data.folderTrackItems;
        case 7: return data.audioTrackItems;
        default: return data.instTrackItems;
    }
}

void SectionsManagerDialog::updateContextButtons()
{
    for (size_t i = 0; i < contextButtons.size(); ++i)
    {
        const bool active = (int)i == currentContextIndex;
        contextButtons[i]->setColour(juce::TextButton::buttonColourId,
                                     active ? CubaseTheme::accentBlue() : CubaseTheme::bgSurface());
        contextButtons[i]->setColour(juce::TextButton::textColourOffId,
                                     active ? juce::Colours::white : CubaseTheme::textSecondary());
    }
    repaint();
}

int SectionsManagerDialog::cardCountInLane(SectionDockPosition position, int excludingItemId) const
{
    const auto& target = getItemsForContext(currentContextIndex);
    return (int)std::count_if(target.begin(), target.end(), [&](const auto& item) {
        return item.position == position && item.id != excludingItemId;
    });
}

const std::vector<SectionDockItem>& SectionsManagerDialog::getItemsForContext(int idx) const
{
    switch (idx)
    {
        case 0: return data.keyEditorItems;
        case 1: return data.instTrackItems;
        case 2: return data.chordTrackItems;
        case 3: return data.modeTrackItems;
        case 4: return data.markerTrackItems;
        case 5: return data.timeSigTrackItems;
        case 6: return data.folderTrackItems;
        case 7: return data.audioTrackItems;
        default: return data.instTrackItems;
    }
}

void SectionsManagerDialog::loadContextToUi(int contextIdx)
{
    cards.clear();
    for (auto& item : getItemsForContext(contextIdx))
    {
        if ((int)item.position < 0 || (int)item.position > 2)
            item.position = SectionDockPosition::Left;
        auto card = std::make_unique<ModuleCard>(*this, item);
        lanes[(size_t)item.position]->addAndMakeVisible(card.get());
        cards.push_back(std::move(card));
    }
    updateContextButtons();
    resized();
    repaint();
}

void SectionsManagerDialog::openModuleSettings(int itemId)
{
    juce::Component::SafePointer<SectionsManagerDialog> safeThis(this);
    auto onApplied = [safeThis]() {
        if (auto* dialog = safeThis.getComponent())
            if (dialog->onSettingsApplied)
                dialog->onSettingsApplied();
    };
    if (itemId == 0)
        NoteInspectorSettingsDialog::showModal(std::move(onApplied));
    else if (itemId == 2)
        ChordAnalysisSettingsDialog::showModal(std::move(onApplied));
    else if (itemId == 12)
        ChordShapeSettingsDialog::showModal(std::move(onApplied));
    else if (itemId == kQuickChordSectionId)
        QuickChordSettingsDialog::showModal(std::move(onApplied));
}

void SectionsManagerDialog::moveItemToLane(int itemId, SectionDockPosition newPosition,
                                           int insertionIndex)
{
    auto& target = getItemsForContext(currentContextIndex);
    const auto dragged = std::find_if(target.begin(), target.end(), [itemId](const auto& item) {
        return item.id == itemId;
    });
    if (dragged == target.end())
        return;

    SectionDockItem moved = *dragged;
    moved.position = newPosition;
    std::array<std::vector<SectionDockItem>, 3> groups;
    for (const auto& item : target)
        if (item.id != itemId)
            groups[(size_t)item.position].push_back(item);

    auto& destination = groups[(size_t)newPosition];
    destination.insert(destination.begin() + std::clamp(insertionIndex, 0, (int)destination.size()),
                       std::move(moved));
    target.clear();
    for (auto& group : groups)
        target.insert(target.end(), group.begin(), group.end());
    syncSharedModuleOrder();

    // The drag source is still being used by JUCE until itemDropped returns.
    juce::Component::SafePointer<SectionsManagerDialog> safeThis(this);
    juce::MessageManager::callAsync([safeThis]() {
        if (auto* dialog = safeThis.getComponent())
            dialog->loadContextToUi(dialog->currentContextIndex);
    });
}

void SectionsManagerDialog::syncSharedModuleOrder()
{
    if (currentContextIndex > 1)
        return;

    const auto& source = getItemsForContext(currentContextIndex);
    auto& other = getItemsForContext(1 - currentContextIndex);
    std::vector<SectionDockItem> reordered;
    reordered.reserve(other.size());
    for (const auto& item : source)
    {
        auto it = std::find_if(other.begin(), other.end(), [&](const auto& candidate) {
            return candidate.id == item.id;
        });
        if (it != other.end())
            reordered.push_back(*it);
    }
    if (reordered.size() == other.size())
        other = std::move(reordered);
}

void SectionsManagerDialog::paint(juce::Graphics& g)
{
    g.fillAll(CubaseTheme::bgDeep());
    g.setColour(CubaseTheme::bgSurface());
    g.fillRect(0, 0, getWidth(), 102);
    g.setColour(CubaseTheme::borderSubtle());
    g.drawHorizontalLine(102, 0.0f, (float)getWidth());
    g.setColour(CubaseTheme::textSecondary());
    g.setFont(CubaseTheme::getFont(11.5f));
    g.drawText(currentContextIndex < 2
                   ? localizedUiText("Drag cards to change position and order. Piano Roll and Instrument Track share their order.",
                                     u8"拖动卡片调整位置和顺序；钢琴卷帘窗与乐器轨共用排序。")
                   : localizedUiText("Drag the module to the left, right or hidden area.",
                                     u8"将模块拖到左侧、右侧或隐藏区域。"),
               18, 67, getWidth() - 244, 20, juce::Justification::centredLeft);

    const int footerTop = getHeight() - 80;
    g.setColour(CubaseTheme::bgSurface());
    g.fillRect(0, footerTop, getWidth(), 80);
    g.setColour(CubaseTheme::borderSubtle());
    g.drawHorizontalLine(footerTop, 0.0f, (float)getWidth());
    g.setColour(CubaseTheme::textSecondary());
    g.setFont(CubaseTheme::getFont(11.5f, true));
    g.drawText(data.detachedZoneControls
                   ? localizedUiText("Detached editor sidebars", u8"独立卷帘窗侧栏")
                   : localizedUiText("Main window sidebars", u8"主窗口侧栏"),
               18, footerTop + 23, 185, 27,
               juce::Justification::centredLeft);
}

void SectionsManagerDialog::resized()
{
    const int contextGap = 8;
    const int contextWidth = (getWidth() - 32 - contextGap * 7) / 8;
    for (size_t i = 0; i < contextButtons.size(); ++i)
        contextButtons[i]->setBounds(16 + (int)i * (contextWidth + contextGap),
                                      14, contextWidth, 38);
    btnReset.setBounds(getWidth() - 210, 61, 192, 30);

    const int laneGap = 12;
    const int laneWidth = (getWidth() - 32 - laneGap * 2) / 3;
    const int laneHeight = getHeight() - 198;
    for (size_t i = 0; i < lanes.size(); ++i)
        lanes[i]->setBounds(16 + (int)i * (laneWidth + laneGap), 106,
                            laneWidth, laneHeight);

    std::array<int, 3> laneCardCounts {};
    for (const auto& item : getItemsForContext(currentContextIndex))
    {
        const auto lane = (size_t)item.position;
        auto it = std::find_if(cards.begin(), cards.end(), [&](const auto& card) {
            return card->itemId == item.id;
        });
        if (it != cards.end())
            (*it)->setBounds(10, 58 + laneCardCounts[lane]++ * 54,
                             laneWidth - 20, 46);
    }

    const int footerTop = getHeight() - 80;
    chkLeftZone.setBounds(210, footerTop + 25, 155, 25);
    chkRightZone.setBounds(375, footerTop + 25, 155, 25);
    btnCancel.setBounds(getWidth() - 240, footerTop + 20, 90, 38);
    btnOk.setBounds(getWidth() - 138, footerTop + 20, 120, 38);
}

void SectionsManagerDialog::showModal(const ManagerData& initialData,
                                     std::function<void(const ManagerData&)> onApply,
                                     std::function<void()> onSettingsApplied)
{
    auto dialog = std::make_unique<SectionsManagerDialog>(initialData, onApply);
    if (onSettingsApplied)
        dialog->setOnSettingsApplied(onSettingsApplied);
    juce::DialogWindow::LaunchOptions opt;
    opt.dialogTitle = localizedUiText("Sections manager", u8"分区管理");
    opt.dialogBackgroundColour = CubaseTheme::bgPanel();
    opt.content.setOwned(dialog.release());
    opt.useNativeTitleBar = true;
    opt.resizable = false;
    opt.launchAsync();
}

//==============================================================================
// 5b. NoteInspectorSettingsDialog Implementation
//==============================================================================
NoteInspectorSettingsDialog::NoteInspectorSettingsDialog(std::function<void()> onApplied)
    : applyCallback(std::move(onApplied))
{
    auto& state = AppStateManager::getInstance();

    grpFields.setText(localizedUiText("Visible fields", u8"显示字段"));
    grpFields.setTextLabelPosition(juce::Justification::left);
    addAndMakeVisible(grpFields);

    auto setupCheck = [this](juce::ToggleButton& btn, const juce::String& text, bool initialVal) {
        btn.setButtonText(text);
        btn.setToggleState(initialVal, juce::dontSendNotification);
        btn.setColour(juce::ToggleButton::textColourId, CubaseTheme::textPrimary());
        btn.setColour(juce::ToggleButton::tickColourId, CubaseTheme::accentCyan());
        addAndMakeVisible(btn);
    };

    setupCheck(chkPitch,          localizedUiText("Pitch", "音高"),                   state.noteShowPitch);
    setupCheck(chkStart,          localizedUiText("Start position", "起始位置"),      state.noteShowStart);
    setupCheck(chkLength,         localizedUiText("Note length", "音符时值"),        state.noteShowLength);
    setupCheck(chkVelocity,       localizedUiText("Velocity", "击弦力度"),          state.noteShowVelocity);
    setupCheck(chkScaleDegree,    localizedUiText("Scale degree", "调式音级"),      state.noteShowScaleDegree);
    setupCheck(chkFunctionalRole, localizedUiText("Modal function", "调式功能"),    state.noteShowFunctionalRole);
    setupCheck(chkMotionTendency, localizedUiText("Motion tendency", "运动倾向"),   state.noteShowMotionTendency);
    setupCheck(chkColorUsage,     localizedUiText("Harmonic color", "色彩定位"),     state.noteShowColorUsage);
    setupCheck(chkInterval,       localizedUiText("Interval", "双音音程"),          state.noteShowInterval);

    // Font size controls
    lblFontSizeTitle.setText(localizedUiText("Text size", "内容字号"), juce::dontSendNotification);
    lblFontSizeTitle.setFont(CubaseTheme::getFont(12.0f));
    lblFontSizeTitle.setColour(juce::Label::textColourId, CubaseTheme::textPrimary());
    addAndMakeVisible(lblFontSizeTitle);

    sliderFontSize.setSliderStyle(juce::Slider::LinearHorizontal);
    sliderFontSize.setTextBoxStyle(juce::Slider::TextBoxRight, false, 55, 20);
    sliderFontSize.setRange(9.0, 24.0, 1.0);
    sliderFontSize.setValue((double)state.notePropertyFontSizePt, juce::dontSendNotification);
    sliderFontSize.setTextValueSuffix(" pt");
    addAndMakeVisible(sliderFontSize);

    lblFontSizeHint.setText(localizedUiText("9–24 pt, default 12", u8"9–24 pt，默认 12 pt"), juce::dontSendNotification);
    lblFontSizeHint.setFont(CubaseTheme::getFont(11.0f));
    lblFontSizeHint.setColour(juce::Label::textColourId, CubaseTheme::textSecondary());
    addAndMakeVisible(lblFontSizeHint);

    lblHint.setText(localizedUiText("Hold Ctrl and scroll over the panel to adjust text size.", u8"按住 Ctrl 并滚动鼠标滚轮，可在音符属性面板中微调字号。"), juce::dontSendNotification);
    lblHint.setFont(CubaseTheme::getFont(11.0f));
    lblHint.setColour(juce::Label::textColourId, CubaseTheme::textSecondary());
    addAndMakeVisible(lblHint);

    btnResetDefault.setButtonText(localizedUiText("Restore defaults", "恢复默认"));
    btnResetDefault.onClick = [this]() {
        chkPitch.setToggleState(true, juce::dontSendNotification);
        chkStart.setToggleState(true, juce::dontSendNotification);
        chkLength.setToggleState(true, juce::dontSendNotification);
        chkVelocity.setToggleState(true, juce::dontSendNotification);
        chkScaleDegree.setToggleState(true, juce::dontSendNotification);
        chkFunctionalRole.setToggleState(true, juce::dontSendNotification);
        chkMotionTendency.setToggleState(true, juce::dontSendNotification);
        chkColorUsage.setToggleState(true, juce::dontSendNotification);
        chkInterval.setToggleState(true, juce::dontSendNotification);
        sliderFontSize.setValue(12.0, juce::dontSendNotification);
    };
    addAndMakeVisible(btnResetDefault);

    btnOk.setButtonText(localizedUiText("Save settings", "保存设置"));
    btnOk.setColour(juce::TextButton::buttonColourId, CubaseTheme::accentCyan());
    btnOk.setColour(juce::TextButton::textColourOffId, juce::Colours::black);
    btnOk.onClick = [this]() {
        auto& s = AppStateManager::getInstance();
        s.noteShowPitch          = chkPitch.getToggleState();
        s.noteShowStart          = chkStart.getToggleState();
        s.noteShowLength         = chkLength.getToggleState();
        s.noteShowVelocity       = chkVelocity.getToggleState();
        s.noteShowScaleDegree    = chkScaleDegree.getToggleState();
        s.noteShowFunctionalRole = chkFunctionalRole.getToggleState();
        s.noteShowMotionTendency = chkMotionTendency.getToggleState();
        s.noteShowColorUsage     = chkColorUsage.getToggleState();
        s.noteShowInterval       = chkInterval.getToggleState();
        s.notePropertyFontSizePt = (int)std::round(sliderFontSize.getValue());
        s.save();
        if (applyCallback) applyCallback();
        if (auto* w = findParentComponentOfClass<juce::DialogWindow>())
            w->exitModalState(1);
    };
    addAndMakeVisible(btnOk);

    btnCancel.setButtonText(localizedUiText("Cancel", "取消"));
    btnCancel.onClick = [this]() {
        if (auto* w = findParentComponentOfClass<juce::DialogWindow>())
            w->exitModalState(0);
    };
    addAndMakeVisible(btnCancel);

    setSize(560, 480);
}

void NoteInspectorSettingsDialog::paint(juce::Graphics& g)
{
    g.fillAll(CubaseTheme::bgDeep());
    g.setColour(CubaseTheme::bgSurface());
    g.fillRect(0, 0, getWidth(), 70);
    g.setColour(CubaseTheme::textPrimary());
    g.setFont(CubaseTheme::getFont(19.0f, true));
    g.drawText(localizedUiText("Note inspector settings", "音符属性设置"), 20, 11, getWidth() - 40, 27,
               juce::Justification::centredLeft);
    g.setColour(CubaseTheme::textSecondary());
    g.setFont(CubaseTheme::getFont(11.5f));
    g.drawText(localizedUiText("Choose the note details shown in the inspector.", "选择要在检查器中显示的音符信息。"),
               20, 39, getWidth() - 40, 20, juce::Justification::centredLeft);
    g.setFont(CubaseTheme::getFont(11.0f, true));
    g.drawText(localizedUiText("Basic details", "基础信息"), 42, 116, 210, 20,
               juce::Justification::centredLeft);
    g.drawText(localizedUiText("Harmony analysis", "和声分析"), 296, 116, 210, 20,
               juce::Justification::centredLeft);
    g.setColour(CubaseTheme::borderSubtle());
    g.drawVerticalLine(280, 117.0f, 322.0f);
    g.drawHorizontalLine(419, 0.0f, (float)getWidth());
    g.drawRect(getLocalBounds(), 1);
}

void NoteInspectorSettingsDialog::resized()
{
    grpFields.setBounds(20, 84, 520, 252);
    chkPitch.setBounds(42, 145, 210, 26);
    chkStart.setBounds(42, 178, 210, 26);
    chkLength.setBounds(42, 211, 210, 26);
    chkVelocity.setBounds(42, 244, 210, 26);
    chkScaleDegree.setBounds(296, 145, 220, 26);
    chkFunctionalRole.setBounds(296, 178, 220, 26);
    chkMotionTendency.setBounds(296, 211, 220, 26);
    chkColorUsage.setBounds(296, 244, 220, 26);
    chkInterval.setBounds(296, 277, 220, 26);

    lblFontSizeTitle.setBounds(28, 351, 96, 28);
    sliderFontSize.setBounds(126, 351, 250, 28);
    lblFontSizeHint.setBounds(388, 351, 155, 28);
    lblHint.setBounds(28, 389, 510, 23);

    btnResetDefault.setBounds(20, 435, 110, 30);
    btnCancel.setBounds(356, 435, 80, 30);
    btnOk.setBounds(444, 435, 96, 30);
}

void NoteInspectorSettingsDialog::showModal(std::function<void()> onApplied)
{
    auto dialog = std::make_unique<NoteInspectorSettingsDialog>(onApplied);
    juce::DialogWindow::LaunchOptions opt;
    opt.dialogTitle = localizedUiText("Note inspector settings", "音符属性设置");
    opt.dialogBackgroundColour = CubaseTheme::bgPanel();
    opt.content.setOwned(dialog.release());
    opt.useNativeTitleBar = true;
    opt.resizable = false;
    opt.launchAsync();
}

//==============================================================================
// 5c. ChordAnalysisSettingsDialog Implementation
//==============================================================================
ChordAnalysisSettingsDialog::ChordAnalysisSettingsDialog(std::function<void()> onApplied)
    : applyCallback(std::move(onApplied))
{
    auto& state = AppStateManager::getInstance();

    auto setupLabel = [this](juce::Label& lbl, const juce::String& text) {
        lbl.setText(text, juce::dontSendNotification);
        lbl.setFont(CubaseTheme::getFont(12.0f));
        lbl.setColour(juce::Label::textColourId, CubaseTheme::textPrimary());
        addAndMakeVisible(lbl);
    };

    setupLabel(lblDisplayModeTitle, localizedUiText("Content", u8"显示内容"));
    comboDisplayMode.addItem(localizedUiText("Chord name and scale degree", u8"和弦名称与调式级数"), 1);
    comboDisplayMode.addItem(localizedUiText("Chord name only", u8"仅显示和弦名称"), 2);
    comboDisplayMode.addItem(localizedUiText("Scale degree only", u8"仅显示调式级数"), 3);
    comboDisplayMode.setSelectedId(state.chordAnalysisDisplayMode + 1, juce::dontSendNotification);
    comboDisplayMode.onChange = [this]() { updatePreview(); };
    addAndMakeVisible(comboDisplayMode);

    setupLabel(lblOrientationTitle, localizedUiText("Layout", u8"排列方式"));
    comboOrientation.addItem(localizedUiText("Vertical", u8"上下排列"), 1);
    comboOrientation.addItem(localizedUiText("Horizontal", u8"左右排列"), 2);
    comboOrientation.setSelectedId(state.chordAnalysisOrientation + 1, juce::dontSendNotification);
    comboOrientation.onChange = [this]() { updatePreview(); };
    addAndMakeVisible(comboOrientation);

    setupLabel(lblFontSizeTitle, localizedUiText("Text size", u8"内容字号"));
    sliderFontSize.setSliderStyle(juce::Slider::LinearHorizontal);
    sliderFontSize.setTextBoxStyle(juce::Slider::TextBoxRight, false, 50, 20);
    sliderFontSize.setRange(10.0, 48.0, 1.0);
    sliderFontSize.setValue((double)state.chordAnalysisFontSizePt, juce::dontSendNotification);
    sliderFontSize.setTextValueSuffix(" pt");
    sliderFontSize.onValueChange = [this]() { updatePreview(); };
    addAndMakeVisible(sliderFontSize);

    lblFontSizeHint.setText(localizedUiText("10–48 pt; hold Ctrl and scroll to fine tune", u8"10–48 pt；在模块中按住 Ctrl + 滚轮可微调"), juce::dontSendNotification);
    lblFontSizeHint.setFont(CubaseTheme::getFont(10.5f));
    lblFontSizeHint.setColour(juce::Label::textColourId, CubaseTheme::textSecondary());
    addAndMakeVisible(lblFontSizeHint);

    setupLabel(lblVisualStyleTitle, localizedUiText("Color theme", u8"配色方案"));
    comboVisualStyle.addItem(localizedUiText("Classic: gold / cyan", u8"经典：金黄 / 青色"), 1);
    comboVisualStyle.addItem(localizedUiText("Neon: amber / magenta", u8"霓虹：琥珀橙 / 品红"), 2);
    comboVisualStyle.addItem(localizedUiText("Silver: white / ice blue", u8"白银：银白 / 冰蓝"), 3);
    comboVisualStyle.addItem(localizedUiText("Mint: emerald / mint", u8"薄荷：翡翠绿 / 薄荷青"), 4);
    comboVisualStyle.addItem(localizedUiText("Violet: purple / pink", u8"粉紫：紫罗兰 / 荧光粉"), 5);
    comboVisualStyle.addItem(localizedUiText("Sunset: coral / gold", u8"落日：珊瑚红 / 暖金黄"), 6);
    comboVisualStyle.addItem(localizedUiText("Aurora: blue / lavender", u8"极光：电光蓝 / 薰衣草"), 7);
    comboVisualStyle.addItem(localizedUiText("Matrix: green / sage", u8"矩阵：荧光绿 / 灰绿"), 8);
    comboVisualStyle.setSelectedId(state.chordAnalysisVisualStyle + 1, juce::dontSendNotification);
    comboVisualStyle.onChange = [this]() { updatePreview(); };
    addAndMakeVisible(comboVisualStyle);

    grpPreview.setText(localizedUiText("Live preview", u8"实时预览"));
    addAndMakeVisible(grpPreview);

    lblPreviewChord.setText("Cmaj7", juce::dontSendNotification);
    lblPreviewChord.setJustificationType(juce::Justification::centred);
    grpPreview.addAndMakeVisible(lblPreviewChord);

    lblPreviewRoman.setText(juce::String::fromUTF8(u8"级数: Imaj7"), juce::dontSendNotification);
    lblPreviewRoman.setJustificationType(juce::Justification::centred);
    grpPreview.addAndMakeVisible(lblPreviewRoman);

    btnResetDefault.setButtonText(localizedUiText("Restore defaults", u8"恢复默认"));
    btnResetDefault.onClick = [this]() {
        comboDisplayMode.setSelectedId(1);
        comboOrientation.setSelectedId(1);
        sliderFontSize.setValue(18.0);
        comboVisualStyle.setSelectedId(1);
        updatePreview();
    };
    addAndMakeVisible(btnResetDefault);

    btnOk.setButtonText(localizedUiText("Save settings", u8"保存设置"));
    btnOk.setColour(juce::TextButton::buttonColourId, CubaseTheme::accentCyan());
    btnOk.setColour(juce::TextButton::textColourOffId, juce::Colours::black);
    btnOk.onClick = [this]() {
        auto& s = AppStateManager::getInstance();
        s.chordAnalysisDisplayMode = comboDisplayMode.getSelectedId() - 1;
        s.chordAnalysisOrientation = comboOrientation.getSelectedId() - 1;
        s.chordAnalysisFontSizePt  = (int)sliderFontSize.getValue();
        s.chordAnalysisVisualStyle = comboVisualStyle.getSelectedId() - 1;
        s.save();
        if (applyCallback) applyCallback();
        if (auto* w = findParentComponentOfClass<juce::DialogWindow>())
            w->exitModalState(1);
    };
    addAndMakeVisible(btnOk);

    btnCancel.setButtonText(localizedUiText("Cancel", u8"取消"));
    btnCancel.onClick = [this]() {
        if (auto* w = findParentComponentOfClass<juce::DialogWindow>())
            w->exitModalState(0);
    };
    addAndMakeVisible(btnCancel);

    updatePreview();
    setSize(540, 560);
}

void ChordAnalysisSettingsDialog::updatePreview()
{
    int mode = comboDisplayMode.getSelectedId() - 1;
    int orient = comboOrientation.getSelectedId() - 1;
    float pt = (float)sliderFontSize.getValue();
    int style = comboVisualStyle.getSelectedId() - 1;

    lblPreviewChord.setVisible(mode != 2);
    lblPreviewRoman.setVisible(mode != 1);

    float chordFont = pt;
    float romanFont = std::max(9.0f, std::round(pt * 0.72f));
    lblPreviewChord.setFont(juce::FontOptions(chordFont).withStyle("bold"));
    lblPreviewRoman.setFont(juce::FontOptions(romanFont).withStyle("bold"));

    switch (style)
    {
        case 1: // 霓虹科技
            lblPreviewChord.setColour(juce::Label::textColourId, juce::Colour(0xffff9900));
            lblPreviewRoman.setColour(juce::Label::textColourId, juce::Colour(0xffff44aa));
            break;
        case 2: // 纯净白银
            lblPreviewChord.setColour(juce::Label::textColourId, juce::Colours::white);
            lblPreviewRoman.setColour(juce::Label::textColourId, juce::Colour(0xffb0c4de));
            break;
        case 3: // 翡翠薄荷
            lblPreviewChord.setColour(juce::Label::textColourId, juce::Colour(0xff10b981));
            lblPreviewRoman.setColour(juce::Label::textColourId, juce::Colour(0xff6ee7b7));
            break;
        case 4: // 赛博粉紫
            lblPreviewChord.setColour(juce::Label::textColourId, juce::Colour(0xffa855f7));
            lblPreviewRoman.setColour(juce::Label::textColourId, juce::Colour(0xfff472b6));
            break;
        case 5: // 落日炽焰
            lblPreviewChord.setColour(juce::Label::textColourId, juce::Colour(0xffef4444));
            lblPreviewRoman.setColour(juce::Label::textColourId, juce::Colour(0xfff59e0b));
            break;
        case 6: // 极光幽蓝
            lblPreviewChord.setColour(juce::Label::textColourId, juce::Colour(0xff38bdf8));
            lblPreviewRoman.setColour(juce::Label::textColourId, juce::Colour(0xff818cf8));
            break;
        case 7: // 矩阵暗绿
            lblPreviewChord.setColour(juce::Label::textColourId, juce::Colour(0xff22c55e));
            lblPreviewRoman.setColour(juce::Label::textColourId, juce::Colour(0xff86efac));
            break;
        case 0: // 经典高亮
        default:
            lblPreviewChord.setColour(juce::Label::textColourId, CubaseTheme::accentGold());
            lblPreviewRoman.setColour(juce::Label::textColourId, CubaseTheme::accentCyan());
            break;
    }

    auto pArea = grpPreview.getLocalBounds().reduced(8, 20);
    int cH = juce::roundToInt(chordFont * 1.35f);
    int rH = juce::roundToInt(romanFont * 1.35f);

    if (orient == 1) // Horizontal Inline
    {
        int rowH = std::clamp(std::max(cH, rH), 24, pArea.getHeight());
        auto row = pArea.removeFromTop(rowH);
        if (mode == 0) // Both
        {
            int halfW = row.getWidth() / 2;
            lblPreviewChord.setBounds(row.removeFromLeft(halfW));
            lblPreviewRoman.setBounds(row);
        }
        else if (mode == 1) // Chord only
        {
            lblPreviewChord.setBounds(row);
        }
        else if (mode == 2) // Roman only
        {
            lblPreviewRoman.setBounds(row);
        }
    }
    else // Vertical Stack
    {
        if (mode == 0)
        {
            lblPreviewChord.setBounds(pArea.removeFromTop(std::min(cH, pArea.getHeight() / 2)));
            pArea.removeFromTop(2);
            lblPreviewRoman.setBounds(pArea.removeFromTop(std::min(rH, pArea.getHeight())));
        }
        else if (mode == 1)
        {
            lblPreviewChord.setBounds(pArea.removeFromTop(std::min(cH + 4, pArea.getHeight())));
        }
        else if (mode == 2)
        {
            lblPreviewRoman.setBounds(pArea.removeFromTop(std::min(rH + 4, pArea.getHeight())));
        }
    }
}

void ChordAnalysisSettingsDialog::paint(juce::Graphics& g)
{
    g.fillAll(CubaseTheme::bgDeep());
    g.setColour(CubaseTheme::bgSurface());
    g.fillRect(0, 0, getWidth(), 70);
    g.setColour(CubaseTheme::textPrimary());
    g.setFont(CubaseTheme::getFont(19.0f, true));
    g.drawText(localizedUiText("Chord analysis settings", u8"实时和弦分析设置"), 20, 11, getWidth() - 40, 27,
               juce::Justification::centredLeft);
    g.setColour(CubaseTheme::textSecondary());
    g.setFont(CubaseTheme::getFont(11.5f));
    g.drawText(localizedUiText("Adjust content, layout and color with a live preview.", u8"调整信息、排版和配色，下方可立即预览。"),
               20, 39, getWidth() - 40, 20, juce::Justification::centredLeft);
    g.setColour(CubaseTheme::bgSurface());
    g.fillRoundedRectangle(20.0f, 86.0f, (float)getWidth() - 40.0f, 252.0f, 7.0f);
    g.setColour(CubaseTheme::borderSubtle());
    g.drawHorizontalLine(505, 0.0f, (float)getWidth());
    g.setColour(CubaseTheme::borderSubtle());
    g.drawRect(getLocalBounds(), 1);
}

void ChordAnalysisSettingsDialog::resized()
{
    lblDisplayModeTitle.setBounds(38, 103, 90, 32);
    comboDisplayMode.setBounds(136, 103, 366, 32);
    lblOrientationTitle.setBounds(38, 153, 90, 32);
    comboOrientation.setBounds(136, 153, 366, 32);
    lblFontSizeTitle.setBounds(38, 203, 90, 32);
    sliderFontSize.setBounds(136, 203, 366, 32);
    lblFontSizeHint.setBounds(136, 238, 366, 23);
    lblVisualStyleTitle.setBounds(38, 277, 90, 32);
    comboVisualStyle.setBounds(136, 277, 366, 32);
    grpPreview.setBounds(20, 353, 500, 135);
    btnResetDefault.setBounds(20, 520, 110, 30);
    btnCancel.setBounds(338, 520, 80, 30);
    btnOk.setBounds(426, 520, 94, 30);
    updatePreview();
}

void ChordAnalysisSettingsDialog::showModal(std::function<void()> onApplied)
{
    auto dialog = std::make_unique<ChordAnalysisSettingsDialog>(onApplied);
    juce::DialogWindow::LaunchOptions opt;
    opt.dialogTitle = localizedUiText("Chord analysis settings", u8"实时和弦分析设置");
    opt.dialogBackgroundColour = CubaseTheme::bgPanel();
    opt.content.setOwned(dialog.release());
    opt.useNativeTitleBar = true;
    opt.resizable = false;
    opt.launchAsync();
}

ChordShapeSettingsDialog::ChordShapeSettingsDialog(std::function<void()> onApplied)
    : applyCallback(std::move(onApplied))
{
    showDirections.setButtonText(localizedUiText(
        "Show major / minor / dominant stack directions",
        u8"显示大／小／属高叠方向按钮"));
    showDirections.setToggleState(AppStateManager::getInstance().showChordStackDirections,
                                  juce::dontSendNotification);
    addAndMakeVisible(showDirections);

    explanation.setText(localizedUiText(
        "Natural stacking follows the current mode and is always available.",
        u8"默认使用当前调式内的自然高叠；调内按钮始终显示。"),
        juce::dontSendNotification);
    explanation.setColour(juce::Label::textColourId, CubaseTheme::textSecondary());
    explanation.setFont(CubaseTheme::getFont(11.5f));
    addAndMakeVisible(explanation);

    saveButton.setButtonText(localizedUiText("Save", u8"保存"));
    cancelButton.setButtonText(localizedUiText("Cancel", u8"取消"));
    addAndMakeVisible(saveButton);
    addAndMakeVisible(cancelButton);
    saveButton.onClick = [this] {
        auto& state = AppStateManager::getInstance();
        state.showChordStackDirections = showDirections.getToggleState();
        state.save();
        if (applyCallback) applyCallback();
        if (auto* window = findParentComponentOfClass<juce::DialogWindow>())
            window->exitModalState(1);
    };
    cancelButton.onClick = [this] {
        if (auto* window = findParentComponentOfClass<juce::DialogWindow>())
            window->exitModalState(0);
    };
    setSize(490, 174);
}

void ChordShapeSettingsDialog::paint(juce::Graphics& g)
{
    g.fillAll(CubaseTheme::bgPanel());
    g.setColour(CubaseTheme::textPrimary());
    g.setFont(CubaseTheme::getFont(16.0f, true));
    g.drawText(localizedUiText("Chord shape settings", u8"和弦类型与排列设置"),
               20, 12, getWidth() - 40, 24, juce::Justification::centredLeft);
}

void ChordShapeSettingsDialog::resized()
{
    showDirections.setBounds(20, 45, getWidth() - 40, 26);
    explanation.setBounds(25, 80, getWidth() - 50, 28);
    cancelButton.setBounds(getWidth() - 190, getHeight() - 46, 80, 29);
    saveButton.setBounds(getWidth() - 99, getHeight() - 46, 80, 29);
}

void ChordShapeSettingsDialog::showModal(std::function<void()> onApplied)
{
    auto dialog = std::make_unique<ChordShapeSettingsDialog>(std::move(onApplied));
    juce::DialogWindow::LaunchOptions options;
    options.dialogTitle = localizedUiText("Chord shape settings", u8"和弦类型与排列设置");
    options.dialogBackgroundColour = CubaseTheme::bgPanel();
    options.content.setOwned(dialog.release());
    options.useNativeTitleBar = true;
    options.resizable = false;
    options.launchAsync();
}

//==============================================================================
// 6. BarOffsetDialog Implementation
//==============================================================================
BarOffsetDialog::BarOffsetDialog(int initialOffset, std::function<void(int)> onConfirm)
    : confirmCallback(std::move(onConfirm)), currentOffset(initialOffset)
{
    lblTitle.setText(juce::String::fromUTF8(u8"走带小节编号偏移设置 (Bar Number Offset)"), juce::dontSendNotification);
    lblTitle.setFont(CubaseTheme::getFont(13.0f, true));
    lblTitle.setColour(juce::Label::textColourId, CubaseTheme::textPrimary());
    lblTitle.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(lblTitle);

    // Stepper
    btnMinus.setButtonText("-");
    btnMinus.setColour(juce::TextButton::buttonColourId, CubaseTheme::buttonBackground());
    btnMinus.setColour(juce::TextButton::textColourOffId, CubaseTheme::textPrimary());
    btnMinus.onClick = [this]() { setOffsetValue(currentOffset - 1); };
    addAndMakeVisible(btnMinus);

    lblValue.setFont(juce::FontOptions(26.0f).withStyle("bold"));
    lblValue.setColour(juce::Label::backgroundColourId, CubaseTheme::bgDeep());
    lblValue.setColour(juce::Label::textColourId, CubaseTheme::accentCyan());
    lblValue.setColour(juce::Label::outlineColourId, CubaseTheme::borderSubtle());
    lblValue.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(lblValue);

    btnPlus.setButtonText("+");
    btnPlus.setColour(juce::TextButton::buttonColourId, CubaseTheme::buttonBackground());
    btnPlus.setColour(juce::TextButton::textColourOffId, CubaseTheme::textPrimary());
    btnPlus.onClick = [this]() { setOffsetValue(currentOffset + 1); };
    addAndMakeVisible(btnPlus);

    // Slider
    sliderOffset.setRange(-8.0, 8.0, 1.0);
    sliderOffset.setValue((double)currentOffset, juce::dontSendNotification);
    sliderOffset.setSliderStyle(juce::Slider::LinearHorizontal);
    sliderOffset.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    sliderOffset.onValueChange = [this]() { setOffsetValue((int)sliderOffset.getValue()); };
    addAndMakeVisible(sliderOffset);

    // Presets
    btnPresetStandard.setButtonText(juce::String::fromUTF8(u8"标准工程 (从第 1 小节开始 / 偏移 0)"));
    btnPresetStandard.onClick = [this]() { setOffsetValue(0); };
    addAndMakeVisible(btnPresetStandard);

    btnPresetPickup.setButtonText(juce::String::fromUTF8(u8"弱起小节 (从第 0 小节开始 / 偏移 -1)"));
    btnPresetPickup.onClick = [this]() { setOffsetValue(-1); };
    addAndMakeVisible(btnPresetPickup);

    btnPresetCountIn.setButtonText(juce::String::fromUTF8(u8"预备拍 2 小节 (从第 -1 小节开始 / 偏移 -2)"));
    btnPresetCountIn.onClick = [this]() { setOffsetValue(-2); };
    addAndMakeVisible(btnPresetCountIn);

    btnPresetIntro.setButtonText(juce::String::fromUTF8(u8"前奏 4 小节 (从第 -3 小节开始 / 偏移 -4)"));
    btnPresetIntro.onClick = [this]() { setOffsetValue(-4); };
    addAndMakeVisible(btnPresetIntro);

    // Live explanation
    lblMappingExplanation.setFont(CubaseTheme::getFont(12.0f));
    lblMappingExplanation.setColour(juce::Label::backgroundColourId, CubaseTheme::bgPanel());
    lblMappingExplanation.setColour(juce::Label::textColourId, CubaseTheme::accentOrange());
    lblMappingExplanation.setColour(juce::Label::outlineColourId, CubaseTheme::borderSubtle());
    lblMappingExplanation.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(lblMappingExplanation);

    btnOk.setButtonText(juce::String::fromUTF8(u8"确定"));
    btnOk.setColour(juce::TextButton::buttonColourId, CubaseTheme::accentCyan());
    btnOk.setColour(juce::TextButton::textColourOffId, juce::Colours::black);
    btnOk.onClick = [this]() {
        if (confirmCallback) confirmCallback(currentOffset);
        if (auto* w = findParentComponentOfClass<juce::DialogWindow>())
            w->exitModalState(1);
    };
    addAndMakeVisible(btnOk);

    btnCancel.setButtonText(juce::String::fromUTF8(u8"取消"));
    btnCancel.onClick = [this]() {
        if (auto* w = findParentComponentOfClass<juce::DialogWindow>())
            w->exitModalState(0);
    };
    addAndMakeVisible(btnCancel);

    setOffsetValue(currentOffset);
    setSize(440, 320);
}

void BarOffsetDialog::setOffsetValue(int val)
{
    currentOffset = std::clamp(val, -8, 8);
    sliderOffset.setValue((double)currentOffset, juce::dontSendNotification);

    juce::String sign = currentOffset > 0 ? "+" : "";
    lblValue.setText(sign + juce::String(currentOffset), juce::dontSendNotification);

    updateReadout();
}

void BarOffsetDialog::updateReadout()
{
    int startBarNumber = 1 + currentOffset;
    juce::String text = juce::String::fromUTF8(u8"标尺起始点 (0.0 拍) 对应标记为：第 ") + juce::String(startBarNumber) + juce::String::fromUTF8(u8" 小节");
    lblMappingExplanation.setText(text, juce::dontSendNotification);
}

void BarOffsetDialog::paint(juce::Graphics& g)
{
    g.fillAll(CubaseTheme::bgDeep());
    g.setColour(CubaseTheme::borderSubtle());
    g.drawRect(getLocalBounds(), 1);
}

void BarOffsetDialog::resized()
{
    lblTitle.setBounds(16, 12, getWidth() - 32, 24);

    // Stepper row
    int cx = getWidth() / 2;
    btnMinus.setBounds(cx - 100, 48, 44, 40);
    lblValue.setBounds(cx - 50, 48, 100, 40);
    btnPlus.setBounds(cx + 56, 48, 44, 40);

    sliderOffset.setBounds(30, 96, getWidth() - 60, 24);

    // Preset buttons
    int py = 132;
    int ph = 26;
    btnPresetStandard.setBounds(30, py, getWidth() - 60, ph); py += ph + 6;
    btnPresetPickup.setBounds(30, py, getWidth() - 60, ph); py += ph + 6;
    btnPresetCountIn.setBounds(30, py, getWidth() - 60, ph); py += ph + 6;
    btnPresetIntro.setBounds(30, py, getWidth() - 60, ph); py += ph + 10;

    lblMappingExplanation.setBounds(24, py, getWidth() - 48, 30);

    btnCancel.setBounds(getWidth() - 175, getHeight() - 38, 75, 26);
    btnOk.setBounds(getWidth() - 90, getHeight() - 38, 75, 26);
}

void BarOffsetDialog::showModal(TrackModel& trackModel)
{
    auto dialog = std::make_unique<BarOffsetDialog>(trackModel.getBarNumberOffset(), [&trackModel](int newOffset) {
        trackModel.setBarNumberOffset(newOffset);
    });

    juce::DialogWindow::LaunchOptions opt;
    opt.dialogTitle = juce::String::fromUTF8(u8"小节编号偏移 (Bar Number Offset)");
    opt.dialogBackgroundColour = CubaseTheme::bgPanel();
    opt.content.setOwned(dialog.release());
    opt.useNativeTitleBar = true;
    opt.resizable = false;
    opt.launchAsync();
}

QuickChordSettingsDialog::QuickChordSettingsDialog(std::function<void()> onApplied)
    : applyCallback(std::move(onApplied))
{
    complexMode.setButtonText(localizedUiText("Complex mode", u8"复杂模式"));
    complexMode.setToggleState(AppStateManager::getInstance().quickChordComplexMode,
                               juce::dontSendNotification);
    addAndMakeVisible(complexMode);

    explanation.setText(localizedUiText(
        "Simple mode shows common chords; complex mode shows the full catalog.",
        u8"简单模式显示常用和弦；复杂模式显示完整和弦目录。"),
        juce::dontSendNotification);
    explanation.setColour(juce::Label::textColourId, CubaseTheme::textSecondary());
    explanation.setFont(CubaseTheme::getFont(11.5f));
    addAndMakeVisible(explanation);

    preserveTiming.setButtonText(localizedUiText(
        "Preserve selected note timing", u8"保留所选音符的时间位置"));
    preserveTiming.setToggleState(
        AppStateManager::getInstance().quickChordPreserveSelectedTiming,
        juce::dontSendNotification);
    preserveTiming.setTooltip(localizedUiText(
        "Additional chord tones use the root note's timing.",
        u8"新增的声部沿用根音的时间位置。"));
    addAndMakeVisible(preserveTiming);

    timingExplanation.setText(localizedUiText(
        "Map voices by pitch; keep their start, length and velocity.",
        u8"按音高对应声部，保留原音符的起点、时长和力度。"),
        juce::dontSendNotification);
    timingExplanation.setColour(juce::Label::textColourId, CubaseTheme::textSecondary());
    timingExplanation.setFont(CubaseTheme::getFont(11.5f));
    addAndMakeVisible(timingExplanation);

    saveButton.setButtonText(localizedUiText("Save", u8"保存"));
    cancelButton.setButtonText(localizedUiText("Cancel", u8"取消"));
    addAndMakeVisible(saveButton);
    addAndMakeVisible(cancelButton);
    saveButton.onClick = [this] {
        auto& state = AppStateManager::getInstance();
        state.quickChordComplexMode = complexMode.getToggleState();
        state.quickChordPreserveSelectedTiming = preserveTiming.getToggleState();
        state.save();
        if (applyCallback) applyCallback();
        if (auto* window = findParentComponentOfClass<juce::DialogWindow>())
            window->exitModalState(1);
    };
    cancelButton.onClick = [this] {
        if (auto* window = findParentComponentOfClass<juce::DialogWindow>())
            window->exitModalState(0);
    };
    setSize(480, 218);
}

void QuickChordSettingsDialog::paint(juce::Graphics& g)
{
    g.fillAll(CubaseTheme::bgPanel());
    g.setColour(CubaseTheme::textPrimary());
    g.setFont(CubaseTheme::getFont(16.0f, true));
    g.drawText(localizedUiText("Quick chord settings", u8"快速和弦设置"),
               20, 12, getWidth() - 40, 24, juce::Justification::centredLeft);
}

void QuickChordSettingsDialog::resized()
{
    complexMode.setBounds(20, 45, getWidth() - 40, 26);
    explanation.setBounds(25, 80, getWidth() - 50, 28);
    preserveTiming.setBounds(20, 111, getWidth() - 40, 26);
    timingExplanation.setBounds(25, 142, getWidth() - 50, 24);
    cancelButton.setBounds(getWidth() - 190, getHeight() - 46, 80, 29);
    saveButton.setBounds(getWidth() - 99, getHeight() - 46, 80, 29);
}

void QuickChordSettingsDialog::showModal(std::function<void()> onApplied)
{
    auto dialog = std::make_unique<QuickChordSettingsDialog>(std::move(onApplied));
    juce::DialogWindow::LaunchOptions options;
    options.dialogTitle = localizedUiText("Quick chord settings", u8"快速和弦设置");
    options.dialogBackgroundColour = CubaseTheme::bgPanel();
    options.content.setOwned(dialog.release());
    options.useNativeTitleBar = true;
    options.resizable = false;
    options.launchAsync();
}

} // namespace Harmonizer
