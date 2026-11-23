// How long the editor takes to paint (hidden: run with "[uimeasure]").
//
// Paints the whole editor into an image at 100% and 200%, once per module,
// and prints the average time per frame: with JUCE's software renderer (every
// pixel on the CPU) and with the platform's native image type (Direct2D on
// Windows, Core Graphics on macOS).

#include "plugin/ThunderEditor.h"
#include "plugin/ThunderProcessor.h"
#include "plugin/ui/regions/ModulePanels.h"
#include "support/TestHost.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <iostream>

using namespace thunder;

namespace
{
double averagePaintMs (juce::Component& c, const juce::ImageType& type, float scale, int frames)
{
    const auto w = juce::roundToInt (static_cast<float> (c.getWidth()) * scale);
    const auto h = juce::roundToInt (static_cast<float> (c.getHeight()) * scale);
    juce::Image image (type.create (juce::Image::ARGB, w, h, true));

    const auto paintOnce = [&]
    {
        juce::Graphics g (image);
        g.addTransform (juce::AffineTransform::scale (scale));
        c.paintEntireComponent (g, true);
    };

    paintOnce(); // untimed: fonts and caches load on first use

    const auto start = juce::Time::getMillisecondCounterHiRes();
    for (int i = 0; i < frames; ++i)
        paintOnce();
    return (juce::Time::getMillisecondCounterHiRes() - start) / frames;
}
} // namespace

TEST_CASE ("Editor paint cost", "[.][uimeasure]")
{
    ThunderProcessor processor;
    processor.prepareToPlay (48000.0, 512);
    std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditorAndMakeActive());
    auto& e = dynamic_cast<ThunderEditor&> (*editor);

    constexpr int frames = 30;
    const juce::SoftwareImageType software;
    const juce::NativeImageType native;

    for (const auto& [name, type] :
         {std::pair<const char*, const juce::ImageType*>{"software", &software}, {"native", &native}})
    {
        for (const auto scale : {1.0f, 2.0f})
        {
            double total = 0.0;
            for (int i = 0; i < e.getNumModules(); ++i)
            {
                e.selectModule (i);
                total += averagePaintMs (e, *type, scale, frames);
            }
            std::cout << "Full editor repaint, " << name << ", " << juce::roundToInt (scale * 100.0f)
                      << "%: " << juce::String (total / e.getNumModules(), 2) << " ms per frame\n";
        }
    }

    processor.editorBeingDeleted (editor.get());
}

namespace
{
// Times `run` (ms), best of `repeats`.
template<typename Run> double bestMs (int repeats, Run&& run)
{
    auto best = 1.0e9;
    for (int r = 0; r < repeats; ++r)
    {
        const auto start = juce::Time::getMillisecondCounterHiRes();
        run();
        best = std::min (best, juce::Time::getMillisecondCounterHiRes() - start);
    }
    return best;
}
} // namespace

TEST_CASE ("Visualiser frame cost and the audio thread's share", "[.][uimeasure]")
{
    constexpr double rate = 48000.0;
    constexpr int frames = 60;

    ThunderProcessor processor;
    processor.prepareToPlay (rate, 512);
    test::TestPlayHead head;
    processor.setPlayHead (&head);
    auto& apvts = processor.getValueTreeState();
    for (const auto& [id, value] : {std::pair<const char*, float>{"grainPitchSpread", 7.0f}, {"grainDensity", 60.0f}})
    {
        auto* p = apvts.getParameter (id);
        p->setValueNotifyingHost (p->convertTo0to1 (value));
    }

    std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditorAndMakeActive());
    auto& e = dynamic_cast<ThunderEditor&> (*editor);
    e.selectModule (2);
    const auto frame = [&e] { e.updateVisuals (true, 512.0 / rate); };
    e.updateVisuals (true, 0.0);
    test::playMelody (processor, head, 6.0, rate, 512, frame);

    // One frame: 1/60 s of audio arrives (not timed), the editor reads it, then
    // what repaints is painted: the visible view and the meters.
    const auto measureView = [&] (const char* name, juce::Component& view)
    {
        auto updateMs = 0.0;
        for (int i = 0; i < frames; ++i)
        {
            test::playMelody (processor, head, 1.0 / 60.0, rate, 800);
            const auto start = juce::Time::getMillisecondCounterHiRes();
            e.updateVisuals (true, 1.0 / 60.0);
            updateMs += juce::Time::getMillisecondCounterHiRes() - start;
        }
        updateMs /= frames;

        const juce::SoftwareImageType software;
        const juce::NativeImageType native;
        const std::array<std::pair<const char*, const juce::ImageType*>, 2> renderers{
            {{"software", &software}, {"native", &native}}};

        for (const auto scale : {1.0f, 2.0f})
            for (const auto& entry : renderers)
            {
                const auto* renderer = entry.first;
                const auto& type = *entry.second;
                const auto paintMs = averagePaintMs (view, type, scale, frames) +
                                     averagePaintMs (e.getOutputPanel().getMeter(), type, scale, frames);
                const auto total = updateMs + paintMs;
                std::cout << name << ", " << renderer << ", " << juce::roundToInt (scale * 100.0f) << "%: read "
                          << juce::String (updateMs, 2) << " ms + paint " << juce::String (paintMs, 2)
                          << " ms = " << juce::String (total, 2) << " ms per frame; at 60 fps "
                          << juce::String (total * 60.0 / 10.0, 1) << "% of a core\n";
            }
    };

    measureView ("Waveform + grains", e.getVisualiser().getWaveform());
    e.showSpectrum();
    e.updateVisuals (true, 0.0);
    test::playMelody (processor, head, 0.5, rate, 512, frame);
    measureView ("Spectrum", e.getVisualiser().getSpectrum());
    e.selectModule (5);
    measureView ("Spectrum + Tone curve", e.getTonePanel().getCurve());

    // The module panels' live displays (each with the meters, as above), with
    // the visualiser back on the waveform.
    e.getVisualiser().getViewToggle().choose (0);
    for (const auto& [id, value] : {std::pair<const char*, float>{"staticOn", 1.0f},
                                    {"staticAmount", 80.0f},
                                    {"shapeOn", 1.0f},
                                    {"duckOn", 1.0f}})
    {
        auto* p = apvts.getParameter (id);
        p->setValueNotifyingHost (p->convertTo0to1 (value));
    }
    e.selectModule (2);
    measureView ("Grain cloud", dynamic_cast<ui::GrainsPanel&> (e.getModulePanel (2)).getCloud());
    e.selectModule (3);
    measureView ("Static trace", dynamic_cast<ui::StaticPanel&> (e.getModulePanel (3)).getTrace());
    e.selectModule (7);
    measureView ("Shape display", dynamic_cast<ui::ShapePanel&> (e.getModulePanel (7)).getDisplay());
    e.selectModule (8);
    measureView ("Duck meter", dynamic_cast<ui::DuckPanel&> (e.getModulePanel (8)).getMeter());

    // Lightning: three bolts at full brightness over the waveform. The first
    // paint of each bolt blurs its glow; later frames draw the cached image.
    {
        auto& bolts = e.getVisualiser().getLightning();
        const juce::SoftwareImageType software;
        for (const auto scale : {1.0f, 2.0f})
        {
            for (int i = 0; i < 3; ++i)
                bolts.strike (0.5f, 1.0f);
            const auto start = juce::Time::getMillisecondCounterHiRes();
            averagePaintMs (bolts, software, scale, 1);
            const auto firstMs = juce::Time::getMillisecondCounterHiRes() - start;
            const auto frameMs = averagePaintMs (bolts, software, scale, frames) +
                                 averagePaintMs (e.getVisualiser().getWaveform(), software, scale, frames);
            std::cout << "Lightning (3 bolts) over the waveform, software, " << juce::roundToInt (scale * 100.0f)
                      << "%: first paint " << juce::String (firstMs, 2) << " ms (glow blur), then "
                      << juce::String (frameMs, 2) << " ms per frame\n";
        }
    }

    // Lightning while playing (T-0.7-07): Static at 100% (the most strikes),
    // 10 s of the melody; each frame reads the data and paints the whole
    // visualiser (more than the real dirty-region repaint), software renderer.
    {
        auto* amount = apvts.getParameter ("staticAmount");
        amount->setValueNotifyingHost (1.0f);
        e.selectModule (0);
        const juce::SoftwareImageType software;
        auto& region = e.getVisualiser();
        for (const auto fps : {60, 30})
        {
            for (const auto scale : {1.0f, 2.0f})
            {
                const auto strikesBefore = region.getLightning().getStrikeCount();
                juce::Image image (software.create (
                    juce::Image::ARGB, juce::roundToInt (static_cast<float> (region.getWidth()) * scale),
                    juce::roundToInt (static_cast<float> (region.getHeight()) * scale), true));
                auto totalMs = 0.0;
                auto worstMs = 0.0;
                const auto count = 10 * fps;
                for (int i = 0; i < count; ++i)
                {
                    test::playMelody (processor, head, 1.0 / fps, rate, 48000 / fps);
                    const auto start = juce::Time::getMillisecondCounterHiRes();
                    e.updateVisuals (true, 1.0 / fps);
                    {
                        juce::Graphics g (image);
                        g.addTransform (juce::AffineTransform::scale (scale));
                        region.paintEntireComponent (g, false);
                    }
                    const auto ms = juce::Time::getMillisecondCounterHiRes() - start;
                    totalMs += ms;
                    worstMs = std::max (worstMs, ms);
                }
                const auto average = totalMs / count;
                std::cout << "Visualiser with lightning (Static 100%), " << fps << " fps, "
                          << juce::roundToInt (scale * 100.0f) << "%: " << juce::String (average, 2)
                          << " ms per frame (worst " << juce::String (worstMs, 1) << " ms), "
                          << juce::String (average * fps / 10.0, 1) << "% of a core; "
                          << (region.getLightning().getStrikeCount() - strikesBefore) << " strikes in 10 s\n";
            }
        }
    }

    processor.setPlayHead (nullptr);
    processor.editorBeingDeleted (editor.get());
    editor.reset();

    // The audio thread: 10 s of the melody, editor closed vs. everything shown.
    for (const auto parts : {0u, UiBridge::kFrames | UiBridge::kSpectrum | UiBridge::kGrains})
    {
        ThunderProcessor p;
        p.prepareToPlay (rate, 512);
        test::TestPlayHead h;
        p.setPlayHead (&h);
        p.getUiBridge().setWanted (parts);
        std::vector<visual::WaveFrame> drainFrames (2048);
        std::vector<visual::SpectrumSample> drainSamples (8192);
        visual::GrainSnapshot drainGrains;
        const auto drain = [&]
        {
            while (p.getUiBridge().readFrames (drainFrames.data(), 2048) > 0)
            {
            }
            while (p.getUiBridge().readSpectrum (drainSamples.data(), 8192) > 0)
            {
            }
            while (p.getUiBridge().readGrains (drainGrains))
            {
            }
        };
        const auto ms = bestMs (3, [&] { test::playMelody (p, h, 10.0, rate, 512, drain); });
        std::cout << "Audio thread, " << (parts == 0 ? "editor closed" : "visualiser showing everything") << ": "
                  << juce::String (ms / 100.0, 2) << "% of a core (incl. the test's melody and draining)\n";
        p.setPlayHead (nullptr);
    }
}
