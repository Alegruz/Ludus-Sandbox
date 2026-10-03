#pragma once

// CPU-side uniform layout for the ocean shader.
//
// This is the application's upload contract (see fullscreen-rendering.md:
// "CPU layouts are application contracts"). The layout MUST match the std140
// layout Slang assigns to the `OceanUniforms` constant buffer in
// shaders/ocean.slang. The offsets below were taken from the Slang reflection
// JSON (set/group 0, binding 0, total size 736) and are asserted so a mismatch
// is a compile error, not a silent visual bug.
//
// The block is set/group 0, binding 0, visible to both stages, 736 bytes
// (within the engine's 16..4096 byte, multiple-of-16 bound).

#include "game/ripple_game.h"
#include "ocean/numeric.h"
#include "ocean/ocean_clock.h"
#include "ocean/ocean_settings.h"

#include <cstddef>
#include <type_traits>

namespace ludus::sandbox::ocean
{
using numeric::float32;
using numeric::uint32;

// Mirrors the std140 layout of shaders/ocean.slang `OceanUniforms`.
// Explicit padding fields reproduce Slang's std140 alignment exactly.
struct alignas(16) OceanUniforms final
{
    float32 Resolution[2]; // offset 0  : vec2
    float32 WorldView[2];  // offset 8  : vec2 (viewWidthMeters, viewHeightMeters)

    float32 WaveTime;         // offset 16 : scalar
    float32 PadA;             // offset 20 : (std140 aligns next vec2 to 8)
    float32 CurrentOffset[2]; // offset 24 : vec2 (x, y meters)

    float32 WaveScaleMeters;    // offset 32
    float32 WaveAnimationSpeed; // offset 36
    float32 WaveIntensity;      // offset 40
    float32 FoamAmount;         // offset 44

    float32 CurrentDirCos; // offset 48 (unit direction x)
    float32 CurrentDirSin; // offset 52 (unit direction y)
    float32 Pad0;          // offset 56
    float32 Pad1;          // offset 60

    float32 Pad2;    // offset 64 (scalar; next vec4 aligns to 80)
    float32 PadB[3]; // offset 68..79 : padding up to the vec4 boundary

    float32 DeepColor[4];    // offset 80  : vec4
    float32 MidColor[4];     // offset 96  : vec4
    float32 ShallowColor[4]; // offset 112 : vec4
    float32 FoamColor[4];    // offset 128 : vec4

    float32 BoatInfo[4];                       // position.xy, heading, hull radius
    float32 GameInfo[4];                       // enabled, ring count, cooldown fraction, contact flash
    float32 Ripples[game::kRippleCapacity][4]; // origin.xy, radius, fade
    float32 LevelBounds[4];                    // half width/height, reserved
    float32 DockInfo[4];                       // center.xy, radius, maximum arrival speed
    float32 LevelInfo[4];                      // rock count, phase, dock dwell fraction, boundary hazard
    float32 Rocks[game::kRockCapacity][4];     // center.xy, radius, reserved
};

static_assert(std::is_standard_layout_v<OceanUniforms>);
static_assert(offsetof(OceanUniforms, Resolution) == 0);
static_assert(offsetof(OceanUniforms, WorldView) == 8);
static_assert(offsetof(OceanUniforms, WaveTime) == 16);
static_assert(offsetof(OceanUniforms, CurrentOffset) == 24);
static_assert(offsetof(OceanUniforms, WaveScaleMeters) == 32);
static_assert(offsetof(OceanUniforms, WaveAnimationSpeed) == 36);
static_assert(offsetof(OceanUniforms, WaveIntensity) == 40);
static_assert(offsetof(OceanUniforms, FoamAmount) == 44);
static_assert(offsetof(OceanUniforms, CurrentDirCos) == 48);
static_assert(offsetof(OceanUniforms, CurrentDirSin) == 52);
static_assert(offsetof(OceanUniforms, DeepColor) == 80);
static_assert(offsetof(OceanUniforms, MidColor) == 96);
static_assert(offsetof(OceanUniforms, ShallowColor) == 112);
static_assert(offsetof(OceanUniforms, FoamColor) == 128);
static_assert(offsetof(OceanUniforms, BoatInfo) == 144);
static_assert(offsetof(OceanUniforms, GameInfo) == 160);
static_assert(offsetof(OceanUniforms, Ripples) == 176);
static_assert(offsetof(OceanUniforms, LevelBounds) == 432);
static_assert(offsetof(OceanUniforms, DockInfo) == 448);
static_assert(offsetof(OceanUniforms, LevelInfo) == 464);
static_assert(offsetof(OceanUniforms, Rocks) == 480);
static_assert(sizeof(OceanUniforms) == 736 && alignof(OceanUniforms) == 16);
// Within the engine's uniform bounds (16..4096, multiple of 16).
static_assert(sizeof(OceanUniforms) % 16 == 0 && sizeof(OceanUniforms) >= 16 && sizeof(OceanUniforms) <= 4096);

// Fill a uniform block from sanitized settings + clock + actual framebuffer
// extent. `widthPx`/`heightPx` are the ACTUAL acquired frame pixels
// (GetFrameInfo), guaranteeing aspect-correct, resize-stable wave scale.
[[nodiscard]] OceanUniforms
BuildUniforms(const OceanSettings& settings, const SceneClock& clock, uint32 widthPx, uint32 heightPx) noexcept;

} // namespace ludus::sandbox::ocean
