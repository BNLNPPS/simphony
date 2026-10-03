set(name CSGOptiX)

find_package(OptiX REQUIRED)

set(SOURCES)
set(OPTIX_PTX_SRC CSGOptiX.cu)
set(OPTIX_PTX_TARGET CSGOptiX_ptx)

option(SIMPHONY_RNG_REBUILD "Rebuild the Philox RNG state after each trace instead of carrying it live" OFF)

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
    CSGOptiXHelpers.h
    CSGOPTIX_API_EXPORT.hh
    CSGOPTIX_LOG.hh
)

add_library(${OPTIX_PTX_TARGET} OBJECT ${OPTIX_PTX_SRC})
set_target_properties(${OPTIX_PTX_TARGET} PROPERTIES CUDA_PTX_COMPILATION ON)
target_include_directories(${OPTIX_PTX_TARGET} PUBLIC
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/CSG>
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/sysrap>
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/third_party/optix-toolkit>
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/qudarap>)
target_compile_definitions(${OPTIX_PTX_TARGET}
    PUBLIC
        WITH_SIMULATE
        WITH_SIMTRACE
        WITH_RENDER
        OPTICKS_CSGOPTIX
        WITH_THRUST
        OPTICKS_CSG
        WITH_CONTIGUOUS
        WITH_S_BB
        $<$<CONFIG:Debug>:CONFIG_Debug>
        $<$<CONFIG:RelWithDebInfo>:CONFIG_RelWithDebInfo>
        $<$<CONFIG:Release>:CONFIG_Release>
        $<$<CONFIG:MinSizeRel>:CONFIG_MinSizeRel>
        OPTICKS_SYSRAP
        PLOG_LOCAL
        $<$<CONFIG:Debug>:DEBUG_TAG>
        $<$<CONFIG:Debug>:DEBUG_PIDX>
        $<$<CONFIG:Debug>:DEBUG_PIDXYZ>
        $<$<CONFIG:Release>:PRODUCTION>
        OPTICKS_OKCONF
        OPTICKS_QUDARAP
        NVCC
        $<$<BOOL:${SIMPHONY_RNG_REBUILD}>:SIMPHONY_RNG_REBUILD>)
target_link_libraries(${OPTIX_PTX_TARGET} PUBLIC OptiX::OptiX)

get_filename_component(ptx_stem "${OPTIX_PTX_SRC}" NAME_WE)
set(ptx_output "${SIMPHONY_BUILD_PTX_DIR}/${ptx_stem}.ptx")
add_custom_target(csgoptix_ptx
    COMMAND ${CMAKE_COMMAND} -E make_directory "${SIMPHONY_BUILD_PTX_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_OBJECTS:${OPTIX_PTX_TARGET}>" "${ptx_output}"
    BYPRODUCTS "${ptx_output}"
    VERBATIM
)
add_dependencies(csgoptix_ptx ${OPTIX_PTX_TARGET})

if(BUILD_TESTING)
    set(RNG_REBUILD_OPPOSITE_PTX_TARGET CSGOptiX_ptx_rng_rebuild_opposite)
    add_library(${RNG_REBUILD_OPPOSITE_PTX_TARGET} OBJECT ${OPTIX_PTX_SRC})
    set_target_properties(${RNG_REBUILD_OPPOSITE_PTX_TARGET}
        PROPERTIES CUDA_PTX_COMPILATION ON)

    get_target_property(RNG_REBUILD_PTX_INCLUDE_DIRS
        ${OPTIX_PTX_TARGET} INCLUDE_DIRECTORIES)
    get_target_property(RNG_REBUILD_PTX_COMPILE_DEFINITIONS
        ${OPTIX_PTX_TARGET} COMPILE_DEFINITIONS)
    list(FILTER RNG_REBUILD_PTX_COMPILE_DEFINITIONS
        EXCLUDE REGEX "SIMPHONY_RNG_REBUILD")

    target_include_directories(${RNG_REBUILD_OPPOSITE_PTX_TARGET} PUBLIC
        ${RNG_REBUILD_PTX_INCLUDE_DIRS})
    target_compile_definitions(${RNG_REBUILD_OPPOSITE_PTX_TARGET}
        PUBLIC ${RNG_REBUILD_PTX_COMPILE_DEFINITIONS}
            $<$<NOT:$<BOOL:${SIMPHONY_RNG_REBUILD}>>:SIMPHONY_RNG_REBUILD>)
    target_link_libraries(${RNG_REBUILD_OPPOSITE_PTX_TARGET} PUBLIC OptiX::OptiX)

    if(SIMPHONY_RNG_REBUILD)
        set(RNG_REBUILD_OFF_PTX_TARGET ${RNG_REBUILD_OPPOSITE_PTX_TARGET})
        set(RNG_REBUILD_ON_PTX_TARGET ${OPTIX_PTX_TARGET})
    else()
        set(RNG_REBUILD_OFF_PTX_TARGET ${OPTIX_PTX_TARGET})
        set(RNG_REBUILD_ON_PTX_TARGET ${RNG_REBUILD_OPPOSITE_PTX_TARGET})
    endif()

    set(SIMPHONY_RNG_REBUILD_PTX_OFF_DIR
        "${PROJECT_BINARY_DIR}/ptx_rng_rebuild/off" PARENT_SCOPE)
    set(SIMPHONY_RNG_REBUILD_PTX_ON_DIR
        "${PROJECT_BINARY_DIR}/ptx_rng_rebuild/on" PARENT_SCOPE)

    add_custom_target(csgoptix_rng_rebuild_ptx
        COMMAND ${CMAKE_COMMAND} -E make_directory
                "${PROJECT_BINARY_DIR}/ptx_rng_rebuild/off"
                "${PROJECT_BINARY_DIR}/ptx_rng_rebuild/on"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_OBJECTS:${RNG_REBUILD_OFF_PTX_TARGET}>"
                "${PROJECT_BINARY_DIR}/ptx_rng_rebuild/off/CSGOptiX.ptx"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_OBJECTS:${RNG_REBUILD_ON_PTX_TARGET}>"
                "${PROJECT_BINARY_DIR}/ptx_rng_rebuild/on/CSGOptiX.ptx"
        BYPRODUCTS
                "${PROJECT_BINARY_DIR}/ptx_rng_rebuild/off/CSGOptiX.ptx"
                "${PROJECT_BINARY_DIR}/ptx_rng_rebuild/on/CSGOptiX.ptx"
        VERBATIM
    )
    add_dependencies(csgoptix_rng_rebuild_ptx
        ${RNG_REBUILD_OFF_PTX_TARGET}
        ${RNG_REBUILD_ON_PTX_TARGET})
endif()

message(STATUS "Configured PTX build for ${OPTIX_PTX_SRC}")

add_library( ${name} SHARED ${SOURCES} ${HEADERS} )
add_dependencies(${name} csgoptix_ptx)

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
