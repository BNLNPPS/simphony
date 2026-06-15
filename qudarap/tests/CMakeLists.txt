set(name QUDARapTest)

find_package(CUDAToolkit REQUIRED)

set(TEST_SOURCES


   QRngTest.cc
   QScintTest.cc
   QCerenkovIntegralTest.cc
   QPolyTest.cc
   QSimDescTest.cc
   QPropTest.cc 
   QEvt_Lifecycle_Test.cc
   QSimCreateTest.cc 
   QUTest.cc
   QTexMakerTest.cc
   QTexLookupTest.cc
   qbnd_boundary_lookup_test.cc
   QMultiFilmTest.cc


   QCurandStateMonolithicTest.cc
   QCurandStateTest.cc

   qrng_test.cc
)


## THESE NEED REJIG : THEY FAIL FOR LACK OF DEPENDENCIES
set(KNOWN_FAIL_TEST_SOURCES
   QCerenkovTest.cc
   QBndTest.cc 
)

set(OTHER_TEST_SOURCES
   QCKTest.cc
)

set(QUDARAP_INPUT_PHOTON_NAMES
   CubeCorners
   CubeCorners10x10
   CubeCorners100
   CubeCorners100x100
   RandomSpherical10
   RandomSpherical100
   InwardsCubeCorners17699
   InwardsCubeCorners1
   RandomDisc10
   RandomDisc100
   UpXZ1000
   DownXZ1000
   RainXZ100
   RainXZ1000
   RainXZ100k
   RainXZ10k
   RainXZ_Z230_100
   RainXZ_Z230_1000
   RainXZ_Z230_100k
   RainXZ_Z230_10k
   RainXZ_Z230_1M
   RainXZ_Z195_100
   RainXZ_Z195_1000
   RainXZ_Z195_100k
   RainXZ_Z195_10k
   RainXZ_Z230_X700_100
   RainXZ_Z230_X700_1000
   RainXZ_Z230_X700_10k
   UniformDisc_R500_10k
   GridXY_X700_Z230_10k
   GridXY_X1000_Z1000_40k
)

if(BUILD_TESTING)
   find_package(Python3 REQUIRED COMPONENTS Interpreter)

   add_test(
      NAME Setup.SimphonyInputPhotons
      COMMAND ${CMAKE_COMMAND} -E env
              PYTHONPATH=${CMAKE_SOURCE_DIR}
              ${Python3_EXECUTABLE} -c "from optiphy.tools.generate_input_photons import main; main()"
              ${QUDARAP_INPUT_PHOTON_NAMES}
              --dtype float64
   )
   set_tests_properties(Setup.SimphonyInputPhotons PROPERTIES
      FIXTURES_SETUP SimphonyInputPhotons
   )
endif()


foreach(SRC ${TEST_SOURCES})
    get_filename_component(TGT ${SRC} NAME_WE)
    add_executable(${TGT} ${SRC})
    target_link_libraries(${TGT} QUDARap CUDA::cudart)

    if(TGT STREQUAL QEvt_Lifecycle_Test)
        foreach(INPUT_PHOTON_NAME ${QUDARAP_INPUT_PHOTON_NAMES})
            set(TEST_NAME ${name}.${TGT}.${INPUT_PHOTON_NAME})
            add_test(
               NAME ${TEST_NAME}
               COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/QTestRunner.sh ${CMAKE_CURRENT_BINARY_DIR}/${TGT} ${INPUT_PHOTON_NAME}_f8.npy
            )
            set_tests_properties(${TEST_NAME} PROPERTIES
                FIXTURES_REQUIRED SimphonyInputPhotons
            )
        endforeach()
    else()
        add_test(
           NAME ${name}.${TGT}
           COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/QTestRunner.sh ${CMAKE_CURRENT_BINARY_DIR}/${TGT}
        )
    endif()

endforeach()

foreach(SRC ${KNOWN_FAIL_TEST_SOURCES})
    get_filename_component(TGT ${SRC} NAME_WE)
    add_executable(${TGT} ${SRC})
    target_link_libraries(${TGT} QUDARap )
endforeach()


find_package(PMTSim_standalone CONFIG)

if(PMTSim_standalone_FOUND)

    set(PMTSIM_TEST_SOURCES
        QPMTTest.cc 
    )

    foreach(SRC ${PMTSIM_TEST_SOURCES})
        get_filename_component(TGT ${SRC} NAME_WE)
        add_executable(${TGT} ${SRC})

        set(testname ${name}.${TGT})
        add_test(${testname} ${TGT})

        target_link_libraries(${TGT} QUDARap )
        target_link_libraries( ${TGT} Opticks::PMTSim_standalone )
        target_compile_definitions( ${TGT} PRIVATE WITH_PMTSIM)
    endforeach()

else()
    message(STATUS "${name} PMTSim_standalone_FOUND:${PMTSim_standalone_FOUND}  " )
endif()
