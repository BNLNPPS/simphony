#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>

#include "G4AutoLock.hh"
#include "G4BooleanSolid.hh"
#include "G4Cerenkov.hh"
#include "G4Electron.hh"
#include "G4Event.hh"
#include "G4GDMLParser.hh"
#include "G4LogicalVolumeStore.hh"
#include "G4OpBoundaryProcess.hh"
#include "G4OpticalPhoton.hh"
#include "G4PhysicalConstants.hh"
#include "G4PrimaryParticle.hh"
#include "G4PrimaryVertex.hh"
#include "G4RunManager.hh"
#include "G4RunManagerFactory.hh"
#include "G4SDManager.hh"
#include "G4Scintillation.hh"
#include "G4SubtractionSolid.hh"
#include "G4SystemOfUnits.hh"
#include "G4ThreeVector.hh"
#include "G4Track.hh"
#include "G4TrackStatus.hh"
#include "G4UserEventAction.hh"
#include "G4UserRunAction.hh"
#include "G4UserSteppingAction.hh"
#include "G4UserTrackingAction.hh"
#include "G4VPhysicalVolume.hh"
#include "G4VProcess.hh"
#include "G4VUserDetectorConstruction.hh"
#include "G4VUserPrimaryGeneratorAction.hh"

#include "g4cx/G4CXOpticks.hh"
#include "sysrap/NP.hh"
#include "sysrap/SEvt.hh"
#include "sysrap/STrackInfo.h"
#include "sysrap/spho.h"
#include "sysrap/sphoton.h"
#include "u4/U4.hh"
#include "u4/U4Random.hh"
#include "u4/U4StepPoint.hh"
#include "u4/U4Touchable.h"
#include "u4/U4Track.h"

namespace
{
G4Mutex genstep_mutex = G4MUTEX_INITIALIZER;
G4Mutex g4hits_mutex = G4MUTEX_INITIALIZER;
std::vector<std::array<float, 16>> g4_accumulated_hits;
} // namespace

bool IsSubtractionSolid(G4VSolid *solid)
{
    if (!solid)
        return false;

    // Check if the solid is directly a G4SubtractionSolid
    if (dynamic_cast<G4SubtractionSolid *>(solid))
        return true;

    // If the solid is a Boolean solid, check its constituent solids
    G4BooleanSolid *booleanSolid = dynamic_cast<G4BooleanSolid *>(solid);
    if (booleanSolid)
    {
        G4VSolid *solidA = booleanSolid->GetConstituentSolid(0);
        G4VSolid *solidB = booleanSolid->GetConstituentSolid(1);

        // Recursively check the constituent solids
        if (IsSubtractionSolid(solidA) || IsSubtractionSolid(solidB))
            return true;
    }

    // For other solid types, return false
    return false;
}

std::string str_tolower(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
    return s;
}

struct PhotonHit : public G4VHit
{
    PhotonHit() = default;

    PhotonHit(unsigned id, G4double energy, G4double time, G4ThreeVector position, G4ThreeVector direction,
              G4ThreeVector polarization)
        : fid(id), fenergy(energy), ftime(time), fposition(position), fdirection(direction), fpolarization(polarization)
    {
    }

    // Copy constructor
    PhotonHit(const PhotonHit &right)
        : G4VHit(right), fid(right.fid), fenergy(right.fenergy), ftime(right.ftime), fposition(right.fposition),
          fdirection(right.fdirection), fpolarization(right.fpolarization)
    {
    }

    // Assignment operator
    const PhotonHit &operator=(const PhotonHit &right)
    {
        if (this != &right)
        {
            G4VHit::operator=(right);
            fid = right.fid;
            fenergy = right.fenergy;
            ftime = right.ftime;
            fposition = right.fposition;
            fdirection = right.fdirection;
            fpolarization = right.fpolarization;
        }
        return *this;
    }

    // Equality operator
    G4bool operator==(const PhotonHit &right) const
    {
        return (this == &right);
    }

    // Print method
    void Print() override
    {
        G4cout << "Detector id: " << fid << " energy: " << fenergy << " nm" << " time: " << ftime << " ns"
               << " position: " << fposition << " direction: " << fdirection << " polarization: " << fpolarization
               << G4endl;
    }

    // Member variables
    G4int fid{0};
    G4double fenergy{0};
    G4double ftime{0};
    G4ThreeVector fposition{0, 0, 0};
    G4ThreeVector fdirection{0, 0, 0};
    G4ThreeVector fpolarization{0, 0, 0};
};

using PhotonHitsCollection = G4THitsCollection<PhotonHit>;

struct PhotonSD : public G4VSensitiveDetector
{
    PhotonSD(G4String name) : G4VSensitiveDetector(name), fHCID(-1)
    {
        G4String HCname = name + "_HC";
        collectionName.insert(HCname);
        G4cout << collectionName.size() << "   PhotonSD name:  " << name << " collection Name: " << HCname << G4endl;
    }

    void Initialize(G4HCofThisEvent *hce) override
    {
        fPhotonHitsCollection = new PhotonHitsCollection(SensitiveDetectorName, collectionName[0]);
        if (fHCID < 0)
        {
            // G4cout << "PhotonSD::Initialize:  " << SensitiveDetectorName << "   " << collectionName[0] << G4endl;
            fHCID = G4SDManager::GetSDMpointer()->GetCollectionID(collectionName[0]);
        }
        hce->AddHitsCollection(fHCID, fPhotonHitsCollection);
    }

    G4bool ProcessHits(G4Step *aStep, G4TouchableHistory *) override
    {
        G4Track *theTrack = aStep->GetTrack();
        if (theTrack->GetDefinition() != G4OpticalPhoton::OpticalPhotonDefinition())
            return false;

        G4double theEnergy = theTrack->GetTotalEnergy() / CLHEP::eV;

        // Create a new hit (CopyNr is set to 0 as DetectorID is omitted)
        PhotonHit *newHit = new PhotonHit(
            0, // CopyNr set to 0
            theEnergy, theTrack->GetGlobalTime(), aStep->GetPostStepPoint()->GetPosition(),
            aStep->GetPostStepPoint()->GetMomentumDirection(), aStep->GetPostStepPoint()->GetPolarization());

        fPhotonHitsCollection->insert(newHit);
        theTrack->SetTrackStatus(fStopAndKill);
        return true;
    }

    void EndOfEvent(G4HCofThisEvent *) override
    {
        G4int NbHits = fPhotonHitsCollection->entries();
        G4cout << "PhotonSD::EndOfEvent Number of PhotonHits: " << NbHits << G4endl;
    }

    void AddOpticksHits()
    {
        SEvt *sev = SEvt::Get_EGPU();
        unsigned int num_hits = sev->GetNumHit(0);

        for (int idx = 0; idx < int(num_hits); idx++)
        {
            sphoton hit;
            sev->getHit(hit, idx);
            G4ThreeVector position = G4ThreeVector(hit.pos.x, hit.pos.y, hit.pos.z);
            G4ThreeVector direction = G4ThreeVector(hit.mom.x, hit.mom.y, hit.mom.z);
            G4ThreeVector polarization = G4ThreeVector(hit.pol.x, hit.pol.y, hit.pol.z);
            int theCreationProcessid;
            if (OpticksPhoton::HasCerenkovFlag(hit.flagmask))
            {
                theCreationProcessid = 0;
            }
            else if (OpticksPhoton::HasScintillationFlag(hit.flagmask))
            {
                theCreationProcessid = 1;
            }
            else
            {
                theCreationProcessid = -1;
            }
            std::cout << hit.wavelength << " " << position << " " << direction << " " << polarization << std::endl;

            PhotonHit *newHit = new PhotonHit(0, hit.wavelength, hit.time, position, direction, polarization);
            fPhotonHitsCollection->insert(newHit);
        }
    }

  private:
    PhotonHitsCollection *fPhotonHitsCollection{nullptr};
    G4int fHCID;
};

struct DetectorConstruction : G4VUserDetectorConstruction
{
    DetectorConstruction(std::filesystem::path gdml_file) : gdml_file_(gdml_file)
    {
    }

    G4VPhysicalVolume *Construct() override
    {
        parser_.Read(gdml_file_.string(), false);
        G4VPhysicalVolume *world = parser_.GetWorldVolume();

        G4CXOpticks::SetGeometry(world);
        G4LogicalVolumeStore *lvStore = G4LogicalVolumeStore::GetInstance();

        static G4VisAttributes invisibleVisAttr(false);

        // Check if the store is not empty
        if (lvStore && !lvStore->empty())
        {
            // Iterate over all logical volumes in the store
            for (auto &logicalVolume : *lvStore)
            {
                G4VSolid *solid = logicalVolume->GetSolid();

                // Check if the solid uses subtraction
                if (IsSubtractionSolid(solid))
                {
                    // Assign the invisible visual attributes to the logical volume
                    logicalVolume->SetVisAttributes(&invisibleVisAttr);

                    // Optionally, print out the name of the logical volume
                    G4cout << "Hiding logical volume: " << logicalVolume->GetName() << G4endl;
                }
            }
        }

        return world;
    }

    void ConstructSDandField() override
    {
        G4cout << "ConstructSDandField is called." << G4endl;
        G4SDManager *SDman = G4SDManager::GetSDMpointer();

        const G4GDMLAuxMapType *auxmap = parser_.GetAuxMap();
        for (auto const &[logVol, listType] : *auxmap)
        {
            for (auto const &auxtype : listType)
            {
                if (auxtype.type == "SensDet")
                {
                    G4cout << "Attaching sensitive detector to logical volume: " << logVol->GetName() << G4endl;
                    G4String name = logVol->GetName() + "_PhotonDetector";
                    PhotonSD *aPhotonSD = new PhotonSD(name);
                    SDman->AddNewDetector(aPhotonSD);
                    logVol->SetSensitiveDetector(aPhotonSD);
                }
            }
        }
    }

  private:
    std::filesystem::path gdml_file_;
    G4GDMLParser parser_;
};

struct PrimaryGenerator : G4VUserPrimaryGeneratorAction
{
    SEvt *sev;

    PrimaryGenerator(SEvt *sev) : sev(sev)
    {
    }

    void GeneratePrimaries(G4Event *event) override
    {
        G4ThreeVector position_mm(0.0 * m, 0.0 * m, 0.0 * m);
        G4double time_ns = 0;
        G4ThreeVector direction(0, 0.2, 0.8);
        G4double wavelength_nm = 0.1;

        G4PrimaryVertex *vertex = new G4PrimaryVertex(position_mm, time_ns);
        G4PrimaryParticle *particle = new G4PrimaryParticle(G4Electron::Definition());
        particle->SetKineticEnergy(10 * MeV);
        particle->SetMomentumDirection(direction);
        vertex->SetPrimary(particle);
        event->AddPrimaryVertex(vertex);
    }
};

struct EventAction : G4UserEventAction
{
    SEvt *sev;
    G4int fTotalG4Hits{0};

    EventAction(SEvt *sev) : sev(sev)
    {
    }

    void BeginOfEventAction(const G4Event *event) override
    {
    }

    void EndOfEventAction(const G4Event *event) override
    {
        G4HCofThisEvent *hce = event->GetHCofThisEvent();
        if (hce)
        {
            for (G4int i = 0; i < hce->GetNumberOfCollections(); i++)
            {
                G4VHitsCollection *hc = hce->GetHC(i);
                if (!hc)
                    continue;

                PhotonHitsCollection *phc = dynamic_cast<PhotonHitsCollection *>(hc);
                if (phc)
                {
                    G4AutoLock lock(&g4hits_mutex);
                    for (size_t j = 0; j < phc->entries(); j++)
                    {
                        PhotonHit *hit = (*phc)[j];
                        float wl = 1239.84198f / static_cast<float>(hit->fenergy);
                        g4_accumulated_hits.push_back(
                            {float(hit->fposition.x()), float(hit->fposition.y()), float(hit->fposition.z()),
                             float(hit->ftime), float(hit->fdirection.x()), float(hit->fdirection.y()),
                             float(hit->fdirection.z()), 0.f, float(hit->fpolarization.x()),
                             float(hit->fpolarization.y()), float(hit->fpolarization.z()), wl, 0.f, 0.f, 0.f, 0.f});
                    }
                    fTotalG4Hits += phc->entries();
                }
                else
                {
                    fTotalG4Hits += hc->GetSize();
                }
            }
        }
    }

    G4int GetTotalG4Hits() const
    {
        return fTotalG4Hits;
    }
};

struct RunAction : G4UserRunAction
{
    EventAction *fEventAction;
    bool fSavePhotonHistory{false};

    RunAction(EventAction *eventAction) : fEventAction(eventAction)
    {
    }

    void BeginOfRunAction(const G4Run *run) override
    {
    }

    void EndOfRunAction(const G4Run *run) override
    {
        if (G4Threading::IsMasterThread())
        {
            G4CXOpticks *gx = G4CXOpticks::Get();

            auto start = std::chrono::high_resolution_clock::now();
            gx->simulate(0, false);
            cudaDeviceSynchronize();
            auto end = std::chrono::high_resolution_clock::now();
            // Compute duration
            std::chrono::duration<double> elapsed = end - start;
            std::cout << "Simulation time: " << elapsed.count() << " seconds" << std::endl;

            // unsigned int num_hits = SEvt::GetNumHit(EGPU);
            SEvt *sev = SEvt::Get_EGPU();
            unsigned int num_hits = sev->GetNumHit(0);

            std::cout << "Opticks: NumCollected:  " << sev->GetNumGenstepFromGenstep(0) << std::endl;
            std::cout << "Opticks: NumCollected:  " << sev->GetNumPhotonCollected(0) << std::endl;
            std::cout << "Opticks: NumHits:  " << num_hits << std::endl;
            std::cout << "Geant4: NumHits:  " << fEventAction->GetTotalG4Hits() << std::endl;

            if (fSavePhotonHistory)
            {
                // Save full SEvt (photon, record, seq, hit) when DebugLite/DebugHeavy
                sev->save();
                std::cout << "SEvt::save() complete" << std::endl;

                // Save GPU hits as .npy (sphoton layout: N x 4 x 4 float32)
                {
                    NP *gpu_h = NP::Make<float>(num_hits, 4, 4);
                    for (unsigned idx = 0; idx < num_hits; idx++)
                    {
                        sphoton hit;
                        sev->getHit(hit, idx);
                        memcpy(gpu_h->bytes() + idx * sizeof(sphoton), &hit, sizeof(sphoton));
                    }
                    gpu_h->save("gpu_hits.npy");
                    std::cout << "Saved GPU hits: " << num_hits << " to gpu_hits.npy" << std::endl;
                }

                // Save G4 hits as .npy (same layout: N x 4 x 4 float32)
                {
                    G4AutoLock lock(&g4hits_mutex);
                    size_t ng4 = g4_accumulated_hits.size();
                    if (ng4 > 0)
                    {
                        NP *g4h = NP::Make<float>(ng4, 4, 4);
                        memcpy(g4h->bytes(), g4_accumulated_hits.data(), ng4 * 16 * sizeof(float));
                        g4h->save("g4_hits.npy");
                        std::cout << "Saved G4 hits: " << ng4 << " to g4_hits.npy" << std::endl;
                    }
                }
            }
        }
    }
};

struct SteppingAction : G4UserSteppingAction
{
    SEvt *sev;

    SteppingAction(SEvt *sev) : sev(sev)
    {
    }

    void UserSteppingAction(const G4Step *aStep)
    {
        G4Track *aTrack;
        G4int fNumPhotons = 0;

        G4StepPoint *preStep = aStep->GetPostStepPoint();
        G4VPhysicalVolume *volume = preStep->GetPhysicalVolume();

        if (aStep->GetTrack()->GetDefinition() == G4OpticalPhoton::OpticalPhotonDefinition())
        {
            // Kill if step count exceeds 10000 to avoid reflection forever
            if (aStep->GetTrack()->GetCurrentStepNumber() > 10000)
            {
                aStep->GetTrack()->SetTrackStatus(fStopAndKill);
            }
        }

        if (volume && volume->GetName() == "MirrorPyramid")
        {
            aTrack = aStep->GetTrack();
            if (aTrack->GetDefinition() != G4OpticalPhoton::OpticalPhotonDefinition())
            {
                aTrack->SetTrackStatus(fStopAndKill);
            }
        }

        G4SteppingManager *fpSteppingManager =
            G4EventManager::GetEventManager()->GetTrackingManager()->GetSteppingManager();
        G4StepStatus stepStatus = fpSteppingManager->GetfStepStatus();

        if (stepStatus != fAtRestDoItProc)
        {
            G4ProcessVector *procPost = fpSteppingManager->GetfPostStepDoItVector();
            size_t MAXofPostStepLoops = fpSteppingManager->GetMAXofPostStepLoops();
            for (size_t i3 = 0; i3 < MAXofPostStepLoops; i3++)
            {
                if ((*procPost)[i3]->GetProcessName() == "Cerenkov")
                {
                    aTrack = aStep->GetTrack();
                    const G4DynamicParticle *aParticle = aTrack->GetDynamicParticle();
                    G4double charge = aParticle->GetDefinition()->GetPDGCharge();
                    const G4Material *aMaterial = aTrack->GetMaterial();
                    G4MaterialPropertiesTable *MPT = aMaterial->GetMaterialPropertiesTable();

                    G4MaterialPropertyVector *Rindex = MPT->GetProperty(kRINDEX);
                    if (!Rindex || Rindex->GetVectorLength() == 0)
                    {
                        G4cout << "WARNING: Material has no valid RINDEX data. Skipping Cerenkov calculation."
                               << G4endl;
                        return;
                    }

                    G4Cerenkov *proc = (G4Cerenkov *)(*procPost)[i3];
                    fNumPhotons = proc->GetNumPhotons();

                    G4AutoLock lock(&genstep_mutex); // <-- Mutex is locked here

                    if (fNumPhotons > 0)
                    {
                        G4double Pmin = Rindex->Energy(0);
                        G4double Pmax = Rindex->GetMaxEnergy();
                        G4double nMax = Rindex->GetMaxValue();
                        G4double beta1 = aStep->GetPreStepPoint()->GetBeta();
                        G4double beta2 = aStep->GetPostStepPoint()->GetBeta();
                        G4double beta = (beta1 + beta2) * 0.5;
                        G4double BetaInverse = 1. / beta;
                        G4double maxCos = BetaInverse / nMax;
                        G4double maxSin2 = (1.0 - maxCos) * (1.0 + maxCos);
                        G4double MeanNumberOfPhotons1 =
                            proc->GetAverageNumberOfPhotons(charge, beta1, aMaterial, Rindex);
                        G4double MeanNumberOfPhotons2 =
                            proc->GetAverageNumberOfPhotons(charge, beta2, aMaterial, Rindex);

                        U4::CollectGenstep_G4Cerenkov_modified(aTrack, aStep, fNumPhotons, BetaInverse, Pmin, Pmax,
                                                               maxCos, maxSin2, MeanNumberOfPhotons1,
                                                               MeanNumberOfPhotons2);
                    }
                }
                if ((*procPost)[i3]->GetProcessName() == "Scintillation")
                {
                    G4Scintillation *proc1 = (G4Scintillation *)(*procPost)[i3];
                    fNumPhotons = proc1->GetNumPhotons();
                    if (fNumPhotons > 0)
                    {
                        aTrack = aStep->GetTrack();
                        const G4Material *aMaterial = aTrack->GetMaterial();
                        G4MaterialPropertiesTable *MPT = aMaterial->GetMaterialPropertiesTable();
                        if (!MPT || !MPT->ConstPropertyExists(kSCINTILLATIONTIMECONSTANT1))
                        {
                            G4cout << "WARNING: Material has no valid SCINTILLATIONTIMECONSTANT1 data. Skipping "
                                      "Scintillation calculation."
                                   << G4endl;
                            return;
                        }
                        G4double SCINTILLATIONTIMECONSTANT1 = MPT->GetConstProperty(kSCINTILLATIONTIMECONSTANT1);

                        U4::CollectGenstep_DsG4Scintillation_r4695(aTrack, aStep, fNumPhotons, 1,
                                                                   SCINTILLATIONTIMECONSTANT1);
                    }
                }
            }
        }
    }
};

struct TrackingAction : G4UserTrackingAction
{
    const G4Track *transient_fSuspend_track = nullptr;
    SEvt *sev;

    TrackingAction(SEvt *sev) : sev(sev)
    {
    }

    void PreUserTrackingAction_Optical_FabricateLabel(const G4Track *track)
    {
    }

    void PreUserTrackingAction(const G4Track *track) override
    {
    }

    void PostUserTrackingAction(const G4Track *track) override
    {
    }
};

struct G4App
{
    G4App(std::filesystem::path gdml_file)
        : sev(SEvt::CreateOrReuse_EGPU()), det_cons_(new DetectorConstruction(gdml_file)),
          prim_gen_(new PrimaryGenerator(sev)), event_act_(new EventAction(sev)), run_act_(new RunAction(event_act_)),
          stepping_(new SteppingAction(sev)),

          tracking_(new TrackingAction(sev))
    {
    }

    //~G4App(){ G4CXOpticks::Finalize();}

    // Create "global" event
    SEvt *sev;

    G4VUserDetectorConstruction *det_cons_;
    G4VUserPrimaryGeneratorAction *prim_gen_;
    EventAction *event_act_;
    RunAction *run_act_;
    SteppingAction *stepping_;
    TrackingAction *tracking_;
};
