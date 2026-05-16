#pragma once
/**
Frame.h : Frame as in window, not volume 
===========================================

When ctor argument pointers are not provided the ctor allocates device buffers.
Pointer arguments allow callers to provide externally managed CUDA buffers.

HMM: lots of overlap between this and SGLFW

**/

#include <vector>
#include "scuda.h"
#include "squad.h"
#include "plog/Severity.h"

#include "CSGOPTIX_API_EXPORT.hh"

struct CSGOPTIX_API Frame
{
    static const plog::Severity LEVEL ; 

    unsigned mask ; 
    int width ; 
    int height ; 
    int depth ; 
    int channels ; 

    unsigned num_pixels ; 

    std::vector<float4> isect ; 
    std::vector<uchar4> pixel ; 
#ifdef WITH_FRAME_PHOTON
    std::vector<quad4>  fphoton ; 
#endif

    template<typename T> 
    static T* DeviceAlloc(unsigned num_pixels, bool enabled ); 

    uchar4* d_pixel ; 
    float4* d_isect ; 
#ifdef WITH_FRAME_PHOTON
    quad4*  d_fphoton ; 
#else
    quad4*  d_dummy ; 
#endif

public:
    Frame(int width_, int height_, int depth_, uchar4* d_pixel_=nullptr, float4* d_isect_=nullptr, quad4* d_fphoton_=nullptr ); 
    void setExternalDevicePixels(uchar4* _d_pixel );
    void download_(bool flip_vertical); 
    void download();           // with flip_vertical:true
    void download_inverted();  // with flip_vertical:false
    void snap(const char* path);

  private:
    void flipPixelVertical();
    void writePixels(const char* path) const;
    void writeIsect(const char* dir, const char* name) const;

    float* getIntersectData() const;

#ifdef WITH_FRAME_PHOTON
    void writeFPhoton( const char* dir, const char* name) const ;
    float*         getFPhotonData() const ;
#endif

}; 
