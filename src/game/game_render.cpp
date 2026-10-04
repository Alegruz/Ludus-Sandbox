#include "game/game_render.h"

#include <algorithm>
#include <cmath>

namespace ludus::sandbox::game
{
Camera
PresentationCamera(uint32 width, uint32 height, const RippleGame& simulation, bool paused, bool gameplay) noexcept
{
    const auto& boat = simulation.GetBoat();
    const float64 alpha = paused || simulation.Phase() != GamePhase::Playing ? 1.0 : simulation.Alpha();
    const Point position{boat.PreviousPosition.X + (boat.Position.X - boat.PreviousPosition.X) * alpha,
                         boat.PreviousPosition.Y + (boat.Position.Y - boat.PreviousPosition.Y) * alpha};
    return gameplay ? FollowCamera(width, height, simulation.GetLevel(), position)
                    : FitCamera(width, height, simulation.GetLevel().HalfExtent);
}

ocean::OceanUniforms BuildUniforms(const ocean::OceanSettings& settings,
                                   const ocean::SceneClock& clock,
                                   uint32 width,
                                   uint32 height,
                                   const RippleGame& simulation,
                                   bool paused,
                                   bool gameplay) noexcept
{
    auto u = ocean::BuildUniforms(settings, clock, width, height);
    const auto& level = simulation.GetLevel();
    const auto camera = PresentationCamera(width, height, simulation, paused, gameplay);
    u.CameraX = static_cast<float32>(camera.Center.X);
    u.CameraY = static_cast<float32>(camera.Center.Y);
    u.RiverTime = static_cast<float32>(static_cast<float64>(simulation.Ticks()) * kTickSeconds);
    u.WorldView[0] = static_cast<float32>(camera.Width);
    u.WorldView[1] = static_cast<float32>(camera.Height);
    const auto& boat = simulation.GetBoat();
    const float64 alpha = paused || simulation.Phase() != GamePhase::Playing ? 1.0 : simulation.Alpha();
    u.BoatInfo[0] = static_cast<float32>(boat.PreviousPosition.X + (boat.Position.X - boat.PreviousPosition.X) * alpha);
    u.BoatInfo[1] = static_cast<float32>(boat.PreviousPosition.Y + (boat.Position.Y - boat.PreviousPosition.Y) * alpha);
    u.BoatInfo[2] = static_cast<float32>(boat.Heading);
    u.BoatInfo[3] = static_cast<float32>(game::kBoatRadius);
    u.GameInfo[0] = gameplay ? 1.0F : 0.0F;
    u.GameInfo[2] = static_cast<float32>(simulation.CooldownFraction());
    u.GameInfo[3] = static_cast<float32>(boat.ContactFlash / 0.18);
    u.BoatMotion[0] = static_cast<float32>(boat.Velocity.X);
    u.BoatMotion[1] = static_cast<float32>(boat.Velocity.Y);
    u.BoatMotion[2] = static_cast<float32>(std::hypot(boat.Velocity.X, boat.Velocity.Y));
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
    u.FlowInfo[0] = 16.0F;
    u.FlowInfo[1] = 24.0F;
    u.FlowInfo[2] = simulation.GetWater().Active() || (gameplay && level.RiverSpeed > 0.0) ? 10.0F : 0.0F;
    u.FlowInfo[3] = 3.0F;
    const auto signedByte = [](float64 value, float64 range) noexcept {
        return static_cast<uint32>(std::clamp(std::round(value / range * 127.0 + 128.0), 1.0, 255.0));
    };
    const auto pack = [](uint32 a, uint32 b, uint32 c) noexcept {
        return static_cast<float32>(a + b * 256U + c * 65536U);
    };
    for (usize y = 0; y < 24; ++y)
    {
        for (usize x = 0; x < 16; ++x)
        {
            const Point position{((static_cast<float64>(x) + 0.5) / 16.0 * 2.0 - 1.0) * level.HalfExtent.X,
                                 ((static_cast<float64>(y) + 0.5) / 24.0 * 2.0 - 1.0) * level.HalfExtent.Y};
            const auto water = simulation.GetWater().Sample(position);
            const auto river = gameplay ? RiverCurrent(level, position) : Point{};
            const usize cell = y * 16 + x;
            auto& packed = u.Flow[cell / 2];
            packed[(cell % 2) * 2] = pack(signedByte(water.VelocityX + river.X, 10.0),
                                          signedByte(water.VelocityY + river.Y, 10.0),
                                          signedByte(water.Height, 3.0));
            packed[(cell % 2) * 2 + 1] = pack(static_cast<uint32>(std::round(std::clamp(water.Foam, 0.0, 1.0) * 255.0)),
                                              signedByte(water.DisplacementX, 12.0),
                                              signedByte(water.DisplacementY, 12.0));
        }
    }
    for (usize y = 0; y < kWaterHeight; ++y)
    {
        for (usize x = 0; x < kWaterWidth; ++x)
        {
            const auto water = simulation.GetWater().SurfaceCell(x, y);
            const auto heightCode =
                static_cast<uint32>(std::clamp(std::round(water.Height / 6.0 * 32767.0 + 32768.0), 1.0, 65535.0));
            const auto foamCode = static_cast<uint32>(std::round(std::clamp(water.Foam, 0.0, 1.0) * 255.0));
            const usize cell = y * 48 + x;
            u.Surface[cell / 4][cell % 4] = static_cast<float32>(heightCode + foamCode * 65536U);
        }
    }
    u.LevelBounds[0] = static_cast<float32>(level.HalfExtent.X);
    u.LevelBounds[1] = static_cast<float32>(level.HalfExtent.Y);
    u.LevelBounds[2] = gameplay ? static_cast<float32>(level.RiverSpeed) : 0.0F;
    u.LevelBounds[3] = static_cast<float32>(level.RapidsBoost);
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
