/**
intersect_leaf_phi_wedge_test.cc
==================================

Self-checking regression test for the phi-wedge clip baked into the ZSphere
(csg_intersect_leaf_zsphere.h) and Cylinder (csg_intersect_leaf_cylinder.h)
leaf primitives.

Unlike intersect_leaf_phicut_test.cc (which exercises the standalone phicut
half-space node), this targets the per-primitive phi-wedge encoding:

    q0.f.x = startPhi (rad)   q0.f.y = deltaPhi (rad)   q0.f.w = radius
    q1.f.x = z1               q1.f.y = z2

It pins the behaviour the original CSG_INTERSECTION(prim, PhiCut) leak got
wrong: a ray whose NEAR surface candidate falls outside the wedge must still
find the in-wedge surface/wall further along the ray (rather than be lost),
while a ray that crosses the primitive entirely outside the wedge must MISS.

CPU-only: the leaf intersect headers are shared host/device code.
Exit code is non-zero if any assertion fails, so it is meaningful under ctest.
**/

#include <cmath>
#include <cstdio>

#include "scuda.h"
#include "sqat4.h"
#include "squad.h"

// required include order: csg_intersect_leaf_head.h defines LEAF_FUNC etc.
// used by csg_robust_quadratic_roots.h and the leaf intersect headers below
// clang-format off
#include "csg_intersect_leaf_head.h"
#include "csg_robust_quadratic_roots.h"
#include "csg_intersect_leaf_zsphere.h"
#include "csg_intersect_leaf_cylinder.h"
// clang-format on

static int g_fail = 0;

static void check(const char* tag, bool cond)
{
    printf("// %-58s %s\n", tag, cond ? "PASS" : "*** FAIL ***");
    if (!cond)
        g_fail += 1;
}

// relative-ish float compare, generous enough for fp32 geometry
static bool feq(float a, float b, float eps = 2e-2f)
{
    return fabsf(a - b) <= eps * (1.f + fabsf(b));
}

static quad q_phi(float startPhi, float deltaPhi, float radius)
{
    quad q;
    q.f = make_float4(startPhi, deltaPhi, 0.f, radius);
    return q;
}
static quad q_zrange(float z1, float z2)
{
    quad q;
    q.f = make_float4(z1, z2, 0.f, 0.f);
    return q;
}

static bool run_zsphere(const quad& q0, const quad& q1, float3 o, float3 d, float3& hit, float& t)
{
    float4 isect = make_float4(0.f, 0.f, 0.f, 0.f);
    bool   valid = false;
    intersect_leaf_zsphere(valid, isect, q0, q1, 0.f, o, d);
    t = isect.w;
    hit = make_float3(o.x + t * d.x, o.y + t * d.y, o.z + t * d.z);
    return valid;
}
static bool run_cylinder(const quad& q0, const quad& q1, float3 o, float3 d, float3& hit, float& t)
{
    float4 isect = make_float4(0.f, 0.f, 0.f, 0.f);
    bool   valid = false;
    intersect_leaf_cylinder(valid, isect, q0, q1, 0.f, o, d);
    t = isect.w;
    hit = make_float3(o.x + t * d.x, o.y + t * d.y, o.z + t * d.z);
    return valid;
}

// azimuth of a hit point, normalized into [startPhi, startPhi+2pi)
static bool phi_in_wedge(float3 h, float startPhi, float deltaPhi)
{
    float phi = atan2f(h.y, h.x);
    while (phi < startPhi)
        phi += 2.f * M_PIf;
    return (phi - startPhi) <= deltaPhi + 1e-4f;
}

void test_zsphere_phi()
{
    const float R = 100.f;
    const float HALF = 0.5f * M_PIf;  // deltaPhi = pi/2 wedge  [0, pi/2]
    const quad  q1 = q_zrange(-R, R); // full sphere in z
    const quad  qw = q_phi(0.f, HALF, R);
    float3      h;
    float       t;
    const float c = cosf(0.25f * M_PIf), s = sinf(0.25f * M_PIf);

    // (1) ray aimed at a surface point squarely inside the wedge (phi=pi/4) -> hit
    bool v1 = run_zsphere(qw, q1, make_float3(2 * R * c, 2 * R * s, 0.f), make_float3(-c, -s, 0.f), h, t);
    check("zsphere: in-wedge ray hits", v1);
    check("zsphere: in-wedge hit on surface inside wedge", v1 && feq(h.x, R * c) && feq(h.y, R * s) && phi_in_wedge(h, 0.f, HALF));

    // (2) LEAK regression: near surface candidate is OUTSIDE the wedge (phi~150deg),
    //     the far candidate (phi~30deg) is inside -> must be recovered, not lost.
    bool v2 = run_zsphere(qw, q1, make_float3(-2 * R, 0.5f * R, 0.f), make_float3(1.f, 0.f, 0.f), h, t);
    check("zsphere: leak-case recovers far in-wedge hit (was lost)", v2);
    check("zsphere: leak-case hit is the +x (far) surface, in wedge", v2 && h.x > 0.f && feq(h.y, 0.5f * R) && phi_in_wedge(h, 0.f, HALF));

    // (3) ray crossing the sphere entirely outside the wedge (y<0 side) -> MISS
    bool v3 = run_zsphere(qw, q1, make_float3(-2 * R, -0.5f * R, 0.f), make_float3(1.f, 0.f, 0.f), h, t);
    check("zsphere: ray fully outside wedge misses", !v3);

    // (4) control: same geometry as (2) but NO phi clip (deltaPhi=0) -> takes the NEAR (-x) surface
    bool v4 = run_zsphere(q_phi(0.f, 0.f, R), q1, make_float3(-2 * R, 0.5f * R, 0.f), make_float3(1.f, 0.f, 0.f), h, t);
    check("zsphere: no-clip control hits", v4);
    check("zsphere: no-clip control takes near (-x) surface", v4 && h.x < 0.f && feq(h.y, 0.5f * R));
}

void test_cylinder_phi()
{
    const float R = 100.f;
    const float HALF = 0.5f * M_PIf; // [0, pi/2]
    const quad  q1 = q_zrange(-R, R);
    const quad  qw = q_phi(0.f, HALF, R);
    float3      h;
    float       t;
    const float c = cosf(0.25f * M_PIf), s = sinf(0.25f * M_PIf);

    // (1) ray aimed at the curved surface inside the wedge (phi=pi/4) -> hit
    bool v1 = run_cylinder(qw, q1, make_float3(2 * R * c, 2 * R * s, 0.f), make_float3(-c, -s, 0.f), h, t);
    check("cylinder: in-wedge ray hits curved surface", v1);
    check("cylinder: in-wedge hit inside wedge & on radius", v1 && feq(h.x, R * c) && feq(h.y, R * s) && phi_in_wedge(h, 0.f, HALF));

    // (2) wedge-boundary regression: ray enters from -x at y=+50; the near curved
    //     candidate (phi~150deg) is clipped, so the first valid hit must be in-wedge
    //     (the phi=pi/2 wall at x=0, or the far curved surface) -- NOT a leak-through.
    bool v2 = run_cylinder(qw, q1, make_float3(-2 * R, 0.5f * R, 0.f), make_float3(1.f, 0.f, 0.f), h, t);
    check("cylinder: wedge-boundary ray yields an in-wedge hit (no leak)", v2);
    check("cylinder: wedge-boundary hit is inside the wedge", v2 && phi_in_wedge(h, 0.f, HALF) && h.y > 0.f);

    // (3) ray crossing entirely outside the wedge (y<0) -> MISS
    bool v3 = run_cylinder(qw, q1, make_float3(-2 * R, -0.5f * R, 0.f), make_float3(1.f, 0.f, 0.f), h, t);
    check("cylinder: ray fully outside wedge misses", !v3);

    // (4) control: NO phi clip -> near (-x) curved surface
    bool v4 = run_cylinder(q_phi(0.f, 0.f, R), q1, make_float3(-2 * R, 0.5f * R, 0.f), make_float3(1.f, 0.f, 0.f), h, t);
    check("cylinder: no-clip control hits near (-x) surface", v4 && h.x < 0.f && feq(h.y, 0.5f * R));
}

int main(int argc, char** argv)
{
    test_zsphere_phi();
    test_cylinder_phi();
    printf("// intersect_leaf_phi_wedge_test : %s (%d failure%s)\n", g_fail == 0 ? "PASS" : "FAIL", g_fail, g_fail == 1 ? "" : "s");
    return g_fail == 0 ? 0 : 1;
}
