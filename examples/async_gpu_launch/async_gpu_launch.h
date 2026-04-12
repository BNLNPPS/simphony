#pragma once
//
// async_gpu_launch.h — Async CPU+GPU optical photon simulation
//
// Based on GPURaytrace.h with asynchronous GPU processing added.
// Follows the double-buffering pattern from esi-g4ox:
//   https://github.com/BNLNPPS/esi-g4ox/commit/c9f39f59
//
// Architecture:
//   CPU event loop collects gensteps into a buffer.
//   When the buffer reaches a photon threshold, it is submitted
//   to a GPU worker via G4TaskGroup. The buffers are swapped so
//   the CPU can continue filling the next buffer while the GPU
//   processes the previous one.
//
// Modes:
//   --async   : Async double-buffered GPU processing (default)
//   --sync    : Original end-of-run GPU simulation
//

#include <array>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <mutex>
#include <sstream>
#include <vector>

#include "G4AutoLock.hh"
#include "G4BooleanSolid.hh"
#include "G4Cerenkov.hh"
#include "G4Electron.hh"
#include "G4Event.hh"
#include "G4EventManager.hh"
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
#include "G4TaskGroup.hh"
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
#include "sysrap/OpticksGenstep.h"
#include "sysrap/SEvt.hh"
#include "sysrap/scerenkov.h"
#include "sysrap/sgs.h"
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
G4Mutex g4hits_mutex = G4MUTEX_INITIALIZER;
std::vector<std::array<float, 16>> g4_accumulated_hits;
} // namespace

// ============================================================================
// Geometry helpers
// ============================================================================

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
// GenstepBuffer — Thread-safe buffer for double-buffering
// ============================================================================

struct GenstepBuffer
{
    std::vector<quad6> gensteps;
    std::vector<sgs> labels;
    int64_t photon_count = 0;
    int64_t genstep_count = 0;
    int event_id = 0;

    void clear()
    {
        gensteps.clear();
        labels.clear();
        photon_count = 0;
        genstep_count = 0;
    }

    void addGenstep(const quad6 &gs, int64_t numphotons)
    {
        sgs label;
        label.index = gensteps.size();
        label.photons = numphotons;
        label.offset = photon_count;
        label.gentype = gs.gentype();

        gensteps.push_back(gs);
        labels.push_back(label);
        photon_count += numphotons;
        genstep_count++;
    }

    bool empty() const
    {
        return gensteps.empty();
    }
    size_t size() const
    {
        return gensteps.size();
    }
};

// ============================================================================
// GPUTaskManager — Async GPU processing with G4TaskGroup
//
// Uses Geant4's built-in tasking (G4TaskGroup) for the worker thread.
// A single GPU mutex ensures only one kernel runs at a time.
// ============================================================================

class GPUTaskManager
{
  public:
    static constexpr int64_t DEFAULT_PHOTON_THRESHOLD = 10000000; // 10M photons

  private:
    int64_t photon_threshold_;

    std::shared_ptr<GenstepBuffer> active_buffer_;
    G4Mutex buffer_mutex_;

    std::unique_ptr<G4TaskGroup<void, void>> task_group_;
    G4Mutex gpu_mutex_;

    // Statistics
    std::atomic<int> batch_counter_{0};
    std::atomic<int> completed_batches_{0};
    std::atomic<uint64_t> total_hits_{0};
    std::atomic<uint64_t> total_photons_{0};
    std::atomic<uint64_t> total_gpu_time_us_{0};

  public:
    GPUTaskManager(int64_t threshold = DEFAULT_PHOTON_THRESHOLD)
        : photon_threshold_(threshold), active_buffer_(std::make_shared<GenstepBuffer>()), task_group_(nullptr)
    {
        const char *env_thresh = std::getenv("GPU_PHOTON_FLUSH_THRESHOLD");
        if (env_thresh)
            photon_threshold_ = std::atoll(env_thresh);
    }

    ~GPUTaskManager()
    {
        shutdown();
    }

    void start()
    {
        task_group_ = std::make_unique<G4TaskGroup<void, void>>();
        G4cout << "GPUTaskManager: Started (threshold=" << photon_threshold_ << " photons)" << G4endl;
    }

    void shutdown()
    {
        {
            G4AutoLock lock(&buffer_mutex_);
            if (active_buffer_ && !active_buffer_->empty())
            {
                submitBuffer(active_buffer_);
                active_buffer_ = std::make_shared<GenstepBuffer>();
            }
        }
        waitForCompletion();
        task_group_.reset();

        G4cout << "GPUTaskManager: Shutdown" << G4endl;
        G4cout << "  Batches:  " << completed_batches_.load() << G4endl;
        G4cout << "  Photons:  " << total_photons_.load() << G4endl;
        G4cout << "  Hits:     " << total_hits_.load() << G4endl;
        G4cout << "  GPU time: " << (total_gpu_time_us_.load() / 1e6) << " s" << G4endl;
    }

    // Hot path — called from SteppingAction
    void addGenstep(const quad6 &gs, int64_t numphotons, int eventID)
    {
        std::shared_ptr<GenstepBuffer> buffer_to_submit;

        {
            G4AutoLock lock(&buffer_mutex_);
            active_buffer_->event_id = eventID;
            active_buffer_->addGenstep(gs, numphotons);

            if (active_buffer_->photon_count >= photon_threshold_)
            {
                buffer_to_submit = active_buffer_;
                active_buffer_ = std::make_shared<GenstepBuffer>();
            }
        }

        // Submit outside the lock
        if (buffer_to_submit)
            submitBuffer(buffer_to_submit);
    }

    void flushRemaining(int eventID)
    {
        std::shared_ptr<GenstepBuffer> buffer_to_submit;
        {
            G4AutoLock lock(&buffer_mutex_);
            if (active_buffer_ && !active_buffer_->empty())
            {
                active_buffer_->event_id = eventID;
                buffer_to_submit = active_buffer_;
                active_buffer_ = std::make_shared<GenstepBuffer>();
            }
        }
        if (buffer_to_submit)
        {
            G4cout << "GPUTaskManager: Final flush (" << buffer_to_submit->photon_count << " photons)" << G4endl;
            submitBuffer(buffer_to_submit);
        }
        waitForCompletion();
    }

    void waitForCompletion()
    {
        if (task_group_)
            task_group_->join();
    }

    int getCompletedBatches() const
    {
        return completed_batches_.load();
    }
    uint64_t getTotalHits() const
    {
        return total_hits_.load();
    }
    uint64_t getTotalPhotons() const
    {
        return total_photons_.load();
    }
    int64_t getThreshold() const
    {
        return photon_threshold_;
    }
    double getTotalGPUTime() const
    {
        return total_gpu_time_us_.load() / 1e6;
    }

  private:
    void submitBuffer(std::shared_ptr<GenstepBuffer> buffer)
    {
        if (!buffer || buffer->empty() || !task_group_)
            return;

        int batch_id = batch_counter_.fetch_add(1);

        G4cout << "GPUTaskManager: Queued batch " << batch_id << " (" << buffer->photon_count << " photons, "
               << buffer->genstep_count << " gensteps)" << G4endl;

        task_group_->exec([this, batch_id, buffer]() { processGPUTask(batch_id, buffer); });
    }

    void processGPUTask(int batch_id, std::shared_ptr<GenstepBuffer> buffer)
    {
        // Only one GPU task at a time — G4CXOpticks/SEvt are not thread-safe
        G4AutoLock gpu_lock(&gpu_mutex_);

        G4cout << "=== GPU Batch " << batch_id << " ===" << G4endl;
        G4cout << "  Photons:  " << buffer->photon_count << G4endl;
        G4cout << "  Gensteps: " << buffer->genstep_count << G4endl;

        G4CXOpticks *gx = G4CXOpticks::Get();
        SEvt *sev = SEvt::Get_EGPU();

        if (!gx || !sev)
        {
            G4cerr << "GPUTaskManager: G4CXOpticks or SEvt not available" << G4endl;
            return;
        }

        // Load buffered gensteps into SEvt
        sev->clear_genstep();
        NP *gs_array = NP::Make<float>(buffer->gensteps.size(), 6, 4);
        memcpy(gs_array->values<float>(), buffer->gensteps.data(), buffer->gensteps.size() * sizeof(quad6));
        sev->addGenstep(gs_array);

        // Run GPU simulation
        auto start = std::chrono::high_resolution_clock::now();
        gx->simulate(buffer->event_id, false);
        cudaDeviceSynchronize();
        auto end = std::chrono::high_resolution_clock::now();
        auto elapsed_us = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();

        unsigned int num_hits = sev->GetNumHit(0);

        total_gpu_time_us_ += elapsed_us;
        total_hits_ += num_hits;
        total_photons_ += buffer->photon_count;

        G4cout << "  GPU time: " << (elapsed_us / 1e6) << " s" << G4endl;
        G4cout << "  Hits:     " << num_hits << G4endl;

        if (num_hits > 0)
            saveHits(batch_id, sev, num_hits);

        gx->reset(buffer->event_id);
        completed_batches_++;

        G4cout << "=== GPU Batch " << batch_id << " Complete ===" << G4endl;
    }

    void saveHits(int batch_id, SEvt *sev, unsigned int num_hits)
    {
        // Save as .npy (sphoton layout: N x 4 x 4 float32)
        std::ostringstream fname;
        fname << "gpu_hits_batch_" << batch_id << ".npy";

        NP *arr = NP::Make<float>(num_hits, 4, 4);
        for (unsigned idx = 0; idx < num_hits; idx++)
        {
            sphoton hit;
            sev->getHit(hit, idx);
            memcpy(arr->bytes() + idx * sizeof(sphoton), &hit, sizeof(sphoton));
        }
        arr->save(fname.str().c_str());
        G4cout << "  Saved " << num_hits << " hits to " << fname.str() << G4endl;
    }
};

// ============================================================================
// Genstep construction helpers (bypass U4/SEvt for async mode)
// ============================================================================

static quad6 MakeGenstep_Cerenkov(const G4Track *aTrack, const G4Step *aStep, G4int numPhotons, G4double betaInverse,
                                  G4double pmin, G4double pmax, G4double maxCos, G4double maxSin2,
                                  G4double meanNumberOfPhotons1, G4double meanNumberOfPhotons2)
{
    G4StepPoint *pPreStepPoint = aStep->GetPreStepPoint();
    G4StepPoint *pPostStepPoint = aStep->GetPostStepPoint();
    G4ThreeVector x0 = pPreStepPoint->GetPosition();
    G4double t0 = pPreStepPoint->GetGlobalTime();
    G4ThreeVector deltaPosition = aStep->GetDeltaPosition();
    const G4DynamicParticle *aParticle = aTrack->GetDynamicParticle();
    const G4Material *aMaterial = aTrack->GetMaterial();

    G4double Wmin_nm = h_Planck * c_light / pmax / nm;
    G4double Wmax_nm = h_Planck * c_light / pmin / nm;

    quad6 gs;
    gs.zero();
    scerenkov *ck = (scerenkov *)(&gs);

    ck->gentype = OpticksGenstep_G4Cerenkov_modified;
    ck->trackid = aTrack->GetTrackID();
    ck->matline = aMaterial->GetIndex() + SEvt::G4_INDEX_OFFSET;
    ck->numphoton = numPhotons;

    ck->pos.x = x0.x();
    ck->pos.y = x0.y();
    ck->pos.z = x0.z();
    ck->time = t0;

    ck->DeltaPosition.x = deltaPosition.x();
    ck->DeltaPosition.y = deltaPosition.y();
    ck->DeltaPosition.z = deltaPosition.z();
    ck->step_length = aStep->GetStepLength();

    ck->code = aParticle->GetDefinition()->GetPDGEncoding();
    ck->charge = aParticle->GetDefinition()->GetPDGCharge();
    ck->weight = aTrack->GetWeight();
    ck->preVelocity = pPreStepPoint->GetVelocity();

    ck->BetaInverse = betaInverse;
    ck->Wmin = Wmin_nm;
    ck->Wmax = Wmax_nm;
    ck->maxCos = maxCos;

    ck->maxSin2 = maxSin2;
    ck->MeanNumberOfPhotons1 = meanNumberOfPhotons1;
    ck->MeanNumberOfPhotons2 = meanNumberOfPhotons2;
    ck->postVelocity = pPostStepPoint->GetVelocity();

    return gs;
}

static quad6 MakeGenstep_Scintillation(const G4Track *aTrack, const G4Step *aStep, G4int numPhotons, G4int scnt,
                                       G4double ScintillationTime)
{
    G4StepPoint *pPreStepPoint = aStep->GetPreStepPoint();
    G4StepPoint *pPostStepPoint = aStep->GetPostStepPoint();
    G4ThreeVector x0 = pPreStepPoint->GetPosition();
    G4double t0 = pPreStepPoint->GetGlobalTime();
    G4ThreeVector deltaPosition = aStep->GetDeltaPosition();
    G4double meanVelocity = (pPreStepPoint->GetVelocity() + pPostStepPoint->GetVelocity()) / 2.;
    const G4DynamicParticle *aParticle = aTrack->GetDynamicParticle();
    const G4Material *aMaterial = aTrack->GetMaterial();

    quad6 gs;
    gs.zero();
    sscint *sc = (sscint *)(&gs);

    sc->gentype = OpticksGenstep_DsG4Scintillation_r4695;
    sc->trackid = aTrack->GetTrackID();
    sc->matline = aMaterial->GetIndex() + SEvt::G4_INDEX_OFFSET;
    sc->numphoton = numPhotons;

    sc->pos.x = x0.x();
    sc->pos.y = x0.y();
    sc->pos.z = x0.z();
    sc->time = t0;

    sc->DeltaPosition.x = deltaPosition.x();
    sc->DeltaPosition.y = deltaPosition.y();
    sc->DeltaPosition.z = deltaPosition.z();
    sc->step_length = aStep->GetStepLength();

    sc->code = aParticle->GetDefinition()->GetPDGEncoding();
    sc->charge = aParticle->GetDefinition()->GetPDGCharge();
    sc->weight = aTrack->GetWeight();
    sc->meanVelocity = meanVelocity;

    sc->scnt = scnt;
    sc->ScintillationTime = ScintillationTime;

    return gs;
}

// ============================================================================
// Sensitive detector and hits
// ============================================================================

struct PhotonHit : public G4VHit
{
    PhotonHit() = default;
    PhotonHit(unsigned id, G4double energy, G4double time, G4ThreeVector position, G4ThreeVector direction,
              G4ThreeVector polarization)
        : fid(id), fenergy(energy), ftime(time), fposition(position), fdirection(direction), fpolarization(polarization)
    {
    }
    PhotonHit(const PhotonHit &right)
        : G4VHit(right), fid(right.fid), fenergy(right.fenergy), ftime(right.ftime), fposition(right.fposition),
          fdirection(right.fdirection), fpolarization(right.fpolarization)
    {
    }

    unsigned fid{0};
    G4double fenergy{0};
    G4double ftime{0};
    G4ThreeVector fposition;
    G4ThreeVector fdirection;
    G4ThreeVector fpolarization;
};

using PhotonHitsCollection = G4THitsCollection<PhotonHit>;

struct PhotonSD : public G4VSensitiveDetector
{
    PhotonHitsCollection *fPhotonHitsCollection{nullptr};
    G4int fHCID{-1};
    G4int fTotalG4Hits{0};

    PhotonSD(const G4String &name) : G4VSensitiveDetector(name)
    {
        collectionName.insert("photon_hits");
    }

    void Initialize(G4HCofThisEvent *hce) override
    {
        fPhotonHitsCollection = new PhotonHitsCollection(SensitiveDetectorName, collectionName[0]);
        if (fHCID < 0)
            fHCID = G4SDManager::GetSDMpointer()->GetCollectionID(collectionName[0]);
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
        if (NbHits > 0)
        {
            G4AutoLock lock(&g4hits_mutex);
            for (G4int i = 0; i < NbHits; i++)
            {
                PhotonHit *hit = (*fPhotonHitsCollection)[i];
                float wl = (hit->fenergy > 0) ? static_cast<float>(1239.84198 / hit->fenergy) : 0.f;
                g4_accumulated_hits.push_back({float(hit->fposition.x()), float(hit->fposition.y()),
                                               float(hit->fposition.z()), float(hit->ftime), float(hit->fdirection.x()),
                                               float(hit->fdirection.y()), float(hit->fdirection.z()), 0.f,
                                               float(hit->fpolarization.x()), float(hit->fpolarization.y()),
                                               float(hit->fpolarization.z()), wl, 0.f, 0.f, 0.f, float(hit->fid)});
            }
            fTotalG4Hits += NbHits;
        }
    }

    G4int GetTotalG4Hits() const
    {
        return fTotalG4Hits;
    }
};

// ============================================================================
// Detector construction
// ============================================================================

struct DetectorConstruction : G4VUserDetectorConstruction
{
    std::filesystem::path gdml_file_;
    G4GDMLParser parser_;

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
        if (lvStore && !lvStore->empty())
        {
            for (auto lv : *lvStore)
            {
                G4String name = str_tolower(lv->GetName());
                if (name.find("detect") != std::string::npos || name.find("sipm") != std::string::npos ||
                    name.find("sensor") != std::string::npos || name.find("pmt") != std::string::npos ||
                    name.find("arapuca") != std::string::npos)
                {
                    G4String sdName = "PhotonDetector_" + lv->GetName();
                    if (!G4SDManager::GetSDMpointer()->FindSensitiveDetector(sdName, false))
                    {
                        PhotonSD *photonSD = new PhotonSD(sdName);
                        G4SDManager::GetSDMpointer()->AddNewDetector(photonSD);
                        lv->SetSensitiveDetector(photonSD);
                    }
                }
                G4VSolid *solid = lv->GetSolid();
                if (solid && IsSubtractionSolid(solid))
                    lv->SetVisAttributes(&invisibleVisAttr);
            }
        }
        return world;
    }
};

// ============================================================================
// Primary generator
// ============================================================================

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

        G4PrimaryParticle *particle = new G4PrimaryParticle(G4Electron::Definition());
        particle->SetKineticEnergy(10 * MeV);
        particle->SetMomentumDirection(direction);

        G4PrimaryVertex *vertex = new G4PrimaryVertex(position_mm, time_ns);
        vertex->SetPrimary(particle);
        event->AddPrimaryVertex(vertex);
    }
};

// ============================================================================
// Event action
// ============================================================================

struct EventAction : G4UserEventAction
{
    SEvt *sev;
    G4int fTotalG4Hits{0};

    EventAction(SEvt *sev) : sev(sev)
    {
    }

    void EndOfEventAction(const G4Event *event) override
    {
        G4HCofThisEvent *hce = event->GetHCofThisEvent();
        if (!hce)
            return;
        G4int nCollections = hce->GetNumberOfCollections();
        for (G4int i = 0; i < nCollections; i++)
        {
            G4VHitsCollection *hc = hce->GetHC(i);
            if (!hc)
                continue;
            PhotonHitsCollection *phc = dynamic_cast<PhotonHitsCollection *>(hc);
            if (phc)
                fTotalG4Hits += phc->entries();
            else
                fTotalG4Hits += hc->GetSize();
        }
    }

    G4int GetTotalG4Hits() const
    {
        return fTotalG4Hits;
    }
};

// ============================================================================
// Forward declarations
// ============================================================================

struct SteppingAction;
struct TrackingAction;

// ============================================================================
// RunAction — manages GPU lifecycle (sync or async)
// ============================================================================

struct RunAction : G4UserRunAction
{
    EventAction *fEventAction;
    GPUTaskManager *fGPUTaskMgr{nullptr}; // null = sync mode

    RunAction(EventAction *eventAction, GPUTaskManager *gpuMgr = nullptr)
        : fEventAction(eventAction), fGPUTaskMgr(gpuMgr)
    {
    }

    void BeginOfRunAction(const G4Run *) override
    {
        if (G4Threading::IsMasterThread() && fGPUTaskMgr)
            fGPUTaskMgr->start();
    }

    void EndOfRunAction(const G4Run *) override
    {
        if (!G4Threading::IsMasterThread())
            return;

        if (fGPUTaskMgr)
        {
            // Async mode: flush remaining gensteps and wait
            fGPUTaskMgr->flushRemaining(0);

            G4cout << "\n=== Async GPU Summary ===" << G4endl;
            G4cout << "Batches processed: " << fGPUTaskMgr->getCompletedBatches() << G4endl;
            G4cout << "Total GPU photons: " << fGPUTaskMgr->getTotalPhotons() << G4endl;
            G4cout << "Total GPU hits:    " << fGPUTaskMgr->getTotalHits() << G4endl;
            G4cout << "Total GPU time:    " << fGPUTaskMgr->getTotalGPUTime() << " s" << G4endl;
            G4cout << "G4 hits:           " << fEventAction->GetTotalG4Hits() << G4endl;
        }
        else
        {
            // Sync mode: run all gensteps at once
            G4CXOpticks *gx = G4CXOpticks::Get();

            auto start = std::chrono::high_resolution_clock::now();
            gx->simulate(0, false);
            cudaDeviceSynchronize();
            auto end = std::chrono::high_resolution_clock::now();
            std::chrono::duration<double> elapsed = end - start;

            SEvt *sev = SEvt::Get_EGPU();
            unsigned int num_hits = sev->GetNumHit(0);

            G4cout << "\n=== Sync GPU Summary ===" << G4endl;
            G4cout << "GPU sim time:      " << elapsed.count() << " s" << G4endl;
            G4cout << "Gensteps:          " << sev->GetNumGenstepFromGenstep(0) << G4endl;
            G4cout << "Photons collected: " << sev->GetNumPhotonCollected(0) << G4endl;
            G4cout << "GPU hits:          " << num_hits << G4endl;
            G4cout << "G4 hits:           " << fEventAction->GetTotalG4Hits() << G4endl;

            // Save GPU hits
            if (num_hits > 0)
            {
                NP *gpu_h = NP::Make<float>(num_hits, 4, 4);
                for (unsigned idx = 0; idx < num_hits; idx++)
                {
                    sphoton hit;
                    sev->getHit(hit, idx);
                    memcpy(gpu_h->bytes() + idx * sizeof(sphoton), &hit, sizeof(sphoton));
                }
                gpu_h->save("gpu_hits.npy");
                G4cout << "Saved GPU hits to gpu_hits.npy" << G4endl;
            }
        }

        // Save G4 hits
        {
            G4AutoLock lock(&g4hits_mutex);
            size_t ng4 = g4_accumulated_hits.size();
            if (ng4 > 0)
            {
                NP *g4h = NP::Make<float>(ng4, 4, 4);
                memcpy(g4h->bytes(), g4_accumulated_hits.data(), ng4 * 16 * sizeof(float));
                g4h->save("g4_hits.npy");
                G4cout << "Saved G4 hits (" << ng4 << ") to g4_hits.npy" << G4endl;
            }
        }
    }
};

// ============================================================================
// SteppingAction — routes gensteps to SEvt (sync) or GPUTaskManager (async)
// ============================================================================

struct SteppingAction : G4UserSteppingAction
{
    SEvt *sev;
    GPUTaskManager *fGPUTaskMgr{nullptr};

    SteppingAction(SEvt *sev, GPUTaskManager *gpuMgr = nullptr) : sev(sev), fGPUTaskMgr(gpuMgr)
    {
    }

    void UserSteppingAction(const G4Step *aStep) override
    {
        G4Track *aTrack;
        G4int fNumPhotons = 0;

        G4StepPoint *postStep = aStep->GetPostStepPoint();
        G4VPhysicalVolume *volume = postStep->GetPhysicalVolume();

        // Kill optical photons stuck in reflection loops
        if (aStep->GetTrack()->GetDefinition() == G4OpticalPhoton::OpticalPhotonDefinition())
        {
            if (aStep->GetTrack()->GetCurrentStepNumber() > 10000)
                aStep->GetTrack()->SetTrackStatus(fStopAndKill);
        }

        // Collect gensteps from Cerenkov and Scintillation processes
        G4SteppingManager *fpSteppingManager =
            G4EventManager::GetEventManager()->GetTrackingManager()->GetSteppingManager();
        G4StepStatus stepStatus = fpSteppingManager->GetfStepStatus();

        if (stepStatus == fAtRestDoItProc)
            return;

        G4ProcessVector *procPost = fpSteppingManager->GetfPostStepDoItVector();
        size_t MAXofPostStepLoops = fpSteppingManager->GetMAXofPostStepLoops();

        for (size_t i3 = 0; i3 < MAXofPostStepLoops; i3++)
        {
            // --- Cerenkov ---
            if ((*procPost)[i3]->GetProcessName() == "Cerenkov")
            {
                aTrack = aStep->GetTrack();
                const G4DynamicParticle *aParticle = aTrack->GetDynamicParticle();
                G4double charge = aParticle->GetDefinition()->GetPDGCharge();
                const G4Material *aMaterial = aTrack->GetMaterial();
                G4MaterialPropertiesTable *MPT = aMaterial->GetMaterialPropertiesTable();
                G4MaterialPropertyVector *Rindex = MPT->GetProperty(kRINDEX);

                if (!Rindex || Rindex->GetVectorLength() == 0)
                    return;

                G4Cerenkov *proc = (G4Cerenkov *)(*procPost)[i3];
                fNumPhotons = proc->GetNumPhotons();

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
                    G4double MeanNumberOfPhotons1 = proc->GetAverageNumberOfPhotons(charge, beta1, aMaterial, Rindex);
                    G4double MeanNumberOfPhotons2 = proc->GetAverageNumberOfPhotons(charge, beta2, aMaterial, Rindex);

                    if (fGPUTaskMgr)
                    {
                        // ASYNC: construct quad6 directly into buffer
                        const G4Event *event = G4EventManager::GetEventManager()->GetConstCurrentEvent();
                        if (!event)
                            return;
                        quad6 gs = MakeGenstep_Cerenkov(aTrack, aStep, fNumPhotons, BetaInverse, Pmin, Pmax, maxCos,
                                                        maxSin2, MeanNumberOfPhotons1, MeanNumberOfPhotons2);
                        fGPUTaskMgr->addGenstep(gs, fNumPhotons, event->GetEventID());
                    }
                    else
                    {
                        // SYNC: standard SEvt path
                        G4AutoLock lock(&genstep_mutex);
                        U4::CollectGenstep_G4Cerenkov_modified(aTrack, aStep, fNumPhotons, BetaInverse, Pmin, Pmax,
                                                               maxCos, maxSin2, MeanNumberOfPhotons1,
                                                               MeanNumberOfPhotons2);
                    }
                }
            }

            // --- Scintillation ---
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
                        return;

                    const G4int tcKeys[3] = {kSCINTILLATIONTIMECONSTANT1, kSCINTILLATIONTIMECONSTANT2,
                                             kSCINTILLATIONTIMECONSTANT3};
                    const G4int yieldKeys[3] = {kSCINTILLATIONYIELD1, kSCINTILLATIONYIELD2, kSCINTILLATIONYIELD3};

                    G4double tc[3] = {0, 0, 0};
                    G4double yield[3] = {0, 0, 0};
                    G4double yieldSum = 0;
                    G4int nComp = 0;

                    for (G4int c = 0; c < 3; c++)
                    {
                        if (MPT->ConstPropertyExists(tcKeys[c]))
                        {
                            tc[c] = MPT->GetConstProperty(tcKeys[c]);
                            yield[c] = MPT->ConstPropertyExists(yieldKeys[c]) ? MPT->GetConstProperty(yieldKeys[c])
                                                                              : (c == 0 ? 1.0 : 0.0);
                            yieldSum += yield[c];
                            nComp = c + 1;
                        }
                    }

                    if (fGPUTaskMgr)
                    {
                        // ASYNC: construct quad6 directly into buffer
                        const G4Event *event = G4EventManager::GetEventManager()->GetConstCurrentEvent();
                        if (!event)
                            return;
                        int eventid = event->GetEventID();

                        G4int nRemaining = fNumPhotons;
                        for (G4int c = 0; c < nComp; c++)
                        {
                            G4int nPhotComp;
                            if (c == nComp - 1)
                                nPhotComp = nRemaining;
                            else
                                nPhotComp = static_cast<G4int>(fNumPhotons * yield[c] / yieldSum);
                            nRemaining -= nPhotComp;

                            if (nPhotComp > 0)
                            {
                                quad6 gs = MakeGenstep_Scintillation(aTrack, aStep, nPhotComp, c + 1, tc[c]);
                                fGPUTaskMgr->addGenstep(gs, nPhotComp, eventid);
                            }
                        }
                    }
                    else
                    {
                        // SYNC: standard SEvt path
                        G4AutoLock lock(&genstep_mutex);
                        G4int nRemaining = fNumPhotons;
                        for (G4int c = 0; c < nComp; c++)
                        {
                            G4int nPhotComp;
                            if (c == nComp - 1)
                                nPhotComp = nRemaining;
                            else
                                nPhotComp = static_cast<G4int>(fNumPhotons * yield[c] / yieldSum);
                            nRemaining -= nPhotComp;

                            if (nPhotComp > 0)
                                U4::CollectGenstep_DsG4Scintillation_r4695(aTrack, aStep, nPhotComp, c + 1, tc[c]);
                        }
                    }
                }
            }
        }
    }
};

// ============================================================================
// TrackingAction
// ============================================================================

struct TrackingAction : G4UserTrackingAction
{
    const G4Track *transient_fSuspend_track = nullptr;
    SEvt *sev;

    TrackingAction(SEvt *sev) : sev(sev)
    {
    }

    void PreUserTrackingAction(const G4Track *) override
    {
    }

    void PostUserTrackingAction(const G4Track *track) override
    {
        if (track->GetTrackStatus() == fSuspend)
            transient_fSuspend_track = track;
    }
};

// ============================================================================
// G4App — wires everything together
// ============================================================================

struct G4App
{
    SEvt *sev;
    GPUTaskManager *gpu_task_mgr_;

    G4VUserDetectorConstruction *det_cons_;
    G4VUserPrimaryGeneratorAction *prim_gen_;
    EventAction *event_act_;
    RunAction *run_act_;
    SteppingAction *stepping_;
    TrackingAction *tracking_;

    G4App(std::filesystem::path gdml_file, bool enable_async = true)
        : sev(SEvt::CreateOrReuse_EGPU()), gpu_task_mgr_(enable_async ? new GPUTaskManager() : nullptr),
          det_cons_(new DetectorConstruction(gdml_file)), prim_gen_(new PrimaryGenerator(sev)),
          event_act_(new EventAction(sev)), run_act_(new RunAction(event_act_, gpu_task_mgr_)),
          stepping_(new SteppingAction(sev, gpu_task_mgr_)), tracking_(new TrackingAction(sev))
    {
        if (gpu_task_mgr_)
            G4cout << "G4App: Async GPU mode (threshold=" << gpu_task_mgr_->getThreshold() << " photons)" << G4endl;
        else
            G4cout << "G4App: Sync GPU mode (end-of-run)" << G4endl;
    }

    ~G4App()
    {
        delete gpu_task_mgr_;
    }
};
