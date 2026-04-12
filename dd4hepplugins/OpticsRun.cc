#include "OpticsRun.hh"

#include <DD4hep/InstanceCount.h>
#include <DDG4/Factories.h>
#include <DDG4/Geant4Kernel.h>

#include <G4Material.hh>
#include <G4MaterialPropertiesTable.hh>
#include <G4Run.hh>

#include <chrono>

#include <G4CXOpticks.hh>
#include <SEventConfig.hh>
#include <SEvt.hh>
#include <SSim.hh>

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
    static const std::pair<const char *, const char *> aliases[] = {
        {"SCINTILLATIONCOMPONENT1", "FASTCOMPONENT"},
        {"SCINTILLATIONCOMPONENT2", "SLOWCOMPONENT"},
    };

    auto const *matTable = G4Material::GetMaterialTable();
    for (auto const *mat : *matTable)
    {
        auto *mpt = mat->GetMaterialPropertiesTable();
        if (!mpt)
            continue;

        for (auto const &[g4_11_name, g4_10_name] : aliases)
        {
            auto *prop = mpt->GetProperty(g4_11_name);
            if (prop && !mpt->GetProperty(g4_10_name))
            {
                bool createNewKey = true;
                mpt->AddProperty(g4_10_name, prop, createNewKey);
            }
        }
    }
}

//---------------------------------------------------------------------------//
OpticsRun::OpticsRun(dd4hep::sim::Geant4Context *ctxt, std::string const &name)
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
void OpticsRun::begin(G4Run const *run)
{
    G4VPhysicalVolume *world = context()->world();
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
    static DD4hepSensorIdentifier dd4hep_sid;
    G4CXOpticks::SetSensorIdentifier(&dd4hep_sid);

    bool hasDevice = SEventConfig::HasDevice();
    info("HasDevice=%s, IntegrationMode=%d", hasDevice ? "YES" : "NO", SEventConfig::IntegrationMode());
    G4CXOpticks::SetGeometry(world);

    if (save_geometry_)
    {
        info("Saving Opticks geometry to disk");
        G4CXOpticks::SaveGeometry();
    }

    // Log boundary count
    {
        const SSim *sim = SSim::Get();
        if (sim && sim->get_bnd())
            info("Boundary table: %zu boundaries", sim->get_bnd()->names.size());
    }

    info("G4CXOpticks geometry initialized successfully");
}

//---------------------------------------------------------------------------//
void OpticsRun::end(G4Run const *run)
{
    // Flush any remaining batched gensteps (from PhotonThreshold mode)
    SEvt *sev = SEvt::Get_EGPU();
    if (sev)
    {
        int64_t num_genstep = sev->getNumGenstepFromGenstep();
        int64_t num_photon = sev->getNumPhotonFromGenstep();
        if (num_genstep > 0)
        {
            G4CXOpticks *gx = G4CXOpticks::Get();
            if (gx)
            {
                int eventID = run->GetNumberOfEvent() > 0 ? run->GetNumberOfEvent() - 1 : 0;
                info("Flushing %lld remaining photons from %lld gensteps", static_cast<long long>(num_photon),
                     static_cast<long long>(num_genstep));

                auto t0 = std::chrono::high_resolution_clock::now();
                gx->simulate(eventID, /*reset=*/false);
                auto t1 = std::chrono::high_resolution_clock::now();
                double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();

                unsigned num_hit = sev->getNumHit();
                info("OPTICKS_GPU_TIME event=%d ms=%.3f photons=%lld hits=%u", eventID, ms,
                     static_cast<long long>(num_photon), num_hit);

                sev->endOfEvent(eventID);
                gx->reset(eventID);
            }
        }
    }

    info("Finalizing G4CXOpticks (run #%d)", run->GetRunID());
    G4CXOpticks::Finalize();
}

//---------------------------------------------------------------------------//
} // namespace ddeicopticks

DECLARE_GEANT4ACTION_NS(ddeicopticks, OpticsRun)
