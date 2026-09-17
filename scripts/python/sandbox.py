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

    presets = {
        "version": 6,
        "configurePresets": [
            {
                "name": "linux-clang-development",
                "displayName": "Linux Clang Development",
                "inherits": "linux-clang-development-base",
                "cacheVariables": {
                    "CMAKE_MAKE_PROGRAM": str(ninja),
                    "CMAKE_CXX_COMPILER": str(clangxx),
                    "CMAKE_PREFIX_PATH": str(sdk_dir),
                },
            }
        ],
        "buildPresets": [
            {
                "name": "linux-clang-development",
                "configurePreset": "linux-clang-development",
            }
        ],
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


def configure_sandbox(
    repo_root: Path,
    ludus_source: Path,
    preset: str,
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
        [str(cmake), "--preset", preset],
        cwd=repo_root,
    )

    
def init_command(args: argparse.Namespace) -> None:
    repo_root = Path(__file__).resolve().parents[2]

    print("Initializing Ludus Sandbox")
    print(f"Repository: {repo_root}")

    ludus_source = acquire_ludus(
        repo_root=repo_root,
        source_override=args.ludus_source,
    )

    initialize_ludus(ludus_source)

    sdk_dir = install_ludus_sdk(
        ludus_source=ludus_source,
        preset=args.preset,
    )

    write_user_presets(
        repo_root=repo_root,
        ludus_source=ludus_source,
        sdk_dir=sdk_dir,
    )

    configure_sandbox(
        repo_root=repo_root,
        ludus_source=ludus_source,
        preset=args.preset,
    )

    print()
    print("Ludus Sandbox initialization complete.")
    print()
    print(f"Ludus source: {ludus_source}")
    print(f"Ludus SDK:    {sdk_dir}")
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
        help=f"CMake/Ludus preset to use (default: {DEFAULT_PRESET}).",
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