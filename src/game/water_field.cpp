#include "game/water_field.h"
#include "game/ripple_game.h"

#include <ludus/physics/fluid/field.h>

#include <algorithm>
#include <cmath>
#include <new>

namespace ludus::sandbox::game
{
namespace
{
constexpr usize kWidth = kWaterWidth;
constexpr usize kHeight = kWaterHeight;
constexpr usize kCells = kWidth * kHeight;
constexpr usize kUFaces = (kWidth + 1) * kHeight;
constexpr usize kVFaces = kWidth * (kHeight + 1);
constexpr float64 kGravity = 9.8;
constexpr float64 kDepth = 4.0;
namespace fluid = ludus::physics::fluid;
using Cell = fluid::CellState;
using ludus::foundation::uint8;
[[nodiscard]] usize CellIndex(usize x, usize y) noexcept
{
    return y * kWidth + x;
}
struct SplashSource final
{
    Point Center;
    float64 Strength = 0.0;
    float64 Age = 0.28;
};
struct SeaMode final
{
    float64 Basis[kCells]{};
    float64 Kx = 0.0;
    float64 Ky = 0.0;
    float64 Omega = 0.0;
    float64 Phase = 0.0;
    float64 Amplitude = 0.0;
};
} // namespace

struct WaterField::Storage final
{
    float64 U[kUFaces]{};
    float64 V[kVFaces]{};
    Cell Cells[kCells]{};
    uint8 Solid[kCells]{};
    fluid::Field Grid;
    Point HalfExtent{30.0, 40.0};
    float64 Dx = 1.25;
    float64 Dy = 1.25;
    bool Active = false;
    float64 SeaStrength = 0.0;
    float64 Time = 0.0;
    float64 Pressure[kCells]{};
    SeaMode Modes[8]{};
    SplashSource Splashes[16]{};

    [[nodiscard]] bool Fluid(usize x, usize y) const noexcept
    {
        return x < kWidth && y < kHeight && !Solid[CellIndex(x, y)];
    }
    [[nodiscard]] bool OpenU(usize x, usize y) const noexcept
    {
        return x > 0 && x < kWidth && Fluid(x - 1, y) && Fluid(x, y);
    }
    [[nodiscard]] bool OpenV(usize x, usize y) const noexcept
    {
        return y > 0 && y < kHeight && Fluid(x, y - 1) && Fluid(x, y);
    }
    [[nodiscard]] bool Step(float64 dt) noexcept
    {
        Time += dt;
        std::fill_n(Pressure, kCells, 0.0);
        // Wind pressure drives standing gravity modes of the closed basin. All
        // subsequent motion, rock reflection and interaction come from the PDE.
        for (const auto& mode : Modes)
        {
            if (SeaStrength <= 0.0)
            {
                break;
            }
            if (mode.Omega <= 0.0)
            {
                continue;
            }
            const float64 forcing =
                SeaStrength * mode.Amplitude * 0.06 * kGravity / mode.Omega * std::sin(mode.Phase + mode.Omega * Time);
            for (usize i = 0; i < kCells; ++i)
            {
                Pressure[i] += mode.Basis[i] * forcing;
            }
        }
        for (auto& splash : Splashes)
        {
            if (splash.Age >= 0.28)
            {
                continue;
            }
            const float64 interval = std::min(dt, 0.28 - splash.Age);
            const float64 phase = (splash.Age + interval * 0.5) / 0.28 * 3.141592653589793;
            const float64 pulse = std::sin(phase);
            const float64 pressure = splash.Strength * 80.0 * pulse * pulse * interval / dt;
            splash.Age += interval;
            for (usize y = 0; y < kHeight; ++y)
            {
                for (usize x = 0; x < kWidth; ++x)
                {
                    const float64 rx = (static_cast<float64>(x) + 0.5) * Dx - HalfExtent.X - splash.Center.X;
                    const float64 ry = (static_cast<float64>(y) + 0.5) * Dy - HalfExtent.Y - splash.Center.Y;
                    const float64 footprint = std::max(1.0 - (rx * rx + ry * ry) / 36.0, 0.0);
                    Pressure[CellIndex(x, y)] += pressure * footprint * footprint * footprint;
                }
            }
        }
        if (Grid.TrySetPressure(Pressure) != fluid::Status::Success)
        {
            return false;
        }
        fluid::StepInfo info;
        return Grid.TryAdvance(dt, info) == fluid::Status::Success;
    }
};

WaterField::WaterField() noexcept : mStorage(new(std::nothrow) Storage)
{
    if (mStorage != nullptr && mStorage->Grid.TryInitialize({}) != fluid::Status::Success)
    {
        delete mStorage;
        mStorage = nullptr;
    }
}
WaterField::~WaterField() noexcept
{
    delete mStorage;
}
bool WaterField::Ready() const noexcept
{
    return mStorage != nullptr;
}
bool WaterField::Active() const noexcept
{
    return mStorage != nullptr && (mStorage->Active || mStorage->Grid.IsActive());
}
bool WaterField::Reset(const LevelDefinition& level) noexcept
{
    if (!std::isfinite(level.HalfExtent.X) || !std::isfinite(level.HalfExtent.Y) || level.HalfExtent.X <= kBoatRadius ||
        level.HalfExtent.Y <= kBoatRadius || level.HalfExtent.X > 1000.0 || level.HalfExtent.Y > 1000.0 ||
        level.RockCount > kRockCapacity)
    {
        return false;
    }
    for (usize i = 0; i < level.RockCount; ++i)
    {
        const auto& rock = level.Rocks[i];
        if (!std::isfinite(rock.Center.X) || !std::isfinite(rock.Center.Y) || !std::isfinite(rock.Radius) ||
            rock.Radius <= 0.0 || std::abs(rock.Center.X) + rock.Radius >= level.HalfExtent.X ||
            std::abs(rock.Center.Y) + rock.Radius >= level.HalfExtent.Y)
        {
            return false;
        }
    }
    if (mStorage == nullptr)
    {
        return false;
    }
    auto& s = *mStorage;
    std::fill_n(s.U, kUFaces, 0.0);
    std::fill_n(s.V, kVFaces, 0.0);
    std::fill_n(s.Cells, kCells, Cell{});
    s.HalfExtent = level.HalfExtent;
    s.Dx = level.HalfExtent.X * 2.0 / static_cast<float64>(kWidth);
    s.Dy = level.HalfExtent.Y * 2.0 / static_cast<float64>(kHeight);
    s.Active = s.SeaStrength > 0.0;
    s.Time = 0.0;
    for (auto& splash : s.Splashes)
    {
        splash = {};
    }
    for (usize y = 0; y < kHeight; ++y)
    {
        for (usize x = 0; x < kWidth; ++x)
        {
            const Point p{(static_cast<float64>(x) + 0.5) * s.Dx - s.HalfExtent.X,
                          (static_cast<float64>(y) + 0.5) * s.Dy - s.HalfExtent.Y};
            bool solid = false;
            for (usize i = 0; i < level.RockCount; ++i)
            {
                solid |=
                    std::hypot(p.X - level.Rocks[i].Center.X, p.Y - level.Rocks[i].Center.Y) <= level.Rocks[i].Radius;
            }
            s.Solid[CellIndex(x, y)] = solid;
        }
    }
    constexpr usize frequencies[8][2] = {{2, 3}, {4, 1}, {3, 6}, {6, 4}, {8, 2}, {5, 9}, {9, 7}, {11, 5}};
    float64 mean = 0.0;
    usize wetCells = 0;
    for (usize m = 0; m < 8; ++m)
    {
        auto& mode = s.Modes[m];
        mode.Kx = static_cast<float64>(frequencies[m][0]) * 3.141592653589793 / (s.HalfExtent.X * 2.0);
        mode.Ky = static_cast<float64>(frequencies[m][1]) * 3.141592653589793 / (s.HalfExtent.Y * 2.0);
        mode.Omega = std::sqrt(kGravity * kDepth * (mode.Kx * mode.Kx + mode.Ky * mode.Ky));
        mode.Phase = static_cast<float64>(m) * 2.399963229728653;
        mode.Amplitude = 0.52 / std::sqrt(static_cast<float64>(m + 1));
        for (usize y = 0; y < kHeight; ++y)
        {
            for (usize x = 0; x < kWidth; ++x)
            {
                const usize index = CellIndex(x, y);
                mode.Basis[index] = std::cos((static_cast<float64>(x) + 0.5) * s.Dx * mode.Kx) *
                                    std::cos((static_cast<float64>(y) + 0.5) * s.Dy * mode.Ky);
                if (!s.Solid[index])
                {
                    s.Cells[index].Height += s.SeaStrength * mode.Amplitude * mode.Basis[index] * std::cos(mode.Phase);
                }
            }
        }
        const float64 velocity = s.SeaStrength * mode.Amplitude * kGravity / mode.Omega * std::sin(mode.Phase);
        for (usize y = 0; y < kHeight; ++y)
        {
            for (usize x = 1; x < kWidth; ++x)
            {
                if (s.OpenU(x, y))
                {
                    s.U[y * (kWidth + 1) + x] += velocity * mode.Kx *
                                                 std::sin(static_cast<float64>(x) * s.Dx * mode.Kx) *
                                                 std::cos((static_cast<float64>(y) + 0.5) * s.Dy * mode.Ky);
                }
            }
        }
        for (usize y = 1; y < kHeight; ++y)
        {
            for (usize x = 0; x < kWidth; ++x)
            {
                if (s.OpenV(x, y))
                {
                    s.V[y * kWidth + x] += velocity * mode.Ky *
                                           std::cos((static_cast<float64>(x) + 0.5) * s.Dx * mode.Kx) *
                                           std::sin(static_cast<float64>(y) * s.Dy * mode.Ky);
                }
            }
        }
    }
    for (usize i = 0; i < kCells; ++i)
    {
        if (!s.Solid[i])
        {
            mean += s.Cells[i].Height;
            ++wetCells;
        }
    }
    if (wetCells > 0)
    {
        mean /= static_cast<float64>(wetCells);
        for (usize i = 0; i < kCells; ++i)
        {
            if (!s.Solid[i])
            {
                s.Cells[i].Height -= mean;
            }
        }
    }
    fluid::Config config;
    config.HalfExtent = {level.HalfExtent.X, level.HalfExtent.Y};
    config.MaxPressure = 20000.0;
    return s.Grid.TryInitialize(config) == fluid::Status::Success &&
           s.Grid.TryReset(s.Solid, s.Cells) == fluid::Status::Success &&
           s.Grid.TrySetVelocity(s.U, s.V) == fluid::Status::Success;
}
bool WaterField::Stroke(Point start, Point end) noexcept
{
    if (mStorage == nullptr)
    {
        return false;
    }
    const fluid::Stroke stroke{{start.X, start.Y}, {end.X, end.Y}};
    return mStorage->Grid.TryAddStroke(stroke) == fluid::Status::Success;
}
bool WaterField::Splash(Point center, float64 strength) noexcept
{
    if (mStorage == nullptr || !std::isfinite(center.X) || !std::isfinite(center.Y) ||
        std::abs(center.X) >= mStorage->HalfExtent.X || std::abs(center.Y) >= mStorage->HalfExtent.Y ||
        !std::isfinite(strength) || std::abs(strength) > 8.0)
    {
        return false;
    }
    if (strength == 0.0)
    {
        return true;
    }
    for (auto& splash : mStorage->Splashes)
    {
        if (splash.Age >= 0.28)
        {
            splash = {.Center = center, .Strength = strength, .Age = 0.0};
            mStorage->Active = true;
            return true;
        }
    }
    return false;
}
bool WaterField::SetSeaState(float64 strength) noexcept
{
    if (mStorage == nullptr || !std::isfinite(strength) || strength < 0.0 || strength > 1.0)
    {
        return false;
    }
    mStorage->SeaStrength = strength;
    mStorage->Active |= strength > 0.0;
    return true;
}
float64 WaterField::SeaState() const noexcept
{
    return mStorage != nullptr ? mStorage->SeaStrength : 0.0;
}
void WaterField::Advance(float64 seconds) noexcept
{
    if (!Active() || !std::isfinite(seconds) || seconds <= 0.0 || seconds > kTickSeconds)
    {
        return;
    }
    auto& s = *mStorage;
    float64 advanced = 0.0;
    for (usize i = 0; i < 256 && advanced < seconds; ++i)
    {
        float64 limit = 0.0;
        if (s.Grid.TryGetStepLimit(limit) != fluid::Status::Success)
        {
            return;
        }
        const float64 remaining = seconds - advanced;
        const float64 dt = std::min(remaining, limit);
        if (!s.Step(dt))
        {
            return;
        }
        advanced = dt == remaining ? seconds : advanced + dt;
    }
}
WaterSample WaterField::Sample(Point world) const noexcept
{
    fluid::Sample result;
    if (mStorage == nullptr || mStorage->Grid.TrySample({world.X, world.Y}, result) != fluid::Status::Success)
    {
        return {};
    }
    return {result.VelocityX,
            result.VelocityY,
            result.Height,
            result.Foam,
            result.DisplacementX,
            result.DisplacementY,
            result.Curl,
            result.Divergence};
}
WaterSurface WaterField::SurfaceCell(usize x, usize y) const noexcept
{
    if (mStorage == nullptr)
    {
        return {};
    }
    const auto cell = mStorage->Grid.GetCell(x, y);
    return {cell.Height, cell.Foam};
}
WaterDiagnostics WaterField::Diagnostics() const noexcept
{
    if (mStorage == nullptr)
    {
        return {};
    }
    const auto result = mStorage->Grid.GetDiagnostics();
    return {result.Energy, result.MaxCurl, result.MaxDivergence, result.MaxHeight};
}
} // namespace ludus::sandbox::game
