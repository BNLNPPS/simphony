#include <algorithm>
#include <filesystem>
#include <iostream>
#include <string>

#include "G4GDMLParser.hh"
#include "G4VPhysicalVolume.hh"
#include "sysrap/OPTICKS_LOG.hh"

#include <argparse/argparse.hpp>

#include "simphony.h"

using namespace std;

int main(int argc, char **argv)
{
    OPTICKS_LOG(argc, argv);

    argparse::ArgumentParser program("consgeo", "0.0.0");

    string gdml_file;
    string out_prefix;

    program.add_argument("-g", "--gdml")
        .help("path to GDML file")
        .default_value(string("geom.gdml"))
        .nargs(1)
        .store_into(gdml_file);

    program.add_argument("-o", "--out-prefix")
        .help("where to save CSG")
        .default_value(string("csg"))
        .nargs(1)
        .store_into(out_prefix);

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

    LOG_INFO << "gdml_file: " << gdml_file << endl;

    try
    {
        G4GDMLParser parser;
        parser.Read(gdml_file, false);
        const G4VPhysicalVolume* world = parser.GetWorldVolume();
        if (world == nullptr)
            throw runtime_error("failed to create a Geant4 world from " + gdml_file);

        simphony::GeometryOptions options;
        options.gpu = simphony::GpuRequirement::Disabled;
        simphony::initialize(world, options);

        const filesystem::path outpath =
            filesystem::path(out_prefix) / filesystem::path(gdml_file).stem();
        simphony::save_geometry(outpath);

        LOG_INFO << "Created G4 volume " << world->GetName() << " from " << gdml_file << endl;
        LOG_INFO << "Saved CSG tree to " << outpath << endl;
    }
    catch (const exception& error)
    {
        LOG_ERROR << error.what() << endl;
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
