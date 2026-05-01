// GPURaytraceSubEvt.h — sub-event parallel variant of GPURaytraceQuasi.
//
// Same DetectorConstruction / PhotonSD / PrimaryGenerator / EventAction /
// RunAction / TrackingAction as examples/GPURaytraceQuasi, with the diagnostic
// prefix renamed and the worker thread id added.
//
// SteppingAction has two capture modes selected at construction:
//
//   CAPTURE_PARENT (used with --subevt-route cascade and --mode {serial,event-mt}
//                   default): genstep captured at the charged-parent step via
//                   the existing QuasiCerenkov / QuasiScintillation post-step
//                   intercept, identical to GPURaytraceQuasi.
//
//   CAPTURE_TOKEN  (used with --subevt-route photon): with stackPhotons=true
//                   upstream, G4QuasiCerenkov::PostStepDoIt pushes one
//                   G4QuasiOpticalPhoton secondary per parent step carrying
//                   G4QuasiOpticalData via G4VAuxiliaryTrackInformation. The
//                   tokens are routed to sub-event workers; on the
//                   QuasiOpticalPhoton step, this SteppingAction extracts the
//                   aux info and builds the genstep from it. The parent-step
//                   intercept is skipped (would double-count).
//
// Sub-event mode wiring lives in GPURaytraceSubEvt.cpp (main +
// ActionInitialization).

#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>

#include "G4AutoLock.hh"
#include "G4BooleanSolid.hh"
#include "G4Cerenkov.hh"
#include "G4CerenkovQuasiTrackInfo.hh"
#include "G4Electron.hh"
#include "G4Event.hh"
#include "G4GDMLParser.hh"
#include "G4LogicalVolumeStore.hh"
#include "G4Material.hh"
#include "G4MaterialPropertiesTable.hh"
#include "G4OpBoundaryProcess.hh"
#include "G4OpticalPhoton.hh"
#include "G4PhysicalConstants.hh"
#include "G4PrimaryParticle.hh"
#include "G4PrimaryVertex.hh"
#include "G4QuasiCerenkov.hh"
#include "G4QuasiOpticalData.hh"
#include "G4QuasiOpticalPhoton.hh"
#include "G4QuasiScintillation.hh"
#include "G4RunManager.hh"
#include "G4RunManagerFactory.hh"
#include "G4SDManager.hh"
#include "G4Scintillation.hh"
#include "G4ScintillationQuasiTrackInfo.hh"
#include "G4SubtractionSolid.hh"
#include "G4SystemOfUnits.hh"
#include "G4Threading.hh"
#include "G4ThreeVector.hh"
#include "G4Track.hh"
#include "G4TrackStatus.hh"
#include "G4UserEventAction.hh"
#include "G4UserRunAction.hh"
#include "G4UserSteppingAction.hh"
#include "G4UserTrackingAction.hh"
#include "G4VAuxiliaryTrackInformation.hh"
#include "G4VPhysicalVolume.hh"
#include "G4VProcess.hh"
#include "G4VUserDetectorConstruction.hh"
#include "G4VUserPrimaryGeneratorAction.hh"

#include "g4cx/G4CXOpticks.hh"
#include "sysrap/NP.hh"
#include "sysrap/OpticksGenstep.h"
#include "sysrap/SEvt.hh"
#include "sysrap/STrackInfo.h"
#include "sysrap/scerenkov.h"
#include "sysrap/spho.h"
#include "sysrap/sphoton.h"
#include "sysrap/sscint.h"
#include "u4/U4.hh"
#include "u4/U4Random.hh"
#include "u4/U4StepPoint.hh"
#include "u4/U4Touchable.h"
#include "u4/U4Track.h"

namespace
{
G4Mutex genstep_mutex = G4MUTEX_INITIALIZER;
}

bool IsSubtractionSolid(G4VSolid *solid)
{
    if (!solid)
        return false;

    if (dynamic_cast<G4SubtractionSolid *>(solid))
        return true;

    G4BooleanSolid *booleanSolid = dynamic_cast<G4BooleanSolid *>(solid);
    if (booleanSolid)
    {
        G4VSolid *solidA = booleanSolid->GetConstituentSolid(0);
        G4VSolid *solidB = booleanSolid->GetConstituentSolid(1);

        if (IsSubtractionSolid(solidA) || IsSubtractionSolid(solidB))
            return true;
    }

    return false;
}

std::string str_tolower(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
    return s;
}

// ============================================================================
// Genstep construction from G4QuasiOpticalPhoton aux info (CAPTURE_TOKEN mode)
//
// In photon-route + stackPhotons=true, G4QuasiCerenkov::PostStepDoIt creates
// a single G4QuasiOpticalPhoton secondary per parent step, positioned at the
// parent's pre-step (x0, t0) and decorated with G4CerenkovQuasiTrackInfo /
// G4ScintillationQuasiTrackInfo via G4VAuxiliaryTrackInformation. The
// G4QuasiOpticalData payload is everything we need to rebuild the eic-opticks
// genstep (quad6) without the original parent G4Track/G4Step in hand.
//
// Field mapping is mechanical: every quad6 field has a corresponding source
// in the aux info or on the token track itself. See sysrap/scerenkov.h and
// sysrap/sscint.h for the target layout.
//
// The PDG encoding (ck->code / sc->code) is not consumed by Opticks's GPU
// kernel — left as 0.
// ============================================================================

static quad6 MakeGenstep_QuasiCerenkov(const G4Step *aStep, const G4Track *token,
                                       const G4CerenkovQuasiTrackInfo *info)
{
    G4QuasiOpticalData data = info->GetQuasiOpticalData();

    // The QuasiOpticalPhoton secondary is born at the parent's pre-step (x0,t0)
    // per G4QuasiCerenkov::PostStepDoIt line 273 (`new G4Track(quasiPhoton, t0, x0)`).
    // By the time UserSteppingAction fires, token->GetPosition() reflects the
    // POST-step of the token's first step, not its birth position. Read birth
    // values from aStep->GetPreStepPoint() instead.
    G4ThreeVector pos = aStep->GetPreStepPoint()->GetPosition();
    G4double time = aStep->GetPreStepPoint()->GetGlobalTime();

    G4double pre_velocity = data.pre_velocity;
    G4double post_velocity = data.pre_velocity + data.delta_velocity;
    G4double mean_velocity = 0.5 * (pre_velocity + post_velocity);
    G4double beta = mean_velocity / c_light;
    G4double BetaInverse = beta > 0 ? 1.0 / beta : 0.0;

    // Pmin / Pmax / nMax derived from the parent material's RINDEX vector.
    const G4MaterialTable *matTable = G4Material::GetMaterialTable();
    const G4Material *mat = (data.mat_index < matTable->size()) ? (*matTable)[data.mat_index] : nullptr;
    G4double Pmin = 0.0, Pmax = 0.0, nMax = 1.0;
    if (mat)
    {
        G4MaterialPropertiesTable *MPT = mat->GetMaterialPropertiesTable();
        if (MPT)
        {
            G4MaterialPropertyVector *Rindex = MPT->GetProperty(kRINDEX);
            if (Rindex && Rindex->GetVectorLength() > 0)
            {
                Pmin = Rindex->Energy(0);
                Pmax = Rindex->GetMaxEnergy();
                nMax = Rindex->GetMaxValue();
            }
        }
    }
    G4double maxCos = (nMax > 0) ? BetaInverse / nMax : 0.0;
    G4double maxSin2 = (1.0 - maxCos) * (1.0 + maxCos);
    G4double Wmin_nm = Pmax > 0 ? h_Planck * c_light / Pmax / nm : 0.0;
    G4double Wmax_nm = Pmin > 0 ? h_Planck * c_light / Pmin / nm : 0.0;

    quad6 gs;
    gs.zero();
    scerenkov *ck = (scerenkov *)(&gs);

    ck->gentype = OpticksGenstep_G4Cerenkov_modified;
    ck->trackid = token->GetParentID();
    ck->matline = data.mat_index + SEvt::G4_INDEX_OFFSET;
    ck->numphoton = data.num_photons;

    ck->pos.x = pos.x();
    ck->pos.y = pos.y();
    ck->pos.z = pos.z();
    ck->time = time;

    ck->DeltaPosition.x = data.delta_position.x();
    ck->DeltaPosition.y = data.delta_position.y();
    ck->DeltaPosition.z = data.delta_position.z();
    ck->step_length = data.step_length;

    ck->code = 0;
    ck->charge = data.charge;
    ck->weight = token->GetWeight();
    ck->preVelocity = pre_velocity;

    ck->BetaInverse = BetaInverse;
    ck->Wmin = Wmin_nm;
    ck->Wmax = Wmax_nm;
    ck->maxCos = maxCos;

    ck->maxSin2 = maxSin2;
    ck->MeanNumberOfPhotons1 = info->GetPreNumPhotons();
    ck->MeanNumberOfPhotons2 = info->GetPostNumPhotons();
    ck->postVelocity = post_velocity;
    return gs;
}

static quad6 MakeGenstep_QuasiScintillation(const G4Step *aStep, const G4Track *token,
                                            const G4ScintillationQuasiTrackInfo *info)
{
    G4QuasiOpticalData data = info->GetQuasiOpticalData();

    // See note in MakeGenstep_QuasiCerenkov on why we read pos/time from aStep
    // pre-step rather than token->GetPosition().
    G4ThreeVector pos = aStep->GetPreStepPoint()->GetPosition();
    G4double time = aStep->GetPreStepPoint()->GetGlobalTime();

    G4double pre_velocity = data.pre_velocity;
    G4double post_velocity = data.pre_velocity + data.delta_velocity;
    G4double mean_velocity = 0.5 * (pre_velocity + post_velocity);

    quad6 gs;
    gs.zero();
    sscint *sc = (sscint *)(&gs);

    sc->gentype = OpticksGenstep_DsG4Scintillation_r4695;
    sc->trackid = token->GetParentID();
    sc->matline = data.mat_index + SEvt::G4_INDEX_OFFSET;
    sc->numphoton = data.num_photons;

    sc->pos.x = pos.x();
    sc->pos.y = pos.y();
    sc->pos.z = pos.z();
    sc->time = time;

    sc->DeltaPosition.x = data.delta_position.x();
    sc->DeltaPosition.y = data.delta_position.y();
    sc->DeltaPosition.z = data.delta_position.z();
    sc->step_length = data.step_length;

    sc->code = 0;
    sc->charge = data.charge;
    sc->weight = token->GetWeight();
    sc->meanVelocity = mean_velocity;

    sc->scnt = 1;
    sc->ScintillationTime = info->GetScintTime();
    return gs;
}

// Iterate the auxiliary track info map and dispatch to the right genstep
// builder. Returns true if a genstep was extracted and pushed.
static bool ExtractAndPushQuasiGenstep(const G4Step *aStep)
{
    const G4Track *token = aStep->GetTrack();
    auto *aux_map = token->GetAuxiliaryTrackInformationMap();
    if (!aux_map)
        return false;

    for (auto const &[modelId, aux] : *aux_map)
    {
        if (auto *cInfo = G4CerenkovQuasiTrackInfo::Cast(aux))
        {
            quad6 gs = MakeGenstep_QuasiCerenkov(aStep, token, cInfo);
            SEvt::AddGenstep(gs);
            return true;
        }
        if (auto *sInfo = G4ScintillationQuasiTrackInfo::Cast(aux))
        {
            quad6 gs = MakeGenstep_QuasiScintillation(aStep, token, sInfo);
            SEvt::AddGenstep(gs);
            return true;
        }
    }
    return false;
}

struct PhotonHit : public G4VHit
{
    PhotonHit() = default;

    PhotonHit(unsigned id, G4double energy, G4double time, G4ThreeVector position, G4ThreeVector direction,
              G4ThreeVector polarization) :
        fid(id),
        fenergy(energy),
        ftime(time),
        fposition(position),
        fdirection(direction),
        fpolarization(polarization)
    {
    }

    PhotonHit(const PhotonHit &right) :
        G4VHit(right),
        fid(right.fid),
        fenergy(right.fenergy),
        ftime(right.ftime),
        fposition(right.fposition),
        fdirection(right.fdirection),
        fpolarization(right.fpolarization)
    {
    }

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

    G4bool operator==(const PhotonHit &right) const
    {
        return (this == &right);
    }

    void Print() override
    {
        G4cout << "Detector id: " << fid << " energy: " << fenergy << " nm" << " time: " << ftime << " ns"
               << " position: " << fposition << " direction: " << fdirection << " polarization: " << fpolarization
               << G4endl;
    }

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
    PhotonSD(G4String name) :
        G4VSensitiveDetector(name),
        fHCID(-1)
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

        PhotonHit *newHit = new PhotonHit(
            0, theEnergy, theTrack->GetGlobalTime(), aStep->GetPostStepPoint()->GetPosition(),
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

  private:
    PhotonHitsCollection *fPhotonHitsCollection{nullptr};
    G4int fHCID;
};

struct DetectorConstruction : G4VUserDetectorConstruction
{
    DetectorConstruction(std::filesystem::path gdml_file) :
        gdml_file_(gdml_file)
    {
    }

    G4VPhysicalVolume *Construct() override
    {
        parser_.Read(gdml_file_.string(), false);
        G4VPhysicalVolume *world = parser_.GetWorldVolume();

        G4CXOpticks::SetGeometry(world);
        G4LogicalVolumeStore *lvStore = G4LogicalVolumeStore::GetInstance();

        static G4VisAttributes invisibleVisAttr(false);

        if (lvStore && !lvStore->empty())
        {
            for (auto &logicalVolume : *lvStore)
            {
                G4VSolid *solid = logicalVolume->GetSolid();

                if (IsSubtractionSolid(solid))
                {
                    logicalVolume->SetVisAttributes(&invisibleVisAttr);
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

    PrimaryGenerator(SEvt *sev) :
        sev(sev)
    {
    }

    void GeneratePrimaries(G4Event *event) override
    {
        G4ThreeVector position_mm(0.0 * m, 0.0 * m, 0.0 * m);
        G4double time_ns = 0;
        G4ThreeVector direction(0, 0.2, 0.8);

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

    EventAction(SEvt *sev) :
        sev(sev)
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

    RunAction(EventAction *eventAction) :
        fEventAction(eventAction)
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
            std::chrono::duration<double> elapsed = end - start;
            std::cout << "Simulation time: " << elapsed.count() << " seconds" << std::endl;

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
                outFile << hit.time << " " << hit.wavelength << "  " << "(" << position.x() << ", " << position.y()
                        << ", " << position.z() << ")  " << "(" << direction.x() << ", " << direction.y() << ", "
                        << direction.z() << ")  " << "(" << polarization.x() << ", " << polarization.y() << ", "
                        << polarization.z() << ")  " << "CreationProcessID=" << theCreationProcessid << std::endl;
            }

            outFile.close();
        }
    }
};

struct SteppingAction : G4UserSteppingAction
{
    enum CaptureMode
    {
        CAPTURE_PARENT, // genstep captured at the charged-parent post-step
        CAPTURE_TOKEN,  // genstep captured on the G4QuasiOpticalPhoton step
    };

    SEvt *sev;
    CaptureMode fCaptureMode;

    SteppingAction(SEvt *sev, CaptureMode mode = CAPTURE_PARENT) :
        sev(sev),
        fCaptureMode(mode)
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
            if (aStep->GetTrack()->GetCurrentStepNumber() > 10000)
            {
                aStep->GetTrack()->SetTrackStatus(fStopAndKill);
            }
        }

        // G4QuasiCerenkov / G4QuasiScintillation create a G4QuasiOpticalPhoton
        // secondary per parent step (carrying the burst metadata as aux info).
        if (aStep->GetTrack()->GetDefinition() == G4QuasiOpticalPhoton::Definition())
        {
            if (fCaptureMode == CAPTURE_TOKEN)
            {
                // Photon-route: extract the burst metadata from auxiliary track
                // information, build the genstep, push it into SEvt. Lock since
                // SEvt is process-wide and multiple sub-event workers may step
                // tokens concurrently.
                //
                // KNOWN LIMITATION: in --mode subevt, the auxiliary track
                // information is not preserved across the master->worker
                // sub-event handoff (G4 11.4.1 sub-event marshalling appears
                // not to copy the aux map when re-instantiating tracks on the
                // worker). aux_map_size observed as 0 on workers in subevt
                // mode; ExtractAndPushQuasiGenstep returns false and no
                // genstep is captured. Works correctly in --mode serial and
                // --mode event-mt where no marshalling occurs.
                G4AutoLock lock(&genstep_mutex);
                bool ok = ExtractAndPushQuasiGenstep(aStep);
                static thread_local bool quasi_token_dump_done = false;
                if (!quasi_token_dump_done)
                {
                    auto *aux_map = aStep->GetTrack()->GetAuxiliaryTrackInformationMap();
                    G4cout << "[GPURaytraceSubEvt] tid=" << G4Threading::G4GetThreadId()
                           << " first QuasiOpticalPhoton seen: ok=" << ok
                           << " aux_map_size=" << (aux_map ? aux_map->size() : 0) << G4endl;
                    quasi_token_dump_done = true;
                }
            }
            // In both modes the token has no further purpose — kill it.
            aStep->GetTrack()->SetTrackStatus(fStopAndKill);
            return;
        }

        if (volume && volume->GetName() == "MirrorPyramid")
        {
            aTrack = aStep->GetTrack();
            if (aTrack->GetDefinition() != G4OpticalPhoton::OpticalPhotonDefinition())
            {
                aTrack->SetTrackStatus(fStopAndKill);
            }
        }

        // In CAPTURE_TOKEN mode the genstep is captured at the QuasiOpticalPhoton
        // step (above); skip the parent-step intercept to avoid double-counting.
        if (fCaptureMode == CAPTURE_TOKEN)
            return;

        G4SteppingManager *fpSteppingManager =
            G4EventManager::GetEventManager()->GetTrackingManager()->GetSteppingManager();
        G4StepStatus stepStatus = fpSteppingManager->GetfStepStatus();

        if (stepStatus != fAtRestDoItProc)
        {
            G4ProcessVector *procPost = fpSteppingManager->GetfPostStepDoItVector();
            size_t MAXofPostStepLoops = fpSteppingManager->GetMAXofPostStepLoops();
            for (size_t i3 = 0; i3 < MAXofPostStepLoops; i3++)
            {
                const G4String &procName = (*procPost)[i3]->GetProcessName();
                // Once per worker, dump the optical processes we see to verify the Quasi
                // variants are installed AND to expose the worker thread id so that
                // sub-event mode firing on >=2 distinct workers is observable.
                static thread_local bool quasi_dump_done = false;
                if (!quasi_dump_done &&
                    (procName == "Cerenkov" || procName == "QuasiCerenkov" ||
                     procName == "Scintillation" || procName == "QuasiScintillation" ||
                     procName == "QausiScintillation"))
                {
                    G4cout << "[GPURaytraceSubEvt] tid=" << G4Threading::G4GetThreadId()
                           << " post-step process found: " << procName << G4endl;
                    if (procName == "Cerenkov" || procName == "Scintillation")
                    {
                        G4cout << "[GPURaytraceSubEvt] WARNING: legacy " << procName
                               << " process is registered — offload swap may not have taken effect."
                               << G4endl;
                    }
                }

                if (procName == "QuasiCerenkov")
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

                    G4QuasiCerenkov *proc = (G4QuasiCerenkov *)(*procPost)[i3];
                    fNumPhotons = proc->GetNumPhotons();

                    G4AutoLock lock(&genstep_mutex);

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
                        if (!quasi_dump_done)
                        {
                            G4cout << "[GPURaytraceSubEvt] tid=" << G4Threading::G4GetThreadId()
                                   << " first QuasiCerenkov genstep captured: N=" << fNumPhotons << G4endl;
                            quasi_dump_done = true;
                        }
                    }
                }
                if (procName == "QuasiScintillation" || procName == "QausiScintillation")
                {
                    G4QuasiScintillation *proc1 = (G4QuasiScintillation *)(*procPost)[i3];
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
                        if (!quasi_dump_done)
                        {
                            G4cout << "[GPURaytraceSubEvt] tid=" << G4Threading::G4GetThreadId()
                                   << " first QuasiScintillation genstep captured: N=" << fNumPhotons << G4endl;
                            quasi_dump_done = true;
                        }
                    }
                }
            }
        }
    }
};

struct TrackingAction : G4UserTrackingAction
{
    SEvt *sev;

    TrackingAction(SEvt *sev) :
        sev(sev)
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
    G4App(std::filesystem::path gdml_file, SteppingAction::CaptureMode capture_mode) :
        sev(SEvt::CreateOrReuse_EGPU()),
        det_cons_(new DetectorConstruction(gdml_file)),
        prim_gen_(new PrimaryGenerator(sev)),
        event_act_(new EventAction(sev)),
        run_act_(new RunAction(event_act_)),
        stepping_(new SteppingAction(sev, capture_mode)),
        tracking_(new TrackingAction(sev))
    {
    }

    SEvt *sev;

    G4VUserDetectorConstruction *det_cons_;
    G4VUserPrimaryGeneratorAction *prim_gen_;
    EventAction *event_act_;
    RunAction *run_act_;
    SteppingAction *stepping_;
    TrackingAction *tracking_;
};
