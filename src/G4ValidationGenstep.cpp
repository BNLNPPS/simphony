#include <string>

#include <argparse/argparse.hpp>

#include "FTFP_BERT.hh"
#include "G4OpticalPhysics.hh"
#include "G4RunManager.hh"
#include "G4UImanager.hh"
#include "G4VModularPhysicsList.hh"

#include "G4ValidationGenstep.h"

using namespace std;

int main(int argc, char **argv)
{
    argparse::ArgumentParser program("G4ValidationGenstep", "0.0.0");

    string gdml_file;
    double energy_MeV = 1.0;
    int num_events = 1;

    program.add_argument("-g", "--gdml")
        .help("path to GDML file")
        .default_value(string("det.gdml"))
        .nargs(1)
        .store_into(gdml_file);

    program.add_argument("-e", "--energy")
        .help("electron kinetic energy in MeV")
        .default_value(1.0)
        .scan<'g', double>()
        .store_into(energy_MeV);

    program.add_argument("-n", "--nevents")
        .help("number of events")
        .default_value(1)
        .scan<'i', int>()
        .store_into(num_events);

    program.add_argument("-s", "--seed").help("random seed").scan<'i', long>();

    program.add_argument("--pos")
        .help("electron position x,y,z in mm (comma-separated)")
        .default_value(string("0,0,0"));

    program.add_argument("--dir").help("electron direction x,y,z (comma-separated)").default_value(string("0,0,1"));

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
        seed = program.get<long>("--seed");
    else
        seed = static_cast<long>(time(nullptr));

    // Parse position
    G4ThreeVector pos(0, 0, 0);
    {
        string s = program.get<string>("--pos");
        float x, y, z;
        if (sscanf(s.c_str(), "%f,%f,%f", &x, &y, &z) == 3)
            pos = G4ThreeVector(x, y, z);
    }

    // Parse direction
    G4ThreeVector dir(0, 0, 1);
    {
        string s = program.get<string>("--dir");
        float x, y, z;
        if (sscanf(s.c_str(), "%f,%f,%f", &x, &y, &z) == 3)
            dir = G4ThreeVector(x, y, z);
    }

    G4cout << "G4ValidationGenstep:" << G4endl;
    G4cout << "  GDML:     " << gdml_file << G4endl;
    G4cout << "  Energy:   " << energy_MeV << " MeV" << G4endl;
    G4cout << "  Events:   " << num_events << G4endl;
    G4cout << "  Position: (" << pos.x() << "," << pos.y() << "," << pos.z() << ") mm" << G4endl;
    G4cout << "  Direction: (" << dir.x() << "," << dir.y() << "," << dir.z() << ")" << G4endl;
    G4cout << "  Seed:     " << seed << G4endl;

    GenstepHitAccumulator accumulator;

    G4VModularPhysicsList *physics = new FTFP_BERT;
    physics->RegisterPhysics(new G4OpticalPhysics);

    G4RunManager run_mgr;
    run_mgr.SetUserInitialization(physics);
    run_mgr.SetUserInitialization(new GenstepDetectorConstruction(gdml_file, &accumulator));
    run_mgr.SetUserInitialization(new GenstepActionInitialization(&accumulator, pos, dir, energy_MeV, num_events));
    run_mgr.Initialize();

    CLHEP::HepRandom::setTheSeed(seed);

    G4cout << "G4Genstep: Starting " << num_events << " events..." << G4endl;
    run_mgr.BeamOn(num_events);

    return EXIT_SUCCESS;
}
