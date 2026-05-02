#pragma once

#if defined(__CUDACC__) || defined(__CUDABE__)
   #define SCURAND_METHOD __device__
   #include "curand_kernel.h"
#else
#define SCURAND_METHOD
#endif

#include "srng_traits.h"

template <typename T>
struct scurand
{
    template <typename Rng> static SCURAND_METHOD T uniform(Rng *rng);
};

template <> template <typename Rng> inline SCURAND_METHOD float scurand<float>::uniform(Rng *rng)
{ 
#ifdef FLIP_RANDOM
    return 1.f - srng_uniform(*rng);
#else
    return srng_uniform(*rng);
#endif
}

template <> template <typename Rng> inline SCURAND_METHOD double scurand<double>::uniform(Rng *rng)
{ 
#ifdef FLIP_RANDOM
    return 1. - srng_uniform_double(*rng);
#else
    return srng_uniform_double(*rng);
#endif
}
