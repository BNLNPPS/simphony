#pragma once
/**
srng_traits.h : common RNG traits interface
===========================================

Shared CPU/GPU generation headers should accept an RNG object explicitly
instead of depending on an include-order-selected global RNG alias.

Concrete RNG headers specialize srng<T> and provide uniform accessors.
**/

#if defined(__CUDACC__) || defined(__CUDABE__)
#define SRNG_METHOD __device__
#else
#define SRNG_METHOD inline
#endif

template <typename T> struct srng;

template <typename Rng> SRNG_METHOD float srng_uniform(Rng &rng)
{
    return srng<Rng>::uniform(rng);
}

template <typename Rng> SRNG_METHOD double srng_uniform_double(Rng &rng)
{
    return srng<Rng>::uniform_double(rng);
}
