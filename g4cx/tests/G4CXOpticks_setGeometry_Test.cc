/**
G4CXOpticks_setGeometry_Test.cc
=================================

Action depends on envvars such as OpticksGDMLPath, see G4CXOpticks::setGeometry

**/

#include "OPTICKS_LOG.hh"
#include "G4CXOpticks.hh"

int main(int argc, char** argv)
{
    OPTICKS_LOG(argc, argv);

    const char* gdmlpath = argc > 1 ? argv[1] : nullptr;
    LOG(info) << "[SetGeometry" ;
    if (gdmlpath)
        G4CXOpticks::SetGeometry(gdmlpath);
    else
        G4CXOpticks::SetGeometry();
    LOG(info) << "]SetGeometry" ;

    return 0 ;
}
