#pragma once

// Ocean playground tuning model (Sandbox-owned, GPU-free).
//
// This header defines the authoritative scene/tuning state for the "Drift"
// ocean playground. It deliberately contains NO engine, GPU, or RHI types:
// the engine is unaware of ocean controls (see fullscreen-rendering-handoff.md).
// Everything here is plain data plus pure functions, so it is unit-testable on
// any host without a GPU or an installed Ludus SDK.
//
// Numeric aliases follow the Ludus convention (float32/float64/uint32/...).
// When built as part of the SDK consumer, these come from
// <ludus/foundation/base/types.h>. For the standalone host tests we provide a
// tiny shim (see tests), so this header only needs the aliases to exist in the
// enclosing namespace.

#include "ocean/numeric.h"

#include <string>
#include <string_view>

namespace ludus::sandbox::ocean
{
using numeric::float32;
using numeric::float64;
using numeric::int32;
using numeric::uint32;

// ---------------------------------------------------------------------------
// World units and coordinate orientation (documented contract)
// ---------------------------------------------------------------------------
//
// The ocean is viewed through an ORTHOGRAPHIC, directly-overhead camera. There
// is no perspective and no horizon. We author everything in "world meters".
//
//   * World space is a 2D plane measured in meters. +X points right (east),
//     +Y points up on screen (north). The camera looks straight down the -Z
//     axis onto this plane.
//   * The viewport always shows a fixed VERTICAL extent of the world
//     (kWorldViewHeightMeters), regardless of window size or aspect ratio. The
//     horizontal extent is derived from the aspect ratio so that one world
//     meter is the same number of pixels horizontally and vertically. This is
//     what keeps wave scale consistent on resize and on wide/tall windows:
//     stretching the window reveals MORE ocean sideways rather than distorting
//     the waves.
//   * Screen space uses a top-left origin with +Y pointing DOWN, matching the
//     engine's fragment coordinate convention (GetFrameInfo / SV_Position).
//     The shader flips Y when converting to world space so that current/wave
//     "up" matches north.
//
// A fragment at pixel (px, py) in a WxH framebuffer maps to world coordinates:
//
//   metersPerPixel = kWorldViewHeightMeters / H
//   worldX = (px - W * 0.5) * metersPerPixel
//   worldY = (H * 0.5 - py) * metersPerPixel      // note Y flip
//
// This same mapping is implemented in ScreenToWorld() below (CPU, for tests)
// and in ocean.slang (GPU). Keeping one documented formula in both places is
// what makes the mapping aspect-correct and resize-stable.

inline constexpr float32 kWorldViewHeightMeters = 100.0F;

struct Vec2 final
{
    float32 X = 0.0F;
    float32 Y = 0.0F;
};

// Aspect-correct screen -> world mapping. widthPx/heightPx are framebuffer
// pixels. Returns world meters with +Y up. Safe for zero/!finite inputs.
[[nodiscard]] Vec2 ScreenToWorld(float32 pixelX, float32 pixelY, uint32 widthPx, uint32 heightPx) noexcept;

// Meters covered horizontally given a framebuffer aspect ratio. Vertical extent
// is always kWorldViewHeightMeters.
[[nodiscard]] float32 WorldViewWidthMeters(uint32 widthPx, uint32 heightPx) noexcept;

// ---------------------------------------------------------------------------
// RGB color (linear 0..1). Palette authoring lives in the application.
// ---------------------------------------------------------------------------
struct Color final
{
    float32 R = 0.0F;
    float32 G = 0.0F;
    float32 B = 0.0F;
};

// ---------------------------------------------------------------------------
// The tuning parameters. Bounds are enforced by Validate()/Clamp(); every
// field is a finite value within [min, max]. Separate bulk CURRENT (advection)
// from WAVE animation, per the design.
// ---------------------------------------------------------------------------
struct OceanSettings final
{
    // --- Bulk current (advection of the whole color field) ---
    // Direction in degrees, 0 = +X (east), 90 = +Y (north). Any finite value is
    // accepted and normalized into [0, 360) by Clamp().
    float32 CurrentDirectionDegrees = 35.0F;
    // Current speed in world meters per second.
    float32 CurrentSpeedMetersPerSecond = 1.5F;

    // --- Wave animation (independent of current) ---
    // Spatial scale of the dominant wave, in world meters per wavelength.
    float32 WaveScaleMeters = 18.0F;
    // Temporal animation speed multiplier (dimensionless; 1 = nominal).
    float32 WaveAnimationSpeed = 1.0F;
    // Wave intensity / contrast of the two overlapping patterns (0..1).
    float32 WaveIntensity = 0.55F;

    // --- Foam / highlights ---
    // Foam amount: higher reveals more broken foam streaks (0..1).
    float32 FoamAmount = 0.35F;

    // --- Palette (restrained: deep navy, blue, teal, warm pale foam) ---
    Color DeepColor = {.R = 0.031F, .G = 0.118F, .B = 0.165F};    // ink navy
    Color MidColor = {.R = 0.063F, .G = 0.247F, .B = 0.302F};     // painted ocean blue
    Color ShallowColor = {.R = 0.157F, .G = 0.427F, .B = 0.439F}; // muted teal
    Color FoamColor = {.R = 0.953F, .G = 0.933F, .B = 0.863F};    // parchment foam
};

// Numeric bounds (inclusive). Exposed so UI and tests share one source.
struct Bounds final
{
    float32 Min = 0.0F;
    float32 Max = 0.0F;
};

inline constexpr Bounds kCurrentSpeedBounds = {.Min = 0.0F, .Max = 12.0F};
inline constexpr Bounds kWaveScaleBounds = {.Min = 3.0F, .Max = 60.0F};
inline constexpr Bounds kWaveAnimationSpeedBounds = {.Min = 0.0F, .Max = 4.0F};
inline constexpr Bounds kWaveIntensityBounds = {.Min = 0.0F, .Max = 1.0F};
inline constexpr Bounds kFoamAmountBounds = {.Min = 0.0F, .Max = 1.0F};
inline constexpr Bounds kColorChannelBounds = {.Min = 0.0F, .Max = 1.0F};

// Current schema version for exported JSON. Bump on breaking changes.
inline constexpr int32 kSettingsSchemaVersion = 1;

// Preset identities.
enum class Preset : uint32
{
    Calm,
    Drifting,
    Choppy
};

[[nodiscard]] OceanSettings MakePreset(Preset preset) noexcept;
[[nodiscard]] OceanSettings DefaultSettings() noexcept; // == Drifting

// Clamp every field into its bounds and normalize direction. Non-finite inputs
// are replaced with the corresponding default field so the scene never sees a
// NaN. Returns the sanitized copy.
[[nodiscard]] OceanSettings Clamp(const OceanSettings& settings) noexcept;

// Validate reports whether `settings` is already finite and within bounds, with
// a friendly message naming the first offending field. Used for import.
struct ValidationResult final
{
    bool Ok = true;
    std::string Message; // empty when Ok
};
[[nodiscard]] ValidationResult Validate(const OceanSettings& settings) noexcept;

// ---------------------------------------------------------------------------
// Versioned JSON round-trip. Hand-rolled (no heavy JSON dependency) so the
// settings module stays dependency-free and the format is explicit/stable.
// ---------------------------------------------------------------------------

// Serialize to a versioned JSON object string. Always emits sanitized values.
[[nodiscard]] std::string ToJson(const OceanSettings& settings) noexcept;

// Parse JSON produced by ToJson (or compatible). On success, out holds the
// imported settings (already validated AND clamped). On failure, returns a
// friendly error and leaves `out` untouched.
struct ParseResult final
{
    bool Ok = false;
    std::string Message; // friendly error on failure; empty on success
    OceanSettings Value;
};
[[nodiscard]] ParseResult FromJson(std::string_view json) noexcept;

} // namespace ludus::sandbox::ocean
