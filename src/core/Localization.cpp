#include "Localization.h"

namespace Harmonizer
{

Localization& Localization::getInstance()
{
    static Localization instance;
    return instance;
}

Localization::Localization()
{
    initDictionary();
    loadConfig();
}

void Localization::setLanguage(Language lang)
{
    if (currentLang != lang)
    {
        currentLang = lang;
        saveConfig();
        listeners.call([](Listener& l) { l.languageChanged(); });
    }
}

void Localization::saveConfig()
{
    auto dir = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory).getChildFile("HarmonizerStudio");
    dir.createDirectory();
    auto file = dir.getChildFile("language_settings.xml");

    juce::XmlElement root("LanguageSettings");
    root.setAttribute("language", (int)currentLang);
    root.writeTo(file);
}

void Localization::loadConfig()
{
    auto dir = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory).getChildFile("HarmonizerStudio");
    auto file = dir.getChildFile("language_settings.xml");
    if (!file.existsAsFile())
        return;

    auto xml = juce::parseXML(file);
    if (!xml || xml->getTagName() != "LanguageSettings")
        return;

    int l = xml->getIntAttribute("language", (int)Language::SimplifiedChinese);
    if (l == (int)Language::English || l == (int)Language::SimplifiedChinese)
        currentLang = (Language)l;
}

juce::String Localization::get(const juce::String& key) const
{
    if (currentLang == Language::English)
        return key;

    auto it = zhStrings.find(key);
    if (it != zhStrings.end())
        return it->second;

    return key;
}

void Localization::initDictionary()
{
    auto add = [this](const char* en, const char* zh)
    {
        zhStrings[juce::String::fromUTF8(en)] = juce::String::fromUTF8(zh);
    };

    // Menus
    add("File", "文件");
    add("Edit", "编辑");
    add("Transport", "走带");
    add("Help", "帮助");
    add("New Project", "新建工程");
    add("Open Project... (Ctrl+O)", "打开工程... (Ctrl+O)");
    add("Save Project (Ctrl+S)", "保存工程 (Ctrl+S)");
    add("Save Project As...", "工程另存为...");
    add("Import MIDI File (.mid)...", "导入 MIDI 文件 (.mid)...");
    add("Export MIDI File (.mid)...", "导出 MIDI 文件 (.mid)...");
    add("Workspace", "工作区");
    add("Split Layout (Default)", "上下分栏视图 (默认)");
    add("Arranger Only (Full Screen)", "仅编排轨道视图 (全屏)");
    add("Key Editor Only (Full Screen)", "仅钢琴卷帘窗视图 (全屏)");
    add("Dual Windows (Detached Key Editor)", "独立窗口模式 (卷帘分离)");
    add("Save Workspace Preset...", "保存工作区预设...");
    add("Load Workspace Preset...", "加载工作区预设...");
    add("Preferences / Audio & VST...", "首选项 / 音频与 VST 设置...");
    add("Exit", "退出");

    add("Undo (Ctrl+Z)", "撤销 (Ctrl+Z)");
    add("Redo (Ctrl+Y)", "重做 (Ctrl+Y)");
    add("Copy (Ctrl+C)", "复制 (Ctrl+C)");
    add("Paste (Ctrl+V)", "粘贴 (Ctrl+V)");
    add("Duplicate (Ctrl+D)", "向后复制 (Ctrl+D)");
    add("Delete (Del)", "删除 (Del)");
    add("Select All (Ctrl+A)", "全选 (Ctrl+A)");

    add("Play (Space)", "播放 (空格)");
    add("Pause (Space)", "暂停 (空格)");
    add("Stop", "停止");
    add("Click", "节拍器");
    add("Sound", "音色");
    add("Toggle metronome", "开启或关闭节拍器");
    add("Metronome sound preset", "节拍器声音预设");
    add("Classic Click", "经典滴答");
    add("Wood Click", "木质敲击");
    add("Electronic Click", "电子脉冲");
    add("Delete Selected Track", "删除所选轨道");
    add("Toggle Metronome", "切换节拍器");
    add("Toggle Loop (/)", "循环切换 (/)");
    add("Cubase Shortcuts Guide", "Cubase 快捷键指南");
    add("About Harmonizer Studio", "关于 Harmonizer Studio");

    // Toolbar
    add("1: Select", "1: 选择");
    add("3: Split", "3: 剪刀");
    add("5: Erase", "5: 橡皮");
    add("8: Draw", "8: 画笔");
    add("Snap [J]", "吸附 [J]");
    add("Toggle Playhead Follow", "切换光标追踪");
    add("Triplet", "三连音");
    add("Undo", "撤销");
    add("Redo", "重做");
    add("Zoom +", "水平缩放 +");
    add("Zoom -", "水平缩放 -");

    // Inspector
    add("Note Inspector", "音符属性检查器");
    add("No Notes Selected", "未选中任何音符");
    add("1 Note Selected", "已选中 1 个音符");
    add("Notes Selected", "个音符被选中");
    add("Pitch", "音高");
    add("Start", "起始位置");
    add("Length", "音符时值");
    add("Velocity", "击弦力度");
    add("Chord", "和弦");
    add("None", "无");
    add("Degree", "调式级数");
    add("Multiple", "多值不同");
    add("beats", "拍");

    add("Mode Colour", "调式色彩");
    add("Track & Note Inspector", "音符与轨道属性");
    add("Real-time Chord Analysis", "实时和弦分析");
    add("Folder Track Inspector", "文件夹轨检查器");
    add("Chord Track Inspector", "和弦轨属性检查器");
    add("Extract Chords From MIDI", "从乐器轨提取和弦");
    add("Extracts polyphonic MIDI harmonies to Chord blocks", "从乐器轨音符自动解析和弦并填充和弦块");
    add("Mode Track Inspector", "调式轨属性检查器");
    add("Analyze & Populate Modes", "全曲调式分析与填充");
    add("Governs tonality, scales, and chromatic modal guidance", "统领全曲各段落调式、音阶与钢琴卷帘窗色彩标注");
    add("Marker Inspector", "标记轨属性检查器");
    add("Pure arrangement milestones with overlapping stacking", "段落结构与事件标注，支持多层叠置显示");
    add("Time Signature Track Inspector", "拍号轨属性检查器");
    add("Dynamic piecewise meter and measure grid management", "动态分段拍号管理与小节网格划分");
    add("Sections", "分区管理");
    add("Manage", "管理");
    add("Set module location and options", "设置此模块的位置、属性和分区布局");
    add("Dock Left", "停靠左侧");
    add("Dock Right", "停靠右侧");
    add("Hide Module", "隐藏模块");
    add("Open Sections Manager", "打开分区管理");
    add("Module Settings", "模块设置");
    add("View: Arranger / Editor", "视图: 编排 / 卷帘");

    add("Chord & Scale Analysis", "和弦与调式分析");
    add("Instrument / VST3", "乐器 / VST3 音源");
    add("Open VST GUI (Kontakt)", "打开 VST 界面 (Kontakt)");
    add("Harmonic Operations", "和声算子操作");
    add("Harmony Editing", "和声编辑");
    add("Secondary Dominant Module", "副属和弦");
    add("Quick Chord", "快速和弦");
    add("Reharmonization", "和声重配");
    add("Chord Shape & Voicing", "和弦类型与排列");
    add("No clear chord", "无明确和弦");
    add("Select one clear chord or arpeggio.", "请只选择一个明确的和弦或琶音。");
    add("Select at least one clear chord or arpeggio.", "请至少选择一个明确的和弦或琶音。");
    add("chord positions were skipped because the chord track is occupied.", "个和弦位置因和弦轨已有事件而跳过。");
    add("Mode Borrowing", "调式借用");
    add("Apply Reharmonization", "重配所选和弦");
    add("Diatonic common-tone substitution", "调内共同音替代");
    add("Tritone substitution", "三全音替代");
    add("Parallel mode borrowing", "平行调式借用");
    add("Chromatic mediant", "色彩三度关系");
    add("Shares two tones within the current scale", "在当前调式中寻找两个共同音");
    add("Replaces a dominant seventh a tritone away", "将属七和弦替换为三全音外的属七和弦");
    add("Borrows major or minor from the parallel mode", "借用同根音的大小三和弦");
    add("Chooses a chromatic third-related chord", "选择具有共同音的变化三度和弦");
    add("Finds the closest chord within the current scale", "在当前调式中寻找声部移动最少的替代和弦");
    add("Exchanges tonic with sixth degree or fourth with second", "主和弦与六级、四级与二级互换");
    add("Replaces V7 with a flat-seventh dominant in a major key", "在大调中将五级属七替换为降七级属七");
    add("Octave Up", "音符上移八度");
    add("Octave Down", "音符下移八度");
    add("Secondary Dominant", "生成副属和弦");
    add("Harmonic Settings", "副属和弦设置");
    add("Expand Detailed Settings", "展开详细设置");
    add("Collapse Detailed Settings", "收起详细设置");
    add("Selected notes form one block chord", "选中音符合成一组柱式和弦");
    add("Each note gets its own chord", "每个选中音分别生成和弦");
    add("Following harmony uses separate groups", "按后续和声分组生成");
    add("Target", "目标");
    add("Root", "根音");
    add("Chord Type", "类型");
    add("Voicing", "声部");
    add("Selected notes as one chord", "选中音符合成一个和弦");
    add("Each selected note", "每个选中音独立生成");
    add("Next harmony event", "后续和弦轨 / MIDI");
    add("Fixed target root", "固定目标根音");
    add("Smooth voice leading", "平滑连接");
    add("Root in bass", "根音低音");

    // Transport
    add("Built-in Synth", "内置合成器");
    add("VST3 (Kontakt)", "VST3 音源 (Kontakt)");
    add("Settings", "设置");
    add("Play [Space]", "播放 [空格]");
    add("Pause [Space]", "暂停 [空格]");
    add("Loop", "循环");

    // Preferences
    add("Preferences & Settings", "首选项与全局设置");
    add("Audio Hardware", "音频设备与驱动");
    add("VST3 Plugins", "VST3 插件管理");
    add("Keyboard Shortcuts", "快捷键对照");
    add("Language", "语言 / Language");
    add("Language / 语言", "语言 / Language");
    add("Scan for VST3 Plugins", "扫描 VST3 插件目录");
    add("Scanning VST3...", "正在检索 VST3 插件...");
    add("Standard VST3 search path: C:\\Program Files\\Common Files\\VST3", "标准 VST3 检索目录: C:\\Program Files\\Common Files\\VST3");
    add("Cubase Style Key Commands", "Cubase 风格快捷键对照表");
    add("Select Interface Language:", "选择界面语言 (Select Language):");
    add("Changes take effect immediately across all windows.", "修改后将实时刷新整个软件的界面语言。");

    // Shortcut Descriptions
    add("Select Tool (Normal selection)", "选择工具 (常规框选与拖拽音符)");
    add("Split Tool (Scissors - split note at position)", "剪刀工具 (在鼠标点击位置切断音符)");
    add("Erase Tool (Delete clicked note)", "橡皮擦工具 (删除点击的音符)");
    add("Draw Tool (Create note of current snap length)", "画笔工具 (按当前吸附网格长度绘制音符)");
    add("Play / Pause playback", "播放 / 暂停回放");
    add("Toggle Grid Snap on / off", "开启 / 关闭网格吸附");
    add("Zoom Time horizontally (Out / In)", "水平时间轴缩放 (缩小 / 放大)");
    add("Zoom Pitch vertically (Out / In)", "垂直音高轴缩放 (缩小 / 放大)");
    add("Delete selected notes", "删除选中的音符");
    add("Duplicate selected notes", "向后复制选中的音符");
    add("Copy / Paste selected notes", "复制 / 粘贴选中的音符");
    add("Select all notes", "全选所有音符");
    add("Undo / Redo action", "撤销 / 重做操作");

    // Scale Coloring Modes
    add("Scale Adaptive (In: Green / Out: Red)", "智能调式自适应 (调内: 绿 / 离调: 红)");
    add("Degree Function (I: Gold / V: Blue)", "调式级数色彩 (主和弦: 金 / 属和弦: 蓝)");
    add("Uniform / Track Blue", "经典统一轨色 (默认深蓝)");

    // Themes & Preferences
    add("Appearance / Colors", "外观与色彩");
    add("Theme preset", "颜色预设");
    add("Restore preset", "恢复当前预设");
    add("Interface colours", "界面与文字");
    add("Selection and locators", "选中与定位");
    add("Piano roll and grid", "钢琴卷帘窗与网格");
    add("DAW Theme & UI Preset:", "DAW 主题风格预设:");
    add("Cubase Classic Dark", "Cubase 经典暗黑 (Classic Dark)");
    add("Modern Studio Slate", "现代工作站板岩蓝 (Studio Slate)");
    add("Ableton Minimalist", "Ableton 极简炭灰 (Minimalist)");
    add("Warm Space Grey", "深邃太空暖灰 (Space Grey)");
    add("Modern Studio Light", "现代工作站亮白 (Studio Light)");
    add("Cubase Classic Light", "Cubase 经典浅灰 (Classic Light)");
    add("Custom Theme", "自定义主题 (Custom)");
    add("Saved presets", "已保存的自定义预设");
    add("Modified", "已修改");
    add("Save as custom preset", "保存为自定义预设");
    add("Rename preset", "重命名预设");
    add("Overwrite preset", "覆盖预设");
    add("Delete preset", "删除预设");
    add("Custom preset", "自定义预设");
    add("Preset name", "预设名称");
    add("Enter a unique name for the current colors.", "为当前颜色输入一个不重复的名称。");
    add("Enter a unique name for this preset.", "为这个预设输入一个不重复的名称。");
    add("Preset could not be saved", "无法保存预设");
    add("Preset could not be updated", "无法更新预设");
    add("Please check that theme settings can be saved.", "请检查主题配置是否可以写入。");
    add("Choose a unique name of 1 to 64 characters and check that settings can be saved.", "请输入 1 至 64 个字符的不重复名称，并检查主题配置是否可以写入。");
    add("Choose a unique name of 1 to 64 characters.", "请输入 1 至 64 个字符的不重复名称。");
    add("Use a unique name of 1 to 64 characters; check that settings can be saved.", "名称需为 1 至 64 个字符且不重复；请检查设置是否可保存。");
    add("Replace the saved colors in", "用当前颜色覆盖预设");
    add("Delete the saved preset", "删除自定义预设");
    add("The current colors will remain available as a custom theme.", "当前颜色会保留为自定义主题。");
    add("Overwrite", "覆盖");
    add("Rename", "重命名");
    add("Delete", "删除");
    add("Save", "保存");
    add("OK", "确定");
    add("Reset Preset Defaults", "重置预设默认值");
    add("Reset Default Theme", "重置为当前预设默认值");

    add("Custom Color Customization (Live Hot-Reload):", "全局界面自定义颜色方案 (实时热重载):");
    add("Background Deep", "最深底色");
    add("Background Surface", "表面底色");
    add("Background Panel", "面板底色");
    add("Toolbar Background", "顶部控制栏底色");
    add("Sidebar Background", "侧边栏底色");
    add("Module Header Background", "模块标题底色");
    add("Button Background", "按钮底色");
    add("Input Background", "输入框底色");
    add("Menu Background", "菜单底色");
    add("Primary Text", "主要文本");
    add("Secondary Text", "次要文本");
    add("Accent Blue", "强调蓝色");
    add("Accent Orange", "强调橙色");
    add("Accent Cyan", "强调青色");
    add("Accent Green", "强调绿色");
    add("Accent Purple", "强调紫色");
    add("Note Selection Glow", "音符发光");
    add("Note Glow", "音符选中光晕");
    add("Subtle Border", "微弱边框");
    add("Bright Border", "高亮边框");

    add("Locator Handles & Range Colors:", "标志定位滑块与循环范围配色方案:");
    add("Locator Handles [L/R]", "定位把手滑块 [L/R]");
    add("Locator Range Overlay", "轨道与卷帘窗范围选区");

    add("Piano Roll Background & Grid Custom Colors:", "钢琴卷帘窗背景与时间网格配色:");
    add("White Key Row", "白键行底色");
    add("Black Key Row", "黑键行底色");
    add("Tonic Highlight Tint", "主音底色高亮");
    add("Tonic Tint", "主音底色高亮");
    add("Overlay on White Key Row", "按透明度叠加在白键行底色上");
    add("Bar Line", "小节线");
    add("Beat Line", "拍线");
    add("Sub-Beat Line", "细分拍线");

    add("Choose Color: ", "选取颜色: ");

    // Sections & Focus Tabs
    add("Sections & Docking Manager", "侧边栏分区与停靠管理");
    add("Key Editor", "钢琴卷帘窗");
    add("Instrument Track", "乐器轨");
    add("Chord Track", "和弦轨");
    add("Mode Track", "调式轨");
    add("Marker Track", "标记轨");
    add("Time Signature Track", "拍号轨");
    add("Left Zone", "左侧边栏");
    add("Right Zone", "右侧边栏");
    add("Hidden", "隐藏");
    add("Reset Defaults", "恢复默认");
    add("Apply & Save", "应用并保存");
    add("Apply", "应用");
    add("Cancel", "取消");
    add("Chord Analysis", "实时和弦分析");
    add("Degree", "级数");
}


} // namespace Harmonizer
