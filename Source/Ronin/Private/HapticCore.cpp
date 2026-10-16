#include "HapticCore.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace ronin
{
namespace
{
struct Keyframe
{
    float time;
    float large;
    float small;
};

struct Envelope
{
    const Keyframe* frames;
    std::size_t count;
    int priority;

    float Duration() const { return frames[count - 1].time; }
};

template <std::size_t Count>
Envelope MakeEnvelope(const Keyframe (&frames)[Count], int priority)
{
    return {frames, Count, priority};
}

Envelope GetEnvelope(HapticEvent event)
{
    // Shape the two motor frequencies independently. UE's Linux Xbox path takes
    // max(leftLarge, rightLarge) and max(leftSmall, rightSmall), so side balance
    // alone cannot distinguish events on that controller.
    static constexpr Keyframe swing[]{{0.f, .13f, .32f}, {.015f, .1f, .24f}, {.065f, 0.f, 0.f}};
    static constexpr Keyframe heavySwing[]{{0.f, .38f, .24f}, {.025f, .28f, .13f}, {.095f, 0.f, 0.f}};
    static constexpr Keyframe dodge[]{{0.f, .16f, .03f}, {.015f, .12f, .02f}, {.055f, 0.f, 0.f}};
    static constexpr Keyframe perfectDodge[]{{0.f, .06f, .62f}, {.025f, .12f, .3f},
        {.09f, .04f, .12f}, {.12f, 0.f, 0.f}};
    static constexpr Keyframe hit[]{{0.f, .85f, .25f}, {.025f, .7f, .08f},
        {.065f, .36f, .02f}, {.14f, 0.f, 0.f}};
    static constexpr Keyframe block[]{{0.f, .45f, .85f}, {.018f, .32f, .45f},
        {.045f, .12f, .15f}, {.105f, 0.f, 0.f}};
    static constexpr Keyframe damage[]{{0.f, 1.f, .35f}, {.035f, 1.f, .15f},
        {.095f, .55f, .02f}, {.24f, 0.f, 0.f}};
    // A zero plateau longer than two 60 Hz frames preserves the double tap even
    // when a low-priority swing/dodge arrives during the gap.
    static constexpr Keyframe parry[]{{0.f, .3f, 1.f}, {.016f, .2f, .85f},
        {.04f, 0.f, 0.f}, {.075f, 0.f, 0.f}, {.09f, .16f, .9f},
        {.115f, .08f, .35f}, {.18f, 0.f, 0.f}};
    static constexpr Keyframe guardBreak[]{{0.f, 1.f, .65f}, {.035f, .8f, .3f},
        {.08f, .25f, .05f}, {.115f, .7f, .25f}, {.16f, .4f, .1f}, {.29f, 0.f, 0.f}};
    static constexpr Keyframe execute[]{{0.f, 1.f, .8f}, {.045f, .9f, .4f},
        {.1f, .4f, .08f}, {.14f, 0.f, 0.f}, {.18f, .8f, .45f},
        {.24f, .5f, .16f}, {.4f, 0.f, 0.f}};
    static constexpr Keyframe silent[]{{0.f, 0.f, 0.f}};

    switch (event)
    {
    case HapticEvent::Swing: return MakeEnvelope(swing, 1);
    case HapticEvent::HeavySwing: return MakeEnvelope(heavySwing, 1);
    case HapticEvent::Dodge: return MakeEnvelope(dodge, 1);
    case HapticEvent::Hit: return MakeEnvelope(hit, 2);
    case HapticEvent::Block: return MakeEnvelope(block, 3);
    case HapticEvent::PerfectDodge: return MakeEnvelope(perfectDodge, 4);
    case HapticEvent::Damage: return MakeEnvelope(damage, 5);
    case HapticEvent::Parry: return MakeEnvelope(parry, 6);
    case HapticEvent::GuardBreak: return MakeEnvelope(guardBreak, 7);
    case HapticEvent::Execute: return MakeEnvelope(execute, 8);
    }
    return MakeEnvelope(silent, 0);
}

float Unit(float value)
{
    return std::isfinite(value) ? std::clamp(value, 0.f, 1.f) : 0.f;
}

HapticOutput Sample(const Envelope& envelope, float time, float scale)
{
    for (std::size_t index = 1; index < envelope.count; ++index)
    {
        const auto& next = envelope.frames[index];
        if (time <= next.time)
        {
            const auto& previous = envelope.frames[index - 1];
            const float alpha = (time - previous.time) / (next.time - previous.time);
            const float large = Unit((previous.large + alpha * (next.large - previous.large)) * scale);
            const float small = Unit((previous.small + alpha * (next.small - previous.small)) * scale);
            return {large, small, large, small};
        }
    }
    return {};
}
} // namespace

bool HapticCore::Trigger(HapticEvent event, float strength)
{
    strength = Unit(strength);
    const auto incoming = GetEnvelope(event);
    if (!enabled_ || intensity_ <= 0.f || strength <= 0.f || incoming.Duration() <= 0.f)
        return false;
    // Repeated callbacks must not pin an impact at its peak or fill its silent
    // gap. Discarded events never queue; a fresh event can start after decay.
    if (active_ && (event == event_ || incoming.priority < GetEnvelope(event_).priority))
        return false;
    event_ = event;
    elapsed_ = 0.f;
    strength_ = strength;
    active_ = true;
    return true;
}

void HapticCore::Tick(float deltaSeconds)
{
    if (!active_ || !std::isfinite(deltaSeconds) || deltaSeconds <= 0.f)
        return;
    const float duration = GetEnvelope(event_).Duration();
    if (deltaSeconds >= duration - elapsed_)
    {
        Stop();
        return;
    }
    elapsed_ += deltaSeconds;
}

void HapticCore::SetIntensity(float intensity)
{
    intensity_ = Unit(intensity);
    if (intensity_ <= 0.f)
        Stop();
}

void HapticCore::SetEnabled(bool enabled)
{
    enabled_ = enabled;
    if (!enabled)
        Stop();
}

void HapticCore::Stop()
{
    active_ = false;
    elapsed_ = 0.f;
    strength_ = 0.f;
}

HapticOutput HapticCore::GetOutput() const
{
    if (!enabled_ || !active_)
        return {};
    return Sample(GetEnvelope(event_), elapsed_, strength_ * intensity_);
}
} // namespace ronin
