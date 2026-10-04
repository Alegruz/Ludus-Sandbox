# Exact-package browser checks

After building and packaging Release, extract the ZIP and run this pinned
Playwright 1.55.1 / Chromium 140.0.7339.186 harness:

```bash
python3 -m zipfile -e out/packages/drift-ocean-web-release.zip out/browser-qa/extracted
cd tools/browser-tests
npm ci
npx playwright install --with-deps chromium
npm test
```

The harness verifies all payload hashes, then serves those exact files with
ordinary localhost headers. It uses real WGSL/GLSL ES compilation and presented
pixels on SwiftShader. Cases cover backend pixel agreement, actual controls,
pause/resume, restart, startup fallback, forced failure, narrow status/control
layout, DPR, a sandbox iframe, context loss and asset failure.

A test-only 100 ms RAF delay bounds the procedural shader queue on software
rendering. The recorded flags and delay do not establish physical-GPU support,
flag-free user compatibility, interactive performance or hosted acceptance.
Screenshots, requests, ZIP identity and results go to `out/browser-qa/results`.
The harness and its dependencies are never included in the game package.
On a busy software-rendering host, `DRIFT_QA_RAF_MS=500` increases the test-only
delay for the renderer, game and gesture harnesses. The report records the actual delay;
this setting changes neither the packaged game nor the assertions.

## Mobile HUD

`npm run test:ui -- ../../out/browser-qa/extracted ../../out/browser-qa/ui-results`
checks portrait phones, short embeds, landscape, simulated safe-area insets,
44 px touch targets and a canvas clear of persistent HUD controls. Help freezes
the game, owns keyboard focus and restores the previous pause choice when closed
by its button or Escape. Real touch verifies the fitted, inset canvas mapping;
graphics restart and tuning mode retain the layout. Both renderers save screenshots.
`DRIFT_UI_SCALE=0.5` selects the software-GPU CI scale.
The harness uses a test-only 500 ms RAF delay to bound software rendering work.

## Boat and ripple controls

After the same extraction, use the command below for gameplay checks, including
rock rejection, crashing, rescue docking and Retry.
Without arguments, `npm run test:game` targets the development build.
These verify outward mouse/touch pushes, exactly one ring per placement, paused
input rejection, reset, fitted resize/DPR mapping, tuning-mode transitions,
blur/focus, UI input isolation, pointer cancellation and input after a graphics
restart on WebGPU and WebGL 2. Screenshots and results go to `out/ripple-qa` by
default. To use an explicit artifact directory:

```bash
npm run test:game -- ../../out/browser-qa/extracted ../../out/browser-qa/ripple-results
```

This harness uses a test-only 250 ms RAF delay to
bound software-rendering work. Touch is
emulated, not a physical mobile acceptance result.
The test waits for reset/mode telemetry instead of assuming a 150 ms update.
`DRIFT_GAME_FILTER` selects a named case; `DRIFT_GAME_DPR=2` optionally increases
the emulated touch framebuffer; the software-renderer default is 1.

## Ocean gestures

`npm run test:gestures -- ../../out/browser-qa/extracted ../../out/browser-qa/gesture-results`
checks actual mouse strokes and CDP touch drags on WebGPU and WebGL 2. Cases
cover traveling-wave transport, both vortex directions, exactly one effect per
stroke, frozen water pixels on pause, restart preservation, reset, secondary
pointer rejection and cancellation. Screenshots include ambient water, a wave,
and clockwise/counterclockwise swirls. `DRIFT_GESTURE_FILTER` optionally selects
one case by name, such as `webgl2-touch`.
`DRIFT_GESTURE_DPR=2` enables higher-resolution emulated touch captures; the
report records the tested DPR (default 1, matching the rescue-course harness).
The gesture harness uses a test-only 500 ms RAF delay. Paused captures wait for
three frozen frames to render before the first capture, then two new frames
before the second. The exact comparison excludes the outer four CSS pixels of the canvas,
which contain the independently composited focus outline; the fitted water is
fully inside this region. Both comparison screenshots are saved.
`DRIFT_GESTURE_SCALE=0.5` selects the CI framebuffer scale.

## CI execution

CI runs on pull requests and pushes to `main`; a new PR update cancels its
superseded run. The build job packages one Release ZIP, and every browser job
downloads and extracts that same artifact. Pixels, UI, waves and fallback run
in one job. Gameplay and gestures run in four independent jobs:
`webgpu-mouse`, `webgpu-touch`, `webgl2-mouse` and `webgl2-touch`.
All jobs must succeed before the existing browser check passes.

To reproduce one gameplay/gesture shard locally after extraction:

```bash
DRIFT_GAME_FILTER=webgpu-touch DRIFT_GAME_SCALE=0.5 npm run test:game -- ../../out/browser-qa/extracted ../../out/browser-qa/ripple-results
DRIFT_GESTURE_FILTER=webgpu-touch DRIFT_GESTURE_SCALE=0.5 npm run test:gestures -- ../../out/browser-qa/extracted ../../out/browser-qa/gesture-results
```

Filters matching no cases fail. Omitting a filter still runs all four cases.
`node --test cases.test.mjs` verifies selection without downloading browsers.
Native dependencies, shader tools, compiler outputs and the pinned Emscripten
toolchain are cached by their input identities. Setup regenerates local presets
and uses the engine-managed CMake/Ninja; build trees and machine settings are
not restored from caches.
