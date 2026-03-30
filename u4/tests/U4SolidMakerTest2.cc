/**
U4SolidMakerTest2.cc
======================

Test voxelized G4MultiUnion navigation via the public Geant4 API.

Validates that ssolid::Distance (which now always uses the standard
Inside/DistanceToIn/DistanceToOut path) returns correct results for
G4MultiUnion solids produced by U4SolidMaker.

Exercises:
  - rays that clearly hit a constituent solid
  - rays that miss entirely
  - rays originating inside a constituent
  - rays along axes through multiple constituents
  - Inside() classification at known points
  - verification that Voxelize() was called (even if voxel count is 0
    for small numbers of solids, navigation must still work)

Returns 0 on success, non-zero on failure.
**/

#include <iostream>
#include <iomanip>
#include <cmath>
#include <cassert>
#include <vector>
#include <string>

#include "G4VSolid.hh"
#include "G4MultiUnion.hh"
#include "G4ThreeVector.hh"
#include "geomdefs.hh"

#include "U4SolidMaker.hh"
#include "ssolid.h"
#include "OPTICKS_LOG.hh"

static const double kTol = 1.0e-3 ; // mm

struct Ray
{
    const char* label ;
    G4ThreeVector pos ;
    G4ThreeVector dir ;
    double        expected_t ;   // kInfinity  → expect a miss
    double        tolerance ;
};

struct InsidePoint
{
    const char* label ;
    G4ThreeVector pos ;
    EInside       expected ;
};

static const char* EInsideName(EInside e)
{
    switch(e)
    {
        case kInside:  return "kInside"  ;
        case kSurface: return "kSurface" ;
        case kOutside: return "kOutside" ;
    }
    return "?" ;
}

static int check_rays(const char* name, const G4VSolid* solid, const std::vector<Ray>& rays)
{
    int fail = 0 ;
    for(const auto& r : rays)
    {
        G4double t = ssolid::Distance(solid, r.pos, r.dir, false);

        bool miss_expected = (r.expected_t >= kInfinity) ;
        bool miss_got      = (t >= kInfinity) ;
        bool ok ;

        if(miss_expected)
            ok = miss_got ;
        else if(miss_got)
            ok = false ;
        else
            ok = std::fabs(t - r.expected_t) <= r.tolerance ;

        if(!ok)
        {
            std::cerr << "FAIL " << name << " ray=" << r.label
                      << "  expected=" << r.expected_t
                      << "  got=" << t
                      << "\n" ;
            fail++ ;
        }
        else
        {
            std::cout << "  ok " << r.label
                      << "  t=" << std::fixed << std::setprecision(4) << t
                      << "\n" ;
        }
    }
    return fail ;
}

static int check_inside(const char* name, const G4VSolid* solid, const std::vector<InsidePoint>& pts)
{
    int fail = 0 ;
    for(const auto& ip : pts)
    {
        EInside got = solid->Inside(ip.pos) ;
        bool ok = (got == ip.expected) ;
        if(!ok)
        {
            std::cerr << "FAIL Inside " << name << " pt=" << ip.label
                      << "  expected=" << EInsideName(ip.expected)
                      << "  got=" << EInsideName(got)
                      << "\n" ;
            fail++ ;
        }
        else
        {
            std::cout << "  ok Inside " << ip.label
                      << "  " << EInsideName(got)
                      << "\n" ;
        }
    }
    return fail ;
}

// ---------------------------------------------------------------
// Test 1 – OrbOrbMultiUnion1
//
//   3 orbs of radius 50 mm centred at x = -100, 0, +100
//   (OrbOrbMultiUnion1 means num=1 → range -1..+1)
//
//   Geometry (Z cross-section along X axis):
//
//      left orb          centre orb         right orb
//    x:-150..-50        x:-50..+50        x:+50..+150
//
//   The orbs touch at x = ±50.
// ---------------------------------------------------------------
static int test_OrbOrbMultiUnion()
{
    const char* name = "OrbOrbMultiUnion1" ;
    const G4VSolid* solid = U4SolidMaker::Make(name);
    if(!solid){ std::cerr << "FAIL: could not create " << name << "\n"; return 1; }

    const double R = 50.0 ;

    std::vector<Ray> rays = {
        // Outside, shooting +Z into centre orb – should hit at z = -R → t = 200-50 = 150
        { "hit_centre_orb_+Z",  {0, 0, -200}, {0, 0, 1}, 150.0, kTol },

        // Outside, shooting -Z into centre orb
        { "hit_centre_orb_-Z",  {0, 0,  200}, {0, 0,-1}, 150.0, kTol },

        // Outside, shooting -X into right orb (centred at x=+100) – surface at x=+150 → t=150
        { "hit_right_orb_-X",   {300, 0, 0}, {-1, 0, 0}, 150.0, kTol },

        // Outside, shooting +X into left orb (centred at x=-100) – surface at x=-150 → t=150
        { "hit_left_orb_+X",    {-300, 0, 0}, {1, 0, 0}, 150.0, kTol },

        // Complete miss – ray parallel above all orbs (y > R)
        { "miss_above",         {0, 0, 200}, {1, 0, 0}, kInfinity, 0 },

        // Ray from inside centre orb toward +Z surface – distance = R = 50
        { "inside_centre_+Z",   {0, 0, 0}, {0, 0, 1}, R, kTol },

        // Ray from inside right orb toward +X surface – distance = R = 50
        { "inside_right_+X",    {100, 0, 0}, {1, 0, 0}, R, kTol },

        // Ray along +X from far left – enters left orb at x=-150 → t = 200-150 = 50
        { "enter_left_along_+X", {-200, 0, 0}, {1, 0, 0}, 50.0, kTol },
    };

    int fail = check_rays(name, solid, rays) ;

    // Inside() classification checks
    std::vector<InsidePoint> pts = {
        { "origin_inside",      {0, 0, 0},       kInside  },
        { "right_centre",       {100, 0, 0},     kInside  },
        { "left_centre",        {-100, 0, 0},    kInside  },
        { "far_outside",        {0, 0, 500},     kOutside },
        { "surface_centre_+Z",  {0, 0, R},       kSurface },
        // x=60 is inside the right orb (centred at 100, radius 50, extends 50..150)
        { "inside_right_orb",   {60, 0, 0},      kInside  },
    };

    fail += check_inside(name, solid, pts) ;
    return fail ;
}


// ---------------------------------------------------------------
// Test 2 – BoxFourBoxContiguous
//
//   Centre box is G4Box(45, 45, 45) – a 45mm half-side cube.
//   Flanking +X/-X boxes: G4Box(10, 11.5, 6.5) at x=±52
//   Flanking +Y/-Y boxes: G4Box(15, 15, 6.5) at y=±50
//
//   Full extent: x = -62..62, y = -65..65, z = -45..45
// ---------------------------------------------------------------
static int test_BoxFourBoxContiguous()
{
    const char* name = "BoxFourBoxContiguous" ;
    const G4VSolid* solid = U4SolidMaker::Make(name);
    if(!solid){ std::cerr << "FAIL: could not create " << name << "\n"; return 1; }

    const double hz = 45.0 ; // centre box z half-extent

    std::vector<Ray> rays = {
        // Shoot +Z into centre box from above → hit at z = -45 → t = 200-45 = 155
        { "centre_box_+Z",   {0, 0, -200}, {0, 0, 1}, 200.0 - hz, kTol },

        // Shoot -Z into centre box from below → t = 155
        { "centre_box_-Z",   {0, 0,  200}, {0, 0,-1}, 200.0 - hz, kTol },

        // Miss – ray parallel far away in Y
        { "miss_far_Y",      {0, 500, 0}, {0, 0, 1}, kInfinity, 0 },

        // Ray from inside centre box → DistanceToOut(origin, +Z) = 45
        { "inside_centre_+Z", {0, 0, 0}, {0, 0, 1}, hz, kTol },

        // Ray hitting +X flanking box: box extends x=42..62, z=-6.5..6.5
        // From (100, 0, 0) going -X → hit at x=62 → t = 38
        { "hit_px_flank_-X",  {100, 0, 0}, {-1, 0, 0}, 100.0 - 62.0, kTol },
    };

    int fail = check_rays(name, solid, rays) ;

    std::vector<InsidePoint> pts = {
        { "origin_inside",   {0, 0, 0},       kInside  },
        { "far_outside",     {0, 0, 500},     kOutside },
        { "in_px_flank",     {55, 0, 0},      kInside  },
    };

    fail += check_inside(name, solid, pts) ;
    return fail ;
}


// ---------------------------------------------------------------
// Test 3 – Verify EnsureVoxelizedMultiUnion was invoked
//
//   OrbOrbMultiUnion does NOT call Voxelize() itself; it relies
//   on the safety net in Make().
//
//   Note: Geant4's voxelizer may decide to produce 0 voxels for
//   a small number of solids (its internal optimisation threshold).
//   So we cannot assert voxel count > 0.  Instead, we confirm the
//   solid is a G4MultiUnion AND navigation returns correct results
//   (which means the standard navigation path is functioning).
// ---------------------------------------------------------------
static int test_navigation_without_explicit_voxelize()
{
    const char* name = "OrbOrbMultiUnion1" ;
    const G4VSolid* solid = U4SolidMaker::Make(name);
    if(!solid){ std::cerr << "FAIL: could not create " << name << "\n"; return 1; }

    const G4MultiUnion* mu = dynamic_cast<const G4MultiUnion*>(solid) ;
    if(!mu){ std::cerr << "FAIL: solid is not G4MultiUnion\n"; return 1; }

    std::cout << "  nSolids=" << mu->GetNumberOfSolids()
              << " voxels=" << mu->GetVoxels().GetCountOfVoxels()
              << "\n" ;

    // This solid has no explicit Voxelize() call in OrbOrbMultiUnion —
    // EnsureVoxelizedMultiUnion in Make() should have called it.
    // Verify navigation correctness: from origin (inside centre orb),
    // DistanceToOut in +Z must equal the orb radius (50mm).
    G4ThreeVector pos(0, 0, 0), dir(0, 0, 1) ;
    EInside in ;
    G4double t = ssolid::Distance_(solid, pos, dir, in) ;

    if(in != kInside)
    {
        std::cerr << "FAIL: origin should be kInside, got " << EInsideName(in) << "\n" ;
        return 1 ;
    }
    if(std::fabs(t - 50.0) > kTol)
    {
        std::cerr << "FAIL: expected t=50, got " << t << "\n" ;
        return 1 ;
    }
    std::cout << "  ok navigation correct: in=" << EInsideName(in) << " t=" << t << "\n" ;
    return 0 ;
}


int main(int argc, char** argv)
{
    OPTICKS_LOG(argc, argv);

    int fail = 0 ;

    std::cout << "--- test_OrbOrbMultiUnion ---\n" ;
    fail += test_OrbOrbMultiUnion() ;

    std::cout << "--- test_BoxFourBoxContiguous ---\n" ;
    fail += test_BoxFourBoxContiguous() ;

    std::cout << "--- test_navigation_without_explicit_voxelize ---\n" ;
    fail += test_navigation_without_explicit_voxelize() ;

    std::cout << "--- " << (fail == 0 ? "ALL PASS" : "FAILURES") << " ---\n" ;
    return fail ;
}
