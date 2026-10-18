#pragma once

#include <juce_core/juce_core.h>

namespace Harmonizer
{

/**
 * @brief 音符在上下文调式中的 4 维高信息密度属性（严格去情感化、简短精炼、客观标准乐理）
 * 
 * 1. degree: 调式音级（客观音级与特征音标识，如 "1", "♭2 (特征音)", "7 (导音)"）
 * 2. functionalRole: 调式功能（大小调严格对应 T-S-D 与传统音名；中古调式对应 Finalis 中心与特征和弦）
 * 3. motionTendency: 运动倾向（客观几何运动向量与音程倾向，无主观修饰词，如 "稳定 (无倾向)", "下行半音至 1 (强倾向)"）
 * 4. colorUsage: 色彩定位（客观声学和声色彩与张力标识，如 "协和基底 (Tonic)", "特征大六度色彩 (T13)", "避免音 (Avoid Note)"）
 */
struct ModalNoteInfo
{
    juce::String degree;           // 调式音级 (如 "1", "♭2 (特征音)", "7 (导音)")
    juce::String functionalRole;   // 调式功能 (如 "主功能 (Tonic)", "核心特征音 (自然大六度)")
    juce::String motionTendency;   // 运动倾向 (如 "稳定 (无倾向)", "下行半音至 1 (强倾向)")
    juce::String colorUsage;       // 色彩定位 (如 "协和基底 (Tonic)", "特征大六度色彩 (T13)")
};

class ModalFunctionEngine
{
public:
    /**
     * 根据当前调式名称与相对半音差（0-11）查询多维音乐学属性
     * @param scaleName 调式名称（如 Ionian, Aeolian, Dorian, Phrygian, Lydian, Mixolydian, Locrian 等）
     * @param semitone  选中音符与调式根音的半音差: (notePitch - tonicPc + 144) % 12
     */
    static ModalNoteInfo lookup(const juce::String& scaleName, int semitone);

    /** 判断是否为传统自然大小调 (Ionian / Aeolian) */
    static bool isTraditionalMajorMinor(const juce::String& scaleName);
};

} // namespace Harmonizer
