#pragma once
/**
G4ValidationGenstep.h
======================

Pure G4 simulation with electron primary that produces scintillation/Cerenkov
optical photons. G4 handles all physics including optical photon propagation.
Collects hits via sensitive detector. Used as the CPU reference for comparison
with GPU (simg4ox) genstep-based optical simulation.
**/

#include <filesystem>
#include <mutex>
#include <vector>

#include "G4Electron.hh"
#include "G4Event.hh"
#include "G4GDMLParser.hh"
#include "G4OpticalPhoton.hh"
#include "G4PhysicalConstants.hh"
#include "G4PrimaryParticle.hh"
#include "G4PrimaryVertex.hh"
#include "G4Run.hh"
#include "G4SDManager.hh"
#include "G4SystemOfUnits.hh"
#include "G4THitsCollection.hh"
#include "G4ThreeVector.hh"
#include "G4Track.hh"
#include "G4TrackStatus.hh"
#include "G4UserEventAction.hh"
#include "G4UserRunAction.hh"
#include "G4VHit.hh"
#include "G4VPhysicalVolume.hh"
#include "G4VUserActionInitialization.hh"
#include "G4VUserDetectorConstruction.hh"
#include "G4VUserPrimaryGeneratorAction.hh"

#include "sysrap/NP.hh"
#include "sysrap/sphoton.h"

// ---- Hit accumulator ----

struct GenstepHitAccumulator
{
    std::mutex           mtx;
    std::vector<sphoton> hits;
    int                  total_optical_photons = 0;
    int                  total_scintillation = 0;
    int                  total_cerenkov = 0;

    void AddHits(const std::vector<sphoton>& event_hits)
    {
        std::lock_guard<std::mutex> lock(mtx);
        hits.insert(hits.end(), event_hits.begin(), event_hits.end());
    }

    void Save(const char* filename)
    {
        std::lock_guard<std::mutex> lock(mtx);
        G4int                       num_hits = hits.size();
        NP*                         arr = NP::Make<float>(num_hits, 4, 4);
        for (int i = 0; i < num_hits; i++)
        {
            float* data = reinterpret_cast<float*>(&hits[i]);
            std::copy(data, data + 16, arr->values<float>() + i * 16);
        }
        arr->save(filename);
        delete arr;
        G4cout << "G4Genstep: Saved " << num_hits << " hits to " << filename << G4endl;
    }
};

// ---- Sensitive Detector ----

struct GenstepPhotonHit : public G4VHit
{
    GenstepPhotonHit() = default;

    GenstepPhotonHit(G4double energy, G4double time, G4ThreeVector position,
                     G4ThreeVector direction, G4ThreeVector polarization) :
        photon()
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

    void Print() override
    {
        G4cout << photon << G4endl;
    }
    sphoton photon;
};

using GenstepPhotonHitsCollection = G4THitsCollection<GenstepPhotonHit>;

struct GenstepPhotonSD : public G4VSensitiveDetector
{
    GenstepHitAccumulator* accumulator;

    GenstepPhotonSD(G4String name, GenstepHitAccumulator* acc) :
        G4VSensitiveDetector(name),
        accumulator(acc)
    {
        collectionName.insert(name + "_HC");
    }

    void Initialize(G4HCofThisEvent* hce) override
    {
        fHC = new GenstepPhotonHitsCollection(SensitiveDetectorName, collectionName[0]);
        if (fHCID < 0)
            fHCID = G4SDManager::GetSDMpointer()->GetCollectionID(collectionName[0]);
        hce->AddHitsCollection(fHCID, fHC);
    }

    G4bool ProcessHits(G4Step* aStep, G4TouchableHistory*) override
    {
        G4Track* track = aStep->GetTrack();
        if (track->GetDefinition() != G4OpticalPhoton::OpticalPhotonDefinition())
            return false;

        fHC->insert(new GenstepPhotonHit(
            track->GetTotalEnergy(),
            track->GetGlobalTime(),
            aStep->GetPostStepPoint()->GetPosition(),
            aStep->GetPostStepPoint()->GetMomentumDirection(),
            aStep->GetPostStepPoint()->GetPolarization()));

        track->SetTrackStatus(fStopAndKill);
        return true;
    }

    void EndOfEvent(G4HCofThisEvent*) override
    {
        G4int                n = fHC->entries();
        std::vector<sphoton> event_hits;
        event_hits.reserve(n);
        for (GenstepPhotonHit* hit : *fHC->GetVector())
            event_hits.push_back(hit->photon);
        accumulator->AddHits(event_hits);
    }

  private:
    GenstepPhotonHitsCollection* fHC = nullptr;
    G4int                        fHCID = -1;
};

// ---- Detector Construction ----

struct GenstepDetectorConstruction : G4VUserDetectorConstruction
{
    GenstepDetectorConstruction(std::filesystem::path gdml_file, GenstepHitAccumulator* acc) :
        gdml_file_(gdml_file),
        accumulator_(acc)
    {
    }

    G4VPhysicalVolume* Construct() override
    {
        parser_.Read(gdml_file_.string(), false);
        return parser_.GetWorldVolume();
    }

    void ConstructSDandField() override
    {
        G4SDManager*            SDman = G4SDManager::GetSDMpointer();
        const G4GDMLAuxMapType* auxmap = parser_.GetAuxMap();

        for (auto const& [logVol, listType] : *auxmap)
        {
            for (auto const& auxtype : listType)
            {
                if (auxtype.type == "SensDet")
                {
                    G4String name = logVol->GetName() + "_" + auxtype.value;
                    G4cout << "G4Genstep: Attaching SD to " << logVol->GetName() << G4endl;
                    GenstepPhotonSD* sd = new GenstepPhotonSD(name, accumulator_);
                    SDman->AddNewDetector(sd);
                    logVol->SetSensitiveDetector(sd);
                }
            }
        }
    }

  private:
    std::filesystem::path  gdml_file_;
    G4GDMLParser           parser_;
    GenstepHitAccumulator* accumulator_;
};

// ---- Electron Primary Generator ----

struct ElectronPrimaryGenerator : G4VUserPrimaryGeneratorAction
{
    G4ThreeVector position;
    G4ThreeVector direction;
    G4double      energy_MeV;

    ElectronPrimaryGenerator(G4ThreeVector pos, G4ThreeVector dir, G4double energy) :
        position(pos),
        direction(dir.unit()),
        energy_MeV(energy)
    {
    }

    void GeneratePrimaries(G4Event* event) override
    {
        G4PrimaryVertex*   vertex = new G4PrimaryVertex(position, 0.0);
        G4PrimaryParticle* particle = new G4PrimaryParticle(G4Electron::Definition());
        particle->SetKineticEnergy(energy_MeV * MeV);
        particle->SetMomentumDirection(direction);
        vertex->SetPrimary(particle);
        event->AddPrimaryVertex(vertex);
    }
};

// ---- Event Action with optical photon counting ----

struct GenstepEventAction : G4UserEventAction
{
    GenstepHitAccumulator* accumulator;
    int                    total_events;

    GenstepEventAction(GenstepHitAccumulator* acc, int total) :
        accumulator(acc),
        total_events(total)
    {
    }

    void EndOfEventAction(const G4Event* event) override
    {
        int id = event->GetEventID();
        if (id == 0 || (id + 1) % 10 == 0 || id + 1 == total_events)
            G4cout << "G4Genstep: Event " << id + 1 << "/" << total_events << G4endl;
    }
};

// ---- Run Action ----

struct GenstepRunAction : G4UserRunAction
{
    GenstepHitAccumulator* accumulator;

    GenstepRunAction(GenstepHitAccumulator* acc) :
        accumulator(acc)
    {
    }

    void EndOfRunAction(const G4Run*) override
    {
        G4cout << "G4Genstep: Total hits: " << accumulator->hits.size() << G4endl;
        accumulator->Save("g4_genstep_hits.npy");
    }
};

// ---- Action Initialization ----

struct GenstepActionInitialization : G4VUserActionInitialization
{
    GenstepHitAccumulator* accumulator;
    G4ThreeVector          position;
    G4ThreeVector          direction;
    G4double               energy_MeV;
    int                    num_events;

    GenstepActionInitialization(GenstepHitAccumulator* acc,
                                G4ThreeVector pos, G4ThreeVector dir,
                                G4double energy, int nevt) :
        accumulator(acc),
        position(pos),
        direction(dir),
        energy_MeV(energy),
        num_events(nevt)
    {
    }

    void BuildForMaster() const override
    {
        SetUserAction(new GenstepRunAction(accumulator));
    }

    void Build() const override
    {
        SetUserAction(new ElectronPrimaryGenerator(position, direction, energy_MeV));
        SetUserAction(new GenstepEventAction(accumulator, num_events));
        SetUserAction(new GenstepRunAction(accumulator));
    }
};
