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
#include "G4VPhysicalVolume.hh"
#include "G4VUserActionInitialization.hh"
#include "G4VUserDetectorConstruction.hh"
#include "G4VUserPrimaryGeneratorAction.hh"

#include "sysrap/NP.hh"
#include "sysrap/sphoton.h"

#include "config.h"
#include "torch.h"

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

    G4OnlyRunAction(HitAccumulator *acc) : accumulator(acc) {}

    void EndOfRunAction(const G4Run *) override
    {
        if (G4Threading::IsMasterThread() || !G4Threading::IsMultithreadedApplication())
        {
            G4cout << "G4: Total accumulated hits: " << accumulator->hits.size() << G4endl;
            accumulator->Save("g4_hits.npy");
        }
    }
};

// ---- Action Initialization (required for MT) ----

struct G4OnlyActionInitialization : G4VUserActionInitialization
{
    gphox::Config cfg;
    HitAccumulator *accumulator;
    int photons_per_event;
    int num_events;

    G4OnlyActionInitialization(const gphox::Config &cfg, HitAccumulator *acc,
                               int photons_per_event, int num_events)
        : cfg(cfg), accumulator(acc), photons_per_event(photons_per_event),
          num_events(num_events) {}

    void BuildForMaster() const override
    {
        SetUserAction(new G4OnlyRunAction(accumulator));
    }

    void Build() const override
    {
        SetUserAction(new G4OnlyPrimaryGenerator(cfg, photons_per_event));
        SetUserAction(new G4OnlyEventAction(num_events));
        SetUserAction(new G4OnlyRunAction(accumulator));
    }
};
