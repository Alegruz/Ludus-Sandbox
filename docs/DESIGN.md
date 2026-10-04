# Design: Drift ocean playground

## Goals

A top-down ocean playground with an evolving shallow-water surface. Wind seeds
multiple gravity modes; splashes and pointer momentum feed the same solver.
Surface gradients drive lighting and reflection, and transported foam makes
compression visible. See [Ocean interactions](OCEAN_INTERACTIONS.md) for the
current physics and rendering contract.

## Architecture: scene/tuning state lives outside GPU code

```
                 reads (never mutates)
  SettingsStore  ───────────────────────►  OceanScene ──► Ludus public RHI
  (the bridge)                               (lifecycle)    (rhi.h/render.h)
     ▲                                           │ uploads
     │ writes                                    ▼
  UI / config  ──────────────────────────►  OceanUniforms  ◄── matches ──► shaders/ocean.slang
  (DOM panel / CLI / JSON)                   (CPU layout)
```

* `src/ocean/ocean_settings.*` — the authoritative tuning model: parameters,
  bounds, presets, finite/bounded validation, and versioned-JSON round-trip.
  **No engine, GPU, or RHI types.**
* `src/ocean/ocean_clock.*` — the scene clock: separates bulk-current advection
  from wave animation, integrates current displacement continuously, clamps
  resume deltas, freezes while hidden, and preserves precision over long
  sessions.
* `src/ocean/ocean_uniforms.*` — the CPU uniform block and the function that
  builds it from settings + clock + the actual framebuffer extent. The layout
  mirrors the Slang constant buffer exactly and is `static_assert`-checked.
* `src/ocean/settings_store.h` — the single explicit bridge between controls and
  the scene. Everything is sanitized on the way in, so the scene reads a valid,
  finite value every frame.
* `src/ocean/ocean_scene.*` — drives the Ludus public fullscreen RHI lifecycle.
  It reuses the engine's documented lifecycle semantics without touching any
  private/backend code. **The engine APIs remain unaware of ocean controls.**

Because the first four are pure, they are unit-tested on any host without a GPU
or an installed SDK (`tests/ocean_tests.cpp`).

## World units and coordinate orientation (contract)

* **World space** is a 2D plane measured in **meters**. `+X` is right (east),
  `+Y` is up on screen (north). The camera is orthographic and looks straight
  down; there is no perspective and no horizon.
* The viewport always shows a **fixed vertical extent in ocean tuning mode** of the world,
  `kWorldViewHeightMeters = 100 m`, regardless of window size or aspect ratio.
  The horizontal extent is derived from the aspect ratio
  (`WorldViewWidthMeters = 100 m × width/height`). This keeps **one world meter
  the same number of pixels on both axes**, so resizing or using a wide/tall
  window reveals more or less ocean rather than stretching the waves.
* **Screen space** uses a **top-left origin** with `+Y` pointing down, matching
  the engine's fragment coordinate convention (`SV_Position` / `GetFrameInfo`).
  The mapping flips Y so world "up" is north.

Screen → world (implemented identically on CPU in `ScreenToWorld()` and on GPU
in `ocean.slang`):

```
metersPerPixel = kWorldViewHeightMeters / frameHeightPixels
worldX = (pixelX - frameWidth  * 0.5) * metersPerPixel
worldY = (frameHeight * 0.5 - pixelY) * metersPerPixel   // Y flip
```

Keeping one documented formula in both the CPU and GPU paths is what makes the
mapping aspect-correct and resize/DPR-stable. The uniform always carries the
**actual acquired** frame extent (from `GetFrameInfo` after `BeginFrame`), so a
native extent negotiation or a browser DPR change cannot distort wave scale.

## Current vs. wave animation (the "Drift" continuity contract)

The scene clock keeps **two independent accumulators**:

* **`WaveTime`** drives the two overlapping wave patterns. It advances by
  `delta × waveAnimationSpeed` and wraps every `kWavePhaseWrap = 3600 s` to keep
  float precision indefinitely.
* **`CurrentOffset` (meters)** is the time-integral of the current velocity
  `v = speed · (cos dir, sin dir)`. The shader samples the broad color field at
  `world − currentOffset`, so the patches **drift continuously**. Because the
  already-accumulated displacement lives in `CurrentOffset`, changing current
  speed or direction only changes future motion — the pattern never teleports.
  The offset wraps on a large world period (`kCurrentWrapMeters = 100 km`) that
  is a multiple of the shader's spatial period, so wrapping is seamless and
  precise over long sessions.

Robustness rules (all in `SceneClock::Advance`, all unit-tested):

* **Resume clamp.** Raw wall-clock deltas are sanitized to
  `[0, kMaxResumeDeltaSeconds = 0.1 s]`. A long stall (tab hidden, breakpoint,
  GC pause) advances at most 100 ms, so resuming never jumps the ocean.
* **Freeze while hidden.** When the page/window is not visible the clock does
  not advance (the renderer keeps drawing the frozen frame). The web driver
  wires this to `document.visibilitychange`; the native driver treats the
  window as visible.
* **Pause.** The user pause flag freezes time the same way but is distinct from
  hidden, so pausing preserves the scene and the last frame keeps drawing.
* **Precision.** Both accumulators are `float64` and wrapped, then converted to
  `float32` for upload, so sub-frame resolution survives hours of play.

## The uniform contract

`OceanUniforms` (CPU, `src/ocean/ocean_uniforms.h`) mirrors the `OceanUniforms`
constant buffer in `shaders/ocean.slang`. It is **set/group 0, binding 0**,
visible to both stages, **16128 bytes** within the engine's 16..16384-byte bound.
Every offset is asserted in C++ and checked against all generated shader targets.

The original ocean/game fields occupy bytes 0–735. Boat motion starts at 736,
flow metadata at 752, the 16 × 24 transport snapshot at 768, and the complete
48 × 64 height/foam surface at 3840. Each surface cell stores signed 16-bit
height over ±6 m and eight-bit foam in one exact 24-bit float integer. Normals
come from derivatives of a nine-sample quadratic B-spline reconstruction.
Legacy ripple fields remain for diagnostic compatibility; they draw no rings
and apply no boat impulses.

In boat mode, the camera fits a 60 × 80 m placement area with a 5 m margin:
`viewHeight = max(90, 70 / aspect)` and `viewWidth = viewHeight * aspect`.
Screen mapping uses that fitted extent on both CPU and GPU. See the
[implementation record](RIPPLE_GAME_IMPLEMENTATION.md) for gameplay timing.

## Palette

A restrained palette authored in the application (never in engine code): deep
navy → ocean blue → teal, broad-banded by a height-ish signal, with warm pale
foam blended on the crest streaks. Palette and all four colors are live controls.

## Presets

| Preset | Current | Waves | Foam |
| --- | --- | --- | --- |
| Calm | slow (0.5 m/s), shallow angle | large scale (26 m), gentle (0.30) | sparse (0.12) |
| Drifting (default) | moderate (1.5 m/s) | 18 m, nominal (0.55) | moderate (0.35) |
| Choppy | fast (3.5 m/s), steeper angle | tight (10 m), strong (0.85) | heavy (0.70) |
