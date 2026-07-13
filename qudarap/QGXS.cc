#include <sstream>

#include "SLOG.hh"

#include "QGXS.hh"
#include "QU.hh"
#include "qgxs.h"

const plog::Severity QGXS::LEVEL = SLOG::EnvLevel("QGXS", "DEBUG");

QGXS::QGXS(float sigma_mm, float T_mm, float cap_zlo, float cap_zhi)
    :
    gxs(new qgxs),
    d_gxs(nullptr)
{
    gxs->sigma_mm = sigma_mm ;
    gxs->inv_tau  = T_mm > 0.f ? sigma_mm/T_mm : 0.f ;
    gxs->cap_zlo  = cap_zlo ;
    gxs->cap_zhi  = cap_zhi ;

    init();
}

void QGXS::init()
{
    d_gxs = QU::UploadArray<qgxs>( gxs, 1, "QGXS::init/d_gxs" );
    LOG(LEVEL) << desc() ;
}

std::string QGXS::desc() const
{
    std::stringstream ss ;
    ss << "QGXS::desc"
       << " sigma_mm " << gxs->sigma_mm
       << " inv_tau " << gxs->inv_tau
       << " cap_zlo " << gxs->cap_zlo
       << " cap_zhi " << gxs->cap_zhi
       << " d_gxs " << d_gxs
       ;
    return ss.str();
}
