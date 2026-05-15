#include <algorithm>
#include <string>
#include <sstream>
#include <iostream>

#include "ssys.h"
#include "sstr.h"
#include "scuda.h"
#include "saabb.h"

#include "DemoGrid.h"

#include "SLOG.hh"
#include "CSGFoundry.h"

namespace
{
    void ParseGridSpec(std::array<int,9>& grid, const char* spec)
    {
        std::vector<int> values;
        std::stringstream ss(spec ? spec : "");
        std::string item;
        while(std::getline(ss, item, ',')) sstr::split<int>(values, item.c_str(), ':');
        assert(values.size() == grid.size());
        std::copy(values.begin(), values.end(), grid.begin());
    }

    void GridMinMax(const std::array<int,9>& grid, glm::ivec3& mn, glm::ivec3& mx)
    {
        mn.x = grid[0] ; mx.x = grid[1] ;
        mn.y = grid[3] ; mx.y = grid[4] ;
        mn.z = grid[6] ; mx.z = grid[7] ;
    }
}


float4 DemoGrid::AddInstances( CSGFoundry* foundry_, unsigned ias_idx_, unsigned num_solid_ )  // static 
{
    DemoGrid gr(foundry_, ias_idx_, num_solid_ ); 
    return gr.center_extent(); 
} 

DemoGrid::DemoGrid( CSGFoundry* foundry_, unsigned ias_idx_, unsigned num_solid_ )
    :
    foundry(foundry_),
    ias_idx(ias_idx_),
    num_solid(num_solid_),
    gridscale(ssys::getenvfloat("GRIDSCALE", 1.f))
{
    std::string gridspec = ssys::getenv_<std::string>("GRIDSPEC", "-10:11,2,-10:11:2,-10:11,2") ; 
    ParseGridSpec(grid, gridspec.c_str());
    ssys::fill_evec<unsigned>(solid_modulo, "GRIDMODULO", "0,1", ',');
    ssys::fill_evec<unsigned>(solid_single, "GRIDSINGLE", "2", ',');

    LOG(info) << "GRIDSPEC " << gridspec ; 
    LOG(info) << "GRIDSCALE " << gridscale ; 
    LOG(info) << "GRIDMODULO " << ssys::desc_vec(&solid_modulo) ; 
    LOG(info) << "GRIDSINGLE " << ssys::desc_vec(&solid_single) ; 

    init();   // add qat4 instances to foundry 
}


const float4 DemoGrid::center_extent() const 
{
    glm::ivec3 imn(0,0,0); 
    glm::ivec3 imx(0,0,0); 
    GridMinMax(grid, imn, imx); 

    float3 mn = gridscale*make_float3( float(imn.x), float(imn.y), float(imn.z) ) ;
    float3 mx = gridscale*make_float3( float(imx.x), float(imx.y), float(imx.z) ) ;

    // hmm this does not accomodat the bbox of the item, just the grid centers of the items
    AABB bb = { mn, mx }; 
    float4 ce = bb.center_extent(); 

    return ce ; 
}

void DemoGrid::init()
{
    unsigned num_solid_modulo = solid_modulo.size() ; 
    unsigned num_solid_single = solid_single.size() ; 

    LOG(info) 
        << "DemoGrid::init"
        << " num_solid_modulo " << num_solid_modulo
        << " num_solid_single " << num_solid_single
        << " num_solid " << num_solid
        ;

    // check the input solid_idx are valid 
    for(unsigned i=0 ; i < num_solid_modulo ; i++ ) assert(solid_modulo[i] < num_solid) ; 
    for(unsigned i=0 ; i < num_solid_single ; i++ ) assert(solid_single[i] < num_solid) ; 

    for(int i=0 ; i < int(num_solid_single) ; i++)
    {
        unsigned ins_idx = foundry->inst.size() ; // 0-based index within the DemoGrid
        unsigned gas_idx = solid_single[i] ;      // 0-based solid index
        unsigned sensor_identifier = 0 ; 
        unsigned sensor_index = 0 ; 

        qat4 instance  ; 
        instance.setIdentity( ins_idx, gas_idx, sensor_identifier, sensor_index  ); 

        foundry->inst.push_back( instance ); 
    }

    for(int i=grid[0] ; i < grid[1] ; i+=grid[2] ){
    for(int j=grid[3] ; j < grid[4] ; j+=grid[5] ){
    for(int k=grid[6] ; k < grid[7] ; k+=grid[8] ){

        qat4 instance  ; 
        instance.q3.f.x = float(i)*gridscale ; 
        instance.q3.f.y = float(j)*gridscale ; 
        instance.q3.f.z = float(k)*gridscale ; 
       
        unsigned ins_idx = foundry->inst.size() ;     
        unsigned solid_modulo_idx = ins_idx % num_solid_modulo ; 
        unsigned gas_idx = solid_modulo[solid_modulo_idx] ; 
        unsigned sensor_identifier = 0 ; 
        unsigned sensor_index = 0 ; 

        instance.setIdentity( ins_idx, gas_idx, sensor_identifier, sensor_index ); 
        foundry->inst.push_back( instance ); 
    }
    }
    }
}
