#pragma once

#include "ocean/numeric.h"

namespace ludus::sandbox::game
{
struct Point;
struct LevelDefinition;
using ocean::numeric::float64;

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

// Staggered velocity / cell-centered free surface. Owns one bounded allocation;
// reset, forcing, sampling and stepping reuse it. No renderer dependencies.
class WaterField final
{
public:
    WaterField() noexcept;
    ~WaterField() noexcept;
    WaterField(const WaterField&) = delete;
    WaterField& operator=(const WaterField&) = delete;
    [[nodiscard]] bool Reset(const LevelDefinition& level) noexcept;
    [[nodiscard]] bool Ready() const noexcept;
    // An undisturbed field has no state to advance or upload. Reset clears it.
    [[nodiscard]] bool Active() const noexcept;
    // Integrate a compact momentum brush over a segment, independent of events.
    [[nodiscard]] bool Stroke(Point start, Point end) noexcept;
    void Advance(float64 seconds) noexcept;
    [[nodiscard]] WaterSample Sample(Point world) const noexcept;
    [[nodiscard]] WaterDiagnostics Diagnostics() const noexcept;

private:
    struct Storage;
    Storage* mStorage = nullptr;
};
} // namespace ludus::sandbox::game
