#pragma once
/**
qgxs.h : gamma (soft X-ray) grazing-incidence surface reflection on GPU
=========================================================================

Device-side counterpart of QGXS.hh for the synchrotron-radiation X-ray
transport mode (qsim::propagate_gamma, active when qsim::gxs is non-null).
Photon energy is carried in the sphoton wavelength slot, in keV; X-rays fly
at c (n ~= 1) through the vacuum and interact only AT vacuum->wall
boundaries, where they either reflect or are killed at the surface -- the
reflect-or-absorb model of the SynradG4 benchmark
GammaReflectionProcess (https://github.com/eicorg/SynradBenchmark).

The reflection probability is a tabulated Cu reflectivity over (grazing
angle, energy), qgxs_synradg4.h, bilinearly log-log interpolated with the
same closest-index semantics as the reference FindClosestIndexesInVec.
A reflected photon is specular with probability = Debye-Waller factor
exp(-2 (k_iz sigma)^2), k_iz = sin(grazing) E/hbarc; otherwise diffuse:
polar/azimuth Gaussian perturbation of the specular direction in the GLOBAL
frame with sigma_theta = 2.9267/tau and
sigma_phi = (2.80657 a^-1.00238 - 1.00293 a^1.2266)/tau, tau = T/sigma_RMS,
a = angle from the surface normal in radians.

Boundary contacts at z outside [cap_zlo, cap_zhi] absorb unconditionally
(the absorber end plates).
**/

#if defined(__CUDACC__) || defined(__CUDABE__)
   #define QGXS_METHOD __device__
#else
   #define QGXS_METHOD
#endif

#include "qrng.h"

struct sphoton ;

struct qgxs
{
    float sigma_mm ;   // surface RMS roughness
    float inv_tau ;    // sigma/T (0 when T unset); diffuse smearing scale
    float cap_zlo ;    // z below this -> unconditional surface absorb
    float cap_zhi ;    // z above this -> unconditional surface absorb

    static constexpr float SPEED_OF_LIGHT = 299.792458f ; // mm/ns, X-rays fly at c (n ~= 1)
    static constexpr float HBARC_KEV_MM = 1.9732698e-7f ; // hbar*c in keV*mm (k = sin(theta)*E/hbarc)
    static constexpr float PUSH = 0.010f ;                // mm, post-reflection push-off along the
                                                          // vacuum-side normal: at grazing sine ~mrad a
                                                          // float32 position within an ulp of the surface
                                                          // can falsely re-intersect it at t ~ ulp/sin_g

#if defined(__CUDACC__) || defined(__CUDABE__) || defined(MOCK_CURAND) || defined(MOCK_CUDA)
    QGXS_METHOD int boundary_reflect3( RNG& rng, sphoton& p, const float3& nrm, float cosTheta ) const ;
#endif
};

#if defined(__CUDACC__) || defined(__CUDABE__) || defined(MOCK_CURAND) || defined(MOCK_CUDA)

#include "smath.h"
#include "sphoton.h"
#include "qgxs_synradg4.h"

/**
qsg_closest : indices i1,i2 of the two grid values bracketing val, clamped at
the edges, i1 == i2 on an exact hit -- port of the reference
FindClosestIndexesInVec, whose edge-clamp semantics the interpolation parity
depends on.
**/

inline QGXS_METHOD void qsg_closest( const float* v, int n, float val, int& i1, int& i2 )
{
    int lo = 0, hi = n ;
    while( lo < hi ){ int mid = (lo + hi)/2 ; if( v[mid] < val ) lo = mid + 1 ; else hi = mid ; }
    i2 = lo ;
    if( lo == n ) { i2 = n - 1 ; }
    else if( v[i2] == val ){ i1 = i2 ; return ; }
    i1 = i2 ? i2 - 1 : i2 ;
    i1 = ( val > v[i2] ) ? i2 : i1 ;
}

/**
qsg_loglog : bilinear interpolation of table P[angle][energy], linear in
log10(angle) and log10(energy), flat-clamped outside the grid.
**/

inline QGXS_METHOD float qsg_loglog( const float* P, const float* av, const float* lav,
                                     const float* ev, const float* lev, int ne_,
                                     float aval, float eval )
{
    int ai, aj, ei, ej ;
    qsg_closest( av, QSG_NA, aval, ai, aj );
    qsg_closest( ev, ne_,   eval, ei, ej );
    float pii = P[ai*ne_+ei], pji = P[aj*ne_+ei], pij = P[ai*ne_+ej], pjj = P[aj*ne_+ej] ;
    float p1, p2 ;
    if( ev[ej] == ev[ei] ){ p1 = pii ; p2 = pji ; }
    else
    {
        float le = log10f(eval) ;
        float k1 = (pij - pii)/(lev[ej] - lev[ei]) ; p1 = pii + k1*(le - lev[ei]) ;
        float k2 = (pjj - pji)/(lev[ej] - lev[ei]) ; p2 = pji + k2*(le - lev[ei]) ;
    }
    if( av[aj] == av[ai] ) return p1 ;
    float k = (p2 - p1)/(lav[aj] - lav[ai]) ;
    return p1 + k*(log10f(aval) - lav[ai]) ;
}

/**
qgxs::boundary_reflect3
------------------------

nrm is the intersect normal and cosTheta = dot(p.mom, nrm), frontface-flipped
by the caller so cosTheta > 0 on entry. Returns 1 (p.mom updated) on
reflection, 0 when the photon is absorbed at the wall.
**/

inline QGXS_METHOD int qgxs::boundary_reflect3( RNG& rng, sphoton& p, const float3& nrm, float cosTheta ) const
{
    if( p.pos.z < cap_zlo || p.pos.z > cap_zhi ) return 0 ;  // absorber end plates: unconditional surface absorb

    float sin_g = fminf( fabsf(cosTheta), 1.f );
    float graz  = asinf( sin_g );             // grazing angle, rad
    float E_eV  = p.wavelength*1000.f ;

    float prob = qsg_loglog( QSG_CU_P, QSG_ANGLE, QSG_LOG_ANGLE, QSG_CU_E, QSG_CU_LOGE, QSG_CU_NE, graz, E_eV ) ;

    float kiz = sin_g*p.wavelength/HBARC_KEV_MM ;
    float argw = 2.f*(kiz*sigma_mm)*(kiz*sigma_mm) ;
    float probSpec = argw < 80.f ? expf(-argw) : 0.f ;     // Debye-Waller specular fraction

    float3 nv  = -1.f*nrm ;                                // into the vacuum (cosTheta > 0 after the flip)
    float3 dir = normalize( p.mom - 2.f*cosTheta*nrm );    // specular

    if( curand_uniform(&rng) > probSpec && inv_tau > 0.f ) // diffuse: perturb GLOBAL theta/phi
    {
        float alpha  = acosf( sin_g );                     // angle(mom, outward normal)
        float sig_th = 2.9267f*inv_tau ;
        float sig_ph = ( 2.80657f*powf(alpha, -1.00238f) - 1.00293f*powf(alpha, 1.2266f) )*inv_tau ;
        if( sig_ph < 0.f ) sig_ph = 0.f ;
        float th0 = acosf( fminf(fmaxf(dir.z, -1.f), 1.f) );
        float ph0 = atan2f(dir.y, dir.x) ;
        for(int i=0 ; i < 1000 ; i++)
        {
            float u1 = curand_uniform(&rng) ;
            float u2 = curand_uniform(&rng) ;
            float rr = sqrtf(fmaxf(-2.f*logf(u1), 0.f)) ;
            float th = th0 + sig_th*rr*cosf(2.f*M_PIf*u2) ;
            float ph = ph0 + sig_ph*rr*sinf(2.f*M_PIf*u2) ;
            float sth, cth, sph, cph ;
#if defined(MOCK_CURAND) || defined(MOCK_CUDA)
            sth = sinf(th) ; cth = cosf(th) ; sph = sinf(ph) ; cph = cosf(ph) ;
#else
            sincosf(th, &sth, &cth);
            sincosf(ph, &sph, &cph);
#endif
            // CLHEP setTheta+setPhi fold: setPhi rebuilds x,y from perp() >= 0, so a
            // pole-crossing theta draw keeps azimuth ph with |sin(th)| transverse
            // magnitude. Signed sin would flip the azimuth by pi there, rejecting the
            // shallow skaters and biasing the accepted outgoing distribution steeper
            // at grazing (verified vs TVector3).
            float asth = fabsf(sth) ;
            float3 cand = make_float3( asth*cph, asth*sph, cth );
            if( dot(cand, nv) > 0.f ){ dir = cand ; break ; }
            // sampled into the surface: retry; keep unperturbed specular after 1000 tries
        }
    }

    if( curand_uniform(&rng) > prob ) return 0 ;   // absorbed at the wall
    p.mom = dir ;
    return 1 ;
}

#endif
