#include "game/water_field.h"
#include "game/ripple_game.h"

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
constexpr float64 kMaxSpeed = 10.0;
constexpr float64 kBrushRadius = 4.0;
struct Cell final
{
    float64 Height = 0.0;
    float64 Foam = 0.0;
    float64 DisplacementX = 0.0;
    float64 DisplacementY = 0.0;
};
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
struct GridSize final
{
    usize Width;
    usize Height;
};
// Face/scalar bilinear interpolation. Solid faces are already zeroed.
[[nodiscard]] float64 Interpolate(const float64* values, GridSize size, Point coordinate) noexcept
{
    const auto width = size.Width;
    const auto height = size.Height;
    auto x = coordinate.X;
    auto y = coordinate.Y;
    x = std::clamp(x, 0.0, static_cast<float64>(width - 1));
    y = std::clamp(y, 0.0, static_cast<float64>(height - 1));
    const auto ix = static_cast<usize>(x);
    const auto iy = static_cast<usize>(y);
    const auto nx = std::min(ix + 1, width - 1);
    const auto ny = std::min(iy + 1, height - 1);
    const float64 fx = x - static_cast<float64>(ix);
    const float64 fy = y - static_cast<float64>(iy);
    return std::lerp(std::lerp(values[iy * width + ix], values[iy * width + nx], fx),
                     std::lerp(values[ny * width + ix], values[ny * width + nx], fx),
                     fy);
}
} // namespace

struct WaterField::Storage final
{
    float64 U[kUFaces]{};
    float64 V[kVFaces]{};
    float64 NextU[kUFaces]{};
    float64 NextV[kVFaces]{};
    Cell Cells[kCells]{};
    Cell NextCells[kCells]{};
    bool Solid[kCells]{};
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
    [[nodiscard]] bool Wet(Point p) const noexcept
    {
        if (!std::isfinite(p.X) || !std::isfinite(p.Y) || std::abs(p.X) >= HalfExtent.X ||
            std::abs(p.Y) >= HalfExtent.Y)
        {
            return false;
        }
        return Fluid(static_cast<usize>((p.X + HalfExtent.X) / Dx), static_cast<usize>((p.Y + HalfExtent.Y) / Dy));
    }
    [[nodiscard]] Point Velocity(Point p) const noexcept
    {
        const float64 x = (p.X + HalfExtent.X) / Dx;
        const float64 y = (p.Y + HalfExtent.Y) / Dy;
        return {Interpolate(U, {kWidth + 1, kHeight}, {x, y - 0.5}),
                Interpolate(V, {kWidth, kHeight + 1}, {x - 0.5, y})};
    }
    [[nodiscard]] Cell Material(Point p) const noexcept
    {
        const float64 x = std::clamp((p.X + HalfExtent.X) / Dx - 0.5, 0.0, static_cast<float64>(kWidth - 1));
        const float64 y = std::clamp((p.Y + HalfExtent.Y) / Dy - 0.5, 0.0, static_cast<float64>(kHeight - 1));
        const auto ix = static_cast<usize>(x);
        const auto iy = static_cast<usize>(y);
        const auto nx = std::min(ix + 1, kWidth - 1);
        const auto ny = std::min(iy + 1, kHeight - 1);
        const float64 fx = x - static_cast<float64>(ix), fy = y - static_cast<float64>(iy);
        // Renormalize fluid weights so rock values cannot leak into water.
        Cell result;
        float64 total = 0.0;
        for (usize j = 0; j < 2; ++j)
        {
            for (usize i = 0; i < 2; ++i)
            {
                const usize index = CellIndex(i == 0 ? ix : nx, j == 0 ? iy : ny);
                const float64 weight = (i == 0 ? 1.0 - fx : fx) * (j == 0 ? 1.0 - fy : fy);
                if (!Solid[index])
                {
                    total += weight;
                    result.Height += Cells[index].Height * weight;
                    result.Foam += Cells[index].Foam * weight;
                    result.DisplacementX += Cells[index].DisplacementX * weight;
                    result.DisplacementY += Cells[index].DisplacementY * weight;
                }
            }
        }
        if (total > 0.0)
        {
            result.Height /= total;
            result.Foam /= total;
            result.DisplacementX /= total;
            result.DisplacementY /= total;
        }
        return result;
    }
    [[nodiscard]] Point Departure(Point p, float64 dt) const noexcept
    {
        const Point velocity = Velocity(p);
        const Point middle{p.X - velocity.X * dt * 0.5, p.Y - velocity.Y * dt * 0.5};
        const Point midVelocity = Velocity(middle);
        const Point back{p.X - midVelocity.X * dt, p.Y - midVelocity.Y * dt};
        // CFL limits traces to less than a cell; reject solid-crossing traces.
        return Wet(middle) && Wet(back) ? back : p;
    }
    [[nodiscard]] float64 Divergence(usize x, usize y) const noexcept
    {
        return (U[y * (kWidth + 1) + x + 1] - U[y * (kWidth + 1) + x]) / Dx +
               (V[(y + 1) * kWidth + x] - V[y * kWidth + x]) / Dy;
    }
    [[nodiscard]] float64 Curl(Point p) const noexcept
    {
        return (Velocity({p.X + Dx * 0.5, p.Y}).Y - Velocity({p.X - Dx * 0.5, p.Y}).Y) / Dx -
               (Velocity({p.X, p.Y + Dy * 0.5}).X - Velocity({p.X, p.Y - Dy * 0.5}).X) / Dy;
    }
    void Step(float64 dt) noexcept
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
        const float64 damping = std::exp(-0.06 * dt);
        // Semi-Lagrangian self-advection of staggered momentum, then free-surface
        // pressure. No incompressible projection: divergence must drive height.
        for (usize y = 0; y < kHeight; ++y)
        {
            for (usize x = 0; x <= kWidth; ++x)
            {
                const usize index = y * (kWidth + 1) + x;
                NextU[index] = 0.0;
                if (OpenU(x, y))
                {
                    const Point p{static_cast<float64>(x) * Dx - HalfExtent.X,
                                  (static_cast<float64>(y) + 0.5) * Dy - HalfExtent.Y};
                    const Point back = Departure(p, dt);
                    const float64 advected = Velocity(back).X;
                    const usize right = CellIndex(x, y), left = CellIndex(x - 1, y);
                    const float64 gradient =
                        (Cells[right].Height - Cells[left].Height + (Pressure[right] - Pressure[left]) / kGravity) / Dx;
                    NextU[index] = std::clamp((advected - kGravity * gradient * dt) * damping, -kMaxSpeed, kMaxSpeed);
                }
            }
        }
        for (usize y = 0; y <= kHeight; ++y)
        {
            for (usize x = 0; x < kWidth; ++x)
            {
                const usize index = y * kWidth + x;
                NextV[index] = 0.0;
                if (OpenV(x, y))
                {
                    const Point p{(static_cast<float64>(x) + 0.5) * Dx - HalfExtent.X,
                                  static_cast<float64>(y) * Dy - HalfExtent.Y};
                    const Point back = Departure(p, dt);
                    const float64 advected = Velocity(back).Y;
                    const usize top = CellIndex(x, y), bottom = CellIndex(x, y - 1);
                    const float64 gradient =
                        (Cells[top].Height - Cells[bottom].Height + (Pressure[top] - Pressure[bottom]) / kGravity) / Dy;
                    NextV[index] = std::clamp((advected - kGravity * gradient * dt) * damping, -kMaxSpeed, kMaxSpeed);
                }
            }
        }
        // Passive material and foam are transported by the old velocity, before
        // replacing faces. Displacement stores the backtraced material map.
        for (usize y = 0; y < kHeight; ++y)
        {
            for (usize x = 0; x < kWidth; ++x)
            {
                const usize index = CellIndex(x, y);
                NextCells[index] = {};
                if (!Solid[index])
                {
                    const Point p{(static_cast<float64>(x) + 0.5) * Dx - HalfExtent.X,
                                  (static_cast<float64>(y) + 0.5) * Dy - HalfExtent.Y};
                    const Point back = Departure(p, dt);
                    const Cell material = Material(back);
                    auto& next = NextCells[index];
                    next.DisplacementX =
                        std::clamp((material.DisplacementX + p.X - back.X) * std::exp(-0.035 * dt), -12.0, 12.0);
                    next.DisplacementY =
                        std::clamp((material.DisplacementY + p.Y - back.Y) * std::exp(-0.035 * dt), -12.0, 12.0);
                    const float64 hx = (Cells[CellIndex(std::min(x + 1, kWidth - 1), y)].Height -
                                        Cells[CellIndex(x > 0 ? x - 1 : x, y)].Height) /
                                       (2.0 * Dx);
                    const float64 hy = (Cells[CellIndex(x, std::min(y + 1, kHeight - 1))].Height -
                                        Cells[CellIndex(x, y > 0 ? y - 1 : y)].Height) /
                                       (2.0 * Dy);
                    const float64 breaking = std::max(std::hypot(hx, hy) - 0.25, 0.0) * 2.0;
                    const float64 source = std::max(-Divergence(x, y) - 0.08, 0.0) * 0.3 + breaking;
                    next.Foam = std::clamp(material.Foam * std::exp(-0.65 * dt) + source * dt, 0.0, 1.0);
                }
            }
        }
        std::copy_n(NextU, kUFaces, U);
        std::copy_n(NextV, kVFaces, V);
        for (usize y = 0; y < kHeight; ++y)
        {
            for (usize x = 0; x < kWidth; ++x)
            {
                const usize index = CellIndex(x, y);
                if (!Solid[index])
                {
                    // Nonlinear shallow-water continuity: upwind total depth
                    // on each shared face. Closed faces have exactly zero flux.
                    const auto depth = [&](usize cx, usize cy) noexcept {
                        return kDepth + Cells[CellIndex(cx, cy)].Height;
                    };
                    const float64 left = U[y * (kWidth + 1) + x];
                    const float64 right = U[y * (kWidth + 1) + x + 1];
                    const float64 bottom = V[y * kWidth + x];
                    const float64 top = V[(y + 1) * kWidth + x];
                    const float64 leftFlux = left * depth(left > 0.0 && x > 0 ? x - 1 : x, y);
                    const float64 rightFlux = right * depth(right < 0.0 && x + 1 < kWidth ? x + 1 : x, y);
                    const float64 bottomFlux = bottom * depth(x, bottom > 0.0 && y > 0 ? y - 1 : y);
                    const float64 topFlux = top * depth(x, top < 0.0 && y + 1 < kHeight ? y + 1 : y);
                    NextCells[index].Height =
                        Cells[index].Height - dt * ((rightFlux - leftFlux) / Dx + (topFlux - bottomFlux) / Dy);
                }
            }
        }
        std::copy_n(NextCells, kCells, Cells);
    }
};

WaterField::WaterField() noexcept : mStorage(new(std::nothrow) Storage) {}
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
    return mStorage != nullptr && mStorage->Active;
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
    return true;
}
bool WaterField::Stroke(Point start, Point end) noexcept
{
    if (mStorage == nullptr || !mStorage->Wet(start) || !mStorage->Wet(end))
    {
        return false;
    }
    auto& s = *mStorage;
    const Point segment{end.X - start.X, end.Y - start.Y};
    const float64 distance = std::hypot(segment.X, segment.Y);
    if (!std::isfinite(distance) || distance <= 0.0)
    {
        return false;
    }
    const auto steps = static_cast<usize>(std::ceil(distance / 0.4));
    const Point impulse{segment.X * 1.8 / static_cast<float64>(steps), segment.Y * 1.8 / static_cast<float64>(steps)};
    for (usize i = 0; i < steps; ++i)
    {
        const float64 t = (static_cast<float64>(i) + 0.5) / static_cast<float64>(steps);
        const Point p{start.X + segment.X * t, start.Y + segment.Y * t};
        const auto weight = [&](float64 x, float64 y) noexcept {
            const float64 q = ((x - p.X) * (x - p.X) + (y - p.Y) * (y - p.Y)) / (kBrushRadius * kBrushRadius);
            return q < 1.0 ? (1.0 - q) * (1.0 - q) : 0.0;
        };
        const auto lower = [](float64 coordinate, float64 spacing, usize limit) noexcept {
            return static_cast<usize>(
                std::clamp(std::floor(coordinate / spacing) - 1.0, 0.0, static_cast<float64>(limit)));
        };
        const auto upper = [](float64 coordinate, float64 spacing, usize limit) noexcept {
            return static_cast<usize>(
                std::clamp(std::ceil(coordinate / spacing) + 1.0, 0.0, static_cast<float64>(limit)));
        };
        const auto x0 = lower(p.X + s.HalfExtent.X - kBrushRadius, s.Dx, kWidth);
        const auto y0 = lower(p.Y + s.HalfExtent.Y - kBrushRadius, s.Dy, kHeight);
        const auto x1 = upper(p.X + s.HalfExtent.X + kBrushRadius, s.Dx, kWidth);
        const auto y1 = upper(p.Y + s.HalfExtent.Y + kBrushRadius, s.Dy, kHeight);
        for (usize y = y0; y < y1; ++y)
        {
            for (usize x = std::max(x0, usize{1}); x < x1; ++x)
            {
                if (s.OpenU(x, y))
                {
                    auto& u = s.U[y * (kWidth + 1) + x];
                    u = std::clamp(u + impulse.X * weight(static_cast<float64>(x) * s.Dx - s.HalfExtent.X,
                                                          (static_cast<float64>(y) + 0.5) * s.Dy - s.HalfExtent.Y),
                                   -kMaxSpeed,
                                   kMaxSpeed);
                }
            }
        }
        for (usize y = std::max(y0, usize{1}); y < y1; ++y)
        {
            for (usize x = x0; x < x1; ++x)
            {
                if (s.OpenV(x, y))
                {
                    auto& v = s.V[y * kWidth + x];
                    v = std::clamp(v + impulse.Y * weight((static_cast<float64>(x) + 0.5) * s.Dx - s.HalfExtent.X,
                                                          static_cast<float64>(y) * s.Dy - s.HalfExtent.Y),
                                   -kMaxSpeed,
                                   kMaxSpeed);
                }
            }
        }
    }
    s.Active = true;
    return true;
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
    if (mStorage == nullptr || !mStorage->Active || !std::isfinite(seconds) || seconds <= 0.0 || seconds > kTickSeconds)
    {
        return;
    }
    auto& s = *mStorage;
    // Conservative CFL bound includes the velocity cap and gravity wave speed.
    float64 maxDepth = kDepth;
    for (usize i = 0; i < kCells; ++i)
    {
        maxDepth = std::max(maxDepth, kDepth + s.Cells[i].Height);
    }
    const auto steps = static_cast<usize>(
        std::ceil(seconds * (kMaxSpeed + std::sqrt(kGravity * maxDepth)) / (0.3 * std::min(s.Dx, s.Dy))));
    for (usize i = 0; i < steps; ++i)
    {
        s.Step(seconds / static_cast<float64>(steps));
    }
}
WaterSample WaterField::Sample(Point world) const noexcept
{
    if (!Active() || !mStorage->Wet(world))
    {
        return {};
    }
    const auto& s = *mStorage;
    const auto velocity = s.Velocity(world);
    const auto cell = s.Material(world);
    const auto x = static_cast<usize>((world.X + s.HalfExtent.X) / s.Dx);
    const auto y = static_cast<usize>((world.Y + s.HalfExtent.Y) / s.Dy);
    return {velocity.X,
            velocity.Y,
            cell.Height,
            cell.Foam,
            cell.DisplacementX,
            cell.DisplacementY,
            s.Curl(world),
            s.Divergence(x, y)};
}
WaterSurface WaterField::SurfaceCell(usize x, usize y) const noexcept
{
    if (!Active() || !mStorage->Fluid(x, y))
    {
        return {};
    }
    const auto& cell = mStorage->Cells[CellIndex(x, y)];
    return {cell.Height, cell.Foam};
}
WaterDiagnostics WaterField::Diagnostics() const noexcept
{
    WaterDiagnostics result;
    if (mStorage == nullptr || !mStorage->Active)
    {
        return result;
    }
    const auto& s = *mStorage;
    for (usize y = 0; y < kHeight; ++y)
    {
        for (usize x = 0; x < kWidth; ++x)
        {
            if (!s.Fluid(x, y))
            {
                continue;
            }
            const Point p{(static_cast<float64>(x) + 0.5) * s.Dx - s.HalfExtent.X,
                          (static_cast<float64>(y) + 0.5) * s.Dy - s.HalfExtent.Y};
            const auto velocity = s.Velocity(p);
            const float64 height = s.Cells[CellIndex(x, y)].Height;
            result.Energy +=
                (kDepth * (velocity.X * velocity.X + velocity.Y * velocity.Y) + kGravity * height * height) * 0.5 *
                s.Dx * s.Dy;
            result.MaxCurl = std::max(result.MaxCurl, std::abs(s.Curl(p)));
            result.MaxDivergence = std::max(result.MaxDivergence, std::abs(s.Divergence(x, y)));
            result.MaxHeight = std::max(result.MaxHeight, std::abs(height));
        }
    }
    return result;
}
} // namespace ludus::sandbox::game
