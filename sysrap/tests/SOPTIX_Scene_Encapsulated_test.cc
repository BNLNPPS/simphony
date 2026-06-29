/**
SOPTIX_Scene_Encapsulated_test.cc 
====================================

::
 
    SOPTIX_Scene_test
    ~/o/sysrap/tests/SOPTIX_Scene_test.cc

For an enchancement of this with OpenGL interactive control see::

    SGLFW_SOPTIX_Scene_test
    ~/o/sysrap/tests/SGLFW_SOPTIX_Scene_test.cc  

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
    SScene* scene = SScene::Load("$SCENE_FOLD") ; 
    sfr fr = scene->getFrame(ssys::getenvint("FRAME", -1))  ; 

    SGLM gm ; 
    gm.set_frame(fr);   
    std::cout << gm.desc() ; 

    SOPTIX opx(scene, gm) ; 
    opx.render_ppm("$PPM_PATH"); 

    return 0 ; 
}
