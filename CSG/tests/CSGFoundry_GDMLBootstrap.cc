#include <cstdlib>

#include "SLOG.hh"
#include "U4GDML.h"
#include "simphony.h"

namespace
{
struct CSGFoundry_GDMLBootstrap
{
    CSGFoundry_GDMLBootstrap()
    {
        const char* gdml = std::getenv("SIMPHONY_GEOM_FILE");
        if (gdml == nullptr || gdml[0] == '\0')
            return;

        setenv("CSGFoundry_GDMLBootstrap_ReturnExisting", "1", 0);

        const G4VPhysicalVolume*  world = U4GDML::Read(gdml);
        simphony::GeometryOptions options;
        options.gpu = simphony::GpuRequirement::Disabled;
        simphony::initialize(world, options);
    }
};

CSGFoundry_GDMLBootstrap boot;
} // namespace
