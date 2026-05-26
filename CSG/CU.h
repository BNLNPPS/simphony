#pragma once
/**
CU.h : UploadArray/DownloadArray/UploadVec/DownloadVec 
========================================================

* used for CSGFoundry upload 

::

    epsilon:CSG blyth$ opticks-fl CU.h
    ./CSGOptiX/SBT.cc
    ./CSG/CMakeLists.txt
    ./CSG/CU.h
    ./CSG/tests/CSGPrimImpTest.cc
    ./CSG/tests/CUTest.cc
    ./CSG/CU.cc
    ./CSG/CSGFoundry.cc

**/
#include "plog/Severity.h"

struct dim3 ;
#include <vector>
#include "CSG_API_EXPORT.hh"

struct CSG_API CU
{
    static const plog::Severity LEVEL;

    template <typename T>
    static T* AllocArray(unsigned num_items ) ; 

    template <typename T>
    static T* UploadArray(const T* array, unsigned num_items ) ; 

    template <typename T>
    static T* DownloadArray(const T* array, unsigned num_items ) ; 


    template <typename T>
    static T* UploadVec(const std::vector<T>& vec);

    template <typename T>
    static void DownloadVec(std::vector<T>& vec, const T* d_array, unsigned num_items);

    static void ConfigureLaunch1D( dim3& numBlocks, dim3& threadsPerBlock, unsigned num, unsigned threads_per_block ); 

};
