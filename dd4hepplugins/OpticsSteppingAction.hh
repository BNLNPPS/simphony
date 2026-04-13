#pragma once

#include <DDG4/Geant4Action.h>
#include <DDG4/Geant4SteppingAction.h>
#include <string>

namespace ddeicopticks
{
//---------------------------------------------------------------------------//
/*!
 * DDG4 stepping action that intercepts standard Geant4 Cerenkov and
 * Scintillation processes and collects gensteps for GPU simulation.
 *
 * This follows the same approach as eic-opticks GPUCerenkov example:
 * use standard G4Cerenkov / G4Scintillation, then read back the photon
 * count via GetNumPhotons() and pack gensteps for the GPU.
 */
class OpticsSteppingAction final : public dd4hep::sim::Geant4SteppingAction
{
  public:
    OpticsSteppingAction(dd4hep::sim::Geant4Context *ctxt, std::string const &name);

    void operator()(const G4Step *step, G4SteppingManager *mgr) final;

  protected:
    DDG4_DEFINE_ACTION_CONSTRUCTORS(OpticsSteppingAction);
    ~OpticsSteppingAction() final;

  private:
    int verbose_{0};
};

//---------------------------------------------------------------------------//
} // namespace ddeicopticks
