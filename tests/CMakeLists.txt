find_package(Python3 REQUIRED COMPONENTS Interpreter)
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
