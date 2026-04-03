#include "OpticsRun.hh"

#include <DD4hep/InstanceCount.h>
#include <DDG4/Factories.h>
#include <DDG4/Geant4Kernel.h>

#include <G4Run.hh>
#include <G4Material.hh>
#include <G4MaterialPropertiesTable.hh>

#include <G4CXOpticks.hh>
#include <SEvt.hh>
#include <SEventConfig.hh>

#include "DD4hepSensorIdentifier.hh"

namespace ddeicopticks
{

//---------------------------------------------------------------------------//
/*!
 * Add Geant4 10.x property name aliases for Opticks compatibility.
 *
 * Opticks GPU code looks up scintillation spectra by Geant4 10.x names
 * (FASTCOMPONENT, SLOWCOMPONENT) while DD4hep + Geant4 11.x uses
 * SCINTILLATIONCOMPONENT1/2. This adds the old names pointing to the
 * same data so both sides find what they need.
 */
static void addOpticksPropertyAliases()
{
    static const std::pair<const char*, const char*> aliases[] = {
        {"SCINTILLATIONCOMPONENT1", "FASTCOMPONENT"},
        {"SCINTILLATIONCOMPONENT2", "SLOWCOMPONENT"},
    };

    auto const* matTable = G4Material::GetMaterialTable();
    for (auto const* mat : *matTable)
    {
        auto* mpt = mat->GetMaterialPropertiesTable();
        if (!mpt)
            continue;

        for (auto const& [g4_11_name, g4_10_name] : aliases)
        {
            auto* prop = mpt->GetProperty(g4_11_name);
            if (prop)
            {
                bool createNewKey = true;
                mpt->AddProperty(g4_10_name, prop, createNewKey);
            }
        }
    }
}

//---------------------------------------------------------------------------//
OpticsRun::OpticsRun(dd4hep::sim::Geant4Context* ctxt,
                     std::string const& name)
    : dd4hep::sim::Geant4RunAction(ctxt, name)
{
    dd4hep::InstanceCount::increment(this);
    declareProperty("SaveGeometry", save_geometry_);
}

//---------------------------------------------------------------------------//
OpticsRun::~OpticsRun()
{
    dd4hep::InstanceCount::decrement(this);
}

//---------------------------------------------------------------------------//
void OpticsRun::begin(G4Run const* run)
{
    G4VPhysicalVolume* world = context()->world();
    if (!world)
    {
        except("OpticsRun: world volume is null at begin-of-run");
        return;
    }

    info("Initializing G4CXOpticks geometry (run #%d)", run->GetRunID());

    // Add Geant4 10.x scintillation property aliases for Opticks GPU
    addOpticksPropertyAliases();

    SEvt::CreateOrReuse(SEvt::EGPU);

    // Register DD4hep-aware sensor identifier before geometry translation.
    // Unlike the default which requires GLOBAL_SENSOR_BOUNDARY_LIST env var,
    // this checks G4VSensitiveDetector directly on volumes.
    static DD4hepSensorIdentifier dd4hep_sid ;
    G4CXOpticks::SetSensorIdentifier(&dd4hep_sid);

    bool hasDevice = SEventConfig::HasDevice();
    info("HasDevice=%s, IntegrationMode=%d", hasDevice ? "YES" : "NO",
         SEventConfig::IntegrationMode());
    G4CXOpticks::SetGeometry(world);

    if (save_geometry_)
    {
        info("Saving Opticks geometry to disk");
        G4CXOpticks::SaveGeometry();
    }

    info("G4CXOpticks geometry initialized successfully");
}

//---------------------------------------------------------------------------//
void OpticsRun::end(G4Run const* run)
{
    info("Finalizing G4CXOpticks (run #%d)", run->GetRunID());
    G4CXOpticks::Finalize();
}

//---------------------------------------------------------------------------//
}  // namespace ddeicopticks

DECLARE_GEANT4ACTION_NS(ddeicopticks, OpticsRun)
