"""Release bootstrap wiring without downloading/building an engine."""
import json
import os
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

import sandbox


class ReleaseBootstrapTests(unittest.TestCase):
    def test_release_and_development_presets_keep_distinct_build_trees(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            source, project = root / 'engine', root / 'game'
            project.mkdir()
            for name in ('out/host-tools/venv/bin/cmake', 'out/host-tools/venv/bin/ninja',
                         'out/host-tools/bin/clang++',
                         'out/host-tools/emsdk/upstream/emscripten/cmake/Modules/Platform/Emscripten.cmake'):
                path = source / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.touch()
            sandbox.write_user_presets(project, source, root / 'native-sdk', root / 'slangc',
                                       root / 'spirv-val', root / 'web-sdk')
            obj = json.loads((project / 'CMakeUserPresets.json').read_text())
            presets = {preset['name']: preset for preset in obj['configurePresets']}
            release = presets['web-emscripten-release']
            development = presets['web-emscripten-development']
            self.assertEqual(release['cacheVariables']['CMAKE_BUILD_TYPE'], 'Release')
            self.assertEqual(release['binaryDir'], '${sourceDir}/out/build/web-emscripten-release')
            self.assertEqual(release['inherits'], ['web-emscripten-development', 'web-emscripten-release-base'])
            self.assertEqual(development['cacheVariables']['CMAKE_PREFIX_PATH'], str(root / 'web-sdk'))
            self.assertEqual(development['cacheVariables']['LUDUS_SPIRV_CROSS'], str(source / 'out/shader-tools/spirv-cross/bin/spirv-cross'))
            self.assertEqual({preset['name'] for preset in obj['buildPresets']},
                             {'linux-clang-development', 'web-emscripten-development', 'web-emscripten-release'})

    def test_release_sdk_uses_release_engine_preset(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            for name in ('scripts/init', 'scripts/build', 'out/host-tools/venv/bin/cmake'):
                path = root / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.touch()
            expected = root / 'out/install/web-emscripten-release'
            expected.mkdir(parents=True)
            with patch.object(sandbox, 'run') as run:
                self.assertEqual(sandbox.install_ludus_web_sdk(root), expected)
                self.assertIn('web-emscripten-release', run.call_args_list[0].args[0])
                self.assertIn('web-emscripten-release', run.call_args_list[1].args[0])
                self.assertIn(str(expected), run.call_args_list[2].args[0])

    def test_bootstrap_command_never_receives_upload_secret(self):
        with patch.dict(os.environ, {'BUTLER_API_KEY': 'test-secret'}), patch.object(sandbox.subprocess, 'run') as run:
            sandbox.run(['cmake', '--version'])
            self.assertNotIn('BUTLER_API_KEY', run.call_args.kwargs['env'])
            self.assertEqual(run.call_args.args[0], ['cmake', '--version'])

    def test_release_flag_is_opt_in(self):
        self.assertFalse(sandbox.build_parser().parse_args(['init']).web_release)
        self.assertTrue(sandbox.build_parser().parse_args(['init', '--web-release']).web_release)


if __name__ == '__main__':
    unittest.main()
