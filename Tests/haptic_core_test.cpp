#include "HapticCore.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>

using namespace ronin;

namespace
{
int checks = 0;
constexpr HapticEvent Events[]{HapticEvent::Swing, HapticEvent::HeavySwing, HapticEvent::Dodge,
    HapticEvent::PerfectDodge, HapticEvent::Hit, HapticEvent::Block, HapticEvent::Parry,
    HapticEvent::Damage, HapticEvent::GuardBreak, HapticEvent::Execute};

void Check(bool condition, const char* expression, int line)
{
    ++checks;
    if (!condition)
    {
        std::cerr << "FAIL line " << line << ": " << expression << '\n';
        std::exit(1);
    }
}

#define CHECK(expression) Check((expression), #expression, __LINE__)

bool Near(float a, float b) { return std::fabs(a - b) < 0.0001f; }

bool Silent(HapticOutput output)
{
    return output.leftLarge == 0.f && output.leftSmall == 0.f &&
        output.rightLarge == 0.f && output.rightSmall == 0.f;
}

bool Same(HapticOutput a, HapticOutput b)
{
    return Near(a.leftLarge, b.leftLarge) && Near(a.leftSmall, b.leftSmall) &&
        Near(a.rightLarge, b.rightLarge) && Near(a.rightSmall, b.rightSmall);
}

bool Normalized(HapticOutput output)
{
    for (float value : {output.leftLarge, output.leftSmall, output.rightLarge, output.rightSmall})
        if (!std::isfinite(value) || value < 0.f || value > 1.f)
            return false;
    return true;
}

// The Linux Xbox backend collapses UE's four channels to these two motors.
float Bass(HapticOutput output) { return std::max(output.leftLarge, output.rightLarge); }
float Treble(HapticOutput output) { return std::max(output.leftSmall, output.rightSmall); }

HapticOutput At(HapticEvent event, float time)
{
    HapticCore core;
    core.SetIntensity(1.f);
    CHECK(core.Trigger(event));
    core.Tick(time);
    return core.GetOutput();
}

float Energy(HapticEvent event)
{
    HapticCore core;
    core.SetIntensity(1.f);
    CHECK(core.Trigger(event));
    float energy = 0.f;
    for (int step = 0; step < 500; ++step)
    {
        const auto output = core.GetOutput();
        energy += (Bass(output) + Treble(output)) * .001f;
        core.Tick(.001f);
    }
    CHECK(!core.IsActive());
    CHECK(Silent(core.GetOutput()));
    return energy;
}

void DistinctionSurvivesTwoMotorMapping()
{
    const auto swing = At(HapticEvent::Swing, 0.f);
    const auto heavy = At(HapticEvent::HeavySwing, 0.f);
    const auto dodge = At(HapticEvent::Dodge, 0.f);
    const auto hit = At(HapticEvent::Hit, 0.f);
    const auto block = At(HapticEvent::Block, 0.f);
    const auto damage = At(HapticEvent::Damage, 0.f);
    const auto parry = At(HapticEvent::Parry, 0.f);
    const auto evade = At(HapticEvent::PerfectDodge, 0.f);
    CHECK(Bass(heavy) > 2.f * Bass(swing));
    CHECK(Treble(parry) > 3.f * Bass(parry));
    CHECK(Treble(block) > Bass(block));
    CHECK(Bass(hit) > 3.f * Treble(hit));
    CHECK(Bass(damage) > Bass(hit));
    CHECK(Treble(evade) > 5.f * Treble(dodge));
    // A contact's treble dies before its bass; this is not one scaled waveform.
    const auto hitTail = At(HapticEvent::Hit, .06f);
    CHECK(Treble(hitTail) / Treble(hit) < Bass(hitTail) / Bass(hit));
    CHECK(Energy(HapticEvent::Dodge) < .2f * Energy(HapticEvent::Hit));
    CHECK(Energy(HapticEvent::Swing) < .4f * Energy(HapticEvent::Block));
    CHECK(Energy(HapticEvent::Damage) > Energy(HapticEvent::Hit));
    CHECK(Silent(At(HapticEvent::Dodge, .08f)));
    CHECK(Silent(At(HapticEvent::Swing, .1f)));
    CHECK(Silent(At(HapticEvent::Damage, .3f)));
}

void ParryGapSurvivesFrameRatesAndCompetingEvents()
{
    for (float rate : {30.f, 60.f, 120.f})
    {
        HapticCore core;
        core.SetIntensity(1.f);
        CHECK(core.Trigger(HapticEvent::Parry));
        int pulses = 0;
        bool wasHigh = false;
        for (int frame = 0; frame < 30; ++frame)
        {
            const bool high = Treble(core.GetOutput()) > .2f;
            if (high && !wasHigh) ++pulses;
            wasHigh = high;
            core.Tick(1.f / rate);
        }
        CHECK(pulses == 2);
        CHECK(Silent(core.GetOutput()));
    }
    HapticCore core;
    core.SetIntensity(1.f);
    CHECK(core.Trigger(HapticEvent::Parry));
    core.Tick(.05f);
    CHECK(core.IsActive());
    CHECK(Silent(core.GetOutput()));
    for (auto event : {HapticEvent::Swing, HapticEvent::HeavySwing, HapticEvent::Dodge,
        HapticEvent::Hit, HapticEvent::Block, HapticEvent::Parry})
    {
        CHECK(!core.Trigger(event));
        CHECK(Silent(core.GetOutput()));
    }
    core.Tick(.015f);
    CHECK(Silent(core.GetOutput()));
    core.Tick(.025f);
    CHECK(Treble(core.GetOutput()) > .75f);
    core.Tick(.1f);
    CHECK(!core.IsActive());
    CHECK(Silent(core.GetOutput()));
    core.Tick(.5f);
    CHECK(Silent(core.GetOutput())); // No discarded movement feedback replays.
}

void PrioritiesAndDuplicateCallbacks()
{
    HapticCore core;
    CHECK(core.Trigger(HapticEvent::Swing));
    CHECK(core.Trigger(HapticEvent::HeavySwing)); // A different equal-priority event can replace it.
    CHECK(core.Trigger(HapticEvent::Hit));
    CHECK(core.Trigger(HapticEvent::Block));
    CHECK(core.Trigger(HapticEvent::PerfectDodge));
    CHECK(core.Trigger(HapticEvent::Damage));
    CHECK(core.Trigger(HapticEvent::Parry));
    CHECK(core.Trigger(HapticEvent::GuardBreak));
    CHECK(core.Trigger(HapticEvent::Execute));
    core.Tick(.2f);
    const auto finisher = core.GetOutput();
    for (auto event : Events)
    {
        CHECK(!core.Trigger(event));
        CHECK(Same(finisher, core.GetOutput()));
    }
    core.Tick(.3f);
    CHECK(!core.IsActive());

    for (auto event : Events)
    {
        HapticCore spammed;
        HapticCore once;
        CHECK(spammed.Trigger(event));
        CHECK(once.Trigger(event));
        while (once.IsActive())
        {
            CHECK(!spammed.Trigger(event));
            CHECK(Same(spammed.GetOutput(), once.GetOutput()));
            spammed.Tick(.005f);
            once.Tick(.005f);
        }
        CHECK(Silent(spammed.GetOutput()));
        CHECK(!spammed.IsActive());
        CHECK(spammed.Trigger(event)); // A fresh event after completion is accepted.
    }
}

void StrengthIntensityAndBounds()
{
    HapticCore core;
    CHECK(Near(core.GetIntensity(), .7f));
    core.SetIntensity(.5f);
    CHECK(core.Trigger(HapticEvent::Execute, .4f));
    CHECK(Near(Bass(core.GetOutput()), .2f));
    core.SetIntensity(2.f);
    CHECK(Near(core.GetIntensity(), 1.f));
    CHECK(Near(Bass(core.GetOutput()), .4f));
    const auto before = core.GetOutput();
    CHECK(!core.Trigger(HapticEvent::Execute, -1.f));
    CHECK(!core.Trigger(HapticEvent::Execute, 0.f));
    CHECK(!core.Trigger(HapticEvent::Execute, std::numeric_limits<float>::quiet_NaN()));
    CHECK(Same(before, core.GetOutput()));

    for (auto event : Events)
    {
        core.Stop();
        CHECK(core.Trigger(event, 2.f));
        for (int frame = 0; frame < 150; ++frame)
        {
            CHECK(Normalized(core.GetOutput()));
            core.Tick(1.f / 240.f);
        }
        CHECK(Silent(core.GetOutput()));
        CHECK(!core.IsActive());
    }
}

void MutingDisablingAndStoppingCannotResumeStaleRumble()
{
    HapticCore core;
    CHECK(core.IsEnabled());
    CHECK(!core.IsActive());
    CHECK(Silent(core.GetOutput()));
    CHECK(core.Trigger(HapticEvent::Execute));
    core.SetEnabled(false);
    CHECK(!core.IsEnabled());
    CHECK(!core.IsActive());
    CHECK(!core.Trigger(HapticEvent::Parry));
    core.SetEnabled(true);
    CHECK(Silent(core.GetOutput()));
    CHECK(core.Trigger(HapticEvent::Damage));
    core.Stop();
    core.Tick(.1f);
    CHECK(!core.IsActive());
    CHECK(Silent(core.GetOutput()));
    for (float intensity : {0.f, -1.f, std::numeric_limits<float>::infinity(),
        std::numeric_limits<float>::quiet_NaN()})
    {
        core.SetIntensity(1.f);
        CHECK(core.Trigger(HapticEvent::Execute));
        core.SetIntensity(intensity);
        CHECK(Near(core.GetIntensity(), 0.f));
        CHECK(!core.IsActive());
        CHECK(Silent(core.GetOutput()));
        CHECK(!core.Trigger(HapticEvent::Parry));
        core.SetIntensity(1.f);
        CHECK(Silent(core.GetOutput()));
        CHECK(!core.IsActive());
    }
    CHECK(core.Trigger(HapticEvent::Swing));
}

void FrameSizesAndInvalidInputs()
{
    for (auto event : Events)
    {
        HapticCore one;
        HapticCore many;
        CHECK(one.Trigger(event));
        CHECK(many.Trigger(event));
        one.Tick(.1f);
        for (int frame = 0; frame < 12; ++frame) many.Tick(1.f / 120.f);
        CHECK(Same(one.GetOutput(), many.GetOutput()));
        CHECK(one.IsActive() == many.IsActive());
    }
    HapticCore core;
    CHECK(core.Trigger(HapticEvent::GuardBreak));
    core.Tick(.1f);
    const auto before = core.GetOutput();
    core.Tick(-1.f);
    core.Tick(0.f);
    core.Tick(std::numeric_limits<float>::quiet_NaN());
    core.Tick(std::numeric_limits<float>::infinity());
    CHECK(Same(before, core.GetOutput()));
    CHECK(!core.Trigger(static_cast<HapticEvent>(255)));
    CHECK(Same(before, core.GetOutput()));
    core.Tick(std::numeric_limits<float>::max());
    CHECK(Silent(core.GetOutput()));
    CHECK(!core.IsActive());
}
} // namespace

int main()
{
    DistinctionSurvivesTwoMotorMapping();
    ParryGapSurvivesFrameRatesAndCompetingEvents();
    PrioritiesAndDuplicateCallbacks();
    StrengthIntensityAndBounds();
    MutingDisablingAndStoppingCannotResumeStaleRumble();
    FrameSizesAndInvalidInputs();
    std::cout << "Haptic core: " << checks << " checks passed.\n";
}
