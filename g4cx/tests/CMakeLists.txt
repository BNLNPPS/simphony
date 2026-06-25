set(name G4CXTest)

find_package(CUDAToolkit 12.6 REQUIRED)

#[=[
Promote to ctests by moving between the lists 
Issues preventing them being ctests are:

1. need for environment setup 
2. dependency on geometry specifics

So generally the less config the easier to become tests. 
#]=]


set(SINGLE_SOURCES

   G4CXSimtraceTest.cc

   G4CXSimulateTest.cc
   G4CXTest.cc 

   G4CX_U4TreeCreateCSGFoundryTest.cc 

   G4CXOpticks_SetGeometry_GetInputPhoton_Test.cc
)

set(TEST_SOURCES
   G4CXRenderTest.cc
   G4CXOpticks_setGeometry_Test.cc
)

set(GEOM raindrop)
set(SIMPHONY_GEOM_FILE ${CMAKE_SOURCE_DIR}/tests/geom/${GEOM}.gdml)

foreach(SRC ${SINGLE_SOURCES})
    get_filename_component(TGT ${SRC} NAME_WE)
    add_executable(${TGT} ${SRC})
    target_link_libraries(${TGT} G4CX CUDA::cudart)
endforeach()


foreach(SRC ${TEST_SOURCES})
    get_filename_component(TGT ${SRC} NAME_WE)
    add_executable(${TGT} ${SRC})
    target_link_libraries(${TGT} G4CX CUDA::cudart)

    set(TEST_ARGS)
    if("${TGT}" STREQUAL "G4CXOpticks_setGeometry_Test")
       set(TEST_ARGS ${SIMPHONY_GEOM_FILE})
    endif()

    add_test(
       NAME ${name}.${TGT} 
       COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/GXTestRunner.sh ${CMAKE_CURRENT_BINARY_DIR}/${TGT} ${TEST_ARGS}
    )
    set_tests_properties(${name}.${TGT} PROPERTIES
       ENVIRONMENT "SIMPHONY_GEOM_FILE=${SIMPHONY_GEOM_FILE}"
    )
endforeach()

set_tests_properties(${name}.G4CXRenderTest PROPERTIES DISABLED TRUE)
