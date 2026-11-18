# ThunderTests — the test runner for ThunderCore (no plugin wrapper, no GUI).
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
    support/Realtime.h
    support/Realtime.cpp
    support/AllocationCounter.cpp
    unit/DependencyTests.cpp
    realtime/DependencyRealtimeTests.cpp
    realtime/RealtimeCanary.cpp)

target_include_directories(ThunderTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")

target_link_libraries(ThunderTests PRIVATE
    ThunderCore
    Catch2::Catch2WithMain)

thunder_disable_network_features(ThunderTests)
thunder_apply_compiler_settings(ThunderTests)

set_target_properties(ThunderTests PROPERTIES FOLDER "tests")

include(Catch)
catch_discover_tests(ThunderTests
    ADD_TAGS_AS_LABELS
    DISCOVERY_MODE PRE_TEST)
