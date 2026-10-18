#include "TransportBar.h"
#include "CubaseTheme.h"
#include "../core/TrackModel.h"
#include <iomanip>
#include <sstream>

namespace Harmonizer
{

TransportBar::TransportBar(MidiModel& model, AudioEngine& engine, std::function<void()> onOpenPrefs, TrackModel* tm)
    : midiModel(model), audioEngine(engine), trackModel(tm), openPreferencesCallback(onOpenPrefs)
{
    midiModel.addListener(this);
    audioEngine.addPlayheadListener(this);

    // Transport buttons
    btnRewind.onClick = [this]() { audioEngine.stop(); };
    addAndMakeVisible(btnRewind);

    btnPlay.onClick = [this]() {
        if (audioEngine.isPlaying())
            audioEngine.pause();
        else
            audioEngine.play();
    };
    addAndMakeVisible(btnPlay);

    btnStop.onClick = [this]() { audioEngine.stop(); };
    addAndMakeVisible(btnStop);

    btnLoop.setClickingTogglesState(true);
    btnLoop.setToggleState(midiModel.isLoopEnabled(), juce::dontSendNotification);
    btnLoop.onClick = [this]() { midiModel.setLoopEnabled(btnLoop.getToggleState()); };
    addAndMakeVisible(btnLoop);

    btnMetronome.setClickingTogglesState(true);
    btnMetronome.onClick = [this]() {
        audioEngine.setMetronomeEnabled(btnMetronome.getToggleState());
        refreshMetronomeState();
    };
    addAndMakeVisible(btnMetronome);
    btnMetronomeSound.onClick = [this]() {
        juce::PopupMenu menu;
        const auto sound = audioEngine.getMetronomeSound();
        menu.addItem(1, tr("Classic Click"), true, sound == MetronomeSound::Classic);
        menu.addItem(2, tr("Wood Click"), true, sound == MetronomeSound::Wood);
        menu.addItem(3, tr("Electronic Click"), true, sound == MetronomeSound::Electronic);
        juce::Component::SafePointer<TransportBar> safeThis(this);
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&btnMetronomeSound),
            [safeThis](int choice) {
                if (auto* self = safeThis.getComponent())
                    if (choice >= 1 && choice <= 3)
                    {
                        self->audioEngine.setMetronomeSound(
                            static_cast<MetronomeSound>(choice - 1));
                        self->refreshMetronomeState();
                    }
            });
    };
    addAndMakeVisible(btnMetronomeSound);

    // BPM Label & Editor
    lblBpm.setText(juce::String(midiModel.getBpm(), 1) + " BPM", juce::dontSendNotification);
    lblBpm.setColour(juce::Label::backgroundColourId, CubaseTheme::bgDeep());
    lblBpm.setColour(juce::Label::textColourId, CubaseTheme::accentOrange());
    lblBpm.setColour(juce::Label::outlineColourId, CubaseTheme::borderSubtle());
    lblBpm.setJustificationType(juce::Justification::centred);
    lblBpm.setEditable(true);
    lblBpm.onTextChange = [this]() {
        double newBpm = lblBpm.getText().getDoubleValue();
        if (newBpm >= 20.0 && newBpm <= 400.0)
            midiModel.setBpm(newBpm);
    };
    lblBpm.addMouseListener(this, false);
    addAndMakeVisible(lblBpm);

    // TimeSig Label
    lblTimeSig.setText(juce::String(midiModel.getTimeSigNumerator()) + "/" + juce::String(midiModel.getTimeSigDenominator()), juce::dontSendNotification);
    lblTimeSig.setColour(juce::Label::backgroundColourId, CubaseTheme::bgDeep());
    lblTimeSig.setColour(juce::Label::textColourId, CubaseTheme::textPrimary());
    lblTimeSig.setColour(juce::Label::outlineColourId, CubaseTheme::borderSubtle());
    lblTimeSig.setJustificationType(juce::Justification::centred);
    lblTimeSig.addMouseListener(this, false);
    addAndMakeVisible(lblTimeSig);

    Localization::getInstance().addListener(this);
    CubaseTheme::addThemeListener(this);
    themeChanged();
    languageChanged();
    updateTimecodeStrings(0.0);
    refreshMetronomeState();
}

void TransportBar::refreshMetronomeState()
{
    const bool enabled = audioEngine.isMetronomeEnabled();
    btnMetronome.setToggleState(enabled, juce::dontSendNotification);
    btnMetronome.setColour(juce::TextButton::buttonColourId,
        enabled ? CubaseTheme::accentBlue() : CubaseTheme::buttonBackground());
    btnMetronome.setColour(juce::TextButton::textColourOffId, CubaseTheme::textPrimary());
    btnMetronomeSound.setColour(juce::TextButton::buttonColourId, CubaseTheme::buttonBackground());
    btnMetronomeSound.setColour(juce::TextButton::textColourOffId, CubaseTheme::textPrimary());
    btnMetronomeSound.setTooltip(tr("Metronome sound preset"));
}

void TransportBar::showTimeSigPopup()
{
    juce::PopupMenu menu;
    menu.addItem(1, "4/4");
    menu.addItem(2, "3/4");
    menu.addItem(3, "2/4");
    menu.addItem(4, "6/8");
    menu.addItem(5, "5/4");
    menu.addItem(6, "7/8");

    juce::Component::SafePointer<TransportBar> safeThis(this);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&lblTimeSig), [this, safeThis](int res) {
        if (safeThis.getComponent() == nullptr) return;
        int num = 4, den = 4;
        if (res == 1) { num = 4; den = 4; }
        else if (res == 2) { num = 3; den = 4; }
        else if (res == 3) { num = 2; den = 4; }
        else if (res == 4) { num = 6; den = 8; }
        else if (res == 5) { num = 5; den = 4; }
        else if (res == 6) { num = 7; den = 8; }
        else return;

        midiModel.setTimeSignature(num, den);

        if (trackModel != nullptr)
        {
            trackModel->setDefaultTimeSignature(num, den);
            trackModel->triggerTimeSignaturesChanged();
            trackModel->triggerTracksChanged();
        }

        lblTimeSig.setText(juce::String(num) + "/" + juce::String(den), juce::dontSendNotification);
        repaint();
    });
}

TransportBar::~TransportBar()
{
    CubaseTheme::removeThemeListener(this);
    Localization::getInstance().removeListener(this);
    audioEngine.removePlayheadListener(this);
    midiModel.removeListener(this);
}

void TransportBar::themeChanged()
{
    refreshMetronomeState();
    lblBpm.setColour(juce::Label::backgroundColourId, CubaseTheme::bgDeep());
    lblBpm.setColour(juce::Label::textColourId, CubaseTheme::accentOrange());
    lblBpm.setColour(juce::Label::outlineColourId, CubaseTheme::borderSubtle());

    lblTimeSig.setColour(juce::Label::backgroundColourId, CubaseTheme::bgDeep());
    lblTimeSig.setColour(juce::Label::textColourId, CubaseTheme::textPrimary());
    lblTimeSig.setColour(juce::Label::outlineColourId, CubaseTheme::borderSubtle());

    sendLookAndFeelChange();
    repaint();
}

void TransportBar::languageChanged()
{
    updatePlayButtonState(audioEngine.isPlaying());
    btnRewind.setTooltip(juce::String::fromUTF8(u8"回到起点 (Rewind) [Home]"));
    btnStop.setTooltip(juce::String::fromUTF8(u8"停止 (Stop) [Numpad 0]"));
    btnLoop.setTooltip(juce::String::fromUTF8(u8"循环播放 (Cycle / Loop) [/]"));
    btnMetronome.setButtonText(tr("Click"));
    btnMetronome.setTooltip(tr("Toggle metronome") + " [C]");
    btnMetronomeSound.setButtonText(tr("Sound"));
    refreshMetronomeState();
    repaint();
}

void TransportBar::updatePlayButtonState(bool isPlaying)
{
    btnPlay.setDAWIcon(isPlaying ? DAWIcon::Pause : DAWIcon::Play);
    btnPlay.setToggleState(isPlaying, juce::dontSendNotification);
    btnPlay.setTooltip(isPlaying ? juce::String::fromUTF8(u8"暂停 (Pause) [Space]")
                                 : juce::String::fromUTF8(u8"播放 (Play) [Space]"));
    repaint();
}

void TransportBar::updateTimecodeStrings(double beat)
{
    int offset = trackModel ? trackModel->getBarNumberOffset() : 0;
    int bar = 1;
    int subBeat = 1;
    if (trackModel != nullptr)
    {
        auto span = trackModel->getBarAtBeat(beat);
        bar = span.barIndex + offset;
        double beatInBar = std::max(0.0, beat - span.startBeat);
        subBeat = (int)beatInBar + 1;
    }
    else
    {
        int beatsPerBar = std::max(1, midiModel.getTimeSigNumerator());
        bar = (int)(beat / beatsPerBar) + 1 + offset;
        subBeat = (int)std::fmod(beat, beatsPerBar) + 1;
    }
    double fracBeat = std::fmod(beat, 1.0);
    int sixteenth = (int)(fracBeat * 4.0) + 1;
    int tick = (int)(std::fmod(fracBeat * 4.0, 1.0) * 240.0);

    char buf1[64];
    std::snprintf(buf1, sizeof(buf1), "%03d . %02d . %02d . %03d", bar, subBeat, sixteenth, tick);
    currentBarBeatTimecode = buf1;

    double totalSecs = (beat / std::max(20.0, midiModel.getBpm())) * 60.0;
    int hours = (int)(totalSecs / 3600.0);
    int mins = (int)(std::fmod(totalSecs, 3600.0) / 60.0);
    int secs = (int)std::fmod(totalSecs, 60.0);
    int millis = (int)(std::fmod(totalSecs, 1.0) * 1000.0);

    char buf2[64];
    std::snprintf(buf2, sizeof(buf2), "%02d:%02d:%02d.%03d", hours, mins, secs, millis);
    currentSecondsTimecode = buf2;
}

void TransportBar::playheadMoved(double beatPosition)
{
    updateTimecodeStrings(beatPosition);
    repaint();
}

void TransportBar::playbackStateChanged(bool isPlaying)
{
    updatePlayButtonState(isPlaying);
    repaint();
}

void TransportBar::projectSettingsChanged()
{
    lblBpm.setText(juce::String(midiModel.getBpm(), 1) + " BPM", juce::dontSendNotification);
    lblTimeSig.setText(juce::String(midiModel.getTimeSigNumerator()) + "/" + juce::String(midiModel.getTimeSigDenominator()), juce::dontSendNotification);
    btnLoop.setToggleState(midiModel.isLoopEnabled(), juce::dontSendNotification);
    repaint();
}

void TransportBar::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds();

    // Bar background
    g.setColour(CubaseTheme::toolbarBackground());
    g.fillRect(bounds);

    g.setColour(CubaseTheme::borderSubtle());
    g.drawHorizontalLine(0, 0.0f, (float)bounds.getWidth());

    // Cubase Timecode Display Screen
    auto screenRect = juce::Rectangle<float>(10.0f, 6.0f, 240.0f, (float)bounds.getHeight() - 12.0f);
    g.setColour(CubaseTheme::bgDeep());
    g.fillRoundedRectangle(screenRect, 3.0f);
    g.setColour(CubaseTheme::borderSubtle());
    g.drawRoundedRectangle(screenRect, 3.0f, 1.0f);

    // Primary Bar.Beat LED Text (Monospaced Consolas to prevent glyph jittering)
    g.setColour(CubaseTheme::accentCyan());
    g.setFont(juce::FontOptions(15.0f).withName("Consolas").withStyle("bold"));
    g.drawText(currentBarBeatTimecode, screenRect.reduced(6.0f, 2.0f).removeFromTop(18.0f).toNearestInt(), juce::Justification::centred);

    // Secondary Seconds Display
    g.setColour(CubaseTheme::textSecondary());
    g.setFont(juce::FontOptions(11.0f).withName("Consolas"));
    g.drawText(currentSecondsTimecode, screenRect.reduced(6.0f, 2.0f).removeFromBottom(14.0f).toNearestInt(), juce::Justification::centred);
}

void TransportBar::resized()
{
    int y = 6;
    int h = getHeight() - 12;

    int x = 265;

    // Transport buttons: 紧凑优雅专业排布
    btnRewind.setBounds(x, y, 36, h); x += 40;
    btnPlay.setBounds(x, y, 48, h);   x += 52;
    btnStop.setBounds(x, y, 36, h);   x += 40;
    btnLoop.setBounds(x, y, 38, h);   x += 48;

    // BPM & TimeSig
    lblBpm.setBounds(x, y, 82, h);     x += 88;
    lblTimeSig.setBounds(x, y, 52, h); x += 58;
    btnMetronome.setBounds(x, y, 54, h); x += 58;
    btnMetronomeSound.setBounds(x, y, 62, h);
}


void TransportBar::mouseDown(const juce::MouseEvent& e)
{
    if (e.eventComponent == &lblTimeSig || lblTimeSig.getBounds().contains(e.getPosition()))
    {
        showTimeSigPopup();
    }
}

void TransportBar::mouseDrag(const juce::MouseEvent& e)
{
    if (e.eventComponent == &lblBpm || lblBpm.getBounds().contains(e.getPosition()))
    {
        // Dragging vertically adjusts BPM
        double delta = -e.getDistanceFromDragStartY() * 0.1;
        double current = midiModel.getBpm();
        double newBpm = std::clamp(std::round(current + delta), 20.0, 400.0);
        if (std::abs(newBpm - current) >= 1.0)
            midiModel.setBpm(newBpm);
    }
}

void TransportBar::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    if (e.eventComponent == &lblBpm || lblBpm.getBounds().contains(e.getPosition()))
    {
        double delta = (wheel.deltaY > 0 ? 1.0 : -1.0);
        double newBpm = std::clamp(midiModel.getBpm() + delta, 20.0, 400.0);
        midiModel.setBpm(newBpm);
    }
}

} // namespace Harmonizer
