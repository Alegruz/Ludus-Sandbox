#pragma once

#include "ocean/numeric.h"

namespace ludus::sandbox::game
{
struct Point;
struct LevelDefinition;
using ocean::numeric::float64;
using ocean::numeric::usize;
inline constexpr usize kWaterWidth = 48;
inline constexpr usize kWaterHeight = 64;
struct WaterSurface final
{
    float64 Height = 0.0;
    float64 Foam = 0.0;
};

struct WaterSample final
{
    float64 VelocityX = 0.0;
    float64 VelocityY = 0.0;
    float64 Height = 0.0;
    float64 Foam = 0.0;
    float64 DisplacementX = 0.0;
    float64 DisplacementY = 0.0;
    float64 Curl = 0.0;
    float64 Divergence = 0.0;
};
struct WaterDiagnostics final
{
    float64 Energy = 0.0;
    float64 MaxCurl = 0.0;
    float64 MaxDivergence = 0.0;
    float64 MaxHeight = 0.0;
};

// SDK-backed staggered velocity / cell-centered free surface. Initialization
// allocates; reset, forcing, sampling and stepping reuse storage. No renderer dependencies.
class WaterField final
{
public:
    WaterField() noexcept;
    ~WaterField() noexcept;
    WaterField(const WaterField&) = delete;
    WaterField& operator=(const WaterField&) = delete;
    [[nodiscard]] bool Reset(const LevelDefinition& level) noexcept;
    [[nodiscard]] bool Ready() const noexcept;
    // A still field has no state to advance. Reset seeds the selected sea state.
    [[nodiscard]] bool Active() const noexcept;
    // Integrate a compact momentum brush over a segment, independent of events.
    [[nodiscard]] bool Stroke(Point start, Point end) noexcept;
    // Finite-area pressure applied smoothly over 0.28 seconds, never a point impulse.
    [[nodiscard]] bool Splash(Point center, float64 strength) noexcept;
    // Wind forcing changes without deleting existing disturbances. Reset seeds
    // the selected sea state; zero gives a still-water laboratory for tests.
    [[nodiscard]] bool SetSeaState(float64 strength) noexcept;
    [[nodiscard]] float64 SeaState() const noexcept;
    void Advance(float64 seconds) noexcept;
    [[nodiscard]] WaterSample Sample(Point world) const noexcept;
    [[nodiscard]] WaterSurface SurfaceCell(usize x, usize y) const noexcept;
    [[nodiscard]] WaterDiagnostics Diagnostics() const noexcept;

private:
    struct Storage;
    Storage* mStorage = nullptr;
};
} // namespace ludus::sandbox::game
