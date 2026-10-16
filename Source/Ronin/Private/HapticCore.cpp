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
    float level;
};

struct Envelope
{
    const Keyframe* frames;
    std::size_t count;
    HapticOutput motors;
    int priority;

    float Duration() const { return frames[count - 1].time; }
};

template <std::size_t Count>
Envelope MakeEnvelope(const Keyframe (&frames)[Count], HapticOutput motors, int priority)
{
    return {frames, Count, motors, priority};
}

Envelope GetEnvelope(HapticEvent event)
{
    static constexpr Keyframe swing[]{{0.f, 0.8f}, {0.025f, 1.f}, {0.11f, 0.f}};
    static constexpr Keyframe dodge[]{{0.f, 0.65f}, {0.035f, 1.f}, {0.16f, 0.f}};
    static constexpr Keyframe block[]{{0.f, 1.f}, {0.025f, 0.9f}, {0.06f, 0.35f}, {0.16f, 0.f}};
    static constexpr Keyframe parry[]{{0.f, 1.f}, {0.04f, 0.15f}, {0.07f, 0.f},
        {0.095f, 0.85f}, {0.14f, 0.1f}, {0.24f, 0.f}};
    static constexpr Keyframe damage[]{{0.f, 0.9f}, {0.06f, 1.f}, {0.12f, 0.55f},
        {0.18f, 0.7f}, {0.32f, 0.f}};
    static constexpr Keyframe guardBreak[]{{0.f, 0.35f}, {0.06f, 1.f}, {0.16f, 0.6f},
        {0.21f, 0.92f}, {0.32f, 0.38f}, {0.5f, 0.f}};
    static constexpr Keyframe execute[]{{0.f, 1.f}, {0.11f, 0.72f}, {0.2f, 0.1f},
        {0.26f, 0.9f}, {0.42f, 0.65f}, {0.72f, 0.f}};
    static constexpr Keyframe silent[]{{0.f, 0.f}};

    switch (event)
    {
    case HapticEvent::Swing: return MakeEnvelope(swing, {0.1f, 0.12f, 0.22f, 0.5f}, 1);
    case HapticEvent::Dodge: return MakeEnvelope(dodge, {0.32f, 0.08f, 0.24f, 0.06f}, 2);
    case HapticEvent::Block: return MakeEnvelope(block, {0.5f, 0.45f, 0.4f, 0.35f}, 3);
    case HapticEvent::Parry: return MakeEnvelope(parry, {0.42f, 0.9f, 0.38f, 1.f}, 5);
    case HapticEvent::Damage: return MakeEnvelope(damage, {0.82f, 0.28f, 0.7f, 0.18f}, 4);
    case HapticEvent::GuardBreak: return MakeEnvelope(guardBreak, {0.92f, 0.5f, 0.85f, 0.45f}, 6);
    case HapticEvent::Execute: return MakeEnvelope(execute, {1.f, 0.6f, 1.f, 0.5f}, 7);
    }
    return MakeEnvelope(silent, {}, 0);
}

float Unit(float value)
{
    return std::isfinite(value) ? std::clamp(value, 0.f, 1.f) : 0.f;
}

float Sample(const Envelope& envelope, float time)
{
    for (std::size_t index = 1; index < envelope.count; ++index)
    {
        const auto& next = envelope.frames[index];
        if (time <= next.time)
        {
            const auto& previous = envelope.frames[index - 1];
            const float alpha = (time - previous.time) / (next.time - previous.time);
            return previous.level + alpha * (next.level - previous.level);
        }
    }
    return 0.f;
}
} // namespace

bool HapticCore::Trigger(HapticEvent event, float strength)
{
    strength = Unit(strength);
    const auto incoming = GetEnvelope(event);
    if (!enabled_ || strength <= 0.f || incoming.Duration() <= 0.f)
        return false;
    if (active_ && incoming.priority < GetEnvelope(event_).priority)
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
    const auto envelope = GetEnvelope(event_);
    const float scale = Unit(Sample(envelope, elapsed_)) * strength_ * intensity_;
    return {Unit(envelope.motors.leftLarge * scale), Unit(envelope.motors.leftSmall * scale),
        Unit(envelope.motors.rightLarge * scale), Unit(envelope.motors.rightSmall * scale)};
}
} // namespace ronin
