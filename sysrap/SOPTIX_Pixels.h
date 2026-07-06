#pragma once

#include <cstring>
#include <vector>

#include "NP.hh"

struct SOPTIX_Pixels
{
    const SGLM& gm ;
    size_t num_pixel ; 
    size_t num_bytes ; 
    uchar4* d_pixels ;
    uchar4*     pixels;

    SOPTIX_Pixels(const SGLM& gm);
    void init();

    void download();
    void save_npy(const char* path);
};

inline SOPTIX_Pixels::SOPTIX_Pixels(const SGLM& _gm )
    :
    gm(_gm),
    num_pixel(gm.Width_Height()),
    num_bytes(num_pixel*sizeof(uchar4)),
    d_pixels(nullptr),
    pixels(new uchar4[num_pixel]) 
{
    init(); 
} 

inline void SOPTIX_Pixels::init()
{
    CUDA_CHECK( cudaMalloc(reinterpret_cast<void**>( &d_pixels ), num_bytes )); 
} 
inline void SOPTIX_Pixels::download() 
{
    CUDA_CHECK( cudaMemcpy( pixels, reinterpret_cast<void*>(d_pixels), num_bytes, cudaMemcpyDeviceToHost ));
}
inline void SOPTIX_Pixels::save_npy(const char* path)
{
    const int                  width = gm.Width();
    const int                  height = gm.Height();
    const int                  ncomp = 4;
    const size_t               row_bytes = size_t(width) * size_t(ncomp);
    const unsigned char*       src = reinterpret_cast<const unsigned char*>(pixels);
    std::vector<unsigned char> data(num_bytes);

    for (int h = 0; h < height; h++)
    {
        int y = height - 1 - h;
        std::memcpy(data.data() + size_t(y) * row_bytes, src + size_t(h) * row_bytes, row_bytes);
    }

    NP::Write(path, data.data(), height, width, ncomp);
}
