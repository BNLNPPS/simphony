#pragma once
/**
srng.h : picks curandState implementation 
===========================================

This is included into qudarap/qrng.h 

Template specializations collecting details of the various curandState impls.  See::

    ~/o/sysrap/tests/srng_test.sh 


https://stackoverflow.com/questions/8789867/c-template-class-specialization-why-do-common-methods-need-to-be-re-implement

Each specialisation of a class template gives a different class - they do not share any members.
So have to implement all methods in each specialization, or use a separate helper. 

**/

#include "srng_traits.h"
#include <curand_kernel.h>

using XORWOW = curandStateXORWOW ;
using Philox = curandStatePhilox4_32_10 ; 

#if defined(RNG_XORWOW)
using DefaultDeviceRNG = XORWOW;
#elif defined(RNG_PHILOX)
using DefaultDeviceRNG = Philox;
#endif

#if defined(RNG_XORWOW) || defined(RNG_PHILOX)
#if !defined(MOCK_CURAND) && !defined(MOCK_CUDA)
using RNG = DefaultDeviceRNG;
#endif
#endif

#if defined(__CUDACC__) || defined(__CUDABE__)

template <> struct srng<XORWOW>
{
    static SRNG_METHOD float uniform(XORWOW &state)
    {
        return curand_uniform(&state);
    }
    static SRNG_METHOD double uniform_double(XORWOW &state)
    {
        return curand_uniform_double(&state);
    }
};

template <> struct srng<Philox>
{
    static SRNG_METHOD float uniform(Philox &state)
    {
        return curand_uniform(&state);
    }

    static SRNG_METHOD double uniform_double(Philox &state)
    {
        return curand_uniform_double(&state);
    }
};

#else

#include <cstring>
#include <sstream>
#include <string>

// template specializations for the different states
template<> 
struct srng<XORWOW>  
{ 
    static constexpr char CODE = 'X' ;
    static constexpr const char *NAME = "XORWOW";
    static constexpr unsigned SIZE = sizeof(XORWOW);
    static constexpr bool UPLOAD_RNG_STATES = true;

    static inline float uniform(XORWOW &state)
    {
        return curand_uniform(&state);
    }

    static inline double uniform_double(XORWOW &state)
    {
        return curand_uniform_double(&state);
    }
};

template<> 
struct srng<Philox>  
{ 
    static constexpr char CODE = 'P' ;
    static constexpr const char *NAME = "Philox";
    static constexpr unsigned SIZE = sizeof(Philox);
    static constexpr bool UPLOAD_RNG_STATES = false;

    static inline float uniform(Philox &state)
    {
        return curand_uniform(&state);
    }

    static inline double uniform_double(Philox &state)
    {
        return curand_uniform_double(&state);
    }
};

// helper function
template<typename T> 
inline std::string srng_Desc()
{
    std::stringstream ss ; 
    ss 
       << "[srng_Desc\n" 
       <<  " srng<T>::NAME " << srng<T>::NAME << "\n"
       <<  " srng<T>::CODE " << srng<T>::CODE << "\n"
       <<  " srng<T>::SIZE " << srng<T>::SIZE << "\n"
       << "]srng_Desc" 
       ; 
    std::string str = ss.str() ; 
    return str ; 
}

template<typename T> 
inline bool srng_IsXORWOW(){ return strcmp(srng<T>::NAME, "XORWOW") == 0 ; }

template<typename T> 
inline bool srng_IsPhilox(){ return strcmp(srng<T>::NAME, "Philox") == 0 ; }

template<typename T> 
inline bool srng_Matches(const char* arg)
{
    int match = 0 ; 
    if( arg && strstr(arg, "XORWOW")    && srng_IsXORWOW<T>() )    match += 1 ; 
    if( arg && strstr(arg, "Philox")    && srng_IsPhilox<T>() )    match += 1 ; 
    return match == 1 ; 
} 

#endif
