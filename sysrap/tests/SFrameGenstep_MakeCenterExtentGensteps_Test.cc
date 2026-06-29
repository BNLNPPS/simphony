#include "OPTICKS_LOG.hh"

#include "sframe.h"

#include "SFrameGenstep.hh"
#include "NP.hh"

int main(int argc, char** argv)
{
    OPTICKS_LOG(argc, argv);

    float extent = 100.f ;
    sframe fr = sframe::MakeFromExtent<float>(extent);
    NP* gs = SFrameGenstep::MakeCenterExtentGenstep_FromFrame(fr);

    LOG(info) << " gs " << ( gs ? gs->sstr() : "-" ) ;
    gs->save("$FOLD/gs.npy");

    return 0 ;

}
