#pragma once
/**
U4Surface.h
==============

HMM distinction between border and skin can just be 
carried via the directory path and metadata ? 

HMM: maybe need to enhance NPFold.h metadata or could 
use a small array and plant metadata on that 

**/

#include "G4String.hh"
#include "G4OpticalSurface.hh"
#include "G4MaterialPropertiesTable.hh"
#include "G4LogicalBorderSurface.hh"
#include "G4LogicalSkinSurface.hh"
#include "G4Version.hh"
#include "G4Material.hh"

#include "U4SurfaceType.h"
#include "U4OpticalSurfaceFinish.h"
#include "U4OpticalSurfaceModel.h"

#include "U4SurfacePerfect.h"
#include "S4.h"

#include "sdomain.h"
#include "sproplist.h"

#if G4VERSION_NUMBER >= 1070
#include "SNameOrder.h"
#endif



struct NPFold ; 


enum {
   U4Surface_UNSET, 
   U4Surface_PerfectAbsorber,
   U4Surface_PerfectDetector
};

struct U4Surface
{
    static constexpr const char* PerfectAbsorber = "PerfectAbsorber" ;
    static constexpr const char* PerfectDetector = "PerfectDetector" ;
    static unsigned Type(const char* type_); 

    static G4OpticalSurface* MakeOpticalSurface( const char* name_ ); 

    static G4LogicalBorderSurface* MakeBorderSurface(const char* name_, const char* type_, const char* pv1_, const char* pv2_, const G4VPhysicalVolume* start_pv ); 
    static G4LogicalBorderSurface* MakePerfectAbsorberBorderSurface(const char* name_, const char* pv1, const char* pv2, const G4VPhysicalVolume* start_pv  ); 
    static G4LogicalBorderSurface* MakePerfectDetectorBorderSurface(const char* name_, const char* pv1, const char* pv2, const G4VPhysicalVolume* start_pv  ); 

    static G4LogicalBorderSurface* MakeBorderSurface(const char* name_, const char* type_, const G4VPhysicalVolume* pv1, const G4VPhysicalVolume* pv2 ); 
    static G4LogicalBorderSurface* MakePerfectAbsorberBorderSurface(const char* name_, const G4VPhysicalVolume* pv1, const G4VPhysicalVolume* pv2 ); 
    static G4LogicalBorderSurface* MakePerfectDetectorBorderSurface(const char* name_, const G4VPhysicalVolume* pv1, const G4VPhysicalVolume* pv2 ); 


    static const std::vector<G4LogicalBorderSurface*>* PrepareBorderSurfaceVector(const G4LogicalBorderSurfaceTable* tab ); 
    static const std::vector<G4LogicalSkinSurface*>*   PrepareSkinSurfaceVector(const G4LogicalSkinSurfaceTable* tab ); 

    static void    Collect( std::vector<const G4LogicalSurface*>& surfaces ); 
    static void    CollectRawNames( std::vector<std::string>& rawnames, const std::vector<const G4LogicalSurface*>& surfaces ); 

    static constexpr const char* PERMIT_UNSUPPORTED = "U4Surface__PERMIT_UNSUPPORTED";
    static double Prop(const G4MaterialPropertiesTable* mpt, const char* key, bool lo = false);
    static void Check(const std::vector<const G4LogicalSurface*>& surfaces);

    static NPFold* MakeFold(const std::vector<const G4LogicalSurface*>& surfaces, const std::vector<std::string>& keys ); 
    static NPFold* MakeFold(); 
    static G4LogicalSurface* Find( const G4VPhysicalVolume* thePrePV, const G4VPhysicalVolume* thePostPV ) ;  

};

#include "ssys.h"
#include <algorithm>
#include <set>
#include <stdexcept>

#include "U4Material.hh"
#include "U4MaterialPropertiesTable.h"
#include "U4Volume.h"
#include "NPFold.h"


inline unsigned U4Surface::Type(const char* type_)
{
    unsigned type = U4Surface_UNSET ; 
    if(strcmp(type_, PerfectAbsorber) == 0) type = U4Surface_PerfectAbsorber ; 
    if(strcmp(type_, PerfectDetector) == 0) type = U4Surface_PerfectDetector ; 
    return type ; 
}



inline G4OpticalSurface* U4Surface::MakeOpticalSurface( const char* name_ )
{
    G4String name = name_ ; 
    G4OpticalSurfaceModel model = glisur ; 
    G4OpticalSurfaceFinish finish = polished ; 
    G4SurfaceType type = dielectric_dielectric ; 
    G4double value = 1.0 ; 
    G4OpticalSurface* os = new G4OpticalSurface(name, model, finish, type, value );  
    return os ; 
}

/**
U4Surface::MakeBorderSurface
--------------------------------------

The optical boundary process expects a RINDEX property even though that is not
going to be used for anything. Also it needs REFLECTIVITY of zero.

Getting G4OpBoundaryProcess to always give boundary status Detection for a surface requires:

1. REFLECTIVITY 0. forcing DoAbsoption 
2. EFFICIENCY 1. forcing Detection 

**/


inline G4LogicalBorderSurface* U4Surface::MakeBorderSurface(const char* name_, const char* type_, const char* pv1_, const char* pv2_, const G4VPhysicalVolume* start_pv )
{
    const G4VPhysicalVolume* pv1 = U4Volume::FindPV( start_pv, pv1_ ); 
    const G4VPhysicalVolume* pv2 = U4Volume::FindPV( start_pv, pv2_ ); 
    return ( pv1 && pv2 ) ? MakeBorderSurface(name_, type_, pv1, pv2 ) : nullptr ;  
}

inline G4LogicalBorderSurface* U4Surface::MakePerfectAbsorberBorderSurface(const char* name_, const char* pv1, const char* pv2, const G4VPhysicalVolume* start_pv)
{
    return MakeBorderSurface(name_, PerfectAbsorber, pv1, pv2, start_pv ); 
}
inline G4LogicalBorderSurface* U4Surface::MakePerfectDetectorBorderSurface(const char* name_, const char* pv1, const char* pv2, const G4VPhysicalVolume* start_pv)
{
    return MakeBorderSurface(name_, PerfectDetector, pv1, pv2, start_pv ); 
}




inline G4LogicalBorderSurface* U4Surface::MakeBorderSurface(const char* name_, const char* type_, const G4VPhysicalVolume* pv1, const G4VPhysicalVolume* pv2)
{
    unsigned type = Type(type_); 

    G4OpticalSurface* os = MakeOpticalSurface( name_ );  
    G4MaterialPropertiesTable* mpt = new G4MaterialPropertiesTable ; 
    os->SetMaterialPropertiesTable(mpt);  

    G4MaterialPropertyVector* rindex = U4Material::MakeProperty(1.);  
    mpt->AddProperty("RINDEX", rindex );  

    G4MaterialPropertyVector* reflectivity = U4Material::MakeProperty(0.);  
    mpt->AddProperty("REFLECTIVITY",reflectivity );  


    if( type == U4Surface_PerfectAbsorber )
    {  
    }
    else if(  type == U4Surface_PerfectDetector )
    {
        G4MaterialPropertyVector* efficiency = U4Material::MakeProperty(1.);  
        mpt->AddProperty("EFFICIENCY",efficiency );  
    }

    G4String name = name_ ; 

    G4VPhysicalVolume* pv1_ = const_cast<G4VPhysicalVolume*>(pv1); 
    G4VPhysicalVolume* pv2_ = const_cast<G4VPhysicalVolume*>(pv2); 
    G4LogicalBorderSurface* bs = new G4LogicalBorderSurface(name, pv1_, pv2_, os ); 
    return bs ; 
}

inline G4LogicalBorderSurface* U4Surface::MakePerfectAbsorberBorderSurface(const char* name_, const G4VPhysicalVolume* pv1, const G4VPhysicalVolume* pv2)
{
    return MakeBorderSurface(name_, PerfectAbsorber, pv1, pv2 ); 
}
inline G4LogicalBorderSurface* U4Surface::MakePerfectDetectorBorderSurface(const char* name_, const G4VPhysicalVolume* pv1, const G4VPhysicalVolume* pv2)
{
    return MakeBorderSurface(name_, PerfectDetector, pv1, pv2 ); 
}


/**
U4Surface::PrepareBorderSurfaceVector
---------------------------------------

Prior to Geant4 1070 G4LogicalBorderSurfaceTable was simply typedef to 
std::vector<G4LogicalBorderSurface*> (g4-cls G4LogicalBorderSurface)
for 1070 and above the table type changed to become a std::map with  
pair of pointers key : std::pair<const G4VPhysicalVolume*, const G4VPhysicalVolume*>.

As the std::map iteration order with such a key could potentially change from 
invokation to invokation or between platforms depending on where the pointer 
addresses got allocated it is necessary to impose a more meaningful 
and consistent order. 

As Opticks serializes all geometry objects into arrays for upload 
to GPU buffers and textures and uses indices to reference into these 
buffers and textures it is necessary for all collections of geometry objects 
to have well defined and consistent ordering.
To guarantee this the std::vector obtained from the std::map is sorted based on 
the 0x stripped name of the G4LogicalBorderSurface.


Note that prior to 1070 the table was a vector, and this preparation
does nothing so the order of the border surfaces is just the creation order. 
For consistency of the order between Geant4 versions the surfaces could be 
name sorted, but this is not yet done as there has been no need for consistent
surface indices between Geant4 versions.  

**/

inline const std::vector<G4LogicalBorderSurface*>* U4Surface::PrepareBorderSurfaceVector(const G4LogicalBorderSurfaceTable* tab )  // static
{
    typedef std::vector<G4LogicalBorderSurface*> VBS ; 
#if G4VERSION_NUMBER >= 1070
    typedef std::pair<const G4VPhysicalVolume*, const G4VPhysicalVolume*> PPV ; 
    typedef std::map<PPV, G4LogicalBorderSurface*>::const_iterator IT ; 

    VBS* vec = new VBS ;   
    for(IT it=tab->begin() ; it != tab->end() ; it++ )
    {   
        G4LogicalBorderSurface* bs = it->second ;    
        vec->push_back(bs);    
        const PPV ppv = it->first ; 
        assert( ppv.first == bs->GetVolume1());  
        assert( ppv.second == bs->GetVolume2());  
    }   

    {   
        bool reverse = false ; 
        const char* tail = "0x" ; 
        SNameOrder<G4LogicalBorderSurface>::Sort( *vec, reverse, tail ); 
        std::cout << "U4Surface::PrepareBorderSurfaceVector\n" << SNameOrder<G4LogicalBorderSurface>::Desc( *vec ) << std::endl ; 
    }   

#else
    const VBS* vec = tab ;   
#endif
    return vec ; 
}

/**
U4Surface::PrepareSkinSurfaceVector
------------------------------------

TODO : update the G4VERSION_NUMBER branch guess ">= 1122" to the appropriate one at which the skin surface table 
was changed from a vector to a map

**/


inline const std::vector<G4LogicalSkinSurface*>* U4Surface::PrepareSkinSurfaceVector(const G4LogicalSkinSurfaceTable* tab )  // static
{
    typedef std::vector<G4LogicalSkinSurface*> VKS ; 
#if G4VERSION_NUMBER >= 1122
    typedef std::map<const G4LogicalVolume*,G4LogicalSkinSurface*>::const_iterator IT ; 
    VKS* vec = new VKS ;    // not const as need to push_back

    for(IT it=tab->begin() ; it != tab->end() ; it++ )
    {   
        G4LogicalSkinSurface* ks = it->second ;    
        vec->push_back(ks);    
    }   

    {   
        bool reverse = false ; 
        const char* tail = "0x" ; 
        SNameOrder<G4LogicalSkinSurface>::Sort( *vec, reverse, tail ); 
        std::cout << "U4Surface::PrepareSkinSurfaceVector\n" << SNameOrder<G4LogicalSkinSurface>::Desc( *vec ) << std::endl ; 
    }   

#else
    const VKS* vec = tab ;   
#endif
    return vec ; 
}


/**
U4Surface::Collect 
---------------------

Collects G4LogicalBorderSurface and G4LogicalSkinSurface pointers
into vector of G4LogicalSurface. 

**/


inline void U4Surface::Collect( std::vector<const G4LogicalSurface*>& surfaces )
{
    const G4LogicalBorderSurfaceTable* border_ = G4LogicalBorderSurface::GetSurfaceTable() ;
    const std::vector<G4LogicalBorderSurface*>* border = PrepareBorderSurfaceVector(border_); 

    for(unsigned i=0 ; i < border->size() ; i++)
    {   
        G4LogicalBorderSurface* bs = (*border)[i] ; 
        surfaces.push_back(bs) ;  
    }   

    const G4LogicalSkinSurfaceTable* skin_ = G4LogicalSkinSurface::GetSurfaceTable() ; 
    const std::vector<G4LogicalSkinSurface*>* skin = PrepareSkinSurfaceVector(skin_); 
    for(unsigned i=0 ; i < skin->size() ; i++)
    {   
        G4LogicalSkinSurface* ks = (*skin)[i] ; 
        surfaces.push_back(ks) ;  
    }
}

inline void U4Surface::CollectRawNames( std::vector<std::string>& rawnames, const std::vector<const G4LogicalSurface*>& surfaces )
{
    for(unsigned i=0 ; i < surfaces.size() ; i++)
    {   
        const G4LogicalSurface* ls = surfaces[i] ; 
        const G4String& name = ls->GetName() ; 
        const char* raw = name.c_str() ; 
        rawnames.push_back(raw); 
    }
}

inline double U4Surface::Prop(const G4MaterialPropertiesTable* mpt, const char* key, bool lo)
{
    const G4MaterialPropertyVector* v = mpt->GetProperty(key);
    if (v == nullptr || v->GetVectorLength() < 2)
        return 0.;
    double p = (*v)[0];
    for (std::size_t i = 1; i < v->GetVectorLength(); i++)
        p = lo ? std::min(p, (*v)[i]) : std::max(p, (*v)[i]);
    return p;
}

inline void U4Surface::Check(const std::vector<const G4LogicalSurface*>& surfaces)
{
    std::set<const G4OpticalSurface*> seen;
    std::stringstream                 bad;
    std::stringstream                 approx;

    for (unsigned i = 0; i < surfaces.size(); i++)
    {
        const G4OpticalSurface* os = dynamic_cast<const G4OpticalSurface*>(surfaces[i]->GetSurfaceProperty());
        if (os == nullptr)
            throw std::runtime_error("U4Surface::Check : " + surfaces[i]->GetName() + " has no G4OpticalSurface, nothing to translate");
        if (!seen.insert(os).second)
            continue;

        const char*    sn = os->GetName().c_str();
        const unsigned type = os->GetType();
        const unsigned model = os->GetModel();
        const unsigned finish = os->GetFinish();
        const bool     metal = type == dielectric_metal;
        const bool     dd = type == dielectric_dielectric;
        const bool     basic = model == glisur || model == unified;

        if (!metal && !dd)
            bad << sn << " : type " << U4SurfaceType::Name(type) << " has no GPU boundary model\n";

        if (!basic)
            bad << sn << " : model " << U4OpticalSurfaceModel::Name(model) << " has no GPU implementation\n";

        if (finish > groundbackpainted)
            bad << sn << " : finish " << finish << " names a measured wrapping with no GPU implementation\n";

        if ((!metal && !dd) || !basic || finish > groundbackpainted)
            continue;

        const G4MaterialPropertiesTable* mpt = os->GetMaterialPropertiesTable();
        if (mpt == nullptr)
        {
            bad << sn << " : no material properties table, nothing to translate\n";
            continue;
        }

        const std::vector<G4String>& pname = mpt->GetMaterialPropertyNames();
        for (unsigned j = 0; j < pname.size(); j++)
        {
            const G4MaterialPropertyVector* v = mpt->GetProperty(pname[j]);
            if (v && v->GetVectorLength() < 2)
                approx << sn << " : " << pname[j] << " has a single node, so G4PhysicsVector::Value reads past it, 0 in practice, for Geant4 and the payload alike\n";
        }

        const bool   painted = U4OpticalSurfaceFinish::IsPainted(finish);
        const bool   backpainted = finish == polishedbackpainted || finish == groundbackpainted;
        const bool   specular = U4OpticalSurfaceFinish::IsPolished(finish);
        const double effi = Prop(mpt, "EFFICIENCY");
        const double refl = Prop(mpt, "REFLECTIVITY");
        const double tran = Prop(mpt, "TRANSMITTANCE");
        const bool   sensor = effi > 0.;
        const bool   reflects = !sensor && refl > 0.;
        const bool   cplx = mpt->GetProperty("REALRINDEX") && mpt->GetProperty("IMAGINARYRINDEX");
        const bool   choose = model == unified && finish != polished && !(dd && painted && !backpainted);
        const double ss = Prop(mpt, "SPECULARSPIKECONSTANT");
        const double sl = Prop(mpt, "SPECULARLOBECONSTANT");
        const double bs = Prop(mpt, "BACKSCATTERCONSTANT");
        const bool   g4spike = Prop(mpt, "SPECULARSPIKECONSTANT", true) >= 1. || (Prop(mpt, "SPECULARLOBECONSTANT", true) >= 1. && os->GetSigmaAlpha() == 0.);

        if (backpainted && mpt->GetProperty("RINDEX") == nullptr)
            bad << sn << " : " << U4OpticalSurfaceFinish::Name(finish) << " without RINDEX on the surface makes Geant4 kill every photon as NoRINDEX\n";

        if (dd && backpainted)
            bad << sn << " : " << U4OpticalSurfaceFinish::Name(finish) << " is a two interface stack, the payload has one interface\n";

        if (reflects && choose && (specular ? !g4spike : ss + sl + bs > 0.))
            bad << sn << " : unified " << U4OpticalSurfaceFinish::Name(finish) << " reflects by ChooseReflection with spike " << ss << " lobe " << sl << " backscatter " << bs
                << " and otherwise Lambertian, the payload is pure " << (specular ? "specular" : "Lambertian") << "\n";

        if (reflects && metal && model == glisur && painted && !specular)
            bad << sn << " : " << U4OpticalSurfaceFinish::Name(finish) << " on dielectric_metal+glisur reflects specularly in Geant4, the payload is Lambertian\n";

        if (cplx && mpt->GetProperty("REFLECTIVITY") == nullptr)
            bad << sn << " : REALRINDEX with IMAGINARYRINDEX gives an angle dependent Fresnel reflectivity, the payload has no REFLECTIVITY and absorbs\n";

        if (!cplx && mpt->GetProperty("REFLECTIVITY") == nullptr)
            approx << sn << " : no REFLECTIVITY, so Geant4 " << (dd && !painted ? "refracts or reflects" : "reflects") << " every photon and never absorbs or detects, the payload "
                   << (sensor ? "detects EFFICIENCY " : "absorbs ") << (sensor ? effi : 1.) << " of them\n";

        if (tran > 0.)
            approx << sn << " : TRANSMITTANCE " << tran << " passes straight through in Geant4, the payload never transmits\n";

        if (reflects && dd && !painted)
            approx << sn << " : REFLECTIVITY " << refl << " on dielectric_dielectric is the fraction handed to Fresnel and Snell in Geant4, which mostly transmits, the payload reflects it\n";

        if (reflects && metal && model == glisur && finish == ground)
            approx << sn << " : ground on dielectric_metal+glisur reflects specularly in Geant4 about a facet normal smeared by 1-polish " << 1. - os->GetPolish() << ", the payload is Lambertian\n";

        if (sensor && refl > 0.)
            approx << sn << " : EFFICIENCY " << effi << " makes addSurface a non reflecting sensor, dropping REFLECTIVITY " << refl << " that makes Geant4 detect (1-REFLECTIVITY)*EFFICIENCY\n";
    }

    const std::string sbad = bad.str();
    const std::string sapprox = approx.str();

    if (!sapprox.empty())
        std::cerr << "U4Surface::Check : " << std::count(sapprox.begin(), sapprox.end(), '\n') << " approximated surface feature(s), see docs/physics.md\n"
                  << sapprox;

    if (sbad.empty())
        return;

    std::stringstream msg;
    msg << "U4Surface::Check : " << std::count(sbad.begin(), sbad.end(), '\n') << " unsupported surface feature(s), the GPU propagation would not reproduce the Geant4 surface physics"
        << " [" << PERMIT_UNSUPPORTED << "=1 to translate anyway]\n"
        << sbad;

    if (!ssys::getenvbool(PERMIT_UNSUPPORTED))
        throw std::runtime_error(msg.str());
    std::cerr << msg.str();
}

/**
U4Surface::MakeFold
--------------------

Canonical usage from U4Tree::initSurfaces creating the stree::surface NPFold. 

**/

inline NPFold* U4Surface::MakeFold(const std::vector<const G4LogicalSurface*>& surfaces, const std::vector<std::string>& keys ) // static
{
    assert(surfaces.size() == keys.size());
    Check(surfaces);

    NPFold* fold = new NPFold ; 
    for(unsigned i=0 ; i < surfaces.size() ; i++)
    {   
        const G4LogicalSurface* ls = surfaces[i] ; 
        [[maybe_unused]] const char* rawname = ls->GetName().c_str() ; 
        const char* key = keys[i].c_str() ; 

        G4OpticalSurface* os = dynamic_cast<G4OpticalSurface*>(ls->GetSurfaceProperty());

        G4SurfaceType theType = os->GetType();
        G4OpticalSurfaceModel theModel = os->GetModel();
        G4OpticalSurfaceFinish theFinish = os->GetFinish();       

        // cf X4OpticalSurface::Convert
        G4double ModelValue = theModel == glisur ? os->GetPolish() : os->GetSigmaAlpha() ;
        assert( ModelValue >= 0. && ModelValue <= 1. );

        const char* osn = os->GetName().c_str() ; 
        G4MaterialPropertiesTable* mpt = os->GetMaterialPropertiesTable() ;

        NPFold* sub = mpt ? U4MaterialPropertiesTable::MakeFold(mpt) : new NPFold;

        sub->set_meta<std::string>("rawname", rawname) ; 
        sub->set_meta<std::string>("OpticalSurfaceName", osn) ; 
        sub->set_meta<std::string>("TypeName", U4SurfaceType::Name(theType)) ; 
        sub->set_meta<std::string>("ModelName", U4OpticalSurfaceModel::Name(theModel)) ; 
        sub->set_meta<std::string>("FinishName", U4OpticalSurfaceFinish::Name(theFinish)) ; 

        sub->set_meta<int>("Type", theType) ; 
        sub->set_meta<int>("Model", theModel) ; 
        sub->set_meta<int>("Finish", theFinish) ; 
        sub->set_meta<double>("ModelValue", ModelValue ) ; 


        const G4LogicalBorderSurface* bs = dynamic_cast<const G4LogicalBorderSurface*>(ls) ; 
        const G4LogicalSkinSurface*   ks = dynamic_cast<const G4LogicalSkinSurface*>(ls) ; 

        if(bs)
        {
            const G4VPhysicalVolume* _pv1 = bs->GetVolume1(); 
            const G4VPhysicalVolume* _pv2 = bs->GetVolume2(); 

            const char* pv1 = S4::Name<G4VPhysicalVolume>(_pv1) ;  // these names have 0x...
            const char* pv2 = S4::Name<G4VPhysicalVolume>(_pv2) ; 

            sub->set_meta<std::string>("pv1", pv1) ; 
            sub->set_meta<std::string>("pv2", pv2) ; 
            sub->set_meta<std::string>("type", "Border" ); 
        }
        else if(ks)
        {
            const G4LogicalVolume* _lv = ks->GetLogicalVolume();
            const char* lv = S4::Name<G4LogicalVolume>(_lv);   // name includes 0x...

            sub->set_meta<std::string>("lv", lv ); 
            sub->set_meta<std::string>("type", "Skin" ); 
        }
        fold->add_subfold( key, sub );  
    }   
    return fold ; 
}

inline NPFold* U4Surface::MakeFold()
{
    //assert(0) ; // this is just used from U4SurfaceTest it seems 
    std::vector<const G4LogicalSurface*> surfaces ; 
    Collect(surfaces); 

    std::vector<std::string> suname_raw ;  
    U4Surface::CollectRawNames(suname_raw, surfaces); 

    std::vector<std::string> suname ;  
    sstr::StripTail_Unique( suname, suname_raw, "0x" );

    return MakeFold(surfaces, suname) ; 
}



/**
U4Surface::Find
-----------------

Looks for a border or skin surface in the same way 
as G4OpBoundaryProcess::PostStepDoIt which the code
is based on. 

**/

inline G4LogicalSurface* U4Surface::Find( const G4VPhysicalVolume* thePrePV, const G4VPhysicalVolume* thePostPV ) 
{
    if(thePostPV == nullptr || thePrePV == nullptr ) return nullptr ;  // surface on world volume not allowed 
    G4LogicalSurface* Surface = G4LogicalBorderSurface::GetSurface(thePrePV, thePostPV);
    if(Surface == nullptr)
    {
        G4bool enteredDaughter = thePostPV->GetMotherLogical() == thePrePV->GetLogicalVolume();
        if(enteredDaughter)
        {
            Surface = G4LogicalSkinSurface::GetSurface(thePostPV->GetLogicalVolume());
            if(Surface == nullptr)
                Surface = G4LogicalSkinSurface::GetSurface(thePrePV->GetLogicalVolume());
        }    
        else 
        {
            Surface = G4LogicalSkinSurface::GetSurface(thePrePV->GetLogicalVolume());
            if(Surface == nullptr)
                Surface = G4LogicalSkinSurface::GetSurface(thePostPV->GetLogicalVolume());
        }    
    }    
    return Surface ; 
}


