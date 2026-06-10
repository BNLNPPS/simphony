set(name CSGTest)

find_package(CUDAToolkit REQUIRED)

set(TEST_SOURCES
    CSGNodeTest.cc
    CSGNodeImpTest.cc
    CSGIntersectSolidTest.cc
    CSGPrimImpTest.cc
    CSGPrimSpecTest.cc
    CSGPrimTest.cc
    CSGSolidTest.cc
    CSGFoundryTest.cc

    CSGFoundry_addPrimNodes_Test.cc
    CSGFoundry_getCenterExtent_Test.cc
    CSGFoundry_findSolidIdx_Test.cc

    CSGFoundry_CreateFromSimTest.cc
    CSGFoundry_IntersectPrimTest.cc

    CSGNameTest.cc
    CSGTargetTest.cc
    CSGTargetGlobalTest.cc
    CSGFoundry_MakeCenterExtentGensteps_Test.cc
    CSGFoundry_getFrame_Test.cc
    CSGFoundry_getFrameE_Test.cc 
    CSGFoundry_getMeshName_Test.cc
    CSGFoundry_SGeo_SEvt_Test.cc

    CSGFoundry_ResolveCFBase_Test.cc
    CSGFoundryLoadTest.cc 

    CSGScanTest.cc
    CUTest.cc
    CSGLogTest.cc
    CSGMakerTest.cc
    CSGQueryTest.cc

    CSGSimtraceTest.cc
    CSGSimtraceRerunTest.cc
    CSGSimtraceSampleTest.cc

    CSGCopyTest.cc

    intersect_leaf_phicut_test.cc
    intersect_leaf_phi_wedge_test.cc
    intersect_leaf_thetacut_test.cc
    intersect_leaf_box3_test.cc 
    intersect_leaf_cylinder_test.cc 
    intersect_leaf_cylinder_vs_oldcylinder_test.cc
    CSGIntersectComparisonTest.cc 

    distance_leaf_slab_test.cc

    CSGNodeScanTest.cc
    CSGSignedDistanceFieldTest.cc

    CSGGeometryTest.cc
    CSGClassifyTest.cc
)

set( DEFERRED_TEST_SOURCES
    CSGGeometryFromGeocacheTest.cc
)

foreach(SRC ${TEST_SOURCES})
    get_filename_component(TGT ${SRC} NAME_WE)
    add_executable(${TGT} ${SRC})
    target_link_libraries(${TGT} CSG CUDA::cudart)

    add_test(
       NAME ${name}.${TGT} 
       COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/CSGTestRunner.sh ${CMAKE_CURRENT_BINARY_DIR}/${TGT}
    )
    set_tests_properties(${name}.${TGT} PROPERTIES DEPENDS "G4CXTest.G4CXOpticks_setGeometry_Test")
endforeach()

set(TGT CSGDemoTest)
add_executable(${TGT} CSGDemoTest.cc DemoGeo.cc DemoGrid.cc)
target_link_libraries(${TGT} CSG CUDA::cudart)
