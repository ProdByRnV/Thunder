#include "ThunderFonts.h"

#if THUNDER_EMBEDDED_FONTS
#include <ThunderFontData.h>
#endif

namespace thunder::ui::fonts
{

namespace
{
#if THUNDER_EMBEDDED_FONTS
juce::Typeface::Ptr load (const char* data, int size)
{
    return juce::Typeface::createSystemTypefaceFor (data, static_cast<std::size_t> (size));
}

struct Typefaces
{
    juce::Typeface::Ptr regular = load (ThunderFontData::InterRegular_ttf, ThunderFontData::InterRegular_ttfSize);
    juce::Typeface::Ptr medium = load (ThunderFontData::InterMedium_ttf, ThunderFontData::InterMedium_ttfSize);
    juce::Typeface::Ptr semiBold = load (ThunderFontData::InterSemiBold_ttf, ThunderFontData::InterSemiBold_ttfSize);
    juce::Typeface::Ptr mono =
        load (ThunderFontData::JetBrainsMonoMedium_ttf, ThunderFontData::JetBrainsMonoMedium_ttfSize);
};

const Typefaces& typefaces()
{
    static const Typefaces t;
    return t;
}
#endif
} // namespace

juce::Font ui (float height, Weight weight)
{
#if THUNDER_EMBEDDED_FONTS
    const auto& t = typefaces();
    const auto& face = weight == Weight::semiBold ? t.semiBold : weight == Weight::medium ? t.medium : t.regular;
    return juce::Font (juce::FontOptions (face).withHeight (height));
#else
    return juce::Font (juce::FontOptions (height, weight == Weight::regular ? juce::Font::plain : juce::Font::bold));
#endif
}

juce::Font mono (float height)
{
#if THUNDER_EMBEDDED_FONTS
    return juce::Font (juce::FontOptions (typefaces().mono).withHeight (height));
#else
    return juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), height, juce::Font::plain));
#endif
}

bool areEmbedded() noexcept
{
#if THUNDER_EMBEDDED_FONTS
    return true;
#else
    return false;
#endif
}

} // namespace thunder::ui::fonts
