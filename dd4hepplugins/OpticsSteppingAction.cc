#include "OpticsSteppingAction.hh"

#include <DD4hep/InstanceCount.h>
#include <DDG4/Factories.h>

#include <G4AutoLock.hh>
#include <G4Cerenkov.hh>
#include <G4Scintillation.hh>
#include <G4OpticalPhoton.hh>
#include <G4ProcessManager.hh>
#include <G4SteppingManager.hh>
#include <G4EventManager.hh>
#include <G4TrackingManager.hh>
#include <G4Step.hh>
#include <G4Track.hh>
#include <G4Material.hh>
#include <G4MaterialPropertiesTable.hh>

#include <U4.hh>

namespace
{
G4Mutex genstep_mutex = G4MUTEX_INITIALIZER;
}

namespace ddeicopticks
{
//---------------------------------------------------------------------------//
OpticsSteppingAction::OpticsSteppingAction(
    dd4hep::sim::Geant4Context* ctxt,
    std::string const& name)
    : dd4hep::sim::Geant4SteppingAction(ctxt, name)
{
    dd4hep::InstanceCount::increment(this);
    declareProperty("Verbose", verbose_);
}

//---------------------------------------------------------------------------//
OpticsSteppingAction::~OpticsSteppingAction()
{
    dd4hep::InstanceCount::decrement(this);
}

//---------------------------------------------------------------------------//
void OpticsSteppingAction::operator()(const G4Step* step,
                                      G4SteppingManager* /*mgr*/)
{
    G4SteppingManager* fpSteppingManager =
        G4EventManager::GetEventManager()->GetTrackingManager()->GetSteppingManager();

    const G4Track* track = step->GetTrack();

    G4StepStatus stepStatus = fpSteppingManager->GetfStepStatus();
    if (stepStatus == fAtRestDoItProc)
        return;

    // Skip optical photons — they don't produce gensteps
    if (track->GetDefinition() == G4OpticalPhoton::OpticalPhotonDefinition())
        return;

    G4ProcessVector* procPost = fpSteppingManager->GetfPostStepDoItVector();
    size_t nproc = fpSteppingManager->GetMAXofPostStepLoops();

    if (verbose_ > 1)
    {
        std::string procs;
        for (size_t i = 0; i < nproc; i++)
            if ((*procPost)[i])
                procs += (*procPost)[i]->GetProcessName() + " ";
        info("  nproc=%zu processes=[%s]", nproc, procs.c_str());
    }

    for (size_t i = 0; i < nproc; i++)
    {
        if (!(*procPost)[i])
            continue;

        const G4String& procName = (*procPost)[i]->GetProcessName();

        if (procName == "Cerenkov")
        {
            const G4DynamicParticle* aParticle = track->GetDynamicParticle();
            G4double charge = aParticle->GetDefinition()->GetPDGCharge();
            const G4Material* mat = track->GetMaterial();
            G4MaterialPropertiesTable* mpt = mat->GetMaterialPropertiesTable();
            if (!mpt)
                continue;

            G4MaterialPropertyVector* Rindex = mpt->GetProperty(kRINDEX);
            if (!Rindex || Rindex->GetVectorLength() == 0)
                continue;

            G4Cerenkov* cer = static_cast<G4Cerenkov*>((*procPost)[i]);
            G4int numPhotons = cer->GetNumPhotons();
            if (numPhotons <= 0)
                continue;

            G4double Pmin = Rindex->Energy(0);
            G4double Pmax = Rindex->GetMaxEnergy();
            G4double nMax = Rindex->GetMaxValue();
            G4double beta1 = step->GetPreStepPoint()->GetBeta();
            G4double beta2 = step->GetPostStepPoint()->GetBeta();
            G4double beta = (beta1 + beta2) * 0.5;
            G4double BetaInverse = 1.0 / beta;
            G4double maxCos = BetaInverse / nMax;
            G4double maxSin2 = (1.0 - maxCos) * (1.0 + maxCos);
            G4double MeanNumberOfPhotons1 =
                cer->GetAverageNumberOfPhotons(charge, beta1, mat, Rindex);
            G4double MeanNumberOfPhotons2 =
                cer->GetAverageNumberOfPhotons(charge, beta2, mat, Rindex);

            {
                G4AutoLock lock(&genstep_mutex);
                U4::CollectGenstep_G4Cerenkov_modified(
                    track, step, numPhotons, BetaInverse, Pmin, Pmax,
                    maxCos, maxSin2, MeanNumberOfPhotons1, MeanNumberOfPhotons2);
            }

            if (verbose_ > 0)
                info("Cerenkov genstep: %d photons", numPhotons);
        }
        else if (procName == "Scintillation")
        {
            G4Scintillation* scint = static_cast<G4Scintillation*>((*procPost)[i]);
            G4int numPhotons = scint->GetNumPhotons();
            if (numPhotons <= 0)
                continue;

            const G4Material* mat = track->GetMaterial();
            G4MaterialPropertiesTable* mpt = mat->GetMaterialPropertiesTable();
            if (!mpt)
                continue;

            G4double scintTime = 0.0;
            if (mpt->ConstPropertyExists(kSCINTILLATIONTIMECONSTANT1))
                scintTime = mpt->GetConstProperty(kSCINTILLATIONTIMECONSTANT1);

            {
                G4AutoLock lock(&genstep_mutex);
                U4::CollectGenstep_DsG4Scintillation_r4695(
                    track, step, numPhotons, /*scnt=*/1, scintTime);
            }

            if (verbose_ > 0)
                info("Scintillation genstep: %d photons", numPhotons);
        }
    }
}

//---------------------------------------------------------------------------//
}  // namespace ddeicopticks

DECLARE_GEANT4ACTION_NS(ddeicopticks, OpticsSteppingAction)
