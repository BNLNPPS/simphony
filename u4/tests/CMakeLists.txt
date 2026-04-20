set(name U4Test)

find_package(CUDAToolkit REQUIRED)

#[=[
Unlike most executables U4VPrimaryGeneratorTest 
needs the MOCK_CURAND macro in order to use CUDA 
curand using code on the CPU by mocking the curand
functions

HMM: its kinda confusing to not build an executable 
depending on switches because when flipping the switch OFF
the old executable still runs, but it is not updated and 
so it ignores any code changes made.

It is better to always build the executable and have it 
fail with an informative message when any needed switches 
are not defined.  

#]=]

set(TGT U4VPrimaryGeneratorTest)
add_executable(${TGT} U4VPrimaryGeneratorTest.cc)
target_compile_definitions( ${TGT} PRIVATE MOCK_CURAND ) 
target_link_libraries(${TGT} U4)

set(TEST_SOURCES
   U4Custom4Test.cc
   U4NistManagerTest.cc
   U4MaterialTest.cc
   U4MaterialPropertyVectorTest.cc
   U4TrackInfoTest.cc 
   U4TrackTest.cc
   U4GDMLTest.cc
   U4GDMLReadTest.cc 
   U4PhysicalConstantsTest.cc
   U4RandomTest.cc
   U4UniformRandTest.cc
   U4EngineTest.cc
   U4RandomMonitorTest.cc
   U4RandomArrayTest.cc


   U4VolumeMakerTest.cc
   U4LogTest.cc
   U4RotationMatrixTest.cc
   U4TransformTest.cc
   U4TraverseTest.cc


   U4Material_MakePropertyFold_MakeTest.cc
   U4Material_MakePropertyFold_LoadTest.cc
   U4TouchableTest.cc
   U4SurfaceTest.cc
   U4SolidTest.cc
   U4SolidMakerTest.cc
   U4SolidMakerTest2.cc

   U4SensitiveDetectorTest.cc 

   U4Debug_Test.cc 
   U4Hit_Debug_Test.cc 

   G4ThreeVectorTest.cc 


   U4PhysicsTableTest.cc 
)

set(EXPECTED_TO_FAIL_SOURCES
   U4NavigatorTest.cc
)

set(SLOW_SOURCES
   U4TreeTest.cc
   U4TreeCreateTest.cc
   U4TreeCreateSSimTest.cc
   U4TreeCreateSSimLoadTest.cc
)

foreach(SRC ${TEST_SOURCES})
    get_filename_component(TGT ${SRC} NAME_WE)
    add_executable(${TGT} ${SRC})
    target_link_libraries(${TGT} U4)

    add_test(
       NAME ${name}.${TGT} 
       COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/U4TestRunner.sh ${CMAKE_CURRENT_BINARY_DIR}/${TGT}
    )
endforeach()

foreach(SRC ${EXPECTED_TO_FAIL_SOURCES})
    get_filename_component(TGT ${SRC} NAME_WE)
    add_executable(${TGT} ${SRC})
    target_link_libraries(${TGT} U4)
endforeach()

foreach(SRC ${SLOW_SOURCES})
    get_filename_component(TGT ${SRC} NAME_WE)
    add_executable(${TGT} ${SRC})
    target_link_libraries(${TGT} U4)
endforeach()

#[=[
Note that the U4AppTest executable also 
needs the MOCK_CURAND macro in order to use CUDA 
curand using code on the CPU by mocking the curand
functions
#]=]

set(U4AppTest_SOURCES
   U4AppTest.cc
)

set(TGT U4AppTest)
add_executable(${TGT} ${U4AppTest_SOURCES})
target_compile_definitions( ${TGT} PRIVATE MOCK_CURAND ) 
target_link_libraries(${TGT} U4)


set(U4HitTest_SOURCES
    U4HitTest.cc
)

set(TGT U4HitTest)
add_executable(${TGT} ${U4HitTest_SOURCES})
target_link_libraries(${TGT} U4 CSG CUDA::cudart)

message(STATUS "${CMAKE_CURRENT_LIST_FILE} : PMTSim_standalone_FOUND     : ${PMTSim_standalone_FOUND}")
message(STATUS "${CMAKE_CURRENT_LIST_FILE} : Custom4_FOUND    : ${Custom4_FOUND}")


set(PMTSim_TESTS
   U4SimulateTest.cc
   U4SimtraceTest.cc
   U4PMTFastSimTest.cc
   U4PMTAccessorTest.cc
)

if(PMTSim_standalone_FOUND)

    foreach(SRC ${PMTSim_TESTS})
        get_filename_component(TGT ${SRC} NAME_WE)
        add_executable(${TGT} ${SRC})
        target_compile_definitions( ${TGT} PRIVATE MOCK_CURAND ) 
        target_compile_definitions( ${TGT} PRIVATE WITH_NP )    ## for _PMTSimParamData 
        target_compile_definitions( ${TGT} PRIVATE POM_DEBUG )   

        target_link_libraries(${TGT} U4 SysRap)

           target_compile_definitions( ${TGT} PRIVATE WITH_PMTSIM )   
           target_compile_definitions( ${TGT} PRIVATE PMTSIM_STANDALONE )   
           target_link_libraries(${TGT} PMTSim_standalone)

    endforeach()

endif()
