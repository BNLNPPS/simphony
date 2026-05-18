#pragma once
/**
QPMT_MOCK.h
=============

CPU mockup impl following GPU code : QPMT.cu 

* HMM : this code is very similar to QPMT.cu 

Q: How to rearrange such code for single source ?
A: Presumably use a header with such an impl from the QPMT.cu

   * but as this is used just for testing here.. its not so compelling 
     to be worthy of the effort 

Notice the game for maximum reuse

1. minimize un-sharable .cc and .cu, instead put almost everything 
   into small headers  

**/


#include "qpmt.h"

template <typename F>
void QPMT_lpmtcat_MOCK(
    qpmt<F>* pmt,
    int etype, 
    F* lookup,
    const F* domain,
    unsigned domain_width )
{

    if( etype == qpmt_RINDEX )
    {
        for(unsigned ix=0 ; ix < domain_width ; ix++)
        {  
            F energy_eV = domain[ix] ; 

            const int& ni = qpmt_NUM_CAT ; 
            const int& nj = qpmt_NUM_LAYR ; 
            const int& nk = qpmt_NUM_PROP ; 

            for(int i=0 ; i < ni ; i++)
            for(int j=0 ; j < nj ; j++)
            for(int k=0 ; k < nk ; k++) 
            {
                int iprop = i*nj*nk+j*nk+k ;            // linearized higher dimensions 
                int index = iprop * domain_width + ix ; // output index into lookup 

                F value = pmt->rindex_prop->interpolate(iprop, energy_eV ); 
                lookup[index] = value ; 
            }
        }
    }
    else if( etype == qpmt_QESHAPE )
    {
        for(unsigned ix=0 ; ix < domain_width ; ix++)
        {  
            F energy_eV = domain[ix] ; 
            const int& ni = qpmt_NUM_CAT ; 

            for(int i=0 ; i < ni ; i++)
            {
                int index = i * domain_width + ix ; // output index into lookup 
                F value = pmt->qeshape_prop->interpolate(i, energy_eV ); 
                lookup[index] = value ; 
            }
        }
    }
    else if( etype == qpmt_CATSPEC )
    {
        for(unsigned ix=0 ; ix < domain_width ; ix++)
        {  
            F energy_eV = domain[ix] ; 

            const int& ni = qpmt_NUM_CAT ; 
            const int& nj = domain_width ; 
            const int  nk = 16 ; 
            const int&  j = ix ; 

            F ss[nk] ;
         
            for(int i=0 ; i < ni ; i++)  // over pmtcat 
            {
                int index = i*nj*nk + j*nk  ; 
                pmt->get_lpmtcat_stackspec(ss, i, energy_eV ); 
                for( int k=0 ; k < nk ; k++) lookup[index+k] = ss[k] ;  
            }
        }
    }
}
