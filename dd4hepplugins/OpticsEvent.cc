#include "OpticsEvent.hh"

#include <DD4hep/InstanceCount.h>
#include <DDG4/Factories.h>
#include <DDG4/Geant4Data.h>
#include <DDG4/Geant4HitCollection.h>
#include <DDG4/Geant4SensDetAction.h>
#include <DDG4/Geant4Context.h>

#include <G4Event.hh>

#include <chrono>

#include <G4CXOpticks.hh>
#include <SEvt.hh>
#include <SComp.h>
#include <sphoton.h>
#include <NP.hh>
#include <NPFold.h>

namespace ddeicopticks
{
//---------------------------------------------------------------------------//
OpticsEvent::OpticsEvent(dd4hep::sim::Geant4Context* ctxt,
                         std::string const& name)
    : dd4hep::sim::Geant4EventAction(ctxt, name)
{
    dd4hep::InstanceCount::increment(this);
    declareProperty("Verbose", verbose_);
    declareProperty("PhotonThreshold", photon_threshold_);
}

//---------------------------------------------------------------------------//
OpticsEvent::~OpticsEvent()
{
    dd4hep::InstanceCount::decrement(this);
}

//---------------------------------------------------------------------------//
void OpticsEvent::begin(G4Event const* event)
{
    int eventID = event->GetEventID();

    if (verbose_ > 0)
        info("OpticsEvent::begin -- event #%d", eventID);

    // In batch mode, only start a new SEvt at the beginning of each batch.
    // Skipping beginOfEvent between events lets gensteps accumulate
    // (SEvt::clear_output preserves gensteps by design).
    if (!batch_begun_)
    {
        SEvt::CreateOrReuse_EGPU();
        SEvt* sev = SEvt::Get_EGPU();
        if (sev)
            sev->beginOfEvent(eventID);
        batch_begun_ = true;
    }
}

//---------------------------------------------------------------------------//
void OpticsEvent::end(G4Event const* event)
{
    int eventID = event->GetEventID();

    G4CXOpticks* gx = G4CXOpticks::Get();
    if (!gx)
    {
        error("OpticsEvent::end -- G4CXOpticks not initialized");
        return;
    }

    SEvt* sev = SEvt::Get_EGPU();
    if (!sev)
    {
        error("OpticsEvent::end -- no EGPU SEvt instance");
        return;
    }

    int64_t num_genstep = sev->getNumGenstepFromGenstep();
    int64_t num_photon = sev->getNumPhotonFromGenstep();

    if (verbose_ > 0 || num_genstep > 0)
    {
        info("Event #%d: %lld gensteps, %lld photons accumulated",
             eventID,
             static_cast<long long>(num_genstep),
             static_cast<long long>(num_photon));
    }

    // Batch mode: keep accumulating until threshold reached
    if (photon_threshold_ > 0 && num_photon < photon_threshold_)
        return;

    if (num_genstep > 0)
    {
        auto sim_t0 = std::chrono::high_resolution_clock::now();
        gx->simulate(eventID, /*reset=*/false);
        auto sim_t1 = std::chrono::high_resolution_clock::now();
        double simulate_ms =
            std::chrono::duration<double, std::milli>(sim_t1 - sim_t0).count();

        unsigned num_hit = sev->getNumHit();
        info("OPTICKS_GPU_TIME event=%d ms=%.3f photons=%lld hits=%u",
             eventID, simulate_ms,
             static_cast<long long>(num_photon), num_hit);

        // Inject hits only in per-event mode; batch mode cannot map
        // hits back to individual events.
        if (photon_threshold_ == 0 && num_hit > 0)
            injectHits(event, sev, num_hit);

        sev->endOfEvent(eventID);
        gx->reset(eventID);
    }
    else
    {
        if (verbose_ > 0)
            info("Event #%d: no gensteps, skipping GPU simulation", eventID);
        if (photon_threshold_ == 0)
            sev->endOfEvent(eventID);
    }

    batch_begun_ = false;
}

//---------------------------------------------------------------------------//
void OpticsEvent::injectHits(G4Event const* event,
                             SEvt* sev,
                             unsigned num_hit)
{
    using dd4hep::sim::Geant4SensDetSequences;
    using dd4hep::sim::Geant4HitCollection;
    using dd4hep::sim::Geant4Tracker;

    int eventID = event->GetEventID();

    Geant4SensDetSequences& sens = context()->sensitiveActions();
    auto const& seqs = sens.sequences();

    if (seqs.empty())
    {
        warning("Event #%d: no sensitive detectors registered -- "
                "call setupTracker() in steering script", eventID);
        return;
    }

    for (auto const& [det_name, seq] : seqs)
    {
        Geant4HitCollection* coll = seq->collection(0);
        if (!coll)
            continue;

        for (unsigned i = 0; i < num_hit; i++)
        {
            sphoton ph;
            sev->getHit(ph, i);
            coll->add(createTrackerHit(ph));
        }

        info("Event #%d: injected %u hits into '%s' collection",
             eventID, num_hit, det_name.c_str());
    }
}

//---------------------------------------------------------------------------//
dd4hep::sim::Geant4Tracker::Hit*
OpticsEvent::createTrackerHit(sphoton const& ph)
{
    using dd4hep::sim::Geant4Tracker;

    auto* hit = new Geant4Tracker::Hit();

    hit->position = {ph.pos.x, ph.pos.y, ph.pos.z};
    hit->momentum = {ph.mom.x, ph.mom.y, ph.mom.z};
    hit->length = ph.wavelength;
    hit->energyDeposit = 0;
    hit->cellID = ph.identity;

    hit->truth.trackID = ph.index;
    hit->truth.pdgID = 0;
    hit->truth.deposit = 0;
    hit->truth.time = ph.time;
    hit->truth.length = ph.wavelength;
    hit->truth.x = ph.pos.x;
    hit->truth.y = ph.pos.y;
    hit->truth.z = ph.pos.z;
    hit->truth.px = ph.mom.x;
    hit->truth.py = ph.mom.y;
    hit->truth.pz = ph.mom.z;

    return hit;
}

//---------------------------------------------------------------------------//
}  // namespace ddeicopticks

DECLARE_GEANT4ACTION_NS(ddeicopticks, OpticsEvent)
