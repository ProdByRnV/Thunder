#include "Motion.h"

#include "core/state/UserSettings.h"

#include <juce_core/juce_core.h>

#if JUCE_WINDOWS
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace thunder::ui::reduceMotion
{

namespace
{
bool reducedFlag = false;
} // namespace

bool isOn() noexcept
{
    return reducedFlag;
}

void setOn (bool on) noexcept
{
    reducedFlag = on;
}

bool systemPrefers()
{
#if JUCE_WINDOWS
    BOOL animations = TRUE;
    if (SystemParametersInfoW (SPI_GETCLIENTAREAANIMATION, 0, &animations, 0) != 0)
        return animations == FALSE;
#endif
    return false;
}

bool wanted (const state::UserSettings& settings)
{
    return settings.reduceMotionChosen ? settings.reduceMotion : systemPrefers();
}

} // namespace thunder::ui::reduceMotion
