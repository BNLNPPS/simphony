/**
U4SimulateTest.cc  (HMM: maybe rename U4AppTest.cc)
====================================================

All the Geant4 setup happens in U4App::Create from U4App.h

**/

#include "U4App.h"    
#include "OPTICKS_LOG.hh"

int main(int argc, char** argv)
{
    OPTICKS_LOG(argc, argv); 

    U4App::Main(); 

    return 0 ; 
}
