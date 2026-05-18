/**
QPMT.cu
==========

_QPMT_lpmtcat_rindex
_QPMT_lpmtcat_qeshape
_QPMT_lpmtcat_stackspec
    kernel funcs taking (qpmt,lookup,domain,domain_width) args

QPMT_pmtcat_scan
    CPU entry point to launch above kernels controlled by etype


**/

#include "QUDARAP_API_EXPORT.hh"
#include <stdio.h>
#include "qpmt_enum.h"
#include "qpmt.h"
#include "qprop.h"


/**
_QPMT_lpmtcat_rindex
---------------------------

max_iprop::

   . (ni-1)*nj*nk + (nj-1)*nk + (nk-1)
   =  ni*nj*nk - nj*nk + nj*nk - nk + nk - 1
   =  ni*nj*nk - 1


HMM: not so easy to generalize from rindex to also do qeshape
because of the different array shapes

Each thread does all pmtcat,layers and props for a single energy_eV.

**/

template <typename F>
__global__ void _QPMT_lpmtcat_rindex( int etype, qpmt<F>* pmt, F* lookup , const F* domain, unsigned domain_width )
{
    unsigned ix = blockIdx.x * blockDim.x + threadIdx.x;
    if (ix >= domain_width ) return;
    F domain_value = domain[ix] ;    // energy_eV

    //printf("//_QPMT_rindex domain_width %d ix %d domain_value %10.4f \n", domain_width, ix, domain_value );
    // wierd unsigned/int diff between qpmt.h and here ? to get it to compile for device
    // switching to enum rather than constexpr const avoids the wierdness

    const int& ni = s_pmt::NUM_CAT ;
    const int& nj = s_pmt::NUM_LAYR ;
    const int& nk = s_pmt::NUM_PROP ;

    //printf("//_QPMT_lpmtcat_rindex ni %d nj %d nk %d \n", ni, nj, nk );
    // cf the CPU equivalent NP::combined_interp_5

    for(int i=0 ; i < ni ; i++)
    for(int j=0 ; j < nj ; j++)
    for(int k=0 ; k < nk ; k++)
    {
        int iprop = i*nj*nk+j*nk+k ;            // linearized higher dimensions
        int index = iprop * domain_width + ix ; // output index into lookup

        F value = pmt->rindex_prop->interpolate(iprop, domain_value );

        //printf("//_QPMT_lpmtcat_rindex iprop %d index %d value %10.4f \n", iprop, index, value );

        lookup[index] = value ;
    }
}



template <typename F>
__global__ void _QPMT_lpmtcat_stackspec( int etype, qpmt<F>* pmt, F* lookup , const F* domain, unsigned domain_width )
{
    unsigned ix = blockIdx.x * blockDim.x + threadIdx.x;
    if (ix >= domain_width ) return;
    F domain_value = domain[ix] ;

    //printf("//_QPMT_lpmtcat_stackspec domain_width %d ix %d domain_value %10.4f \n", domain_width, ix, domain_value );

    const int& ni = s_pmt::NUM_CAT ;
    const int& nj = domain_width ;
    const int  nk = 16 ;
    const int&  j = ix ;

    F ss[nk] ;

    for(int i=0 ; i < ni ; i++)  // over pmtcat
    {
        int index = i*nj*nk + j*nk  ;
        pmt->get_lpmtcat_stackspec(ss, i, domain_value );
        for( int k=0 ; k < nk ; k++) lookup[index+k] = ss[k] ;
    }
}



template <typename F>
__global__ void _QPMT_pmtcat_launch( int etype, qpmt<F>* pmt, F* lookup , const F* domain, unsigned domain_width )
{
    unsigned ix = blockIdx.x * blockDim.x + threadIdx.x;
    if (ix >= domain_width ) return;
    F domain_value = domain[ix] ;

    //printf("//_QPMT_pmtcat_launch etype %d domain_width %d ix %d  \n", etype, domain_width, ix  );

    const int ni = ( etype == qpmt_S_QESHAPE ) ? 1 : s_pmt::NUM_CAT ;

    for(int i=0 ; i < ni ; i++)
    {
        int pmtcat = i ;
        F value = 0.f ;

        if( etype == qpmt_QESHAPE )
        {
            value = pmt->qeshape_prop->interpolate( pmtcat, domain_value );
        }
        else if( etype == qpmt_CETHETA )
        {
            //value = pmt->cetheta_prop->interpolate(lpmtcat, domain_value );
            value = pmt->get_lpmtcat_ce( pmtcat, domain_value );
        }
        else if ( etype == qpmt_CECOSTH )
        {
            value = pmt->cecosth_prop->interpolate( pmtcat, domain_value );
        }
        else if( etype == qpmt_S_QESHAPE )
        {
            value = pmt->s_qeshape_prop->interpolate( pmtcat, domain_value );
        }


        int index = i * domain_width + ix ; // output index into lookup
        lookup[index] = value ;
    }
}




/**
QPMT_pmtcat_scan
-------------------

Performs CUDA launches, invoked from QPMT.cc QPMT<T>::pmtcat_scan

**/


template <typename F> extern void QPMT_pmtcat_scan(
    dim3 numBlocks,
    dim3 threadsPerBlock,
    qpmt<F>* pmt,
    int etype,
    F* lookup,
    const F* domain,
    unsigned domain_width
)
{

    switch(etype)
    {
        case qpmt_RINDEX     : _QPMT_lpmtcat_rindex<F><<<numBlocks,threadsPerBlock>>>(    etype, pmt, lookup, domain, domain_width )   ; break ;
        case qpmt_CATSPEC    : _QPMT_lpmtcat_stackspec<F><<<numBlocks,threadsPerBlock>>>( etype, pmt, lookup, domain, domain_width )   ; break ;
        case qpmt_QESHAPE    : _QPMT_pmtcat_launch<F><<<numBlocks,threadsPerBlock>>>(    etype, pmt, lookup, domain, domain_width )   ; break ;
        case qpmt_CETHETA    : _QPMT_pmtcat_launch<F><<<numBlocks,threadsPerBlock>>>(    etype, pmt, lookup, domain, domain_width )   ; break ;
        case qpmt_CECOSTH    : _QPMT_pmtcat_launch<F><<<numBlocks,threadsPerBlock>>>(    etype, pmt, lookup, domain, domain_width )   ; break ;
        case qpmt_S_QESHAPE  : _QPMT_pmtcat_launch<F><<<numBlocks,threadsPerBlock>>>(    etype, pmt, lookup, domain, domain_width )   ; break ;
    }
}

template void QPMT_pmtcat_scan(
   dim3,
   dim3,
   qpmt<float>*,
   int etype,
   float*,
   const float* ,
   unsigned
  );



template <typename F>
__global__ void _QPMT_spmtid(
    qpmt<F>* pmt,
    int etype,
    F* lookup ,
    const int* spmtid,
    unsigned num_spmtid )
{
    unsigned ix = blockIdx.x * blockDim.x + threadIdx.x;
    if (ix >= num_spmtid ) return;
    int _spmtid = spmtid[ix];
    //printf("//_QPMT_spmtid etype %d ix %d num_spmtid %d _spmtid %d \n", etype, ix, num_spmtid, _spmtid );

    F value = 0.f ;
    if( etype == qpmt_S_QESCALE )
    {
        value = pmt->get_s_qescale_from_spmtid( _spmtid );
    }
    lookup[ix] = value ;
}




template <typename F> extern void QPMT_spmtid_scan(
    dim3 numBlocks,
    dim3 threadsPerBlock,
    qpmt<F>* pmt,
    int etype,
    F* lookup,
    const int* spmtid,
    unsigned num_spmtid
)
{
    printf("//QPMT_spmtid_scan etype %d num_spmtid %d \n", etype, num_spmtid);
    switch(etype)
    {
        case qpmt_S_QESCALE:
           _QPMT_spmtid<F><<<numBlocks,threadsPerBlock>>>(pmt, etype, lookup, spmtid, num_spmtid ) ;  break ;
    }
}

template void QPMT_spmtid_scan<float>( dim3, dim3, qpmt<float>*, int, float*, const int*, unsigned );
