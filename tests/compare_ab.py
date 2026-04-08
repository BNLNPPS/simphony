import argparse
import numpy as np

from pathlib import Path
from optiphy.geant4_version import detect_geant4_version, geant4_series


EXPECTED_DIFF = {
    "11.3": [14, 22, 32, 34, 40, 81, 85],
    "11.4+": [0, 30, 32, 34, 42, 69, 78, 85, 86],
}


def expected_diff_for_version(version):
    return EXPECTED_DIFF[geant4_series(version)]


parser = argparse.ArgumentParser()
parser.add_argument(
    "--base",
    default="/tmp",
    help="directory containing the ALL... event folders",
)
args = parser.parse_args()

base = Path(args.base)
geant4_version = detect_geant4_version()
expected_diff = expected_diff_for_version(geant4_version)

a = np.load(base / "ALL0_no_opticks_event_name/A000/record.npy")
b = np.load(base / "ALL0_no_opticks_event_name/B000/f000/record.npy")

print(a.shape)
print(b.shape)
print(f"GEANT4_VERSION={geant4_version}")
print(f"EXPECTED_DIFF={expected_diff}")

assert a.shape == b.shape

# Geant4 and Opticks record one-step-shifted sequences for this test geometry,
# so compare the aligned slices directly, including time.
a_cmp = a[:, 1:]
b_cmp = b[:, 0:-1]

diff = [i for i, (ac, bc) in enumerate(zip(a_cmp, b_cmp)) if not np.allclose(ac, bc, rtol=0, atol=1e-5)]
print(diff)

assert diff == expected_diff
