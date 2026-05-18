#pragma once

/**
QRng
====

Canonical instanciation within QSim::UploadComponents

Small *skipahead_event_offsets* are for functionality testing, 
typically the offset should be greater than the maximum number of 
randoms to simulate an item(photon). 



TODO : implement sanity check for use after loading::

    bool QRng::IsAllZero( RNG* states, unsigned num_states ) //  static

**/

#include <string>
#include "QUDARAP_API_EXPORT.hh"
#include "plog/Severity.h"
#include "curand_kernel.h"   
#include "qrng.h"
#include "SCurandState.h"



struct QUDARAP_API QRng 
{
    typedef unsigned long long ULL ; 
    static constexpr const char* init_VERBOSE = "QRng__init_VERBOSE" ; 
    static constexpr const ULL M = 1000000 ;  
    static const plog::Severity LEVEL ; 
    static const QRng* INSTANCE ; 
    static const QRng* Get(); 
    static std::string Desc();

    static constexpr const char* IMPL = "CHUNKED_CURANDSTATE" ; 
    static XORWOW* LoadAndUpload(ULL rngmax, const SCurandState& cs); 
    static void Save( XORWOW* states, unsigned num_states, const char* path ); 


    const char* RNGNAME ; 
    bool  UPLOAD_RNG_STATES ; 
    ULL            skipahead_event_offset ; 
    ULL            seed ;
    ULL            offset ; 
    const char*    SEED_OFFSET ;
    int            parse_rc ; 
    qrng<RNG>*     qr ;  
    qrng<RNG>*     d_qr ;  
    ULL            rngmax ;
    SCurandState   cs ; 


    QRng(unsigned skipahead_event_offset=1) ;  
    void init(); 

    template<typename R> void initStates();

    void initMeta(); 

    virtual ~QRng(); 

    std::string desc() const ; 


    template <typename T> void generate( T* u, unsigned ni, unsigned nv, unsigned evid ) ; 

    dim3 numBlocks ; 
    dim3 threadsPerBlock ; 

};
