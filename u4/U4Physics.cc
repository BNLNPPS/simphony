/**
U4Physics.cc
==============

Boundary class changes need to match in all the below::

    U4OpBoundaryProcess.h
    U4Physics.cc
    U4Recorder.cc
    U4StepPoint.cc

**/

#include "U4Physics.hh"
#include "G4FastSimulationManagerProcess.hh"
#include "G4ProcessManager.hh"
#include "U4Material.hh"
#include "U4OpBoundaryProcess.h"
#include "sstr.h"
#include "ssys.h"
#include <iomanip>

#include "G4OpBoundaryProcess.hh"


#include "SLOG.hh"
const plog::Severity U4Physics::LEVEL = SLOG::EnvLevel("U4Physics", "DEBUG") ;

U4Physics::U4Physics() :
    fCerenkov(nullptr),
    fScintillation(nullptr),
    fWLS(nullptr),
    fAbsorption(nullptr),
    fRayleigh(nullptr),
    fBoundary(nullptr),
    fFastSim(nullptr)
{
    Cerenkov_DISABLE = EInt(_Cerenkov_DISABLE, "0") ;
    Scintillation_DISABLE = EInt(_Scintillation_DISABLE, "0" );
    OpWLS_DISABLE = EInt(_OpWLS_DISABLE, "0");
    OpAbsorption_DISABLE = EInt(_OpAbsorption_DISABLE, "0") ;
    OpRayleigh_DISABLE = EInt(_OpRayleigh_DISABLE, "0") ;
    OpBoundaryProcess_DISABLE = EInt(_OpBoundaryProcess_DISABLE, "0") ;
    OpBoundaryProcess_LASTPOST = EInt(_OpBoundaryProcess_LASTPOST, "0") ;
    FastSim_ENABLE = EInt(_FastSim_ENABLE, "0") ;
}




#include "G4BosonConstructor.hh"
#include "G4LeptonConstructor.hh"
#include "G4MesonConstructor.hh"
#include "G4BaryonConstructor.hh"
#include "G4IonConstructor.hh"


void U4Physics::ConstructParticle()
{
    G4BosonConstructor::ConstructParticle();
    G4LeptonConstructor::ConstructParticle();
    G4MesonConstructor::ConstructParticle();
    G4BaryonConstructor::ConstructParticle();
    G4IonConstructor::ConstructParticle();
}

void U4Physics::ConstructProcess()
{
    AddTransportation();
    ConstructEM();
    ConstructOp();
}

// from OpNovicePhysicsList::ConstructEM

#include "G4ComptonScattering.hh"
#include "G4GammaConversion.hh"
#include "G4PhotoElectricEffect.hh"

#include "G4eMultipleScattering.hh"
#include "G4MuMultipleScattering.hh"
#include "G4hMultipleScattering.hh"

#include "G4eIonisation.hh"
#include "G4eBremsstrahlung.hh"
#include "G4eplusAnnihilation.hh"

#include "G4MuIonisation.hh"
#include "G4MuBremsstrahlung.hh"
#include "G4MuPairProduction.hh"

#include "G4hIonisation.hh"

void U4Physics::ConstructEM()
{
    G4int em_verbosity = 0 ;
    G4EmParameters* empar = G4EmParameters::Instance() ;
    empar->SetVerbose(em_verbosity);
    empar->SetWorkerVerbose(em_verbosity);

  auto particleIterator=GetParticleIterator();
  particleIterator->reset();
  while( (*particleIterator)() )
  {
    G4ParticleDefinition* particle = particleIterator->value();

    G4ProcessManager* pmanager = particle->GetProcessManager();
    G4String particleName = particle->GetParticleName();

    if (particleName == "gamma") {
    // gamma
      // Construct processes for gamma
      pmanager->AddDiscreteProcess(new G4GammaConversion());
      pmanager->AddDiscreteProcess(new G4ComptonScattering());
      pmanager->AddDiscreteProcess(new G4PhotoElectricEffect());

    } else if (particleName == "e-") {
    //electron
      // Construct processes for electron
      pmanager->AddProcess(new G4eMultipleScattering(),-1, 1, 1);
      pmanager->AddProcess(new G4eIonisation(),       -1, 2, 2);
      pmanager->AddProcess(new G4eBremsstrahlung(),   -1, 3, 3);

    } else if (particleName == "e+") {
    //positron
      // Construct processes for positron
      pmanager->AddProcess(new G4eMultipleScattering(),-1, 1, 1);
      pmanager->AddProcess(new G4eIonisation(),       -1, 2, 2);
      pmanager->AddProcess(new G4eBremsstrahlung(),   -1, 3, 3);
      pmanager->AddProcess(new G4eplusAnnihilation(),  0,-1, 4);

    } else if( particleName == "mu+" ||
               particleName == "mu-"    ) {
    //muon
     // Construct processes for muon
     pmanager->AddProcess(new G4MuMultipleScattering(),-1, 1, 1);
     pmanager->AddProcess(new G4MuIonisation(),      -1, 2, 2);
     pmanager->AddProcess(new G4MuBremsstrahlung(),  -1, 3, 3);
     pmanager->AddProcess(new G4MuPairProduction(),  -1, 4, 4);

    } else {
      if ((particle->GetPDGCharge() != 0.0) &&
          (particle->GetParticleName() != "chargedgeantino") &&
          !particle->IsShortLived()) {
       // all others charged particles except geantino
       pmanager->AddProcess(new G4hMultipleScattering(),-1,1,1);
       pmanager->AddProcess(new G4hIonisation(),       -1,2,2);
     }
    }
  }
}

#include "G4Cerenkov.hh"
#include "G4OpWLS.hh"
#include "G4Scintillation.hh"

#include "ShimG4OpAbsorption.hh"
#include "ShimG4OpRayleigh.hh"


std::string U4Physics::desc() const
{
    std::stringstream ss ;
    ss
        << "U4Physics::desc" << "\n"
        << std::setw(60) << _Cerenkov_DISABLE << " : " << Cerenkov_DISABLE << "\n"
        << std::setw(60) << _Scintillation_DISABLE << " : " << Scintillation_DISABLE << "\n"
        << std::setw(60) << _OpWLS_DISABLE << " : " << OpWLS_DISABLE << "\n"
        << std::setw(60) << _OpAbsorption_DISABLE << " : " << OpAbsorption_DISABLE << "\n"
        << std::setw(60) << _OpRayleigh_DISABLE << " : " << OpRayleigh_DISABLE << "\n"
        << std::setw(60) << _OpBoundaryProcess_DISABLE << " : " << OpBoundaryProcess_DISABLE << "\n"
        << std::setw(60) << _OpBoundaryProcess_LASTPOST << " : " << OpBoundaryProcess_LASTPOST << "\n"
        << std::setw(60) << _FastSim_ENABLE << " : " << FastSim_ENABLE << "\n";
    std::string str = ss.str();
    return str ;
}


std::string U4Physics::Desc()  // static
{
    std::stringstream ss ;
#ifdef DEBUG_TAG
    ss << ( ShimG4OpAbsorption::FLOAT ? "ShimG4OpAbsorption_FLOAT" : "ShimG4OpAbsorption_ORIGINAL" ) ;
    ss << "_" ;
    ss << ( ShimG4OpRayleigh::FLOAT ? "ShimG4OpRayleigh_FLOAT" : "ShimG4OpRayleigh_ORIGINAL" ) ;
#endif
    std::string str = ss.str();
    return str ;
}




std::string U4Physics::Switches()  // static
{
    std::stringstream ss ;
    ss << "U4Physics::Switches" << std::endl ;
#if defined(DEBUG_TAG)
    ss << "DEBUG_TAG" << std::endl ;
#else
    ss << "NOT:DEBUG_TAG" << std::endl ;
#endif
    std::string str = ss.str();
    return str ;
}



int U4Physics::EInt(const char* key, const char* fallback)  // static
{
    const char* val_ = getenv(key) ;
    int val =  std::atoi(val_ ? val_ : fallback) ;
    return val ;
}

/**
 * Creates and registers the optical processes enabled by this physics list.
 *
 * Legacy reemission properties are migrated before process construction.
 * Official Geant4 Cerenkov and scintillation processes are registered on
 * applicable non-optical particles, while WLS, absorption, Rayleigh,
 * boundary, and optional fast-simulation processes are registered on optical
 * photons. Photon stacking remains controlled by `G4OpticalParameters` and
 * Geant4 macro commands so GPU-only runs can suppress CPU optical
 * secondaries.
 */

void U4Physics::ConstructOp()
{
    LOG(info) << desc() ;

    const int numLegacyReemissionConverted = U4Material::ConvertLegacyReemissionToWLS();
    LOG_IF(info, numLegacyReemissionConverted > 0)
        << "converted legacy re-emission materials " << numLegacyReemissionConverted;

    if(Cerenkov_DISABLE == 0)
    {
        fCerenkov = new G4Cerenkov;
        fCerenkov->SetMaxNumPhotonsPerStep(10000);
        fCerenkov->SetMaxBetaChangePerStep(10.0);
        fCerenkov->SetTrackSecondariesFirst(true);
        fCerenkov->SetVerboseLevel(EInt("G4Cerenkov_verboseLevel", "0"));
    }

    if(Scintillation_DISABLE == 0)
    {
        fScintillation = new G4Scintillation;
        fScintillation->SetScintillationTrackInfo(false);
        fScintillation->SetTrackSecondariesFirst(true);
        fScintillation->SetVerboseLevel(EInt("G4Scintillation_verboseLevel", "0"));
    }

    if (OpWLS_DISABLE == 0)
    {
        fWLS = new G4OpWLS;
        fWLS->UseTimeProfile("exponential");
        fWLS->SetVerboseLevel(EInt("G4OpWLS_verboseLevel", "0"));
    }

    if(FastSim_ENABLE == 1 )
    {
        fFastSim  = new G4FastSimulationManagerProcess("fast_sim_man");
    }

    if(OpAbsorption_DISABLE == 0)
    {
#ifdef DEBUG_TAG
        fAbsorption = new ShimG4OpAbsorption();
#else
        fAbsorption = new G4OpAbsorption();
#endif
    }

    if(OpRayleigh_DISABLE == 0)
    {
#ifdef DEBUG_TAG
        fRayleigh = new ShimG4OpRayleigh();
#else
        fRayleigh = new G4OpRayleigh();
#endif
    }

    if(OpBoundaryProcess_DISABLE == 0)
    {
        fBoundary = CreateBoundaryProcess();
        LOG(info) << " fBoundary " << fBoundary ;
    }



  auto particleIterator=GetParticleIterator();
  particleIterator->reset();
  while( (*particleIterator)() )
  {
        G4ParticleDefinition* particle = particleIterator->value();
        G4ProcessManager* pmanager = particle->GetProcessManager();
        G4String particleName = particle->GetParticleName();

        if ( fCerenkov && fCerenkov->IsApplicable(*particle))
        {
            pmanager->AddProcess(fCerenkov);
            pmanager->SetProcessOrdering(fCerenkov,idxPostStep);
        }

        if ( fScintillation && fScintillation->IsApplicable(*particle) && particleName != "opticalphoton")
        {
            pmanager->AddProcess(fScintillation);
            pmanager->SetProcessOrderingToLast(fScintillation, idxAtRest);
            pmanager->SetProcessOrderingToLast(fScintillation, idxPostStep);
        }

        if (particleName == "opticalphoton")
        {
            ConstructOp_opticalphoton(pmanager, particleName);
        }
    }
}

/**
 * Installs the configured processes that operate on optical photons.
 *
 * @param pmanager process manager that receives the optical processes
 * @param particleName particle name, which must be `opticalphoton`
 */

void U4Physics::ConstructOp_opticalphoton(G4ProcessManager* pmanager, const G4String& particleName)
{
    assert( particleName == "opticalphoton" );

    if (fWLS)
        pmanager->AddDiscreteProcess(fWLS);
    if(fAbsorption)    pmanager->AddDiscreteProcess(fAbsorption);
    if(fRayleigh)      pmanager->AddDiscreteProcess(fRayleigh);
    if(fBoundary)      pmanager->AddDiscreteProcess(fBoundary);
    if(fFastSim)       pmanager->AddDiscreteProcess(fFastSim);
}

G4VProcess* U4Physics::CreateBoundaryProcess()  // static
{
    return new G4OpBoundaryProcess();
}
