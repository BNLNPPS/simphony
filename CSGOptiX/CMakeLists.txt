set(name CSGOptiX)

find_package(OptiX 7 REQUIRED)

set(SOURCES)
set(OPTIX_PTX_SRC CSGOptiX7.cu)
set(OPTIX_PTX_TARGET CSGOptiX7_ptx)


list(APPEND SOURCES
    Params.cc 
    CSGOptiX.cc
    Frame.cc 
    CSGOPTIX_LOG.cc

    Ctx.cc
    Properties.cc
    PIP.cc
    SBT.cc
)

list(APPEND HEADERS
    CSGOptiX.h
    CSGOptiXService.h
    CSGOPTIX_API_EXPORT.hh
    CSGOPTIX_LOG.hh
)

add_library(${OPTIX_PTX_TARGET} OBJECT ${OPTIX_PTX_SRC})
set_target_properties(${OPTIX_PTX_TARGET} PROPERTIES CUDA_PTX_COMPILATION ON)
target_include_directories(${OPTIX_PTX_TARGET} PUBLIC
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/CSG>
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/sysrap>
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/qudarap>)
target_compile_definitions(${OPTIX_PTX_TARGET} PUBLIC WITH_PRD WITH_SIMULATE WITH_SIMTRACE WITH_RENDER
        OPTICKS_CSGOPTIX WITH_THRUST OPTICKS_CSG WITH_CONTIGUOUS WITH_S_BB CONFIG_Debug OPTICKS_SYSRAP
        PLOG_LOCAL RNG_PHILOX DEBUG_TAG DEBUG_PIDX DEBUG_PIDXYZ OPTICKS_OKCONF OPTICKS_QUDARAP NVCC)
target_link_libraries(${OPTIX_PTX_TARGET} PUBLIC OptiX::OptiX)

get_filename_component(ptx_stem "${OPTIX_PTX_SRC}" NAME_WE)
set(ptx_output "${GPHOX_BUILD_PTX_DIR}/${ptx_stem}.ptx")
add_custom_target(csgoptix_ptx
    COMMAND ${CMAKE_COMMAND} -E make_directory "${GPHOX_BUILD_PTX_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_OBJECTS:${OPTIX_PTX_TARGET}>" "${ptx_output}"
    BYPRODUCTS "${ptx_output}"
    VERBATIM
)
add_dependencies(csgoptix_ptx ${OPTIX_PTX_TARGET})

message(STATUS "Configured PTX build for ${OPTIX_PTX_SRC}")

add_library( ${name} SHARED ${SOURCES} ${HEADERS} )
add_dependencies(${name} csgoptix_ptx)

target_compile_definitions( ${name} PRIVATE WITH_PRD )  # using Pointer trick means can reduce attrib and payload to 2 
target_compile_definitions( ${name} PUBLIC WITH_SIMULATE )
target_compile_definitions( ${name} PUBLIC WITH_SIMTRACE )
target_compile_definitions( ${name} PUBLIC WITH_RENDER )
target_compile_definitions( ${name} PUBLIC OPTICKS_CSGOPTIX )

#[=[

WITH_THRUST compile definition
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

* WITH_THRUST is needed for the GPU complex math functions used by qpmt.h

* The CPU side does not require WITH_THRUST, so keep the definition PRIVATE
  to avoid leaking it into dependents

#]=]

target_compile_definitions( ${name} PRIVATE WITH_THRUST )

target_include_directories( ${name} PUBLIC
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/CSGOptiX>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/${PROJECT_NAME}>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/${PROJECT_NAME}/CSGOptiX>
)

target_link_libraries(${name} PUBLIC OptiX::OptiX CSG QUDARap SysRap)

install(FILES $<TARGET_OBJECTS:${OPTIX_PTX_TARGET}> DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(FILES ${HEADERS} DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/${PROJECT_NAME}/${name})

install(TARGETS ${name}
        EXPORT ${PROJECT_NAME}Targets
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)
