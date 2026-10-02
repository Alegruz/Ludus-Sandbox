#!/usr/bin/env python3

from __future__ import annotations

import argparse
import json
import os
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
    subprocess.run(args, cwd=cwd, env=env, check=True)


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

    for tool in (cmake, ninja, clangxx):
        if not tool.is_file():
            raise RuntimeError(
                f"Expected Ludus-managed tool does not exist: {tool}"
            )

    configure_presets = [
        {
            "name": "linux-clang-development",
            "displayName": "Linux Clang Development",
            "inherits": "linux-clang-development-base",
            "cacheVariables": {
                "CMAKE_MAKE_PROGRAM": str(ninja),
                "CMAKE_CXX_COMPILER": str(clangxx),
                "CMAKE_PREFIX_PATH": str(sdk_dir),
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

    # Add a browser (Emscripten/WebGPU) preset when the web SDK and the engine's
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
    if web_sdk_dir is not None and emscripten_toolchain.is_file():
        configure_presets.append(
            {
                "name": "web-emscripten-development",
                "displayName": "Web Emscripten Development",
                "inherits": "web-emscripten-development-base",
                "toolchainFile": str(emscripten_toolchain),
                "cacheVariables": {
                    "CMAKE_MAKE_PROGRAM": str(ninja),
                    "CMAKE_PREFIX_PATH": str(web_sdk_dir),
                    "CMAKE_FIND_ROOT_PATH": str(web_sdk_dir),
                    "LUDUS_SLANG_COMPILER": str(slang_compiler),
                    "LUDUS_SPIRV_VALIDATOR": str(spirv_validator),
                },
            }
        )
        build_presets.append(
            {
                "name": "web-emscripten-development",
                "configurePreset": "web-emscripten-development",
            }
        )

    presets = {
        "version": 6,
        "configurePresets": configure_presets,
        "buildPresets": build_presets,
    }

    path = repo_root / "CMakeUserPresets.json"

    path.write_text(
        json.dumps(presets, indent=2) + "\n",
        encoding="utf-8",
    )

    print(f"Generated user presets: {path}")


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

    run([str(init_script)], cwd=ludus_source)


def install_ludus_sdk(
    ludus_source: Path,
    preset: str,
) -> Path:
    install_script = ludus_source / "scripts" / "install-sdk"

    if not install_script.is_file():
        raise RuntimeError(f"Missing Ludus SDK install script: {install_script}")

    run([str(install_script), preset], cwd=ludus_source)

    sdk_dir = ludus_source / "out" / "install" / preset

    if not sdk_dir.is_dir():
        raise RuntimeError(
            "Ludus SDK install completed, but expected install directory "
            f"does not exist: {sdk_dir}"
        )

    return sdk_dir


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

    
def init_command(args: argparse.Namespace) -> None:
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

    # Optional separate web SDK prefix (install with the web preset). Discovered
    # by convention; absent on native-only setups.
    web_sdk_dir = None
    if args.web_sdk_dir:
        candidate = Path(args.web_sdk_dir).expanduser().resolve()
        if candidate.is_dir():
            web_sdk_dir = candidate
    else:
        candidate = ludus_source / "out" / "install" / "web-emscripten-development"
        if candidate.is_dir():
            web_sdk_dir = candidate

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

    print()
    print("Ludus Sandbox initialization complete.")
    print()
    print(f"Ludus source:     {ludus_source}")
    print(f"Ludus SDK:        {sdk_dir}")
    print(f"Slang compiler:   {slang_compiler}")
    print(f"SPIR-V validator: {spirv_validator}")
    print()
    print("The sandbox has been configured but not built.")
    print()
    print("Build with:")
    print(f"  cmake --build --preset {args.preset}")


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

    init_parser.set_defaults(func=init_command)

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
    except RuntimeError as error:
        print(f"error: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())