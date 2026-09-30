import subprocess
from pathlib import Path

import pytest


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
UNSUPPORTED_VERSION_WARNING = "Simphony supports OptiX 8.0.0 and newer"


def configure_with_optix(tmp_path: Path, encoded_version: int) -> subprocess.CompletedProcess[str]:
    source_dir = tmp_path / "source"
    build_dir = tmp_path / "build"
    optix_dir = tmp_path / "optix"
    module_dir = tmp_path / "modules"

    source_dir.mkdir()
    (optix_dir / "include").mkdir(parents=True)
    module_dir.mkdir()

    (optix_dir / "include" / "optix.h").write_text(
        f"#define OPTIX_VERSION {encoded_version}\n"
    )
    (module_dir / "FindCUDAToolkit.cmake").write_text(
        "set(CUDAToolkit_FOUND TRUE)\n"
        "set(CUDAToolkit_INCLUDE_DIRS \"\")\n"
    )
    (source_dir / "CMakeLists.txt").write_text(
        "cmake_minimum_required(VERSION 3.22)\n"
        "project(optix_version_probe LANGUAGES NONE)\n"
        f'list(APPEND CMAKE_MODULE_PATH "{module_dir}" "{REPOSITORY_ROOT / "cmake"}")\n'
        "find_package(OptiX REQUIRED)\n"
    )

    return subprocess.run(
        [
            "cmake",
            "-S",
            str(source_dir),
            "-B",
            str(build_dir),
            f"-DOptiX_INSTALL_DIR={optix_dir}",
        ],
        check=False,
        capture_output=True,
        text=True,
    )


@pytest.mark.parametrize(
    ("encoded_version", "expects_warning"),
    [(70700, True), (80000, False)],
)
def test_optix_support_floor_is_advisory(tmp_path, encoded_version, expects_warning):
    result = configure_with_optix(tmp_path, encoded_version)
    output = result.stdout + result.stderr

    assert result.returncode == 0, output
    assert (UNSUPPORTED_VERSION_WARNING in output) is expects_warning
