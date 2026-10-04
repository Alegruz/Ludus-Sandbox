#include "game/water_field.h"
#include "game/ripple_game.h"

#include <algorithm>
#include <cmath>
#include <new>

namespace ludus::sandbox::game
{
namespace
{
constexpr usize kWidth = 48;
constexpr usize kHeight = 64;
constexpr usize kCells = kWidth * kHeight;
constexpr usize kUFaces = (kWidth + 1) * kHeight;
constexpr usize kVFaces = kWidth * (kHeight + 1);
constexpr float64 kGravity = 9.8;
constexpr float64 kDepth = 2.0;
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
        const float64 damping = std::exp(-0.12 * dt);
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
                    const float64 gradient = (Cells[CellIndex(x, y)].Height - Cells[CellIndex(x - 1, y)].Height) / Dx;
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
                    const float64 gradient = (Cells[CellIndex(x, y)].Height - Cells[CellIndex(x, y - 1)].Height) / Dy;
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
                    const float64 source = std::max(-Divergence(x, y) - 0.08, 0.0) * 0.9;
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
                    // Conservative face-flux difference; closed faces give no
                    // mass flux through rocks or the edge of the water.
                    NextCells[index].Height =
                        std::clamp((Cells[index].Height - kDepth * dt * Divergence(x, y)) * std::exp(-0.06 * dt),
                                   -3.0,
                                   3.0);
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
    s.Active = false;
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
void WaterField::Advance(float64 seconds) noexcept
{
    if (mStorage == nullptr || !mStorage->Active || !std::isfinite(seconds) || seconds <= 0.0 || seconds > kTickSeconds)
    {
        return;
    }
    auto& s = *mStorage;
    // Conservative CFL bound includes the velocity cap and gravity wave speed.
    const auto steps = static_cast<usize>(
        std::ceil(seconds * (kMaxSpeed + std::sqrt(kGravity * kDepth)) / (0.45 * std::min(s.Dx, s.Dy))));
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
