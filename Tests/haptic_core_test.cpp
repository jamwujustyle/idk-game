#include "HapticCore.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>

using namespace ronin;

namespace
{
int checks = 0;

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

bool Near(float a, float b)
{
    return std::fabs(a - b) < 0.0001f;
}

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

void EventTimingAndDistinctPatterns()
{
    const HapticEvent events[]{HapticEvent::Swing, HapticEvent::Dodge, HapticEvent::Block,
        HapticEvent::Parry, HapticEvent::Damage, HapticEvent::GuardBreak, HapticEvent::Execute};
    const float durations[]{0.11f, 0.16f, 0.16f, 0.24f, 0.32f, 0.5f, 0.72f};
    HapticOutput initial[7]{};
    for (int index = 0; index < 7; ++index)
    {
        HapticCore core;
        core.SetIntensity(1.f);
        CHECK(core.Trigger(events[index]));
        CHECK(core.IsActive());
        initial[index] = core.GetOutput();
        CHECK(!Silent(initial[index]));
        core.Tick(durations[index] - 0.005f);
        CHECK(core.IsActive());
        const auto tail = core.GetOutput();
        CHECK(tail.leftLarge < initial[index].leftLarge);
        CHECK(tail.rightSmall < initial[index].rightSmall);
        core.Tick(0.006f);
        CHECK(!core.IsActive());
        CHECK(Silent(core.GetOutput()));
        for (int previous = 0; previous < index; ++previous)
            CHECK(!Same(initial[index], initial[previous]));
    }

    HapticCore parry;
    parry.SetIntensity(1.f);
    CHECK(parry.Trigger(HapticEvent::Parry));
    CHECK(Near(parry.GetOutput().rightSmall, 1.f));
    parry.Tick(0.07f);
    CHECK(Silent(parry.GetOutput()));
    CHECK(parry.IsActive());
    parry.Tick(0.025f);
    CHECK(Near(parry.GetOutput().rightSmall, 0.85f));

    HapticCore execute;
    execute.SetIntensity(1.f);
    CHECK(execute.Trigger(HapticEvent::Execute));
    execute.Tick(0.2f);
    CHECK(Near(execute.GetOutput().leftLarge, 0.1f));
    execute.Tick(0.06f);
    CHECK(Near(execute.GetOutput().leftLarge, 0.9f));
}

void PrioritiesAndRetriggering()
{
    HapticCore core;
    CHECK(core.Trigger(HapticEvent::Swing));
    CHECK(core.Trigger(HapticEvent::Block));
    const auto block = core.GetOutput();
    CHECK(!core.Trigger(HapticEvent::Dodge));
    CHECK(Same(block, core.GetOutput()));
    CHECK(core.Trigger(HapticEvent::Damage));
    CHECK(core.Trigger(HapticEvent::Parry));
    CHECK(!core.Trigger(HapticEvent::Damage));
    CHECK(core.Trigger(HapticEvent::GuardBreak));
    CHECK(core.Trigger(HapticEvent::Execute));
    core.Tick(0.3f);
    const auto finisher = core.GetOutput();
    for (auto event : {HapticEvent::Swing, HapticEvent::Dodge, HapticEvent::Block,
        HapticEvent::Parry, HapticEvent::Damage, HapticEvent::GuardBreak})
    {
        CHECK(!core.Trigger(event));
        CHECK(Same(finisher, core.GetOutput()));
    }
    core.Tick(0.43f);
    CHECK(!core.IsActive());
    CHECK(core.Trigger(HapticEvent::Swing));
    const auto start = core.GetOutput();
    core.Tick(0.08f);
    CHECK(!Same(start, core.GetOutput()));
    CHECK(core.Trigger(HapticEvent::Swing));
    CHECK(Same(start, core.GetOutput()));
}

void StrengthIntensityAndBounds()
{
    HapticCore core;
    CHECK(Near(core.GetIntensity(), 0.7f));
    core.SetIntensity(0.5f);
    CHECK(core.Trigger(HapticEvent::Execute, 0.4f));
    CHECK(Near(core.GetOutput().leftLarge, 0.2f));
    core.SetIntensity(2.f);
    CHECK(Near(core.GetIntensity(), 1.f));
    CHECK(Near(core.GetOutput().leftLarge, 0.4f));
    CHECK(core.Trigger(HapticEvent::Execute, 10.f));
    CHECK(Near(core.GetOutput().leftLarge, 1.f));
    CHECK(!core.Trigger(HapticEvent::Execute, -1.f));
    CHECK(!core.Trigger(HapticEvent::Execute, 0.f));
    CHECK(!core.Trigger(HapticEvent::Execute, std::numeric_limits<float>::quiet_NaN()));
    CHECK(Near(core.GetOutput().leftLarge, 1.f));
    core.SetIntensity(-1.f);
    CHECK(Silent(core.GetOutput()));
    CHECK(Near(core.GetIntensity(), 0.f));
    core.SetIntensity(std::numeric_limits<float>::infinity());
    CHECK(Silent(core.GetOutput()));
    core.SetIntensity(std::numeric_limits<float>::quiet_NaN());
    CHECK(Silent(core.GetOutput()));
    core.SetIntensity(1.f);
    CHECK(Near(core.GetOutput().leftLarge, 1.f));

    for (auto event : {HapticEvent::Swing, HapticEvent::Dodge, HapticEvent::Block,
        HapticEvent::Parry, HapticEvent::Damage, HapticEvent::GuardBreak, HapticEvent::Execute})
    {
        core.Stop();
        CHECK(core.Trigger(event, 2.f));
        for (int frame = 0; frame < 190; ++frame)
        {
            CHECK(Normalized(core.GetOutput()));
            core.Tick(1.f / 240.f);
        }
        CHECK(Silent(core.GetOutput()));
    }
}

void DisabledAndStoppedCannotResumeStaleRumble()
{
    HapticCore core;
    CHECK(core.IsEnabled());
    CHECK(!core.IsActive());
    CHECK(Silent(core.GetOutput()));
    CHECK(core.Trigger(HapticEvent::Execute));
    core.SetEnabled(false);
    CHECK(!core.IsEnabled());
    CHECK(!core.IsActive());
    CHECK(Silent(core.GetOutput()));
    CHECK(!core.Trigger(HapticEvent::Parry));
    core.SetEnabled(true);
    CHECK(Silent(core.GetOutput()));
    CHECK(!core.IsActive());
    CHECK(core.Trigger(HapticEvent::Damage));
    core.Stop();
    CHECK(Silent(core.GetOutput()));
    CHECK(!core.IsActive());
    core.Tick(0.1f);
    CHECK(Silent(core.GetOutput()));
    CHECK(core.Trigger(HapticEvent::Swing));
}

void FrameSizesAndInvalidInputs()
{
    HapticCore one;
    HapticCore many;
    CHECK(one.Trigger(HapticEvent::GuardBreak));
    CHECK(many.Trigger(HapticEvent::GuardBreak));
    one.Tick(0.25f);
    for (int frame = 0; frame < 30; ++frame)
        many.Tick(1.f / 120.f);
    CHECK(Same(one.GetOutput(), many.GetOutput()));
    const auto before = one.GetOutput();
    one.Tick(-1.f);
    one.Tick(0.f);
    one.Tick(std::numeric_limits<float>::quiet_NaN());
    one.Tick(std::numeric_limits<float>::infinity());
    CHECK(Same(before, one.GetOutput()));
    CHECK(!one.Trigger(static_cast<HapticEvent>(255)));
    CHECK(Same(before, one.GetOutput()));
    one.Tick(std::numeric_limits<float>::max());
    CHECK(Silent(one.GetOutput()));
    CHECK(!one.IsActive());
}
} // namespace

int main()
{
    EventTimingAndDistinctPatterns();
    PrioritiesAndRetriggering();
    StrengthIntensityAndBounds();
    DisabledAndStoppedCannotResumeStaleRumble();
    FrameSizesAndInvalidInputs();
    std::cout << "Haptic core: " << checks << " checks passed.\n";
}
