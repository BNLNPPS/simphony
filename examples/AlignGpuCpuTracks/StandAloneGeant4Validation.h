#pragma once

#include <filesystem>
#include <mutex>
#include <vector>
#include <cmath>

#include "G4Event.hh"
#include "G4GDMLParser.hh"
#include "G4THitsCollection.hh"
#include "G4VHit.hh"
#include "G4OpticalPhoton.hh"
#include "G4PhysicalConstants.hh"
#include "G4PrimaryParticle.hh"
#include "G4PrimaryVertex.hh"
#include "G4Run.hh"
#include "G4SDManager.hh"
#include "G4SystemOfUnits.hh"
#include "G4ThreeVector.hh"
#include "G4Track.hh"
#include "G4TrackStatus.hh"
#include "G4UserEventAction.hh"
#include "G4UserRunAction.hh"
#include "G4UserSteppingAction.hh"
#include "G4UserTrackingAction.hh"
#include "G4VPhysicalVolume.hh"
#include "G4VUserActionInitialization.hh"
#include "G4VUserDetectorConstruction.hh"
#include "G4VUserPrimaryGeneratorAction.hh"
#include "G4OpBoundaryProcess.hh"
#include "G4ProcessManager.hh"
#include "G4VPhysicsConstructor.hh"
#include "G4OpWLS.hh"

#include "ShimG4OpAbsorption.hh"
#include "ShimG4OpRayleigh.hh"
#include "U4Random.hh"

#include "sysrap/NP.hh"
#include "sysrap/sphoton.h"

#include "src/config.h"
#include "src/torch.h"

// ---- Global hit accumulator (thread-safe) ----

struct HitAccumulator
{
    std::mutex mtx;
    std::vector<sphoton> hits;

    void AddHits(const std::vector<sphoton> &event_hits)
    {
        std::lock_guard<std::mutex> lock(mtx);
        hits.insert(hits.end(), event_hits.begin(), event_hits.end());
    }

    void Save(const char *filename)
    {
        std::lock_guard<std::mutex> lock(mtx);
        G4int num_hits = hits.size();
        NP *arr = NP::Make<float>(num_hits, 4, 4);
        for (int i = 0; i < num_hits; i++)
        {
            float *data = reinterpret_cast<float *>(&hits[i]);
            std::copy(data, data + 16, arr->values<float>() + i * 16);
        }
        arr->save(filename);
        delete arr;
        G4cout << "G4: Saved " << num_hits << " total hits to " << filename << G4endl;
    }
};

// ---- Sensitive Detector: collects optical photon hits per event ----

struct G4PhotonHit : public G4VHit
{
    G4PhotonHit() = default;

    G4PhotonHit(G4double energy, G4double time, G4ThreeVector position,
                G4ThreeVector direction, G4ThreeVector polarization)
        : photon()
    {
        photon.pos = {static_cast<float>(position.x()),
                      static_cast<float>(position.y()),
                      static_cast<float>(position.z())};
        photon.time = time;
        photon.mom = {static_cast<float>(direction.x()),
                      static_cast<float>(direction.y()),
                      static_cast<float>(direction.z())};
        photon.pol = {static_cast<float>(polarization.x()),
                      static_cast<float>(polarization.y()),
                      static_cast<float>(polarization.z())};
        photon.wavelength = h_Planck * c_light / (energy * CLHEP::eV);
    }

    void Print() override { G4cout << photon << G4endl; }

    sphoton photon;
};

using G4PhotonHitsCollection = G4THitsCollection<G4PhotonHit>;

struct G4PhotonSD : public G4VSensitiveDetector
{
    HitAccumulator *accumulator;

    G4PhotonSD(G4String name, HitAccumulator *acc)
        : G4VSensitiveDetector(name), accumulator(acc)
    {
        G4String HCname = name + "_HC";
        collectionName.insert(HCname);
    }

    void Initialize(G4HCofThisEvent *hce) override
    {
        fHitsCollection = new G4PhotonHitsCollection(SensitiveDetectorName, collectionName[0]);
        if (fHCID < 0)
            fHCID = G4SDManager::GetSDMpointer()->GetCollectionID(collectionName[0]);
        hce->AddHitsCollection(fHCID, fHitsCollection);
    }

    G4bool ProcessHits(G4Step *aStep, G4TouchableHistory *) override
    {
        G4Track *track = aStep->GetTrack();
        if (track->GetDefinition() != G4OpticalPhoton::OpticalPhotonDefinition())
            return false;

        G4PhotonHit *hit = new G4PhotonHit(
            track->GetTotalEnergy(),
            track->GetGlobalTime(),
            aStep->GetPostStepPoint()->GetPosition(),
            aStep->GetPostStepPoint()->GetMomentumDirection(),
            aStep->GetPostStepPoint()->GetPolarization());

        fHitsCollection->insert(hit);
        track->SetTrackStatus(fStopAndKill);
        return true;
    }

    void EndOfEvent(G4HCofThisEvent *) override
    {
        G4int num_hits = fHitsCollection->entries();

        std::vector<sphoton> event_hits;
        event_hits.reserve(num_hits);
        for (G4PhotonHit *hit : *fHitsCollection->GetVector())
            event_hits.push_back(hit->photon);

        accumulator->AddHits(event_hits);
    }

  private:
    G4PhotonHitsCollection *fHitsCollection = nullptr;
    G4int fHCID = -1;
};

// ---- Detector Construction: loads GDML, attaches SD ----

struct G4OnlyDetectorConstruction : G4VUserDetectorConstruction
{
    G4OnlyDetectorConstruction(std::filesystem::path gdml_file, HitAccumulator *acc)
        : gdml_file_(gdml_file), accumulator_(acc) {}

    G4VPhysicalVolume *Construct() override
    {
        parser_.Read(gdml_file_.string(), false);
        return parser_.GetWorldVolume();
    }

    void ConstructSDandField() override
    {
        G4SDManager *SDman = G4SDManager::GetSDMpointer();
        const G4GDMLAuxMapType *auxmap = parser_.GetAuxMap();

        for (auto const &[logVol, listType] : *auxmap)
        {
            for (auto const &auxtype : listType)
            {
                if (auxtype.type == "SensDet")
                {
                    G4String name = logVol->GetName() + "_" + auxtype.value;
                    G4cout << "G4: Attaching SD to " << logVol->GetName() << G4endl;
                    G4PhotonSD *sd = new G4PhotonSD(name, accumulator_);
                    SDman->AddNewDetector(sd);
                    logVol->SetSensitiveDetector(sd);
                }
            }
        }
    }

  private:
    std::filesystem::path gdml_file_;
    G4GDMLParser parser_;
    HitAccumulator *accumulator_;
};

// ---- Primary Generator: distributes photons across events ----

struct G4OnlyPrimaryGenerator : G4VUserPrimaryGeneratorAction
{
    gphox::Config cfg;
    int photons_per_event;

    G4OnlyPrimaryGenerator(const gphox::Config &cfg, int photons_per_event)
        : cfg(cfg), photons_per_event(photons_per_event) {}

    void GeneratePrimaries(G4Event *event) override
    {
        int eventID = event->GetEventID();

        // Generate photons for this event's batch using event-specific seed offset
        storch t = cfg.torch;
        t.numphoton = photons_per_event;
        std::vector<sphoton> sphotons = generate_photons(t, photons_per_event, eventID);

        for (const sphoton &p : sphotons)
        {
            G4ThreeVector position(p.pos.x, p.pos.y, p.pos.z);
            G4ThreeVector direction(p.mom.x, p.mom.y, p.mom.z);
            G4ThreeVector polarization(p.pol.x, p.pol.y, p.pol.z);
            G4double wavelength_nm = p.wavelength;

            G4PrimaryVertex *vertex = new G4PrimaryVertex(position, p.time);
            G4double energy = h_Planck * c_light / (wavelength_nm * nm);

            G4PrimaryParticle *particle = new G4PrimaryParticle(G4OpticalPhoton::Definition());
            particle->SetKineticEnergy(energy);
            particle->SetMomentumDirection(direction);
            particle->SetPolarization(polarization);

            vertex->SetPrimary(particle);
            event->AddPrimaryVertex(vertex);
        }
    }
};

// ---- Photon fate accumulator: tracks ALL photon final states ----

struct PhotonFateAccumulator
{
    std::mutex mtx;
    std::vector<sphoton> photons;
    bool indexed = false;  // true for aligned mode: store by photon index

    // Opticks flag enum values
    static constexpr unsigned TORCH            = 0x0004;
    static constexpr unsigned BULK_ABSORB      = 0x0008;
    static constexpr unsigned BULK_REEMIT      = 0x0010;
    static constexpr unsigned BULK_SCATTER     = 0x0020;
    static constexpr unsigned SURFACE_DETECT   = 0x0040;
    static constexpr unsigned SURFACE_ABSORB   = 0x0080;
    static constexpr unsigned SURFACE_DREFLECT = 0x0100;
    static constexpr unsigned SURFACE_SREFLECT = 0x0200;
    static constexpr unsigned BOUNDARY_REFLECT = 0x0400;
    static constexpr unsigned BOUNDARY_TRANSMIT= 0x0800;
    static constexpr unsigned MISS             = 0x8000;

    void Resize(int n)
    {
        photons.resize(n);
        indexed = true;
    }

    void Set(int idx, const sphoton& p)
    {
        if (idx >= 0 && idx < (int)photons.size())
            photons[idx] = p;
    }

    void Add(const sphoton& p)
    {
        std::lock_guard<std::mutex> lock(mtx);
        photons.push_back(p);
    }

    void Save(const char* filename)
    {
        std::lock_guard<std::mutex> lock(mtx);
        int n = photons.size();
        NP* arr = NP::Make<float>(n, 4, 4);
        for (int i = 0; i < n; i++)
        {
            float* data = reinterpret_cast<float*>(&photons[i]);
            std::copy(data, data + 16, arr->values<float>() + i * 16);
        }
        arr->save(filename);
        delete arr;
        G4cout << "G4: Saved " << n << " photon fates to " << filename << G4endl;
    }
};

// ---- Step Record: saves per-step photon state as (num_photons, max_steps, 4, 4) ----

struct StepRecordAccumulator
{
    int max_photons;
    int max_steps;
    std::vector<sphoton> records;  // flat array: [photon_idx * max_steps + step_idx]
    std::vector<int> step_counts;  // current step count per photon

    StepRecordAccumulator(int np, int ms) : max_photons(np), max_steps(ms),
        records(np * ms), step_counts(np, 0)
    {
        memset(records.data(), 0, records.size() * sizeof(sphoton));
    }

    void RecordStep(int photon_idx, const G4StepPoint* point, unsigned flag)
    {
        if (photon_idx < 0 || photon_idx >= max_photons) return;
        int step = step_counts[photon_idx];
        if (step >= max_steps) return;

        G4ThreeVector pos = point->GetPosition();
        G4ThreeVector mom = point->GetMomentumDirection();
        G4ThreeVector pol = point->GetPolarization();
        G4double energy = point->GetTotalEnergy();

        sphoton& s = records[photon_idx * max_steps + step];
        s.pos = { float(pos.x()), float(pos.y()), float(pos.z()) };
        s.time = float(point->GetGlobalTime());
        s.mom = { float(mom.x()), float(mom.y()), float(mom.z()) };
        s.pol = { float(pol.x()), float(pol.y()), float(pol.z()) };
        s.wavelength = (energy > 0) ? float(h_Planck * c_light / (energy * CLHEP::eV)) : 0.f;
        s.orient_boundary_flag = flag & 0xFFFF;
        s.flagmask = flag;

        step_counts[photon_idx]++;
    }

    void Save(const char* filename)
    {
        NP* arr = NP::Make<float>(max_photons, max_steps, 4, 4);
        memcpy(arr->values<float>(), records.data(), records.size() * sizeof(sphoton));
        arr->save(filename);
        delete arr;
        G4cout << "G4: Saved step records (" << max_photons << " x " << max_steps
               << ") to " << filename << G4endl;
    }
};

// ---- Stepping Action: tracks photon fates with opticks-compatible flags ----

struct G4OnlySteppingAction : G4UserSteppingAction
{
    PhotonFateAccumulator* fate;
    StepRecordAccumulator* record;
    bool aligned;
    std::map<std::string, int> proc_death_counts;
    std::map<int, int> boundary_status_counts;
    std::mutex count_mtx;

    G4OnlySteppingAction(PhotonFateAccumulator* f, bool aligned_ = false,
                         StepRecordAccumulator* rec = nullptr)
        : fate(f), record(rec), aligned(aligned_) {}

    ~G4OnlySteppingAction()
    {
        std::lock_guard<std::mutex> lock(count_mtx);
        if (!proc_death_counts.empty())
        {
            G4cout << "\nG4: Photon death process summary:" << G4endl;
            for (auto& [name, count] : proc_death_counts)
                G4cout << "  " << name << ": " << count << G4endl;
        }
        if (!boundary_status_counts.empty())
        {
            G4cout << "\nG4: OpBoundary status counts (all steps):" << G4endl;
            const char* bnames[] = {
                "Undefined","Transmission","FresnelRefraction","FresnelReflection",
                "TotalInternalReflection","LambertianReflection","LobeReflection",
                "SpikeReflection","BackScattering","Absorption","Detection",
                "NotAtBoundary","SameMaterial","StepTooSmall","NoRINDEX",
                "PolishedLumirrorAirReflection","PolishedLumirrorGlueReflection",
                "PolishedAirReflection","PolishedTeflonAirReflection",
                "PolishedTiOAirReflection","PolishedTyvekAirReflection",
                "PolishedVM2000AirReflection","PolishedVM2000GlueReflection",
                "EtchedLumirrorAirReflection","EtchedLumirrorGlueReflection",
                "EtchedAirReflection","EtchedTeflonAirReflection",
                "EtchedTiOAirReflection","EtchedTyvekAirReflection",
                "EtchedVM2000AirReflection","EtchedVM2000GlueReflection",
                "GroundLumirrorAirReflection","GroundLumirrorGlueReflection",
                "GroundAirReflection","GroundTeflonAirReflection",
                "GroundTiOAirReflection","GroundTyvekAirReflection",
                "GroundVM2000AirReflection","GroundVM2000GlueReflection",
                "Dichroic","CoatedDielectricReflection","CoatedDielectricRefraction",
                "CoatedDielectricFrustratedTransmission"
            };
            for (auto& [st, count] : boundary_status_counts)
            {
                const char* nm = (st >= 0 && st < 43) ? bnames[st] : "?";
                G4cout << "  " << nm << "(" << st << "): " << count << G4endl;
            }
        }
    }

    void UserSteppingAction(const G4Step* aStep) override
    {
        G4Track* track = aStep->GetTrack();
        if (track->GetDefinition() != G4OpticalPhoton::OpticalPhotonDefinition())
            return;

        if (track->GetCurrentStepNumber() > 1000)
            track->SetTrackStatus(fStopAndKill);

        G4StepPoint* post = aStep->GetPostStepPoint();
        G4TrackStatus status = track->GetTrackStatus();

        // Record every step for aligned photon-by-photon comparison
        if (record)
        {
            int photon_idx = track->GetTrackID() - 1;
            if (track->GetCurrentStepNumber() == 1)
                record->RecordStep(photon_idx, aStep->GetPreStepPoint(), 0x0004); // TORCH
            record->RecordStep(photon_idx, post, 0x0001);  // placeholder flag
        }

        // Find the OpBoundary process to get its status (for ALL steps)
        G4OpBoundaryProcess* boundary = nullptr;
        G4ProcessManager* pm = track->GetDefinition()->GetProcessManager();
        for (int i = 0; i < pm->GetPostStepProcessVector()->entries(); i++)
        {
            G4VProcess* p = (*pm->GetPostStepProcessVector())[i];
            boundary = dynamic_cast<G4OpBoundaryProcess*>(p);
            if (boundary) break;
        }

        G4OpBoundaryProcessStatus bStatus = boundary ? boundary->GetStatus() : Undefined;

        // Count boundary status for ALL steps
        if (boundary && bStatus != NotAtBoundary && bStatus != Undefined && bStatus != StepTooSmall)
        {
            std::lock_guard<std::mutex> lock(count_mtx);
            boundary_status_counts[int(bStatus)]++;
        }

        // Only record photon state when the photon is about to die
        if (status != fStopAndKill && status != fStopButAlive)
            return;

        // Identify the process
        const G4VProcess* proc = post->GetProcessDefinedStep();
        G4String procName = proc ? proc->GetProcessName() : "Unknown";

        // Build detailed key for counting
        std::string key = procName;
        if (procName == "OpBoundary" && boundary)
            key += "(" + std::to_string(int(bStatus)) + ")";
        key += (status == fStopAndKill ? "/Kill" : "/Alive");

        {
            std::lock_guard<std::mutex> lock(count_mtx);
            proc_death_counts[key]++;
        }

        // Map to opticks flag
        unsigned flag = 0;

        if (procName == "OpAbsorption")
        {
            flag = PhotonFateAccumulator::BULK_ABSORB;
        }
        else if (procName == "OpWLS")
        {
            flag = PhotonFateAccumulator::BULK_REEMIT;
        }
        else if (procName == "OpBoundary" && boundary)
        {
            switch (bStatus)
            {
                case Detection:       flag = PhotonFateAccumulator::SURFACE_DETECT; break;
                case Absorption:      flag = PhotonFateAccumulator::SURFACE_ABSORB; break;
                case FresnelReflection:
                case TotalInternalReflection:
                                      flag = PhotonFateAccumulator::BOUNDARY_REFLECT; break;
                case FresnelRefraction: flag = PhotonFateAccumulator::BOUNDARY_TRANSMIT; break;
                case LambertianReflection:
                case LobeReflection:  flag = PhotonFateAccumulator::SURFACE_DREFLECT; break;
                case SpikeReflection: flag = PhotonFateAccumulator::SURFACE_SREFLECT; break;
                case BackScattering:  flag = PhotonFateAccumulator::SURFACE_DREFLECT; break;
                default:              flag = PhotonFateAccumulator::SURFACE_ABSORB; break;
            }
        }
        else if (procName == "Transportation")
        {
            // Check if an SD killed this photon (SURFACE_DETECT)
            G4StepPoint* pre = aStep->GetPreStepPoint();
            G4VPhysicalVolume* preVol = pre->GetPhysicalVolume();
            G4VPhysicalVolume* postVol = post->GetPhysicalVolume();
            G4LogicalVolume* preLog = preVol ? preVol->GetLogicalVolume() : nullptr;
            G4LogicalVolume* postLog = postVol ? postVol->GetLogicalVolume() : nullptr;
            bool sd_pre = preLog && preLog->GetSensitiveDetector();
            bool sd_post = postLog && postLog->GetSensitiveDetector();
            if (sd_pre || sd_post)
                flag = PhotonFateAccumulator::SURFACE_DETECT;
            else
                flag = PhotonFateAccumulator::BOUNDARY_TRANSMIT;
        }

        if (flag == 0) flag = PhotonFateAccumulator::MISS; // catch-all

        // Update the last recorded step with the death flag
        if (record)
        {
            int photon_idx = track->GetTrackID() - 1;
            if (photon_idx >= 0 && photon_idx < record->max_photons)
            {
                int step = record->step_counts[photon_idx];
                if (step > 0)
                {
                    sphoton& last = record->records[photon_idx * record->max_steps + step - 1];
                    last.orient_boundary_flag = flag & 0xFFFF;
                    last.flagmask = flag;
                }
            }
        }

        // Build sphoton with the final state
        G4ThreeVector pos = post->GetPosition();
        G4ThreeVector mom = post->GetMomentumDirection();
        G4ThreeVector pol = post->GetPolarization();
        G4double time = post->GetGlobalTime();
        G4double energy = post->GetTotalEnergy();

        sphoton p = {};
        p.pos = { float(pos.x()), float(pos.y()), float(pos.z()) };
        p.time = float(time);
        p.mom = { float(mom.x()), float(mom.y()), float(mom.z()) };
        p.pol = { float(pol.x()), float(pol.y()), float(pol.z()) };
        p.wavelength = (energy > 0) ? float(h_Planck * c_light / (energy * CLHEP::eV)) : 0.f;

        p.orient_boundary_flag = flag & 0xFFFF;
        p.flagmask = flag;

        if (aligned && fate->indexed)
        {
            int photon_idx = track->GetTrackID() - 1;  // G4 trackIDs are 1-based
            fate->Set(photon_idx, p);
        }
        else
        {
            fate->Add(p);
        }
    }
};

// ---- Tracking Action: per-photon RNG sync for aligned mode ----

struct G4OnlyTrackingAction : G4UserTrackingAction
{
    void PreUserTrackingAction(const G4Track* track) override
    {
        if (track->GetDefinition() != G4OpticalPhoton::OpticalPhotonDefinition())
            return;
        int photon_idx = track->GetTrackID() - 1;  // G4 trackIDs are 1-based
        U4Random::SetSequenceIndex(photon_idx);
    }

    void PostUserTrackingAction(const G4Track* track) override
    {
        if (track->GetDefinition() != G4OpticalPhoton::OpticalPhotonDefinition())
            return;
        U4Random::SetSequenceIndex(-1);
    }
};

// ---- AlignedOpticalPhysics: uses Shim processes for precise RNILL matching ----

struct AlignedOpticalPhysics : G4VPhysicsConstructor
{
    AlignedOpticalPhysics() : G4VPhysicsConstructor("AlignedOptical") {}
    void ConstructParticle() override {}
    void ConstructProcess() override
    {
        auto* pm = G4OpticalPhoton::OpticalPhoton()->GetProcessManager();
        pm->AddDiscreteProcess(new ShimG4OpAbsorption());
        pm->AddDiscreteProcess(new ShimG4OpRayleigh());
        pm->AddDiscreteProcess(new G4OpBoundaryProcess());
        pm->AddDiscreteProcess(new G4OpWLS());
    }
};

// ---- Event Action: reports per-event progress ----

struct G4OnlyEventAction : G4UserEventAction
{
    int total_events;

    G4OnlyEventAction(int total_events) : total_events(total_events) {}

    void EndOfEventAction(const G4Event *event) override
    {
        int id = event->GetEventID();
        if (id == 0 || (id + 1) % 10 == 0 || id + 1 == total_events)
            G4cout << "G4: Event " << id + 1 << "/" << total_events << G4endl;
    }
};

// ---- Run Action: saves merged hits at end ----

struct G4OnlyRunAction : G4UserRunAction
{
    HitAccumulator *accumulator;
    PhotonFateAccumulator *fate;
    StepRecordAccumulator *record;

    G4OnlyRunAction(HitAccumulator *acc, PhotonFateAccumulator *f = nullptr,
                    StepRecordAccumulator *rec = nullptr)
        : accumulator(acc), fate(f), record(rec) {}

    void EndOfRunAction(const G4Run *) override
    {
        if (G4Threading::IsMasterThread() || !G4Threading::IsMultithreadedApplication())
        {
            G4cout << "G4: Total accumulated hits: " << accumulator->hits.size() << G4endl;
            accumulator->Save("g4_hits.npy");
            if (fate)
            {
                G4cout << "G4: Total photon fates: " << fate->photons.size() << G4endl;
                fate->Save("g4_photon.npy");
            }
            if (record)
                record->Save("g4_record.npy");
        }
    }
};

// ---- Action Initialization (required for MT) ----

struct G4OnlyActionInitialization : G4VUserActionInitialization
{
    gphox::Config cfg;
    HitAccumulator *accumulator;
    PhotonFateAccumulator *fate;
    StepRecordAccumulator *record;
    int photons_per_event;
    int num_events;
    bool aligned;

    G4OnlyActionInitialization(const gphox::Config &cfg, HitAccumulator *acc,
                               PhotonFateAccumulator *f,
                               int photons_per_event, int num_events,
                               bool aligned_ = false,
                               StepRecordAccumulator *rec = nullptr)
        : cfg(cfg), accumulator(acc), fate(f), record(rec),
          photons_per_event(photons_per_event),
          num_events(num_events), aligned(aligned_) {}

    void BuildForMaster() const override
    {
        SetUserAction(new G4OnlyRunAction(accumulator, fate, record));
    }

    void Build() const override
    {
        SetUserAction(new G4OnlyPrimaryGenerator(cfg, photons_per_event));
        SetUserAction(new G4OnlyEventAction(num_events));
        SetUserAction(new G4OnlyRunAction(accumulator, fate, record));
        SetUserAction(new G4OnlySteppingAction(fate, aligned, record));
        if (aligned)
            SetUserAction(new G4OnlyTrackingAction());
    }
};
