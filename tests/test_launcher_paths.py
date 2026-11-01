"""Frozen launchers keep payloads and recovery files beside the executable."""
from pathlib import Path
import os
import subprocess
import sys
import unittest
from unittest.mock import patch

from tools import launcher_paths


class LauncherPathTests(unittest.TestCase):
    @unittest.skipUnless(os.name == "nt", "Windows DLL search API")
    def test_frozen_startup_restores_system_dll_search_for_external_programs(self):
        code = """
import ctypes
import sys
from tools.launcher_paths import prepare_frozen_process
kernel = ctypes.windll.kernel32
kernel.SetDllDirectoryW.argtypes = [ctypes.c_wchar_p]
assert kernel.SetDllDirectoryW('C:\\Windows')
sys.frozen = True
prepare_frozen_process()
buffer = ctypes.create_unicode_buffer(4096)
assert kernel.GetDllDirectoryW(len(buffer), buffer) == 0
assert buffer.value == ''
"""
        result = subprocess.run([sys.executable, "-c", code], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_source_uses_repository_not_python_interpreter_directory(self):
        with patch.object(launcher_paths.sys, "frozen", False, create=True):
            self.assertEqual(launcher_paths.application_root(), Path(launcher_paths.__file__).resolve().parents[1])

    def test_frozen_uses_executable_directory_not_temporary_bundle(self):
        with patch.object(launcher_paths.sys, "frozen", True, create=True), \
             patch.object(launcher_paths.sys, "executable", str(Path("package") / "Portal 2 VR Enriched Launcher.exe")), \
             patch.object(launcher_paths.sys, "_MEIPASS", str(Path("temporary-bundle")), create=True):
            root = launcher_paths.application_root()
            self.assertEqual(root, Path("package").resolve())
            self.assertEqual(root / "tools/.launcher-state.json", Path("package/tools/.launcher-state.json").resolve())


if __name__ == "__main__":
    unittest.main()
