import re

import pytest

from optiphy.geant4_version import geant4_series, parse_geant4_version


@pytest.mark.parametrize(
    ("version", "expected"),
    [
        ("11.3.2", "11.3"),
        ("11.3.0.beta", "11.3"),
        ("11.4.0", "11.4+"),
        ("11.4.p01", "11.4+"),
        ("11.4.0.beta", "11.4+"),
        ("11.5.1", "11.4+"),
        ("12.0.0", "11.4+"),
        ("12.0.p02", "11.4+"),
        ("12.0.0.beta", "11.4+"),
        ("12.1.3", "11.4+"),
        ("13.0.0", "11.4+"),
    ],
)
def test_geant4_series_supported_versions(version, expected):
    assert geant4_series(version) == expected


@pytest.mark.parametrize("version", ["9.6.1", "9.6.p03", "10.7.4"])
def test_geant4_series_rejects_older_major_versions(version):
    with pytest.raises(RuntimeError, match=re.escape(f"Unsupported Geant4 version {version!r}")):
        geant4_series(version)


@pytest.mark.parametrize("version", ["11.2.0", "11.2.0.beta"])
def test_geant4_series_rejects_unsupported_11_minor(version):
    with pytest.raises(RuntimeError, match=re.escape(f"Unsupported Geant4 version {version!r}")):
        geant4_series(version)


def test_geant4_series_rejects_unparseable_version():
    version = "not-a-version"
    with pytest.raises(RuntimeError, match=re.escape(f"Unable to parse Geant4 version: {version!r}")):
        geant4_series(version)


@pytest.mark.parametrize(
    ("version", "expected"),
    [
        ("11.4", (11, 4, 0)),
        ("11.4.0", (11, 4, 0)),
        ("11.4.0.beta", (11, 4, 0)),
        ("11.4.p01", (11, 4, 1)),
        ("11.2.0.beta", (11, 2, 0)),
        ("12.0.p02", (12, 0, 2)),
    ],
)
def test_parse_geant4_version_accepts_supported_patch_formats(version, expected):
    assert parse_geant4_version(version) == expected


@pytest.mark.parametrize("version", ["11.4.p", "11.4-dev", "11.4foo", "11.4.p01foo", "11.4.0.betafoo"])
def test_parse_geant4_version_rejects_trailing_suffixes(version):
    with pytest.raises(RuntimeError, match=re.escape(f"Unable to parse Geant4 version: {version!r}")):
        parse_geant4_version(version)
