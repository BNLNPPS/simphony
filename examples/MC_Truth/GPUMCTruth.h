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
#include "G4THitsCollection.hh"
#include "G4Threading.hh"
#include "G4ThreeVector.hh"
#include "G4Track.hh"
#include "G4TrackStatus.hh"
#include "G4UserEventAction.hh"
#include "G4UserRunAction.hh"
#include "G4UserSteppingAction.hh"
#include "G4UserTrackingAction.hh"
#include "G4VHit.hh"
#include "G4VPhysicalVolume.hh"
#include "G4VProcess.hh"
#include "G4VUserDetectorConstruction.hh"
#include "G4VUserPrimaryGeneratorAction.hh"
#include "G4VisAttributes.hh"

#include "eic-opticks/g4cx/G4CXOpticks.hh"
#include "eic-opticks/sysrap/NP.hh"
#include "eic-opticks/sysrap/SEvt.hh"
#include "eic-opticks/sysrap/STrackInfo.h"
#include "eic-opticks/sysrap/spho.h"
#include "eic-opticks/sysrap/sphoton.h"
#include "eic-opticks/u4/U4.hh"
#include "eic-opticks/u4/U4Random.hh"
#include "eic-opticks/u4/U4StepPoint.hh"
#include "eic-opticks/u4/U4Touchable.h"
#include "eic-opticks/u4/U4Track.h"

namespace
{
G4Mutex genstep_mutex = G4MUTEX_INITIALIZER;
}

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
        particle->SetKineticEnergy(5 * GeV);
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
                if (hc)
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
            std::ofstream outFile("opticks_hits_output.txt");
            if (!outFile.is_open())
            {
                std::cerr << "Error opening output file!" << std::endl;
                return;
            }

            const bool emit_trackid = getenv("OPTICKS_MC_TRUTH") != nullptr;

            // Isolated MC-truth benchmark: two loops without file I/O so the
            // paired delta measures only the lookup cost (binary search +
            // trackid read), not the ostream formatting that dominates the
            // regular hit-write loop. Gated by OPTICKS_MC_TRUTH_BENCH so
            // normal runs pay nothing.
            if (getenv("OPTICKS_MC_TRUTH_BENCH"))
            {
                volatile uint64_t sink = 0;
                const NP *hit_np = sev->getHit();
                const sphoton *hits = reinterpret_cast<const sphoton *>(hit_np->bytes());
                const int M = int(sev->gs.size());
                auto bt0 = std::chrono::high_resolution_clock::now();
                for (int idx = 0; idx < int(num_hits); idx++)
                {
                    sink ^= hits[idx].index;
                }
                auto bt1 = std::chrono::high_resolution_clock::now();
                int g = 0;
                for (int idx = 0; idx < int(num_hits); idx++)
                {
                    const int64_t pidx = hits[idx].index;
                    while (g + 1 < M && pidx >= sev->gs[g + 1].offset)
                        ++g;
                    int tid = pidx < sev->gs[g].offset + sev->gs[g].photons ? int(sev->genstep[g].trackid()) : -1;
                    sink ^= hits[idx].index ^ unsigned(tid);
                }
                auto bt2 = std::chrono::high_resolution_clock::now();
                std::chrono::duration<double> base_t = bt1 - bt0;
                std::chrono::duration<double> mc_t = bt2 - bt1;
                double base_ns = num_hits > 0 ? 1e9 * base_t.count() / num_hits : 0.0;
                double mc_ns = num_hits > 0 ? 1e9 * mc_t.count() / num_hits : 0.0;
                std::cout << "Bench baseline:  " << base_t.count() << " s  (" << base_ns << " ns/hit)" << std::endl;
                std::cout << "Bench mctruth:   " << mc_t.count() << " s  (" << mc_ns << " ns/hit)" << std::endl;
                std::cout << "Bench delta:     " << (mc_t.count() - base_t.count()) << " s  (" << (mc_ns - base_ns)
                          << " ns/hit)   [sink=" << sink << "]" << std::endl;
            }

            auto hit_loop_start = std::chrono::high_resolution_clock::now();
            const NP *hit_np_main = sev->getHit();
            const sphoton *hits_main = reinterpret_cast<const sphoton *>(hit_np_main->bytes());
            const int M_gs = int(sev->gs.size());
            int g_cur = 0;
            for (int idx = 0; idx < int(num_hits); idx++)
            {
                const sphoton &hit = hits_main[idx];
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
                //    std::cout << "Adding hit from Opticks:" << hit.wavelength << " " << position << " " << direction
                //    << "
                //    "
                //              << polarization << std::endl;
                outFile << hit.time << " " << hit.wavelength << "  " << "(" << position.x() << ", " << position.y()
                        << ", " << position.z() << ")  " << "(" << direction.x() << ", " << direction.y() << ", "
                        << direction.z() << ")  " << "(" << polarization.x() << ", " << polarization.y() << ", "
                        << polarization.z() << ")  " << "CreationProcessID=" << theCreationProcessid;
                if (emit_trackid)
                {
                    const int64_t pidx = hit.index;
                    while (g_cur + 1 < M_gs && pidx >= sev->gs[g_cur + 1].offset)
                        ++g_cur;
                    int trackID =
                        pidx < sev->gs[g_cur].offset + sev->gs[g_cur].photons ? int(sev->genstep[g_cur].trackid()) : -1;
                    outFile << " TrackID=" << trackID;
                }
                outFile << std::endl;
            }
            auto hit_loop_end = std::chrono::high_resolution_clock::now();
            std::chrono::duration<double> hit_loop_elapsed = hit_loop_end - hit_loop_start;
            std::cout << "Hit-write loop time: " << hit_loop_elapsed.count() << " seconds"
                      << " (emit_trackid=" << (emit_trackid ? "1" : "0") << ", num_hits=" << num_hits << ")"
                      << std::endl;

            outFile.close();
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
