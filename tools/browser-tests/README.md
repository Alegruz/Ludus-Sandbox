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

## Boat and ripple controls

After the same extraction, use the command below for the M1 gameplay checks.
Without arguments, `npm run test:game` targets the development build.
These verify outward mouse/touch pushes, exactly one ring per placement, paused
input rejection, reset, fitted resize/DPR mapping, tuning-mode transitions,
blur/focus, UI input isolation, pointer cancellation and input after a graphics
restart on WebGPU and WebGL 2. Screenshots and results go to `out/ripple-qa` by
default. To use an explicit artifact directory:

```bash
npm run test:game -- ../../out/browser-qa/extracted ../../out/browser-qa/ripple-results
```

This harness uses a 50 ms RAF delay to bound software-rendering work. Touch is
emulated, not a physical mobile acceptance result.
