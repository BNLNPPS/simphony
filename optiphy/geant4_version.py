import argparse
import os
import re
import subprocess


def parse_geant4_version(version):
    match = re.match(r"^\s*(\d+)\.(\d+)(?:\.(\d+))?", version)
    if match is None:
        raise RuntimeError(f"Unable to parse Geant4 version: {version!r}")

    return tuple(int(part) for part in match.groups(default="0"))


def detect_geant4_version():
    version = os.environ.get("GEANT4_VERSION")
    if version:
        return version.strip()

    try:
        return subprocess.check_output(["geant4-config", "--version"], text=True).strip()
    except (OSError, subprocess.CalledProcessError) as err:
        raise RuntimeError(
            "Unable to determine Geant4 version. Set GEANT4_VERSION or ensure geant4-config is available."
        ) from err


def geant4_series(version=None):
    resolved = detect_geant4_version() if version is None else version
    major, minor, _patch = parse_geant4_version(resolved)

    if (major, minor) == (11, 3):
        return "11.3"
    if (major, minor) >= (11, 4):
        return "11.4+"

    raise RuntimeError(
        f"Unsupported Geant4 version {resolved}. Add support for this release family."
    )


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("field", nargs="?", choices=("version", "series"), default="version")
    args = parser.parse_args()

    version = detect_geant4_version()

    if args.field == "version":
        print(version)
    else:
        print(geant4_series(version))


if __name__ == "__main__":
    main()
