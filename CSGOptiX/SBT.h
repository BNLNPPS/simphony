#pragma once
/**
SBT : OptiX 7 RG,MS,HG program data preparation
=================================================

Aim to minimize geometry specifics in here while keeping one
active geometry path for both analytic and triangulated solids.

Analytic solids are built using custom primitive build inputs.
Triangulated solids are built using triangle build inputs.
Both end up using SOPTIX_Accel and share the same Shader Binding
Table construction flow.
**/

#include <map>
#include <string>
#include <vector>
#include <optix.h>
#include "plog/Severity.h"

#include "Binding.h"
#include "sqat4.h"

struct PIP ;
struct CSGFoundry ;
struct CSGPrim ;
struct Properties ;
struct SScene ;
struct SOPTIX_Accel ;
struct SOPTIX_BuildInput;
struct SOPTIX_MeshGroup ;
struct SCUDA_MeshGroup ;

struct SBT
{
    static const plog::Severity LEVEL ;

    static bool ValidSpec(const char* spec);
    std::vector<unsigned>  solid_selection ;
    unsigned long long emm ;
    const PIP*      pip ;
    const Properties* properties ;
    Raygen*       raygen ;
    Miss*         miss ;
    HitGroup*     hitgroup ;
    HitGroup*     check ;

    const CSGFoundry*  foundry ;
    const SScene*      scene ;

    CUdeviceptr   d_raygen ;
    CUdeviceptr   d_miss ;
    CUdeviceptr   d_hitgroup ;

    std::vector<OptixInstance> instances ;

    OptixShaderBindingTable sbt = {};

    std::map<unsigned, SOPTIX_Accel*> vgas ;
    std::map<unsigned, const SOPTIX_MeshGroup*> xgas ;
    typedef std::map<unsigned, SOPTIX_Accel*>::const_iterator IT ;

    std::vector<SOPTIX_Accel*> vias ;
    std::vector<SOPTIX_BuildInput*> vbis;

    static std::string Desc();
    SBT(const PIP* pip_ );
    ~SBT();

    void init();
    void destroy();

    void createRaygen();
    void destroyRaygen();
    void updateRaygen();

    void createMiss();
    void destroyMiss();
    void updateMiss();

    void setFoundry(const CSGFoundry* foundry);
    void createGeom();

    void createGAS();
    void createGAS(unsigned gas_idx);
    OptixTraversableHandle getGASHandle(unsigned gas_idx) const;

    void createIAS();
    void createIAS(unsigned ias_idx);
    void collectInstances( const std::vector<qat4>& ias_inst ) ;
    NP* serializeInstances() const ;
    std::string descIAS(const std::vector<qat4>& inst ) const ;
    OptixTraversableHandle getIASHandle(unsigned ias_idx) const;
    OptixTraversableHandle getTOPHandle() const ;



    int getOffset(unsigned shape_idx_ , unsigned layer_idx_ ) const ;
    int _getOffset(unsigned shape_idx_, unsigned layer_idx_) const;
    unsigned getTotalRec() const;
    std::string descGAS() const;
    void createHitgroup();

    static void UploadHitGroup(OptixShaderBindingTable& sbt, CUdeviceptr& d_hitgroup, HitGroup* hitgroup, size_t tot_rec );

    void destroyHitgroup();
    void checkHitgroup();

    void setPrimData( CustomPrim& cp, const CSGPrim* prim);
    void checkPrimData( CustomPrim& cp, const CSGPrim* prim);
    void dumpPrimData( const CustomPrim& cp ) const ;

    void setMeshData( TriMesh& tm, const SCUDA_MeshGroup* cmg, int j, int boundary, unsigned globalPrimIdx );

};
