/**
 * viztracks — Visualize Opticks GPU photon trajectories in Geant4
 *
 * Runs the same Geant4 + Opticks GPU simulation as GPURaytrace, then
 * converts the photon step records into G4VTrajectory objects and
 * injects them into the G4Event for interactive visualization.
 *
 * Requires: OPTICKS_EVENT_MODE=DebugHeavy (or DebugLite) and
 *           OPTICKS_MAX_SLOT=M1 to gather record arrays from GPU.
 *
 * Usage:
 *   OPTICKS_EVENT_MODE=DebugHeavy OPTICKS_MAX_SLOT=M1 \
 *     viztracks -g geometry.gdml -m run.mac -s 42 -i
 */

#include <string>

#include <argparse/argparse.hpp>

#include "FTFP_BERT.hh"
#include "G4OpticalPhysics.hh"
#include "G4VModularPhysicsList.hh"

#include "G4UIExecutive.hh"
#include "G4UImanager.hh"
#include "G4VisExecutive.hh"

#include "sysrap/OPTICKS_LOG.hh"

#include "src/GPURaytrace.h"

#include "G4EventManager.hh"
#include "G4Run.hh"
#include "G4RunManager.hh"
#include "G4RunManagerFactory.hh"
#include "G4TrajectoryContainer.hh"
#include "G4VUserActionInitialization.hh"

#include "sysrap/NPFold.h"
#include "sysrap/SEventConfig.hh"
#include "sysrap/sphoton.h"

#include "OpticksTrajectory.h"

using namespace std;

struct VizRunAction : G4UserRunAction
{
    EventAction *fEventAction;

    VizRunAction(EventAction *eventAction) : fEventAction(eventAction) {}

    void BeginOfRunAction(const G4Run *) override {}

    void EndOfRunAction(const G4Run *run) override
    {
        if (!G4Threading::IsMasterThread())
            return;

        G4CXOpticks *gx = G4CXOpticks::Get();

        auto start = std::chrono::high_resolution_clock::now();
        gx->simulate(0, false);
        cudaDeviceSynchronize();
        auto end = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double> elapsed = end - start;
        cout << "Simulation time: " << elapsed.count() << " seconds" << endl;

        SEvt *sev = SEvt::Get_EGPU();
        unsigned int num_hits = sev->GetNumHit(0);

        cout << "Opticks: NumGensteps: " << sev->GetNumGenstepFromGenstep(0) << endl;
        cout << "Opticks: NumPhotons:  " << sev->GetNumPhotonCollected(0) << endl;
        cout << "Opticks: NumHits:     " << num_hits << endl;
        cout << "Geant4:  NumHits:     " << fEventAction->GetTotalG4Hits() << endl;

        // Build trajectories from the photon step record array
        const NP *record = sev->topfold->get("record");
        if (!record || record->shape.size() < 2)
        {
            cerr << "No record array — set OPTICKS_EVENT_MODE=DebugHeavy and OPTICKS_MAX_SLOT=M1" << endl;
            return;
        }

        int num_photons = record->shape[0];
        int max_steps = record->shape[1];
        const sphoton *steps = (const sphoton *)record->bytes();

        auto *trajContainer = new G4TrajectoryContainer();

        for (int j = 0; j < num_photons; j++)
        {
            const sphoton &first = steps[j * max_steps];
            auto *traj = new OpticksTrajectory(
                j, G4ThreeVector(first.mom.x, first.mom.y, first.mom.z));

            for (int i = 0; i < max_steps; i++)
            {
                const sphoton &s = steps[j * max_steps + i];
                if (s.flagmask == 0)
                    break;
                traj->AddPoint(G4ThreeVector(s.pos.x, s.pos.y, s.pos.z));
            }

            if (traj->GetPointEntries() >= 2)
                trajContainer->push_back(traj);
            else
                delete traj;
        }

        // Inject trajectories into the last kept event
        auto *eventVector = run->GetEventVector();
        G4Event *event =
            (eventVector && !eventVector->empty())
                ? const_cast<G4Event *>(eventVector->back())
                : nullptr;

        if (event)
        {
            event->SetTrajectoryContainer(trajContainer);
            cout << "Injected " << trajContainer->size()
                 << " Opticks photon trajectories into G4Event" << endl;
        }
        else
        {
            delete trajContainer;
            cerr << "No kept G4Event available for trajectory injection" << endl;
        }
    }
};

struct VizEventAction : public EventAction
{
    using EventAction::EventAction;

    void EndOfEventAction(const G4Event *event) override
    {
        EventAction::EndOfEventAction(event);
        // Keep the event alive so EndOfRunAction can inject trajectories
        G4EventManager::GetEventManager()->KeepTheCurrentEvent();
    }
};

struct VizActionInitialization : public G4VUserActionInitialization
{
    G4App *fG4App;
    VizEventAction *fVizEventAction;
    VizRunAction *fVizRunAction;

    VizActionInitialization(G4App *app)
        : G4VUserActionInitialization(), fG4App(app),
          fVizEventAction(new VizEventAction(app->sev)),
          fVizRunAction(new VizRunAction(fVizEventAction))
    {
    }

    void BuildForMaster() const override
    {
        SetUserAction(fVizRunAction);
    }

    void Build() const override
    {
        SetUserAction(fG4App->prim_gen_);
        SetUserAction(fVizRunAction);
        SetUserAction(fVizEventAction);
        SetUserAction(fG4App->tracking_);
        SetUserAction(fG4App->stepping_);
    }
};

int main(int argc, char **argv)
{
    OPTICKS_LOG(argc, argv);

    argparse::ArgumentParser program("viztracks", "0.0.0");

    string gdml_file, macro_name;
    bool interactive;

    program.add_argument("-g", "--gdml")
        .help("path to GDML file")
        .default_value(string("geom.gdml"))
        .nargs(1)
        .store_into(gdml_file);

    program.add_argument("-m", "--macro")
        .help("path to G4 macro")
        .default_value(string("run.mac"))
        .nargs(1)
        .store_into(macro_name);

    program.add_argument("-i", "--interactive")
        .help("open an interactive viewer (required to see trajectories)")
        .flag()
        .store_into(interactive);

    program.add_argument("-s", "--seed").help("fixed random seed (default: time-based)").scan<'i', long>();

    try
    {
        program.parse_args(argc, argv);
    }
    catch (const exception &err)
    {
        cerr << err.what() << endl;
        cerr << program;
        exit(EXIT_FAILURE);
    }

    long seed;
    if (program.is_used("--seed"))
    {
        seed = program.get<long>("--seed");
    }
    else
    {
        seed = static_cast<long>(time(nullptr));
    }
    CLHEP::HepRandom::setTheSeed(seed);
    G4cout << "Random seed set to: " << seed << G4endl;

    G4VModularPhysicsList *physics = new FTFP_BERT;
    physics->RegisterPhysics(new G4OpticalPhysics);

    auto *run_mgr = G4RunManagerFactory::CreateRunManager();
    run_mgr->SetUserInitialization(physics);

    G4App *g4app = new G4App(gdml_file);

    VizActionInitialization *actionInit = new VizActionInitialization(g4app);
    run_mgr->SetUserInitialization(actionInit);
    run_mgr->SetUserInitialization(g4app->det_cons_);

    G4UIExecutive *uix = nullptr;
    G4VisManager *vis = nullptr;

    if (interactive)
    {
        uix = new G4UIExecutive(argc, argv);
        vis = new G4VisExecutive;
        vis->Initialize();
    }

    G4UImanager *ui = G4UImanager::GetUIpointer();
    ui->ApplyCommand("/control/execute " + macro_name);

    if (interactive)
    {
        uix->SessionStart();
    }

    delete uix;

    return EXIT_SUCCESS;
}
