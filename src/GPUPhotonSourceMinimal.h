#include <filesystem>
#include <fstream>
#include <vector>

#include "G4Event.hh"
#include "G4GDMLParser.hh"
#include "G4OpticalPhoton.hh"
#include "G4PhysicalConstants.hh"
#include "G4PrimaryParticle.hh"
#include "G4PrimaryVertex.hh"
#include "G4SystemOfUnits.hh"
#include "G4ThreeVector.hh"
#include "G4UserEventAction.hh"
#include "G4UserRunAction.hh"
#include "G4VPhysicalVolume.hh"
#include "G4VUserDetectorConstruction.hh"
#include "G4VUserPrimaryGeneratorAction.hh"

#include "g4cx/G4CXOpticks.hh"
#include "sysrap/NP.hh"
#include "sysrap/SEvt.hh"
#include "sysrap/sphoton.h"

#include "config.h"
#include "torch.h"

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

        return world;
    }

  private:
    std::filesystem::path gdml_file_;
    G4GDMLParser parser_;
};

struct PrimaryGenerator : G4VUserPrimaryGeneratorAction
{
    simphony::Config cfg;
    SEvt *sev;

    PrimaryGenerator(const simphony::Config& cfg, SEvt* sev) :
        cfg(cfg),
        sev(sev)
    {
    }

    void GeneratePrimaries(G4Event *event) override
    {
        std::vector<sphoton> sphotons = generate_photons(cfg.torch);

        size_t num_floats = sphotons.size() * 4 * 4;
        float *data = reinterpret_cast<float *>(sphotons.data());
        NP *photons = NP::MakeFromValues<float>(data, num_floats);

        photons->reshape({static_cast<int64_t>(sphotons.size()), 4, 4});

        for (const sphoton &p : sphotons)
        {
            G4ThreeVector position_mm(p.pos.x, p.pos.y, p.pos.z);
            G4double time_ns = p.time;
            G4ThreeVector direction(p.mom.x, p.mom.y, p.mom.z);
            G4double wavelength_nm = p.wavelength;
            G4ThreeVector polarization(p.pol.x, p.pol.y, p.pol.z);

            G4PrimaryVertex *vertex = new G4PrimaryVertex(position_mm, time_ns);
            G4double kineticEnergy = h_Planck * c_light / (wavelength_nm * nm);

            G4PrimaryParticle *particle = new G4PrimaryParticle(G4OpticalPhoton::Definition());
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
    SEvt *sev;
    unsigned int fTotalOpticksHits{0};

    EventAction(SEvt *sev) : sev(sev)
    {
    }

    void BeginOfEventAction(const G4Event *event) override
    {
        sev->beginOfEvent(event->GetEventID());
    }

    void EndOfEventAction(const G4Event *event) override
    {
        int eventID = event->GetEventID();
        sev->addEventConfigArray();
        sev->gather();
        sev->endOfEvent(eventID);

        // GPU-based simulation
        G4CXOpticks *gx = G4CXOpticks::Get();

        gx->simulate(eventID, false);
        cudaDeviceSynchronize();

        SEvt *sev_gpu = SEvt::Get_EGPU();
        unsigned int num_hits = sev_gpu->GetNumHit(0);

        std::cout << "Opticks: NumHits:  " << num_hits << std::endl;
        fTotalOpticksHits += num_hits;

        std::ofstream outFile("opticks_hits_output.txt");
        if (!outFile.is_open())
        {
            std::cerr << "Error opening output file!" << std::endl;
        }
        else
        {
            for (int idx = 0; idx < int(num_hits); idx++)
            {
                sphoton hit;
                sev_gpu->getHit(hit, idx);
                G4ThreeVector position = G4ThreeVector(hit.pos.x, hit.pos.y, hit.pos.z);
                G4ThreeVector direction = G4ThreeVector(hit.mom.x, hit.mom.y, hit.mom.z);
                G4ThreeVector polarization = G4ThreeVector(hit.pol.x, hit.pol.y, hit.pol.z);
                outFile << hit.time << " " << hit.wavelength << "  " << "(" << position.x() << ", " << position.y()
                        << ", " << position.z() << ")  " << "(" << direction.x() << ", " << direction.y() << ", "
                        << direction.z() << ")  " << "(" << polarization.x() << ", " << polarization.y() << ", "
                        << polarization.z() << ")" << std::endl;
            }
            outFile.close();
        }

        gx->reset(eventID);
    }

    unsigned int GetTotalOpticksHits() const
    {
        return fTotalOpticksHits;
    }
};

struct RunAction : G4UserRunAction
{
    EventAction *fEventAction;

    RunAction(EventAction *eventAction) : fEventAction(eventAction)
    {
    }

    void BeginOfRunAction(const G4Run *) override
    {
    }

    void EndOfRunAction(const G4Run *) override
    {
        std::cout << "Opticks: NumHits:  " << fEventAction->GetTotalOpticksHits() << std::endl;
    }
};

struct G4App
{
    G4App(const simphony::Config& cfg, std::filesystem::path gdml_file) :
        sev(SEvt::CreateOrReuse_ECPU()),
        det_cons_(new DetectorConstruction(gdml_file)),
        prim_gen_(new PrimaryGenerator(cfg, sev)),
        event_act_(new EventAction(sev)),
        run_act_(new RunAction(event_act_))
    {
    }

    SEvt *sev;

    G4VUserDetectorConstruction *det_cons_;
    G4VUserPrimaryGeneratorAction *prim_gen_;
    EventAction *event_act_;
    RunAction *run_act_;
};
