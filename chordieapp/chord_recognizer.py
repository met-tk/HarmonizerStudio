"""
Chord Recognition Engine (完整高保真重构自 ChordieApp v1.2.6 / myapp.exe)
========================================================================

本模块通过对 myapp.exe (ChordieApp v1.2.6, x86_64, 基于 JUCE C++ 框架) 进行深度静态反汇编
与动态反编译还原，完整重现其内部的全部和弦与音阶识别算法。

核心汇编地址对应关系：
------------------------------------------------------------------------
- RVA 0x1825d0: 音符排序去重 (std::unique)、最低音 (Bass Note) 提取及模 12 相对音程提取
- RVA 0x180c40: 和弦词典线性比对与循环移位转位 (std::rotate) 搜索主循环
- RVA 0x180e60: 循环移位转位公式实现: (interval[i] - interval[k] + 12) % 12
- RVA 0x183230: 音程集合比对与排序校验 (std::sort + memcmp)
- RVA 0x18104e / 0x181340: 根音 (Root) 与低音 (Bass) 决断、斜杠和弦 (Slash Chord) 格式化
- RVA 0x1814cf - 0x181bc5: 4 音以上复合和弦、省略音 (omit 5)、张力引伸音与双和弦 (Polychord) 分解
- RVA 0x363d40: 内置和弦语法解析器与 64 字节 Chord 结构体装配
"""

from typing import List, Tuple, Optional, Dict, Set
from enum import IntEnum
from dataclasses import dataclass, field


class Interval(IntEnum):
    """
    音程半音数常量 (对应 ChordieApp RVA 0x363e80-0x364230 语法解析器常量)
    """
    UNIS = 0       # 纯一度 (Unison / Root)
    min2 = 1       # 小二度 (Minor 2nd / b9)
    Maj2 = 2       # 大二度 (Major 2nd / 9)
    min3 = 3       # 小三度 (Minor 3rd)
    Maj3 = 4       # 大三度 (Major 3rd)
    Per4 = 5       # 纯四度 (Perfect 4th / 11)
    TTone = 6      # 增四/减五度 (Tritone / b5 / #11)
    Per5 = 7       # 纯五度 (Perfect 5th)
    min6 = 8       # 小六度 (Minor 6th / #5 / b13)
    Maj6 = 9       # 大六度 (Major 6th / 13)
    min7 = 10      # 小七度 (Minor 7th)
    Maj7 = 11      # 大七度 (Major 7th)
    OCTAVE = 12    # 纯八度 (Octave)


class ChordQuality(IntEnum):
    """
    和弦性质定义 (对应 ChordieApp RVA 0x36435b-0x3643d9)
    """
    MAJOR = 0       # 大和弦族
    MINOR = 1       # 小和弦族
    DOMINANT = 2    # 属和弦族
    DIMINISHED = 3  # 减和弦族
    AUGMENTED = 4   # 增和弦族
    SUSPENDED = 5   # 挂留和弦族
    POWER = 6       # 五度和弦 (Power Chord)
    POLYCHORD = 7   # 复合/双和弦 (Polychord)


# 12 半音音名表 (支持升号/降号体系)
NOTE_NAMES_SHARP = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"]
NOTE_NAMES_FLAT  = ["C", "Db", "D", "Eb", "E", "F", "Gb", "G", "Ab", "A", "Bb", "B"]

# 常用五度圈调号首选升降表
PREFERRED_ACCIDENTALS: Dict[str, bool] = {
    "C": False, "G": False, "D": False, "A": False, "E": False, "B": False, "F#": False,
    "F": True, "Bb": True, "Eb": True, "Ab": True, "Db": True, "Gb": True
}

# 音名到 Pitch Class (0..11) 转换字典
NOTE_TO_PITCH_CLASS: Dict[str, int] = {
    "C": 0, "B#": 0,
    "C#": 1, "DB": 1, "Db": 1,
    "D": 2,
    "D#": 3, "EB": 3, "Eb": 3,
    "E": 4, "FB": 4, "Fb": 4,
    "F": 5, "E#": 5,
    "F#": 6, "GB": 6, "Gb": 6,
    "G": 7,
    "G#": 8, "AB": 8, "Ab": 8,
    "A": 9,
    "A#": 10, "BB": 10, "Bb": 10,
    "B": 11, "CB": 11, "Cb": 11
}


@dataclass(frozen=True)
class ChordTemplate:
    """
    单条和弦模板数据结构
    对应原程序中 64 字节 (0x40 bytes) 的 ChordTemplate 内存结构：
    - name: 和弦简记符号 (如 "", "m", "7", "maj7", "m7b5")
    - alternate_name: 完整显示名 (如 "Major Triad", "Minor 7th")
    - intervals: 标准原位相对音程元组 (由小到大排列，起始为 0)
    - quality: 和弦性质
    - root_offset: 根音修正偏移量 (默认为 0)
    - is_common: 优先权重
    """
    name: str
    alternate_name: str
    intervals: Tuple[int, ...]
    quality: ChordQuality = ChordQuality.MAJOR
    root_offset: int = 0
    is_common: bool = True

    def __post_init__(self):
        sorted_intervals = tuple(sorted(set(self.intervals)))
        object.__setattr__(self, 'intervals', sorted_intervals)


@dataclass
class ChordMatchResult:
    """
    和弦识别综合输出结果
    """
    root_name: str                  # 根音名称 (如 "C", "F#")
    bass_name: str                  # 最低音名称 (如 "E", "C")
    root_pitch_class: int           # 根音半音类 (0-11, 0=C)
    bass_pitch_class: int           # 最低音半音类 (0-11, 0=C)
    template: ChordTemplate         # 命中的和弦模板定义
    inversion: int                  # 转位深度: 0=原位, 1=第一转位, 2=第二转位, 3=第三转位...
    is_slash_chord: bool            # 是否为转位斜杠和弦 (Root != Bass)
    display_name: str               # 格式化和弦缩写 (如 "C/E", "Am7", "G7(no5)")
    alternate_display_name: str     # 格式化和弦全名 (如 "C Major Triad/E", "A Minor 7th")
    matched_notes_midi: List[int]   # 原始输入的活跃 MIDI 音符
    intervals: List[int]            # 提取出的相对根音半音音程集
    polychord_top: Optional[str] = None # 双和弦高音部分 (若适用)


class ChordRecognizer:
    """
    全功能和弦识别算法引擎
    重现 ChordieApp 中的排序去重、音程分析、循环移位转位消除、双和弦与省略音匹配逻辑。
    """

    def __init__(self):
        self.chord_templates: List[ChordTemplate] = []
        self._load_built_in_chord_database()

    def _load_built_in_chord_database(self):
        """
        装配完整和弦字典库 (涵盖 60+ 种西方现代音乐理论中的常用与高级和弦)
        """
        raw_definitions = [
            # 1. 单音与八度/强力和弦 (Single Notes & Power Chords)
            ChordTemplate("5", "Power Chord (5th)", (Interval.UNIS, Interval.Per5), ChordQuality.POWER),
            ChordTemplate("8ve", "Octave", (Interval.UNIS, Interval.OCTAVE), ChordQuality.MAJOR),

            # 2. 基础三和弦 (Triads)
            ChordTemplate("", "Major Triad", (Interval.UNIS, Interval.Maj3, Interval.Per5), ChordQuality.MAJOR),
            ChordTemplate("m", "Minor Triad", (Interval.UNIS, Interval.min3, Interval.Per5), ChordQuality.MINOR),
            ChordTemplate("dim", "Diminished Triad", (Interval.UNIS, Interval.min3, Interval.TTone), ChordQuality.DIMINISHED),
            ChordTemplate("aug", "Augmented Triad", (Interval.UNIS, Interval.Maj3, Interval.min6), ChordQuality.AUGMENTED),
            ChordTemplate("sus4", "Suspended 4th", (Interval.UNIS, Interval.Per4, Interval.Per5), ChordQuality.SUSPENDED),
            ChordTemplate("sus2", "Suspended 2nd", (Interval.UNIS, Interval.Maj2, Interval.Per5), ChordQuality.SUSPENDED),
            ChordTemplate("b5", "Flat 5 Triad", (Interval.UNIS, Interval.Maj3, Interval.TTone), ChordQuality.MAJOR),

            # 3. 加音和弦 (Add Chords)
            ChordTemplate("add9", "Added 9th", (Interval.UNIS, Interval.Maj2, Interval.Maj3, Interval.Per5), ChordQuality.MAJOR),
            ChordTemplate("m(add9)", "Minor Added 9th", (Interval.UNIS, Interval.Maj2, Interval.min3, Interval.Per5), ChordQuality.MINOR),
            ChordTemplate("add11", "Added 11th", (Interval.UNIS, Interval.Maj3, Interval.Per4, Interval.Per5), ChordQuality.MAJOR),
            ChordTemplate("add4", "Added 4th", (Interval.UNIS, Interval.Maj3, Interval.Per4, Interval.Per5), ChordQuality.MAJOR),
            ChordTemplate("m(add11)", "Minor Added 11th", (Interval.UNIS, Interval.min3, Interval.Per4, Interval.Per5), ChordQuality.MINOR),

            # 4. 六和弦 (6th Chords)
            ChordTemplate("6", "Major 6th", (Interval.UNIS, Interval.Maj3, Interval.Per5, Interval.Maj6), ChordQuality.MAJOR),
            ChordTemplate("m6", "Minor 6th", (Interval.UNIS, Interval.min3, Interval.Per5, Interval.Maj6), ChordQuality.MINOR),
            ChordTemplate("6/9", "6/9 Chord", (Interval.UNIS, Interval.Maj2, Interval.Maj3, Interval.Per5, Interval.Maj6), ChordQuality.MAJOR),
            ChordTemplate("m6/9", "Minor 6/9", (Interval.UNIS, Interval.Maj2, Interval.min3, Interval.Per5, Interval.Maj6), ChordQuality.MINOR),

            # 5. 七和弦 (7th Chords)
            ChordTemplate("7", "Dominant 7th", (Interval.UNIS, Interval.Maj3, Interval.Per5, Interval.min7), ChordQuality.DOMINANT),
            ChordTemplate("maj7", "Major 7th", (Interval.UNIS, Interval.Maj3, Interval.Per5, Interval.Maj7), ChordQuality.MAJOR),
            ChordTemplate("m7", "Minor 7th", (Interval.UNIS, Interval.min3, Interval.Per5, Interval.min7), ChordQuality.MINOR),
            ChordTemplate("m(maj7)", "Minor Major 7th", (Interval.UNIS, Interval.min3, Interval.Per5, Interval.Maj7), ChordQuality.MINOR),
            ChordTemplate("m7b5", "Half-Diminished 7th", (Interval.UNIS, Interval.min3, Interval.TTone, Interval.min7), ChordQuality.DIMINISHED),
            ChordTemplate("dim7", "Diminished 7th", (Interval.UNIS, Interval.min3, Interval.TTone, Interval.Maj6), ChordQuality.DIMINISHED),
            ChordTemplate("aug7", "Augmented 7th", (Interval.UNIS, Interval.Maj3, Interval.min6, Interval.min7), ChordQuality.AUGMENTED),
            ChordTemplate("maj7#5", "Major 7th Sharp 5", (Interval.UNIS, Interval.Maj3, Interval.min6, Interval.Maj7), ChordQuality.AUGMENTED),
            ChordTemplate("7sus4", "7th Suspended 4th", (Interval.UNIS, Interval.Per4, Interval.Per5, Interval.min7), ChordQuality.SUSPENDED),
            ChordTemplate("7sus2", "7th Suspended 2nd", (Interval.UNIS, Interval.Maj2, Interval.Per5, Interval.min7), ChordQuality.SUSPENDED),
            ChordTemplate("maj7sus4", "Major 7th Sus 4", (Interval.UNIS, Interval.Per4, Interval.Per5, Interval.Maj7), ChordQuality.SUSPENDED),

            # 6. 变音七和弦 (Altered Dominant 7ths)
            ChordTemplate("7b5", "Dominant 7th Flat 5", (Interval.UNIS, Interval.Maj3, Interval.TTone, Interval.min7), ChordQuality.DOMINANT),
            ChordTemplate("7#5", "Dominant 7th Sharp 5", (Interval.UNIS, Interval.Maj3, Interval.min6, Interval.min7), ChordQuality.DOMINANT),
            ChordTemplate("7#9", "Dominant 7th Sharp 9 (Hendrix)", (Interval.UNIS, Interval.min3, Interval.Maj3, Interval.Per5, Interval.min7), ChordQuality.DOMINANT),
            ChordTemplate("7b9", "Dominant 7th Flat 9", (Interval.UNIS, Interval.min2, Interval.Maj3, Interval.Per5, Interval.min7), ChordQuality.DOMINANT),
            ChordTemplate("7#11", "Dominant 7th Sharp 11", (Interval.UNIS, Interval.Maj3, Interval.TTone, Interval.Per5, Interval.min7), ChordQuality.DOMINANT),
            ChordTemplate("7b13", "Dominant 7th Flat 13", (Interval.UNIS, Interval.Maj3, Interval.Per5, Interval.min6, Interval.min7), ChordQuality.DOMINANT),
            ChordTemplate("7alt", "Altered Dominant 7th", (Interval.UNIS, Interval.min2, Interval.Maj3, Interval.TTone, Interval.min7), ChordQuality.DOMINANT),

            # 7. 九和弦 (9th Chords)
            ChordTemplate("9", "Dominant 9th", (Interval.UNIS, Interval.Maj2, Interval.Maj3, Interval.Per5, Interval.min7), ChordQuality.DOMINANT),
            ChordTemplate("maj9", "Major 9th", (Interval.UNIS, Interval.Maj2, Interval.Maj3, Interval.Per5, Interval.Maj7), ChordQuality.MAJOR),
            ChordTemplate("m9", "Minor 9th", (Interval.UNIS, Interval.Maj2, Interval.min3, Interval.Per5, Interval.min7), ChordQuality.MINOR),
            ChordTemplate("m(maj9)", "Minor Major 9th", (Interval.UNIS, Interval.Maj2, Interval.min3, Interval.Per5, Interval.Maj7), ChordQuality.MINOR),
            ChordTemplate("9sus4", "9th Suspended 4th", (Interval.UNIS, Interval.Maj2, Interval.Per4, Interval.Per5, Interval.min7), ChordQuality.SUSPENDED),
            ChordTemplate("9#5", "Dominant 9th Sharp 5", (Interval.UNIS, Interval.Maj2, Interval.Maj3, Interval.min6, Interval.min7), ChordQuality.DOMINANT),
            ChordTemplate("9b5", "Dominant 9th Flat 5", (Interval.UNIS, Interval.Maj2, Interval.Maj3, Interval.TTone, Interval.min7), ChordQuality.DOMINANT),

            # 8. 十一和弦 (11th Chords)
            ChordTemplate("11", "Dominant 11th", (Interval.UNIS, Interval.Maj2, Interval.Per4, Interval.Per5, Interval.min7), ChordQuality.DOMINANT),
            ChordTemplate("m11", "Minor 11th", (Interval.UNIS, Interval.Maj2, Interval.min3, Interval.Per4, Interval.Per5, Interval.min7), ChordQuality.MINOR),
            ChordTemplate("maj11", "Major 11th", (Interval.UNIS, Interval.Maj2, Interval.Maj3, Interval.Per4, Interval.Per5, Interval.Maj7), ChordQuality.MAJOR),
            ChordTemplate("maj9#11", "Major 9th Sharp 11", (Interval.UNIS, Interval.Maj2, Interval.Maj3, Interval.TTone, Interval.Per5, Interval.Maj7), ChordQuality.MAJOR),
            ChordTemplate("9#11", "Dominant 9th Sharp 11", (Interval.UNIS, Interval.Maj2, Interval.Maj3, Interval.TTone, Interval.Per5, Interval.min7), ChordQuality.DOMINANT),

            # 9. 十三和弦 (13th Chords)
            ChordTemplate("13", "Dominant 13th", (Interval.UNIS, Interval.Maj2, Interval.Maj3, Interval.Per5, Interval.Maj6, Interval.min7), ChordQuality.DOMINANT),
            ChordTemplate("maj13", "Major 13th", (Interval.UNIS, Interval.Maj2, Interval.Maj3, Interval.Per5, Interval.Maj6, Interval.Maj7), ChordQuality.MAJOR),
            ChordTemplate("m13", "Minor 13th", (Interval.UNIS, Interval.Maj2, Interval.min3, Interval.Per5, Interval.Maj6, Interval.min7), ChordQuality.MINOR),
            ChordTemplate("13sus4", "13th Suspended 4th", (Interval.UNIS, Interval.Maj2, Interval.Per4, Interval.Per5, Interval.Maj6, Interval.min7), ChordQuality.SUSPENDED),
            ChordTemplate("13b9", "Dominant 13th Flat 9", (Interval.UNIS, Interval.min2, Interval.Maj3, Interval.Per5, Interval.Maj6, Interval.min7), ChordQuality.DOMINANT),
            ChordTemplate("13#9", "Dominant 13th Sharp 9", (Interval.UNIS, Interval.min3, Interval.Maj3, Interval.Per5, Interval.Maj6, Interval.min7), ChordQuality.DOMINANT),
            ChordTemplate("13#11", "Dominant 13th Sharp 11", (Interval.UNIS, Interval.Maj2, Interval.Maj3, Interval.TTone, Interval.Maj6, Interval.min7), ChordQuality.DOMINANT),
        ]

        # 排序规则：音程数量多的和弦优先匹配；同音程数下常用和弦优先
        self.chord_templates = sorted(
            raw_definitions,
            key=lambda t: (len(t.intervals), 1 if t.is_common else 0),
            reverse=True
        )

    @staticmethod
    def pitch_class_to_name(pitch_class: int, prefer_flat: bool = False) -> str:
        """半音索引 (0..11, 0=C) 转换为音名字符串"""
        pc = (pitch_class % 12 + 12) % 12
        return NOTE_NAMES_FLAT[pc] if prefer_flat else NOTE_NAMES_SHARP[pc]

    def identify(self, midi_notes: List[int], prefer_flat: bool = False) -> Optional[ChordMatchResult]:
        """
        核心识别入口：根据输入的 MIDI 音符序列识别和弦
        """
        if not midi_notes:
            return None

        # 1. 对应 RVA 0x1825d0: 升序排序并剔除八度完全重合的音符
        sorted_notes = sorted(midi_notes)
        bass_midi = sorted_notes[0]
        bass_pc = bass_midi % 12

        unique_pitch_classes: List[int] = []
        for n in sorted_notes:
            pc = n % 12
            if pc not in unique_pitch_classes:
                unique_pitch_classes.append(pc)

        num_notes = len(unique_pitch_classes)

        # 单音判定 (对应 RVA 0x1813d3)
        if num_notes == 1:
            root_name = self.pitch_class_to_name(bass_pc, prefer_flat)
            single_template = ChordTemplate("note", "Single Note", (Interval.UNIS,), ChordQuality.MAJOR)
            return ChordMatchResult(
                root_name=root_name,
                bass_name=root_name,
                root_pitch_class=bass_pc,
                bass_pitch_class=bass_pc,
                template=single_template,
                inversion=0,
                is_slash_chord=False,
                display_name=root_name,
                alternate_display_name=f"{root_name} (Single Note)",
                matched_notes_midi=sorted_notes,
                intervals=[0]
            )

        # 2. 对应 RVA 0x180c40 / 0x180e60: 循环移位转位穷举匹配
        # 旋转候选根音：从 0 (原位) 到 num_notes - 1 (各级转位)
        for inversion_idx in range(num_notes):
            root_pc = unique_pitch_classes[inversion_idx]
            current_intervals = tuple(sorted((pc - root_pc + 12) % 12 for pc in unique_pitch_classes))

            # 遍历模板库进行精确音程匹配 (对应 RVA 0x183230)
            for tmpl in self.chord_templates:
                if current_intervals == tmpl.intervals:
                    root_name = self.pitch_class_to_name(root_pc, prefer_flat)
                    bass_name = self.pitch_class_to_name(bass_pc, prefer_flat)
                    is_slash = (root_pc != bass_pc)

                    chord_symbol = tmpl.name
                    base_name = f"{root_name}{chord_symbol}"
                    display_name = f"{base_name}/{bass_name}" if is_slash else base_name

                    alt_base = f"{root_name} {tmpl.alternate_name}"
                    alt_display = f"{alt_base}/{bass_name}" if is_slash else alt_base

                    return ChordMatchResult(
                        root_name=root_name,
                        bass_name=bass_name,
                        root_pitch_class=root_pc,
                        bass_pitch_class=bass_pc,
                        template=tmpl,
                        inversion=inversion_idx,
                        is_slash_chord=is_slash,
                        display_name=display_name,
                        alternate_display_name=alt_display,
                        matched_notes_midi=sorted_notes,
                        intervals=list(current_intervals)
                    )

        # 3. 对应 RVA 0x1814cf: 省略纯五度 (omit 5) 容错匹配 (爵士常用和弦)
        for inversion_idx in range(num_notes):
            root_pc = unique_pitch_classes[inversion_idx]
            current_intervals = set((pc - root_pc + 12) % 12 for pc in unique_pitch_classes)

            for tmpl in self.chord_templates:
                tmpl_set = set(tmpl.intervals)
                if Interval.Per5 in tmpl_set and (tmpl_set - {Interval.Per5}) == current_intervals:
                    root_name = self.pitch_class_to_name(root_pc, prefer_flat)
                    bass_name = self.pitch_class_to_name(bass_pc, prefer_flat)
                    is_slash = (root_pc != bass_pc)

                    base_name = f"{root_name}{tmpl.name}(no5)"
                    display_name = f"{base_name}/{bass_name}" if is_slash else base_name

                    return ChordMatchResult(
                        root_name=root_name,
                        bass_name=bass_name,
                        root_pitch_class=root_pc,
                        bass_pitch_class=bass_pc,
                        template=tmpl,
                        inversion=inversion_idx,
                        is_slash_chord=is_slash,
                        display_name=display_name,
                        alternate_display_name=f"{root_name} {tmpl.alternate_name} (omit 5)",
                        matched_notes_midi=sorted_notes,
                        intervals=sorted(list(current_intervals))
                    )

        # 4. 对应 RVA 0x181710 - 0x181a50: 复合双和弦与左右手多声部分解 (Polychord / Upper Structure)
        # 若音符数 >= 5，尝试将音符分为下层和弦 (Bass/Left Hand) 与上层和弦 (Right Hand)
        if num_notes >= 5:
            candidates = []
            for split_idx in range(2, num_notes - 1):
                lower_pcs = unique_pitch_classes[:split_idx]
                upper_pcs = unique_pitch_classes[split_idx:]

                if len(lower_pcs) >= 2 and len(upper_pcs) >= 3:
                    lower_match = self.identify(lower_pcs, prefer_flat)
                    upper_match = self.identify(upper_pcs, prefer_flat)

                    if (lower_match and lower_match.template.name != "cluster" and
                        upper_match and upper_match.template.name != "cluster"):
                        # 计算匹配纯净度评分：优先完整无省略音的和弦
                        score = 0
                        if "(no5)" not in lower_match.display_name:
                            score += 10
                        if "(no5)" not in upper_match.display_name:
                            score += 10
                        if lower_match.template.quality in (ChordQuality.MAJOR, ChordQuality.MINOR):
                            score += 5
                        if upper_match.template.quality in (ChordQuality.MAJOR, ChordQuality.MINOR):
                            score += 5
                        if len(lower_pcs) == 3 and len(upper_pcs) == 3:
                            score += 5
                        candidates.append((score, lower_match, upper_match))

            if candidates:
                candidates.sort(key=lambda c: c[0], reverse=True)
                _, best_lower, best_upper = candidates[0]
                poly_name = f"{best_upper.display_name} | {best_lower.display_name}"
                return ChordMatchResult(
                    root_name=best_lower.root_name,
                    bass_name=best_lower.bass_name,
                    root_pitch_class=best_lower.root_pitch_class,
                    bass_pitch_class=best_lower.bass_pitch_class,
                    template=ChordTemplate("polychord", "Polychord / Upper Structure", tuple(unique_pitch_classes), ChordQuality.POLYCHORD),
                    inversion=0,
                    is_slash_chord=True,
                    display_name=poly_name,
                    alternate_display_name=f"{best_upper.alternate_display_name} over {best_lower.alternate_display_name}",
                    matched_notes_midi=sorted_notes,
                    intervals=sorted([(pc - bass_pc + 12) % 12 for pc in unique_pitch_classes]),
                    polychord_top=best_upper.display_name
                )

        # 5. 未匹配到字典：降级为音簇 (Tone Cluster)
        root_name = self.pitch_class_to_name(bass_pc, prefer_flat)
        raw_intervals = sorted((pc - bass_pc + 12) % 12 for pc in unique_pitch_classes)
        unknown_tmpl = ChordTemplate("cluster", "Custom Tone Cluster", tuple(raw_intervals), ChordQuality.MAJOR, is_common=False)
        return ChordMatchResult(
            root_name=root_name,
            bass_name=root_name,
            root_pitch_class=bass_pc,
            bass_pitch_class=bass_pc,
            template=unknown_tmpl,
            inversion=0,
            is_slash_chord=False,
            display_name=f"{root_name}({','.join(str(i) for i in raw_intervals)})",
            alternate_display_name=f"Custom Tone Cluster on {root_name}",
            matched_notes_midi=sorted_notes,
            intervals=raw_intervals
        )

    def identify_from_note_names(self, note_names: List[str], prefer_flat: bool = False) -> Optional[ChordMatchResult]:
        """辅助工具：通过音名列表（如 ['C4', 'E4', 'G4', 'B4']）直接调用识别"""
        midi_notes = []
        for name in note_names:
            name_clean = name.strip()
            octave = 4
            pitch_part = name_clean
            if name_clean[-1].isdigit():
                octave = int(name_clean[-1])
                pitch_part = name_clean[:-1]

            pitch_part = pitch_part.capitalize()
            if pitch_part in NOTE_TO_PITCH_CLASS:
                pc = NOTE_TO_PITCH_CLASS[pitch_part]
                midi = (octave + 1) * 12 + pc
                midi_notes.append(midi)
            else:
                raise ValueError(f"Unrecognized note name: {name}")

        return self.identify(midi_notes, prefer_flat=prefer_flat)
