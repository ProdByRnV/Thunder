# ---------------------------------------------------------------------------
# ThunderCore — DSP, engine, state and music code with no GUI or host dependency.
#
# It is an INTERFACE library: its sources are compiled into each binary that
# uses it (the plugin now; tests and benchmarks later), the same way JUCE modules
# work. That way JUCE code is never compiled twice into one binary, and anything
# that links only ThunderCore can't reach the GUI modules.
# ---------------------------------------------------------------------------
add_library(ThunderCore INTERFACE)

target_include_directories(ThunderCore INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}")

target_link_libraries(ThunderCore INTERFACE
    juce::juce_core
    juce::juce_audio_basics
    juce::juce_audio_formats
    juce::juce_data_structures
    juce::juce_dsp
    signalsmith-stretch
    thunder_cloudseed)

target_compile_definitions(ThunderCore INTERFACE
    THUNDER_VERSION_STRING="${PROJECT_VERSION}")

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
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    VST3_CATEGORIES Fx
    AU_MAIN_TYPE kAudioUnitType_Effect

    # The standalone app reads live audio input.
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "Thunder processes the audio from your input device."

    COPY_PLUGIN_AFTER_BUILD ${THUNDER_COPY_AFTER_BUILD})

target_sources(Thunder PRIVATE
    plugin/ThunderProcessor.cpp
    plugin/ThunderProcessor.h)

target_compile_definitions(Thunder PUBLIC
    # Thunder never uses the network.
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(Thunder PRIVATE
    ThunderCore
    juce::juce_audio_utils)

thunder_apply_compiler_settings(Thunder)
