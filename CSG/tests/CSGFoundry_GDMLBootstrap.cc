#include <cstdlib>

#include "G4CXOpticks.hh"

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

        G4CXOpticks::SetNoGPU(true);
        G4CXOpticks::SetGeometry(gdml);
    }
};

CSGFoundry_GDMLBootstrap boot;
} // namespace
