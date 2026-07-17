/**
SOPTIX_Scene_Encapsulated_test.cc
====================================

::

    SOPTIX_Scene_test
    ~/o/sysrap/tests/SOPTIX_Scene_test.cc

Other related tests::

    SCUDA_Mesh_test
    ~/o/sysrap/tests/SCUDA_Mesh_test.cc

**/

#include "ssys.h"
#include "SGLM.h"
#include "SScene.h"
#include "SOPTIX.h"

int main()
{
    SScene* scene = SScene::Load("$SCENE_FOLD");
    sframe  fr = scene->getFrame(ssys::getenvint("FRAME", -1));

    SGLM gm;
    gm.set_frame(fr);
    std::cout << gm.desc();

    SOPTIX opx(scene, gm);
    opx.render_npy("$NPY_PATH");

    return 0;
}
