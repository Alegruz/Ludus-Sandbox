#include "ocean/ocean_uniforms.h"

#include <cmath>

namespace ludus::sandbox::ocean
{
namespace
{
constexpr float32 kPi = 3.14159265358979323846F;
} // namespace

OceanUniforms
BuildUniforms(const OceanSettings& settings, const SceneClock& clock, uint32 widthPx, uint32 heightPx) noexcept
{
    const OceanSettings s = Clamp(settings);

    const float32 viewHeight = kWorldViewHeightMeters;
    const float32 viewWidth = WorldViewWidthMeters(widthPx, heightPx);

    const float32 directionRadians = s.CurrentDirectionDegrees * (kPi / 180.0F);

    OceanUniforms u{};
    u.Resolution[0] = static_cast<float32>(widthPx);
    u.Resolution[1] = static_cast<float32>(heightPx);
    u.WorldView[0] = viewWidth;
    u.WorldView[1] = viewHeight;

    u.WaveTime = static_cast<float32>(clock.WaveTime);
    u.PadA = 0.0F;
    u.CurrentOffset[0] = static_cast<float32>(clock.CurrentOffsetX);
    u.CurrentOffset[1] = static_cast<float32>(clock.CurrentOffsetY);

    u.WaveScaleMeters = s.WaveScaleMeters;
    u.WaveAnimationSpeed = s.WaveAnimationSpeed;
    u.WaveIntensity = s.WaveIntensity;
    u.FoamAmount = s.FoamAmount;

    u.CurrentDirCos = std::cos(directionRadians);
    u.CurrentDirSin = std::sin(directionRadians);
    u.CameraX = 0.0F;
    u.CameraY = 0.0F;

    u.RiverTime = 0.0F;
    u.PadB[0] = 0.0F;
    u.PadB[1] = 0.0F;
    u.PadB[2] = 0.0F;

    const auto writeColor = [](float32(&dst)[4], const Color& c) noexcept {
        dst[0] = c.R;
        dst[1] = c.G;
        dst[2] = c.B;
        dst[3] = 1.0F;
    };
    writeColor(u.DeepColor, s.DeepColor);
    writeColor(u.MidColor, s.MidColor);
    writeColor(u.ShallowColor, s.ShallowColor);
    writeColor(u.FoamColor, s.FoamColor);

    return u;
}

} // namespace ludus::sandbox::ocean
