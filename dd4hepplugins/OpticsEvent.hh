#pragma once

#include <DDG4/Geant4Action.h>
#include <DDG4/Geant4Data.h>
#include <DDG4/Geant4EventAction.h>
#include <string>

class SEvt;
struct sphoton;

namespace ddeicopticks
{
//---------------------------------------------------------------------------//
/*!
 * DDG4 action plugin for simphony event-level GPU simulation.
 *
 * At begin-of-event: prepares GPU event buffer (SEvt).
 * At end-of-event: triggers GPU optical photon simulation via
 * G4CXOpticks::simulate(), retrieves hits, injects them into DD4hep
 * hit collections, and resets for next event.
 *
 * Requires setupTracker() to be called in the steering script so that
 * DD4hep creates hit collections for the sensitive detectors.
 *
 * Properties:
 *   - Verbose (default: 0) -- verbosity level
 */
class OpticsEvent final : public dd4hep::sim::Geant4EventAction
{
  public:
    OpticsEvent(dd4hep::sim::Geant4Context *ctxt, std::string const &name);

    void begin(G4Event const *event) final;
    void end(G4Event const *event) final;

  protected:
    DDG4_DEFINE_ACTION_CONSTRUCTORS(OpticsEvent);
    ~OpticsEvent() final;

  private:
    void injectHits(G4Event const *event, SEvt *sev, unsigned num_hit);
    static dd4hep::sim::Geant4Tracker::Hit *createTrackerHit(sphoton const &ph);

    int verbose_{0};
    int64_t photon_threshold_{0}; ///< 0 = simulate per-event, >0 = batch until N photons
    bool batch_begun_{false};
};

//---------------------------------------------------------------------------//
} // namespace ddeicopticks
