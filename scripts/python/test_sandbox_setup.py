"""Setup regressions, including real CMake preset discovery without an SDK build."""
import os
import shutil
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

import sandbox


class SetupTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="sandbox setup ")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.project = self.root / "project"
        self.project.mkdir()
        shutil.copyfile(Path(sandbox.__file__).resolve().parents[2] / "CMakePresets.json",
                        self.project / "CMakePresets.json")
        self.source = self.root / "engine"
        self.sdk = self.root / "sdk"
        package = self.sdk / "lib/cmake/Ludus"
        package.mkdir(parents=True)
        for name in ("LudusConfig.cmake", "LudusTargets.cmake", "LudusShaders.cmake"):
            (package / name).touch()
        self.slang = self.source / "out/shader-tools/slang/bin/slangc"
        self.validator = self.source / "out/shader-tools/spirv-tools/bin/spirv-val"
        for path in (self.slang, self.validator, self.source / "out/host-tools/bin/clang++",
                     self.source / "out/host-tools/venv/bin/ninja"):
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text("#!/bin/sh\nexit 0\n")
            path.chmod(0o755)
        cmake = os.environ.get("LUDUS_TEST_CMAKE") or shutil.which("cmake")
        if cmake is None:
            self.skipTest("CMake is required for preset discovery tests")
        (self.source / "out/host-tools/venv/bin/cmake").symlink_to(Path(cmake).resolve())

    def generate(self, web=None):
        sandbox.write_user_presets(self.project, self.source, self.sdk, self.slang, self.validator, web)

    def test_fresh_clone_reports_missing_setup(self):
        with self.assertRaisesRegex(RuntimeError, "CMakeUserPresets.json is missing"):
            sandbox.check_setup(self.project, self.source)

    def test_generation_passes_real_cmake_discovery_in_path_with_spaces(self):
        self.generate()
        sandbox.check_setup(self.project, self.source)
        presets = sandbox.read_json_object(self.project / "CMakeUserPresets.json")
        self.assertIn("cmakeExecutable", presets["configurePresets"][0])
        self.assertNotIn("cmake.cmakePath", sandbox.read_json_object(self.project / ".vscode/settings.json"))

    def test_repeated_generation_preserves_custom_presets_and_settings(self):
        sandbox.write_json_object(self.project / "CMakeUserPresets.json", {
            "version": 6, "vendor": {"example.org/tool/1.0": {"enabled": True}},
            "configurePresets": [{"name": "custom", "inherits": "linux-clang-development-base"}],
        })
        sandbox.write_json_object(self.project / ".vscode/settings.json", {"editor.tabSize": 4})
        self.generate()
        before = (self.project / "CMakeUserPresets.json").read_bytes()
        self.generate()
        self.assertEqual(before, (self.project / "CMakeUserPresets.json").read_bytes())
        self.assertEqual(sandbox.read_json_object(self.project / ".vscode/settings.json")["editor.tabSize"], 4)
        self.assertIn("vendor", sandbox.read_json_object(self.project / "CMakeUserPresets.json"))

    def test_hidden_native_preset_is_rejected(self):
        self.generate()
        path = self.project / "CMakeUserPresets.json"
        data = sandbox.read_json_object(path)
        data["configurePresets"][0]["hidden"] = True
        sandbox.write_json_object(path, data)
        with self.assertRaisesRegex(RuntimeError, "Missing selectable configure"):
            sandbox.check_setup(self.project, self.source)

    def test_missing_build_or_test_presets_are_rejected(self):
        for key in ("buildPresets", "testPresets"):
            self.generate()
            path = self.project / "CMakeUserPresets.json"
            data = sandbox.read_json_object(path)
            data[key] = []
            sandbox.write_json_object(path, data)
            with self.assertRaisesRegex(RuntimeError, "Missing selectable"):
                sandbox.check_setup(self.project, self.source)

    def test_moved_or_removed_tool_is_rejected(self):
        self.generate()
        self.slang.unlink()
        with self.assertRaisesRegex(RuntimeError, "missing executable LUDUS_SLANG_COMPILER"):
            sandbox.check_setup(self.project, self.source)

    def test_outdated_sdk_does_not_replace_presets(self):
        self.generate()
        path = self.project / "CMakeUserPresets.json"
        before = path.read_bytes()
        (self.sdk / "lib/cmake/Ludus/LudusShaders.cmake").unlink()
        with self.assertRaisesRegex(RuntimeError, "SDK is incomplete or outdated"):
            self.generate()
        self.assertEqual(before, path.read_bytes())

    def test_malformed_custom_presets_are_preserved(self):
        path = self.project / "CMakeUserPresets.json"
        path.write_text("{ invalid JSON")
        with self.assertRaisesRegex(RuntimeError, "fix its JSON"):
            self.generate()
        self.assertEqual(path.read_text(), "{ invalid JSON")

    def test_web_sdk_without_toolchain_is_rejected(self):
        with self.assertRaisesRegex(RuntimeError, "toolchain is missing"):
            self.generate(web=self.sdk)
        self.assertFalse((self.project / "CMakeUserPresets.json").exists())

    def test_disabled_or_broken_inheritance_is_rejected(self):
        for change in ({"condition": False}, {"inherits": "missing-base"}):
            self.generate()
            path = self.project / "CMakeUserPresets.json"
            data = sandbox.read_json_object(path)
            data["configurePresets"][0].update(change)
            sandbox.write_json_object(path, data)
            with self.assertRaises(RuntimeError):
                sandbox.check_setup(self.project, self.source)

    def test_doctor_is_read_only(self):
        self.generate()
        paths = [path for path in self.project.rglob("*") if path.is_file()]
        before = {path: (path.read_bytes(), path.stat().st_mtime_ns) for path in paths}
        sandbox.check_setup(self.project, self.source)
        self.assertEqual(before, {path: (path.read_bytes(), path.stat().st_mtime_ns) for path in paths})
        self.assertEqual(paths, [path for path in self.project.rglob("*") if path.is_file()])

    def test_stale_editor_mode_is_rejected(self):
        self.generate()
        sandbox.write_json_object(self.project / ".vscode/settings.json", {"cmake.useCMakePresets": "never"})
        with self.assertRaisesRegex(RuntimeError, "VS Code CMake settings are stale"):
            sandbox.check_setup(self.project, self.source)

    def test_repeated_web_repair_preserves_release_presets(self):
        toolchain = self.source / "out/host-tools/emsdk/upstream/emscripten/cmake/Modules/Platform/Emscripten.cmake"
        toolchain.parent.mkdir(parents=True)
        toolchain.touch()
        translator = self.source / "out/shader-tools/spirv-cross/bin/spirv-cross"
        translator.parent.mkdir(parents=True)
        translator.write_text("#!/bin/sh\nexit 0\n")
        translator.chmod(0o755)
        self.generate(web=self.sdk)
        path = self.project / "CMakeUserPresets.json"
        before = path.read_bytes()
        self.generate(web=self.sdk)
        self.assertEqual(before, path.read_bytes())
        data = sandbox.read_json_object(path)
        for key in ("configurePresets", "buildPresets"):
            self.assertEqual(sum(item["name"] == "web-emscripten-release" for item in data[key]), 1)
        translator.unlink()
        with self.assertRaisesRegex(RuntimeError, "SPIRV-Cross translator is missing"):
            sandbox.check_setup(self.project, self.source)

    def test_engine_initialization_is_noninteractive_and_scoped(self):
        (self.source / "init.sh").touch()
        with patch.object(sandbox, "run") as run:
            sandbox.initialize_ludus(self.source)
        self.assertEqual(run.call_args.args[0][1:], [sandbox.DEFAULT_PRESET, "--preset-only"])
        self.assertEqual(run.call_args.kwargs["env"]["CI"], "true")


if __name__ == "__main__":
    unittest.main()
