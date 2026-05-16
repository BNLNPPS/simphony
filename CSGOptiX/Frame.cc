#include <iostream>
#include <iomanip>
#include <csignal>

#include <cuda_runtime.h>
#include "QU.hh"

#include "SFrameConfig.hh"
#include "SComp.h"
#include "SPath.hh"
#include "SLOG.hh"
#include "NP.hh"
#include "Frame.h"


const plog::Severity Frame::LEVEL = SLOG::EnvLevel("Frame", "DEBUG" ); 

/**
Frame::Frame
--------------

Instanciated by:

1. CSGOptiX::CSGOptiX with null device pointer args
2. callers that provide externally managed CUDA buffers

Accepting device buffer pointer arguments allows the frame to write into
external render targets.

HMM: could use QEvt to hold the pixel, isect, photon ?
**/


template<typename T> 
T* Frame::DeviceAlloc(unsigned num_pixels, bool enabled)
{
    return enabled ? QU::device_alloc<T>(num_pixels,"Frame::DeviceAllo:num_pixels") : nullptr ; 
}

template uchar4* Frame::DeviceAlloc<uchar4>(unsigned num_pixels, bool enabled); 
template float4* Frame::DeviceAlloc<float4>(unsigned num_pixels, bool enabled); 
template quad4*  Frame::DeviceAlloc<quad4>( unsigned num_pixels, bool enabled); 


Frame::Frame(int width_, int height_, int depth_, uchar4* d_pixel_, float4* d_isect_, quad4* d_fphoton_ )
    :
    mask(SFrameConfig::FrameMask()),
    width(width_),
    height(height_),
    depth(depth_),
    channels(4),
    num_pixels(width*height),  
    d_pixel(d_pixel_ == nullptr     ? DeviceAlloc<uchar4>(num_pixels, mask & SCOMP_PIXEL   ) : d_pixel_  ),
    d_isect(d_isect_ == nullptr     ? DeviceAlloc<float4>(num_pixels, mask & SCOMP_ISECT   ) : d_isect_  ),
#ifdef WITH_FRAME_PHOTON
    d_fphoton(d_fphoton_ == nullptr ? DeviceAlloc<quad4>( num_pixels, mask & SCOMP_FPHOTON ) : d_fphoton_)
#else
    d_dummy(nullptr)  
#endif
{
    assert( depth == 1 && num_pixels > 0 ); 
}


/**
Frame::setExternalDevicePixels
-------------------------------

Does nothing when there is no change in the pointer 

**/

void Frame::setExternalDevicePixels(uchar4* _d_pixel )
{
    if( _d_pixel == d_pixel ) return ; 

    if( d_pixel ) QU::device_free<uchar4>(d_pixel ); 
    d_pixel = _d_pixel ; 
}



/**
Frame::download from GPU buffers into vectors
-----------------------------------------------

This is invoked from CSGOptiX::snap
The vectors are resized to num_pixels before the copy. 

**/
void Frame::download_(bool flip_vertical)
{
    if(d_pixel)   QU::Download<uchar4>(pixel, d_pixel, num_pixels ); 
    if(d_isect)   QU::Download<float4>(isect, d_isect, num_pixels ); 
#ifdef WITH_FRAME_PHOTON
    if(d_fphoton) QU::Download<quad4>(photon, d_fphoton, num_pixels ); 
#endif

    if (d_pixel && flip_vertical)
        flipPixelVertical();
}

void Frame::download()
{
    download_(true); 
}
void Frame::download_inverted()
{
    download_(false); 
}

float*         Frame::getIntersectData() const { return d_isect ? (float*)isect.data()         : nullptr ; }
#ifdef WITH_FRAME_PHOTON
float*         Frame::getFPhotonData() const {   return d_fphoton ? (float*)fphoton.data()     : nullptr ; }
#endif

void Frame::flipPixelVertical()
{
    for (int y = 0; y < height / 2; y++)
        for (int x = 0; x < width; x++)
        {
            std::swap(pixel[y * width + x], pixel[(height - 1 - y) * width + x]);
        }
}

void Frame::writePixels(const char* path) const
{
    if (d_pixel)
        NP::Write(path, (unsigned char*)pixel.data(), height, width, channels);
}


void Frame::writeIsect( const char* dir, const char* name) const 
{
    float* isd = getIntersectData() ;
    if(isd) NP::Write(dir, name, isd, height, width, 4 );
}

#ifdef WITH_FRAME_PHOTON
void Frame::writeFPhoton( const char* dir, const char* name) const 
{
    float* fpd = getFPhotonData() ;  
    if(fpd) NP::Write(dir, name, fpd, height, width, 4, 4 );
}
#endif

void Frame::snap( const char* path )
{
    LOG(LEVEL) << "[";

    LOG(LEVEL) << "[ writePixels ";
    writePixels(path);
    LOG(LEVEL) << "] writePixels ";

    LOG(LEVEL) << "[ writeIntersectData " ; 
    const char* fold = SPath::Dirname(path); 
    float* isd = getIntersectData() ;
    if(isd) NP::Write(fold, "isect.npy", isd, height, width, 4 );
    LOG(LEVEL) << "] writeIntersectData " ; 


#ifdef WITH_FRAME_PHOTON
    LOG(LEVEL) << "[ writeFPhoton " ; 
    float* fpd = getFPhotonData() ;  
    if(fpd) NP::Write(fold, "fphoton.npy", fpd, height, width, 4, 4 );
    LOG(LEVEL) << "] writeFPhoton " ; 
#endif

    LOG(LEVEL) << "]" ; 
}
