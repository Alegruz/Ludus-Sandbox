#include "game/game_render.h"

namespace ludus::sandbox::game
{
ocean::OceanUniforms BuildUniforms(const ocean::OceanSettings& settings,
                                   const ocean::SceneClock& clock,
                                   uint32 width,
                                   uint32 height,
                                   const RippleGame& simulation,
                                   bool paused) noexcept
{
    auto u = ocean::BuildUniforms(settings, clock, width, height);
    const auto camera = game::FitCamera(width, height);
    u.WorldView[0] = static_cast<float32>(camera.Width);
    u.WorldView[1] = static_cast<float32>(camera.Height);
    const auto& boat = simulation.GetBoat();
    const float64 alpha = paused ? 1.0 : simulation.Alpha();
    u.BoatInfo[0] = static_cast<float32>(boat.PreviousPosition.X + (boat.Position.X - boat.PreviousPosition.X) * alpha);
    u.BoatInfo[1] = static_cast<float32>(boat.PreviousPosition.Y + (boat.Position.Y - boat.PreviousPosition.Y) * alpha);
    u.BoatInfo[2] = static_cast<float32>(boat.Heading);
    u.BoatInfo[3] = static_cast<float32>(game::kBoatRadius);
    u.GameInfo[0] = 1.0F;
    u.GameInfo[2] = static_cast<float32>(simulation.CooldownFraction());
    u.GameInfo[3] = static_cast<float32>(boat.ContactFlash / 0.18);
    usize count = 0;
    for (usize i = 0; i < game::kRippleCapacity; ++i)
    {
        const auto& ripple = simulation.GetRipples()[i];
        if (!ripple.Active)
        {
            continue;
        }
        const float64 age = ripple.PreviousAge + (ripple.Age - ripple.PreviousAge) * alpha;
        u.Ripples[count][0] = static_cast<float32>(ripple.Origin.X);
        u.Ripples[count][1] = static_cast<float32>(ripple.Origin.Y);
        u.Ripples[count][2] = static_cast<float32>(age * game::kRippleSpeed);
        u.Ripples[count][3] = static_cast<float32>(1.0 - age / game::kRippleLifetime);
        ++count;
    }
    u.GameInfo[1] = static_cast<float32>(count);
    return u;
}

} // namespace ludus::sandbox::game
