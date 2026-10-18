#include "PianoKeyboardComponent.h"
#include "CubaseTheme.h"
#include <cmath>

namespace Harmonizer
{

static const int WHITE_KEY_PCS[7] = { 0, 2, 4, 5, 7, 9, 11 }; // C, D, E, F, G, A, B
static const int BLACK_KEY_PCS[5] = { 1, 3, 6, 8, 10 };       // C#, D#, F#, G#, A#
static const int BLACK_SEAM_MAP[12] = { -1, 1, -1, 2, -1, -1, 4, -1, 5, -1, 6, -1 };

PianoKeyboardComponent::PianoKeyboardComponent(AudioEngine& engine, MidiModel& model)
    : audioEngine(engine), midiModel(model)
{
    setSize(64, (int)(128 * noteHeight));
}

bool PianoKeyboardComponent::isBlackKey(int pitch)
{
    int pc = (pitch % 12 + 12) % 12;
    return (pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10);
}

void PianoKeyboardComponent::setNoteHeight(float height)
{
    noteHeight = std::clamp(height, 8.0f, 48.0f);
    setSize(getWidth(), (int)(128 * noteHeight));
    repaint();
}

int PianoKeyboardComponent::yToPitch(float y) const
{
    return getPitchAt((float)getWidth() * 0.8f, y);
}

float PianoKeyboardComponent::pitchToY(int pitch) const
{
    return (127 - pitch) * noteHeight;
}

int PianoKeyboardComponent::getPitchAt(float x, float y) const
{
    float w = (float)getWidth();
    float blackKeyW = w * 0.65f;
    float whPerOct = (12.0f / 7.0f) * noteHeight;
    float bkH = std::max(8.0f, noteHeight * 1.05f);

    int pitchLinear = 127 - (int)(y / noteHeight);
    int oct = (int)std::floor(pitchLinear / 12.0f) - 1;

    // Check octaves around the cursor
    for (int o = oct - 1; o <= oct + 1; ++o)
    {
        int octCPitch = (o + 1) * 12;
        float octBotY = pitchToY(octCPitch) + noteHeight;

        // 1. If cursor is on the left, check black keys first
        if (x <= blackKeyW)
        {
            for (int bPc : BLACK_KEY_PCS)
            {
                int seamIdx = BLACK_SEAM_MAP[bPc];
                float seamY = octBotY - (float)seamIdx * whPerOct;
                float bkTop = seamY - bkH / 2.0f;
                float bkBot = bkTop + bkH;

                if (y >= bkTop && y <= bkBot)
                {
                    int p = octCPitch + bPc;
                    return std::clamp(p, 0, 127);
                }
            }
        }

        // 2. Check white keys
        for (int wI = 0; wI < 7; ++wI)
        {
            float wBot = octBotY - (float)wI * whPerOct;
            float wTop = octBotY - (float)(wI + 1) * whPerOct;

            if (y >= wTop && y <= wBot)
            {
                int p = octCPitch + WHITE_KEY_PCS[wI];
                return std::clamp(p, 0, 127);
            }
        }
    }

    return std::clamp(pitchLinear, 0, 127);
}

void PianoKeyboardComponent::mouseDown(const juce::MouseEvent& e)
{
    int pitch = getPitchAt((float)e.x, (float)e.y);
    float vel = std::clamp((float)e.x / (float)getWidth(), 0.3f, 1.0f);

    lastPreviewPitch = pitch;
    audioEngine.previewNoteOn(pitch, vel);
    repaint();
}

void PianoKeyboardComponent::mouseDrag(const juce::MouseEvent& e)
{
    int pitch = getPitchAt((float)e.x, (float)e.y);
    if (pitch != lastPreviewPitch)
    {
        if (lastPreviewPitch >= 0)
            audioEngine.previewNoteOff(lastPreviewPitch);

        float vel = std::clamp((float)e.x / (float)getWidth(), 0.3f, 1.0f);
        lastPreviewPitch = pitch;
        audioEngine.previewNoteOn(pitch, vel);
        repaint();
    }
}

void PianoKeyboardComponent::mouseUp(const juce::MouseEvent&)
{
    if (lastPreviewPitch >= 0)
    {
        audioEngine.previewNoteOff(lastPreviewPitch);
        lastPreviewPitch = -1;
    }
    audioEngine.allPreviewNotesOff();
    repaint();
}

void PianoKeyboardComponent::mouseExit(const juce::MouseEvent&)
{
    if (lastPreviewPitch >= 0)
    {
        audioEngine.previewNoteOff(lastPreviewPitch);
        lastPreviewPitch = -1;
        audioEngine.allPreviewNotesOff();
        repaint();
    }
}

void PianoKeyboardComponent::paint(juce::Graphics& g)
{
    auto clip = g.getClipBounds();
    float w = (float)getWidth();
    float blackKeyW = std::round(w * 0.65f);
    float whPerOct = (12.0f / 7.0f) * noteHeight;
    float bkH = std::max(8.0f, std::round(noteHeight * 1.05f));

    int minPitchEstimate = std::max(0, 127 - (int)((clip.getBottom() + noteHeight) / noteHeight));
    int maxPitchEstimate = std::min(127, 127 - (int)(clip.getY() / noteHeight));

    int minOct = (int)std::floor(minPitchEstimate / 12.0f) - 1;
    int maxOct = (int)std::floor(maxPitchEstimate / 12.0f) - 1;

    int tonicPc = midiModel.getKeySignature().rootPitchClass;

    // Pass 1: Render All White Keys (Continuous full-width base, exactly equal height)
    for (int oct = minOct; oct <= maxOct + 1; ++oct)
    {
        int octCPitch = (oct + 1) * 12;
        float octBotY = pitchToY(octCPitch) + noteHeight;

        for (int wI = 0; wI < 7; ++wI)
        {
            float wBot = octBotY - (float)wI * whPerOct;
            float wTop = octBotY - (float)(wI + 1) * whPerOct;
            int pc = WHITE_KEY_PCS[wI];
            int p = octCPitch + pc;

            if (wTop > (float)clip.getBottom() || wBot < (float)clip.getY() || p < 0 || p > 127)
                continue;

            bool isPressed = (p == lastPreviewPitch);
            bool isTonic = (pc == tonicPc);

            // Base ivory white background
            juce::Colour keyBg = isPressed ? CubaseTheme::accentBlue() : juce::Colour(0xffe2e5ec);
            g.setColour(keyBg);
            g.fillRect(0.0f, wTop, w, wBot - wTop);

            // Subtle gold tint on right label area for tonic
            if (isTonic && !isPressed)
            {
                g.setColour(juce::Colour(0x28eab308));
                g.fillRect(blackKeyW, wTop, w - blackKeyW, wBot - wTop);
            }

            // White key seams:
            // B/C seam (wI == 0) and E/F seam (wI == 3) have no black key covering them: full 0 to w line!
            // Other seams start from blackKeyW to w.
            bool isFullCut = (wI == 0 || wI == 3);
            float cutX = isFullCut ? 0.0f : blackKeyW;
            g.setColour(juce::Colour(0xff4a4e58));
            g.drawLine(cutX, wBot, w, wBot, 1.0f);

            // Label on the right
            float labelAreaW = w - blackKeyW;
            float labelAreaX = blackKeyW;
            float labelY = (wTop + wBot) / 2.0f;

            if (pc == 0) // C note: bold octave identifier (e.g. C3, C4)
            {
                g.setColour(isPressed ? juce::Colours::white : juce::Colour(0xff18191c));
                g.setFont(juce::FontOptions(std::clamp(whPerOct - 4.0f, 9.0f, 13.0f)).withStyle("bold"));
                g.drawText("C" + juce::String(oct),
                           (int)labelAreaX, (int)(labelY - 8.0f), (int)labelAreaW, 16,
                           juce::Justification::centred);
            }
            else if (whPerOct >= 15.0f)
            {
                g.setColour(isPressed ? juce::Colours::white : juce::Colour(0xff606674));
                g.setFont(juce::FontOptions(std::clamp(whPerOct - 7.0f, 8.0f, 10.5f)));
                g.drawText(ScaleDefinitions::getPitchName(pc),
                           (int)labelAreaX, (int)(labelY - 7.0f), (int)labelAreaW, 14,
                           juce::Justification::centred);
            }
        }
    }

    // Pass 2: Render Black Keys (Elevated, 65% width, centered on white seams, 2px rounded corners)
    for (int oct = minOct; oct <= maxOct + 1; ++oct)
    {
        int octCPitch = (oct + 1) * 12;
        float octBotY = pitchToY(octCPitch) + noteHeight;

        for (int bPc : BLACK_KEY_PCS)
        {
            int seamIdx = BLACK_SEAM_MAP[bPc];
            float seamY = octBotY - (float)seamIdx * whPerOct;
            float bkTop = std::round(seamY - bkH / 2.0f);
            float bkBot = bkTop + bkH;
            int p = octCPitch + bPc;

            if (bkTop > (float)clip.getBottom() || bkBot < (float)clip.getY() || p < 0 || p > 127)
                continue;

            bool isPressed = (p == lastPreviewPitch);

            juce::Rectangle<float> bkRect(0.0f, bkTop, blackKeyW, bkH);

            // Black key fill
            g.setColour(isPressed ? CubaseTheme::accentBlue() : juce::Colour(0xff181a1e));
            g.fillRoundedRectangle(bkRect, 2.0f);

            // Black key outline
            g.setColour(isPressed ? CubaseTheme::accentBlue().brighter(0.3f) : juce::Colour(0xff2d3036));
            g.drawRoundedRectangle(bkRect, 2.0f, 1.0f);

            // Right edge 3D highlight
            if (!isPressed)
            {
                g.setColour(juce::Colour(0x30ffffff));
                g.drawLine(blackKeyW - 1.0f, bkTop + 1.0f, blackKeyW - 1.0f, bkBot - 1.0f, 1.0f);
            }
        }
    }

    // Right border
    g.setColour(CubaseTheme::borderBright());
    g.drawLine(w - 1.0f, 0.0f, w - 1.0f, (float)getHeight(), 1.0f);
}

void PianoKeyboardComponent::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    if (onScrollRequested)
    {
        onScrollRequested(wheel.deltaY);
    }
    else
    {
        Component::mouseWheelMove(e, wheel);
    }
}

} // namespace Harmonizer
