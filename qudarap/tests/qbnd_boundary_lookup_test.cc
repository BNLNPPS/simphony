/**
qbnd_boundary_lookup_test.cc
===============================

Regression test for qbnd::boundary_lookup with POINT texture filtering
and manual wavelength-axis interpolation.

Verifies:
  1. Row isolation — adjacent material/surface rows with distinct values
     return exact per-row data with no cross-row blending.
  2. Wavelength interpolation — lookup at a wavelength between two bins
     returns the correct linear interpolant.
  3. Boundary clamping — wavelengths outside the domain clamp correctly.

CPU-only: compiled with MOCK_TEXTURE so tex2D is software nearest-neighbor.
**/

#include <cmath>
#include <cstdio>
#include <cstring>

#ifndef MOCK_CUDA
#define MOCK_CUDA
#endif
#ifndef MOCK_TEXTURE
#define MOCK_TEXTURE
#endif

#include "NP.hh"
#include "s_mock_texture.h"
#include "scuda.h"
#include "squad.h"
#include "sstate.h"

MockTextureManager* MockTextureManager::INSTANCE = nullptr;

#include "qbnd.h"

static int g_fail = 0;

static void check(const char* tag, bool cond)
{
    printf("  %-60s %s\n", tag, cond ? "PASS" : "*** FAIL ***");
    if (!cond)
        g_fail++;
}

static bool feq(float a, float b, float tol = 1e-5f)
{
    return fabsf(a - b) <= tol * (1.f + fabsf(b));
}

int main()
{
    // Build a small boundary texture: 2 boundaries, 4 species, 2 groups, 4 wavelength bins.
    // Layout: shape (ni, nj, nk, nl, nm) = (2, 4, 2, 4, 4)
    //   ni=2  boundaries
    //   nj=4  OMAT/OSUR/ISUR/IMAT
    //   nk=2  property groups (k=0, k=1)
    //   nl=4  wavelength samples
    //   nm=4  float4 components
    const unsigned ni = 2, nj = 4, nk = 2, nl = 4, nm = 4;
    const unsigned ny = ni * nj * nk; // 16 rows
    const unsigned nx = nl;           // 4 wavelength bins

    NP*    bnd = NP::Make<float>(ni, nj, nk, nl, nm);
    float* vv = bnd->values<float>();
    memset(vv, 0, bnd->arr_bytes());

    // Fill each row with a distinct recognizable value.
    // Row iy gets float4(100*iy+0, 100*iy+1, 100*iy+2, 100*iy+3) at each wavelength bin,
    // EXCEPT we make the wavelength bins differ so we can test interpolation:
    //   bin ix gets an extra offset of ix*10 in the .x component.
    for (unsigned iy = 0; iy < ny; iy++)
    {
        for (unsigned ix = 0; ix < nx; ix++)
        {
            unsigned idx = (iy * nx + ix) * nm;
            vv[idx + 0] = float(100 * iy + ix * 10); // .x varies with wavelength bin
            vv[idx + 1] = float(100 * iy + 1);       // .y constant per row
            vv[idx + 2] = float(100 * iy + 2);       // .z constant per row
            vv[idx + 3] = float(100 * iy + 3);       // .w constant per row
        }
    }

    // Set domain metadata: wavelength 100..400 nm, step 100 nm (4 bins)
    float nm0 = 100.f;
    float nm_high = 400.f;
    float nm_step = 100.f;
    bnd->set_meta<float>("domain_low", nm0);
    bnd->set_meta<float>("domain_high", nm_high);
    bnd->set_meta<float>("domain_step", nm_step);
    bnd->set_meta<float>("domain_range", nm_high - nm0);

    // Register mock texture
    cudaTextureObject_t texObj = MockTextureManager::Add(bnd);

    // Set up metadata quad (normally uploaded by QTex)
    quad4 meta;
    memset(&meta, 0, sizeof(meta));
    meta.q0.u.x = nx;
    meta.q0.u.y = ny;
    meta.q1.f.x = nm0;
    meta.q1.f.z = nm_step;

    // Build qbnd struct
    qbnd qb;
    qb.boundary_tex = texObj;
    qb.boundary_meta = &meta;

    printf("qbnd_boundary_lookup_test\n");

    // --- Test 1: Row isolation ---
    // Look up boundary 0, OMAT (line=0), k=0 at bin-center wavelengths.
    // Row iy = _BOUNDARY_NUM_FLOAT4 * line + k = 2*0 + 0 = 0.
    // At wavelength = 100 nm (bin 0): expect .x = 100*0 + 0*10 = 0
    {
        float4 p = qb.boundary_lookup(100.f, 0, 0);
        check("row0 bin0: .x == 0", feq(p.x, 0.f));
        check("row0 bin0: .y == 1", feq(p.y, 1.f));
    }

    // Row iy = 2*0 + 1 = 1 (boundary 0, OMAT, k=1).
    // At wavelength = 100 nm: expect .x = 100*1 + 0*10 = 100
    {
        float4 p = qb.boundary_lookup(100.f, 0, 1);
        check("row1 bin0: .x == 100 (distinct from row0)", feq(p.x, 100.f));
        check("row1 bin0: .y == 101", feq(p.y, 101.f));
    }

    // Boundary 1, IMAT (line = 1*4+3 = 7), k=0 → row iy = 2*7+0 = 14.
    // At wavelength = 200 nm (bin 1): expect .x = 100*14 + 1*10 = 1410
    {
        float4 p = qb.boundary_lookup(200.f, 7, 0);
        check("row14 bin1: .x == 1410", feq(p.x, 1410.f));
        check("row14 bin1: .z == 1402", feq(p.z, 1402.f));
    }

    // --- Test 2: Wavelength interpolation ---
    // Look up row 0 at wavelength 150 nm (halfway between bin 0 and bin 1).
    // bin 0 .x = 0, bin 1 .x = 10. Expect lerp = 5.
    {
        float4 p = qb.boundary_lookup(150.f, 0, 0);
        check("row0 lerp 150nm: .x == 5 (midpoint)", feq(p.x, 5.f));
        check("row0 lerp 150nm: .y == 1 (constant)", feq(p.y, 1.f));
    }

    // 25% between bin 1 and bin 2: wavelength = 225 nm.
    // bin 1 .x = 10, bin 2 .x = 20. Expect lerp = 12.5.
    {
        float4 p = qb.boundary_lookup(225.f, 0, 0);
        check("row0 lerp 225nm: .x == 12.5", feq(p.x, 12.5f));
    }

    // --- Test 3: Boundary clamping ---
    // Wavelength below domain (50 nm) — should clamp to bin 0.
    {
        float4 p = qb.boundary_lookup(50.f, 0, 0);
        check("row0 clamp below: .x == 0 (bin 0)", feq(p.x, 0.f));
    }

    // Wavelength above domain (500 nm) — should clamp to last bin.
    {
        float4 p = qb.boundary_lookup(500.f, 0, 0);
        check("row0 clamp above: .x == 30 (bin 3)", feq(p.x, 30.f));
    }

    printf("qbnd_boundary_lookup_test: %s (%d failure%s)\n",
           g_fail == 0 ? "PASS" : "FAIL", g_fail, g_fail == 1 ? "" : "s");
    return g_fail == 0 ? 0 : 1;
}
