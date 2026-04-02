set(name CSG)

set(SOURCES)
set(HEADERS)

set(CSG_HEADERS)
set(INTERSECT_HEADERS)

list(APPEND SOURCES
    CSGNode.cc
    CU.cc
    CSGPrim.cc
    CSGSolid.cc
    CSGFoundry.cc
    CSGCopy.cc
    CSGMaker.cc
    CSGImport.cc
    CSGTarget.cc

    CSGScan.cc
    CSGScan.cu

    CSGView.cc
    CSGGrid.cc
    CSGQuery.cc
    CSGGeometry.cc
    CSGDraw.cc
    CSGRecord.cc
    CSGSimtraceRerun.cc
    CSGSimtraceSample.cc
    CSGSimtrace.cc

    CSGDebug_Cylinder.cc

    CSG_LOG.cc
)
    
list(APPEND CSG_HEADERS
    CSGNode.h 

    CU.h
    CSGPrim.h
    CSGSolid.h

    CSGParams.h 

    CSGFoundry.h

    CSGCopy.h
    CSGMaker.h
    CSGImport.h
    CSGTarget.h
    CSGScan.h
    CSGView.h
    CSGGrid.h
    CSGQuery.h
    CSGGeometry.h
    CSGDraw.h
    CSGRecord.h

    CSGEnum.h

    CSGSimtraceRerun.h
    CSGSimtrace.hh
    CSGDebug_Cylinder.hh

    CSG_LOG.hh 
    CSG_API_EXPORT.hh 
)

list(APPEND INTERSECT_HEADERS
    csg_intersect_leaf.h 
    csg_intersect_node.h 
    csg_intersect_tree.h 

    csg_intersect_leaf_box3.h
    csg_intersect_leaf_convexpolyhedron.h
    csg_intersect_leaf_cylinder.h
    csg_intersect_leaf_disc.h
    csg_intersect_leaf_head.h
    csg_intersect_leaf_hyperboloid.h
    csg_intersect_leaf_infcylinder.h
    csg_intersect_leaf_newcone.h
    csg_intersect_leaf_oldcone.h
    csg_intersect_leaf_oldcylinder.h
    csg_intersect_leaf_phicut.h
    csg_intersect_leaf_plane.h
    csg_intersect_leaf_slab.h
    csg_intersect_leaf_sphere.h
    csg_intersect_leaf_templated.h
    csg_intersect_leaf_thetacut.h
    csg_intersect_leaf_zsphere.h

    csg_robust_quadratic_roots.h
    csg_classify.h
    csg_pack.h
    csg_stack.h
    csg_tranche.h
    csg_error.h
    csg_postorder.h

    f4_stack.h
)

list(APPEND HEADERS
    ${CSG_HEADERS}
    ${INTERSECT_HEADERS}
)

add_library( ${name} ${SOURCES} )

target_compile_definitions( ${name} PUBLIC OPTICKS_CSG )
target_compile_definitions( ${name} PUBLIC WITH_CONTIGUOUS )
target_compile_definitions( ${name} PUBLIC WITH_S_BB )
target_compile_definitions( ${name} PRIVATE GLM_ENABLE_EXPERIMENTAL )

target_include_directories(${name} PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/${PROJECT_NAME}>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/${PROJECT_NAME}/CSG>
)

target_link_libraries(${name} SysRap)

install(FILES ${HEADERS} DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/${PROJECT_NAME}/${name})

install(TARGETS ${name}
        EXPORT ${PROJECT_NAME}Targets
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)
