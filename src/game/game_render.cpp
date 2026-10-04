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
    const auto& level = simulation.GetLevel();
    const auto camera = game::FitCamera(width, height, level.HalfExtent);
    u.WorldView[0] = static_cast<float32>(camera.Width);
    u.WorldView[1] = static_cast<float32>(camera.Height);
    const auto& boat = simulation.GetBoat();
    const float64 alpha = paused || simulation.Phase() != GamePhase::Playing ? 1.0 : simulation.Alpha();
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
    u.LevelBounds[0] = static_cast<float32>(level.HalfExtent.X);
    u.LevelBounds[1] = static_cast<float32>(level.HalfExtent.Y);
    u.DockInfo[0] = static_cast<float32>(level.DockCenter.X);
    u.DockInfo[1] = static_cast<float32>(level.DockCenter.Y);
    u.DockInfo[2] = static_cast<float32>(level.DockRadius);
    u.DockInfo[3] = static_cast<float32>(level.DockSpeed);
    u.LevelInfo[0] = static_cast<float32>(level.RockCount);
    u.LevelInfo[1] = static_cast<float32>(simulation.Phase());
    u.LevelInfo[2] = static_cast<float32>(simulation.DockProgress());
    u.LevelInfo[3] = level.BoundaryHazard ? 1.0F : 0.0F;
    for (usize i = 0; i < level.RockCount; ++i)
    {
        u.Rocks[i][0] = static_cast<float32>(level.Rocks[i].Center.X);
        u.Rocks[i][1] = static_cast<float32>(level.Rocks[i].Center.Y);
        u.Rocks[i][2] = static_cast<float32>(level.Rocks[i].Radius);
    }
    return u;
}

} // namespace ludus::sandbox::game
