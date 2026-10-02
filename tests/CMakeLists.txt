find_package(Python3 REQUIRED COMPONENTS Interpreter)
find_package(Geant4 REQUIRED OPTIONAL_COMPONENTS multithreaded)
find_program(BASH_EXECUTABLE bash REQUIRED)

set(SIMPHONY_SIMG4OX_TEST_WORKDIR "${CMAKE_CURRENT_BINARY_DIR}/simg4ox")

function(add_simg4ox_integration_test TEST_CASE)
    set(TEST_NAME "Integration.simg4ox.${TEST_CASE}")
    set(TEST_WORKDIR "${SIMPHONY_SIMG4OX_TEST_WORKDIR}/${TEST_CASE}")
    file(MAKE_DIRECTORY "${TEST_WORKDIR}")

    add_test(
        NAME "${TEST_NAME}"
        COMMAND ${CMAKE_COMMAND} -E env
                REPO_DIR=${PROJECT_SOURCE_DIR}
                PYTHON=${Python3_EXECUTABLE}
                SIMG4OX_BIN=$<TARGET_FILE:simg4ox>
                ${BASH_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_simg4ox.sh "${TEST_CASE}"
    )

    set_tests_properties("${TEST_NAME}" PROPERTIES
        WORKING_DIRECTORY "${TEST_WORKDIR}"
        LABELS "integration"
        TIMEOUT 120
    )
endfunction()

add_simg4ox_integration_test(raindrop)
add_simg4ox_integration_test(dune_mock_wls_detector_box)
add_simg4ox_integration_test(8x8SiPM_w_CSI_optial_grease)
add_simg4ox_integration_test(opticks_two_spheres)
add_simg4ox_integration_test(drich_direct_sensor)
add_simg4ox_integration_test(drich_aerogel)
add_simg4ox_integration_test(drich_mirror)
add_simg4ox_integration_test(sibling_pair)

set(SIMPHONY_SIMG4OX_MULTIEVENT_TEST_WORKDIR "${CMAKE_CURRENT_BINARY_DIR}/simg4ox_multievent")
file(MAKE_DIRECTORY "${SIMPHONY_SIMG4OX_MULTIEVENT_TEST_WORKDIR}")

add_test(
    NAME Integration.simg4ox_multievent
    COMMAND ${CMAKE_COMMAND} -E env
            REPO_DIR=${PROJECT_SOURCE_DIR}
            PYTHON=${Python3_EXECUTABLE}
            SIMG4OX_BIN=$<TARGET_FILE:simg4ox>
            ${BASH_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_simg4ox_multievent.sh
)

set_tests_properties(Integration.simg4ox_multievent PROPERTIES
    WORKING_DIRECTORY "${SIMPHONY_SIMG4OX_MULTIEVENT_TEST_WORKDIR}"
    LABELS "integration"
    TIMEOUT 120
)

add_dependencies(simg4ox csgoptix_rng_rebuild_ptx)

set(SIMPHONY_RNG_REBUILD_TEST_WORKDIR
    "${CMAKE_CURRENT_BINARY_DIR}/simg4ox_rng_rebuild")

function(add_simg4ox_rng_rebuild_run MODE PTX_DIR)
    set(TEST_NAME "Integration.simg4ox_rng_rebuild.${MODE}")
    set(TEST_WORKDIR "${SIMPHONY_RNG_REBUILD_TEST_WORKDIR}/${MODE}")
    file(MAKE_DIRECTORY "${TEST_WORKDIR}")

    add_test(
        NAME "${TEST_NAME}"
        COMMAND ${CMAKE_COMMAND} -E env
                REPO_DIR=${PROJECT_SOURCE_DIR}
                PYTHON=${Python3_EXECUTABLE}
                SIMG4OX_BIN=$<TARGET_FILE:simg4ox>
                SIMPHONY_PTX_DIR=${PTX_DIR}
                SIMG4OX_SEED=42
                ${BASH_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_simg4ox_multievent.sh
    )

    set_tests_properties("${TEST_NAME}" PROPERTIES
        WORKING_DIRECTORY "${TEST_WORKDIR}"
        LABELS "integration;rng-rebuild"
        TIMEOUT 120
    )
endfunction()

add_simg4ox_rng_rebuild_run(off "${SIMPHONY_RNG_REBUILD_PTX_OFF_DIR}")
add_simg4ox_rng_rebuild_run(on "${SIMPHONY_RNG_REBUILD_PTX_ON_DIR}")

set(SIMPHONY_RNG_REBUILD_OFF_WORKDIR "${SIMPHONY_RNG_REBUILD_TEST_WORKDIR}/off")
set(SIMPHONY_RNG_REBUILD_ON_WORKDIR "${SIMPHONY_RNG_REBUILD_TEST_WORKDIR}/on")
set(SIMPHONY_RNG_REBUILD_RUN_TESTS
    "Integration.simg4ox_rng_rebuild.off;Integration.simg4ox_rng_rebuild.on")

add_test(
    NAME Integration.simg4ox_rng_rebuild.ptx_differ
    COMMAND ${CMAKE_COMMAND} -E compare_files
            "${SIMPHONY_RNG_REBUILD_PTX_OFF_DIR}/CSGOptiX.ptx"
            "${SIMPHONY_RNG_REBUILD_PTX_ON_DIR}/CSGOptiX.ptx"
)
set_tests_properties(Integration.simg4ox_rng_rebuild.ptx_differ PROPERTIES
    DEPENDS "${SIMPHONY_RNG_REBUILD_RUN_TESTS}"
    LABELS "integration;rng-rebuild"
    WILL_FAIL TRUE
)

add_test(
    NAME Integration.simg4ox_rng_rebuild.counts_match
    COMMAND ${CMAKE_COMMAND} -E compare_files
            "${SIMPHONY_RNG_REBUILD_OFF_WORKDIR}/simg4ox-multievent-summary.txt"
            "${SIMPHONY_RNG_REBUILD_ON_WORKDIR}/simg4ox-multievent-summary.txt"
)
set_tests_properties(Integration.simg4ox_rng_rebuild.counts_match PROPERTIES
    DEPENDS "${SIMPHONY_RNG_REBUILD_RUN_TESTS}"
    LABELS "integration;rng-rebuild"
)

add_test(
    NAME Integration.simg4ox_rng_rebuild.hits_match
    COMMAND ${CMAKE_COMMAND} -E compare_files
            "${SIMPHONY_RNG_REBUILD_OFF_WORKDIR}/s_hits.npy"
            "${SIMPHONY_RNG_REBUILD_ON_WORKDIR}/s_hits.npy"
)
set_tests_properties(Integration.simg4ox_rng_rebuild.hits_match PROPERTIES
    DEPENDS "${SIMPHONY_RNG_REBUILD_RUN_TESTS}"
    LABELS "integration;rng-rebuild"
)

if(Geant4_multithreaded_FOUND)
    set(SIMPHONY_SIMG4OX_MULTITHREAD_TEST_WORKDIR "${CMAKE_CURRENT_BINARY_DIR}/simg4ox_multithread")
    file(MAKE_DIRECTORY "${SIMPHONY_SIMG4OX_MULTITHREAD_TEST_WORKDIR}")

    add_test(
        NAME Integration.simg4ox_multithread
        COMMAND ${CMAKE_COMMAND} -E env
                REPO_DIR=${PROJECT_SOURCE_DIR}
                PYTHON=${Python3_EXECUTABLE}
                SIMG4OX_BIN=$<TARGET_FILE:simg4ox>
                THREADS=2
                ${BASH_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_simg4ox_multithread.sh
    )

    set_tests_properties(Integration.simg4ox_multithread PROPERTIES
        WORKING_DIRECTORY "${SIMPHONY_SIMG4OX_MULTITHREAD_TEST_WORKDIR}"
        LABELS "integration"
        PROCESSORS 2
        TIMEOUT 120
    )
endif()
