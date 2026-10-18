/**
 * ChordRecognizer.hpp
 * 逆向还原自 ChordieApp v1.2.6 (x86_64, JUCE C++ Audio Application)
 *
 * 核心算法汇编映射：
 * - 0x1401825d0: 音符排序与去重 (std::unique), 相对最低音的模12音程提取
 * - 0x140180c40: 核心循环移位匹配 (std::rotate) 与转位检测
 * - 0x140183230: 音程集合比对 (memcmp)
 * - 0x140181340: 根音/低音计算与斜杠和弦格式化输出
 * - 0x140181710 - 0x140181a50: 复合双和弦与子集分解 (Polychord / Upper Structure)
 */

#pragma once

#include <string>
#include <vector>
#include <algorithm>
#include <array>
#include <sstream>

namespace ChordieReverse {

enum class Interval : int {
    UNIS = 0,
    min2 = 1,
    Maj2 = 2,
    min3 = 3,
    Maj3 = 4,
    Per4 = 5,
    TTone = 6,
    Per5 = 7,
    min6 = 8,
    Maj6 = 9,
    min7 = 10,
    Maj7 = 11,
    OCTAVE = 12
};

enum class ChordQuality : int {
    MAJOR = 0,
    MINOR = 1,
    DOMINANT = 2,
    DIMINISHED = 3,
    AUGMENTED = 4,
    SUSPENDED = 5,
    POWER = 6,
    POLYCHORD = 7
};

struct ChordTemplate {
    std::string name;             // 简记符号 (如 "", "m", "7", "maj7")
    std::string alternate_name;   // 完整名称 (如 "Major Triad", "Minor 7th")
    std::vector<int> intervals;   // 相对根音的标准半音音程集 (升序排列)
    ChordQuality quality;         // 和弦性质
    int root_offset = 0;
    bool is_common = true;
};

struct ChordMatchResult {
    std::string root_name;
    std::string bass_name;
    int root_pitch_class = 0;
    int bass_pitch_class = 0;
    int inversion = 0;
    bool is_slash_chord = false;
    std::string display_name;
    std::string alternate_display_name;
    std::vector<int> matched_notes_midi;
    std::vector<int> intervals;
    std::string chord_symbol;
    std::string polychord_top;
};

class ChordRecognizer {
public:
    ChordRecognizer() {
        initDatabase();
    }

    static std::string pitchClassToName(int pitchClass, bool preferFlat = false) {
        static const char* sharpNames[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        static const char* flatNames[12]  = { "C", "Db", "D", "Eb", "E", "F", "Gb", "G", "Ab", "A", "Bb", "B" };
        int pc = (pitchClass % 12 + 12) % 12;
        return preferFlat ? flatNames[pc] : sharpNames[pc];
    }

    ChordMatchResult identify(const std::vector<int>& midiNotes, bool preferFlat = false) const {
        ChordMatchResult res;
        if (midiNotes.empty()) return res;

        // 1. 对应 RVA 0x1825d0: 音符升序排序并剔除八度重复音
        std::vector<int> sortedNotes = midiNotes;
        std::sort(sortedNotes.begin(), sortedNotes.end());

        int bassMidi = sortedNotes[0];
        int bassPc = (bassMidi % 12 + 12) % 12;

        std::vector<int> uniquePcs;
        for (int n : sortedNotes) {
            int pc = (n % 12 + 12) % 12;
            if (std::find(uniquePcs.begin(), uniquePcs.end(), pc) == uniquePcs.end()) {
                uniquePcs.push_back(pc);
            }
        }

        res.matched_notes_midi = sortedNotes;
        int numNotes = static_cast<int>(uniquePcs.size());

        // 单音判定 (对应 RVA 0x1813d3)
        if (numNotes == 1) {
            std::string rootName = pitchClassToName(bassPc, preferFlat);
            res.root_name = rootName;
            res.bass_name = rootName;
            res.root_pitch_class = bassPc;
            res.bass_pitch_class = bassPc;
            res.inversion = 0;
            res.is_slash_chord = false;
            res.display_name = rootName;
            res.alternate_display_name = rootName + " (Single Note)";
            res.intervals = { 0 };
            return res;
        }

        // 2. 循环移位转位匹配算法 (对应 RVA 0x180c40 / 0x180e60)
        for (int inv = 0; inv < numNotes; ++inv) {
            int rootPc = uniquePcs[inv];

            std::vector<int> currentIntervals;
            currentIntervals.reserve(numNotes);
            for (int pc : uniquePcs) {
                currentIntervals.push_back((pc - rootPc + 12) % 12);
            }
            std::sort(currentIntervals.begin(), currentIntervals.end());

            for (const auto& tmpl : m_templates) {
                if (currentIntervals == tmpl.intervals) {
                    std::string rootName = pitchClassToName(rootPc, preferFlat);
                    std::string bassName = pitchClassToName(bassPc, preferFlat);
                    bool isSlash = (rootPc != bassPc);

                    res.root_name = rootName;
                    res.bass_name = bassName;
                    res.root_pitch_class = rootPc;
                    res.bass_pitch_class = bassPc;
                    res.inversion = inv;
                    res.is_slash_chord = isSlash;
                    res.intervals = currentIntervals;
                    res.chord_symbol = tmpl.name;

                    std::string base = rootName + tmpl.name;
                    res.display_name = isSlash ? (base + "/" + bassName) : base;

                    std::string alt = rootName + " " + tmpl.alternate_name;
                    res.alternate_display_name = isSlash ? (alt + "/" + bassName) : alt;
                    return res;
                }
            }
        }

        // 3. 省略纯五度 (omit 5) 容错匹配 (对应 RVA 0x1814cf)
        for (int inv = 0; inv < numNotes; ++inv) {
            int rootPc = uniquePcs[inv];
            std::vector<int> currentIntervals;
            for (int pc : uniquePcs) {
                currentIntervals.push_back((pc - rootPc + 12) % 12);
            }
            std::sort(currentIntervals.begin(), currentIntervals.end());

            for (const auto& tmpl : m_templates) {
                auto itFive = std::find(tmpl.intervals.begin(), tmpl.intervals.end(), 7);
                if (itFive != tmpl.intervals.end()) {
                    std::vector<int> withoutFive = tmpl.intervals;
                    withoutFive.erase(std::find(withoutFive.begin(), withoutFive.end(), 7));
                    if (currentIntervals == withoutFive) {
                        std::string rootName = pitchClassToName(rootPc, preferFlat);
                        std::string bassName = pitchClassToName(bassPc, preferFlat);
                        bool isSlash = (rootPc != bassPc);

                        res.root_name = rootName;
                        res.bass_name = bassName;
                        res.root_pitch_class = rootPc;
                        res.bass_pitch_class = bassPc;
                        res.inversion = inv;
                        res.is_slash_chord = isSlash;
                        res.intervals = currentIntervals;
                        res.chord_symbol = tmpl.name + "(no5)";

                        std::string base = rootName + tmpl.name + "(no5)";
                        res.display_name = isSlash ? (base + "/" + bassName) : base;
                        res.alternate_display_name = rootName + " " + tmpl.alternate_name + " (omit 5)";
                        return res;
                    }
                }
            }
        }

        // 4. 双和弦 (Polychord) 与复合结构分解 (对应 RVA 0x181710 - 0x181a50)
        if (numNotes >= 5) {
            struct PolyCandidate {
                int score;
                ChordMatchResult lower;
                ChordMatchResult upper;
            };
            std::vector<PolyCandidate> candidates;

            for (int split = 2; split <= numNotes - 2; ++split) {
                std::vector<int> lowerPcs(uniquePcs.begin(), uniquePcs.begin() + split);
                std::vector<int> upperPcs(uniquePcs.begin() + split, uniquePcs.end());

                if (lowerPcs.size() >= 2 && upperPcs.size() >= 3) {
                    ChordMatchResult lowerMatch = identify(lowerPcs, preferFlat);
                    ChordMatchResult upperMatch = identify(upperPcs, preferFlat);

                    if (lowerMatch.chord_symbol != "cluster" && upperMatch.chord_symbol != "cluster") {
                        int score = 0;
                        if (lowerMatch.display_name.find("(no5)") == std::string::npos) score += 10;
                        if (upperMatch.display_name.find("(no5)") == std::string::npos) score += 10;
                        if (lowerMatch.template_info.quality == ChordQuality::MAJOR || lowerMatch.template_info.quality == ChordQuality::MINOR) score += 5;
                        if (upperMatch.template_info.quality == ChordQuality::MAJOR || upperMatch.template_info.quality == ChordQuality::MINOR) score += 5;
                        if (lowerPcs.size() == 3 && upperPcs.size() == 3) score += 5;
                        candidates.push_back({ score, lowerMatch, upperMatch });
                    }
                }
            }

            if (!candidates.empty()) {
                std::sort(candidates.begin(), candidates.end(), [](const PolyCandidate& a, const PolyCandidate& b) {
                    return a.score > b.score;
                });
                const auto& best = candidates.front();
                res.root_name = best.lower.root_name;
                res.bass_name = best.lower.bass_name;
                res.root_pitch_class = best.lower.root_pitch_class;
                res.bass_pitch_class = best.lower.bass_pitch_class;
                res.inversion = 0;
                res.is_slash_chord = true;
                res.display_name = best.upper.display_name + " | " + best.lower.display_name;
                res.alternate_display_name = best.upper.alternate_display_name + " over " + best.lower.alternate_display_name;
                res.polychord_top = best.upper.display_name;
                for (int pc : uniquePcs) {
                    res.intervals.push_back((pc - bassPc + 12) % 12);
                }
                std::sort(res.intervals.begin(), res.intervals.end());
                return res;
            }
        }

        // 5. 音簇 (Tone Cluster)
        std::string rootName = pitchClassToName(bassPc, preferFlat);
        res.root_name = rootName;
        res.bass_name = rootName;
        res.root_pitch_class = bassPc;
        res.bass_pitch_class = bassPc;
        res.inversion = 0;
        res.is_slash_chord = false;
        res.chord_symbol = "cluster";
        res.display_name = rootName + " Cluster";
        res.alternate_display_name = "Tone Cluster on " + rootName;
        for (int pc : uniquePcs) {
            res.intervals.push_back((pc - bassPc + 12) % 12);
        }
        std::sort(res.intervals.begin(), res.intervals.end());
        return res;
    }

private:
    void initDatabase() {
        m_templates = {
            // Power & Octave
            { "5", "Power Chord", { 0, 7 }, ChordQuality::POWER },
            { "8ve", "Octave", { 0, 12 }, ChordQuality::MAJOR },

            // Triads
            { "", "Major Triad", { 0, 4, 7 }, ChordQuality::MAJOR },
            { "m", "Minor Triad", { 0, 3, 7 }, ChordQuality::MINOR },
            { "dim", "Diminished Triad", { 0, 3, 6 }, ChordQuality::DIMINISHED },
            { "aug", "Augmented Triad", { 0, 4, 8 }, ChordQuality::AUGMENTED },
            { "sus4", "Suspended 4th", { 0, 5, 7 }, ChordQuality::SUSPENDED },
            { "sus2", "Suspended 2nd", { 0, 2, 7 }, ChordQuality::SUSPENDED },
            { "b5", "Flat 5 Triad", { 0, 4, 6 }, ChordQuality::MAJOR },

            // Add Chords
            { "add9", "Added 9th", { 0, 2, 4, 7 }, ChordQuality::MAJOR },
            { "m(add9)", "Minor Added 9th", { 0, 2, 3, 7 }, ChordQuality::MINOR },
            { "add11", "Added 11th", { 0, 4, 5, 7 }, ChordQuality::MAJOR },
            { "add4", "Added 4th", { 0, 4, 5, 7 }, ChordQuality::MAJOR },

            // 6th Chords
            { "6", "Major 6th", { 0, 4, 7, 9 }, ChordQuality::MAJOR },
            { "m6", "Minor 6th", { 0, 3, 7, 9 }, ChordQuality::MINOR },
            { "6/9", "6/9 Chord", { 0, 2, 4, 7, 9 }, ChordQuality::MAJOR },
            { "m6/9", "Minor 6/9", { 0, 2, 3, 7, 9 }, ChordQuality::MINOR },

            // 7th Chords
            { "7", "Dominant 7th", { 0, 4, 7, 10 }, ChordQuality::DOMINANT },
            { "maj7", "Major 7th", { 0, 4, 7, 11 }, ChordQuality::MAJOR },
            { "m7", "Minor 7th", { 0, 3, 7, 10 }, ChordQuality::MINOR },
            { "m(maj7)", "Minor Major 7th", { 0, 3, 7, 11 }, ChordQuality::MINOR },
            { "m7b5", "Half-Diminished 7th", { 0, 3, 6, 10 }, ChordQuality::DIMINISHED },
            { "dim7", "Diminished 7th", { 0, 3, 6, 9 }, ChordQuality::DIMINISHED },
            { "aug7", "Augmented 7th", { 0, 4, 8, 10 }, ChordQuality::AUGMENTED },
            { "maj7#5", "Major 7th Sharp 5", { 0, 4, 8, 11 }, ChordQuality::AUGMENTED },
            { "7sus4", "7th Suspended 4th", { 0, 5, 7, 10 }, ChordQuality::SUSPENDED },
            { "7sus2", "7th Suspended 2nd", { 0, 2, 7, 10 }, ChordQuality::SUSPENDED },
            { "maj7sus4", "Major 7th Sus 4", { 0, 5, 7, 11 }, ChordQuality::SUSPENDED },

            // Altered 7ths
            { "7b5", "Dominant 7th Flat 5", { 0, 4, 6, 10 }, ChordQuality::DOMINANT },
            { "7#5", "Dominant 7th Sharp 5", { 0, 4, 8, 10 }, ChordQuality::DOMINANT },
            { "7#9", "Dominant 7th Sharp 9 (Hendrix)", { 0, 3, 4, 7, 10 }, ChordQuality::DOMINANT },
            { "7b9", "Dominant 7th Flat 9", { 0, 1, 4, 7, 10 }, ChordQuality::DOMINANT },
            { "7#11", "Dominant 7th Sharp 11", { 0, 4, 6, 7, 10 }, ChordQuality::DOMINANT },
            { "7b13", "Dominant 7th Flat 13", { 0, 4, 7, 8, 10 }, ChordQuality::DOMINANT },

            // 9th Chords
            { "9", "Dominant 9th", { 0, 2, 4, 7, 10 }, ChordQuality::DOMINANT },
            { "maj9", "Major 9th", { 0, 2, 4, 7, 11 }, ChordQuality::MAJOR },
            { "m9", "Minor 9th", { 0, 2, 3, 7, 10 }, ChordQuality::MINOR },
            { "m(maj9)", "Minor Major 9th", { 0, 2, 3, 7, 11 }, ChordQuality::MINOR },
            { "9sus4", "9th Suspended 4th", { 0, 2, 5, 7, 10 }, ChordQuality::SUSPENDED },
            { "9#5", "Dominant 9th Sharp 5", { 0, 2, 4, 8, 10 }, ChordQuality::DOMINANT },
            { "9b5", "Dominant 9th Flat 5", { 0, 2, 4, 6, 10 }, ChordQuality::DOMINANT },

            // 11th Chords
            { "11", "Dominant 11th", { 0, 2, 5, 7, 10 }, ChordQuality::DOMINANT },
            { "m11", "Minor 11th", { 0, 2, 3, 5, 7, 10 }, ChordQuality::MINOR },
            { "maj11", "Major 11th", { 0, 2, 4, 5, 7, 11 }, ChordQuality::MAJOR },
            { "maj9#11", "Major 9th Sharp 11", { 0, 2, 4, 6, 7, 11 }, ChordQuality::MAJOR },
            { "9#11", "Dominant 9th Sharp 11", { 0, 2, 4, 6, 7, 10 }, ChordQuality::DOMINANT },

            // 13th Chords
            { "13", "Dominant 13th", { 0, 2, 4, 7, 9, 10 }, ChordQuality::DOMINANT },
            { "maj13", "Major 13th", { 0, 2, 4, 7, 9, 11 }, ChordQuality::MAJOR },
            { "m13", "Minor 13th", { 0, 2, 3, 7, 9, 10 }, ChordQuality::MINOR },
            { "13sus4", "13th Suspended 4th", { 0, 2, 5, 7, 9, 10 }, ChordQuality::SUSPENDED },
            { "13b9", "Dominant 13th Flat 9", { 0, 1, 4, 7, 9, 10 }, ChordQuality::DOMINANT },
            { "13#9", "Dominant 13th Sharp 9", { 0, 3, 4, 7, 9, 10 }, ChordQuality::DOMINANT }
        };

        std::sort(m_templates.begin(), m_templates.end(), [](const ChordTemplate& a, const ChordTemplate& b) {
            return a.intervals.size() > b.intervals.size();
        });
    }

    std::vector<ChordTemplate> m_templates;
};

} // namespace ChordieReverse
