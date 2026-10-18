#include "ModalFunctionEngine.h"

namespace Harmonizer
{

static inline juce::String u(const char8_t* s)
{
    return juce::String::fromUTF8(reinterpret_cast<const char*>(s));
}

static inline juce::String u(const char* s)
{
    return juce::String::fromUTF8(s);
}

enum class ChurchModeType
{
    Ionian,      // 自然大调 (传统功能体系)
    Aeolian,     // 自然小调 (传统小调体系)
    Dorian,      // 中古多利亚
    Phrygian,    // 中古弗里吉亚
    Lydian,      // 中古利底亚
    Mixolydian,  // 中古混合利底亚
    Locrian,     // 中古洛克里亚
    Other
};

static ChurchModeType identifyChurchMode(const juce::String& scaleName)
{
    auto lower = scaleName.toLowerCase();
    if (lower.contains("dorian")) return ChurchModeType::Dorian;
    if (lower.contains("phrygian")) return ChurchModeType::Phrygian;
    if (lower.contains("mixolydian")) return ChurchModeType::Mixolydian;
    if (lower.contains("lydian")) return ChurchModeType::Lydian;
    if (lower.contains("aeolian") || lower.contains("natural minor")) return ChurchModeType::Aeolian;
    if (lower.contains("locrian")) return ChurchModeType::Locrian;
    if (lower.contains("ionian") || lower.contains("natural major") || lower.contains("major")) return ChurchModeType::Ionian;
    return ChurchModeType::Other;
}

bool ModalFunctionEngine::isTraditionalMajorMinor(const juce::String& scaleName)
{
    auto t = identifyChurchMode(scaleName);
    return t == ChurchModeType::Ionian || t == ChurchModeType::Aeolian;
}

ModalNoteInfo ModalFunctionEngine::lookup(const juce::String& scaleName, int semitone)
{
    int semi = ((semitone % 12) + 12) % 12;
    auto modeType = identifyChurchMode(scaleName);

    switch (modeType)
    {
        //==============================================================================
        // 1. Ionian (自然大调体系：激活传统 T-S-D 与传统音名)
        //==============================================================================
        case ChurchModeType::Ionian:
        {
            switch (semi)
            {
                case 0:  return { u(u8"1"),          u(u8"主功能 (Tonic)"),             u(u8"稳定 (无倾向)"),             u(u8"协和基底 (Tonic)") };
                case 1:  return { u(u8"♭2"),         u(u8"那不勒斯音 (♭II)"),           u(u8"下行半音至 1 (强倾向)"),      u(u8"小二度张力 (T♭9)") };
                case 2:  return { u(u8"2"),          u(u8"下属功能 (上主音)"),          u(u8"级进至 1 或 3"),             u(u8"纯净九度延伸 (T9)") };
                case 3:  return { u(u8"♭3"),         u(u8"借用音 (同主音小调)"),        u(u8"下行半音至 2"),              u(u8"蓝调小三度 (Blue 3rd)") };
                case 4:  return { u(u8"3"),          u(u8"主功能 (中音)"),              u(u8"稳定 (性质音)"),             u(u8"大三度骨架 (CT 3)") };
                case 5:  return { u(u8"4"),          u(u8"下属功能 (下属音)"),          u(u8"下行半音至 3 (强倾向)"),      u(u8"避免音 (Avoid Note)") };
                case 6:  return { u(u8"♯4"),         u(u8"利底亚借用音"),               u(u8"上行半音至 5"),              u(u8"增四度张力 (T♯11)") };
                case 7:  return { u(u8"5"),          u(u8"属功能 (属音)"),              u(u8"下行五度至 1"),              u(u8"纯五度支柱 (CT 5)") };
                case 8:  return { u(u8"♭6"),         u(u8"借用音 (小下属音)"),          u(u8"下行半音至 5 (强倾向)"),      u(u8"小六度张力 (T♭13)") };
                case 9:  return { u(u8"6"),          u(u8"下属功能 (下中音)"),          u(u8"下行二度至 5"),              u(u8"大六度延伸 (T13)") };
                case 10: return { u(u8"♭7"),         u(u8"借用音 (副属音)"),            u(u8"下行二度至 6"),              u(u8"小七度属色彩 (♭7)") };
                case 11: return { u(u8"7 (导音)"),   u(u8"属功能 (导音)"),              u(u8"上行半音至 1 (强倾向)"),      u(u8"大七度导音 (Maj7)") };
                default: break;
            }
            break;
        }

        //==============================================================================
        // 2. Aeolian (自然小调体系)
        //==============================================================================
        case ChurchModeType::Aeolian:
        {
            switch (semi)
            {
                case 0:  return { u(u8"1"),          u(u8"主功能 (Tonic)"),             u(u8"稳定 (无倾向)"),             u(u8"小调基底 (Tonic)") };
                case 1:  return { u(u8"♭2"),         u(u8"弗里吉亚借用音"),             u(u8"下行半音至 1 (强倾向)"),      u(u8"小二度张力 (T♭9)") };
                case 2:  return { u(u8"2"),          u(u8"下属功能 (上主音)"),          u(u8"级进至 1 或 ♭3"),            u(u8"纯净九度延伸 (T9)") };
                case 3:  return { u(u8"♭3"),         u(u8"主功能 (中音)"),              u(u8"稳定 (性质音)"),             u(u8"小三度骨架 (CT ♭3)") };
                case 4:  return { u(u8"♮3"),         u(u8"皮卡第三度 (Picardy)"),       u(u8"稳定 (大三度终止)"),         u(u8"大三度色彩 (CT 3)") };
                case 5:  return { u(u8"4"),          u(u8"下属功能 (下属音)"),          u(u8"下行二度至 ♭3"),             u(u8"下属和声支柱 (iv)") };
                case 6:  return { u(u8"♭5"),         u(u8"蓝调变化音"),                 u(u8"趋向 4 或 5"),               u(u8"减五度蓝调音 (Blue Note)") };
                case 7:  return { u(u8"5"),          u(u8"属功能 (属音)"),              u(u8"下行五度至 1"),              u(u8"纯五度支柱 (CT 5)") };
                case 8:  return { u(u8"♭6 (特征音)"),u(u8"下属功能 (下中音)"),          u(u8"下行半音至 5 (强倾向)"),      u(u8"小六度特征音 (♭6)") };
                case 9:  return { u(u8"♮6"),         u(u8"多利亚借用音"),               u(u8"级进至 5 或 ♭7"),            u(u8"大六度延伸 (T13)") };
                case 10: return { u(u8"♭7"),         u(u8"副属功能 (自然下主音)"),      u(u8"上行全音至 1"),              u(u8"小七度自然音 (♭7)") };
                case 11: return { u(u8"♮7 (导音)"),  u(u8"属功能 (和声导音)"),          u(u8"上行半音至 1 (强倾向)"),      u(u8"大七度导音 (V7)") };
                default: break;
            }
            break;
        }

        //==============================================================================
        // 3. Dorian (中古多利亚调式：特征音 ♮6)
        //==============================================================================
        case ChurchModeType::Dorian:
        {
            switch (semi)
            {
                case 0:  return { u(u8"1"),          u(u8"调式主音 (Finalis)"),         u(u8"稳定 (无倾向)"),             u(u8"协和基底 (Tonic)") };
                case 1:  return { u(u8"♭2"),         u(u8"调式借用音"),                 u(u8"下行半音至 1 (强倾向)"),      u(u8"小二度张力 (T♭9)") };
                case 2:  return { u(u8"2"),          u(u8"调内自然音"),                 u(u8"级进至 1 或 ♭3"),            u(u8"纯净九度延伸 (T9)") };
                case 3:  return { u(u8"♭3"),         u(u8"调式性质音 (小三度)"),        u(u8"稳定 (性质音)"),             u(u8"小三度骨架 (CT ♭3)") };
                case 4:  return { u(u8"♮3"),         u(u8"皮卡第三度"),                 u(u8"稳定 (大三度终止)"),         u(u8"大三度色彩 (CT 3)") };
                case 5:  return { u(u8"4"),          u(u8"调内骨架音 (四度)"),          u(u8"级进至 ♭3 或 5"),            u(u8"四度延伸 (T11)") };
                case 6:  return { u(u8"♯4"),         u(u8"调式变化音"),                 u(u8"趋向 4 或 5"),               u(u8"三全音张力 (♯4/♭5)") };
                case 7:  return { u(u8"5"),          u(u8"调式支柱音 (五度)"),          u(u8"下行五度至 1"),              u(u8"纯五度支柱 (CT 5)") };
                case 8:  return { u(u8"♭6"),         u(u8"调式变化音"),                 u(u8"下行半音至 5"),              u(u8"小六度张力 (T♭13)") };
                case 9:  return { u(u8"♮6 (特征音)"),u(u8"核心特征音 (自然大六度)"),    u(u8"下行二度至 5"),              u(u8"特征大六度色彩 (T13)") };
                case 10: return { u(u8"♭7"),         u(u8"调内自然音 (小七度)"),        u(u8"上行全音至 1"),              u(u8"小七度色彩 (♭7)") };
                case 11: return { u(u8"♮7"),         u(u8"旋律导音"),                   u(u8"上行半音至 1 (强倾向)"),      u(u8"大七度导音张力") };
                default: break;
            }
            break;
        }

        //==============================================================================
        // 4. Phrygian (中古弗里吉亚调式：特征音 ♭2)
        //==============================================================================
        case ChurchModeType::Phrygian:
        {
            switch (semi)
            {
                case 0:  return { u(u8"1"),          u(u8"调式主音 (Finalis)"),         u(u8"稳定 (无倾向)"),             u(u8"协和基底 (Tonic)") };
                case 1:  return { u(u8"♭2 (特征音)"),u(u8"核心特征音 (小二度)"),        u(u8"下行半音至 1 (强倾向)"),      u(u8"特征小二度色彩 (♭9)") };
                case 2:  return { u(u8"♮2"),         u(u8"调式借用音"),                 u(u8"级进至 ♭3"),                 u(u8"大二度延伸 (T9)") };
                case 3:  return { u(u8"♭3"),         u(u8"调式性质音 (小三度)"),        u(u8"稳定 (性质音)"),             u(u8"小三度骨架 (CT ♭3)") };
                case 4:  return { u(u8"♮3"),         u(u8"弗里吉亚大三度"),             u(u8"稳定 (大调释放)"),           u(u8"大三度色彩 (CT 3)") };
                case 5:  return { u(u8"4"),          u(u8"调内骨架音 (四度)"),          u(u8"级进至 ♭3 或 5"),            u(u8"四度骨架支柱") };
                case 6:  return { u(u8"♯4"),         u(u8"调式变化音"),                 u(u8"趋向 5"),                    u(u8"三全音张力 (♯4)") };
                case 7:  return { u(u8"5"),          u(u8"调式支柱音 (五度)"),          u(u8"下行五度至 1"),              u(u8"纯五度支柱 (CT 5)") };
                case 8:  return { u(u8"♭6"),         u(u8"调内自然音 (小六度)"),        u(u8"下行半音至 5"),              u(u8"小六度延伸 (♭13)") };
                case 9:  return { u(u8"♮6"),         u(u8"调式借用音"),                 u(u8"级进至 ♭7"),                 u(u8"大六度延伸 (T13)") };
                case 10: return { u(u8"♭7"),         u(u8"调内自然音 (小七度)"),        u(u8"上行全音至 1"),              u(u8"小七度色彩 (♭7)") };
                case 11: return { u(u8"♮7"),         u(u8"和声导音"),                   u(u8"上行半音至 1 (强倾向)"),      u(u8"大七度导音张力") };
                default: break;
            }
            break;
        }

        //==============================================================================
        // 5. Lydian (中古利底亚调式：特征音 ♯4)
        //==============================================================================
        case ChurchModeType::Lydian:
        {
            switch (semi)
            {
                case 0:  return { u(u8"1"),          u(u8"调式主音 (Finalis)"),         u(u8"稳定 (无倾向)"),             u(u8"协和基底 (Tonic)") };
                case 1:  return { u(u8"♭2"),         u(u8"调式借用音"),                 u(u8"下行半音至 1"),              u(u8"小二度张力 (♭9)") };
                case 2:  return { u(u8"2"),          u(u8"调内自然音"),                 u(u8"级进至 1 或 3"),             u(u8"纯净九度延伸 (T9)") };
                case 3:  return { u(u8"♭3"),         u(u8"调式借用音"),                 u(u8"下行半音至 2"),              u(u8"小三度色彩 (♭3)") };
                case 4:  return { u(u8"3"),          u(u8"调式性质音 (大三度)"),        u(u8"稳定 (性质音)"),             u(u8"大三度骨架 (CT 3)") };
                case 5:  return { u(u8"♮4"),         u(u8"自然大调借用音"),             u(u8"下行半音至 3"),              u(u8"避免音 (Avoid Note)") };
                case 6:  return { u(u8"♯4 (特征音)"),u(u8"核心特征音 (增四度)"),        u(u8"上行半音至 5"),              u(u8"特征增四度色彩 (T♯11)") };
                case 7:  return { u(u8"5"),          u(u8"调式支柱音 (五度)"),          u(u8"下行五度至 1"),              u(u8"纯五度支柱 (CT 5)") };
                case 8:  return { u(u8"♭6"),         u(u8"调式借用音"),                 u(u8"下行半音至 5"),              u(u8"小六度张力 (♭13)") };
                case 9:  return { u(u8"6"),          u(u8"调内自然音 (大六度)"),        u(u8"下行二度至 5"),              u(u8"大六度延伸 (T13)") };
                case 10: return { u(u8"♭7"),         u(u8"调式借用音"),                 u(u8"下行二度至 6"),              u(u8"小七度色彩 (♭7)") };
                case 11: return { u(u8"7"),          u(u8"调内自然音 (大七度)"),        u(u8"上行半音至 1 (强倾向)"),      u(u8"大七度色彩 (Maj7)") };
                default: break;
            }
            break;
        }

        //==============================================================================
        // 6. Mixolydian (中古混合利底亚调式：特征音 ♭7)
        //==============================================================================
        case ChurchModeType::Mixolydian:
        {
            switch (semi)
            {
                case 0:  return { u(u8"1"),          u(u8"调式主音 (Finalis)"),         u(u8"稳定 (无倾向)"),             u(u8"协和基底 (Tonic)") };
                case 1:  return { u(u8"♭2"),         u(u8"调式借用音"),                 u(u8"下行半音至 1"),              u(u8"小二度张力 (♭9)") };
                case 2:  return { u(u8"2"),          u(u8"调内自然音"),                 u(u8"级进至 1 或 3"),             u(u8"纯净九度延伸 (T9)") };
                case 3:  return { u(u8"♭3"),         u(u8"蓝调借用音"),                 u(u8"趋向 3"),                    u(u8"蓝调小三度 (Blue 3rd)") };
                case 4:  return { u(u8"3"),          u(u8"调式性质音 (大三度)"),        u(u8"稳定 (性质音)"),             u(u8"大三度骨架 (CT 3)") };
                case 5:  return { u(u8"4"),          u(u8"调内骨架音 (四度)"),          u(u8"下行半音至 3"),              u(u8"四度挂留音 (sus4)") };
                case 6:  return { u(u8"♯4"),         u(u8"调式借用音"),                 u(u8"上行半音至 5"),              u(u8"增四度张力 (♯4)") };
                case 7:  return { u(u8"5"),          u(u8"调式支柱音 (五度)"),          u(u8"下行五度至 1"),              u(u8"纯五度支柱 (CT 5)") };
                case 8:  return { u(u8"♭6"),         u(u8"调式借用音"),                 u(u8"下行半音至 5"),              u(u8"小六度张力 (♭13)") };
                case 9:  return { u(u8"6"),          u(u8"调内自然音 (大六度)"),        u(u8"级进至 5 或 ♭7"),            u(u8"大六度延伸 (T13)") };
                case 10: return { u(u8"♭7 (特征音)"),u(u8"核心特征音 (小七度)"),        u(u8"下行二度至 6"),              u(u8"特征小七度色彩 (♭7)") };
                case 11: return { u(u8"♮7"),         u(u8"调式借用音"),                 u(u8"上行半音至 1 (强倾向)"),      u(u8"大七度导音张力") };
                default: break;
            }
            break;
        }

        //==============================================================================
        // 7. Locrian (中古洛克里亚调式：特征音 ♭5, ♭2)
        //==============================================================================
        case ChurchModeType::Locrian:
        {
            switch (semi)
            {
                case 0:  return { u(u8"1"),          u(u8"调式主音 (Finalis)"),         u(u8"微弱稳定 (中心音)"),         u(u8"减和弦基底 (m7♭5)") };
                case 1:  return { u(u8"♭2 (特征音)"),u(u8"核心特征音 (小二度)"),        u(u8"下行半音至 1 (强倾向)"),      u(u8"特征小二度张力 (♭9)") };
                case 2:  return { u(u8"♮2"),         u(u8"调式借用音"),                 u(u8"级进至 ♭3"),                 u(u8"大二度延伸 (9)") };
                case 3:  return { u(u8"♭3"),         u(u8"调式性质音 (小三度)"),        u(u8"稳定 (性质音)"),             u(u8"小三度骨架 (♭3)") };
                case 4:  return { u(u8"♮3"),         u(u8"调式借用音"),                 u(u8"趋向 4"),                    u(u8"大三度色彩 (3)") };
                case 5:  return { u(u8"4"),          u(u8"调内骨架音 (四度)"),          u(u8"相对稳定 (四度支柱)"),       u(u8"四度协和音 (11)") };
                case 6:  return { u(u8"♭5 (特征音)"),u(u8"核心特征音 (减五度)"),        u(u8"下行半音至 4"),              u(u8"减五度三全音 (♭5)") };
                case 7:  return { u(u8"♮5"),         u(u8"调式借用音"),                 u(u8"下行五度至 1"),              u(u8"纯五度支柱 (5)") };
                case 8:  return { u(u8"♭6"),         u(u8"调内自然音 (小六度)"),        u(u8"下行半音至 ♭5"),             u(u8"小六度延伸 (♭13)") };
                case 9:  return { u(u8"♮6"),         u(u8"调式借用音"),                 u(u8"趋向 ♭7"),                   u(u8"大六度延伸 (13)") };
                case 10: return { u(u8"♭7"),         u(u8"调内自然音 (小七度)"),        u(u8"上行全音至 1"),              u(u8"小七度色彩 (♭7)") };
                case 11: return { u(u8"♮7"),         u(u8"调式借用音"),                 u(u8"上行半音至 1 (强倾向)"),      u(u8"大七度导音张力") };
                default: break;
            }
            break;
        }

        //==============================================================================
        // 8. Other (通用回退音阶规则)
        //==============================================================================
        default:
        {
            static const char* defDegrees[12] = { "1", "♭2", "2", "♭3", "3", "4", "♯4/♭5", "5", "♭6", "6", "♭7", "7" };
            switch (semi)
            {
                case 0:  return { u(defDegrees[semi]), u(u8"中心主音"),                 u(u8"稳定 (无倾向)"),             u(u8"协和基底 (Tonic)") };
                case 1:  return { u(defDegrees[semi]), u(u8"小二度音"),                 u(u8"下行半音至 1"),              u(u8"小二度张力 (♭9)") };
                case 2:  return { u(defDegrees[semi]), u(u8"大二度音"),                 u(u8"级进至 1 或 3"),             u(u8"九度延伸 (9)") };
                case 3:  return { u(defDegrees[semi]), u(u8"小三度音"),                 u(u8"稳定 (小调性质)"),           u(u8"小三度骨架 (♭3)") };
                case 4:  return { u(defDegrees[semi]), u(u8"大三度音"),                 u(u8"稳定 (大调性质)"),           u(u8"大三度骨架 (3)") };
                case 5:  return { u(defDegrees[semi]), u(u8"纯四度音"),                 u(u8"趋向 3 或 5"),               u(u8"四度挂留音 (11)") };
                case 6:  return { u(defDegrees[semi]), u(u8"增四/减五度"),              u(u8"趋向 4 或 5"),               u(u8"三全音张力 (♯4/♭5)") };
                case 7:  return { u(defDegrees[semi]), u(u8"纯五度音"),                 u(u8"下行五度至 1"),              u(u8"纯五度支柱 (5)") };
                case 8:  return { u(defDegrees[semi]), u(u8"小六度音"),                 u(u8"下行半音至 5"),              u(u8"小六度张力 (♭13)") };
                case 9:  return { u(defDegrees[semi]), u(u8"大六度音"),                 u(u8"下行二度至 5"),              u(u8"大六度延伸 (13)") };
                case 10: return { u(defDegrees[semi]), u(u8"小七度音"),                 u(u8"上行全音至 1"),              u(u8"小七度色彩 (7)") };
                case 11: return { u(defDegrees[semi]), u(u8"大七度音"),                 u(u8"上行半音至 1 (强倾向)"),      u(u8"大七度导音 (Maj7)") };
                default: break;
            }
            break;
        }
    }

    return { u(u8"1"), u(u8"调式自然音"), u(u8"稳定 (无倾向)"), u(u8"和声骨架") };
}

} // namespace Harmonizer
