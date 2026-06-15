// ~/o/CSG/tests/CSGScanTest.sh

#include "OPTICKS_LOG.hh"


#include "scuda.h"
#include "SSim.hh"
#include "ssys.h"
#include "spath.h"

#include "CSGFoundry.h"
#include "CSGMaker.h"
#include "CSGScan.h"
#include "CSGSolid.h"
#include "G4CXOpticks.hh"

struct CSGScanTest
{
    const char* geom ;
    const char* scan ;
    bool            gdml_mode;
    CSGFoundry* fd ;
    const CSGSolid* so ;
    CSGScan*  sc ;

    CSGScanTest();
    void init();
    int intersect();
};

inline CSGScanTest::CSGScanTest() :
    geom(ssys::getenvvar("GEOM")),
    scan(ssys::getenvvar("SCAN", "axis,rectangle,circle")),
    gdml_mode(ssys::hasenv_("SIMPHONY_GEOM_FILE")),
    fd(nullptr),
    so(nullptr),
    sc(nullptr)
{
    init();
};

inline void CSGScanTest::init()
{
    if (gdml_mode)
    {
        G4CXOpticks::SetNoGPU(true);
        G4CXOpticks::SetGeometry(ssys::getenvvar("SIMPHONY_GEOM_FILE"));
        fd = CSGFoundry::Get();
    }
    else if (CSGMaker::CanMake(geom))
    {
        SSim::Create();
        fd = CSGMaker::MakeGeom(geom);
        if(ssys::getenvbool("CSGScanTest__init_SAVEFOLD"))
        {
            fd->save("$CSGScanTest__init_SAVEFOLD");
        }
    }
    else
    {
        SSim::Create();
        fd = CSGFoundry::Load();
    }
    if (!gdml_mode)
        fd->upload();
    so = fd->getSolid(0);
    // TODO: makes more sense to pick a CSGPrim (or root CSGNode) not a solid

    sc = new CSGScan(fd, so, scan, !gdml_mode);
}

inline int CSGScanTest::intersect()
{
    sc->intersect_h();
    sc->intersect_d();
    std::cout << sc->brief() ;
    sc->save("$FOLD");

    // TODO: compare intersects to define rc
    return 0 ;
}


int main(int argc, char** argv)
{
    OPTICKS_LOG(argc, argv);
    CSGScanTest t ;
    return t.intersect();
}
