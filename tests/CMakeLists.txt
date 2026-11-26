# ThunderTests — the test runner for ThunderCore and the plugin processor (no
# plugin format wrapper; no window is ever opened).
#
# Tests are tagged in Catch2 and those tags become CTest labels:
#   [unit]      functional tests, run on every platform
#   [realtime]  audio-path code run inside a real-time section; fails on any
#               allocation (all builds) or any lock/blocking call (RTSan build)
#   [regression] fixed settings rendered and compared with the stored
#               measurements in fixtures/audio/regression.json
#
#   ctest --test-dir build -C <config> -L unit
#   ctest --test-dir build -C <config> -L realtime
#   ctest --test-dir build -C <config> -L regression

juce_add_console_app(ThunderTests
    PRODUCT_NAME "ThunderTests")

target_sources(ThunderTests PRIVATE
    support/AllocationCounter.cpp
    support/JuceEnvironment.cpp
    support/Realtime.cpp
    support/Realtime.h
    support/TestSignals.h
    support/TestHost.h
    support/TestSettings.h
    audio/LatencyAlignmentTests.cpp
    audio/RegressionSuite.cpp
    audio/RegressionSuite.h
    audio/RegressionTests.cpp
    unit/CaptureTests.cpp
    unit/DesignSystemTests.cpp
    unit/DependencyTests.cpp
    unit/EditorTests.cpp
    unit/EngineTests.cpp
    unit/GainTests.cpp
    unit/GrainModuleTests.cpp
    unit/IntervalFollowerTests.cpp
    unit/KeyDetectionTests.cpp
    unit/ModuleSlotTests.cpp
    unit/MorphTests.cpp
    unit/PerformTests.cpp
    unit/ModuleExtrasTests.cpp
    unit/ModuleDisplayTests.cpp
    unit/LightningTests.cpp
    unit/MotionTests.cpp
    unit/MusicTheoryTests.cpp
    unit/ParamTests.cpp
    unit/PresetLibraryTests.cpp
    unit/PresetTests.cpp
    unit/PitchModuleTests.cpp
    unit/PitchTrackerTests.cpp
    unit/ProcessorTests.cpp
    unit/RhythmModuleTests.cpp
    unit/SourceTests.cpp
    unit/SourceUiTests.cpp
    unit/SpaceModuleTests.cpp
    unit/StateTests.cpp
    unit/TransportStateTests.cpp
    unit/ToneModuleTests.cpp
    unit/UtilTests.cpp
    unit/VisualiserTests.cpp
    unit/WidthModuleTests.cpp
    realtime/DependencyRealtimeTests.cpp
    realtime/EngineRealtimeTests.cpp
    realtime/MusicRealtimeTests.cpp
    realtime/RealtimeCanary.cpp
    measure/Benchmark.cpp
    measure/ChainMeasure.cpp
    measure/EditorMeasure.cpp
    measure/EditorSnapshot.cpp
    measure/GrainMeasure.cpp
    measure/PitchConfigMeasure.cpp
    measure/PitchModesMeasure.cpp
    measure/PitchTrackerMeasure.cpp
    measure/ResourceMeasure.cpp
    measure/SpaceMeasure.cpp)

target_include_directories(ThunderTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")

# Saved states and presets from earlier versions (QA-STATE-001).
target_compile_definitions(ThunderTests PRIVATE
    THUNDER_TEST_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")

target_link_libraries(ThunderTests PRIVATE
    ThunderPluginCode
    Catch2::Catch2WithMain)

thunder_disable_network_features(ThunderTests)
thunder_apply_compiler_settings(ThunderTests)

set_target_properties(ThunderTests PROPERTIES FOLDER "tests")

include(Catch)
# A test that stops responding fails after 15 minutes instead of holding up the
# whole run. The slowest test takes about 5.5 minutes under the sanitizers.
catch_discover_tests(ThunderTests
    ADD_TAGS_AS_LABELS
    DISCOVERY_MODE PRE_TEST
    PROPERTIES TIMEOUT 900)
