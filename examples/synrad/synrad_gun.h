// synrad_gun.h — photon gun and hit I/O of the synrad example.
#pragma once

#include <cmath>
#include <cstring>
#include <iostream>
#include <random>
#include <string>
#include <vector>

#include "sysrap/NP.hh"
#include "sysrap/OpticksPhoton.h"
#include "sysrap/sphoton.h"

// pencil-beam photon gun: log-uniform energy in [emin,emax] keV, direction
// Gaussian smeared around (dx,dy,dz) by fan_mrad
static NP* GeneratePhotons(int n, unsigned seed, const double* I, double fan_mrad)
{
    NP* ip = NP::Make<float>(n, 4, 4);
    sphoton* pp = reinterpret_cast<sphoton*>(ip->values<float>());

    double dn = std::sqrt(I[3]*I[3] + I[4]*I[4] + I[5]*I[5]) ;
    double d0[3] = { I[3]/dn, I[4]/dn, I[5]/dn } ;
    // orthonormal transverse basis for the fan
    double a[3] = { -d0[1], d0[0], 0. } ;
    double an = std::sqrt(a[0]*a[0] + a[1]*a[1]) ;
    if( an < 1e-6 ){ a[0] = 1. ; a[1] = 0. ; a[2] = 0. ; an = 1. ; }
    a[0] /= an ; a[1] /= an ;
    double b[3] = { d0[1]*a[2] - d0[2]*a[1], d0[2]*a[0] - d0[0]*a[2], d0[0]*a[1] - d0[1]*a[0] } ;

    std::mt19937 rng(seed);
    std::uniform_real_distribution<double> uni(0., 1.);
    std::normal_distribution<double> fan(0., fan_mrad*1e-3);

    for(int i=0 ; i < n ; i++)
    {
        sphoton& p = pp[i] ;
        p.pos.x = float(I[0]) ; p.pos.y = float(I[1]) ; p.pos.z = float(I[2]) ;
        p.time = 0.f ;

        double ta = fan(rng), tb = fan(rng) ;
        double d[3] = { d0[0] + ta*a[0] + tb*b[0], d0[1] + ta*a[1] + tb*b[1], d0[2] + ta*a[2] + tb*b[2] } ;
        double dl = std::sqrt(d[0]*d[0] + d[1]*d[1] + d[2]*d[2]) ;
        p.mom.x = float(d[0]/dl) ; p.mom.y = float(d[1]/dl) ; p.mom.z = float(d[2]/dl) ;

        p.pol.x = 1.f ;
        p.wavelength = float( I[6]*std::pow(I[7]/I[6], uni(rng)) ) ;  // energy, keV
    }
    return ip ;
}

static void Summary(const char* tag, long n, const std::vector<sphoton>& hits,
                    const double* U, double t_s, const std::string& path)
{
    unsigned n_refl = 0, n_cap = 0 ;
    for(const sphoton& h : hits)
    {
        if( h.flagmask & BOUNDARY_REFLECT ) n_refl += 1 ;
        if( h.pos.z < U[0] || h.pos.z > U[1] ) n_cap += 1 ;
    }
    std::cout << tag << ":"
              << " photons " << n
              << " wall-absorbed " << hits.size()
              << " reflected>=1 " << n_refl
              << " on-caps " << n_cap
              << " | transport " << t_s << " s = " << 1e6*t_s/double(n > 0 ? n : 1) << " us/photon"
              << " | " << path
              << std::endl ;
}

static void SaveHits(const std::vector<sphoton>& hits, const std::string& path)
{
    if( hits.empty() ) return ;
    NP* a = NP::Make<float>(int(hits.size()), 4, 4);
    memcpy( a->bytes(), hits.data(), hits.size()*sizeof(sphoton) );
    a->save(path.c_str());
}
