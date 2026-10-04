# Drift ocean validation

The current fluid solver and rendered surface are described in
[Ocean interactions](OCEAN_INTERACTIONS.md), with local evidence in
[OCEAN_WAVES_VALIDATION.json](OCEAN_WAVES_VALIDATION.json). The older build
identities and validation records below remain historical.

The current M0/M1 boat and ripple slice has its own [implementation and validation record](RIPPLE_GAME_IMPLEMENTATION.md). The ocean-only evidence below describes the earlier baseline.

The WebGL fallback work consumes the public installed SDK from [engine PR #56](https://github.com/Alegruz/Ludus/pull/56).
The exact engine commit is in `config/ludus-version.txt`; the tested Release ZIP
records that commit and the pinned toolchains in `build-info.json`.

## Current checks

The reference tools are Clang/LLVM 18, Slang 2026.1.2, the digest-pinned SPIR-V
validator and SPIRV-Cross `vulkan-sdk-1.4.313.0`. The browser SDK uses the engine's
pinned Emscripten toolchain. Shader compilers and translators are build tools;
none run during gameplay.

| Check | Result |
| --- | --- |
| Native and Release browser builds against installed SDK | Warning-clean with warnings as errors. |
| GPU-free ocean logic | 81 checks, zero failures; native CTest passed. |
| Logic ASan/UBSan with leak detection | 81 checks, zero failures. |
| Pinned Clang 18 format/tidy | Logic and changed browser integration passed. |
| Real shader compilation and validation | SPIR-V, WGSL and GLSL ES emitted; real `spirv-val` passed. |
| Uniform contract | 144 bytes in CPU, SPIR-V, WGSL and independently derived GLSL ES std140 layout. |
| Release archive | No debug wasm, developer paths or missing/external runtime assets; payload hashes and notices verified. |
| Exact archive browser QA | 13 cases passed in Chromium 140.0.7339.186 on SwiftShader. |

The browser cases verify actual presented ocean pixels and controls in both
backends, paused frame agreement (orientation and palette), pause/resume,
restart, fallback after missing WebGPU API/adapter/device/surface, strict forced
WebGPU failure, readable errors when both backends are unavailable, narrow
controls, DPR, an ordinary sandbox iframe, WebGL context loss/restart/resize,
and JavaScript/wasm network failure. Screenshots were visually inspected.
Machine-readable results and ZIP identity are in `webgl-validation.json`.

The harness injects a **test-only 100 ms RAF delay** to bound the expensive
procedural shader queue on software rendering. It does not measure interactive
frame rate. Production code has no such delay. See
[the browser test instructions](../tools/browser-tests/README.md) for exact
commands, launch flags and limitations.

## Remaining acceptance gates

CI results must be checked on this PR. The engine passed every local per-header
limit but exceeded its aggregate budget on a loaded host. The unchanged budget
subsequently passed on the CI reference runner for the pinned engine commit:
[engine CI run](https://github.com/Alegruz/Ludus/actions/runs/37086398722).
Native SDK and ASan/UBSan CI jobs also passed there. No budget was relaxed.
Physical WebGPU/WebGL 2 devices, flag-free browser compatibility, native
Wayland/Vulkan presentation, hosted HTTPS/itch.io acceptance, frame-rate targets
and long-session resource measurements remain unverified. Software rendering
establishes actual shader/pixel behavior but does not establish those targets.

On a target device, serve the Release files over HTTPS or localhost. Exercise
Auto, `?backend=webgpu` and `?backend=webgl2`, panel controls, settings export and
import, hide/resume, restart, DPR/resize and iframe embedding. Record browser,
adapter, resolution, frame time and observed resource behavior. For native
presentation, run `LudusSandbox` on Wayland with a Vulkan GPU. Report measured
results rather than inferring performance from shader complexity.

## Continuous integration

The `fast` job runs Clang 18 format/tidy and GPU-free logic tests. The `native`
job acquires the pinned SDK and builds/tests the native application with real
SPIR-V validation. The `web` job builds a Release browser SDK and application,
packages/extracts the exact ZIP, runs the pinned Playwright/SwiftShader checks,
and uploads the tested ZIP and evidence. CI status is reported by the PR;
previous green runs do not establish the result of this revision.

## Historical baseline

The original Kiro implementation was validated on Amazon Linux 2023 with glibc
2.34, where the pinned validator could not run and no GPU presentation was
possible. Its 81 logic checks, shader compilation and offline CPU screenshots
were useful baseline evidence. The current reference-host builds and real
software-GPU checks above supersede those limitations. Existing
`docs/screenshots/` images remain authored-look previews, not hardware proof.

The original SDK Threads workaround remains in the sandbox's CMake consumer.
The current engine SDK packaging and external consumer checks pass; removing
that compatibility workaround is separate from the rendering fix.

## Editor release integration

The game-owned release profile uses the shared Ludus packager and an explicit
`GameRelease` CMake install component. The exact archive SHA256
`4d130b1dead7de561df8b25fd1992c4d742980efeccaaf909172021928eafc76`
was built against browser Release SDK revision
`70f9debf16a7ec53e33e0f9b87a4106c33896c7a`, statically verified, extracted,
and passed all 13 existing Chromium cases. WebGPU/WebGL 2 pixels, controls,
pause/restart, fallback, responsive/DPR/iframe, context loss and asset failures
were exercised. Desktop and narrow screenshots were visually reviewed. This
is software-GPU evidence with a test-only 100 ms RAF delay, not hardware
performance or hosted itch.io acceptance. See
[editor-release-validation.json](editor-release-validation.json).

The real Qt Editor controller opened this project and packaged its `web-release`
profile through the asynchronous adapter, reporting `Ok`, confirmed cleanup,
and zero dropped output. Four release bootstrap tests and 81 GPU-free checks
passed; ASan/UBSan with leak checks passed. The final browser app built with
warnings as errors, and touched C++ files passed pinned clang-format 18.

The tag/manual release workflow runs those browser tests against the shared
packager's archive before transferring it to the separate upload job.
Configure `ITCH_IO_TARGET` and environment `itch-release`/`BUTLER_API_KEY`
as described in [RELEASING.md](../RELEASING.md) before the first live release.
No live upload was performed during this implementation.
