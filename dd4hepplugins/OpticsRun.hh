#pragma once

#include <DDG4/Geant4Action.h>
#include <DDG4/Geant4RunAction.h>
#include <string>

namespace ddeicopticks
{
//---------------------------------------------------------------------------//
/*!
 * DDG4 action plugin for eic-opticks run-level lifecycle.
 *
 * At begin-of-run: initializes G4CXOpticks geometry from the Geant4 world
 * volume, translating the geometry to OptiX acceleration structures on GPU.
 *
 * At end-of-run: finalizes G4CXOpticks, releasing GPU resources.
 *
 * Properties:
 *   - SaveGeometry (default: false) -- save Opticks geometry to disk
 */
class OpticsRun final : public dd4hep::sim::Geant4RunAction
{
  public:
    OpticsRun(dd4hep::sim::Geant4Context *ctxt, std::string const &name);

    void begin(G4Run const *run) final;
    void end(G4Run const *run) final;

  protected:
    DDG4_DEFINE_ACTION_CONSTRUCTORS(OpticsRun);
    ~OpticsRun() final;

  private:
    bool save_geometry_{false};
};

//---------------------------------------------------------------------------//
} // namespace ddeicopticks
