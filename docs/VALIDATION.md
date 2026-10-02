# Validation results (honest)

This records exactly what was verified, in which environment, and what still
requires a GPU host. Mocks cannot prove appearance; interactive GPU verification
is called out explicitly below and is **not** claimed as done where it was not.

## Environment used for the checks below

* OS: Amazon Linux 2023 (dnf), x86_64, **glibc 2.34**.
* Compiler: Clang/clang-tidy/clang-format **18.1.8**.
* Pinned Slang **2026.1.2** acquired via the engine `shader-probe` (works here).
* Pinned `spirv-val` digest **matches the engine pin**, but it requires glibc
  **2.38+** and therefore does **not run** on this host.
* **No GPU** (`/dev/dri` absent, no `libvulkan`, no Wayland). So neither the
  native Vulkan scene nor real browser WebGPU can execute in this environment —
  the same reason the engine's own fullscreen handoff states its GPU validation
  ran "outside sandbox restrictions."

## What was verified here

| Check | Command | Result |
| --- | --- | --- |
| GPU-free logic tests (settings validation, JSON round-trip, coordinate mapping, aspect/resize stability, current/time continuity, long-session precision) | `clang++ -std=c++23 -Wall -Wextra -Wpedantic -fno-exceptions -I src tests/ocean_tests.cpp src/ocean/*.cpp && ./a.out` | **81 checks, 0 failures.** Warning-clean. |
| Uniform CPU/GPU layout agreement | `static_assert`s in `src/ocean/ocean_uniforms.h` (compiled above) | **Pass** — 144-byte block, every offset asserted. |
| Shader compiles to SPIR-V (both stages) + WGSL with the **pinned** Slang | `slangc … -target spirv …` / `-target wgsl …` | **Pass** — vertex SPIR-V, fragment SPIR-V, combined WGSL all emit cleanly. |
| Shader reflection matches the CPU contract | Slang `-reflection-json` | **Pass** — one constant buffer at set/group 0, binding 0, size **144** (= `sizeof(OceanUniforms)`); SPIR-V entries `main`/`main`, WGSL entries `vertexMain`/`fragmentMain`. |
| Generated `ocean.h` factory header | engine `cmake/shaders/compile_shader.py` driver | **Pass** — produces `ludus::shaders::ocean::Vertex()/Fragment()` with `UniformSize = 144`, exactly what `ocean_scene.cpp` calls. |
| Integration code is API-correct | `clang++ -std=c++23 -fsyntax-only -Wall -Wextra -fno-exceptions` against the **real** engine public headers + generated `ocean.h` | **Pass** for `src/ocean/ocean_scene.cpp` and `src/main_native.cpp`. |
| Static analysis | `clang-tidy` (engine `.clang-tidy`) on `src/ocean/*.cpp` | **Clean.** |
| Formatting | `clang-format` (engine `.clang-format`) | Applied to all `src/`/`tests/` C++. |
| Python driver syntax | `python3 -m py_compile scripts/python/sandbox.py` | **Pass.** |
| Authored appearance (offline) | `tools/preview/ocean_preview.cpp` (faithful CPU port of the shader) rendered representative PNGs | Captured; see `docs/screenshots/`. This shows the authored look; it is **not** GPU proof. |

### Important caveat on the SPIR-V validation step

The real build pipeline runs `spirv-val --target-env vulkan1.1` on each SPIR-V
module. The pinned validator binary could not execute on this glibc-2.34 host
(it needs glibc 2.38+). The SPIR-V itself was produced by the pinned Slang
without errors; genuine `spirv-val` validation must be run on a glibc-2.38+ host
(e.g. the reference Ubuntu 24.04). On that host the standard build performs this
automatically — no code change is needed.

## What still requires a GPU host (NOT verified here)

These are the acceptance items that need a Wayland+Vulkan GPU or a WebGPU browser
and were therefore **not** executed in this environment. They are expected to
pass given the API-correct integration, but are honestly reported as unverified:

* The animated ocean rendering through Ludus end-to-end (native and browser).
* Live controls visibly affecting the running scene; pause preserving the scene;
  reset/presets; exported-settings round-trip in the live app.
* Resize / DPR / wide+tall windows / tab hide+resume / repeated restart behaving
  without stretching or resource leaks **at runtime**. (The *math* for
  aspect/resize stability and the continuity/freeze/clamp logic is unit-tested;
  the GPU resource lifecycle is driven by the engine's documented, already-tested
  RHI contract.)
* Measured frame timing / 60 fps at 1080p, and representative GPU screenshots.

### How to reproduce the remaining checks on a capable host

1. On Ubuntu 24.04 (glibc 2.38+, Clang/LLD 18) with a Vulkan GPU:
   ```bash
   ./init.sh
   cmake --build --preset linux-clang-development
   ctest --test-dir out/build/linux-clang-development --output-on-failure
   ./out/build/linux-clang-development/LudusSandbox         # observe the ocean
   ```
   Record the adapter (`vulkaninfo`), resolution, and measured frame time.
2. For the browser, on a machine with a WebGPU browser:
   ```bash
   cmake --build --preset web-emscripten-development
   python3 -m http.server 8000 --bind 127.0.0.1 \
     --directory out/build/web-emscripten-development
   ```
   Open `http://127.0.0.1:8000/`, exercise the panel (presets, sliders,
   pause/reset, export/import), and capture screenshots + the browser's frame
   timing. Report the browser/device/resolution and actual measured fps. Do not
   claim support for untested devices.

## Continuous integration

`.github/workflows/ci.yml` runs on every push and pull request, in three tiers:

| Job | Runner | What it proves |
| --- | --- | --- |
| **fast** | ubuntu-24.04 | Formatting (`clang-format-18 --dry-run --Werror`), `clang-tidy-18` with warnings-as-errors on the GPU-free sources, and the host logic tests (`tests/ocean_tests.cpp`). No engine SDK needed, so it is the quick gate on most changes. |
| **native** | ubuntu-24.04 | `./init.sh` acquires the pinned Ludus SDK + Slang/SPIRV-Tools, builds the native (Vulkan) app — including the Slang → SPIR-V/WGSL shader build, with the pinned `spirv-val` actually running (ubuntu-24.04 has glibc 2.38+) — runs `ctest`, and asserts the generated shader artifacts (`ocean.h`, `*.spv`, `ocean.wgsl`) are present in the build. **This job passes green on CI**, confirming end-to-end SDK linkage and the real SPIR-V validation that the GPU-less dev sandbox could not run. |
| **web** | ubuntu-24.04 | `./init.sh --with-web` acquires the Emscripten toolchain, builds the engine web tree and installs it to a prefix, configures and builds the browser package, verifies `index.html`/`index.js`/`index.wasm`, and uploads it as an artifact. **This job passes green on CI.** |

`native` and `web` depend on `fast`. All three jobs pass green on CI, so each is
a hard gate. The `native` job proves end-to-end SDK linkage and the real
`spirv-val` SPIR-V validation; the `web` job proves the Emscripten/WebGPU
package builds and ships. (The web SDK is consumed via a build-tree
`cmake --install` because the engine exposes no relocatable web SDK through
`install-sdk`; see `install_ludus_web_sdk` in `scripts/python/sandbox.py`.)

GPU rendering, interactive controls, and frame timing are still **not**
exercised (headless runners have no GPU); CI proves the app formats, analyzes,
tests, links against the SDK, and that the shader artifacts build and ship. A
follow-up could add a software-GPU browser smoke (Playwright + SwiftShader under
`xvfb`), mirroring the engine's `webgpu-probe` workflow.

## Known engine SDK packaging gap (Threads)

While wiring CI we hit a genuine engine SDK packaging bug, surfaced on a clean
`ubuntu-24.04` runner consuming the installed SDK:

```
CMake Error at .../lib/cmake/Ludus/LudusTargets.cmake (set_target_properties):
  The link interface of target "Ludus::FoundationProfiling" contains:
    Threads::Threads
  but the target was not found.
```

`modules/foundation/profiling/CMakeLists.txt` links `Threads::Threads` **PUBLIC**
(`find_package(Threads REQUIRED)` + `target_link_libraries(... PUBLIC
Threads::Threads)`), so the imported target leaks into the SDK's exported link
interface. But `cmake/LudusConfig.cmake.in` only re-finds `volk` and
`PkgConfig`/Wayland — it never `find_dependency(Threads)`. Any installed-SDK
consumer that pulls `Ludus::FoundationProfiling` (directly or transitively via
`Ludus::GraphicsRhi`) therefore fails at `find_package(Ludus)` unless it first
resolves `Threads::Threads` itself. The engine's own `install-sdk` consumer
self-test trips this too.

**Exact engine change needed:** add a `Threads` dependency to the installed
package config so consumers do not have to. In `cmake/LudusConfig.cmake.in`,
alongside the existing `find_dependency(volk CONFIG)`:

```cmake
include(CMakeFindDependencyMacro)
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_dependency(Threads)
```

**Sandbox workaround (in place):** `CMakeLists.txt` calls
`find_package(Threads REQUIRED)` before `find_package(Ludus CONFIG REQUIRED)`,
and `scripts/python/sandbox.py` installs the SDK via `scripts/build` +
`cmake --install` (skipping the engine's own failing consumer self-test; the SDK
install itself is complete and valid). Once the engine config adds
`find_dependency(Threads)`, the workaround can be removed.

## Honesty note on performance

No frame-timing numbers are reported because no GPU run happened here. The single
procedural fullscreen pass with two-octave noise and a handful of sine bands is
inexpensive and should hit 60 fps at 1080p on typical integrated GPUs, but that
must be **measured** on the target device before being claimed. If measurements
on a given device justify it, add simpler quality settings (e.g. drop the second
patch octave or the detuned wave bands) rather than assuming they are needed.
