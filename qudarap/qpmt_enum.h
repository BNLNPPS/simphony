#pragma once
/**
qpmt_enum.h
=============


**/

enum {
  qpmt_RINDEX,
  qpmt_KINDEX,
  qpmt_QESHAPE,
  qpmt_CETHETA,
  qpmt_CECOSTH,
  qpmt_CATSPEC,
  qpmt_S_QESHAPE,
  qpmt_S_QESCALE
};


#if defined(__CUDACC__) || defined(__CUDABE__)
#else
#include "QUDARAP_API_EXPORT.hh"
struct qpmt_enum
{
    static constexpr const char* _qpmt_RINDEX  = "qpmt_RINDEX" ;
    static constexpr const char* _qpmt_KINDEX  = "qpmt_KINDEX" ;
    static constexpr const char* _qpmt_QESHAPE = "qpmt_QESHAPE" ;
    static constexpr const char* _qpmt_CETHETA = "qpmt_CETHETA" ;
    static constexpr const char* _qpmt_CECOSTH = "qpmt_CECOSTH" ;
    static constexpr const char* _qpmt_CATSPEC = "qpmt_CATSPEC" ;
    static constexpr const char* _qpmt_S_QESHAPE = "qpmt_S_QESHAPE" ;
    static constexpr const char* _qpmt_S_QESCALE = "qpmt_S_QESCALE" ;

    static const char* Label( int e );
};

inline const char* qpmt_enum::Label(int e)
{
    const char* s = nullptr ;
    switch(e)
    {
        case qpmt_RINDEX:  s = _qpmt_RINDEX  ; break ;
        case qpmt_KINDEX:  s = _qpmt_KINDEX  ; break ;
        case qpmt_QESHAPE: s = _qpmt_QESHAPE ; break ;
        case qpmt_CETHETA: s = _qpmt_CETHETA ; break ;
        case qpmt_CECOSTH: s = _qpmt_CECOSTH ; break ;
        case qpmt_CATSPEC: s = _qpmt_CATSPEC ; break ;
        case qpmt_S_QESHAPE: s = _qpmt_S_QESHAPE ; break ;
        case qpmt_S_QESCALE: s = _qpmt_S_QESCALE ; break ;
    }
    return s ;
}
#endif

