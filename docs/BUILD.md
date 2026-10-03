# Build, run, and package

This document gives the exact, reproducible commands to build and run the Drift
ocean playground, plus the required engine revision and SDK variant.

## Required engine revision and SDK variant

| Item | Value | Where |
| --- | --- | --- |
| Ludus revision | `9554d051b2125580327d9f89c06395798a53028d` ([engine PR #64](https://github.com/Alegruz/Ludus/pull/64), checked dynamics kernels; includes WebGL 2 fallback) | `config/ludus-version.txt` |
| SDK variant | `linux-clang-development` (native) / `web-emscripten-release` (browser SDK; development and release app presets) | `scripts/python/sandbox.py` (`SDK_VARIANT`) |
| Slang compiler | `2026.1.2` | engine `config/shader_toolchain.json` |
| SPIRV-Cross | `vulkan-sdk-1.4.313.0` (source archive + built binary digest verified) | engine `config/spirv_cross_toolchain.json` |
| SPIR-V validator | `spirv-val` `2025.1~rc1` (digest-pinned) | engine `config/shader_toolchain.json` |

The public fullscreen rendering API (`ludus/graphics/rhi/rhi.h` +
`render.h`) and the `ludus_compile_shader` CMake helper only exist from the
revision above. The pin is a commit SHA, so the build is reproducible: `init.sh`
checks out exactly that revision.

## Host requirements

* **Reference host:** Ubuntu 24.04 x86_64, glibc **2.38+** (the pinned
  `spirv-val` requires glibc 2.38+), Clang/LLD 18, Python 3.10+.
* **Native rendering** additionally needs a Wayland display and a Vulkan-capable
  GPU.
* **Browser rendering** needs WebGPU or WebGL 2. Auto prefers WebGPU; use HTTPS or localhost
  for WebGPU. Physical-device and hosted compatibility still need acceptance testing.

> The engine's one-command bootstrap (`init.sh` → `scripts/python/sandbox.py`)
> currently automates system prerequisites on apt-based Ubuntu/Debian only, and
> the pinned validator requires glibc 2.38+. On other hosts, acquire the pinned
> tools separately and pass `--sdk-dir` / the `LUDUS_SLANG_COMPILER` /
> `LUDUS_SPIRV_VALIDATOR` CMake variables yourself (see "Manual tool wiring").

## 1. Initialize

```bash
./init.sh
```

`init.sh` runs `scripts/python/sandbox.py init`, which:

1. Clones Ludus, explicitly fetches the pinned revision, and checks out the
   fetched commit (or uses `--ludus-source <path>` for a local checkout). This
   also resolves PR commits after a squash merge and branch deletion.
2. Runs the engine's `init.sh` and `scripts/shader-probe bootstrap` to acquire
   the pinned, isolated Slang + SPIRV-Tools, then `scripts/bootstrap-spirv-cross`
   acquires/builds the pinned translator. These are explicit network bootstrap
   steps; no compiler, validator or translator is linked into the game.
3. Installs the Ludus SDK (or uses `--sdk-dir <path>` /
   `LUDUS_SANDBOX_SDK_DIR` to reuse an already-installed SDK — no absolute path
   is hardcoded).
4. Writes `CMakeUserPresets.json` wiring the SDK prefix and the pinned shader
   tools, and configures the sandbox.

Useful options:

```bash
./init.sh --ludus-source /path/to/local/Ludus      # iterate on a local engine
./init.sh --sdk-dir /path/to/installed/ludus-sdk    # skip the engine rebuild
./init.sh --web-sdk-dir /path/to/web-sdk            # enable the browser preset
```

## 2. Native build and run (Vulkan)

```bash
cmake --build --preset linux-clang-development

# Run with the default (Drifting) preset:
./out/build/linux-clang-development/LudusSandbox

# ...a named preset:
./out/build/linux-clang-development/LudusSandbox calm      # or drifting / choppy

# ...or a settings file (versioned JSON):
LUDUS_OCEAN_SETTINGS=my-ocean.json \
  ./out/build/linux-clang-development/LudusSandbox

# Offscreen/CI smoke run: cap frames and exit.
LUDUS_OCEAN_FRAMES=120 \
  ./out/build/linux-clang-development/LudusSandbox drifting
```

A native GUI framework is out of scope for this first slice, so the native build
exercises the same settings through config/CLI (preset name, JSON file, and the
`LUDUS_OCEAN_SETTINGS` / `LUDUS_OCEAN_FRAMES` environment variables).

## 3. Browser build, run, and package (WebGPU + WebGL 2)

```bash
./init.sh --with-web
cmake --build --preset web-emscripten-development

# The build directory IS the self-contained web package:
#   index.html  index.js  index.wasm  (+ generated shader artifacts embedded)
python3 -m http.server 8000 --bind 127.0.0.1 \
  --directory out/build/web-emscripten-development
# Open http://127.0.0.1:8000/ (Auto), or append ?backend=webgl2 / ?backend=webgpu.
```

To produce a distributable archive:

```bash
cmake --build --preset web-emscripten-release
./scripts/package-web
# out/packages/drift-ocean-web-release.zip
```

No shader compiler or network request is required during gameplay: the SPIR-V
WGSL and GLSL ES are generated at build time and embedded in the wasm. The
Release packager checks every payload, hashes files, includes redistribution
notices and records the exact engine/toolchain identity in `build-info.json`.
It refuses debug wasm, developer paths, missing assets or layout mismatches.
See [the browser harness](../tools/browser-tests/README.md) to test that exact ZIP.

## 4. GPU-free logic tests

These need no SDK or GPU and run anywhere with a C++23 compiler:

```bash
ctest --test-dir out/build/linux-clang-development --output-on-failure
```

Or standalone, without configuring the engine SDK at all:

```bash
clang++ -std=c++23 -Wall -Wextra -Wpedantic -fno-exceptions -I src \
  tests/ocean_tests.cpp \
  src/ocean/ocean_settings.cpp src/ocean/ocean_clock.cpp src/ocean/ocean_uniforms.cpp \
  -o ocean_tests && ./ocean_tests
```

## 5. Offline screenshots (no GPU)

`tools/preview/ocean_preview.cpp` is a faithful CPU port of `shaders/ocean.slang`
used only to capture the authored look offline (it is **not** part of the game):

```bash
clang++ -std=c++23 -O2 -fno-exceptions -I src \
  tools/preview/ocean_preview.cpp \
  src/ocean/ocean_settings.cpp src/ocean/ocean_clock.cpp src/ocean/ocean_uniforms.cpp \
  -o ocean_preview && ./ocean_preview out_dir   # writes PPMs; convert to PNG
```

## Manual tool wiring

If you are not using the Ubuntu bootstrap, configure directly and pass the
pinned tools (never hardcode them in CMake):

```bash
cmake -S . -B out/build/linux-clang-development -G Ninja \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCMAKE_PREFIX_PATH="/path/to/ludus-sdk" \
  -DLUDUS_SLANG_COMPILER="/path/to/slangc" \
  -DLUDUS_SPIRV_VALIDATOR="/path/to/spirv-val"
cmake --build out/build/linux-clang-development
```

## Continuous integration

`.github/workflows/ci.yml` mirrors these commands on `ubuntu-24.04` runners:

* **fast** — `clang-format-18`/`clang-tidy-18` + the GPU-free logic tests (no SDK).
* **native** — `./init.sh` then `cmake --build --preset linux-clang-development`
  and `ctest`; asserts the generated shader artifacts exist.
* **web** — `./init.sh --with-web` then
  `cmake --build --preset web-emscripten-release`, `scripts/package-web` and
  pinned Playwright checks against the extracted ZIP; uploads the tested ZIP
  and browser evidence.

The `native`/`web` jobs install the engine's system prerequisites
(`clang-18`, `lld-18`, `libwayland-dev`, `wayland-protocols`) before `./init.sh`
so the engine bootstrap finds them satisfied. See
[docs/VALIDATION.md](VALIDATION.md#continuous-integration).

## Shader rebuilds

The shader artifacts rebuild automatically when `shaders/ocean.slang` (or its
includes/defines/compiler/validator) change — this is handled by
`ludus_compile_shader` via the generated depfiles. Changing the shader and
rebuilding regenerates `ocean.vertex.spv`, `ocean.fragment.spv`, `ocean.wgsl`,
`ocean.vertex.glsl`, `ocean.fragment.glsl`, and the `ocean.h` factory header the app includes.

For manual browser configuration, also set `LUDUS_SPIRV_CROSS` to the pinned
verified binary produced by `scripts/bootstrap-spirv-cross`. Normal configure,
build and gameplay remain offline after explicit initialization.

## Editor releases and automatic itch.io uploads

Open `ludus.project.json` in the Editor and use **Release > Package Release**.
See [RELEASING.md](../RELEASING.md) for preparation, package verification, and
GitHub environment/variable setup. The release workflow tests the exact archive
in Chromium before the separate upload job receives credentials.

## Missing presets and local setup repair

A fresh clone contains hidden base presets only. Run `./init.sh` to prepare the
SDK/tools and generate ignored `CMakeUserPresets.json`. Setup runs the engine
initializer noninteractively for the native Development preset. The generated
user presets select the managed CMake for IDEs through `cmakeExecutable`, keeping
machine paths out of tracked VS Code settings, and include a native test preset.

Check setup after opening a clone, moving tools/SDKs, or updating the project:

```bash
python3 scripts/python/sandbox.py doctor --ludus-source /path/to/Ludus
```

Doctor is read-only and does not download, build, or configure. To repair using
existing prepared tools and an installed SDK, without rebuilding the engine:

```bash
python3 scripts/python/sandbox.py repair --ludus-source /path/to/Ludus \
  --sdk-dir /path/to/installed/development-sdk
# Add --web-sdk-dir /path/to/installed/web-sdk to repair browser setup too.
cmake --list-presets=all
cmake --build --preset linux-clang-development
ctest --preset linux-clang-development
```

Repair preserves custom presets and editor preferences, regenerates owned local
presets, checks executable/SDK paths and actual CMake preset visibility, and
configures with a fresh cache to remove old toolchain/SDK paths. Malformed custom
JSON is reported for manual correction. Successful configure does not imply a
completed build/test; run those last two commands after repair. Browser runtime
validation still requires WebGPU and a browser.

Setup regression tests use real CMake discovery with temporary SDK/tool fixtures:

```bash
python3 -m unittest discover -s scripts/python -p 'test_*.py'
```
