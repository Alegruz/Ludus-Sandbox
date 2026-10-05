#pragma once

// CPU-side uniform layout for the ocean shader.
//
// This is the application's upload contract (see fullscreen-rendering.md:
// "CPU layouts are application contracts"). The layout MUST match the std140
// layout Slang assigns to the `OceanUniforms` constant buffer in
// shaders/ocean.slang. The offsets below were taken from the Slang reflection
// JSON (set/group 0, binding 0, total size 16336) and are asserted so a mismatch
// is a compile error, not a silent visual bug.
//
// The block is set/group 0, binding 0, visible to both stages, 16336 bytes
// (within the engine's 16..16384 byte, multiple-of-16 bound).

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
    float32 CameraX;       // offset 56 (camera center X)
    float32 CameraY;       // offset 60 (camera center Y)

    float32 RiverTime; // offset 64 (committed river time; next vec4 aligns to 80)
    float32 PadB[3];   // offset 68..79 : padding up to the vec4 boundary

    float32 DeepColor[4];    // offset 80  : vec4
    float32 MidColor[4];     // offset 96  : vec4
    float32 ShallowColor[4]; // offset 112 : vec4
    float32 FoamColor[4];    // offset 128 : vec4

    float32 BoatInfo[4];                       // position.xy, heading, hull radius
    float32 GameInfo[4];                       // enabled, ring count, cooldown fraction, contact flash
    float32 Ripples[game::kRippleCapacity][4]; // origin.xy, radius, fade
    float32 LevelBounds[4];                    // half width/height, river speed, rapid boost
    float32 DockInfo[4];                       // center.xy, radius, maximum arrival speed
    float32 LevelInfo[4];                      // rock count, phase, dock dwell fraction, boundary hazard
    float32 Rocks[game::kRockCapacity][4];     // center.xy, radius, reserved
    float32 BoatMotion[4];                     // velocity.xy, speed, reserved
    float32 FlowInfo[4];                       // dimensions, velocity range (zero when idle), height range
    // Two exactly represented 24-bit integers per cell. First: velocity.xy,
    // height. Second: foam, advected material displacement.xy. Each channel is
    // eight bits; float32 carries all 24 bits exactly on every shader backend.
    float32 Flow[192][4]; // 16 x 24 transport samples, two cells per vec4
    // Full 48 x 64 simulated surface. A float stores one exact 24-bit integer:
    // low 16 bits height (signed zero 32768), high 8 bits transported foam.
    float32 Surface[768][4]; // offset 3840, four cells per vec4
    float32 UiInfo[4];       // visible solid-rectangle count, reserved
    float32 UiRects[6][4];   // framebuffer x/y/width/height
    float32 UiColors[6][4];  // linear RGB, straight alpha
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
static_assert(offsetof(OceanUniforms, CameraX) == 56);
static_assert(offsetof(OceanUniforms, CameraY) == 60);
static_assert(offsetof(OceanUniforms, RiverTime) == 64);
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
static_assert(offsetof(OceanUniforms, BoatMotion) == 736);
static_assert(offsetof(OceanUniforms, FlowInfo) == 752);
static_assert(offsetof(OceanUniforms, Flow) == 768);
static_assert(sizeof(OceanUniforms) == 16336 && alignof(OceanUniforms) == 16);
static_assert(offsetof(OceanUniforms, Surface) == 3840);
static_assert(offsetof(OceanUniforms, UiInfo) == 16128);
static_assert(offsetof(OceanUniforms, UiRects) == 16144);
static_assert(offsetof(OceanUniforms, UiColors) == 16240);
// Within the engine's uniform bounds (16..16384, multiple of 16).
static_assert(sizeof(OceanUniforms) % 16 == 0 && sizeof(OceanUniforms) >= 16 && sizeof(OceanUniforms) <= 16384);

// Fill a uniform block from sanitized settings + clock + actual framebuffer
// extent. `widthPx`/`heightPx` are the ACTUAL acquired frame pixels
// (GetFrameInfo), guaranteeing aspect-correct, resize-stable wave scale.
[[nodiscard]] OceanUniforms
BuildUniforms(const OceanSettings& settings, const SceneClock& clock, uint32 widthPx, uint32 heightPx) noexcept;

} // namespace ludus::sandbox::ocean
