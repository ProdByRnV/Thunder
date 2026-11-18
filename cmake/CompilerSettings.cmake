# Compiler settings shared by Thunder's own targets.
#
# Never add -ffast-math or /fp:fast here: they break NaN/Inf detection, which the
# audio path relies on to sanitise bad input.

option(THUNDER_WARNINGS_AS_ERRORS "Treat compiler warnings as errors (used by CI)" OFF)

function(thunder_apply_compiler_settings target)
    target_link_libraries(${target} PRIVATE
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

    if(MSVC)
        # /bigobj: Signalsmith's template-heavy headers exceed the default section count.
        # /utf-8:  sources are UTF-8.
        target_compile_options(${target} PRIVATE /bigobj /utf-8 /permissive-)
    endif()

    if(THUNDER_WARNINGS_AS_ERRORS)
        if(MSVC)
            target_compile_options(${target} PRIVATE /WX)
        else()
            target_compile_options(${target} PRIVATE -Werror)
        endif()
    endif()
endfunction()

# Thunder never uses the network (and must not need libcurl or a web view to build).
function(thunder_disable_network_features target)
    target_compile_definitions(${target} PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0)
endfunction()
