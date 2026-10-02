#include "ocean/ocean_clock.h"

#include <cmath>

namespace ludus::sandbox::ocean
{
namespace
{
constexpr float64 kPi = 3.14159265358979323846;

[[nodiscard]] float64 WrapPositive(float64 value, float64 period) noexcept
{
    if (period <= 0.0)
    {
        return value;
    }
    float64 wrapped = std::fmod(value, period);
    if (wrapped < 0.0)
    {
        wrapped += period;
    }
    return wrapped;
}
} // namespace

float64 SanitizeDelta(float64 rawDeltaSeconds) noexcept
{
    if (!std::isfinite(rawDeltaSeconds) || rawDeltaSeconds < 0.0)
    {
        return 0.0;
    }
    if (rawDeltaSeconds > kMaxResumeDeltaSeconds)
    {
        return kMaxResumeDeltaSeconds;
    }
    return rawDeltaSeconds;
}

void Advance(SceneClock& clock, const OceanSettings& settings, float64 rawDeltaSeconds, bool visible) noexcept
{
    // Freeze while hidden or paused: draw continues, time does not move.
    if (!visible || clock.Paused)
    {
        return;
    }

    const float64 delta = SanitizeDelta(rawDeltaSeconds);
    if (delta <= 0.0)
    {
        return;
    }

    // Wave animation advances by delta scaled by the (sanitized) animation
    // speed, wrapped to preserve precision over long sessions.
    const float64 animationSpeed = static_cast<float64>(settings.WaveAnimationSpeed);
    clock.WaveTime = WrapPositive(clock.WaveTime + delta * animationSpeed, kWavePhaseWrap);

    // Current displacement integrates the current velocity continuously.
    const float64 directionRadians = static_cast<float64>(settings.CurrentDirectionDegrees) * (kPi / 180.0);
    const float64 speed = static_cast<float64>(settings.CurrentSpeedMetersPerSecond);
    const float64 vx = speed * std::cos(directionRadians);
    const float64 vy = speed * std::sin(directionRadians);

    clock.CurrentOffsetX = WrapPositive(clock.CurrentOffsetX + vx * delta, kCurrentWrapMeters);
    clock.CurrentOffsetY = WrapPositive(clock.CurrentOffsetY + vy * delta, kCurrentWrapMeters);
}

void ResetTime(SceneClock& clock) noexcept
{
    clock.WaveTime = 0.0;
    clock.CurrentOffsetX = 0.0;
    clock.CurrentOffsetY = 0.0;
}

} // namespace ludus::sandbox::ocean
