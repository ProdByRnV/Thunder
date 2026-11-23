# ---------------------------------------------------------------------------
# ThunderCore — DSP, engine, state and music code with no GUI or host dependency.
#
# It is an INTERFACE library: its sources are compiled into each binary that
# uses it (the plugin, the tests, later the benchmarks), the same way JUCE modules
# work. That way JUCE code is never compiled twice into one binary, and anything
# that links only ThunderCore can't reach the GUI modules.
# ---------------------------------------------------------------------------
# Factory presets, built into the plugin (PRD FR-PRESET-001). Each file lists
# only the settings it changes.
file(GLOB THUNDER_FACTORY_PRESETS CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/assets/presets/*.thunderpreset")
juce_add_binary_data(ThunderPresetData
    HEADER_NAME ThunderPresetData.h
    NAMESPACE ThunderPresetData
    SOURCES ${THUNDER_FACTORY_PRESETS})
set_target_properties(ThunderPresetData PROPERTIES POSITION_INDEPENDENT_CODE ON FOLDER "data")

add_library(ThunderCore INTERFACE)

target_include_directories(ThunderCore INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}")

target_sources(ThunderCore INTERFACE
    core/capture/CaptureController.cpp
    core/capture/CaptureController.h
    core/capture/CaptureNaming.cpp
    core/capture/CaptureNaming.h
    core/capture/CaptureStatus.cpp
    core/capture/CaptureStatus.h
    core/capture/CaptureTypes.h
    core/capture/CaptureWriter.cpp
    core/capture/CaptureWriter.h
    core/dsp/duck/DuckModule.cpp
    core/dsp/duck/DuckModule.h
    core/dsp/gain/AutoGain.h
    core/dsp/gain/LoudnessMeter.h
    core/dsp/gain/SafetyLimiter.h
    core/dsp/grains/GrainBuffer.h
    core/dsp/grains/GrainModule.cpp
    core/dsp/grains/GrainModule.h
    core/dsp/grains/GrainWindow.h
    core/dsp/pitch/IntervalFollower.cpp
    core/dsp/pitch/IntervalFollower.h
    core/dsp/pitch/PitchModule.cpp
    core/dsp/pitch/PitchModule.h
    core/dsp/shape/ShapeModule.cpp
    core/dsp/shape/ShapeModule.h
    core/dsp/space/FdnReverb.cpp
    core/dsp/space/FdnReverb.h
    core/dsp/space/SpaceModule.cpp
    core/dsp/space/SpaceModule.h
    core/dsp/static/StaticModulator.cpp
    core/dsp/static/StaticModulator.h
    core/dsp/time/TimeModule.cpp
    core/dsp/time/TimeModule.h
    core/dsp/tone/Svf.h
    core/dsp/tone/ToneModule.cpp
    core/dsp/tone/ToneModule.h
    core/dsp/width/WidthModule.cpp
    core/dsp/width/WidthModule.h
    core/engine/Module.h
    core/engine/ModuleSlot.h
    core/engine/OutputStage.h
    core/engine/ThunderEngine.cpp
    core/engine/ThunderEngine.h
    core/host/MusicalClock.cpp
    core/host/MusicalClock.h
    core/host/TransportState.cpp
    core/host/TransportState.h
    core/music/IntervalMapper.cpp
    core/music/IntervalMapper.h
    core/music/KeyParams.h
    core/music/PitchTracker.cpp
    core/music/PitchTracker.h
    core/music/Scale.h
    core/params/Enums.h
    core/params/ParamDefs.h
    core/params/ParamSnapshot.h
    core/params/ParamText.cpp
    core/params/ParamText.h
    core/presets/PresetDocument.cpp
    core/presets/PresetDocument.h
    core/presets/PresetLibrary.cpp
    core/presets/PresetLibrary.h
    core/state/SessionState.cpp
    core/state/SessionState.h
    core/state/StateIds.h
    core/state/StateManager.cpp
    core/state/StateManager.h
    core/state/UserSettings.cpp
    core/state/UserSettings.h
    core/util/Crossfade.h
    core/util/LatencyDelay.h
    core/util/Math.h
    core/util/Rng.h
    core/util/Smoother.h
    core/util/SpscFifo.h
    core/visual/VisualFrames.h
    core/visual/VisualTap.cpp
    core/visual/VisualTap.h)

target_link_libraries(ThunderCore INTERFACE
    juce::juce_core
    juce::juce_audio_basics
    juce::juce_audio_formats
    juce::juce_data_structures
    juce::juce_dsp
    signalsmith-stretch
    thunder_cloudseed
    ThunderPresetData)

target_compile_definitions(ThunderCore INTERFACE
    THUNDER_VERSION_STRING="${PROJECT_VERSION}")

# ---------------------------------------------------------------------------
# ThunderPluginCode — the host adapter (processor, parameter layout, editor).
#
# Also an INTERFACE library, so the tests can build and drive the real
# processor (state, buses, processBlock) without a plugin wrapper.
# ---------------------------------------------------------------------------
add_library(ThunderPluginCode INTERFACE)

target_sources(ThunderPluginCode INTERFACE
    plugin/ThunderEditor.cpp
    plugin/ThunderEditor.h
    plugin/ThunderProcessor.cpp
    plugin/ThunderProcessor.h
    plugin/bridge/UiBridge.h
    plugin/params/ParameterLayout.cpp
    plugin/params/ParameterLayout.h
    plugin/presets/PresetManager.cpp
    plugin/presets/PresetManager.h
    plugin/ui/ParamHelp.cpp
    plugin/ui/ParamHelp.h
    plugin/ui/Motion.cpp
    plugin/ui/Motion.h
    plugin/ui/ParamUi.h
    plugin/ui/components/Buttons.cpp
    plugin/ui/components/Buttons.h
    plugin/ui/components/CaptureWidget.cpp
    plugin/ui/components/CaptureWidget.h
    plugin/ui/components/Meters.cpp
    plugin/ui/components/Meters.h
    plugin/ui/components/ModuleDisplays.cpp
    plugin/ui/components/ModuleDisplays.h
    plugin/ui/components/Controls.cpp
    plugin/ui/components/Controls.h
    plugin/ui/components/ThunderKnob.cpp
    plugin/ui/components/ThunderKnob.h
    plugin/ui/overlays/AboutPanel.cpp
    plugin/ui/overlays/AboutPanel.h
    plugin/ui/overlays/SaveForm.cpp
    plugin/ui/overlays/SaveForm.h
    plugin/ui/overlays/SettingsPanel.cpp
    plugin/ui/overlays/SettingsPanel.h
    plugin/ui/regions/ChainRail.cpp
    plugin/ui/regions/ChainRail.h
    plugin/ui/regions/ModulePanels.cpp
    plugin/ui/regions/ModulePanels.h
    plugin/ui/regions/OutputPanel.cpp
    plugin/ui/regions/OutputPanel.h
    plugin/ui/regions/TopBar.cpp
    plugin/ui/regions/TopBar.h
    plugin/ui/regions/VisualiserRegion.cpp
    plugin/ui/regions/VisualiserRegion.h
    plugin/ui/theme/ThunderFonts.cpp
    plugin/ui/theme/ThunderFonts.h
    plugin/ui/theme/ThunderIcons.h
    plugin/ui/theme/ThunderLookAndFeel.cpp
    plugin/ui/theme/ThunderLookAndFeel.h
    plugin/ui/theme/ThunderPaint.cpp
    plugin/ui/theme/ThunderPaint.h
    plugin/ui/theme/ThunderTheme.h
    plugin/ui/visualiser/FilterResponse.h
    plugin/ui/visualiser/Lightning.cpp
    plugin/ui/visualiser/Lightning.h
    plugin/ui/visualiser/LightningView.cpp
    plugin/ui/visualiser/LightningView.h
    plugin/ui/visualiser/MeterModel.cpp
    plugin/ui/visualiser/MeterModel.h
    plugin/ui/visualiser/SpectrumAnalyser.cpp
    plugin/ui/visualiser/SpectrumAnalyser.h
    plugin/ui/visualiser/SpectrumView.cpp
    plugin/ui/visualiser/SpectrumView.h
    plugin/ui/visualiser/VisualModel.cpp
    plugin/ui/visualiser/VisualModel.h
    plugin/ui/visualiser/WaveformHistory.cpp
    plugin/ui/visualiser/WaveformHistory.h
    plugin/ui/visualiser/WaveformView.cpp
    plugin/ui/visualiser/WaveformView.h)

# The UI fonts (UI.md §4.1), built in when present in assets/fonts.
set(THUNDER_FONT_FILES
    "${PROJECT_SOURCE_DIR}/assets/fonts/Inter/Inter-Regular.ttf"
    "${PROJECT_SOURCE_DIR}/assets/fonts/Inter/Inter-Medium.ttf"
    "${PROJECT_SOURCE_DIR}/assets/fonts/Inter/Inter-SemiBold.ttf"
    "${PROJECT_SOURCE_DIR}/assets/fonts/JetBrainsMono/JetBrainsMono-Medium.ttf")
set(_thunder_fonts_present ON)
foreach(font IN LISTS THUNDER_FONT_FILES)
    if(NOT EXISTS "${font}")
        set(_thunder_fonts_present OFF)
    endif()
endforeach()

if(_thunder_fonts_present)
    juce_add_binary_data(ThunderFontData
        HEADER_NAME ThunderFontData.h
        NAMESPACE ThunderFontData
        SOURCES ${THUNDER_FONT_FILES})
    set_target_properties(ThunderFontData PROPERTIES POSITION_INDEPENDENT_CODE ON FOLDER "data")
    target_link_libraries(ThunderPluginCode INTERFACE ThunderFontData)
    target_compile_definitions(ThunderPluginCode INTERFACE THUNDER_EMBEDDED_FONTS=1)
endif()

target_link_libraries(ThunderPluginCode INTERFACE
    ThunderCore
    juce::juce_audio_processors
    juce::juce_opengl)

# ---------------------------------------------------------------------------
# Thunder — the plugin (host wrapper + editor).
# ---------------------------------------------------------------------------
juce_add_plugin(Thunder
    PRODUCT_NAME "Thunder"
    VERSION "${PROJECT_VERSION}"
    COMPANY_NAME "ProdByRnV"
    COMPANY_WEBSITE "https://github.com/ProdByRnV/Thunder"
    BUNDLE_ID "com.prodbyrnv.thunder"
    DESCRIPTION "Turns a melody into an in-key texture layer"

    # Plugin identity. These codes must never change once a build is shared:
    # hosts use them to recognise the plugin in saved sessions.
    PLUGIN_MANUFACTURER_CODE Pbrv
    PLUGIN_CODE Thnd

    FORMATS ${THUNDER_FORMATS}
    IS_SYNTH FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    VST3_CATEGORIES Fx
    AU_MAIN_TYPE kAudioUnitType_Effect

    COPY_PLUGIN_AFTER_BUILD ${THUNDER_COPY_AFTER_BUILD})

target_sources(Thunder PRIVATE
    plugin/PluginEntry.cpp)

if(THUNDER_BUILD_CLAP)
    clap_juce_extensions_plugin(TARGET Thunder
        CLAP_ID "com.prodbyrnv.thunder"
        CLAP_FEATURES audio-effect stereo
        CLAP_SUPPORT_URL "https://github.com/ProdByRnV/Thunder/issues")
endif()

target_compile_definitions(Thunder PUBLIC
    JUCE_VST3_CAN_REPLACE_VST2=0)

thunder_disable_network_features(Thunder)

target_link_libraries(Thunder PRIVATE
    ThunderPluginCode
    juce::juce_audio_utils)

thunder_apply_compiler_settings(Thunder)
