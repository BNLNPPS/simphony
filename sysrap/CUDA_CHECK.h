#pragma once

#include <cstdio>
#include <cuda_runtime.h>
#include <sstream>
#include <stdexcept>

#define CUDA_CHECK( call )                                                     \
    do                                                                         \
    {                                                                          \
        cudaError_t error = call;                                              \
        if( error != cudaSuccess )                                             \
        {                                                                      \
            std::stringstream ss;                                              \
            ss << "CUDA call (" << #call << " ) failed with error: '"          \
               << cudaGetErrorString( error )                                  \
               << "' (" __FILE__ << ":" << __LINE__ << ")\n";                  \
            throw CUDA_Exception( ss.str().c_str() );                        \
        }                                                                      \
    } while( 0 )

inline void CUDA_Log_NoThrow(cudaError_t error, const char* call, const char* file, int line) noexcept
{
    if (error != cudaSuccess)
    {
        std::stringstream ss;
        ss << "CUDA call (" << call << " ) failed with error: '"
           << cudaGetErrorString(error)
           << "' (" << file << ":" << line << ")\n";

        const std::string msg = ss.str();
        std::fputs(msg.c_str(), stderr);
    }
}

#define CUDA_CHECK_NOEXCEPT(call)                           \
    do                                                      \
    {                                                       \
        cudaError_t error = call;                           \
        CUDA_Log_NoThrow(error, #call, __FILE__, __LINE__); \
    } while (0)

#define CUDA_SYNC_CHECK()                                                      \
    do                                                                         \
    {                                                                          \
        cudaDeviceSynchronize();                                               \
        cudaError_t error = cudaGetLastError();                                \
        if( error != cudaSuccess )                                             \
        {                                                                      \
            std::stringstream ss;                                              \
            ss << "CUDA error on synchronize with error '"                     \
               << cudaGetErrorString( error )                                  \
               << "' (" __FILE__ << ":" << __LINE__ << ")\n";                  \
            throw CUDA_Exception( ss.str().c_str() );                        \
        }                                                                      \
    } while( 0 )



class CUDA_Exception : public std::runtime_error
{
 public:
     CUDA_Exception( const char* msg )
         : std::runtime_error( msg )
     { }

};
