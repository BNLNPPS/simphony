set(name SysRap)
set(desc "System Level Utilities depending")

find_package(nlohmann_json REQUIRED)
find_package(plog REQUIRED)
find_package(GLEW REQUIRED)
find_package(glfw3 REQUIRED)
find_package(glm REQUIRED)
find_package(CUDAToolkit REQUIRED)

configure_file(config_path.h.in config_path.h @ONLY)

set(SOURCES
    SYSRAP_LOG.cc 
    SLOG.cc 
    config.cpp
    torch.cpp
    torch.cu

    SSys.cc
    SProf.cc
    SMeta.cc

    SPath.cc
    SSeq.cc
    STranche.cc
    SAr.cc
    SArr.cc
    SArgs.cc
    SPPM.cc
    SVec.cc
    SNameVec.cc
    SMap.cc
    SCount.cc
    SDigest.cc
    SDigestNP.cc
    SDirect.cc
    SArrayDigest.cc

    SLauncher.cc
    SRenderer.cc
    SCtrl.cc
    SGeo.cc
    SConstant.cc

    STimes.cc
    SLogger.cc
    SSortKV.cc
    SPairVec.cc
    ../third_party/solarmd5/md5.c
    SPPM.cc
    SColor.cc
    SId.cc
    SGDML.cc
    S_get_option.cc
    STime.cc
    SPack.cc
    SBit.cc
    SEnabled.cc
    SRand.cc

    CheckGeo.cc

    SLabelCache.cc

    SMath.cc

    snd.cc
    scsg.cc

    s_csg.cc
    sn.cc
    s_pa.cc
    s_bb.cc
    s_tv.cc


    SEvent.cc
    SFrameGenstep.cc

    SOpticks.cc
    SOpticksResource.cc
    SOpticksKey.cc
    SThetaCut.cc 
    SPhiCut.cc 

    SCenterExtentGenstep.cc

    OpticksPhoton.cc

    SU.cu 
    SPM.cu

    SEventConfig.cc
    SFrameConfig.cc
    SGeoConfig.cc

    SEvt.cc
    SSim.cc

    SFastSim_Debug.cc
    SFastSimOpticalModel.cc
) 


set(HEADERS
    OPTICKS_LOG.hh
    config.h
    torch.h

    SYSRAP_LOG.hh
    SYSRAP_API_EXPORT.hh
    SLOG.hh
    slog.h 

    PLOG.hh
    SLOG_INIT.hh  

    SLaunchSequence.h 


    sproc.h
    sprof.h
    SProf.hh
    SMeta.hh
    smeta.h

    SBacktrace.h
    SStackFrame.h
    SProfile.h 

    ssys.h
    SSys.hh

    sstr.h

    schrono.h
    stimer.h
    sstamp.h
    ssystime.h

    SBnd.h
    SPrd.h

    SPath.hh
    spath.h 
    sdirectory.h

    sdevice.h
    scontext.h

    SSeq.hh
    STranche.hh

    SVec.hh
    svec.h

    SNameVec.hh
    SMap.hh
    SCount.hh

    slist.h
    sfreq.h 
    snode.h
    stree.h
    s_unique.h
    suniquename.h

    sstandard.h
    sprop.h 
    sproplist.h 
    sidxname.h 

    snam.h 
    smatsur.h 


    strid.h 
    sfactor.h

    snd.hh
    scsg.hh

    sndtree.h

    s_pool.h
    s_csg.h
    sn.h 
    s_pa.h 
    s_bb.h 
    s_tv.h 

    st.h 
    stra.h 
    sgeomtools.h
 
    spa.h
    sxf.h
    sbb.h
    
    sdigest.h
    SDigest.hh

    

    SDigestNP.hh
    SDirect.hh
    sdirect.h
    SArrayDigest.hh

    SLauncher.hh
    SRenderer.hh
    SRandom.h
    s_seq.h
    S4Random.h

    SCF.h

    SCtrl.hh
    SGeo.hh
    SConstant.hh
    SEvt.hh

    STimes.hh
    SLogger.hh
    SSortKV.hh
    SPairVec.hh
    OpticksCSG.h
    PlainFormatter.hh
    SAr.hh
    SArr.hh
    SArgs.hh
    S_get_option.hh

    STime.hh
    s_time.h

    SPPM.hh
    sppm.h 

    SColor.hh
    SId.hh
    SGDML.hh
    SPack.hh
    SBit.hh
    SBitSet.h
    SEnabled.hh
    SRand.hh

    CheckGeo.hh
    SLabelCache.hh

    SRng.hh

    SName.h 
    SLabel.h 

    smath.h 
    sblackbody.h
    tcomplex.h

    srng.h
    srng_traits.h
    srngcpu.h
    scurand.h  

    s_mock_texture.h
    stexture.h  

    njuffa_erfcinvf.h 
    s_mock_erfcinvf.h


    squad.h
    squadx.h

    sphoton.h
    sphit.h 
    spho.h
    sgs.h 
    srec.h 
    srec.h 
    sseq.h 
    sseq_index.h

    sframe.h
    SCE.h 

    SPMT.h 
    SPMTAccessor.h 

    storchtype.h

    storch.h
    scarrier.h
    scerenkov.h
    sscint.h
    sevent.h
    sstate.h 
    sctx.h
    salloc.h

    SGenerate.h
    
    sflow.h
    sqat4.h
    saabb.h
    stran.h
    stmm.h 

    SPlace.h
    SPlaceSphere.h
    SPlaceRing.h
    SPlaceCylinder.h
    SPlaceCircle.h

    ssincos.h
    sxyz.h
    sc4u.h
    stag.h 

    sbit_.h
    sbibit.h

    sview.h
    strided_range.h 
    iexpand.h 


    SOpticks.hh
    sdomain.h

    SDBG.h

    SOpticksResource.hh

    SCMD.h

    SRecord.h
    SGen.h

    SGLM.h
    SGLM_Parse.h
    SGLM_Modifiers.h
    SGLM_Arcball.h

    GL_CHECK.h

    SScene.h 
    SMesh.h
    SMeshGroup.h

    SGLFW.h
    SGLFW_Keys.h
    SGLFW_check.h
    SGLFW_Buffer.h
    SGLFW_VAO.h
    SGLFW_Attrib.h
    SGLFW_Program.h
    SGLFW_Mesh.h
    SGLFW_Scene.h
    SGLFW_Record.h
    SGLFW_Gen.h
    SGLFW_Evt.h
    SGLFW_GLEQ.h

    SGLDisplay.h
    SCUDA_OutputBuffer.h 
    SGLFW_CUDA.h

    SCUDA_Mesh.h 
    SCU_BufferView.h 
    SCUDA_MeshGroup.h 
    SCSGPrimSpec.h

    scuda.h
    scuda_pointer.h

    SOPTIX.h
    SOPTIX_Context.h
    SOPTIX_Properties.h
    SOPTIX_Options.h
    SOPTIX_getPRD.h
    SOPTIX_Desc.h
    SOPTIX_Accel.h
    SOPTIX_Binding.h
    SOPTIX_MeshGroup.h
    SOPTIX_Module.h
    SOPTIX_OPT.h
    SOPTIX_Pipeline.h
    SOPTIX_SBT.h
    SOPTIX_Scene.h
    
    SOPTIX_BuildInput.h
    SOPTIX_BuildInput_Mesh.h
    SOPTIX_BuildInput_CPA.h
    SOPTIX_BuildInput_IA.h
    
    gleq.h

    SDir.h
    SRG.h
    SCAM.h
    SBAS.h

    SEvt.hh 

    SOpticksKey.hh
    SMath.hh

    SEvent.hh
    SGenstep.h
    sslice.h 

    SFrameGenstep.hh

    SThetaCut.hh
    SPhiCut.hh


    SCenterExtentFrame.h
    SPresent.h

    SCenterExtentGenstep.hh 
    scanvas.h

    OpticksGenstep.h
    OpticksPhoton.h
    OpticksPhoton.hh

    CUDA_CHECK.h
    SCU.h 
    SCU_.h 
    OPTIX_CHECK.h
 

    SU.hh
    SComp.h 

    SEventConfig.hh
    SRM.h 
    SFrameConfig.hh
    SGeoConfig.hh

    np.h
    NP.hh
    NP_nanobind.h

    NPU.hh
    NPX.h
    NPFold.h
    SSim.hh
    SPropMockup.h

    S4MaterialPropertyVector.h

    SNameOrder.h
    S4.h 

    S4RandomMonitor.h
    S4RandomArray.h


    SFastSim_Debug.hh
    SFastSimOpticalModel.hh
    STrackInfo.h
    SPhoton_Debug.h
    SUniformRand.h

    sgeomdefs.h 
    sfmt.h
    ssolid.h
    SSimtrace.h
)

set(WITH_NP YES)


add_library( ${name} ${SOURCES} )

# The project enables CXX/CUDA but not C, so compile the vendored C source
# as C++ so the OpenSSL-compatible MD5 API is available from SysRap.
set_source_files_properties(../third_party/solarmd5/md5.c PROPERTIES LANGUAGE CXX)

set_property( TARGET ${name} PROPERTY INTERFACE_DESCRIPTION ${desc} )

target_link_libraries(${name}
    plog::plog
    nlohmann_json::nlohmann_json
    CUDA::cudart
    GLEW::GLEW
    glfw
    glm::glm
)

target_include_directories( ${name} PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../third_party>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../third_party/optix-toolkit>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/${PROJECT_NAME}>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/${PROJECT_NAME}/sysrap>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/${PROJECT_NAME}/third_party>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/${PROJECT_NAME}/third_party/optix-toolkit>
)


#[=[
SysRap Compile Definitions
----------------------------

former  WITH..SND
   switched the below to use the old inflexible snd.hh CSG node impl, 
   instead of the default more flexible sn.h impl:

   sysrap/stree.h
       WITH..SND saves CSG nodes in CSGFoundry/SSim/stree/csg 
       NOT:WITH..SND saves CSG nodes in CSGFoundry/SSim/stree/_csg 

   u4/U4Tree.h u4/U4Solid.h u4/U4Polycone.h 
       WITH..SND uses snd.hh idx int as CSG root
       NOT:WITH..SND uses sn.h sn* pointer as CSG root 
    
DEBUG_TAG 
   needed for random aligned running in multiple pkgs: sysrap, qudarap, u4  
   however making this PUBLIC makes rebuilding real heavy 
   so must rely on making coordinated switches when doing random aligned running  

PLOG_LOCAL
   changes visibility of plog external symbols, allowing better
   integration with packages (like junosw) that do not hide 
   symbols by default  

#]=]


target_compile_definitions( ${name}
    PUBLIC
      $<$<CONFIG:Debug>:CONFIG_Debug>
      $<$<CONFIG:RelWithDebInfo>:CONFIG_RelWithDebInfo>
      $<$<CONFIG:Release>:CONFIG_Release>
      $<$<CONFIG:MinSizeRel>:CONFIG_MinSizeRel>

      OPTICKS_SYSRAP 
      GLM_ENABLE_EXPERIMENTAL
      PLOG_LOCAL

      $<$<CONFIG:Debug>:DEBUG_TAG>
      $<$<CONFIG:Debug>:DEBUG_PIDX>
      $<$<CONFIG:Debug>:DEBUG_PIDXYZ>
      $<$<CONFIG:Release>:PRODUCTION>
)

install(FILES ${HEADERS} DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/${PROJECT_NAME}/sysrap/)
install(
    FILES
        ../third_party/optix-toolkit/README.md
        ../third_party/optix-toolkit/LICENSE.txt
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/${PROJECT_NAME}/third_party/optix-toolkit
)
install(
    FILES
        ../third_party/optix-toolkit/OptiXToolkit/ShaderUtil/vec_math.h
        ../third_party/optix-toolkit/OptiXToolkit/ShaderUtil/Preprocessor.h
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/${PROJECT_NAME}/third_party/optix-toolkit/OptiXToolkit/ShaderUtil
)

install(TARGETS ${name}
        EXPORT ${PROJECT_NAME}Targets
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)
