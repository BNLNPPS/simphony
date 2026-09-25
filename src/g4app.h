#pragma once

#include <cassert>
#include <condition_variable>
#include <cstring>
#include <cmath>
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include <cuda_runtime_api.h>

#include "G4BooleanSolid.hh"
#include "G4Cerenkov.hh"
#include "G4DynamicParticle.hh"
#include "G4Event.hh"
#include "G4Exception.hh"
#include "G4EventManager.hh"
#include "G4GDMLParser.hh"
#include "G4LogicalVolumeStore.hh"
#include "G4OpBoundaryProcess.hh"
#include "G4OpticalPhoton.hh"
#include "G4ParticleTable.hh"
#include "G4PhysicalConstants.hh"
#include "G4PrimaryParticle.hh"
#include "G4PrimaryVertex.hh"
#include "G4Run.hh"
#include "G4RunManager.hh"
#include "G4SDManager.hh"
#include "G4Scintillation.hh"
#include "G4SteppingManager.hh"
#include "G4SubtractionSolid.hh"
#include "G4SystemOfUnits.hh"
#include "G4Threading.hh"
#include "G4ThreeVector.hh"
#include "G4Track.hh"
#include "G4TrackStatus.hh"
#include "G4TrackVector.hh"
#include "G4TrackingManager.hh"
#include "G4UserEventAction.hh"
#include "G4UserRunAction.hh"
#include "G4UserSteppingAction.hh"
#include "G4UserTrackingAction.hh"
#include "G4VPhysicalVolume.hh"
#include "G4VProcess.hh"
#include "G4VUserActionInitialization.hh"
#include "G4VUserDetectorConstruction.hh"
#include "G4VUserEventInformation.hh"
#include "G4VUserPrimaryGeneratorAction.hh"

#include "g4cx/G4CXOpticks.hh"
#include "sysrap/NP.hh"
#include "sysrap/SEvt.hh"
#include "sysrap/STrackInfo.h"
#include "sysrap/spho.h"
#include "sysrap/sphoton.h"
#include "u4/U4Random.hh"
#include "u4/U4.hh"
#include "u4/U4StepPoint.hh"
#include "u4/U4Touchable.h"
#include "u4/U4Track.h"

#include "config.h"
#include "sysrap/EventTiming.hh"
#include "torch.h"

struct PrimaryParticleConfig
{
    bool          enabled{false};
    std::string   particle{"mu-"};
    double        momentum_gev_c{5.0};
    int           multiplicity{1};
    G4ThreeVector position_mm{0.0, 0.0, 0.0};
    G4ThreeVector direction{0.0, 0.2, 0.8};
};

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

inline std::unique_ptr<NP> MakePhotonArray(const std::vector<sphoton>& photons)
{
    const size_t        num_floats = photons.size() * 4 * 4;
    const float*        data = reinterpret_cast<const float*>(photons.data());
    std::unique_ptr<NP> array(NP::MakeFromValues<float>(data, num_floats));
    array->reshape({static_cast<int64_t>(photons.size()), 4, 4});
    return array;
}

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
        size_t       sensitive_index = 0;

        const G4GDMLAuxMapType* auxmap = parser_.GetAuxMap();
        for (auto const& [logVol, listType] : *auxmap)
        {
            for (auto const& auxtype : listType)
            {
                if (auxtype.type == "SensDet")
                {
                    G4cout << "DetectorConstruction::ConstructSDandField: Attach sensitive detector to logical volume: " << logVol->GetName() << G4endl;
                    G4String  name = logVol->GetName() + "_" + std::to_string(sensitive_index++) + "_PhotonDetector";
                    PhotonSD* aPhotonSD = new PhotonSD(name);
                    SDman->AddNewDetector(aPhotonSD);
                    logVol->SetSensitiveDetector(aPhotonSD);
                }
            }
        }
    }
};

struct PrimaryPhotonInfo : G4VUserEventInformation
{
    explicit PrimaryPhotonInfo(std::vector<sphoton> photons) :
        photons(std::move(photons))
    {
    }

    void Print() const override
    {
    }

    std::vector<sphoton> photons;
};

struct PrimaryGenerator : G4VUserPrimaryGeneratorAction
{
    simphony::Config      cfg;
    SEvt*                 sev;
    PrimaryParticleConfig primary;
    EventTimingRecorder*  timing;
    std::unique_ptr<NP>   input_photon;

    PrimaryGenerator(const simphony::Config& cfg, SEvt* sev, const PrimaryParticleConfig& primary,
                     EventTimingRecorder* timing) :
        cfg(cfg),
        sev(sev),
        primary(primary),
        timing(timing)
    {
    }

    void GeneratePrimaries(G4Event* event) override
    {
        if (timing)
            timing->BeginEvent(event->GetEventID());

        if (primary.enabled)
        {
            G4ParticleDefinition* definition = G4ParticleTable::GetParticleTable()->FindParticle(primary.particle);
            if (!definition)
                throw std::runtime_error("Unknown Geant4 particle: " + primary.particle);

            const G4double momentum = primary.momentum_gev_c * GeV;
            const G4double mass = definition->GetPDGMass();
            const G4double kinetic_energy = std::sqrt(momentum * momentum + mass * mass) - mass;
            const G4ThreeVector direction = primary.direction.unit();

            for (int i = 0; i < primary.multiplicity; ++i)
            {
                G4PrimaryVertex* vertex = new G4PrimaryVertex(primary.position_mm, 0.0);
                G4PrimaryParticle* particle = new G4PrimaryParticle(definition);
                particle->SetKineticEnergy(kinetic_energy);
                particle->SetMomentumDirection(direction);
                vertex->SetPrimary(particle);
                event->AddPrimaryVertex(vertex);
            }
            return;
        }

        std::vector<sphoton> sphotons = generate_photons(cfg.torch);

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

        // The Opticks CPU recorder is intentionally used only by the serial
        // run manager. Its event instance is process-global and cannot be
        // shared safely by Geant4 worker threads. Preserve MT event input on
        // the G4Event until the event reaches the serialized GPU section.
        if (sev)
        {
            input_photon = MakePhotonArray(sphotons);
            SEvt::SetInputPhoton(input_photon.get());
        }
        else
            event->SetUserInformation(new PrimaryPhotonInfo(std::move(sphotons)));
    }
};

struct Simg4oxRun : G4Run
{
    struct EventHits
    {
        std::vector<sphoton> gpu;
        std::vector<sphoton> g4;
    };

    std::map<G4int, EventHits> hits_by_event;

    void AddEvent(G4int event_id, std::vector<sphoton> gpu_hits, std::vector<sphoton> g4_hits)
    {
        hits_by_event.insert_or_assign(event_id, EventHits{std::move(gpu_hits), std::move(g4_hits)});
    }

    void Merge(const G4Run* run) override
    {
        const auto* local_run = static_cast<const Simg4oxRun*>(run);
        for (const auto& [event_id, hits] : local_run->hits_by_event)
            hits_by_event.emplace(event_id, hits);

        G4Run::Merge(run);
    }

    static std::vector<sphoton> Flatten(
        const std::map<G4int, EventHits>& events,
        const std::vector<sphoton> EventHits::* member)
    {
        size_t total = 0;
        for (const auto& [event_id, hits] : events)
        {
            (void)event_id;
            total += (hits.*member).size();
        }

        std::vector<sphoton> flattened;
        flattened.reserve(total);
        for (const auto& [event_id, hits] : events)
        {
            (void)event_id;
            const auto& event_hits = hits.*member;
            flattened.insert(flattened.end(), event_hits.begin(), event_hits.end());
        }
        return flattened;
    }

    std::vector<sphoton> GPUHits() const
    {
        return Flatten(hits_by_event, &EventHits::gpu);
    }
    std::vector<sphoton> G4Hits() const
    {
        return Flatten(hits_by_event, &EventHits::g4);
    }
};

struct Simg4oxSharedState
{
    std::mutex              gpu_mutex;
    std::condition_variable gpu_turn;
    std::unique_ptr<NP>     input_photon;
    G4int                   next_gpu_event{0};

    void BeginRun()
    {
        std::lock_guard lock(gpu_mutex);
        next_gpu_event = 0;
    }
};

struct EventAction : G4UserEventAction
{
    SEvt*                               sev;
    std::shared_ptr<Simg4oxSharedState> shared_state;
    bool                                order_gpu_events;
    bool                                particle_source;
    EventTimingRecorder*                timing;

    EventAction(SEvt* sev, std::shared_ptr<Simg4oxSharedState> shared_state, bool order_gpu_events,
                bool particle_source, EventTimingRecorder* timing) :
        sev(sev),
        shared_state(std::move(shared_state)),
        order_gpu_events(order_gpu_events),
        particle_source(particle_source),
        timing(timing)
    {
    }

    void BeginOfEventAction(const G4Event* event) override
    {
        // Generated optical gensteps are collected before QSim owns the EGPU
        // begin-of-event transition. The torch comparison still needs the ECPU
        // event opened before Geant4 transports its input photons.
        if (sev && !particle_source)
            sev->beginOfEvent(event->GetEventID());
    }

    static std::vector<sphoton> CollectGPUHits(SEvt* sev_gpu)
    {
        const size_t         num_gpu_hits = sev_gpu->getNumHit();
        std::vector<sphoton> gpu_hits(num_gpu_hits);

        for (size_t idx = 0; idx < num_gpu_hits; idx++)
            sev_gpu->getHit(gpu_hits[idx], idx);

        return gpu_hits;
    }

    static std::vector<sphoton> CollectG4Hits(const G4Event* event)
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

        std::vector<sphoton> g4_hits;
        g4_hits.reserve(num_g4_hits);

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

        return g4_hits;
    }

    std::vector<sphoton> SimulateOnGPU(const G4Event* event, bool has_gpu_work)
    {
        const PrimaryPhotonInfo* primary_info = nullptr;
        if (order_gpu_events)
        {
            primary_info = dynamic_cast<const PrimaryPhotonInfo*>(event->GetUserInformation());
            if (!primary_info)
            {
                G4Exception("EventAction::SimulateOnGPU", "MissingPrimaryPhotonInfo", FatalException,
                            "MT event is missing its generated photons for GPU processing");
                return {};
            }
        }

        const G4int      event_id = event->GetEventID();
        std::unique_lock lock(shared_state->gpu_mutex);
        if (order_gpu_events)
            shared_state->gpu_turn.wait(lock, [&] { return event_id == shared_state->next_gpu_event; });

        if (order_gpu_events)
        {
            shared_state->input_photon = MakePhotonArray(primary_info->photons);
            SEvt::SetInputPhoton(shared_state->input_photon.get());
        }

        if (timing)
            timing->BeginGpu();

        std::vector<sphoton> gpu_hits;
        if (has_gpu_work)
        {
            G4CXOpticks* gx = G4CXOpticks::Get();
            gx->simulate(event_id, false);
            cudaDeviceSynchronize();

            if (timing)
                timing->EndGpu();

            gpu_hits = CollectGPUHits(SEvt::Get_EGPU());
            // QSim::reset ends the EGPU event; particle-source mode must not
            // end its aliased SEvt a second time.
            gx->reset(event_id);
        }
        else if (timing)
            timing->EndGpu();

        if (order_gpu_events)
        {
            ++shared_state->next_gpu_event;
            lock.unlock();
            shared_state->gpu_turn.notify_all();
        }

        return gpu_hits;
    }

    void EndOfEventAction(const G4Event* event) override
    {
        const G4int event_id = event->GetEventID();
        std::int64_t num_gensteps = 0;
        std::int64_t num_photons = 0;
        if (particle_source)
        {
            num_gensteps = sev->getNumGenstepFromGenstep();
            num_photons = sev->getNumPhotonFromGenstep();
        }
        else if (sev)
        {
            sev->addEventConfigArray();
            sev->gather();
            num_gensteps = sev->getNumGenstepFromGenstep();
            num_photons = sev->getNumPhotonCollected();
            sev->endOfEvent(event_id);
        }

        auto g4_hits = CollectG4Hits(event);
        if (timing)
            timing->SubmitGpu(num_gensteps, num_photons);

        auto gpu_hits = SimulateOnGPU(event, !particle_source || num_gensteps > 0);

        G4cout << "EventAction::EndOfEventAction: Event " << event_id
               << ": Collected GPU hits: " << gpu_hits.size() << G4endl;
        G4cout << "EventAction::EndOfEventAction: Event " << event_id
               << ": Collected G4  hits: " << g4_hits.size() << G4endl;

        const size_t num_gpu_hits = gpu_hits.size();
        const size_t num_g4_hits = g4_hits.size();
        auto* run = static_cast<Simg4oxRun*>(G4RunManager::GetRunManager()->GetNonConstCurrentRun());
        run->AddEvent(event_id, std::move(gpu_hits), std::move(g4_hits));
        if (timing)
            timing->EndEvent(num_gpu_hits, num_g4_hits);
    }
};

struct SteppingAction : G4UserSteppingAction
{
    SEvt* sev;
    bool  collect_gensteps;

    SteppingAction(SEvt* sev, bool collect_gensteps) :
        sev(sev),
        collect_gensteps(collect_gensteps)
    {
    }

    void UserSteppingAction(const G4Step* step)
    {
        if (step->GetTrack()->GetDefinition() != G4OpticalPhoton::OpticalPhotonDefinition())
        {
            if (collect_gensteps)
                CollectGensteps(step);
            return;
        }

        const G4Track*      track = step->GetTrack();
        const G4VTouchable* touch = track->GetTouchable();

        const spho* label = STrackInfo::GetRef(track);
        assert(label && label->isDefined() && "all photons are expected to be labelled");
        spho ulabel = *label;

        const G4StepPoint* pre = step->GetPreStepPoint();
        const G4StepPoint* post = step->GetPostStepPoint();
        G4VPhysicalVolume* post_pv = post->GetPhysicalVolume();
        G4VPhysicalVolume* pre_pv = pre->GetPhysicalVolume();
        bool               post_is_sensitive =
            post_pv && post_pv->GetLogicalVolume() && post_pv->GetLogicalVolume()->GetSensitiveDetector();
        bool pre_is_sensitive =
            pre_pv && pre_pv->GetLogicalVolume() && pre_pv->GetLogicalVolume()->GetSensitiveDetector();

        sev->checkPhotonLineage(ulabel);

        sphoton& current_photon = sev->current_ctx.p;

        if (current_photon.flagmask_count() == 1)
        {
            U4StepPoint::Update(current_photon, pre); // populate current_photon with pos, mom, pol, time, wavelength
            sev->pointPhoton(ulabel);                 // copying current into buffers
        }

        bool     tir;
        unsigned flag = U4StepPoint::Flag<G4OpBoundaryProcess>(post, true, tir);
        bool     is_sensitive_detect = post_is_sensitive && flag == SURFACE_SREFLECT;
        bool     is_sensitive_termination = pre_is_sensitive && track->GetTrackStatus() == fStopAndKill;

        if (is_sensitive_termination && current_photon.flag() == SURFACE_DETECT)
            return;

        if (is_sensitive_detect || is_sensitive_termination)
            flag = SURFACE_DETECT;

        bool is_detect_flag = OpticksPhoton::IsSurfaceDetectFlag(flag);

        const int touch_depth = touch ? touch->GetHistoryDepth() : 0;
        current_photon.hitcount_iindex =
            touch_depth > 1
                ? (is_detect_flag ? U4Touchable::ImmediateReplicaNumber(touch)
                                  : U4Touchable::AncestorReplicaNumber(touch))
                : 0;

        U4StepPoint::Update(current_photon, post);

        if (is_sensitive_detect)
        {
            const G4ThreeVector& mom = pre->GetMomentumDirection();
            const G4ThreeVector& pol = pre->GetPolarization();

            current_photon.mom = {static_cast<float>(mom.x()), static_cast<float>(mom.y()), static_cast<float>(mom.z())};
            current_photon.pol = {static_cast<float>(pol.x()), static_cast<float>(pol.y()), static_cast<float>(pol.z())};
        }

        current_photon.set_flag(flag);

        sev->pointPhoton(ulabel);
    }

    void CollectGensteps(const G4Step* step)
    {
        G4SteppingManager* stepping_manager =
            G4EventManager::GetEventManager()->GetTrackingManager()->GetSteppingManager();
        if (!stepping_manager || stepping_manager->GetfStepStatus() == fAtRestDoItProc)
            return;

        G4ProcessVector* processes = stepping_manager->GetfPostStepDoItVector();
        const size_t     process_count = stepping_manager->GetMAXofPostStepLoops();
        const G4Track*   track = step->GetTrack();

        for (size_t i = 0; i < process_count; ++i)
        {
            G4VProcess* process = (*processes)[i];
            if (!process)
                continue;

            if (process->GetProcessName() == "Cerenkov")
            {
                G4Cerenkov* cerenkov = dynamic_cast<G4Cerenkov*>(process);
                const G4Material* material = track->GetMaterial();
                G4MaterialPropertiesTable* properties = material ? material->GetMaterialPropertiesTable() : nullptr;
                G4MaterialPropertyVector* rindex = properties ? properties->GetProperty(kRINDEX) : nullptr;
                const G4int num_photons = cerenkov ? cerenkov->GetNumPhotons() : 0;
                if (!cerenkov || !rindex || rindex->GetVectorLength() == 0 || num_photons <= 0)
                    continue;

                const G4double charge = track->GetDynamicParticle()->GetDefinition()->GetPDGCharge();
                const G4double beta1 = step->GetPreStepPoint()->GetBeta();
                const G4double beta2 = step->GetPostStepPoint()->GetBeta();
                const G4double beta_inverse = 2.0 / (beta1 + beta2);
                const G4double pmin = rindex->Energy(0);
                const G4double pmax = rindex->GetMaxEnergy();
                const G4double max_cos = beta_inverse / rindex->GetMaxValue();
                const G4double max_sin2 = (1.0 - max_cos) * (1.0 + max_cos);
                const G4double mean1 = cerenkov->GetAverageNumberOfPhotons(charge, beta1, material, rindex);
                const G4double mean2 = cerenkov->GetAverageNumberOfPhotons(charge, beta2, material, rindex);

                U4::CollectGenstep_G4Cerenkov_modified(track, step, num_photons, beta_inverse, pmin, pmax, max_cos,
                                                       max_sin2, mean1, mean2);
            }
            else if (process->GetProcessName() == "Scintillation")
            {
                G4Scintillation* scintillation = dynamic_cast<G4Scintillation*>(process);
                const G4int num_photons = scintillation ? scintillation->GetNumPhotons() : 0;
                if (!scintillation || num_photons <= 0)
                    continue;

                const G4Material* material = track->GetMaterial();
                G4MaterialPropertiesTable* properties = material ? material->GetMaterialPropertiesTable() : nullptr;
                const G4double decay_time =
                    properties && properties->ConstPropertyExists(kSCINTILLATIONTIMECONSTANT1)
                        ? properties->GetConstProperty(kSCINTILLATIONTIMECONSTANT1)
                        : 0.0;
                U4::CollectGenstep_Scintillation(track, step, num_photons, 1, decay_time);
            }
        }
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

    void LabelOpticalSecondaries(const spho& parent_label)
    {
        G4TrackVector* secondaries = fpTrackingManager ? fpTrackingManager->GimmeSecondaries() : nullptr;
        if (secondaries == nullptr)
            return;

        spho child_label = parent_label.make_nextgen();
        for (G4Track* secondary : *secondaries)
        {
            if (secondary == nullptr || secondary->GetDefinition() != G4OpticalPhoton::OpticalPhotonDefinition())
                continue;

            if (STrackInfo::GetRef(secondary) == nullptr)
                STrackInfo::Set(secondary, child_label);
        }
    }

    void PreUserTrackingAction(const G4Track* track) override
    {
        if (track->GetDefinition() != G4OpticalPhoton::OpticalPhotonDefinition())
            return;

        if (track->GetDefinition() == G4OpticalPhoton::OpticalPhotonDefinition())
        {
            // Geant4 boundary updates optical velocity via ProposeVelocity, but the
            // track must honor the given velocity for post-boundary timing to match.
            G4Track* mutable_track = const_cast<G4Track*>(track);
            mutable_track->UseGivenVelocity(true);
        }

        if (!sev)
            return;

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
        if (!sev || track->GetDefinition() != G4OpticalPhoton::OpticalPhotonDefinition())
            return;

        G4TrackStatus tstat = track->GetTrackStatus();

        bool is_stop_and_kill = tstat == fStopAndKill;
        bool is_suspend = tstat == fSuspend;
        bool is_stop_and_kill_or_suspend = is_stop_and_kill || is_suspend;

        assert(is_stop_and_kill_or_suspend);

        const spho* label = STrackInfo::GetRef(track);
        assert(label && label->isDefined() && "all photons are expected to be labelled");
        spho ulabel = *label;

        LabelOpticalSecondaries(ulabel);

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
    simphony::Config                    cfg;
    std::shared_ptr<Simg4oxSharedState> shared_state;
    EventTimingRecorder*                timing;
    EventTimingMetadata                 timing_metadata;

    RunAction(const simphony::Config& cfg, std::shared_ptr<Simg4oxSharedState> shared_state,
              EventTimingRecorder* timing, EventTimingMetadata timing_metadata) :
        cfg(cfg),
        shared_state(std::move(shared_state)),
        timing(timing),
        timing_metadata(std::move(timing_metadata))
    {
    }

    G4Run* GenerateRun() override
    {
        return new Simg4oxRun;
    }

    void BeginOfRunAction(const G4Run*) override
    {
        if (!G4Threading::IsWorkerThread())
        {
            shared_state->BeginRun();
            if (timing)
                timing->BeginRun();
        }
    }

    void SaveHits(const std::vector<sphoton>& source, const char* name) const
    {
        NP* hits = NP::Make<float>(source.size(), 4, 4);
        if (!source.empty())
            std::memcpy(hits->bytes(), source.data(), source.size() * sizeof(sphoton));

        hits->save(cfg.output_dir.string().c_str(), name);
        delete hits;
    }

    void EndOfRunAction(const G4Run* run) override
    {
        // Worker runs are merged by G4MTRunManager after their EndOfRunAction.
        // Only the serial/master run owns the complete result.
        if (G4Threading::IsWorkerThread())
            return;

        const auto* simg4ox_run = static_cast<const Simg4oxRun*>(run);
        auto        gpu_hits = simg4ox_run->GPUHits();
        auto        g4_hits = simg4ox_run->G4Hits();

        SaveHits(gpu_hits, "s_hits.npy");
        SaveHits(g4_hits, "g_hits.npy");
        G4cout << "RunAction::EndOfRunAction: Total GPU hits: " << gpu_hits.size() << G4endl;
        G4cout << "RunAction::EndOfRunAction: Total G4  hits: " << g4_hits.size() << G4endl;
        if (timing)
        {
            timing->Write(timing_metadata);
            G4cout << "RunAction::EndOfRunAction: Event timing: " << timing->output() << G4endl;
        }
    }
};

struct ActionInitialization : G4VUserActionInitialization
{
    simphony::Config                    cfg;
    std::shared_ptr<Simg4oxSharedState> shared_state;
    bool                                multithreaded;
    PrimaryParticleConfig               primary;
    EventTimingRecorder*                timing;
    EventTimingMetadata                 timing_metadata;

    ActionInitialization(
        const simphony::Config&             cfg,
        std::shared_ptr<Simg4oxSharedState> shared_state,
        bool                                multithreaded,
        const PrimaryParticleConfig&        primary,
        EventTimingRecorder*                timing,
        EventTimingMetadata                 timing_metadata) :
        cfg(cfg),
        shared_state(std::move(shared_state)),
        multithreaded(multithreaded),
        primary(primary),
        timing(timing),
        timing_metadata(std::move(timing_metadata))
    {
    }

    void BuildForMaster() const override
    {
        SetUserAction(new RunAction(cfg, shared_state, timing, timing_metadata));
    }

    void Build() const override
    {
        // SEvt's CPU instance and its profiling/persistence helpers are
        // process-global. Keep the full CPU history recorder in serial mode;
        // MT workers still perform normal Geant4 tracking and collect SD hits.
        SEvt* sev = multithreaded ? nullptr
                                 : (primary.enabled ? SEvt::CreateOrReuse_EGPU()
                                                    : SEvt::CreateOrReuse_ECPU());

        SetUserAction(new PrimaryGenerator(cfg, sev, primary, timing));
        SetUserAction(new RunAction(cfg, shared_state, timing, timing_metadata));
        SetUserAction(new EventAction(sev, shared_state, multithreaded, primary.enabled, timing));
        SetUserAction(new TrackingAction(sev));

        if (sev)
            SetUserAction(new SteppingAction(sev, primary.enabled));
    }
};
