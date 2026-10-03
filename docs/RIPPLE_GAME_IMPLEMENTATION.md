# Drift first playable slice

Implemented October 3, 2026. This is M0/M1: an open-water trial with one disabled
boat, point ripples, pause/reset and the existing ocean tuning mode. M2 supplies
rocks, collision, docking and crash/retry. There is no win/loss condition yet.

## Play

Build the browser Development preset and serve its build directory, following
[BUILD.md](BUILD.md). Click or tap inside the pale rectangular water boundary.
Each ring pushes the boat radially away from its origin once when it reaches the
hull. Place behind to accelerate, beside to turn, or ahead to oppose motion.
A tap exactly at the boat center produces no impulse; it never creates an
undefined direction. Pause rejects placement; Reset boat clears the entire
simulation while retaining the pause state. Boat mode toggles the open-water
trial and ocean-only tuning view.

The browser is the playable target. Native Vulkan compilation and public SDK
linkage pass, but native pointer input is not wired and native rendering was
not exercised. Mouse and emulated touch pass; physical mobile acceptance and
hardware performance remain open.

## Ownership and tuning

All gameplay lives in Ludus-Sandbox:

| File | Responsibility |
| --- | --- |
| `src/game/ripple_game.*` | GPU-free fixed-tick simulation, input queue and swept ring/hull contact |
| `src/game/game_render.*` | Interpolated, read-only shader snapshot |
| `src/ocean/ocean_scene.*` | Public SDK graphics lifecycle and game/presentation timing |
| `src/main_web.cpp`, `web/shell.html` | DOM controls, primary pointer input and normalized CSS coordinates |
| `shaders/ocean.slang` | Procedural ocean, rings, boat and contact flash |

No engine source or private headers are copied into the game. Slang remains the
shader authoring language. The existing build helper generates SPIR-V, WGSL and
GLSL ES; no runtime shader compiler, GPU readback or water-fluid solver is used.

| Setting | Initial value |
| --- | --- |
| Tick | 1/60 s |
| Catch-up | At most 4 ticks/frame; input delta clamped to 0.1 s; excess debt discarded and counted |
| Ripple pool / pending input | 16 / 8 fixed records |
| Ring speed / lifetime / half width | 20 m/s / 1.5 s / 0.35 m |
| Cooldown | 15 ticks (0.25 s) |
| Push | 2.5 m/s × (1 − contact radius / 30 m) |
| Drag | Exponential, coefficient 0.7/s |
| Speed cap | 7 m/s |
| Boat contact radius | 2 m, enlarged from the initial design sketch for readability |
| Placement area | 60 × 80 m, centered at the origin |

A ring uses a swept interval against the moving hull, including tangency and
coincident origins. Contacts are ordered by hit time and then creation ID; each
ring can affect the boat once. Impulses change end-of-tick velocity and therefore
the next tick's displacement. Rendering interpolates the previous/current boat
position and ring ages. Pause, blur, hidden state, mode changes and graphics
restart discard queued input and frame debt; Reset also clears contacts,
cooldown, rings, boat state and counters. Graphics restart preserves the world.

The upload block is 432 bytes. Original ocean fields remain at offsets 0–143,
boat data starts at 144, game data at 160 and sixteen vec4 ring records at 176.
The CPU array and the individually named Slang ring fields have identical
contiguous layout. Named fields accommodate the SDK's present WebGL 2 reflection
subset. C++ assertions and per-target shader reflection validate these offsets.

The fitted camera uses `height = max(90, 70/aspect)`, `width = height*aspect`.
Pointer positions are normalized against the current canvas CSS rectangle and
mapped through the camera used for the rendered framebuffer. Delegated input
survives canvas replacement during backend fallback and graphics restart.

## Validation

The laptop uses prepared local SDK overrides whose manifests record Ludus
`652ea51a19d7`: native Development and browser Release SDKs. This is distinct
from the repository bootstrap pin `b87895f291575f57c090cd72e7dd2d145eb6ad8c`.
Fresh bootstrap against that pin is exercised by PR CI. The pin was corrected
from the unpublished pre-merge commit to the merged commit with identical files. The package records the SDK's
actual revision separately from the checkout used to supply build tools.

Verified with managed CMake/Ninja, Clang 18.1.3, Slang 2026.1.2, the pinned
SPIR-V validator/translator and Emscripten 4.0.23:

- Actual configure/build/test preset discovery and read-only setup doctor pass.
- Native Development, browser Development and browser Release builds are warning-clean.
- CTest passes both ocean and ripple suites; ripple physics has 63 checks,
  including outward steering, braking, turning, swept contacts, queue admission,
  catch-up, pause/reset, presentation-rate independence, camera and snapshots.
- The same 63 ripple checks pass ASan/UBSan with leak detection enabled.
- Clang-format 18 passes repository C++ sources. Clang-tidy 18 passes changed
  simulation/render/scene sources and the browser bridge with its web sysroot
  and actual exception-free compile policy.
- Existing setup/bootstrap Python tests pass (18 tests).
- Generated SPIR-V validates; WGSL and generated GLSL ES compile and present
  actual pixels in Chromium 140.0.7339.186 using SwiftShader.
- The existing 13-case exact-archive suite passes: ocean pixels/controls,
  backend agreement, fallback, explicit failure, narrow layout/DPR/iframe,
  context loss/restart and asset failures.

Run the game browser checks against the extracted Release archive:

```bash
python3 -m zipfile -e out/packages/drift-ocean-web-release.zip out/browser-qa/extracted
cd tools/browser-tests
npm ci
npx playwright install --with-deps chromium
npm run test:game -- ../../out/browser-qa/extracted ../../out/browser-qa/ripple-results
```

All four exact-archive game cases pass. They cover WebGPU/WebGL 2 × mouse/emulated touch, outward impulse,
one ring per input, pause, reset, boat pixels, resize/DPR, mode switching,
blur/focus, UI input isolation, cancellation and input after graphics restart.
The harness delays RAF by 50 ms to bound software-rendering work. This validates
behavior and real shader pixels; it is not a frame-rate measurement. A fresh
player trial, physical touch, physical GPU/native execution, hidden-tab scheduling
on real browsers and hosted/itch.io deployment are still acceptance work.

Screenshots and browser reports are saved with this record. The Release ZIP is
`out/packages/drift-ocean-web-release.zip`; the per-file hashes and actual SDK
identity are in its `build-info.json`. Publishing is not part of this slice.

Tested ZIP SHA-256: `8e0a132c7a331754b25fe15afbafc08bd0dd54013fe2e7b87d00348e091a5e24`.
