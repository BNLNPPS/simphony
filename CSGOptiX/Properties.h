#pragma once

/**
Properties
============

OptiX 7.0::

    Properties::desc
                          limitMaxTraceDepth :         31
               limitMaxTraversableGraphDepth :         16
                    limitMaxPrimitivesPerGas :  536870912  20000000
                     limitMaxInstancesPerIas :   16777216   1000000
                               rtcoreVersion :          0
                          limitMaxInstanceId :   16777215    ffffff
          limitNumBitsInstanceVisibilityMask :          8
                    limitMaxSbtRecordsPerGas :   16777216   1000000
                           limitMaxSbtOffset :   16777215    ffffff
**/

#include <string>
#include <optix.h>

struct Properties 
{
    unsigned rtcoreVersion ; 
    unsigned limitMaxTraceDepth ; 
    unsigned limitMaxTraversableGraphDepth ;
    unsigned limitMaxPrimitivesPerGas ;
    unsigned limitMaxInstancesPerIas ;
    unsigned limitMaxInstanceId ;
    unsigned limitNumBitsInstanceVisibilityMask ;
    unsigned limitMaxSbtRecordsPerGas ;
    unsigned limitMaxSbtOffset ;   

    OptixDeviceContext context;
    explicit Properties(OptixDeviceContext context);

    unsigned visibilityMask_FULL() const ;
    unsigned visibilityMask(unsigned idx) const ;

    std::string desc() const ; 
};
