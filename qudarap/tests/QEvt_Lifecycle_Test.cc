#include <csignal>
#include "OPTICKS_LOG.hh"
#include "ssys.h"
#include "SEventConfig.hh"
#include "SEvt.hh"
#include "QEvt.hh"

struct QEvt_Lifecycle_Test
{
    static int EventLoop();
};

/**
QEvt_Lifecycle_Test::EventLoop
-------------------------------------

Comments are from an input photon centric viewpoint
as those are useful for debugging.

**/

int QEvt_Lifecycle_Test::EventLoop()
{
    SEvt* sev = SEvt::Create_EGPU() ;
    // instanciation may load input_photons if configured
    assert( sev );

    sev->setFramePlaceholder();
    // calls SEvt::setFrame which
    // for non-placeholder frame might transform the input photons
    // using the frame transform



    QEvt* event = new QEvt ; // grabs SEvt::EGPU

    int num_event = SEventConfig::NumEvent() ;
    std::cout << " num_event " << num_event << std::endl ;

    for(int i = 0 ; i < num_event ; i++)
    {
        //std::cout << i << std::endl ;
        // follow pattern of QSim::simulate

        int eventID = i ;

        sev->beginOfEvent(eventID);
        // SEvt::beginOfEvent calls SEvt::setFrameGenstep which creates
        // the input photon genstep and calls SEvt::addGenstep

        NP* igs = sev->makeGenstepArrayFromVector();
        int rc = event->setGenstepUpload_NP(igs);
        assert( rc == 0 );
        if(rc!=0) std::raise(SIGINT);

        // IN REALITY THE LAUNCH WOULD BE HERE
        // propagating the photons, changing GPU side buffers


        sev->endOfEvent(eventID);
    }
    return 0 ;
}

int main(int argc, char** argv)
{
    OPTICKS_LOG(argc, argv);

    const char* input_photon = argc > 1 ? argv[1] : "RainXZ_Z230_10k_f8.npy";
    SEventConfig::SetInputPhoton(input_photon);
    SEventConfig::SetMaxSlot(1000000); // cap slots: lifecycle test only needs the 10k input photons, not VRAM-scale

    return QEvt_Lifecycle_Test::EventLoop() ;
}
