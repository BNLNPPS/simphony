import subprocess
import sys
from pathlib import Path


WLS_TEST_SCRIPT = Path(__file__).parents[1] / "ana" / "wls_test.py"


def test_package_import_does_not_mutate_sys_path():
    code = """
import sys

before = list(sys.path)
import optiphy.ana.wls_test
assert sys.path == before, f"sys.path mutated: {before!r} -> {sys.path!r}"
"""

    result = subprocess.run(
        [sys.executable, "-c", code],
        capture_output=True,
        text=True,
    )

    assert result.returncode == 0, result.stderr


def test_direct_script_help_remains_supported(tmp_path):
    result = subprocess.run(
        [sys.executable, str(WLS_TEST_SCRIPT), "--help"],
        cwd=tmp_path,
        capture_output=True,
        text=True,
    )

    assert result.returncode == 0, result.stderr
    assert "usage:" in result.stdout
