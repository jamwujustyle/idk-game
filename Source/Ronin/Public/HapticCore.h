#pragma once

#include <cstdint>

namespace ronin
{
enum class HapticEvent : std::uint8_t
{
    Swing, HeavySwing, Dodge, PerfectDodge, Hit, Block, Parry, Damage, GuardBreak, Execute
};

struct HapticOutput
{
    float leftLarge = 0.f;
    float leftSmall = 0.f;
    float rightLarge = 0.f;
    float rightSmall = 0.f;
};

// Portable standard-rumble envelopes. The engine owns controller delivery and
// must call Stop on pause, restart, controller loss, and gameplay shutdown.
class HapticCore
{
public:
    bool Trigger(HapticEvent event, float strength = 1.f);
    void Tick(float deltaSeconds);
    void SetIntensity(float intensity);
    void SetEnabled(bool enabled);
    void Stop();

    HapticOutput GetOutput() const;
    bool IsActive() const { return active_; }
    bool IsEnabled() const { return enabled_; }
    float GetIntensity() const { return intensity_; }

private:
    HapticEvent event_ = HapticEvent::Swing;
    float elapsed_ = 0.f;
    float strength_ = 0.f;
    float intensity_ = 0.7f;
    bool active_ = false;
    bool enabled_ = true;
};
} // namespace ronin
