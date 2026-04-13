#!/usr/bin/env python3
"""
test_trapezoid_intersect.py — Validate trapezoid intersection math

Tests the 6-plane slab intersection algorithm used in
csg_intersect_leaf_trapezoid.h against analytical expectations.
"""
import numpy as np
import sys

def make_trapezoid_planes(dx1, dy1, dz, dx2, dy2):
    """Compute 6 face planes (nx, ny, nz, d) with outward unit normals."""
    two_dz = 2 * dz
    ddx = dx2 - dx1
    Lx = np.sqrt(two_dz**2 + ddx**2)
    ddy = dy2 - dy1
    Ly = np.sqrt(two_dz**2 + ddy**2)

    # NB: opposing faces share the same z-component in their normals.
    # -X has nz = -ddx/Lx (same as +X), NOT +ddx/Lx.
    planes = np.array([
        [ two_dz / Lx,           0, -ddx / Lx, dz * (dx1 + dx2) / Lx],  # +X
        [-two_dz / Lx,           0, -ddx / Lx, dz * (dx1 + dx2) / Lx],  # -X
        [          0,  two_dz / Ly, -ddy / Ly, dz * (dy1 + dy2) / Ly],  # +Y
        [          0, -two_dz / Ly, -ddy / Ly, dz * (dy1 + dy2) / Ly],  # -Y
        [          0,            0,         1,                     dz],  # +Z
        [          0,            0,        -1,                     dz],  # -Z
    ])
    return planes


def intersect_trapezoid(o, d, dx1, dy1, dz, dx2, dy2):
    """Python implementation of the slab intersection algorithm.
    Returns (t_hit, normal) or (None, None) if miss.
    """
    planes = make_trapezoid_planes(dx1, dy1, dz, dx2, dy2)

    t0 = -1e30
    t1 = 1e30
    n0 = np.zeros(3)
    n1 = np.zeros(3)

    for plane in planes:
        n = plane[:3]
        dp = plane[3]
        nd = np.dot(n, d)
        no = np.dot(n, o)
        dist = no - dp

        if nd == 0:
            if dist > 0:
                return None, None  # parallel outside
            continue  # parallel inside

        t_cand = -dist / nd
        if nd < 0:  # entering
            if t_cand > t0:
                t0 = t_cand
                n0 = n.copy()
        else:  # exiting
            if t_cand < t1:
                t1 = t_cand
                n1 = n.copy()

    if t0 >= t1:
        return None, None

    t_min = 0.0
    if t0 > t_min:
        return t0, n0
    elif t1 > t_min:
        return t1, n1
    return None, None


def signed_distance(p, dx1, dy1, dz, dx2, dy2):
    """Signed distance: positive outside, negative inside."""
    planes = make_trapezoid_planes(dx1, dy1, dz, dx2, dy2)
    sd = -1e30
    for plane in planes:
        n = plane[:3]
        dp = plane[3]
        sd = max(sd, np.dot(n, p) - dp)
    return sd


def test_axis_aligned_hits():
    """Fire rays along each axis and check they hit the correct face."""
    dx1, dy1, dz, dx2, dy2 = 50, 40, 150, 100, 80
    planes = make_trapezoid_planes(dx1, dy1, dz, dx2, dy2)
    n_pass = 0

    # Ray along +X from outside, should hit -X face
    o = np.array([-200.0, 0.0, 0.0])
    d = np.array([1.0, 0.0, 0.0])
    t, n = intersect_trapezoid(o, d, dx1, dy1, dz, dx2, dy2)
    assert t is not None, "Expected hit on -X face"
    hit = o + t * d
    assert n[0] < 0, f"-X face normal should point in -x, got {n}"
    assert abs(signed_distance(hit, dx1, dy1, dz, dx2, dy2)) < 1e-4, "Hit should be on surface"
    n_pass += 1

    # Ray along -X from outside, should hit +X face
    o = np.array([200.0, 0.0, 0.0])
    d = np.array([-1.0, 0.0, 0.0])
    t, n = intersect_trapezoid(o, d, dx1, dy1, dz, dx2, dy2)
    assert t is not None, "Expected hit on +X face"
    hit = o + t * d
    assert n[0] > 0, f"+X face normal should point in +x, got {n}"
    assert abs(signed_distance(hit, dx1, dy1, dz, dx2, dy2)) < 1e-4
    n_pass += 1

    # Ray along +Y
    o = np.array([0.0, -200.0, 0.0])
    d = np.array([0.0, 1.0, 0.0])
    t, n = intersect_trapezoid(o, d, dx1, dy1, dz, dx2, dy2)
    assert t is not None, "Expected hit on -Y face"
    assert n[1] < 0, f"-Y face normal should point in -y, got {n}"
    n_pass += 1

    # Ray along +Z
    o = np.array([0.0, 0.0, -200.0])
    d = np.array([0.0, 0.0, 1.0])
    t, n = intersect_trapezoid(o, d, dx1, dy1, dz, dx2, dy2)
    assert t is not None, "Expected hit on -Z face"
    assert abs(n[2] - (-1.0)) < 1e-6, f"-Z normal should be (0,0,-1), got {n}"
    hit = o + t * d
    assert abs(hit[2] - (-dz)) < 1e-4, f"Hit z should be -dz={-dz}, got {hit[2]}"
    n_pass += 1

    # Ray along -Z
    o = np.array([0.0, 0.0, 200.0])
    d = np.array([0.0, 0.0, -1.0])
    t, n = intersect_trapezoid(o, d, dx1, dy1, dz, dx2, dy2)
    assert t is not None, "Expected hit on +Z face"
    assert abs(n[2] - 1.0) < 1e-6, f"+Z normal should be (0,0,1), got {n}"
    hit = o + t * d
    assert abs(hit[2] - dz) < 1e-4, f"Hit z should be dz={dz}, got {hit[2]}"
    n_pass += 1

    print(f"  axis_aligned_hits: {n_pass}/5 passed")
    return n_pass


def test_tilted_face_normals():
    """Verify tilted face normals are correct for asymmetric trapezoid."""
    dx1, dy1, dz, dx2, dy2 = 50, 40, 150, 100, 80
    planes = make_trapezoid_planes(dx1, dy1, dz, dx2, dy2)
    n_pass = 0

    # +X face normal should be in the xz-plane, tilted outward
    n_px = planes[0, :3]
    assert abs(n_px[1]) < 1e-10, "+X normal should have ny=0"
    assert n_px[0] > 0, "+X normal should have nx>0"
    # Since dx2 > dx1, the face tilts outward at top => nz < 0
    assert n_px[2] < 0, "+X: dx2>dx1 means face tilts outward at +z, so nz<0"
    assert abs(np.linalg.norm(n_px) - 1.0) < 1e-10, "Should be unit normal"
    n_pass += 1

    # +Y face normal should be in the yz-plane
    n_py = planes[2, :3]
    assert abs(n_py[0]) < 1e-10, "+Y normal should have nx=0"
    assert n_py[1] > 0, "+Y normal should have ny>0"
    assert n_py[2] < 0, "+Y: dy2>dy1 means nz<0"
    assert abs(np.linalg.norm(n_py) - 1.0) < 1e-10
    n_pass += 1

    # Verify symmetry: +X and -X differ only in sign of x-component; z-component is shared
    n_mx = planes[1, :3]
    assert abs(n_mx[0] + n_px[0]) < 1e-10, "x-components should be negated"
    assert abs(n_mx[2] - n_px[2]) < 1e-10, "z-components should be the SAME"
    assert abs(planes[0, 3] - planes[1, 3]) < 1e-10, "+X and -X should have same distance"
    n_pass += 1

    print(f"  tilted_face_normals: {n_pass}/3 passed")
    return n_pass


def test_box_degenerate():
    """When dx1==dx2 and dy1==dy2, trapezoid should behave like a box."""
    dx, dy, dz = 50, 40, 150
    planes = make_trapezoid_planes(dx, dy, dz, dx, dy)
    n_pass = 0

    # All normals should be axis-aligned
    for i, (name, expected) in enumerate([
        ("+X", [1, 0, 0]), ("-X", [-1, 0, 0]),
        ("+Y", [0, 1, 0]), ("-Y", [0, -1, 0]),
        ("+Z", [0, 0, 1]), ("-Z", [0, 0, -1]),
    ]):
        n = planes[i, :3]
        assert np.allclose(n, expected, atol=1e-10), f"{name} normal: expected {expected}, got {n}"
    n_pass += 1

    # Distances should match box half-lengths
    assert abs(planes[0, 3] - dx) < 1e-10
    assert abs(planes[2, 3] - dy) < 1e-10
    assert abs(planes[4, 3] - dz) < 1e-10
    n_pass += 1

    # Intersection should match box
    o = np.array([-200.0, 0.0, 0.0])
    d = np.array([1.0, 0.0, 0.0])
    t, n = intersect_trapezoid(o, d, dx, dy, dz, dx, dy)
    assert t is not None
    hit = o + t * d
    assert abs(hit[0] - (-dx)) < 1e-4, f"Should hit x=-dx, got {hit[0]}"
    n_pass += 1

    print(f"  box_degenerate: {n_pass}/3 passed")
    return n_pass


def test_miss():
    """Rays that should miss the trapezoid."""
    dx1, dy1, dz, dx2, dy2 = 50, 40, 150, 100, 80
    n_pass = 0

    # Ray parallel to +X face, outside
    o = np.array([200.0, 0.0, 0.0])
    d = np.array([0.0, 1.0, 0.0])
    t, n = intersect_trapezoid(o, d, dx1, dy1, dz, dx2, dy2)
    assert t is None, "Should miss (parallel outside)"
    n_pass += 1

    # Ray going away from trapezoid
    o = np.array([200.0, 0.0, 0.0])
    d = np.array([1.0, 0.0, 0.0])
    t, n = intersect_trapezoid(o, d, dx1, dy1, dz, dx2, dy2)
    assert t is None or t < 0, "Should miss (going away)"
    n_pass += 1

    print(f"  miss: {n_pass}/2 passed")
    return n_pass


def test_inside_ray():
    """Ray from inside should hit the exit face."""
    dx1, dy1, dz, dx2, dy2 = 50, 40, 150, 100, 80
    n_pass = 0

    # From center, pointing +z
    o = np.array([0.0, 0.0, 0.0])
    d = np.array([0.0, 0.0, 1.0])
    t, n = intersect_trapezoid(o, d, dx1, dy1, dz, dx2, dy2)
    assert t is not None, "Should hit +Z from inside"
    assert abs(t - dz) < 1e-4, f"Distance to +Z should be dz={dz}, got {t}"
    assert abs(n[2] - 1.0) < 1e-6, f"Normal should be (0,0,1), got {n}"
    n_pass += 1

    # From center, pointing +x
    o = np.array([0.0, 0.0, 0.0])
    d = np.array([1.0, 0.0, 0.0])
    t, n = intersect_trapezoid(o, d, dx1, dy1, dz, dx2, dy2)
    assert t is not None, "Should hit +X from inside"
    # At z=0, x-extent is (dx1+dx2)/2 = 75
    x_at_z0 = (dx1 + dx2) / 2
    assert abs(t - x_at_z0) < 0.1, f"At z=0, +X face is at x={(dx1+dx2)/2}, got t={t}"
    n_pass += 1

    print(f"  inside_ray: {n_pass}/2 passed")
    return n_pass


def test_signed_distance():
    """Verify signed distance function."""
    dx1, dy1, dz, dx2, dy2 = 50, 40, 150, 100, 80
    n_pass = 0

    # Center should be inside (negative)
    sd = signed_distance(np.array([0, 0, 0]), dx1, dy1, dz, dx2, dy2)
    assert sd < 0, f"Center should be inside, got sd={sd}"
    n_pass += 1

    # Far outside should be positive
    sd = signed_distance(np.array([200, 0, 0]), dx1, dy1, dz, dx2, dy2)
    assert sd > 0, f"Far outside should be positive, got sd={sd}"
    n_pass += 1

    # On +Z face should be ~0
    sd = signed_distance(np.array([0, 0, dz]), dx1, dy1, dz, dx2, dy2)
    assert abs(sd) < 1e-10, f"On face should be ~0, got sd={sd}"
    n_pass += 1

    # On vertex should be ~0
    sd = signed_distance(np.array([dx1, dy1, -dz]), dx1, dy1, dz, dx2, dy2)
    assert abs(sd) < 1e-6, f"On vertex should be ~0, got sd={sd}"
    n_pass += 1

    print(f"  signed_distance: {n_pass}/4 passed")
    return n_pass


def test_vertex_verification():
    """Verify all 8 vertices lie on the trapezoid surface (sd=0)."""
    dx1, dy1, dz, dx2, dy2 = 50, 40, 150, 100, 80
    vertices = np.array([
        [-dx1, -dy1, -dz], [+dx1, -dy1, -dz], [-dx1, +dy1, -dz], [+dx1, +dy1, -dz],
        [-dx2, -dy2, +dz], [+dx2, -dy2, +dz], [-dx2, +dy2, +dz], [+dx2, +dy2, +dz],
    ])
    n_pass = 0
    for i, v in enumerate(vertices):
        sd = signed_distance(v, dx1, dy1, dz, dx2, dy2)
        assert abs(sd) < 1e-6, f"Vertex {i} {v}: sd={sd} should be ~0"
        n_pass += 1

    print(f"  vertex_verification: {n_pass}/8 passed")
    return n_pass


def test_legacy_plane_comparison():
    """Compare our planes against the legacy Python make_trapezoid planes."""
    dx1, dy1, dz, dx2, dy2 = 50, 40, 150, 100, 80

    # Compute planes via vertex cross products (legacy method)
    v = np.array([
        [-dx1, -dy1, -dz], [+dx1, -dy1, -dz], [-dx1, +dy1, -dz], [+dx1, +dy1, -dz],
        [-dx2, -dy2, +dz], [+dx2, -dy2, +dz], [-dx2, +dy2, +dz], [+dx2, +dy2, +dz],
    ], dtype=np.float64)

    def make_plane3(a, b, c):
        ba = b - a
        ca = c - a
        n = np.cross(ba, ca)
        n /= np.linalg.norm(n)
        d = np.dot(n, a)
        return np.array([n[0], n[1], n[2], d])

    legacy_planes = np.array([
        make_plane3(v[3], v[7], v[5]),  # +X
        make_plane3(v[0], v[4], v[6]),  # -X
        make_plane3(v[2], v[6], v[7]),  # +Y
        make_plane3(v[1], v[5], v[4]),  # -Y
        make_plane3(v[5], v[7], v[6]),  # +Z
        make_plane3(v[3], v[1], v[0]),  # -Z
    ])

    our_planes = make_trapezoid_planes(dx1, dy1, dz, dx2, dy2)

    n_pass = 0
    for i, (name, lp, op) in enumerate(zip(
        ["+X", "-X", "+Y", "-Y", "+Z", "-Z"], legacy_planes, our_planes
    )):
        assert np.allclose(lp, op, atol=1e-10), \
            f"{name} plane mismatch:\n  legacy: {lp}\n  ours:   {op}"
        n_pass += 1

    print(f"  legacy_plane_comparison: {n_pass}/6 passed")
    return n_pass


if __name__ == "__main__":
    total = 0
    print("=== Trapezoid Intersection Validation ===\n")

    total += test_axis_aligned_hits()
    total += test_tilted_face_normals()
    total += test_box_degenerate()
    total += test_miss()
    total += test_inside_ray()
    total += test_signed_distance()
    total += test_vertex_verification()
    total += test_legacy_plane_comparison()

    print(f"\nTotal: {total}/33 passed")
    if total == 33:
        print("ALL TESTS PASSED")
        sys.exit(0)
    else:
        print("SOME TESTS FAILED")
        sys.exit(1)
