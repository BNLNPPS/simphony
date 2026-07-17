#include <cstring>
#include <filesystem>
#include <type_traits>
#include <utility>
#include <vector>

#include "G4BooleanSolid.hh"
#include "G4Event.hh"
#include "G4GDMLParser.hh"
#include "G4LogicalVolumeStore.hh"
#include "G4OpBoundaryProcess.hh"
#include "G4OpticalPhoton.hh"
#include "G4PhysicalConstants.hh"
#include "G4PrimaryParticle.hh"
#include "G4PrimaryVertex.hh"
#include "G4SDManager.hh"
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
#include "u4/U4Random.hh"
#include "u4/U4StepPoint.hh"
#include "u4/U4Touchable.h"
#include "u4/U4Track.h"

#include "config.h"
#include "torch.h"

struct PhotonHit : public G4VHit
{
    PhotonHit() = default;

    PhotonHit(G4double energy, G4double time, G4ThreeVector position, G4ThreeVector direction,
              G4ThreeVector polarization) :
        photon()
    {
        photon.pos = {static_cast<float>(position.x()), static_cast<float>(position.y()),
                      static_cast<float>(position.z())};
        photon.time = time;
        photon.mom = {static_cast<float>(direction.x()), static_cast<float>(direction.y()),
                      static_cast<float>(direction.z())};
        photon.pol = {static_cast<float>(polarization.x()), static_cast<float>(polarization.y()),
                      static_cast<float>(polarization.z())};
        photon.wavelength = h_Planck * c_light / (energy * CLHEP::eV);
    }

    void Print() override
    {
        G4cout << photon << G4endl;
    }

    sphoton photon;
};

using PhotonHitsCollection = G4THitsCollection<PhotonHit>;

// NumPy hit arrays use the sphoton (4, 4) float layout.
static_assert(sizeof(sphoton) == 16 * sizeof(float));
static_assert(std::is_trivially_copyable_v<sphoton>);

struct PhotonSD : public G4VSensitiveDetector
{
    PhotonHitsCollection* photon_hit_collection{nullptr};
    G4int                 fHCID;

    PhotonSD(G4String name) :
        G4VSensitiveDetector(name),
        fHCID(-1)
    {
        G4String HCname = name + "_HC";
        collectionName.insert(HCname);
        G4cout << "PhotonSD::PhotonSD: name: " << name << ", collection: " << HCname << ", size: " << collectionName.size() << G4endl;
    }

    void Initialize(G4HCofThisEvent* hce) override
    {
        photon_hit_collection = new PhotonHitsCollection(SensitiveDetectorName, collectionName[0]);
        if (fHCID < 0)
        {
            G4cout << "PhotonSD::Initialize:  " << SensitiveDetectorName << "   " << collectionName[0] << G4endl;
            fHCID = G4SDManager::GetSDMpointer()->GetCollectionID(collectionName[0]);
        }
        hce->AddHitsCollection(fHCID, photon_hit_collection);
    }

    G4bool ProcessHits(G4Step* aStep, G4TouchableHistory*) override
    {
        G4Track* track = aStep->GetTrack();

        // Only process optical photons
        if (track->GetDefinition() != G4OpticalPhoton::OpticalPhotonDefinition())
            return false;

        // Create a new hit
        PhotonHit* hit = new PhotonHit(
            track->GetTotalEnergy(), track->GetGlobalTime(), aStep->GetPostStepPoint()->GetPosition(),
            aStep->GetPostStepPoint()->GetMomentumDirection(), aStep->GetPostStepPoint()->GetPolarization());

        photon_hit_collection->insert(hit);
        track->SetTrackStatus(fStopAndKill);

        return true;
    }

    void EndOfEvent(G4HCofThisEvent*) override
    {
        G4int num_g4_hits = photon_hit_collection->entries();
        G4cout << "PhotonSD::EndOfEvent: number of Geant4 hits: " << num_g4_hits << G4endl;
    }
};

struct DetectorConstruction : G4VUserDetectorConstruction
{
    std::filesystem::path gdml_file_;
    G4GDMLParser          parser_;

    DetectorConstruction(std::filesystem::path gdml_file) :
        gdml_file_(std::move(gdml_file))
    {
    }

    G4VPhysicalVolume* Construct() override
    {
        parser_.Read(gdml_file_.string(), false);
        G4VPhysicalVolume* world = parser_.GetWorldVolume();

        G4CXOpticks::SetGeometry(world);

        return world;
    }

    void ConstructSDandField() override
    {
        G4SDManager* SDman = G4SDManager::GetSDMpointer();

        const G4GDMLAuxMapType* auxmap = parser_.GetAuxMap();
        for (auto const& [logVol, listType] : *auxmap)
        {
            for (auto const& auxtype : listType)
            {
                if (auxtype.type == "SensDet")
                {
                    G4cout << "DetectorConstruction::ConstructSDandField: Attach sensitive detector to logical volume: " << logVol->GetName() << G4endl;
                    G4String  name = logVol->GetName() + "_PhotonDetector";
                    PhotonSD* aPhotonSD = new PhotonSD(name);
                    SDman->AddNewDetector(aPhotonSD);
                    logVol->SetSensitiveDetector(aPhotonSD);
                }
            }
        }
    }
};

struct PrimaryGenerator : G4VUserPrimaryGeneratorAction
{
    simphony::Config cfg;
    SEvt*            sev;

    PrimaryGenerator(const simphony::Config& cfg, SEvt* sev) :
        cfg(cfg),
        sev(sev)
    {
    }

    void GeneratePrimaries(G4Event* event) override
    {
        std::vector<sphoton> sphotons = generate_photons(cfg.torch);

        size_t num_floats = sphotons.size() * 4 * 4;
        float* data = reinterpret_cast<float*>(sphotons.data());
        NP*    photons = NP::MakeFromValues<float>(data, num_floats);

        photons->reshape({static_cast<int64_t>(sphotons.size()), 4, 4});

        for (const sphoton& p : sphotons)
        {
            G4ThreeVector position_mm(p.pos.x, p.pos.y, p.pos.z);
            G4double      time_ns = p.time;
            G4ThreeVector direction(p.mom.x, p.mom.y, p.mom.z);
            G4double      wavelength_nm = p.wavelength;
            G4ThreeVector polarization(p.pol.x, p.pol.y, p.pol.z);

            G4PrimaryVertex* vertex = new G4PrimaryVertex(position_mm, time_ns);
            G4double         kineticEnergy = h_Planck * c_light / (wavelength_nm * nm);

            G4PrimaryParticle* particle = new G4PrimaryParticle(G4OpticalPhoton::Definition());
            particle->SetKineticEnergy(kineticEnergy);
            particle->SetMomentumDirection(direction);
            particle->SetPolarization(polarization);

            vertex->SetPrimary(particle);
            event->AddPrimaryVertex(vertex);
        }

        sev->SetInputPhoton(photons);
    }
};

struct EventAction : G4UserEventAction
{
    simphony::Config     cfg;
    SEvt*                sev;
    std::vector<sphoton> g4_hits;
    std::vector<sphoton> gpu_hits;

    EventAction(const simphony::Config& cfg, SEvt* sev) :
        cfg(cfg),
        sev(sev)
    {
    }

    void BeginOfEventAction(const G4Event* event) override
    {
        sev->beginOfEvent(event->GetEventID());
    }

    void ClearHits()
    {
        g4_hits.clear();
        gpu_hits.clear();
    }

    size_t CollectGPUHits(SEvt* sev_gpu)
    {
        const size_t num_gpu_hits = sev_gpu->getNumHit();
        const size_t offset = gpu_hits.size();
        gpu_hits.resize(offset + num_gpu_hits);

        for (size_t idx = 0; idx < num_gpu_hits; idx++)
            sev_gpu->getHit(gpu_hits[offset + idx], idx);

        return num_gpu_hits;
    }

    size_t CollectG4Hits(const G4Event* event)
    {
        G4HCofThisEvent* hce = event->GetHCofThisEvent();
        size_t           num_g4_hits = 0;

        if (hce)
        {
            for (G4int i = 0; i < hce->GetNumberOfCollections(); i++)
            {
                PhotonHitsCollection* hc = dynamic_cast<PhotonHitsCollection*>(hce->GetHC(i));
                if (hc)
                    num_g4_hits += static_cast<size_t>(hc->entries());
            }
        }

        g4_hits.reserve(g4_hits.size() + num_g4_hits);

        if (hce)
        {
            for (G4int i = 0; i < hce->GetNumberOfCollections(); i++)
            {
                PhotonHitsCollection* hc = dynamic_cast<PhotonHitsCollection*>(hce->GetHC(i));
                if (!hc)
                    continue;

                for (PhotonHit* hit : *hc->GetVector())
                    g4_hits.push_back(hit->photon);
            }
        }

        return num_g4_hits;
    }

    void SaveHits(const std::vector<sphoton>& source, const char* name) const
    {
        NP* hits = NP::Make<float>(source.size(), 4, 4);
        if (!source.empty())
            std::memcpy(hits->bytes(), source.data(), source.size() * sizeof(sphoton));

        hits->save(cfg.output_dir.string().c_str(), name);
        delete hits;
    }

    void SaveRunHits() const
    {
        SaveHits(gpu_hits, "s_hits.npy");
        SaveHits(g4_hits, "g_hits.npy");
    }

    void EndOfEventAction(const G4Event* event) override
    {
        int eventID = event->GetEventID();
        sev->addEventConfigArray();
        sev->gather();
        sev->endOfEvent(eventID);

        // GPU-based simulation
        G4CXOpticks* gx = G4CXOpticks::Get();

        gx->simulate(eventID, false);
        cudaDeviceSynchronize();

        SEvt*  sev_gpu = SEvt::Get_EGPU();
        size_t num_hits_gpu = sev_gpu->getNumHit();
        size_t num_hits_cpu = sev->getNumHit();

        G4cout << "EventAction::EndOfEventAction: GPU hits:  " << num_hits_gpu << G4endl;
        G4cout << "EventAction::EndOfEventAction: CPU hits:  " << num_hits_cpu << G4endl;

        // Append the event-wide GPU buffer and all Geant4 hit collections to
        // run-scoped buffers before the event data is reset by either backend.
        size_t collected_gpu_hits = CollectGPUHits(sev_gpu);
        size_t collected_g4_hits = CollectG4Hits(event);
        G4cout << "EventAction::EndOfEventAction: Collected GPU hits: " << collected_gpu_hits << G4endl;
        G4cout << "EventAction::EndOfEventAction: Collected G4  hits: " << collected_g4_hits << G4endl;

        gx->reset(eventID);
    }
};

struct SteppingAction : G4UserSteppingAction
{
    SEvt* sev;

    SteppingAction(SEvt* sev) :
        sev(sev)
    {
    }

    void UserSteppingAction(const G4Step* step)
    {
        if (step->GetTrack()->GetDefinition() != G4OpticalPhoton::OpticalPhotonDefinition())
            return;

        const G4Track*      track = step->GetTrack();
        const G4VTouchable* touch = track->GetTouchable();

        const spho* label = STrackInfo::GetRef(track);
        assert(label && label->isDefined() && "all photons are expected to be labelled");
        spho ulabel = *label;

        const G4StepPoint* pre = step->GetPreStepPoint();
        const G4StepPoint* post = step->GetPostStepPoint();

        sev->checkPhotonLineage(ulabel);

        sphoton& current_photon = sev->current_ctx.p;

        if (current_photon.flagmask_count() == 1)
        {
            U4StepPoint::Update(current_photon, pre); // populate current_photon with pos, mom, pol, time, wavelength
            sev->pointPhoton(ulabel);                 // copying current into buffers
        }

        bool     tir;
        unsigned flag = U4StepPoint::Flag<G4OpBoundaryProcess>(post, true, tir);
        bool     is_detect_flag = OpticksPhoton::IsSurfaceDetectFlag(flag);

        const int touch_depth = touch ? touch->GetHistoryDepth() : 0;
        current_photon.hitcount_iindex =
            touch_depth > 1
                ? (is_detect_flag ? U4Touchable::ImmediateReplicaNumber(touch)
                                  : U4Touchable::AncestorReplicaNumber(touch))
                : 0;

        U4StepPoint::Update(current_photon, post);

        current_photon.set_flag(flag);

        sev->pointPhoton(ulabel);
    }
};

struct TrackingAction : G4UserTrackingAction
{
    const G4Track* transient_suspend_track = nullptr;
    SEvt*          sev;

    TrackingAction(SEvt* sev) :
        sev(sev)
    {
    }

    void PreUserTrackingAction_Optical_FabricateLabel(const G4Track* track)
    {
        U4Track::SetFabricatedLabel(track);
        spho* label = STrackInfo::GetRef(track);
        assert(label);
    }

    void PreUserTrackingAction(const G4Track* track) override
    {
        if (track->GetDefinition() == G4OpticalPhoton::OpticalPhotonDefinition())
        {
            // Geant4 boundary updates optical velocity via ProposeVelocity, but the
            // track must honor the given velocity for post-boundary timing to match.
            G4Track* mutable_track = const_cast<G4Track*>(track);
            mutable_track->UseGivenVelocity(true);
        }

        if (!STrackInfo::Exists(track))
            PreUserTrackingAction_Optical_FabricateLabel(track);

        const spho* label = STrackInfo::GetRef(track);
        assert(label && label->isDefined() && "all photons are expected to be labelled");
        spho ulabel = *label;

        U4Random::SetSequenceIndex(ulabel.id);

        bool resume_fSuspend = track == transient_suspend_track;

        if (ulabel.gen() == 0)
        {
            if (resume_fSuspend == false)
                sev->beginPhoton(ulabel);
            else
                sev->resumePhoton(ulabel);
        }
        else if (ulabel.gen() > 0)
        {
            if (resume_fSuspend == false)
                sev->rjoinPhoton(ulabel);
            else
                sev->rjoin_resumePhoton(ulabel);
        }
    }

    void PostUserTrackingAction(const G4Track* track) override
    {
        G4TrackStatus tstat = track->GetTrackStatus();

        bool is_stop_and_kill = tstat == fStopAndKill;
        bool is_suspend = tstat == fSuspend;
        bool is_stop_and_kill_or_suspend = is_stop_and_kill || is_suspend;

        assert(is_stop_and_kill_or_suspend);

        const spho* label = STrackInfo::GetRef(track);
        assert(label && label->isDefined() && "all photons are expected to be labelled");
        spho ulabel = *label;

        if (is_stop_and_kill)
        {
            U4Random::SetSequenceIndex(-1);
            sev->finalPhoton(ulabel);
            transient_suspend_track = nullptr;
        }
        else if (is_suspend)
        {
            transient_suspend_track = track;
        }
    }
};

struct RunAction : G4UserRunAction
{
    EventAction* event_action;

    RunAction(EventAction* eventAction) :
        event_action(eventAction)
    {
    }

    void BeginOfRunAction(const G4Run*) override
    {
        event_action->ClearHits();
    }

    void EndOfRunAction(const G4Run*) override
    {
        event_action->SaveRunHits();
        G4cout << "RunAction::EndOfRunAction: Total GPU hits: " << event_action->gpu_hits.size() << G4endl;
        G4cout << "RunAction::EndOfRunAction: Total G4  hits: " << event_action->g4_hits.size() << G4endl;
    }
};

struct G4App
{
    G4App(const simphony::Config& cfg, std::filesystem::path gdml_file) :
        sev(SEvt::CreateOrReuse_ECPU()),
        det_cons_(new DetectorConstruction(gdml_file)),
        prim_gen_(new PrimaryGenerator(cfg, sev)),
        event_act_(new EventAction(cfg, sev)),
        run_act_(new RunAction(event_act_)),
        stepping_(new SteppingAction(sev)),
        tracking_(new TrackingAction(sev))
    {
    }

    SEvt* sev;

    G4VUserDetectorConstruction*   det_cons_;
    G4VUserPrimaryGeneratorAction* prim_gen_;
    EventAction*                   event_act_;
    RunAction*                     run_act_;
    SteppingAction*                stepping_;
    TrackingAction*                tracking_;
};
