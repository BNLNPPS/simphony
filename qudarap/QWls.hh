#pragma once

#include "QUDARAP_API_EXPORT.hh"
#include "plog/Severity.h"
#include <string>

struct NP;
template <typename T> struct QTex;
struct qwls;

/**
QWls : Host-side WLS ICDF Texture Upload
============================================

Uploads the WLS inverse CDF array into a GPU texture and creates
the device-side qwls struct with material mapping and time constants.

Follows the same pattern as QScint for scintillation ICDF textures.

**/

struct QUDARAP_API QWls
{
    static const plog::Severity LEVEL;
    static const QWls *INSTANCE;
    static const QWls *Get();

    static QTex<float> *MakeWlsTex(const NP *src, unsigned hd_factor);
    static qwls *MakeInstance(const QTex<float> *tex, const NP *mat_map, const NP *time_constants, unsigned hd_factor,
                              unsigned num_wls);

    const NP *dsrc;   // original double-precision ICDF
    const NP *src;    // narrowed float ICDF
    QTex<float> *tex; // GPU texture
    qwls *wls;        // host-side instance (with device pointers)
    qwls *d_wls;      // device copy of qwls struct

    QWls(const NP *wls_icdf, const NP *mat_map, const NP *time_constants, unsigned hd_factor);

    std::string desc() const;
};
