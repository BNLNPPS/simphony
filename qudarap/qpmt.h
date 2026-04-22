#pragma once
/**
qpmt.h
=======


**/

#if defined(__CUDACC__) || defined(__CUDABE__)
   #define QPMT_METHOD __device__
#else
   #define QPMT_METHOD
#endif





#if defined(__CUDACC__) || defined(__CUDABE__)
#else
#include "QUDARAP_API_EXPORT.hh"
#endif


template <typename T> struct qprop ;

#include "scuda.h"
#include "squad.h"
#include "qprop.h"

#include "s_pmt.h"


template<typename F>
struct qpmt
{
    enum { L0, L1, L2, L3 } ;

    static constexpr const F hc_eVnm = 1239.84198433200208455673  ;
    static constexpr const F zero = 0. ;
    static constexpr const F one = 1. ;
    // constexpr should mean any double conversions happen at compile time ?

    qprop<F>* rindex_prop ;
    qprop<F>* qeshape_prop ;
    qprop<F>* cetheta_prop ;
    qprop<F>* cecosth_prop ;

    F*        thickness ;
    F*        lcqs ;
    int*      i_lcqs ;  // int* "view" of lcqs memory

    qprop<F>* s_qeshape_prop ;
    F*        s_qescale ;


#if defined(__CUDACC__) || defined(__CUDABE__) || defined( MOCK_CURAND ) || defined(MOCK_CUDA)
    // loosely follow SPMT.h
    QPMT_METHOD int  get_lpmtcat_from_lpmtid(  int lpmtid  ) const  ;
    QPMT_METHOD int  get_lpmtcat_from_lpmtidx( int lpmtidx ) const  ;
    QPMT_METHOD F    get_qescale_from_lpmtid(  int lpmtid  ) const  ;
    QPMT_METHOD F    get_qescale_from_lpmtidx( int lpmtidx ) const  ;

    QPMT_METHOD F    get_s_qescale_from_spmtid(  int spmtid  ) const  ;


    QPMT_METHOD F    get_lpmtcat_qe( int pmtcat, F energy_eV ) const ;
    QPMT_METHOD F    get_lpmtcat_ce( int pmtcat, F theta ) const ;

    QPMT_METHOD F    get_lpmtcat_rindex(    int lpmtcat, int layer, int prop, F energy_eV ) const ;
    QPMT_METHOD F    get_lpmtcat_rindex_wl( int lpmtcat, int layer, int prop, F wavelength_nm ) const ;


    QPMT_METHOD void get_lpmtcat_stackspec( F* spec16, int pmtcat, F energy_eV ) const ;

    QPMT_METHOD void get_lpmtid_stackspec(          F* spec16, int lpmtid, F energy_eV ) const ;


#if !defined(PRODUCTION) && defined(DEBUG_PIDX)
    QPMT_METHOD void get_lpmtid_stackspec_ce_acosf( F* spec16, int lpmtid, F energy_eV, F lposcost, unsigned pidx, bool pidx_debug ) const ;
#else
    QPMT_METHOD void get_lpmtid_stackspec_ce_acosf( F* spec16, int lpmtid, F energy_eV, F lposcost ) const ;
#endif

    QPMT_METHOD void get_lpmtid_stackspec_ce(       F* spec15, int lpmtid, F energy_eV, F lposcost ) const ;

// end __CUDACC__ etc..
#endif
};

#if defined(__CUDACC__) || defined(__CUDABE__) || defined( MOCK_CURAND ) || defined(MOCK_CUDA)

template<typename F>
inline QPMT_METHOD int qpmt<F>::get_lpmtcat_from_lpmtid( int lpmtid ) const
{
    int lpmtidx = s_pmt::lpmtidx_from_pmtid(lpmtid);
    return lpmtidx < s_pmt::NUM_LPMTIDX && lpmtidx > -1 ? i_lcqs[lpmtidx*2+0] : -2 ;
    // extended from former NUM_CD_LPMT_AND_WP after s_pmt.h overhaul for WP_ATM_LPMT WP_WAL_PMT
}

template<typename F>
inline QPMT_METHOD int qpmt<F>::get_lpmtcat_from_lpmtidx( int lpmtidx ) const
{
    return lpmtidx < s_pmt::NUM_LPMTIDX && lpmtidx > -1 ? i_lcqs[lpmtidx*2+0] : -2 ;
}

template<typename F>
inline QPMT_METHOD F qpmt<F>::get_qescale_from_lpmtid( int lpmtid ) const
{
    int lpmtidx = s_pmt::lpmtidx_from_pmtid(lpmtid);
    return lpmtidx < s_pmt::NUM_LPMTIDX && lpmtidx > -1 ? lcqs[lpmtidx*2+1] : -2.f ;
}
template<typename F>
inline QPMT_METHOD F qpmt<F>::get_qescale_from_lpmtidx( int lpmtidx ) const
{
    return lpmtidx < s_pmt::NUM_LPMTIDX && lpmtidx > -1 ? lcqs[lpmtidx*2+1] : -2.f ;
}


template<typename F>
inline QPMT_METHOD F qpmt<F>::get_s_qescale_from_spmtid( int spmtid ) const
{
    int spmtidx = s_pmt::spmtidx_from_pmtid(spmtid);
    return spmtidx < s_pmt::NUM_SPMT && spmtidx > -1 ? s_qescale[spmtidx] : -2.f ;
}







template<typename F>
inline QPMT_METHOD F qpmt<F>::get_lpmtcat_qe( int lpmtcat, F energy_eV ) const
{
    return lpmtcat > -1 && lpmtcat < s_pmt::NUM_CAT ? qeshape_prop->interpolate( lpmtcat, energy_eV ) : -1.f ;
}

/**
qpmt::get_lpmtcat_ce
---------------------

theta_radians range 0->pi/2


              cos(th)=1
                th=0

                  |
               +  |  +
           +      |      +
                  |
         +        |         +
        +---------+----------+--   th = pi/2
                                cos(th)=0

**/

template<typename F>
inline QPMT_METHOD F qpmt<F>::get_lpmtcat_ce( int lpmtcat, F theta_radians ) const
{
    //return lpmtcat > -1 && lpmtcat < qpmt_NUM_CAT ? cetheta_prop->interpolate( lpmtcat, theta_radians ) : -1.f ;
    return lpmtcat > -1 && lpmtcat < s_pmt::NUM_CAT ? cetheta_prop->interpolate( lpmtcat, theta_radians ) : -1.f ;
}
template<typename F>
inline QPMT_METHOD F qpmt<F>::get_lpmtcat_rindex( int lpmtcat, int layer, int prop, F energy_eV ) const
{
    //const unsigned idx = lpmtcat*qpmt_NUM_LAYR*qpmt_NUM_PROP + layer*qpmt_NUM_PROP + prop ;
    const unsigned idx = lpmtcat*s_pmt::NUM_LAYR*s_pmt::NUM_PROP + layer*s_pmt::NUM_PROP + prop ;
    return rindex_prop->interpolate( idx, energy_eV ) ;
}
template<typename F>
inline QPMT_METHOD F qpmt<F>::get_lpmtcat_rindex_wl( int lpmtcat, int layer, int prop, F wavelength_nm ) const
{
    const F energy_eV = hc_eVnm/wavelength_nm ;
    return get_lpmtcat_rindex(lpmtcat, layer, prop, energy_eV );
}



template<typename F>
inline QPMT_METHOD void qpmt<F>::get_lpmtcat_stackspec( F* spec, int lpmtcat, F energy_eV ) const
{
    //const unsigned idx = lpmtcat*qpmt_NUM_LAYR*qpmt_NUM_PROP ;
    //const unsigned idx0 = idx + L0*qpmt_NUM_PROP ;
    //const unsigned idx1 = idx + L1*qpmt_NUM_PROP ;
    //const unsigned idx2 = idx + L2*qpmt_NUM_PROP ;

    const unsigned idx = lpmtcat*s_pmt::NUM_LAYR*s_pmt::NUM_PROP ;

    const unsigned idx0 = idx + L0*s_pmt::NUM_PROP ;
    const unsigned idx1 = idx + L1*s_pmt::NUM_PROP ;
    const unsigned idx2 = idx + L2*s_pmt::NUM_PROP ;


    spec[0*4+0] = rindex_prop->interpolate( idx0+0u, energy_eV );
    spec[0*4+1] = zero ;
    spec[0*4+2] = zero ;

    spec[1*4+0] = rindex_prop->interpolate( idx1+0u, energy_eV );
    spec[1*4+1] = rindex_prop->interpolate( idx1+1u, energy_eV );
    spec[1*4+2] = thickness[lpmtcat*s_pmt::NUM_LAYR+L1] ;

    spec[2*4+0] = rindex_prop->interpolate( idx2+0u, energy_eV );
    spec[2*4+1] = rindex_prop->interpolate( idx2+1u, energy_eV );
    spec[2*4+2] = thickness[lpmtcat*s_pmt::NUM_LAYR+L2] ;

    spec[3*4+0] = one ;  // Vacuum RINDEX
    spec[3*4+1] = zero ;
    spec[3*4+2] = zero ;

    // "4th" column untouched, as pmtid info goes in there
}



template<typename F>
inline QPMT_METHOD void qpmt<F>::get_lpmtid_stackspec( F* spec, int lpmtid, F energy_eV ) const
{
    int lpmtidx = s_pmt::lpmtidx_from_pmtid(lpmtid);
    const int& lpmtcat = i_lcqs[lpmtidx*2+0] ;
    if(lpmtcat < 0) printf("//qpmt::get_lpmtidx_stackspec lpmtid %d lpmtcat %d \n", lpmtid, lpmtcat );

    const F& qe_scale = lcqs[lpmtidx*2+1] ;
    const F qe_shape = qeshape_prop->interpolate( lpmtcat, energy_eV ) ;
    const F qe = qe_scale*qe_shape ;

    spec[0*4+3] = lpmtcat ;  // profligate int to float, s.spec[:,:,0,3].astype(np.int32)
    spec[1*4+3] = qe_scale ;
    spec[2*4+3] = qe_shape ;
    spec[3*4+3] = qe ;

    get_lpmtcat_stackspec( spec, lpmtcat, energy_eV );
}


/**
get_lpmtid_stackspec_ce_acosf (see alternative get_lpmtid_stackspec_ce)
-------------------------------------------------------------------------

This uses cetheta_prop interpolation forcing use of acosf to get theta

lposcost
    local position cosine theta,
    expected range 1->0 (as front of PMT is +Z)
    so theta_radians expected 0->pi/2

Currently called by qpmt::get_lpmtid_ATQC

**/


template<typename F>
inline QPMT_METHOD void qpmt<F>::get_lpmtid_stackspec_ce_acosf(
    F* spec
    , int lpmtid
    , F energy_eV
    , F lposcost
#if !defined(PRODUCTION) && defined(DEBUG_PIDX)
    , unsigned pidx
    , bool pidx_debug
#endif
    ) const
{
    int lpmtidx = s_pmt::lpmtidx_from_pmtid(lpmtid);  // lpmtidx:-1 FOR SPMT, BUT THAT SHOULD NEVER HAPPEN
    const int& lpmtcat = i_lcqs[lpmtidx*2+0] ;

    const F& qe_scale = lcqs[lpmtidx*2+1] ;
    const F qe_shape = qeshape_prop->interpolate( lpmtcat, energy_eV ) ;
    const F qe = qe_scale*qe_shape ;

    const F theta_radians = acosf(lposcost);
    const F ce = cetheta_prop->interpolate( lpmtcat, theta_radians );

    spec[0*4+3] = lpmtcat ;       //  3
    spec[1*4+3] = ce ;            //  7
    spec[2*4+3] = qe_shape ;      // 11
    spec[3*4+3] = qe ;            // 15

    get_lpmtcat_stackspec( spec, lpmtcat, energy_eV );


#if !defined(PRODUCTION) && defined(DEBUG_PIDX)
    if(pidx_debug) printf("//qpmt::get_lpmtid_stackspec_ce_acosf pidx %7d lpmtid %6d lpmtidx %6d lpmtcat %2d qe_scale %8.4f energy_eV %8.4f qe_shape %8.4f qe(qe_scale*qe_shape) %8.4f spec[15] %8.4f lposcost %8.4f theta_radians %8.4f ce %8.4f \n",
                                                                      pidx, lpmtid, lpmtidx, lpmtcat,  qe_scale, energy_eV, qe_shape, qe, spec[15], lposcost, theta_radians, ce );
#endif



}



/**
get_lpmtid_stackspec_ce
-------------------------

This uses cecosth_prop interpolation avoiding use of acosf
as directly interpolate in the cosine.

lposcost
    local position cosine theta,
    expected range 1->0 (as front of PMT is +Z)
    so theta_radians expected 0->pi/2

Potentially called by qpmt::get_lpmtid_ATQC

**/


template<typename F>
inline QPMT_METHOD void qpmt<F>::get_lpmtid_stackspec_ce( F* spec, int lpmtid, F energy_eV, F lposcost ) const
{
    int lpmtidx = s_pmt::lpmtidx_from_pmtid(lpmtid);
    const int& lpmtcat = i_lcqs[lpmtidx*2+0] ;
    const F& qe_scale = lcqs[lpmtidx*2+1] ;

    const F qe_shape = qeshape_prop->interpolate( lpmtcat, energy_eV ) ;
    const F qe = qe_scale*qe_shape ;

    const F ce = cecosth_prop->interpolate( lpmtcat, lposcost );

    spec[0*4+3] = lpmtcat ;       //  3
    spec[1*4+3] = ce ;            //  7
    spec[2*4+3] = qe_shape ;      // 11
    spec[3*4+3] = qe ;            // 15

    get_lpmtcat_stackspec( spec, lpmtcat, energy_eV );

    //printf("//qpmt::get_lpmtid_stackspec_ce lpmtid %d lpmtidx %d lpmtcat %d lposcost %7.3f  ce %7.3f spec[7] %7.3f \n", lpmtid, lpmtidx, lpmtcat, lposcost, ce, spec[7] );
}

// end CUDA/MOCK_CUDA
#endif


#if defined(__CUDACC__) || defined(__CUDABE__) || defined( MOCK_CURAND ) || defined(MOCK_CUDA)
#else
template struct QUDARAP_API qpmt<float>;
//template struct QUDARAP_API qpmt<double>;
#endif
