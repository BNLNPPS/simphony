#pragma once
//
// async_gpu_std.h — Async CPU+GPU optical photon simulation, std-only.
//
// Same double-buffering pattern as examples/async_gpu_launch but without
// G4TaskGroup / G4Mutex.  The GPU worker is a plain std::thread driven by
// std::mutex + std::condition_variable + std::queue, with explicit
// backpressure controlled by GPU_MAX_QUEUE_SIZE.
//
// Modes:
//   --async   : double-buffered async GPU processing (default)
//   --sync    : end-of-run GPU simulation (one batch)
//

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <mutex>
#include <queue>
#include <sstream>
#include <thread>
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
G4Mutex genstep_mutex = G4MUTEX_INITIALIZER; // sync-mode SEvt collection
std::mutex g4hits_mutex;                     // accumulator across worker threads
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
    if (G4BooleanSolid *bs = dynamic_cast<G4BooleanSolid *>(solid))
    {
        if (IsSubtractionSolid(bs->GetConstituentSolid(0)) || IsSubtractionSolid(bs->GetConstituentSolid(1)))
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
// GenstepBuffer — accumulates gensteps for one GPU batch
// ============================================================================

struct GenstepBuffer
{
    std::vector<quad6> gensteps;
    std::vector<sgs> labels;
    int64_t photon_count = 0;
    int64_t genstep_count = 0;
    int event_id = 0;

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

struct GPUTask
{
    int batch_id;
    std::shared_ptr<GenstepBuffer> buffer;

    GPUTask() : batch_id(-1), buffer(nullptr)
    {
    }
    GPUTask(int bid, std::shared_ptr<GenstepBuffer> buf) : batch_id(bid), buffer(std::move(buf))
    {
    }
};

// ============================================================================
// GPUTaskManager — std::thread + std::queue worker, no G4Task
//
// CPU thread(s) push completed buffers into task_queue_ via submitBuffer()
// when the photon threshold is reached.  A single worker std::thread pops
// tasks and runs G4CXOpticks::simulate().  The queue has a fixed depth
// (GPU_MAX_QUEUE_SIZE) so producers block when the GPU falls behind.
// ============================================================================

class GPUTaskManager
{
  public:
    static constexpr int64_t DEFAULT_PHOTON_THRESHOLD = 10000000; // 10M photons
    static constexpr size_t DEFAULT_MAX_QUEUE_SIZE = 3;

  private:
    int64_t photon_threshold_;
    size_t max_queue_size_;

    std::shared_ptr<GenstepBuffer> active_buffer_;
    std::mutex buffer_mutex_;

    std::queue<GPUTask> task_queue_;
    std::mutex queue_mutex_;
    std::condition_variable queue_not_full_;
    std::condition_variable queue_not_empty_;
    std::condition_variable queue_drained_;

    std::thread worker_;
    std::atomic<bool> shutdown_{false};
    std::atomic<bool> running_{false};

    std::atomic<int> batch_counter_{0};
    std::atomic<int> completed_batches_{0};
    std::atomic<uint64_t> total_hits_{0};
    std::atomic<uint64_t> total_photons_{0};
    std::atomic<uint64_t> total_gpu_time_us_{0};

  public:
    GPUTaskManager(int64_t threshold = DEFAULT_PHOTON_THRESHOLD, size_t max_queue = DEFAULT_MAX_QUEUE_SIZE)
        : photon_threshold_(threshold), max_queue_size_(max_queue), active_buffer_(std::make_shared<GenstepBuffer>())
    {
        if (const char *e = std::getenv("GPU_PHOTON_FLUSH_THRESHOLD"))
            photon_threshold_ = std::atoll(e);
        if (const char *e = std::getenv("GPU_MAX_QUEUE_SIZE"))
            max_queue_size_ = std::max(1, std::atoi(e));
    }

    ~GPUTaskManager()
    {
        shutdown();
    }

    void start()
    {
        if (running_.exchange(true))
            return;
        shutdown_ = false;
        worker_ = std::thread(&GPUTaskManager::workerLoop, this);

        G4cout << "GPUTaskManager [std]: started"
               << " threshold=" << photon_threshold_ << " max_queue=" << max_queue_size_ << G4endl;
    }

    void shutdown()
    {
        if (!running_.exchange(false))
            return;

        // Flush any remaining gensteps in the active buffer
        std::shared_ptr<GenstepBuffer> remainder;
        {
            std::lock_guard<std::mutex> lock(buffer_mutex_);
            if (active_buffer_ && !active_buffer_->empty())
            {
                remainder = active_buffer_;
                active_buffer_ = std::make_shared<GenstepBuffer>();
            }
        }
        if (remainder)
            submitBuffer(remainder);

        {
            std::lock_guard<std::mutex> lock(queue_mutex_);
            shutdown_ = true;
        }
        queue_not_empty_.notify_all();
        queue_not_full_.notify_all();

        if (worker_.joinable())
            worker_.join();

        G4cout << "GPUTaskManager [std]: shutdown"
               << " batches=" << completed_batches_.load() << " photons=" << total_photons_.load()
               << " hits=" << total_hits_.load() << " gpu_time=" << (total_gpu_time_us_.load() / 1e6) << "s" << G4endl;
    }

    // Hot path — invoked from SteppingAction
    void addGenstep(const quad6 &gs, int64_t numphotons, int eventID)
    {
        std::shared_ptr<GenstepBuffer> to_submit;
        {
            std::lock_guard<std::mutex> lock(buffer_mutex_);
            active_buffer_->event_id = eventID;
            active_buffer_->addGenstep(gs, numphotons);

            if (active_buffer_->photon_count >= photon_threshold_)
            {
                to_submit = active_buffer_;
                active_buffer_ = std::make_shared<GenstepBuffer>();
            }
        }
        if (to_submit)
            submitBuffer(to_submit);
    }

    void flushRemaining(int eventID)
    {
        std::shared_ptr<GenstepBuffer> to_submit;
        {
            std::lock_guard<std::mutex> lock(buffer_mutex_);
            if (active_buffer_ && !active_buffer_->empty())
            {
                active_buffer_->event_id = eventID;
                to_submit = active_buffer_;
                active_buffer_ = std::make_shared<GenstepBuffer>();
            }
        }
        if (to_submit)
        {
            G4cout << "GPUTaskManager [std]: final flush (" << to_submit->photon_count << " photons)" << G4endl;
            submitBuffer(to_submit);
        }
        waitForDrain();
    }

    void waitForDrain()
    {
        std::unique_lock<std::mutex> lock(queue_mutex_);
        queue_drained_.wait(lock, [this]() { return task_queue_.empty(); });
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
        if (!buffer || buffer->empty())
            return;

        int batch_id = batch_counter_.fetch_add(1);
        GPUTask task(batch_id, buffer);

        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            queue_not_full_.wait(lock, [this]() { return task_queue_.size() < max_queue_size_ || shutdown_; });
            if (shutdown_)
                return;

            task_queue_.push(std::move(task));
            G4cout << "GPUTaskManager [std]: queued batch " << batch_id << " (" << buffer->photon_count << " photons, "
                   << buffer->genstep_count << " gensteps)"
                   << " queue_size=" << task_queue_.size() << G4endl;
        }
        queue_not_empty_.notify_one();
    }

    void workerLoop()
    {
        G4cout << "GPUTaskManager [std]: worker thread started (tid=" << std::this_thread::get_id() << ")" << G4endl;

        while (true)
        {
            GPUTask task;
            {
                std::unique_lock<std::mutex> lock(queue_mutex_);
                queue_not_empty_.wait(lock, [this]() { return !task_queue_.empty() || shutdown_; });
                if (task_queue_.empty() && shutdown_)
                    break;

                task = std::move(task_queue_.front());
                task_queue_.pop();
            }
            queue_not_full_.notify_one();

            if (task.buffer)
                runBatch(task.batch_id, task.buffer);

            {
                std::lock_guard<std::mutex> lock(queue_mutex_);
                if (task_queue_.empty())
                    queue_drained_.notify_all();
            }
        }
        G4cout << "GPUTaskManager [std]: worker thread exiting" << G4endl;
    }

    void runBatch(int batch_id, std::shared_ptr<GenstepBuffer> buffer)
    {
        G4cout << "=== GPU Batch " << batch_id << " ==="
               << " photons=" << buffer->photon_count << " gensteps=" << buffer->genstep_count << G4endl;

        G4CXOpticks *gx = G4CXOpticks::Get();
        SEvt *sev = SEvt::Get_EGPU();
        if (!gx || !sev)
        {
            G4cerr << "GPUTaskManager [std]: G4CXOpticks/SEvt not available" << G4endl;
            return;
        }

        sev->clear_genstep();
        NP *gs_array = NP::Make<float>(buffer->gensteps.size(), 6, 4);
        std::memcpy(gs_array->values<float>(), buffer->gensteps.data(), buffer->gensteps.size() * sizeof(quad6));
        sev->addGenstep(gs_array);

        auto t0 = std::chrono::high_resolution_clock::now();
        gx->simulate(buffer->event_id, false);
        cudaDeviceSynchronize();
        auto t1 = std::chrono::high_resolution_clock::now();
        auto us = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();

        unsigned num_hits = sev->GetNumHit(0);
        total_gpu_time_us_ += us;
        total_hits_ += num_hits;
        total_photons_ += buffer->photon_count;

        G4cout << "  gpu_time=" << (us / 1e6) << "s hits=" << num_hits << G4endl;
        if (num_hits > 0)
            saveHits(batch_id, sev, num_hits);

        gx->reset(buffer->event_id);
        completed_batches_++;
        G4cout << "=== GPU Batch " << batch_id << " complete ===" << G4endl;
    }

    void saveHits(int batch_id, SEvt *sev, unsigned num_hits)
    {
        std::ostringstream fname;
        fname << "gpu_hits_batch_" << batch_id << ".npy";

        NP *arr = NP::Make<float>(num_hits, 4, 4);
        for (unsigned idx = 0; idx < num_hits; idx++)
        {
            sphoton hit;
            sev->getHit(hit, idx);
            std::memcpy(arr->bytes() + idx * sizeof(sphoton), &hit, sizeof(sphoton));
        }
        arr->save(fname.str().c_str());
        G4cout << "  saved " << num_hits << " hits to " << fname.str() << G4endl;
    }
};

// ============================================================================
// Genstep construction (bypass U4 / SEvt for async path)
// ============================================================================

static quad6 MakeGenstep_Cerenkov(const G4Track *aTrack, const G4Step *aStep, G4int numPhotons, G4double betaInverse,
                                  G4double pmin, G4double pmax, G4double maxCos, G4double maxSin2,
                                  G4double meanNumberOfPhotons1, G4double meanNumberOfPhotons2)
{
    G4StepPoint *pre = aStep->GetPreStepPoint();
    G4StepPoint *post = aStep->GetPostStepPoint();
    G4ThreeVector x0 = pre->GetPosition();
    G4double t0 = pre->GetGlobalTime();
    G4ThreeVector dx = aStep->GetDeltaPosition();
    const G4DynamicParticle *dp = aTrack->GetDynamicParticle();
    const G4Material *mat = aTrack->GetMaterial();

    G4double Wmin_nm = h_Planck * c_light / pmax / nm;
    G4double Wmax_nm = h_Planck * c_light / pmin / nm;

    quad6 gs;
    gs.zero();
    scerenkov *ck = (scerenkov *)(&gs);

    ck->gentype = OpticksGenstep_G4Cerenkov_modified;
    ck->trackid = aTrack->GetTrackID();
    ck->matline = mat->GetIndex() + SEvt::G4_INDEX_OFFSET;
    ck->numphoton = numPhotons;

    ck->pos.x = x0.x();
    ck->pos.y = x0.y();
    ck->pos.z = x0.z();
    ck->time = t0;

    ck->DeltaPosition.x = dx.x();
    ck->DeltaPosition.y = dx.y();
    ck->DeltaPosition.z = dx.z();
    ck->step_length = aStep->GetStepLength();

    ck->code = dp->GetDefinition()->GetPDGEncoding();
    ck->charge = dp->GetDefinition()->GetPDGCharge();
    ck->weight = aTrack->GetWeight();
    ck->preVelocity = pre->GetVelocity();

    ck->BetaInverse = betaInverse;
    ck->Wmin = Wmin_nm;
    ck->Wmax = Wmax_nm;
    ck->maxCos = maxCos;

    ck->maxSin2 = maxSin2;
    ck->MeanNumberOfPhotons1 = meanNumberOfPhotons1;
    ck->MeanNumberOfPhotons2 = meanNumberOfPhotons2;
    ck->postVelocity = post->GetVelocity();
    return gs;
}

static quad6 MakeGenstep_Scintillation(const G4Track *aTrack, const G4Step *aStep, G4int numPhotons, G4int scnt,
                                       G4double ScintillationTime)
{
    G4StepPoint *pre = aStep->GetPreStepPoint();
    G4StepPoint *post = aStep->GetPostStepPoint();
    G4ThreeVector x0 = pre->GetPosition();
    G4double t0 = pre->GetGlobalTime();
    G4ThreeVector dx = aStep->GetDeltaPosition();
    G4double meanV = (pre->GetVelocity() + post->GetVelocity()) * 0.5;
    const G4DynamicParticle *dp = aTrack->GetDynamicParticle();
    const G4Material *mat = aTrack->GetMaterial();

    quad6 gs;
    gs.zero();
    sscint *sc = (sscint *)(&gs);

    sc->gentype = OpticksGenstep_DsG4Scintillation_r4695;
    sc->trackid = aTrack->GetTrackID();
    sc->matline = mat->GetIndex() + SEvt::G4_INDEX_OFFSET;
    sc->numphoton = numPhotons;

    sc->pos.x = x0.x();
    sc->pos.y = x0.y();
    sc->pos.z = x0.z();
    sc->time = t0;

    sc->DeltaPosition.x = dx.x();
    sc->DeltaPosition.y = dx.y();
    sc->DeltaPosition.z = dx.z();
    sc->step_length = aStep->GetStepLength();

    sc->code = dp->GetDefinition()->GetPDGEncoding();
    sc->charge = dp->GetDefinition()->GetPDGCharge();
    sc->weight = aTrack->GetWeight();
    sc->meanVelocity = meanV;

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
        G4Track *track = aStep->GetTrack();
        if (track->GetDefinition() != G4OpticalPhoton::OpticalPhotonDefinition())
            return false;

        G4double energy_eV = track->GetTotalEnergy() / CLHEP::eV;
        PhotonHit *hit = new PhotonHit(0, energy_eV, track->GetGlobalTime(), aStep->GetPostStepPoint()->GetPosition(),
                                       aStep->GetPostStepPoint()->GetMomentumDirection(),
                                       aStep->GetPostStepPoint()->GetPolarization());
        fPhotonHitsCollection->insert(hit);
        track->SetTrackStatus(fStopAndKill);
        return true;
    }

    void EndOfEvent(G4HCofThisEvent *) override
    {
        G4int n = fPhotonHitsCollection->entries();
        if (n <= 0)
            return;

        std::lock_guard<std::mutex> lock(g4hits_mutex);
        for (G4int i = 0; i < n; i++)
        {
            PhotonHit *h = (*fPhotonHitsCollection)[i];
            float wl = (h->fenergy > 0) ? static_cast<float>(1239.84198 / h->fenergy) : 0.f;
            g4_accumulated_hits.push_back({float(h->fposition.x()), float(h->fposition.y()), float(h->fposition.z()),
                                           float(h->ftime), float(h->fdirection.x()), float(h->fdirection.y()),
                                           float(h->fdirection.z()), 0.f, float(h->fpolarization.x()),
                                           float(h->fpolarization.y()), float(h->fpolarization.z()), wl, 0.f, 0.f, 0.f,
                                           float(h->fid)});
        }
        fTotalG4Hits += n;
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
                G4String lname = str_tolower(lv->GetName());
                if (lname.find("detect") != std::string::npos || lname.find("sipm") != std::string::npos ||
                    lname.find("sensor") != std::string::npos || lname.find("pmt") != std::string::npos ||
                    lname.find("arapuca") != std::string::npos)
                {
                    G4String sdName = "PhotonDetector_" + lv->GetName();
                    if (!G4SDManager::GetSDMpointer()->FindSensitiveDetector(sdName, false))
                    {
                        PhotonSD *sd = new PhotonSD(sdName);
                        G4SDManager::GetSDMpointer()->AddNewDetector(sd);
                        lv->SetSensitiveDetector(sd);
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
        G4ThreeVector pos(0.0 * m, 0.0 * m, 0.0 * m);
        G4double time_ns = 0;
        G4ThreeVector dir(0, 0.2, 0.8);

        G4PrimaryParticle *p = new G4PrimaryParticle(G4Electron::Definition());
        p->SetKineticEnergy(10 * MeV);
        p->SetMomentumDirection(dir);

        G4PrimaryVertex *v = new G4PrimaryVertex(pos, time_ns);
        v->SetPrimary(p);
        event->AddPrimaryVertex(v);
    }
};

// ============================================================================
// Event action — count G4 hits across all collections
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
        G4int n = hce->GetNumberOfCollections();
        for (G4int i = 0; i < n; i++)
        {
            G4VHitsCollection *hc = hce->GetHC(i);
            if (!hc)
                continue;
            if (auto *phc = dynamic_cast<PhotonHitsCollection *>(hc))
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
// Run action — drives the GPU lifecycle (sync or async)
// ============================================================================

struct RunAction : G4UserRunAction
{
    EventAction *fEventAction;
    GPUTaskManager *fGPUTaskMgr{nullptr};

    RunAction(EventAction *ea, GPUTaskManager *mgr = nullptr) : fEventAction(ea), fGPUTaskMgr(mgr)
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
            fGPUTaskMgr->flushRemaining(0);

            G4cout << "\n=== Async GPU Summary (std) ===" << G4endl;
            G4cout << "Batches processed: " << fGPUTaskMgr->getCompletedBatches() << G4endl;
            G4cout << "Total GPU photons: " << fGPUTaskMgr->getTotalPhotons() << G4endl;
            G4cout << "Total GPU hits:    " << fGPUTaskMgr->getTotalHits() << G4endl;
            G4cout << "Total GPU time:    " << fGPUTaskMgr->getTotalGPUTime() << " s" << G4endl;
            G4cout << "G4 hits:           " << fEventAction->GetTotalG4Hits() << G4endl;
        }
        else
        {
            G4CXOpticks *gx = G4CXOpticks::Get();
            auto t0 = std::chrono::high_resolution_clock::now();
            gx->simulate(0, false);
            cudaDeviceSynchronize();
            auto t1 = std::chrono::high_resolution_clock::now();
            std::chrono::duration<double> elapsed = t1 - t0;

            SEvt *sev = SEvt::Get_EGPU();
            unsigned num_hits = sev->GetNumHit(0);

            G4cout << "\n=== Sync GPU Summary ===" << G4endl;
            G4cout << "GPU sim time:      " << elapsed.count() << " s" << G4endl;
            G4cout << "Gensteps:          " << sev->GetNumGenstepFromGenstep(0) << G4endl;
            G4cout << "Photons collected: " << sev->GetNumPhotonCollected(0) << G4endl;
            G4cout << "GPU hits:          " << num_hits << G4endl;
            G4cout << "G4 hits:           " << fEventAction->GetTotalG4Hits() << G4endl;

            if (num_hits > 0)
            {
                NP *gpu_h = NP::Make<float>(num_hits, 4, 4);
                for (unsigned idx = 0; idx < num_hits; idx++)
                {
                    sphoton hit;
                    sev->getHit(hit, idx);
                    std::memcpy(gpu_h->bytes() + idx * sizeof(sphoton), &hit, sizeof(sphoton));
                }
                gpu_h->save("gpu_hits.npy");
                G4cout << "Saved GPU hits to gpu_hits.npy" << G4endl;
            }
        }

        std::lock_guard<std::mutex> lock(g4hits_mutex);
        size_t ng4 = g4_accumulated_hits.size();
        if (ng4 > 0)
        {
            NP *g4h = NP::Make<float>(ng4, 4, 4);
            std::memcpy(g4h->bytes(), g4_accumulated_hits.data(), ng4 * 16 * sizeof(float));
            g4h->save("g4_hits.npy");
            G4cout << "Saved G4 hits (" << ng4 << ") to g4_hits.npy" << G4endl;
        }
    }
};

// ============================================================================
// Stepping action — collect Cerenkov / scintillation gensteps
// ============================================================================

struct SteppingAction : G4UserSteppingAction
{
    SEvt *sev;
    GPUTaskManager *fGPUTaskMgr{nullptr};

    SteppingAction(SEvt *sev, GPUTaskManager *mgr = nullptr) : sev(sev), fGPUTaskMgr(mgr)
    {
    }

    void UserSteppingAction(const G4Step *aStep) override
    {
        // Cap optical photon step count so reflection loops can't run forever
        if (aStep->GetTrack()->GetDefinition() == G4OpticalPhoton::OpticalPhotonDefinition() &&
            aStep->GetTrack()->GetCurrentStepNumber() > 10000)
            aStep->GetTrack()->SetTrackStatus(fStopAndKill);

        G4SteppingManager *sm = G4EventManager::GetEventManager()->GetTrackingManager()->GetSteppingManager();
        if (sm->GetfStepStatus() == fAtRestDoItProc)
            return;

        G4ProcessVector *procPost = sm->GetfPostStepDoItVector();
        size_t MAXofPostStepLoops = sm->GetMAXofPostStepLoops();

        for (size_t i = 0; i < MAXofPostStepLoops; i++)
        {
            const G4String &pname = (*procPost)[i]->GetProcessName();

            if (pname == "Cerenkov")
            {
                G4Track *track = aStep->GetTrack();
                const G4DynamicParticle *dp = track->GetDynamicParticle();
                G4double charge = dp->GetDefinition()->GetPDGCharge();
                const G4Material *mat = track->GetMaterial();
                G4MaterialPropertiesTable *MPT = mat->GetMaterialPropertiesTable();
                G4MaterialPropertyVector *Rindex = MPT ? MPT->GetProperty(kRINDEX) : nullptr;
                if (!Rindex || Rindex->GetVectorLength() == 0)
                    return;

                G4Cerenkov *proc = (G4Cerenkov *)(*procPost)[i];
                G4int numPhotons = proc->GetNumPhotons();
                if (numPhotons <= 0)
                    continue;

                G4double Pmin = Rindex->Energy(0);
                G4double Pmax = Rindex->GetMaxEnergy();
                G4double nMax = Rindex->GetMaxValue();
                G4double beta1 = aStep->GetPreStepPoint()->GetBeta();
                G4double beta2 = aStep->GetPostStepPoint()->GetBeta();
                G4double beta = (beta1 + beta2) * 0.5;
                G4double BetaInverse = 1. / beta;
                G4double maxCos = BetaInverse / nMax;
                G4double maxSin2 = (1.0 - maxCos) * (1.0 + maxCos);
                G4double mean1 = proc->GetAverageNumberOfPhotons(charge, beta1, mat, Rindex);
                G4double mean2 = proc->GetAverageNumberOfPhotons(charge, beta2, mat, Rindex);

                if (fGPUTaskMgr)
                {
                    const G4Event *ev = G4EventManager::GetEventManager()->GetConstCurrentEvent();
                    if (!ev)
                        return;
                    quad6 gs = MakeGenstep_Cerenkov(track, aStep, numPhotons, BetaInverse, Pmin, Pmax, maxCos, maxSin2,
                                                    mean1, mean2);
                    fGPUTaskMgr->addGenstep(gs, numPhotons, ev->GetEventID());
                }
                else
                {
                    G4AutoLock lock(&genstep_mutex);
                    U4::CollectGenstep_G4Cerenkov_modified(track, aStep, numPhotons, BetaInverse, Pmin, Pmax, maxCos,
                                                           maxSin2, mean1, mean2);
                }
            }
            else if (pname == "Scintillation")
            {
                G4Scintillation *proc = (G4Scintillation *)(*procPost)[i];
                G4int numPhotons = proc->GetNumPhotons();
                if (numPhotons <= 0)
                    continue;

                G4Track *track = aStep->GetTrack();
                const G4Material *mat = track->GetMaterial();
                G4MaterialPropertiesTable *MPT = mat->GetMaterialPropertiesTable();
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
                    const G4Event *ev = G4EventManager::GetEventManager()->GetConstCurrentEvent();
                    if (!ev)
                        return;
                    int eventid = ev->GetEventID();
                    G4int remaining = numPhotons;
                    for (G4int c = 0; c < nComp; c++)
                    {
                        G4int n = (c == nComp - 1) ? remaining : static_cast<G4int>(numPhotons * yield[c] / yieldSum);
                        remaining -= n;
                        if (n > 0)
                        {
                            quad6 gs = MakeGenstep_Scintillation(track, aStep, n, c + 1, tc[c]);
                            fGPUTaskMgr->addGenstep(gs, n, eventid);
                        }
                    }
                }
                else
                {
                    G4AutoLock lock(&genstep_mutex);
                    G4int remaining = numPhotons;
                    for (G4int c = 0; c < nComp; c++)
                    {
                        G4int n = (c == nComp - 1) ? remaining : static_cast<G4int>(numPhotons * yield[c] / yieldSum);
                        remaining -= n;
                        if (n > 0)
                            U4::CollectGenstep_DsG4Scintillation_r4695(track, aStep, n, c + 1, tc[c]);
                    }
                }
            }
        }
    }
};

// ============================================================================
// Tracking action
// ============================================================================

struct TrackingAction : G4UserTrackingAction
{
    SEvt *sev;
    TrackingAction(SEvt *sev) : sev(sev)
    {
    }

    void PreUserTrackingAction(const G4Track *) override
    {
    }

    void PostUserTrackingAction(const G4Track *) override
    {
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
            G4cout << "G4App [std]: async GPU mode (threshold=" << gpu_task_mgr_->getThreshold() << " photons)"
                   << G4endl;
        else
            G4cout << "G4App [std]: sync GPU mode (end-of-run)" << G4endl;
    }

    ~G4App()
    {
        delete gpu_task_mgr_;
    }
};
