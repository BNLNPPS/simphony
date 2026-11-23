
#include <iostream>
#include <iomanip>
#include <cstdlib>
#include <mutex>

#include "Ctx.h"

#include <optix.h>
#include <optix_stubs.h>
#include <optix_function_table_definition.h>
#include <cuda_runtime.h>

#include "CUDA_CHECK.h"
#include "OPTIX_CHECK.h"
#include "Properties.h"

#include "ssys.h"
#include "SLOG.hh"

const plog::Severity Ctx::LEVEL = SLOG::EnvLevel("Ctx", "DEBUG") ; 

namespace
{
std::once_flag optix_initialized;
}

std::string Ctx::getLog() const
{
    std::stringstream ss ; 
    ss << "[Ctx::GetLOG" << std::endl ; 
    std::lock_guard<std::mutex> lock(log_mutex);
    for(int i=0 ; i < int(loglines.size()) ; i++) ss << loglines[i] << std::endl ;
    ss << "]Ctx::GetLOG" << std::endl ; 
    std::string str = ss.str(); 
    return str ; 
}




void Ctx::log_cb( unsigned int level, const char* tag, const char* message, void* cbdata)  // static
{
    std::stringstream ss ; 
    ss << "[" << std::setw(2) << level << "][" << std::setw( 12 ) << tag << "]: " << message ;
    std::string line = ss.str() ; 
    {
        Ctx& ctx = *static_cast<Ctx*>(cbdata);
        std::lock_guard<std::mutex> lock(ctx.log_mutex);
        ctx.loglines.push_back(line);
    }
    LOG(LEVEL) << line ; 
}

Ctx::Ctx()
    :
    props(nullptr)
{
    //CUDA_CHECK(cudaSetDevice(device)); // TOO LATE TO DO THIS HERE AS GEOM ALREADY UPLOADED
    CUDA_CHECK(cudaFree( 0 ) ); 

    CUcontext cuCtx = 0;  // zero means take the current context
    std::call_once(optix_initialized, [] { OPTIX_CHECK(optixInit()); });
    OptixDeviceContextOptions options = {};
    options.logCallbackFunction       = &Ctx::log_cb;
    options.logCallbackLevel          = 4;
    options.logCallbackData           = this;
    //options.validationMode = OPTIX_DEVICE_CONTEXT_VALIDATION_MODE_ALL ; 

    OPTIX_CHECK( optixDeviceContextCreate( cuCtx, &options, &context ) );

    try
    {
        props = new Properties(context);
    }
    catch (...)
    {
        optixDeviceContextDestroy(context);
        throw;
    }
}


Ctx::~Ctx()
{
    delete props;
    optixDeviceContextDestroy(context);
}

std::string Ctx::desc() const 
{
    std::stringstream ss ; 
    ss
        << "Ctx::desc" << std::endl 
        << props->desc()
        ;

    std::string str = ss.str(); 
    return str ; 
}




