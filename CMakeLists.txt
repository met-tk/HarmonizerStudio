cmake_minimum_required(VERSION 3.22)

project(HarmonizerStudio VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Add JUCE from local third_party folder
add_subdirectory(third_party/JUCE)

juce_add_gui_app(HarmonizerStudio
    PRODUCT_NAME "Harmonizer Studio"
    VERSION "1.0.0"
)

target_sources(HarmonizerStudio PRIVATE
    src/main.cpp

    # Core Data & Undo
    src/core/ScaleDefinitions.h
    src/core/ScaleDefinitions.cpp
    src/core/TrackModel.h
    src/core/TrackModel.cpp
    src/core/KeyCommandManager.h
    src/core/KeyCommandManager.cpp
    src/core/MidiModel.h
    src/core/MidiModel.cpp
    src/core/UndoActions.h
    src/core/UndoActions.cpp
    src/core/ProjectFile.h
    src/core/ProjectFile.cpp
    src/core/ProjectSelfTests.h
    src/core/Localization.h
    src/core/Localization.cpp
    src/core/ModalFunctionEngine.h
    src/core/ModalFunctionEngine.cpp
    src/core/AppStateManager.h
    src/core/SectionLayout.h
    src/core/GeneralSettings.h

    # Audio Engine & VST3 Host
    src/audio/BuiltinSynth.h
    src/audio/BuiltinSynth.cpp
    src/audio/VstHostManager.h
    src/audio/VstHostManager.cpp
    src/audio/AudioEngine.h
    src/audio/AudioEngine.cpp
    src/audio/ChannelStripMath.h
    src/audio/ChannelStripMathSelfTests.h

    # Core Analysis
    src/core/ModeAnalyzer.h
    src/core/ModeAnalyzer.cpp
    src/core/ChordClassifier.h
    src/core/ChordClassifier.cpp
    src/core/ChordTheorySelfTests.h
    src/core/ChordPlanAnalysis.h
    src/core/SecondaryDominant.h
    src/core/SecondaryDominant.cpp
    src/core/SecondaryDominantSelfTests.h
    src/core/QuickChordCatalog.h
    src/core/QuickChord.h
    src/core/QuickChord.cpp
    src/core/QuickChordSelfTests.h
    src/core/Reharmonizer.h
    src/core/Reharmonizer.cpp
    src/core/HarmonyGrouping.h
    src/core/ReharmonizerSelfTests.h
    src/core/ChordEditing.h
    src/core/ChordEditing.cpp
    src/core/NoteEditOperations.h
    src/core/NoteEditOperations.cpp

    # UI Components
    src/ui/CubaseTheme.h
    src/ui/CubaseTheme.cpp
    src/ui/ChannelStripSlider.h
    src/ui/ChannelStripSlider.cpp
    src/ui/EventEditors.h
    src/ui/EventEditors.cpp
    src/ui/FloatingToolbox.h
    src/ui/FloatingToolbox.cpp
    src/ui/TimelineRuler.h
    src/ui/TimelineRuler.cpp
    src/ui/PianoKeyboardComponent.h
    src/ui/PianoKeyboardComponent.cpp
    src/ui/VelocityLaneComponent.h
    src/ui/VelocityLaneComponent.cpp
    src/ui/GlobalTrackOverviewLane.h
    src/ui/GlobalTrackOverviewLane.cpp
    src/ui/TrackArrangerComponent.h
    src/ui/TrackArrangerComponent.cpp
    src/ui/PianoRollComponent.h
    src/ui/PianoRollComponent.cpp
    src/ui/ChordPlacementGroup.h
    src/ui/ToolbarComponent.h
    src/ui/ToolbarComponent.cpp
    src/ui/TransportBar.h
    src/ui/TransportBar.cpp
    src/ui/InspectorComponent.h
    src/ui/InspectorComponent.cpp
    src/ui/SecondaryDominantAction.h
    src/ui/SecondaryDominantAction.cpp
    src/ui/PreferencesDialog.h
    src/ui/PreferencesDialog.cpp
    src/ui/MainComponent.h
    src/ui/MainComponent.cpp
)

target_include_directories(HarmonizerStudio PRIVATE
    src
)

target_compile_definitions(HarmonizerStudio PRIVATE
    JUCE_PLUGINHOST_VST3=1
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_WASAPI=1
    JUCE_DIRECTSOUND=1
    JUCE_MODAL_LOOPS_PERMITTED=1
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0
    _UNICODE
    UNICODE
)

if (MSVC)
    target_compile_options(HarmonizerStudio PRIVATE /utf-8 /MP)
endif()

target_link_libraries(HarmonizerStudio PRIVATE
    juce::juce_core
    juce::juce_events
    juce::juce_graphics
    juce::juce_data_structures
    juce::juce_gui_basics
    juce::juce_gui_extra
    juce::juce_audio_basics
    juce::juce_audio_devices
    juce::juce_audio_formats
    juce::juce_audio_processors
    juce::juce_audio_utils
)
