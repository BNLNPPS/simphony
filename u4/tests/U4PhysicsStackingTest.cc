/**
 * Regression test for preserving Geant4 optical photon-stacking settings.
 *
 * GPU-only transport disables Cerenkov and scintillation secondary stacking so
 * Geant4 computes photon counts without paying the CPU tracking cost. This
 * test configures both global optical parameters before U4 process creation and
 * verifies that `U4Physics::ConstructOp` does not override them.
 */

#include <cstdlib>
#include <iostream>

#include "G4Box.hh"
#include "G4Cerenkov.hh"
#include "G4LogicalVolume.hh"
#include "G4Material.hh"
#include "G4NistManager.hh"
#include "G4OpticalParameters.hh"
#include "G4PVPlacement.hh"
#include "G4RunManager.hh"
#include "G4Scintillation.hh"
#include "G4SystemOfUnits.hh"
#include "G4ThreeVector.hh"
#include "G4VUserDetectorConstruction.hh"

#include "OPTICKS_LOG.hh"
#include "U4Physics.hh"

namespace
{
/**
 * Supplies the minimum geometry required for run-manager initialization.
 */
class TestDetector final : public G4VUserDetectorConstruction
{
  public:
    G4VPhysicalVolume* Construct() override
    {
        G4Material* vacuum =
            G4NistManager::Instance()->FindOrBuildMaterial("G4_Galactic");
        G4Box*           solid = new G4Box("World", 1. * m, 1. * m, 1. * m);
        G4LogicalVolume* logical = new G4LogicalVolume(solid, vacuum, "World");
        return new G4PVPlacement(
            nullptr, G4ThreeVector(), logical, "World", nullptr, false, 0);
    }
};

/**
 * Terminates the test executable with a diagnostic when a requirement fails.
 *
 * @param condition requirement result
 * @param message diagnostic printed when `condition` is false
 */
void Require(bool condition, const char* message)
{
    if (condition)
        return;
    std::cerr << "U4PhysicsStackingTest FAILED: " << message << std::endl;
    std::exit(EXIT_FAILURE);
}
} // namespace

int main(int argc, char** argv)
{
    OPTICKS_LOG(argc, argv);

    G4OpticalParameters* optical = G4OpticalParameters::Instance();
    optical->SetDefaults();
    optical->SetCerenkovStackPhotons(false);
    optical->SetScintStackPhotons(false);

    G4RunManager runManager;
    U4Physics*   physics = new U4Physics;
    runManager.SetUserInitialization(new TestDetector);
    runManager.SetUserInitialization(physics);
    runManager.Initialize();

    Require(physics->fCerenkov != nullptr, "Cerenkov process was not created");
    Require(physics->fScintillation != nullptr, "scintillation process was not created");
    Require(!physics->fCerenkov->GetStackPhotons(),
            "Cerenkov stacking setting was overridden");
    Require(!physics->fScintillation->GetStackPhotons(),
            "scintillation stacking setting was overridden");
    return 0;
}
