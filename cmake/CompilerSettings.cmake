# Compiler settings shared by Thunder's own targets.
#
# Never add -ffast-math or /fp:fast here: they break NaN/Inf detection, which the
# audio path relies on to sanitise bad input.

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
endfunction()
