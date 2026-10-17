#include <cstdlib>
#include <cstdio>

#include <cuda_runtime.h>

#include "CSGOptiXHelpers.h"

namespace
{
    void require(bool condition, const char* expression, int line)
    {
        if (condition) return;
        std::fprintf(stderr, "%s:%d requirement failed: %s\n", __FILE__, line, expression);
        std::exit(EXIT_FAILURE);
    }
}

#define REQUIRE(expression) require((expression), #expression, __LINE__)

struct CSGOptiXHelpersTestResult
{
    bool torch_carries_matline;
    bool frame_carries_matline;
    bool cerenkov_carries_matline;
    bool scintillation_carries_matline;
    bool modified_cerenkov_carries_matline;
    float entering_distance;
    float exiting_distance;
    float non_simulation_exit_distance;
};

__global__ void CSGOptiXHelpersTest_kernel(CSGOptiXHelpersTestResult* result)
{
    result->torch_carries_matline =
        CSGOptiX_GenstepCarriesMaterialLine(OpticksGenstep_TORCH);
    result->frame_carries_matline =
        CSGOptiX_GenstepCarriesMaterialLine(OpticksGenstep_FRAME);
    result->cerenkov_carries_matline =
        CSGOptiX_GenstepCarriesMaterialLine(OpticksGenstep_CERENKOV);
    result->scintillation_carries_matline =
        CSGOptiX_GenstepCarriesMaterialLine(OpticksGenstep_SCINTILLATION);
    result->modified_cerenkov_carries_matline =
        CSGOptiX_GenstepCarriesMaterialLine(OpticksGenstep_G4Cerenkov_modified);

    const float coincident_distance = 10.f;
    result->entering_distance = CSGOptiX_ReportedIntersectionDistance(
        coincident_distance,
        SRG_SIMULATE,
        -1.f);
    result->exiting_distance = CSGOptiX_ReportedIntersectionDistance(
        coincident_distance,
        SRG_SIMULATE,
        1.f);
    result->non_simulation_exit_distance = CSGOptiX_ReportedIntersectionDistance(
        coincident_distance,
        SRG_SIMTRACE,
        1.f);
}

int main()
{
    CSGOptiXHelpersTestResult* device_result = nullptr;
    REQUIRE(cudaMalloc(&device_result, sizeof(CSGOptiXHelpersTestResult)) == cudaSuccess);

    CSGOptiXHelpersTest_kernel<<<1, 1>>>(device_result);
    REQUIRE(cudaGetLastError() == cudaSuccess);
    REQUIRE(cudaDeviceSynchronize() == cudaSuccess);

    CSGOptiXHelpersTestResult result = {};
    REQUIRE(cudaMemcpy(
        &result,
        device_result,
        sizeof(CSGOptiXHelpersTestResult),
        cudaMemcpyDeviceToHost) == cudaSuccess);
    REQUIRE(cudaFree(device_result) == cudaSuccess);

    REQUIRE(!result.torch_carries_matline);
    REQUIRE(!result.frame_carries_matline);
    REQUIRE(result.cerenkov_carries_matline);
    REQUIRE(result.scintillation_carries_matline);
    REQUIRE(result.modified_cerenkov_carries_matline);

    REQUIRE(result.entering_distance == 10.f);
    REQUIRE(result.exiting_distance == nextafterf(10.f, INFINITY));
    REQUIRE(result.entering_distance < result.exiting_distance);
    REQUIRE(result.non_simulation_exit_distance == 10.f);

    std::puts("CSGOptiXHelpersTest: PASS");
    return 0;
}

#undef REQUIRE
