/**
SGLM_set_frame_test.cc
=======================

1. load sfr from $SFR_FOLD/sfr.npy
2. instanciate SGLM
3. set the frame into SGLM 
4. write SGLM description to $SFR_FOLD/SGLM_set_frame_test.log

::

    SGLM_set_frame_test info_build_run_cat

**/

#include "SGLM.h"

int main(int argc, char** argv)
{
    sfr fr = sfr::Load("$SFR_FOLD") ;

    std::cout << "//SGLM_set_frame_test.main load sfr from SFR_FOLD " << std::endl ;

    SGLM* sglm = new SGLM  ; 

    sglm->addlog("CSGOptiX::init", "start");

    sglm->set_frame(fr) ; 

    sglm->addlog("CSGOptiX::render_snap", "from SGLM_set_frame_test.cc" );

    sglm->writeDesc("$SFR_FOLD", "SGLM_set_frame_test", ".log" );

    std::cout << "//SGLM_set_frame_test.main write frame description to SFR_FOLD/SGLM_set_frame_test.log" << std::endl ;

    return 0 ; 
} 
