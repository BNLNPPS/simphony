/**
generate_precooked_rng.cu
==========================

Generates precooked curand Philox sample arrays for U4Random aligned mode.
Each photon gets its own pre-evaluated random-number stream matching what
the GPU simulation would draw at the same step.

Relation to QCurandStateMonolithicTest
---------------------------------------
This tool is NOT a replacement for QCurandStateMonolithicTest — they
produce different artifacts for different consumers:

    QCurandStateMonolithicTest (qudarap/tests/)
        - Output: curand internal STATE structs (Philox4_32_10_t)
                  ~/.opticks/rngcache/RNG/QCurandState_<N>_0_0.bin
        - Consumer: GPU simulation initialisation. The kernel picks up a
                    state and starts drawing from it.
        - Purpose: pre-seeded RNG state for reproducible GPU runs.

    generate_precooked_rng (this tool)
        - Output: pre-evaluated random SAMPLES (float32 NumPy array)
                  ~/.opticks/precooked/QSimTest/rng_sequence/.../
                      rng_sequence_f_ni<NI>_nj<NJ>_nk<NK>_ioffset000000.npy
        - Consumer: U4Random in --aligned mode (CPU-side G4). G4 reads
                    sample i from the array on its i-th draw, byte-for-byte
                    matching whatever the GPU kernel would have drawn at
                    that step.
        - Purpose: photon-by-photon GPU<->G4 alignment.

States vs samples; GPU init vs G4 alignment. Both are needed for the
full validation workflow.

Build:
    nvcc -o generate_precooked_rng tools/generate_precooked_rng.cu \
         -I. -I/opt/eic-opticks/include/eic-opticks -lcurand -std=c++17

Usage:
    ./generate_precooked_rng [num_photons] [num_randoms_per_photon]
    Defaults: 100000 photons, 256 randoms each (nj=16, nk=16)

Output:
    ~/.opticks/precooked/QSimTest/rng_sequence/
        rng_sequence_f_ni<NI>_nj<NJ>_nk<NK>_tranche<NI>/
            rng_sequence_f_ni<NI>_nj<NJ>_nk<NK>_ioffset000000.npy
*/

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <sys/stat.h>

#include <curand_kernel.h>
#include "sysrap/NP.hh"

__global__ void generate_sequences(float* out, unsigned ni, unsigned nv, unsigned id_offset)
{
    unsigned idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx >= ni) return;

    unsigned photon_idx = id_offset + idx;

    // Match GPU simulation: curand_init(seed=0, subsequence=photon_idx, offset=0)
    curandStatePhilox4_32_10_t rng;
    curand_init(0ULL, (unsigned long long)photon_idx, 0ULL, &rng);

    float* row = out + idx * nv;
    for (unsigned j = 0; j < nv; j++)
        row[j] = curand_uniform(&rng);
}

static void mkdirp(const char* path)
{
    char tmp[1024];
    snprintf(tmp, sizeof(tmp), "%s", path);
    for (char* p = tmp + 1; *p; p++)
    {
        if (*p == '/') { *p = 0; mkdir(tmp, 0755); *p = '/'; }
    }
    mkdir(tmp, 0755);
}

int main(int argc, char** argv)
{
    unsigned ni = 100000;
    unsigned nj = 16;
    unsigned nk = 16;

    if (argc > 1) ni = atoi(argv[1]);
    if (argc > 2)
    {
        unsigned total = atoi(argv[2]);
        nj = 1; nk = total;
        for (unsigned f = 2; f * f <= total; f++)
        {
            if (total % f == 0 && f <= 64) { nj = f; nk = total / f; }
        }
    }

    unsigned nv = nj * nk;
    printf("Generating precooked curand Philox sequences:\n");
    printf("  photons: %u, randoms/photon: %u (nj=%u, nk=%u), memory: %.1f MB\n",
           ni, nv, nj, nk, (double)ni * nv * sizeof(float) / (1024 * 1024));

    const char* home = getenv("HOME");
    char dirpath[512], filename[256], fullpath[768];

    snprintf(dirpath, sizeof(dirpath),
        "%s/.opticks/precooked/QSimTest/rng_sequence/rng_sequence_f_ni%u_nj%u_nk%u_tranche%u",
        home, ni, nj, nk, ni);
    mkdirp(dirpath);

    snprintf(filename, sizeof(filename),
        "rng_sequence_f_ni%u_nj%u_nk%u_ioffset%06u.npy", ni, nj, nk, 0);
    snprintf(fullpath, sizeof(fullpath), "%s/%s", dirpath, filename);

    float* d_out = nullptr;
    cudaMalloc(&d_out, (size_t)ni * nv * sizeof(float));

    unsigned threads = 256;
    unsigned blocks = (ni + threads - 1) / threads;
    generate_sequences<<<blocks, threads>>>(d_out, ni, nv, 0);
    cudaDeviceSynchronize();

    cudaError_t err = cudaGetLastError();
    if (err != cudaSuccess) { fprintf(stderr, "CUDA error: %s\n", cudaGetErrorString(err)); return 1; }

    NP* seq = NP::Make<float>(ni, nj, nk);
    cudaMemcpy(seq->values<float>(), d_out, (size_t)ni * nv * sizeof(float), cudaMemcpyDeviceToHost);
    cudaFree(d_out);

    seq->save(fullpath);
    printf("Saved: %s\n", fullpath);
    printf("Set OPTICKS_RANDOM_SEQPATH=%s\n", fullpath);

    delete seq;
    return 0;
}
