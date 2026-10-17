#include <cassert>
#include <cstdlib>

#include <cuda_runtime.h>

#include "CSGOptiX.h"
#include "G4CXOpticks.hh"
#include "OPTICKS_LOG.hh"
#include "SEventConfig.hh"
#include "SFrameConfig.hh"
#include "SGLM.h"

int main(int argc, char** argv)
{
    OPTICKS_LOG(argc, argv);

    if (argc != 2)
        return EXIT_FAILURE;

    int device_count = 0;
    if (cudaGetDeviceCount(&device_count) != cudaSuccess || device_count == 0)
        return 77;

    SGLM::SetWH(64, 48);
    SFrameConfig::SetFrameMask("pixel");
    SEventConfig::SetRGModeRender();
    SEventConfig::Initialize();

    G4CXOpticks* gx = G4CXOpticks::SetGeometry(argv[1]);
    assert(gx);
    assert(gx->cx);

    gx->cx->setFrame("-1");
    const unsigned char* pixels = gx->cx->renderFrame();
    assert(pixels);
    assert(gx->cx->getRenderWidth() == 64);
    assert(gx->cx->getRenderHeight() == 48);

    return EXIT_SUCCESS;
}
