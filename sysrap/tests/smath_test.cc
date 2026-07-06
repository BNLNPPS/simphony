#include <cmath>
#include <iostream>
#include <iomanip>
#include <type_traits>
#include <utility>

#include "scuda.h"
#include "squad.h"
#include "smath.h"

struct smath_test
{
    static int sincos();
    static int count_nibbles();
    static int rotateUz();
    static int erfcinvf();
    static int main();
};

int smath_test::sincos()
{
    using DoublePair = decltype(smath::sincos(0.0));
    using FloatPair = decltype(smath::sincos(0.0f));
    static_assert(std::is_same<DoublePair, std::pair<double, double>>::value, "double input returns double pair");
    static_assert(std::is_same<FloatPair, std::pair<float, float>>::value, "float input returns float pair");

    const double pi = std::acos(-1.0);
    const double eps = 1e-14;
    int rc = 0;

    auto check_near = [&](const char* label, double phi, double got, double expected, double tol)
    {
        const double delta = std::abs(got - expected);
        if(delta > tol)
        {
            std::cout
                << "smath_test::sincos FAIL "
                << label
                << " phi " << std::setprecision(17) << phi
                << " got " << got
                << " expected " << expected
                << " delta " << delta
                << " tol " << tol
                << std::endl
                ;
            rc += 1;
        }
    };

    struct Case
    {
        double phi;
        double s;
        double c;
    };

    const Case cases[] = {
        { 0.0,       0.0,  1.0 },
        { pi/2.0,    1.0,  0.0 },
        { pi,        0.0, -1.0 },
        { 3.0*pi/2.0,-1.0, 0.0 },
        { 2.0*pi,    0.0,  1.0 },
        { -pi/2.0,  -1.0,  0.0 },
    };

    for(const Case& tc : cases)
    {
        const auto [s, c] = smath::sincos(tc.phi);
        check_near("case.sin", tc.phi, s, tc.s, eps);
        check_near("case.cos", tc.phi, c, tc.c, eps);
        check_near("case.unit", tc.phi, s*s + c*c, 1.0, eps);
    }

    for(int i=0 ; i < 32 ; i++)
    {
        const double phi = -3.0*pi + 6.0*pi*double(i)/31.0;
        const auto [s, c] = smath::sincos(phi);
        check_near("grid.sin", phi, s, std::sin(phi), eps);
        check_near("grid.cos", phi, c, std::cos(phi), eps);
        check_near("grid.unit", phi, s*s + c*c, 1.0, eps);
    }

    const float fphi = 0.25f*float(pi);
    const auto [fs, fc] = smath::sincos(fphi);
    check_near("float.sin", fphi, fs, std::sin(fphi), 1e-6);
    check_near("float.cos", fphi, fc, std::cos(fphi), 1e-6);
    check_near("float.unit", fphi, fs*fs + fc*fc, 1.0, 1e-6);

    return rc;
}


int smath_test::count_nibbles()
{
    typedef unsigned long long ULL ;
    static const int N = 21 ;
    ULL xx[N] ;
    int nn[N] ;

    xx[ 0] = 0x0123456789abcdefull ; nn[ 0] = 15 ;
    xx[ 1] = 0x0023456789abcdefull ; nn[ 1] = 14 ;
    xx[ 2] = 0x0003456789abcdefull ; nn[ 2] = 13 ;
    xx[ 3] = 0x0000456789abcdefull ; nn[ 3] = 12 ;
    xx[ 4] = 0x0000056789abcdefull ; nn[ 4] = 11 ;
    xx[ 5] = 0x0000006789abcdefull ; nn[ 5] = 10 ;
    xx[ 6] = 0x0000000789abcdefull ; nn[ 6] =  9 ;
    xx[ 7] = 0x0000000089abcdefull ; nn[ 7] =  8 ;
    xx[ 8] = 0x0000000009abcdefull ; nn[ 8] =  7 ;
    xx[ 9] = 0x0000000000abcdefull ; nn[ 9] =  6 ;
    xx[10] = 0x00000000000bcdefull ; nn[10] =  5 ;
    xx[11] = 0x000000000000cdefull ; nn[11] =  4 ;
    xx[12] = 0x0000000000000defull ; nn[12] =  3 ;
    xx[13] = 0x00000000000000efull ; nn[13] =  2 ;
    xx[14] = 0x000000000000000full ; nn[14] =  1 ;
    xx[15] = 0x0000000000000000ull ; nn[15] =  0 ;
    xx[16] = 0x0000d00e000a000dull ; nn[16] =  4 ;
    xx[17] = 0x0000100000000000ull ; nn[17] =  1 ;
    xx[18] = 0xa123456789abcdefull ; nn[18] = 16 ;
    xx[19] = 0x1111111111111111ull ; nn[19] = 16 ;
    xx[20] = 0x0000000000000000ull ; nn[20] =  0 ;

    int rc = 0;
    for(int i=0 ; i < N ; i++)
    {
        ULL x = xx[i] ;
        int n = smath::count_nibbles(x) ;
        if(n != nn[i])
        {
            std::cout
                << "smath_test::count_nibbles FAIL "
                << " i " << std::setw(3)  << i
                << " x " << std::setw(16) << std::hex << x  << std::dec
                << " n " << std::setw(3)  << n
                << " expected " << std::setw(3) << nn[i]
                << std::endl
                ;
            rc += 1;
        }
    }
    return rc;
}


/**
rotateUz
---------

Consider mom is some direction, say +Z::

   (0, 0, 1)

There is a circle of vectors that are perpendicular
to that mom, all in the XY plane, and with dot product
with that direction of zero::

   ( cos(phi), sin(phi), 0 )    phi 0->2pi

**/

int smath_test::rotateUz()
{
    float3 u = normalize(make_float3( 1.f, 0.f, -1.f ));

    static const int N = 16 ;
    int rc = 0;
    for(int i=0 ; i <= N ; i++)
    {
        float phi = 2.f*M_PIf*float(i)/float(N) ;
        float3 d0 = make_float3( cos(phi), sin(phi), 0.f ) ;
        // d0: ring of vectors in XY plane, "around" the +Z direction

        float3 d1(d0);
        smath::rotateUz(d1,u);

        // d1: rotated XY ring of vectors to point in direction u
        // So all the d1 are perpendicular to u

        const float dot_d1_u = dot(d1,u);
        const float len_d0 = length(d0);
        const float len_d1 = length(d1);
        const float tol = 1e-5f;

        if(std::abs(dot_d1_u) > tol || std::abs(len_d1 - len_d0) > tol)
        {
            std::cout
                << "smath_test::rotateUz FAIL "
                << " i " << std::setw(2) << i
                << " d0 " << d0
                << " d1 " << d1
                << " dot(d1,u) " << dot_d1_u
                << " len_d0 " << len_d0
                << " len_d1 " << len_d1
                << std::endl
                ;
            rc += 1;
        }
    }

    return rc;
}

int smath_test::erfcinvf()
{
#if defined(__CUDACC__) || defined(__CUDABE__) || defined(MOCK_CUDA)
    float SQRT2 = sqrtf(2.f) ;
    int rc = 0;

    int N = 100 ;
    for(int i=0 ; i < N ; i++)
    {
        float u2 = 2.f*float(i)/float(N-1) ;
        float v = -SQRT2*smath::erfcinvf(u2) ;
        std::cout
            << " i " << std::setw(5) << i
            << " u2 " << std::setw(10) << std::setprecision(5) << std::fixed << u2
            << " v  " << std::setw(10) << std::setprecision(5) << std::fixed << v
            << std::endl
            ;
        if(!std::isfinite(v) && i != 0 && i != N-1) rc += 1;
    }
    return rc;
#else
    return smath::erfcinvf(1.f) == 0.f ? 0 : 1;
#endif
}

int smath_test::main()
{
    int rc = 0;
    rc += sincos();
    rc += count_nibbles();
    rc += rotateUz();
    rc += erfcinvf();

    return rc;
}

int main(int argc, char** argv)
{
    return smath_test::main();
}
