#include <cassert>
#include <csignal>
#include <sstream>

#include "scuda.h"
#include "squad.h"

#include "NP.hh"
#include "SLOG.hh"
#include "ssys.h"

#include "QTex.hh"
#include "QU.hh"
#include "QUDA_CHECK.h"
#include "QWls.hh"

#include "qwls.h"

const plog::Severity QWls::LEVEL = SLOG::EnvLevel("QWls", "DEBUG");

const QWls *QWls::INSTANCE = nullptr;
const QWls *QWls::Get()
{
    return INSTANCE;
}

/**
QWls::QWls
------------

1. Narrows ICDF from double to float if needed
2. Uploads ICDF into GPU texture
3. Creates qwls instance with device pointers and uploads it

**/

QWls::QWls(const NP *wls_icdf, const NP *mat_map, const NP *time_constants, unsigned hd_factor) :
    dsrc(wls_icdf->ebyte == 8 ? wls_icdf : nullptr),
    src(wls_icdf->ebyte == 4 ? wls_icdf : NP::MakeNarrow(dsrc)),
    tex(MakeWlsTex(src, hd_factor)),
    wls(MakeInstance(tex, mat_map, time_constants, hd_factor, time_constants->shape[0])),
    d_wls(QU::UploadArray<qwls>(wls, 1, "QWls::QWls/d_wls"))
{
    INSTANCE = this;
}

/**
QWls::MakeWlsTex
-------------------

Creates a 2D CUDA texture from the ICDF array.
Shape: (num_wls*3, 4096, 1) where 3 = HD layers per material.

**/

QTex<float> *QWls::MakeWlsTex(const NP *src, unsigned hd_factor)
{
    assert(src);
    assert(src->shape.size() == 3);

    unsigned ni = src->shape[0]; // height: num_wls * 3
    unsigned nj = src->shape[1]; // width: 4096
    unsigned nk = src->shape[2]; // 1

    assert(nk == 1);
    assert(nj == 4096);
    assert(ni % 3 == 0); // must be multiple of 3 (3 HD layers per material)
    assert(src->uifc == 'f' && src->ebyte == 4);

    unsigned ny = ni; // height
    unsigned nx = nj; // width

    bool normalizedCoords = true;
    QTex<float> *tx = new QTex<float>(nx, ny, src->cvalues<float>(), 'L', normalizedCoords, src);

    tx->setHDFactor(hd_factor);
    tx->uploadMeta();

    LOG(LEVEL) << " src " << src->desc() << " nx (width) " << nx << " ny (height) " << ny << " tx.HDFactor "
               << tx->getHDFactor();

    return tx;
}

/**
QWls::MakeInstance
---------------------

Creates the host-side qwls struct populated with device pointers.
Uploads material_map and time_constants to device memory.

**/

qwls *QWls::MakeInstance(const QTex<float> *tex, const NP *mat_map, const NP *time_constants, unsigned hd_factor,
                         unsigned num_wls)
{
    assert(mat_map);
    assert(time_constants);
    assert(mat_map->uifc == 'i' && mat_map->ebyte == 4);
    assert(time_constants->uifc == 'f' && time_constants->ebyte == 4);

    qwls *w = new qwls;
    w->wls_tex = tex->texObj;
    w->hd_factor = hd_factor;
    w->num_wls = num_wls;
    w->tex_height = tex->height;

    // Upload material_map to device
    unsigned num_mat = mat_map->shape[0];
    int *d_mat_map = nullptr;
    size_t mat_map_size = num_mat * sizeof(int);
    QUDA_CHECK(cudaMalloc(reinterpret_cast<void **>(&d_mat_map), mat_map_size));
    QUDA_CHECK(cudaMemcpy(d_mat_map, mat_map->cvalues<int>(), mat_map_size, cudaMemcpyHostToDevice));
    w->material_map = d_mat_map;

    // Upload time_constants to device
    float *d_tc = nullptr;
    size_t tc_size = num_wls * sizeof(float);
    QUDA_CHECK(cudaMalloc(reinterpret_cast<void **>(&d_tc), tc_size));
    QUDA_CHECK(cudaMemcpy(d_tc, time_constants->cvalues<float>(), tc_size, cudaMemcpyHostToDevice));
    w->time_constants = d_tc;

    return w;
}

std::string QWls::desc() const
{
    std::stringstream ss;
    ss << "QWls" << " dsrc " << (dsrc ? dsrc->desc() : "-") << " src " << (src ? src->desc() : "-") << " tex "
       << (tex ? tex->desc() : "-");
    return ss.str();
}
