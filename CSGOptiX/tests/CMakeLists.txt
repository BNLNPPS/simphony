set(name CSGOptiXTest)

find_package(CUDAToolkit REQUIRED)

# The static core (Windows) embeds cudart_static via CMake's default runtime
# for static CUDA targets; also linking shared cudart collides at the exe
# link (LNK2005). Match the core's runtime flavor.
if(WIN32 AND NOT BUILD_SHARED_LIBS)
    set(CUDART CUDA::cudart_static)
else()
    set(CUDART CUDA::cudart)
endif()

#[=[
Promote sources to ctests by moving to lower list
generally the problem is required environment
or doing geometry specific things.
#]=]

set(SINGLE_SOURCES

    CSGOptiXDescTest.cc
    CSGOptiXSimtraceTest.cc
    CSGOptiXSimulateTest.cc
    CSGOptiXSimTest.cc
    CXRaindropTest.cc

    CSGOptiXServiceTest.cc

    CSGOptiXRMTest.cc
    CSGOptiXTMTest.cc
    CSGOptiXSMTest.cc

)

set(VIZ_SOURCES
    CSGOptiXRenderInteractiveTest.cc
)


set(TEST_SOURCES
    CSGOptiXVersion.cc
    CSGOptiXVersionTest.cc
    CSGOptiXRenderTest.cc
    ParamsTest.cc
)

foreach(SRC ${SINGLE_SOURCES})
    get_filename_component(TGT ${SRC} NAME_WE)
    add_executable(${TGT} ${SRC})
    target_link_libraries(${TGT} CSGOptiX ${CUDART})
endforeach()


if(SIMPHONY_WITH_VIZ)
find_package(GLEW)
find_package(glfw3)
find_package(OpenGL)

foreach(SRC ${VIZ_SOURCES})
    get_filename_component(TGT ${SRC} NAME_WE)
    add_executable(${TGT} ${SRC})
    target_link_libraries(${TGT}
          CSGOptiX
          OpenGL::GL
          GLEW::GLEW
          glfw
          ${CUDART}
         )
endforeach()
endif()

foreach(SRC ${TEST_SOURCES})
    get_filename_component(TGT ${SRC} NAME_WE)
    add_executable(${TGT} ${SRC})
    target_link_libraries(${TGT} CSGOptiX ${CUDART})

    add_test(
       NAME ${name}.${TGT} 
       COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/CXTestRunner.sh ${CMAKE_CURRENT_BINARY_DIR}/${TGT}
    )
endforeach()

set_tests_properties(${name}.CSGOptiXRenderTest PROPERTIES DISABLED TRUE)
