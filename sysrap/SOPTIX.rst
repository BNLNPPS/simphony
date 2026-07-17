SOPTIX.rst : triangulated machinery 
======================================

Overview
----------

The SOPTIX, SCUDA, and SMesh structs implement a triangulated geometry
workflow with the NVIDIA OptiX 7+ API.


Structs
---------

* SOPTIX.h : top level coordinator of triangulated raytrace render
* SOPTIX_Scene.h : holds vectors of SCUDA_MeshGroup SOPTIX_MeshGroup and OptixInstance 
* SOPTIX_SBT.h : create sbt from pipeline and scene by uploading the prog and hitgroup records

* SOPTIX_Context.h : OptixDeviceContext + SOPTIX_Properties  
* SOPTIX_Properties.h : optixDeviceContextGetProperty results

* SOPTIX_Module.h : Create OptixModule from PTX loaded from file
* SOPTIX_Pipeline.h : Create OptixPipeline from OptixModule
* SOPTIX_Options.h : module and pipeline compile/link options
* SOPTIX_OPT.h : enum strings
* SOPTIX.cu : RG, CH progs for simple triangle renderer
* SOPTIX_Params.h : render control 

* SOPTIX_Accel.h : builds acceleration structure GAS or IAS from the buildInputs
* SOPTIX_Binding.h : CPU/GPU SBT records
* SOPTIX_getPRD.h : unpackPointer from optixPayload

* SOPTIX_BuildInput_Mesh.h : create OptixBuildInput via "part" indexing into SCUDA_MeshGroup (Used from SOPTIX_MeshGroup)
* SOPTIX_MeshGroup.h : create SOPTIX_BuildInput_Mesh for each part of SCUDA_MeshGroup, use those to form SOPTIX_Accel gas  

* SCUDA_Mesh.h : uploads SMesh tri and holds SCU_Buf 
* SCUDA_MeshGroup.h : collect vectors of NP from each SMeshGroup sub, upload together with SCU_BufferView 

* SMeshGroup.h : collection of SMesh subs and names
* SMesh.h : holds tri,vtx,nrm NP either from original G4VSolid conversion or concatenation


tests
-------

SOPTIX_Scene_test.{sh,cc} SOPTIX_Scene_Encapsulated_test.cc
    ppm non-interactive raytrace render of triangulated geometry
    both non-encapsulated and encapsulated versions can be built
    and run with the script::
   
         SOPTIX_Scene_test

SOPTIX_Options_test.{sh,cc}
    TODO: CHECK


SOPTIX_SBT::initHitgroup
---------------------------

HMM: with analytic geometry have "boundary" that 
comes from the CSGNode. To do that with triangles 
need to plant the boundary indices into HitgroupData.  
That means need hitgroup records for each sub-SMesh 
(thats equivalent to each CSGPrim)

Need nested loop like CSGOptiX/SBT.cc SBT::createHitgroup::
 
     GAS 
        BuildInput       (actually 1:1 with GAS) 
           sub-SMesh 

So need access to scene data to form the SBT 
