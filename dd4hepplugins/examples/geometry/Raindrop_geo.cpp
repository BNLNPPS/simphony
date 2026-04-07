//==========================================================================
// Raindrop detector constructor for DD4hep
// Based on DD4hep OpNovice example
//
// Geometry: Vacuum(240) > Lead(220) > Air(200) > Water(100) mm
//==========================================================================
#include <DD4hep/DetFactoryHelper.h>
#include <DD4hep/OpticalSurfaces.h>
#include <DD4hep/Printout.h>
#include <DD4hep/Detector.h>

using namespace dd4hep;

static Ref_t create_raindrop( Detector& description, xml_h e, SensitiveDetector sens )
{
    xml_det_t   x_det = e;
    std::string name  = x_det.nameStr();
    DetElement  sdet( name, x_det.id() );

    // Read child elements from XML
    xml_det_t x_container = x_det.child( _Unicode( container ) );
    xml_det_t x_medium    = x_det.child( _Unicode( medium ) );
    xml_det_t x_drop      = x_det.child( _Unicode( drop ) );

    // Build volumes: nested boxes
    Box    container_box( x_container.x(), x_container.y(), x_container.z() );
    Volume container_vol( "CtPMT", container_box,
                          description.material( x_container.attr<std::string>( _U( material ) ) ) );

    Box    medium_box( x_medium.x(), x_medium.y(), x_medium.z() );
    Volume medium_vol( "Md", medium_box,
                       description.material( x_medium.attr<std::string>( _U( material ) ) ) );

    Box    drop_box( x_drop.x(), x_drop.y(), x_drop.z() );
    Volume drop_vol( "Dr", drop_box,
                     description.material( x_drop.attr<std::string>( _U( material ) ) ) );

    // Visualization
    if( x_container.hasAttr( _U( vis ) ) )
        container_vol.setVisAttributes( description, x_container.visStr() );
    if( x_medium.hasAttr( _U( vis ) ) )
        medium_vol.setVisAttributes( description, x_medium.visStr() );
    if( x_drop.hasAttr( _U( vis ) ) )
        drop_vol.setVisAttributes( description, x_drop.visStr() );

    // Mark container (OpLead) as sensitive
    container_vol.setSensitiveDetector( sens );

    // Place: drop inside medium, medium inside container
    PlacedVolume drop_pv = medium_vol.placeVolume( drop_vol );
    drop_pv.addPhysVolID( "drop", 1 );

    PlacedVolume medium_pv = container_vol.placeVolume( medium_vol );
    medium_pv.addPhysVolID( "medium", 1 );

    // Place container into world
    PlacedVolume container_pv = description.pickMotherVolume( sdet ).placeVolume( container_vol );
    container_pv.addPhysVolID( "system", x_det.id() );
    sdet.setPlacement( container_pv );

    // Border surface between medium (air) and container (lead) with EFFICIENCY=1.0
    OpticalSurfaceManager surfMgr = description.surfaceManager();
    OpticalSurface        surf    = surfMgr.opticalSurface( "/world/" + name + "#MediumContainerSurf" );
    if( surf.isValid() )
    {
        BorderSurface bsurf = BorderSurface( description, sdet, "MediumContainer",
                                             surf, medium_pv, container_pv );
        bsurf.isValid();
        printout( INFO, "Raindrop", "Attached border surface MediumContainer (air/lead)" );
    }

    // Skin surface on container for Opticks sensor detection
    OpticalSurface skinSurf = surfMgr.opticalSurface( "/world/" + name + "#ContainerSkinSurf" );
    if( skinSurf.isValid() )
    {
        SkinSurface ssf = SkinSurface( description, sdet, "ContainerSkin", skinSurf, container_vol );
        ssf.isValid();
        printout( INFO, "Raindrop", "Attached skin surface ContainerSkin with EFFICIENCY" );
    }

    return sdet;
}

DECLARE_DETELEMENT( DD4hep_Raindrop, create_raindrop )
