#pragma once

// Scene clock: separates bulk-current advection from wave animation, integrates
// current displacement continuously, clamps resume deltas, freezes while hidden,
// and keeps elapsed-time precision over long sessions. GPU-free and testable.
//
// Why two separate accumulators?
//   * WaveTime drives the two overlapping wave patterns. It only needs to be a
//     smoothly advancing phase and may be paused/frozen.
//   * CurrentOffset is the integral of (current velocity) over visible time. We
//     integrate the DISPLACEMENT continuously, so changing current
//     speed/direction changes the velocity going forward but never teleports
//     the already-accumulated pattern. This is the "Drift" continuity contract.
//
// Precision over long sessions:
//   WaveTime is wrapped into a bounded phase window (kWavePhaseWrap) so a float
//   never loses sub-frame resolution after hours of play. The current offset is
//   kept as float64 meters and also wrapped to a large world period so it, too,
//   stays precise. Both wraps are multiples of the pattern period baked into the
//   shader, so wrapping is visually seamless.

#include "ocean/numeric.h"
#include "ocean/ocean_settings.h"

namespace ludus::sandbox::ocean
{
using numeric::float32;
using numeric::float64;

// Wave phase wraps every kWavePhaseWrap seconds (seconds * animationSpeed).
// Chosen large enough to be imperceptible but small enough to preserve float
// precision indefinitely.
inline constexpr float64 kWavePhaseWrap = 3600.0;

// Current offset wraps on this world-meter period on each axis. Must be a
// multiple of the shader's largest spatial period so wrapping is seamless.
// The shader's dominant features repeat on WaveScale-scaled lattices; a large
// common multiple keeps it seamless for any in-range WaveScale.
inline constexpr float64 kCurrentWrapMeters = 100000.0;

// Maximum simulated delta applied in a single tick. A long stall (tab hidden,
// breakpoint, GC pause) must not teleport the ocean on resume.
inline constexpr float64 kMaxResumeDeltaSeconds = 0.1;

struct SceneClock final
{
    // Accumulated wave animation phase, in "animation seconds".
    float64 WaveTime = 0.0;
    // Accumulated bulk-current displacement, world meters. +X east, +Y north.
    float64 CurrentOffsetX = 0.0;
    float64 CurrentOffsetY = 0.0;
    // Whether the simulation is paused by the user (distinct from hidden).
    bool Paused = false;
};

// Advance the clock by `rawDeltaSeconds` of wall-clock time.
//
//   * `visible`  - false while the tab/window is hidden; the clock FREEZES
//     (no wave or current advance) so hiding/resuming does not jump the scene.
//   * `rawDeltaSeconds` - may be negative (clock skew), huge (long stall), or
//     non-finite; it is sanitized to [0, kMaxResumeDeltaSeconds].
//   * When Paused, time does not advance either, but the function still returns
//     cleanly so the renderer keeps drawing the frozen frame.
//
// Current displacement integrates velocity derived from the CURRENT settings:
//   vx = speed * cos(dir),  vy = speed * sin(dir)
// so changing speed/direction only changes future motion.
void Advance(SceneClock& clock, const OceanSettings& settings, float64 rawDeltaSeconds, bool visible) noexcept;

// Sanitize a raw delta to the clamped, finite, non-negative range. Exposed for
// tests and reuse by the native/web drivers.
[[nodiscard]] float64 SanitizeDelta(float64 rawDeltaSeconds) noexcept;

// Reset accumulators (used by the "reset" control). Keeps Paused state.
void ResetTime(SceneClock& clock) noexcept;

} // namespace ludus::sandbox::ocean
