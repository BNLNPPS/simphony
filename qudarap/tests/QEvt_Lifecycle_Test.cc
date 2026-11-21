#include <csignal>
#include <memory>
#include <stdexcept>
#include <string_view>

#include <cuda_runtime.h>

#include "NP.hh"
#include "OPTICKS_LOG.hh"
#include "QEvt.hh"
#include "QSim.hh"
#include "SEvent.hh"
#include "SEventConfig.hh"
#include "SEvt.hh"
#include "SGenstep.h"
#include "ssys.h"

struct QEvt_Lifecycle_Test
{
    static int EventLoop();
};

static int CheckSlotCapacity()
{
    SEventConfig::SetMaxSlot(1000);
    SEventConfig::SetMaxGenstep(2);
    SEventConfig::SetNothing();
    SEvt* sev = SEvt::Create_EGPU();
    QSim* sim = QSim::Create();
    QEvt* event = sim->qev;
    SEventConfig::SetDeviceMaxSlot(2000);

    const std::unique_ptr<NP> oversized(SEvent::MakeCountGenstep({1500}, nullptr));
    sev->addGenstep(oversized.get());
    try
    {
        sim->simulate(1, false);
        throw std::runtime_error("Simulation accepted a 1500-photon genstep into a 1000-slot event");
    }
    catch (const std::length_error&)
    {
        if (sev->haveGenstepVec() || event->upload_count != 0)
            throw std::runtime_error("Rejected simulation retained gensteps or uploaded data");
    }

    const std::unique_ptr<NP> fitting(SEvent::MakeCountGenstep({1000, 500}, nullptr));
    sev->addGenstep(fitting.get());
    sim->simulate(2, false);
    if (event->upload_count != 2 || event->getNumPhoton() != 500)
        throw std::runtime_error("Simulation failed to use two launches at the captured capacity");

    const NP* accepted = event->getGenstep();
    try
    {
        event->setGenstepUpload_NP(fitting.get());
        throw std::runtime_error("Upload accepted 1500 photons into a 1000-slot event");
    }
    catch (const std::length_error&)
    {
        if (event->getGenstep() != accepted || event->get_photon_slot_offset() != 1000)
            throw std::runtime_error("Rejected upload replaced the accepted gensteps or slice offset");
        const std::unique_ptr<NP> uploaded(event->gatherGenstepFromDevice());
        if (uploaded->shape[0] != 1 || SGenstep::GetNumPhoton(uploaded.get(), 0) != 500)
            throw std::runtime_error("Rejected upload changed the device gensteps");
    }
    const sslice empty{1, 1, 1000, 0};
    if (event->setGenstepUpload_NP(fitting.get(), &empty) == 0 || event->upload_count != 2)
        throw std::runtime_error("An empty slice changed the accepted upload");
    event->clear();
    return 0;
}

static int CheckSimtraceRetry()
{
    SEventConfig::SetMaxSlot(10);
    SEventConfig::SetMaxGenstep(1);
    SEventConfig::SetRGModeSimtrace();
    SEvt* event = SEvt::Create_EGPU();
    QSim* sim = QSim::Create();

    const std::unique_ptr<NP> oversized(NP::Make<float>(12, 4, 4));
    event->setInputPhoton(oversized.get());
    try
    {
        sim->simtrace(1);
        throw std::runtime_error("Simtrace accepted 12 input photons into a 10-slot event");
    }
    catch (const std::length_error&)
    {
    }

    for (int count : {2, 3})
    {
        const std::unique_ptr<NP> photons(NP::Make<float>(count, 4, 4));
        event->setInputPhoton(photons.get());
        sim->simtrace(count);
        if (sim->qev->getNumSimtrace() != static_cast<size_t>(count) || sim->qev->getGenstep() != nullptr)
            throw std::runtime_error("Simtrace failed to process and clear the replacement input");
    }
    return 0;
}

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
        event->clear();
    }
    return 0 ;
}

int main(int argc, char** argv)
{
    OPTICKS_LOG(argc, argv);

    if (argc > 1 && std::string_view(argv[1]) == "--slot-capacity")
        return CheckSlotCapacity();
    if (argc > 1 && std::string_view(argv[1]) == "--simtrace-retry")
        return CheckSimtraceRetry();

    const char* input_photon = argc > 1 ? argv[1] : "RainXZ_Z230_10k_f8.npy";
    SEventConfig::SetInputPhoton(input_photon);
    SEventConfig::SetMaxSlot(1000000); // cap slots: lifecycle test only needs the 10k input photons, not VRAM-scale

    return QEvt_Lifecycle_Test::EventLoop() ;
}
