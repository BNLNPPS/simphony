#include "torch.h"

#include <cuda_runtime.h>
#include <curand_kernel.h>

#include <cstddef>

#include "CUDA_CHECK.h"
#include "srng.h"

namespace
{

__global__ void generate_photons_kernel(storch torch, sphoton* photons, unsigned int num_photons, unsigned int seed)
{
    unsigned int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx >= num_photons)
    {
        return;
    }

    curandStatePhilox4_32_10 rng;
    curand_init(seed, idx, 0, &rng);

    qtorch  qt{.t = torch};

    sphoton photon;
    storch::generate(photon, rng, qt.q, idx, 0);
    photons[idx] = photon;
}

/**
Small RAII guard for the temporary device photon array used by
generate_photons_gpu. The wrapper is not needed for the successful path, but
keeps cleanup correct when CUDA_CHECK or CUDA_SYNC_CHECK throws after
cudaMalloc. Without a guard, failures in the kernel launch, synchronization, or
device-to-host copy would skip cudaFree and leak the device allocation.
**/
struct device_photon_buffer
{
    explicit device_photon_buffer(size_t count)
    {
        CUDA_CHECK(cudaMalloc(reinterpret_cast<void**>(&ptr), count * sizeof(sphoton)));
    }

    ~device_photon_buffer()
    {
        CUDA_CHECK_NOEXCEPT(cudaFree(ptr));
    }

    device_photon_buffer(const device_photon_buffer&) = delete;
    device_photon_buffer& operator=(const device_photon_buffer&) = delete;

    sphoton* ptr = nullptr;
};

} // namespace

std::vector<sphoton> generate_photons_gpu(const storch& torch, unsigned int num_photons, unsigned int seed)
{
    if (num_photons == 0)
    {
        num_photons = torch.numphoton;
    }

    std::vector<sphoton> photons(num_photons);
    if (num_photons == 0)
    {
        return photons;
    }

    device_photon_buffer d_photons(photons.size());

    constexpr unsigned int threads_per_block = 256;
    unsigned int blocks = (num_photons + threads_per_block - 1) / threads_per_block;

    generate_photons_kernel<<<blocks, threads_per_block>>>(torch, d_photons.ptr, num_photons, seed);
    CUDA_SYNC_CHECK();

    CUDA_CHECK(cudaMemcpy(photons.data(), d_photons.ptr, photons.size() * sizeof(sphoton), cudaMemcpyDeviceToHost));

    return photons;
}
