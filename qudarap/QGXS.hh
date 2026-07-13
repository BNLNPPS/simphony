#pragma once
/**
QGXS.hh : host-side setup of the gamma (soft X-ray) surface reflection mode
=============================================================================

Populates and uploads the device qgxs struct consumed by qsim::propagate_gamma
(see qgxs.h). The Cu reflectivity table is compiled in (qgxs_synradg4.h), so
there is nothing material-dependent to prepare host-side: construction just
fills the surface parameters and uploads.

* sigma_mm : surface RMS roughness
* T_mm     : roughness autocorrelation length (0: no diffuse branch)
* cap_zlo/cap_zhi : unconditional-absorb end planes, mm

Hook into the already-instanciated QSim with QSim::setGXS(qgxs_->d_gxs),
which switches qsim::propagate to the reflect-or-absorb gamma mode.
**/

#include <string>
#include "QUDARAP_API_EXPORT.hh"
#include "plog/Severity.h"

struct qgxs ;

struct QUDARAP_API QGXS
{
    static const plog::Severity LEVEL ;

    qgxs* gxs ;
    qgxs* d_gxs ;

    QGXS(float sigma_mm, float T_mm, float cap_zlo, float cap_zhi);

    void init();
    std::string desc() const ;
};
