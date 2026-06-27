#include "SLOG.hh"
#include <cstdlib>
#include <sstream>
#include <vector>

#include "QRng.hh"
#include "SEventConfig.hh"

#include "sstr.h"
#include "ssys.h"

#include "qrng.h"
#include "srng.h"
#include "QU.hh"

#include "QUDA_CHECK.h"

const plog::Severity QRng::LEVEL = SLOG::EnvLevel("QRng", "DEBUG"); 
const QRng* QRng::INSTANCE = nullptr ; 
const QRng* QRng::Get(){ return INSTANCE ;  }

namespace
{
int ParseSeedOffset(QRng::ULL& seed, QRng::ULL& offset, const char* spec_)
{
    const char*              spec = spec_ ? spec_ : "0:0";
    std::vector<std::string> elem;
    sstr::Split(spec, ':', elem);
    if (elem.size() != 2)
        return 1;
    seed = std::strtoull(elem[0].c_str(), nullptr, 10);
    offset = std::strtoull(elem[1].c_str(), nullptr, 10);
    return 0;
}
} // namespace

std::string QRng::Desc() // static
{
    std::stringstream ss ; 
    ss << "QRng::Desc"
       << " IMPL:" << IMPL 
       ;
    std::string str = ss.str() ;
    return str ;  
}


/**
QRng::QRng
------------

QRng instanciation is invoked from QSim::UploadComponents

**/

QRng::QRng(unsigned skipahead_event_offset_) :
    RNGNAME(srng<RNG>::NAME),
    skipahead_event_offset(skipahead_event_offset_),
    seed(0ull),
    offset(0ull),
    SEED_OFFSET(ssys::getenvvar("QRng__SEED_OFFSET")),
    parse_rc(ParseSeedOffset(seed, offset, SEED_OFFSET)),
    qr(new qrng<RNG>(seed, offset, skipahead_event_offset)),
    d_qr(nullptr),
    rngmax(SEventConfig::MaxCurand())
{
    init(); 
}

void QRng::init()
{
    INSTANCE = this ; 
    assert(parse_rc == 0 ); 

    initMeta(); 

    bool VERBOSE = ssys::getenvbool(init_VERBOSE); 
    LOG_IF(info, VERBOSE)
         << "[" << init_VERBOSE << "] " << ( VERBOSE ? "YES" : "NO " )
         << "\n"
         << desc()
         ;  
}





/**
QRng::initMeta
------------------

1. record device pointer qr->rng_startes

2. upload qrng.h *qr* instance within single element array, setting d_qr

**/

void QRng::initMeta()
{
    const char* label_1 = "QRng::initMeta/d_qr" ; 
    d_qr = QU::UploadArray<qrng<RNG>>(qr, 1, label_1 ); 

    bool uploaded = d_qr != nullptr ; 
    LOG_IF(fatal, !uploaded) << " FAILED to upload RNG and/or metadata " ;  
    assert(uploaded); 
}



QRng::~QRng()
{
}


std::string QRng::desc() const
{
    std::stringstream ss ; 
    ss << "QRng::desc\n"
       << std::setw(30) << " IMPL " << IMPL << "\n" 
       << std::setw(30) << " RNGNAME " << ( RNGNAME ? RNGNAME : "-" ) << "\n" 
       << std::setw(30) << " seed " << seed << "\n"
       << std::setw(30) << " offset " << offset << "\n"
       << std::setw(30) << " rngmax " << rngmax << "\n"
       << std::setw(30) << " rngmax/M " << rngmax/M << "\n"
       << std::setw(30) << " qr " << qr << "\n"
       << std::setw(30) << " qr.skipahead_event_offset " << qr->skipahead_event_offset << "\n"
       << std::setw(30) << " d_qr " << d_qr << "\n"
       ;

    std::string str = ss.str(); 
    return str ; 
}





template <typename T>
extern void QRng_generate(
    dim3, 
    dim3, 
    qrng<RNG>*, 
    unsigned, 
    T*, 
    unsigned, 
    unsigned );


/**
QRng::generate
-----------------

Launch ni threads to generate ni*nv values, via [0:nv] loop in the kernel 
with some light touch encapsulation using event_idx to automate skipahead. 

**/


template<typename T>
void QRng::generate( T* uu, unsigned ni, unsigned nv, unsigned evid )
{
    const char* label = "QRng::generate:ni*nv" ; 

    T* d_uu = QU::device_alloc<T>(ni*nv, label );

    QU::ConfigureLaunch(numBlocks, threadsPerBlock, ni, 1 );  

    QRng_generate<T>(numBlocks, threadsPerBlock, d_qr, evid, d_uu, ni, nv ); 

    QU::copy_device_to_host_and_free<T>( uu, d_uu, ni*nv, label );
}


template void QRng::generate<float>( float*,   unsigned, unsigned, unsigned ); 
template void QRng::generate<double>( double*, unsigned, unsigned, unsigned ); 
