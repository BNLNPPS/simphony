//
// Compatibility facade for Simphony code that historically included scuda.h.
//
// NVIDIA's CUDA vector math helpers are provided by the vendored official
// OptiX Toolkit vec_math.h. This header keeps only project-local helpers and
// legacy names that are not provided by that upstream header.
//

#pragma once

#include <OptiXToolkit/ShaderUtil/vec_math.h>
#include <cuda_runtime_api.h>

#if defined(__CUDACC__) || defined(__CUDABE__)
#define SUTIL_HOSTDEVICE __host__ __device__
#define SUTIL_INLINE __forceinline__
#else
#define SUTIL_HOSTDEVICE
#define SUTIL_INLINE inline
#endif

#if defined(__CUDACC__) && (__CUDACC_VER_MAJOR__ >= 13)
#define LONGLONG4 longlong4_32a
#define ULONGLONG4 ulonglong4_32a
#define DOUBLE4 double4_32a
#define MAKE_LONGLONG4 make_longlong4_32a
#define MAKE_ULONGLONG4 make_ulonglong4_32a
#else
#if (CUDART_VERSION >= 13000)
#define LONGLONG4 longlong4_32a
#define ULONGLONG4 ulonglong4_32a
#define DOUBLE4 double4_32a
#define MAKE_LONGLONG4 make_longlong4_32a
#define MAKE_ULONGLONG4 make_ulonglong4_32a
#else
#define LONGLONG4 longlong4
#define ULONGLONG4 ulonglong4
#define DOUBLE4 double4
#define MAKE_LONGLONG4 make_longlong4
#define MAKE_ULONGLONG4 make_ulonglong4
#endif
#endif

#ifndef M_SQRT2f
#define M_SQRT2f 1.4142135623730951f
#endif

// Preserve the previous scuda.h contract: callers use vec_math helpers without
// an otk:: qualifier.
using namespace otk;

#if CUDART_VERSION < 13000
// CUDA 12 needs these legacy scuda overloads because the upstream compatibility
// guard hides the adjacent OptiX Toolkit definitions with CUDA 13-only types.
SUTIL_INLINE SUTIL_HOSTDEVICE float2 make_float2(const float3& v)
{
    return ::make_float2(v.x, v.y);
}

SUTIL_INLINE SUTIL_HOSTDEVICE float2 make_float2(const float4& v)
{
    return ::make_float2(v.x, v.y);
}

SUTIL_INLINE SUTIL_HOSTDEVICE float3 make_float3(const float4& v)
{
    return ::make_float3(v.x, v.y, v.z);
}
#endif

SUTIL_INLINE SUTIL_HOSTDEVICE float normalize_cost(const float3& v)
{
    return v.z / sqrtf(dot(v, v));
}

SUTIL_INLINE SUTIL_HOSTDEVICE float normalize_fphi(const float3& v)
{
    return (atan2f(v.y, v.x) + M_PIf) / (2.0f * M_PIf);
}

SUTIL_INLINE SUTIL_HOSTDEVICE float phi_from_fphi(float fphi)
{
    return (fphi * 2.0f - 1.f) * M_PIf;
}

#if !defined(__CUDACC__) && !defined(__CUDABE__)

#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

struct scuda
{
    template <typename T>
    static T sval(const char* str);

    template <typename T>
    static T eval(const char* ekey, T fallback);

    template <typename T>
    static void svec(std::vector<T>& v, const char* str, char delim = ',');

    template <typename T>
    static void evec(std::vector<T>& v, const char* ekey, const char* fallback, char delim = ',');

    static float efloat(const char* ekey, float fallback = 0.f);
    static float3 efloat3(const char* ekey, const char* fallback = "0,0,0", char delim = ',');
    static float4 efloat4(const char* ekey, const char* fallback = "0,0,0,0", char delim = ',');
    static float3 efloat3n(const char* ekey, const char* fallback = "0,0,0", char delim = ',');

    static std::string serialize(const float2& v);
    static std::string serialize(const float3& v);
    static std::string serialize(const float4& v);

    static float4 center_extent_(const float* mn, const float* mx);
    static float4 center_extent(const float3& mn, const float3& mx);
    static float4 center_extent(const float4& mn, const float4& mx);
};

template <typename T>
inline T scuda::sval(const char* str)
{
    std::string        s(str);
    std::istringstream iss(s);
    T                  t;
    iss >> t;
    return t;
}

template <typename T>
inline T scuda::eval(const char* ekey, T fallback)
{
    const char* str = getenv(ekey);
    return str ? sval<T>(str) : fallback;
}

template <typename T>
inline void scuda::svec(std::vector<T>& v, const char* str, char delim)
{
    std::stringstream ss;
    ss.str(str);
    std::string s;
    while (std::getline(ss, s, delim))
    {
        T t = sval<T>(s.c_str());
        v.push_back(t);
    }
}

template <typename T>
inline void scuda::evec(std::vector<T>& v, const char* ekey, const char* fallback, char delim)
{
    const char* str = getenv(ekey);
    svec(v, str ? str : fallback, delim);
}

inline float scuda::efloat(const char* ekey, float fallback)
{
    return eval<float>(ekey, fallback);
}

inline float3 scuda::efloat3(const char* ekey, const char* fallback, char delim)
{
    std::vector<float> fv;
    evec(fv, ekey, fallback, delim);
    return make_float3(
        fv.size() > 0 ? fv[0] : 0.f,
        fv.size() > 1 ? fv[1] : 0.f,
        fv.size() > 2 ? fv[2] : 0.f);
}

inline float4 scuda::efloat4(const char* ekey, const char* fallback, char delim)
{
    std::vector<float> fv;
    evec(fv, ekey, fallback, delim);
    return make_float4(
        fv.size() > 0 ? fv[0] : 0.f,
        fv.size() > 1 ? fv[1] : 0.f,
        fv.size() > 2 ? fv[2] : 0.f,
        fv.size() > 3 ? fv[3] : 0.f);
}

inline float3 scuda::efloat3n(const char* ekey, const char* fallback, char delim)
{
    float3 v = efloat3(ekey, fallback, delim);
    return normalize(v);
}

inline std::string scuda::serialize(const float2& v)
{
    std::stringstream ss;
    ss << v.x << "," << v.y;
    return ss.str();
}

inline std::string scuda::serialize(const float3& v)
{
    std::stringstream ss;
    ss << v.x << "," << v.y << "," << v.z;
    return ss.str();
}

inline std::string scuda::serialize(const float4& v)
{
    std::stringstream ss;
    ss << v.x << "," << v.y << "," << v.z << "," << v.w;
    return ss.str();
}

inline float4 scuda::center_extent_(const float* mn, const float* mx)
{
    float3 center = make_float3((mn[0] + mx[0]) / 2.f, (mn[1] + mx[1]) / 2.f, (mn[2] + mx[2]) / 2.f);
    float3 mx_rel = make_float3(mx[0], mx[1], mx[2]) - center;
    float3 mn_rel = center - make_float3(mn[0], mn[1], mn[2]);
    float  extent = fmaxf(fmaxf(mx_rel), fmaxf(mn_rel));
    return make_float4(center, extent);
}

inline float4 scuda::center_extent(const float3& mn, const float3& mx)
{
    return center_extent_(&mn.x, &mx.x);
}

inline float4 scuda::center_extent(const float4& mn, const float4& mx)
{
    return center_extent_(&mn.x, &mx.x);
}

inline std::ostream& operator<<(std::ostream& os, const float2& v)
{
    int w = 6;
    os << "("
       << std::setw(w) << std::fixed << std::setprecision(3) << v.x
       << ","
       << std::setw(w) << std::fixed << std::setprecision(3) << v.y
       << ") ";
    return os;
}

inline std::ostream& operator<<(std::ostream& os, const float3& v)
{
    int w = 6;
    os << "("
       << std::setw(w) << std::fixed << std::setprecision(3) << v.x
       << ","
       << std::setw(w) << std::fixed << std::setprecision(3) << v.y
       << ","
       << std::setw(w) << std::fixed << std::setprecision(3) << v.z
       << ") ";
    return os;
}

inline std::ostream& operator<<(std::ostream& os, const float4& v)
{
    int w = 6;
    os << "("
       << std::setw(w) << std::fixed << std::setprecision(3) << v.x
       << ","
       << std::setw(w) << std::fixed << std::setprecision(3) << v.y
       << ","
       << std::setw(w) << std::fixed << std::setprecision(3) << v.z
       << ","
       << std::setw(w) << std::fixed << std::setprecision(3) << v.w
       << ") ";
    return os;
}

inline std::ostream& operator<<(std::ostream& os, const int2& v)
{
    int w = 6;
    os << "(" << std::setw(w) << v.x << "," << std::setw(w) << v.y << ") ";
    return os;
}

inline std::ostream& operator<<(std::ostream& os, const int3& v)
{
    int w = 6;
    os << "("
       << std::setw(w) << v.x
       << ","
       << std::setw(w) << v.y
       << ","
       << std::setw(w) << v.z
       << ") ";
    return os;
}

inline std::ostream& operator<<(std::ostream& os, const int4& v)
{
    int w = 6;
    os << "("
       << std::setw(w) << v.x
       << ","
       << std::setw(w) << v.y
       << ","
       << std::setw(w) << v.z
       << ","
       << std::setw(w) << v.w
       << ") ";
    return os;
}

inline std::ostream& operator<<(std::ostream& os, const short4& v)
{
    int w = 6;
    os << "("
       << std::setw(w) << v.x
       << ","
       << std::setw(w) << v.y
       << ","
       << std::setw(w) << v.z
       << ","
       << std::setw(w) << v.w
       << ") ";
    return os;
}

inline std::ostream& operator<<(std::ostream& os, const uchar4& v)
{
    int w = 4;
    os << "("
       << std::setw(w) << int(v.x)
       << ","
       << std::setw(w) << int(v.y)
       << ","
       << std::setw(w) << int(v.z)
       << ","
       << std::setw(w) << int(v.w)
       << ") ";
    return os;
}

inline std::ostream& operator<<(std::ostream& os, const char4& v)
{
    int w = 4;
    os << "("
       << std::setw(w) << int(v.x)
       << ","
       << std::setw(w) << int(v.y)
       << ","
       << std::setw(w) << int(v.z)
       << ","
       << std::setw(w) << int(v.w)
       << ") ";
    return os;
}

inline std::ostream& operator<<(std::ostream& os, const uint3& v)
{
    int w = 6;
    os << "("
       << std::setw(w) << v.x
       << ","
       << std::setw(w) << v.y
       << ","
       << std::setw(w) << v.z
       << ") ";
    return os;
}

inline std::ostream& operator<<(std::ostream& os, const uint4& v)
{
    int w = 6;
    os << "("
       << std::setw(w) << v.x
       << ","
       << std::setw(w) << v.y
       << ","
       << std::setw(w) << v.z
       << ","
       << std::setw(w) << v.w
       << ") ";
    return os;
}

#endif
