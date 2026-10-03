#!/usr/bin/env python3

from __future__ import annotations

import argparse
import json
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path


REPOSITORY_URL = "https://github.com/Alegruz/Ludus.git"
DEFAULT_PRESET = "linux-clang-development"

# The SDK variant Ludus-Sandbox consumes. The public fullscreen rendering API
# and the ludus_compile_shader helper (Prompts 1 & 2 handoff) require the engine
# revision pinned in config/ludus-version.txt (PR #51, "Add public fullscreen
# rendering API and SDK shader tooling") installed with this preset. See
# docs/BUILD.md for the full, reproducible recipe.
SDK_VARIANT = DEFAULT_PRESET


def run(
    args: list[str],
    *,
    cwd: Path | None = None,
    env: dict[str, str] | None = None,
) -> None:
    print("+", " ".join(str(arg) for arg in args))
    child_env = dict(os.environ if env is None else env)
    child_env.pop("BUTLER_API_KEY", None)
    subprocess.run(args, cwd=cwd, env=child_env, check=True)


def require_command(name: str) -> None:
    if shutil.which(name) is None:
        raise RuntimeError(f"Required command not found: {name}")

def write_user_presets(
    repo_root: Path,
    ludus_source: Path,
    sdk_dir: Path,
    slang_compiler: Path,
    spirv_validator: Path,
    web_sdk_dir: Path | None,
) -> None:
    tool_venv = ludus_source / "out" / "host-tools" / "venv" / "bin"
    tool_bin = ludus_source / "out" / "host-tools" / "bin"

    cmake = tool_venv / "cmake"
    ninja = tool_venv / "ninja"
    clangxx = tool_bin / "clang++"

    for tool in (cmake, ninja, clangxx, slang_compiler, spirv_validator):
        if not tool.is_file() or not os.access(tool, os.X_OK):
            raise RuntimeError(f"Required executable is missing: {tool}")
    validate_sdk(sdk_dir)
    if web_sdk_dir is not None:
        validate_sdk(web_sdk_dir)

    # The native SDK's LudusConfig.cmake does find_dependency(volk CONFIG), whose
    # package config files live in the engine's Conan output dir. The consumer
    # must therefore see BOTH the SDK install prefix and that Conan dir on
    # CMAKE_PREFIX_PATH (matching the engine's own SDK consumer invocation).
    conan_dir = ludus_source / "out" / "conan" / DEFAULT_PRESET
    native_prefix_path = str(sdk_dir)
    if conan_dir.is_dir():
        native_prefix_path = f"{sdk_dir};{conan_dir}"

    configure_presets = [
        {
            "name": "linux-clang-development",
            "displayName": "Linux Clang Development",
            "cmakeExecutable": str(cmake),
            "inherits": "linux-clang-development-base",
            "cacheVariables": {
                "CMAKE_MAKE_PROGRAM": str(ninja),
                "CMAKE_CXX_COMPILER": str(clangxx),
                "CMAKE_PREFIX_PATH": native_prefix_path,
                "LUDUS_SLANG_COMPILER": str(slang_compiler),
                "LUDUS_SPIRV_VALIDATOR": str(spirv_validator),
            },
        }
    ]
    build_presets = [
        {
            "name": "linux-clang-development",
            "configurePreset": "linux-clang-development",
        }
    ]

    # Add browser (Emscripten/WebGPU + WebGL 2) presets when the web SDK and the engine's
    # emscripten toolchain are present. Paths are discovered, never hardcoded.
    emscripten_toolchain = (
        ludus_source
        / "out"
        / "host-tools"
        / "emsdk"
        / "upstream"
        / "emscripten"
        / "cmake"
        / "Modules"
        / "Platform"
        / "Emscripten.cmake"
    )
    if web_sdk_dir is not None and not emscripten_toolchain.is_file():
        raise RuntimeError(f"Web SDK selected but toolchain is missing: {emscripten_toolchain}")
    if web_sdk_dir is not None:
        spirv_cross = ludus_source / "out/shader-tools/spirv-cross/bin/spirv-cross"
        if not spirv_cross.is_file() or not os.access(spirv_cross, os.X_OK):
            raise RuntimeError(f"Required SPIRV-Cross translator is missing: {spirv_cross}")
        configure_presets.append(
            {
                "name": "web-emscripten-development",
                "displayName": "Web Emscripten Development",
                "cmakeExecutable": str(cmake),
                "inherits": "web-emscripten-development-base",
                "toolchainFile": str(emscripten_toolchain),
                "cacheVariables": {
                    "CMAKE_MAKE_PROGRAM": str(ninja),
                    "CMAKE_PREFIX_PATH": str(web_sdk_dir),
                    "CMAKE_FIND_ROOT_PATH": str(web_sdk_dir),
                    "LUDUS_SLANG_COMPILER": str(slang_compiler),
                    "LUDUS_SPIRV_VALIDATOR": str(spirv_validator),
                    "LUDUS_SPIRV_CROSS": str(ludus_source / "out/shader-tools/spirv-cross/bin/spirv-cross"),
                },
            }
        )
        configure_presets.append({
            "name": "web-emscripten-release",
            "inherits": ["web-emscripten-development", "web-emscripten-release-base"],
            "binaryDir": "${sourceDir}/out/build/web-emscripten-release",
            "cacheVariables": {"CMAKE_BUILD_TYPE": "Release"},
        })
        build_presets.append({"name": "web-emscripten-release", "configurePreset": "web-emscripten-release"})
        build_presets.append(
            {
                "name": "web-emscripten-development",
                "configurePreset": "web-emscripten-development",
            }
        )

    read_json_object(repo_root / ".vscode/settings.json")
    test_presets = [{
        "name": DEFAULT_PRESET,
        "configurePreset": DEFAULT_PRESET,
        "output": {"outputOnFailure": True},
    }]
    path = repo_root / "CMakeUserPresets.json"
    presets = read_json_object(path)
    presets.setdefault("version", 6)
    managed_names = {DEFAULT_PRESET, "web-emscripten-development", "web-emscripten-release"}
    for key, generated in (("configurePresets", configure_presets),
                           ("buildPresets", build_presets),
                           ("testPresets", test_presets)):
        existing = presets.get(key, [])
        if not isinstance(existing, list) or any(not isinstance(item, dict) for item in existing):
            raise RuntimeError(f"Invalid {key} in {path}; repair it before running setup")
        presets[key] = [item for item in existing if item.get("name") not in managed_names] + generated
    write_json_object(path, presets)
    write_editor_settings(repo_root)
    check_setup(repo_root, ludus_source)
    print(f"Generated and checked user presets: {path}")


def read_json_object(path: Path) -> dict:
    if not path.exists():
        return {}
    try:
        result = json.loads(path.read_text(encoding="utf-8"))
    except (ValueError, OSError) as error:
        raise RuntimeError(f"Cannot read {path}; fix its JSON before setup: {error}") from error
    if not isinstance(result, dict):
        raise RuntimeError(f"Expected a JSON object in {path}")
    return result


def write_json_object(path: Path, value: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(path.name + ".tmp")
    temporary.write_text(json.dumps(value, indent=2) + "\n", encoding="utf-8")
    temporary.replace(path)


def validate_sdk(prefix: Path) -> None:
    package = prefix / "lib" / "cmake" / "Ludus"
    for name in ("LudusConfig.cmake", "LudusTargets.cmake", "LudusShaders.cmake"):
        if not (package / name).is_file():
            raise RuntimeError(f"SDK is incomplete or outdated: missing {package / name}; install the pinned SDK")


def write_editor_settings(repo_root: Path) -> None:
    path = repo_root / ".vscode" / "settings.json"
    settings = read_json_object(path)
    if settings.get("cmake.useCMakePresets") == "always":
        return
    settings["cmake.useCMakePresets"] = "always"
    write_json_object(path, settings)


def check_setup(repo_root: Path, ludus_source: Path) -> None:
    """Read-only preflight; CMake owns include/inheritance/condition validation."""
    cmake = ludus_source / "out" / "host-tools" / "venv" / "bin" / "cmake"
    if not cmake.is_file() or not os.access(cmake, os.X_OK):
        raise RuntimeError(f"Missing managed CMake: {cmake}; run ./init.sh first")
    presets_path = repo_root / "CMakeUserPresets.json"
    if not presets_path.is_file():
        raise RuntimeError("CMakeUserPresets.json is missing; run ./init.sh or sandbox.py repair --ludus-source <checkout>")
    presets = read_json_object(presets_path)
    for key in ("configurePresets", "buildPresets", "testPresets"):
        items = presets.get(key, [])
        if not isinstance(items, list) or any(not isinstance(item, dict) for item in items):
            raise RuntimeError(f"Invalid {key} in {presets_path}; fix its JSON before repair")
    native = next((item for item in presets.get("configurePresets", [])
                   if item.get("name") == DEFAULT_PRESET), None)
    if native is None:
        raise RuntimeError("Native configure preset is missing; run sandbox.py repair --ludus-source <checkout>")
    # Check every generated preset's machine paths, including an enabled web SDK.
    for preset in presets.get("configurePresets", []):
        if preset.get("name") not in (DEFAULT_PRESET, "web-emscripten-development"):
            continue
        if preset.get("cmakeExecutable") != str(cmake):
            raise RuntimeError("IDE CMake executable is stale; run repair")
        cache = preset.get("cacheVariables", {})
        if not isinstance(cache, dict) or any(not isinstance(cache.get(key, ""), str) for key in (
                "CMAKE_MAKE_PROGRAM", "LUDUS_SLANG_COMPILER", "LUDUS_SPIRV_VALIDATOR",
                "CMAKE_PREFIX_PATH", "CMAKE_CXX_COMPILER")):
            raise RuntimeError("Generated preset has invalid tool/path values; run repair")
        for key in ("CMAKE_MAKE_PROGRAM", "LUDUS_SLANG_COMPILER", "LUDUS_SPIRV_VALIDATOR"):
            tool = Path(cache.get(key, ""))
            if not tool.is_file() or not os.access(tool, os.X_OK):
                raise RuntimeError(f"{preset['name']}: missing executable {key}: {tool}; run repair")
        prefixes = cache.get("CMAKE_PREFIX_PATH", "").split(";")
        validate_sdk(Path(prefixes[0]))
        for prefix in prefixes:
            if not prefix or not Path(prefix).is_dir():
                raise RuntimeError(f"Missing SDK/dependency prefix: {prefix}; run repair")
        if preset["name"] == DEFAULT_PRESET:
            compiler = Path(cache.get("CMAKE_CXX_COMPILER", ""))
            if not compiler.is_file() or not os.access(compiler, os.X_OK):
                raise RuntimeError(f"Missing native compiler: {compiler}; run repair")
        else:
            if not Path(preset.get("toolchainFile", "")).is_file():
                raise RuntimeError("Web toolchain is missing; run repair with an installed web toolchain")
            translator = Path(cache.get("LUDUS_SPIRV_CROSS", ""))
            if not translator.is_file() or not os.access(translator, os.X_OK):
                raise RuntimeError(f"Web SPIRV-Cross translator is missing: {translator}; run repair")
    settings = read_json_object(repo_root / ".vscode" / "settings.json")
    if settings.get("cmake.useCMakePresets") != "always":
        raise RuntimeError("VS Code CMake settings are stale; run sandbox.py repair --ludus-source <checkout>")
    expected = {DEFAULT_PRESET}
    if any(item.get("name") == "web-emscripten-development" for item in presets.get("configurePresets", [])):
        expected.update(("web-emscripten-development", "web-emscripten-release"))
    for kind in ("configure", "build", "test"):
        result = subprocess.run([str(cmake), f"--list-presets={kind}"], cwd=repo_root,
                                text=True, capture_output=True, timeout=30)
        if result.returncode:
            raise RuntimeError(f"Invalid CMake presets: {result.stderr.strip()}; run repair")
        visible = set(re.findall(r'^  "([^"\n]+)"', result.stdout, re.MULTILINE))
        required = {DEFAULT_PRESET} if kind == "test" else expected
        if not required <= visible:
            raise RuntimeError(f"Missing selectable {kind} presets: {', '.join(sorted(required - visible))}; run repair")
    print("Setup checks passed: selectable presets, SDKs, tools and VS Code CMake settings.")


def local_setup_command(args: argparse.Namespace) -> None:
    """Check or repair using existing artifacts, without fetching/building Ludus."""
    repo_root = Path(__file__).resolve().parents[2]
    ludus_source = Path(args.ludus_source).expanduser().resolve()
    if args.command == "doctor":
        check_setup(repo_root, ludus_source)
        return
    sdk_dir = Path(args.sdk_dir).expanduser().resolve() if args.sdk_dir else ludus_source / "out/install" / DEFAULT_PRESET
    web_sdk_dir = Path(args.web_sdk_dir).expanduser().resolve() if args.web_sdk_dir else None
    slang = ludus_source / "out/shader-tools/slang/bin/slangc"
    validators = sorted((ludus_source / "out/shader-tools/spirv-tools").glob("**/spirv-val"))
    if not validators:
        raise RuntimeError("Pinned spirv-val is missing; run ./init.sh first")
    # Validate settings before modifying presets; never discard invalid custom JSON.
    read_json_object(repo_root / ".vscode/settings.json")
    write_user_presets(repo_root, ludus_source, sdk_dir, slang, validators[0], web_sdk_dir)
    configure_sandbox(repo_root, ludus_source, DEFAULT_PRESET, slang, validators[0])
    if web_sdk_dir is not None:
        configure_sandbox_web(repo_root, ludus_source, slang, validators[0])
    check_setup(repo_root, ludus_source)


def read_ludus_revision(repo_root: Path) -> str:
    version_file = repo_root / "config" / "ludus-version.txt"

    if not version_file.is_file():
        raise RuntimeError(f"Missing Ludus version file: {version_file}")

    revision = version_file.read_text(encoding="utf-8").strip()

    if not revision:
        raise RuntimeError(f"Ludus version file is empty: {version_file}")

    return revision


def acquire_ludus(
    repo_root: Path,
    source_override: str | None,
) -> Path:
    if source_override:
        source_dir = Path(source_override).expanduser().resolve()

        if not source_dir.is_dir():
            raise RuntimeError(f"Ludus source directory does not exist: {source_dir}")

        if not (source_dir / "init.sh").is_file():
            raise RuntimeError(
                f"Directory does not look like a Ludus checkout: {source_dir}"
            )

        print(f"Using local Ludus checkout: {source_dir}")
        return source_dir

    require_command("git")

    revision = read_ludus_revision(repo_root)

    source_dir = repo_root / "out" / "ludus" / "source"
    source_dir.parent.mkdir(parents=True, exist_ok=True)

    if not (source_dir / ".git").is_dir():
        print(f"Cloning Ludus into {source_dir}")
        run(
            [
                "git",
                "clone",
                REPOSITORY_URL,
                str(source_dir),
            ]
        )
    else:
        print(f"Using existing bootstrap-managed Ludus clone: {source_dir}")

    run(["git", "fetch", "--tags", "--prune", "origin"], cwd=source_dir)

    # Resolve branch names, tags, or exact SHAs.
    run(["git", "checkout", "--detach", revision], cwd=source_dir)

    print(f"Ludus revision: {revision}")
    return source_dir


def initialize_ludus(ludus_source: Path) -> None:
    init_script = ludus_source / "init.sh"

    if not init_script.is_file():
        raise RuntimeError(f"Missing Ludus init script: {init_script}")

    # CI disables interactive setup on current engines and remains compatible
    # with older engine revisions that predate the --cli option.
    run([str(init_script), DEFAULT_PRESET, "--preset-only"], cwd=ludus_source,
        env={**os.environ, "CI": "true"})


def install_ludus_sdk(
    ludus_source: Path,
    preset: str,
) -> Path:
    """Build and install the native Ludus SDK to a prefix we consume.

    We drive ``scripts/build`` + ``cmake --install`` directly instead of the
    engine's ``install-sdk`` wrapper. ``install-sdk`` additionally runs an
    engine-owned SDK-consumer self-test that currently fails because the
    installed ``LudusConfig.cmake`` does not ``find_dependency(Threads)`` even
    though ``Ludus::FoundationProfiling`` links ``Threads::Threads`` PUBLIC (see
    docs/VALIDATION.md "Known engine SDK packaging gap"). The SDK *install*
    itself is complete and valid; only that self-test fails. Our consumer works
    around the gap with its own ``find_package(Threads)`` call."""
    build_script = ludus_source / "scripts" / "build"
    if not build_script.is_file():
        raise RuntimeError(f"Missing Ludus build script: {build_script}")

    cmake = ludus_source / "out" / "host-tools" / "venv" / "bin" / "cmake"
    if not cmake.is_file():
        raise RuntimeError(f"Expected Ludus-managed CMake does not exist: {cmake}")

    run([str(build_script), preset], cwd=ludus_source)

    build_dir = ludus_source / "out" / "build" / preset
    sdk_dir = ludus_source / "out" / "install" / preset
    run(
        [str(cmake), "--install", str(build_dir), "--prefix", str(sdk_dir)],
        cwd=ludus_source,
    )

    if not sdk_dir.is_dir():
        raise RuntimeError(
            "Ludus SDK install completed, but expected install directory "
            f"does not exist: {sdk_dir}"
        )

    return sdk_dir


def install_ludus_web_sdk(ludus_source: Path) -> Path:
    """Acquire the Emscripten/WebGPU toolchain, build the engine's web targets,
    and install them to a prefix the sandbox can consume. The engine does not
    expose a relocatable web SDK via ``install-sdk``; the supported path is
    ``web init`` (acquire emsdk) -> ``web build`` -> ``cmake --install`` of the
    web build tree (see fullscreen-rendering-handoff.md). Returns the prefix."""
    web_preset = "web-emscripten-release"

    init_script = ludus_source / "scripts" / "init"
    build_script = ludus_source / "scripts" / "build"
    if not init_script.is_file() or not build_script.is_file():
        raise RuntimeError(f"Missing Ludus init/build scripts under {ludus_source / 'scripts'}")

    # The engine routes web-emscripten-* presets to its browser pipeline. Acquire
    # the pinned emsdk and configure (init --preset-only), then build the web
    # targets in-tree. The preset is passed positionally.
    run([str(init_script), web_preset, "--preset-only"], cwd=ludus_source,
        env={**os.environ, "CI": "true"})
    run([str(build_script), web_preset], cwd=ludus_source)

    # Install the built web tree to a prefix we can point CMAKE_PREFIX_PATH at.
    cmake = ludus_source / "out" / "host-tools" / "venv" / "bin" / "cmake"
    if not cmake.is_file():
        raise RuntimeError(f"Expected Ludus-managed CMake does not exist: {cmake}")

    build_dir = ludus_source / "out" / "build" / web_preset
    web_sdk_dir = ludus_source / "out" / "install" / web_preset
    run(
        [str(cmake), "--install", str(build_dir), "--prefix", str(web_sdk_dir)],
        cwd=ludus_source,
    )

    if not web_sdk_dir.is_dir():
        raise RuntimeError(
            "Ludus web SDK install completed, but expected install directory "
            f"does not exist: {web_sdk_dir}"
        )

    return web_sdk_dir


def acquire_shader_tools(ludus_source: Path) -> tuple[Path, Path]:
    """Acquire the pinned, isolated Slang + SPIRV-Tools used by the shader
    helper, and return (slang_compiler, spirv_validator). These are host tools
    kept separate from the target compiler; no compiler/validator is linked into
    the game, and no network access is needed once acquired."""
    probe = ludus_source / "scripts" / "shader-probe"

    if not probe.is_file():
        raise RuntimeError(f"Missing Ludus shader-probe script: {probe}")

    # Idempotent: re-running verifies the pinned digests without re-downloading.
    run([str(probe), "bootstrap"], cwd=ludus_source)
    run([str(ludus_source / "scripts/bootstrap-spirv-cross")], cwd=ludus_source)

    tools = ludus_source / "out" / "shader-tools"
    slang = tools / "slang" / "bin" / "slangc"
    validators = list(tools.glob("spirv-tools/**/spirv-val"))

    if not slang.is_file():
        raise RuntimeError(f"Expected pinned Slang compiler not found: {slang}")
    if not validators:
        raise RuntimeError(
            "Expected pinned spirv-val not found under "
            f"{tools / 'spirv-tools'}"
        )

    if not (tools / "spirv-cross/bin/spirv-cross").is_file():
        raise RuntimeError("Missing pinned SPIRV-Cross translator")
    return slang, validators[0]


def configure_sandbox(
    repo_root: Path,
    ludus_source: Path,
    preset: str,
    slang_compiler: Path,
    spirv_validator: Path,
) -> None:
    cmake = (
        ludus_source
        / "out"
        / "host-tools"
        / "venv"
        / "bin"
        / "cmake"
    )

    if not cmake.is_file():
        raise RuntimeError(
            f"Expected Ludus-managed CMake does not exist: {cmake}"
        )

    run(
        [
            str(cmake),
            "--fresh",
            "--preset",
            preset,
            # The ludus_compile_shader helper needs the pinned host tools. They
            # are passed explicitly (not hardcoded) so a different developer's
            # paths or a local SDK work without editing CMake.
            f"-DLUDUS_SLANG_COMPILER={slang_compiler}",
            f"-DLUDUS_SPIRV_VALIDATOR={spirv_validator}",
        ],
        cwd=repo_root,
    )


def configure_sandbox_web(
    repo_root: Path,
    ludus_source: Path,
    slang_compiler: Path,
    spirv_validator: Path,
) -> None:
    """Configure the browser (Emscripten/WebGPU) preset so that
    ``cmake --build --preset web-emscripten-development`` has a build tree.

    Emscripten cross-compilation must run under the emsdk's ``emcmake`` wrapper
    so emcc/the toolchain are discovered; the generated user preset supplies the
    toolchain file, prefix and shader tools."""
    cmake = ludus_source / "out" / "host-tools" / "venv" / "bin" / "cmake"
    emcmake = (
        ludus_source
        / "out"
        / "host-tools"
        / "emsdk"
        / "upstream"
        / "emscripten"
        / "emcmake"
    )
    if not cmake.is_file():
        raise RuntimeError(f"Expected Ludus-managed CMake does not exist: {cmake}")
    if not emcmake.is_file():
        raise RuntimeError(f"Expected emsdk emcmake does not exist: {emcmake}")

    for web_preset in ("web-emscripten-development", "web-emscripten-release"):
        run(
            [
                str(emcmake),
                str(cmake),
                "--fresh",
                "--preset",
                web_preset,
                f"-DLUDUS_SLANG_COMPILER={slang_compiler}",
                f"-DLUDUS_SPIRV_VALIDATOR={spirv_validator}",
            ],
            cwd=repo_root,
        )



def init_command(args: argparse.Namespace) -> None:
    if args.web_release:
        args.with_web = True
    repo_root = Path(__file__).resolve().parents[2]

    print("Initializing Ludus Sandbox")
    print(f"Repository: {repo_root}")
    print(f"SDK variant: {args.preset}")

    ludus_source = acquire_ludus(
        repo_root=repo_root,
        source_override=args.ludus_source,
    )

    initialize_ludus(ludus_source)

    # Acquire the pinned, isolated shader tools (Slang + SPIRV-Tools). Required
    # by ludus_compile_shader; separate from the target compiler.
    slang_compiler, spirv_validator = acquire_shader_tools(ludus_source)

    # A developer iterating against an already-installed SDK can point at it
    # directly (--sdk-dir / LUDUS_SANDBOX_SDK_DIR) to skip the engine rebuild.
    if args.sdk_dir:
        sdk_dir = Path(args.sdk_dir).expanduser().resolve()
        if not sdk_dir.is_dir():
            raise RuntimeError(f"Provided --sdk-dir does not exist: {sdk_dir}")
        print(f"Using pre-installed Ludus SDK: {sdk_dir}")
    else:
        sdk_dir = install_ludus_sdk(
            ludus_source=ludus_source,
            preset=args.preset,
        )

    # Optional separate web SDK prefix (install with the web preset). An explicit
    # --web-sdk-dir wins; otherwise --with-web installs it via the engine scripts;
    # otherwise it is discovered by convention and left unset on native-only setups.
    web_sdk_dir = None
    if args.web_sdk_dir:
        candidate = Path(args.web_sdk_dir).expanduser().resolve()
        if not candidate.is_dir():
            raise RuntimeError(f"Provided --web-sdk-dir does not exist: {candidate}")
        web_sdk_dir = candidate
    elif args.with_web and not args.sdk_dir:
        web_sdk_dir = install_ludus_web_sdk(ludus_source)
    else:
        candidate = ludus_source / "out" / "install" / "web-emscripten-release"
        if candidate.is_dir():
            web_sdk_dir = candidate

    read_json_object(repo_root / ".vscode/settings.json")
    write_user_presets(
        repo_root=repo_root,
        ludus_source=ludus_source,
        sdk_dir=sdk_dir,
        slang_compiler=slang_compiler,
        spirv_validator=spirv_validator,
        web_sdk_dir=web_sdk_dir,
    )

    configure_sandbox(
        repo_root=repo_root,
        ludus_source=ludus_source,
        preset=args.preset,
        slang_compiler=slang_compiler,
        spirv_validator=spirv_validator,
    )

    # Configure the browser preset too, when the web SDK and the emsdk toolchain
    # are available, so the web build tree exists for `cmake --build --preset
    # web-emscripten-development`. Mirrors the condition in write_user_presets.
    emscripten_emcmake = (
        ludus_source / "out" / "host-tools" / "emsdk" / "upstream" / "emscripten" / "emcmake"
    )
    web_configured = False
    if web_sdk_dir is not None and emscripten_emcmake.is_file():
        configure_sandbox_web(
            repo_root=repo_root,
            ludus_source=ludus_source,
            slang_compiler=slang_compiler,
            spirv_validator=spirv_validator,
        )
        web_configured = True

    print()
    print("Ludus Sandbox initialization complete.")
    print()
    print(f"Ludus source:     {ludus_source}")
    print(f"Ludus SDK:        {sdk_dir}")
    if web_sdk_dir is not None:
        print(f"Ludus web SDK:    {web_sdk_dir}")
    print(f"Slang compiler:   {slang_compiler}")
    print(f"SPIR-V validator: {spirv_validator}")
    print(f"SPIRV-Cross:      {ludus_source / 'out/shader-tools/spirv-cross/bin/spirv-cross'}")
    print()
    print("The sandbox has been configured but not built.")
    print()
    print("Build with:")
    print(f"  cmake --build --preset {args.preset}")
    if web_configured:
        print("  cmake --build --preset web-emscripten-development")


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(prog="sandbox.py")
    subparsers = parser.add_subparsers(dest="command", required=True)

    init_parser = subparsers.add_parser(
        "init",
        help="Acquire/install Ludus and configure Ludus Sandbox.",
    )

    init_parser.add_argument(
        "--ludus-source",
        help="Use an existing Ludus source checkout instead of cloning one.",
    )

    init_parser.add_argument(
        "--preset",
        default=DEFAULT_PRESET,
        choices=[DEFAULT_PRESET],
        help=f"CMake/Ludus preset / SDK variant to use (default: {DEFAULT_PRESET}).",
    )

    init_parser.add_argument(
        "--sdk-dir",
        default=os.environ.get("LUDUS_SANDBOX_SDK_DIR"),
        help=(
            "Path to an already-installed native Ludus SDK (CMAKE_PREFIX_PATH). "
            "Lets a developer iterate without rebuilding the engine. Defaults to "
            "the LUDUS_SANDBOX_SDK_DIR environment variable if set. No path is "
            "hardcoded."
        ),
    )

    init_parser.add_argument(
        "--web-sdk-dir",
        default=os.environ.get("LUDUS_SANDBOX_WEB_SDK_DIR"),
        help=(
            "Path to an already-installed Emscripten/WebGPU Ludus SDK. Enables "
            "the browser preset. Defaults to LUDUS_SANDBOX_WEB_SDK_DIR."
        ),
    )

    init_parser.add_argument(
        "--with-web",
        action="store_true",
        help=(
            "Also bootstrap the Emscripten toolchain and install the web SDK "
            "variant, enabling the browser (WebGPU) preset. Ignored when "
            "--sdk-dir or --web-sdk-dir is given."
        ),
    )

    init_parser.add_argument("--web-release", action="store_true", help="Prepare browser Release inputs (implies --with-web).")
    init_parser.set_defaults(func=init_command)
    for command in ("doctor", "repair"):
        local_parser = subparsers.add_parser(command, help=f"{command.title()} local setup without downloads or engine builds.")
        local_parser.add_argument("--ludus-source", required=True, help="Existing prepared Ludus checkout.")
        local_parser.add_argument("--sdk-dir", default=os.environ.get("LUDUS_SANDBOX_SDK_DIR"))
        local_parser.add_argument("--web-sdk-dir", default=os.environ.get("LUDUS_SANDBOX_WEB_SDK_DIR"))
        local_parser.set_defaults(func=local_setup_command)

    return parser


def main() -> int:
    try:
        parser = build_parser()
        args = parser.parse_args()
        args.func(args)
        return 0
    except subprocess.CalledProcessError as error:
        print(
            f"Command failed with exit code {error.returncode}.",
            file=sys.stderr,
        )
        return error.returncode
    except (RuntimeError, OSError, ValueError, subprocess.TimeoutExpired) as error:
        print(f"error: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())