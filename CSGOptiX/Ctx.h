#pragma once
/**
Ctx : instanciation creates OptiX optixDeviceContext and populates Properties
==================================================================================

Instanciated by CSGOptiX::initCtx dump properties using: CSGOptiX=INFO


**/

#include <optix.h>
#include <mutex>
#include <vector>
#include <string>
#include "plog/Severity.h"

struct Properties ; 

struct Ctx 
{
    static const plog::Severity LEVEL ; 
    std::string getLog() const;
    std::vector<std::string> loglines;
    mutable std::mutex log_mutex;

    OptixDeviceContext context = nullptr;
    static void log_cb( unsigned int level, const char* tag, const char* message, void* cbdata);

   
    Properties* props  ; 

    Ctx();
    ~Ctx();

    std::string desc() const ; 

};

