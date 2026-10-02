# Build, run, and package

This document gives the exact, reproducible commands to build and run the Drift
ocean playground, plus the required engine revision and SDK variant.

## Required engine revision and SDK variant

| Item | Value | Where |
| --- | --- | --- |
| Ludus revision | `71e56a638044764344eedf9ac7bf9601195a2cfa` (PR #51, "Add public fullscreen rendering API and SDK shader tooling") | `config/ludus-version.txt` |
| SDK variant | `linux-clang-development` (native) / `web-emscripten-development` (browser) | `scripts/python/sandbox.py` (`SDK_VARIANT`) |
| Slang compiler | `2026.1.2` | engine `config/shader_toolchain.json` |
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
* **Browser rendering** needs a WebGPU-capable browser (e.g. recent Chrome).

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

1. Clones Ludus and checks out the pinned revision (or uses `--ludus-source
   <path>` for a local checkout).
2. Runs the engine's `init.sh` and `scripts/shader-probe bootstrap` to acquire
   the pinned, isolated Slang + SPIRV-Tools (digest-verified; no compiler or
   validator is linked into the game).
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

## 3. Browser build, run, and package (WebGPU)

```bash
cmake --build --preset web-emscripten-development

# The build directory IS the self-contained web package:
#   index.html  index.js  index.wasm  (+ generated shader artifacts embedded)
python3 -m http.server 8000 --bind 127.0.0.1 \
  --directory out/build/web-emscripten-development
# Open http://127.0.0.1:8000/ in a WebGPU-capable browser.
```

To produce a distributable archive:

```bash
( cd out/build/web-emscripten-development && zip -r ../../drift-ocean-web.zip index.html index.js index.wasm )
```

No shader compiler or network request is required during gameplay: the SPIR-V
and WGSL are generated at build time and embedded in the wasm.

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

## Shader rebuilds

The shader artifacts rebuild automatically when `shaders/ocean.slang` (or its
includes/defines/compiler/validator) change — this is handled by
`ludus_compile_shader` via the generated depfiles. Changing the shader and
rebuilding regenerates `ocean.vertex.spv`, `ocean.fragment.spv`, `ocean.wgsl`,
and the `ocean.h` factory header the app includes.
