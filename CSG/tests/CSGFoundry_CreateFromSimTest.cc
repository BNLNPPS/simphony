/**
CSGFoundry_CreateFromSimTest.cc
================================

Creates CSGFoundry from SSim and SSim/stree

1. SSim::Load
2. populates CSGFoundry with CSGFoundry::CreateFromSim using
   the SSim that CSGFoundry instanciation adopts
3. saves CSGFoundry to $FOLD

**/

#include "CSGFoundry.h"
#include "OPTICKS_LOG.hh"
#include "SSim.hh"
#include "spath.h"
#include "ssys.h"
#include "stree.h"
#include <csignal>

int main(int argc, char** argv)
{
    OPTICKS_LOG(argc, argv);

    bool  gdml_mode = ssys::hasenv_("SIMPHONY_GEOM_FILE");
    SSim* sim = gdml_mode ? SSim::Get() : SSim::Load();
    std::cout << "sim.tree.desc" << std::endl << sim->tree->desc() ;

    CSGFoundry* fd = gdml_mode ? CSGFoundry::Get() : CSGFoundry::CreateFromSim(); // adopts SSim::INSTANCE
    if (!gdml_mode)
        fd->save("$FOLD");

    bool fd_expect = fd->sim == sim ;
    assert( fd_expect  );
    if(!fd_expect) std::raise(SIGINT);

    return 0 ;
}
