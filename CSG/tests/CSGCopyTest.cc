#include "OPTICKS_LOG.hh"
#include "ssys.h"
#include "SBitSet.h"

#include "SSim.hh"
#include "CSGFoundry.h"
#include "CSGMaker.h"
#include "CSGCopy.h"

int main(int argc, char** argv)
{
    OPTICKS_LOG(argc, argv); 

    char mode = argc > 1 ? argv[1][0] : 'K' ; 

    LOG(info) << " mode [" << mode << "]" ; 

    SSim::Create(); 

    // CAUTION : MOST OF THIS IS DONE BY CSGFoundry::Load

    CSGFoundry* src = mode == 'D' ? CSGMaker::MakeDemo() : CSGFoundry::Load_();
    LOG_IF(fatal, src == nullptr) << " NO GEOMETRY ";
    if (src == nullptr)
        return 1;

    const SBitSet* elv = SBitSet::Create(src->getNumMeshName(), "ELV", "t");

    LOG(info)
        << "env->desc()"
        << std::endl
        << elv->desc()
        << std::endl
        << "src->descELV(elv)"
        << std::endl
        << src->descELV(elv)
        << std::endl;

    bool        gdml_no_selection = ssys::hasenv_("SIMPHONY_GEOM_FILE") && (elv == nullptr || elv->all());
    CSGFoundry* dst = gdml_no_selection ? src : CSGCopy::Select(src, elv);

    int cf = gdml_no_selection ? 0 : CSGFoundry::Compare(src, dst);

    if(ssys::hasenv_("AFOLD")) src->save("$AFOLD"); 
    if(ssys::hasenv_("BFOLD")) dst->save("$BFOLD"); 

    LOG(info) 
        << " src " << src 
        << " dst " << dst   
        << " cf " << cf 
        ;  

    if( elv == nullptr || elv->all() )
    {
        LOG_IF(fatal, cf != 0)
            << " UNEXPECTED DIFFERENCE "
            << " DEBUG WITH :"
            << std::endl
            << " CSGCopyTest ana ";

        assert( cf == 0 ); 
    }

    LOG(info) << " src.cfbase " << src->cfbase << " elv.spec " << elv->spec ; 
    if(src->cfbase && elv->spec)
    {
        LOG(info) << " src.cfbase " << src->cfbase << " elv.spec " << elv->spec ; 
    }

    return 0 ;  
}
