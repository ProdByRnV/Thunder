# ThunderTests — the test runner for ThunderCore and the plugin processor (no
# plugin format wrapper; no window is ever opened).
#
# Tests are tagged in Catch2 and those tags become CTest labels:
#   [unit]      functional tests, run on every platform
#   [realtime]  audio-path code run inside a real-time section; fails on any
#               allocation (all builds) or any lock/blocking call (RTSan build)
#
#   ctest --test-dir build -C <config> -L unit
#   ctest --test-dir build -C <config> -L realtime

juce_add_console_app(ThunderTests
    PRODUCT_NAME "ThunderTests")

target_sources(ThunderTests PRIVATE
    support/AllocationCounter.cpp
    support/JuceEnvironment.cpp
    support/Realtime.cpp
    support/Realtime.h
    support/TestSignals.h
    audio/LatencyAlignmentTests.cpp
    unit/CaptureTests.cpp
    unit/DependencyTests.cpp
    unit/EditorTests.cpp
    unit/EngineTests.cpp
    unit/GainTests.cpp
    unit/GrainModuleTests.cpp
    unit/IntervalFollowerTests.cpp
    unit/ModuleSlotTests.cpp
    unit/MusicTheoryTests.cpp
    unit/ParamTests.cpp
    unit/PresetTests.cpp
    unit/PitchModuleTests.cpp
    unit/PitchTrackerTests.cpp
    unit/ProcessorTests.cpp
    unit/SpaceModuleTests.cpp
    unit/StateTests.cpp
    unit/TransportStateTests.cpp
    unit/ToneModuleTests.cpp
    unit/UtilTests.cpp
    unit/WidthModuleTests.cpp
    realtime/DependencyRealtimeTests.cpp
    realtime/EngineRealtimeTests.cpp
    realtime/MusicRealtimeTests.cpp
    realtime/RealtimeCanary.cpp
    measure/ChainMeasure.cpp
    measure/EditorSnapshot.cpp
    measure/GrainMeasure.cpp
    measure/PitchConfigMeasure.cpp
    measure/PitchModesMeasure.cpp
    measure/PitchTrackerMeasure.cpp)

target_include_directories(ThunderTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")

target_link_libraries(ThunderTests PRIVATE
    ThunderPluginCode
    Catch2::Catch2WithMain)

thunder_disable_network_features(ThunderTests)
thunder_apply_compiler_settings(ThunderTests)

set_target_properties(ThunderTests PROPERTIES FOLDER "tests")

include(Catch)
catch_discover_tests(ThunderTests
    ADD_TAGS_AS_LABELS
    DISCOVERY_MODE PRE_TEST)
