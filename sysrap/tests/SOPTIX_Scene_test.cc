/**
SOPTIX_Scene_test.cc : writes NPY pixel array with raytraced render of triangulated geometry
=============================================================================================

::

    SOPTIX_Scene_test
    ~/o/sysrap/tests/SOPTIX_Scene_test.cc

For an encapsulated version of this see::

    ~/o/sysrap/tests/SOPTIX_Scene_Encapsulated_test.cc

An enhanced version with OpenGL interactive control see::

    SGLFW_SOPTIX_Scene_test
    ~/o/sysrap/tests/SGLFW_SOPTIX_Scene_test.cc

Other related tests::

    SCUDA_Mesh_test
    ~/o/sysrap/tests/SCUDA_Mesh_test.cc

**/

#include "NP.hh"
#include "scuda.h"
#include "spath.h"
#include "ssys.h"

#include "SGLM.h"
#include "SScene.h"

#include "SOPTIX_Context.h"
#include "SOPTIX_Desc.h"
#include "SOPTIX_MeshGroup.h"
#include "SOPTIX_Scene.h"
#include "SOPTIX_Module.h"
#include "SOPTIX_Pipeline.h"
#include "SOPTIX_SBT.h"
#include "SOPTIX_Params.h"

int main()
{
    bool dump = false;

    SScene* _scn = SScene::Load("$SCENE_FOLD");
    if (dump)
        std::cout << _scn->desc();

    int FRAME = ssys::getenvint("FRAME", -1);
    std::cout << "FRAME=" << FRAME << " SOPTIX_Scene_test run \n";
    sfr fr = _scn->getFrame(FRAME);

    SGLM gm;
    gm.set_frame(fr);
    std::cout << gm.desc();

    SOPTIX_Context ctx;
    if (dump)
        std::cout << ctx.desc();

    SOPTIX_Options opt;
    if (dump)
        std::cout << opt.desc();

    SOPTIX_Module mod(ctx.context, opt, "$SOPTIX_PTX");
    if (dump)
        std::cout << mod.desc();

    SOPTIX_Pipeline pip(ctx.context, mod.module, opt);
    if (dump)
        std::cout << pip.desc();

    SOPTIX_Scene scn(&ctx, _scn);
    if (dump)
        std::cout << scn.desc();

    SOPTIX_SBT sbt(pip, scn );
    if (dump)
        std::cout << sbt.desc();

    int                    HANDLE = ssys::getenvint("HANDLE", -1);
    OptixTraversableHandle handle = scn.getHandle(HANDLE) ;

    uchar4* d_pixels = nullptr ;
    size_t  num_pixel = gm.Width() * gm.Height();
    size_t  pixel_bytes = num_pixel * sizeof(uchar4);
    CUDA_CHECK(cudaMalloc(reinterpret_cast<void**>(&d_pixels), pixel_bytes));
    uchar4* pixels = new uchar4[num_pixel];

    SOPTIX_Params* d_param = SOPTIX_Params::DeviceAlloc();
    SOPTIX_Params  par;
    ;

    par.width = gm.Width();
    par.height = gm.Height();
    par.pixels = d_pixels;
    par.tmin = gm.get_near_abs();
    par.tmax = gm.get_far_abs();
    par.cameratype = gm.cam;
    par.visibilityMask = gm.vizmask;

    SGLM::Copy(&par.eye.x, gm.e);
    SGLM::Copy(&par.U.x, gm.u);
    SGLM::Copy(&par.V.x, gm.v);
    SGLM::Copy(&par.W.x, gm.w);

    par.handle = handle;

    par.upload(d_param);

    CUstream stream = 0;
    unsigned depth = 1;

    OPTIX_CHECK( optixLaunch(
                 pip.pipeline,
                 stream,
                 (CUdeviceptr)d_param,
                 sizeof( SOPTIX_Params ),
                 &(sbt.sbt),
                 gm.Width(),  // launch width
                 gm.Height(), // launch height
                 depth        // launch depth
                 ) );

    CUDA_SYNC_CHECK();
    CUDA_CHECK( cudaMemcpy( pixels, reinterpret_cast<void*>(d_pixels), pixel_bytes, cudaMemcpyDeviceToHost ));

    const char* npy_path = getenv("NPY_PATH");
    NP::Write(npy_path, (unsigned char*)pixels, gm.Height(), gm.Width(), 4);

    return 0;
}
