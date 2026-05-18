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


foreach(SRC ${TEST_SOURCES})
    get_filename_component(TGT ${SRC} NAME_WE)
    add_executable(${TGT} ${SRC})
    target_link_libraries(${TGT} QUDARap CUDA::cudart)

    add_test(
       NAME ${name}.${TGT} 
       COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/QTestRunner.sh ${CMAKE_CURRENT_BINARY_DIR}/${TGT}
    )

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
