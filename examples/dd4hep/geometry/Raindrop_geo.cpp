//==========================================================================
// Raindrop detector constructor for DD4hep
// Based on DD4hep OpNovice example
//
// Geometry: Vacuum(240) > Lead(220) > Air(200) > Water(100) mm
// Matches eic-opticks tests/geom/opticks_raindrop_with_scintillation.gdml
//==========================================================================
#include <DD4hep/DetFactoryHelper.h>
#include <DD4hep/OpticalSurfaces.h>
#include <DD4hep/Printout.h>
#include <DD4hep/Detector.h>

using namespace dd4hep;

static Ref_t create_raindrop(Detector& description, xml_h e, SensitiveDetector sens)
{
    xml_det_t   x_det = e;
    std::string name  = x_det.nameStr();
    DetElement  sdet(name, x_det.id());

    // Read child elements from XML
    xml_det_t x_container = x_det.child(_Unicode(container));
    xml_det_t x_medium    = x_det.child(_Unicode(medium));
    xml_det_t x_drop      = x_det.child(_Unicode(drop));

    // Build volumes: nested boxes
    Box    container_box(x_container.x(), x_container.y(), x_container.z());
    Volume container_vol("Ct", container_box,
                         description.material(x_container.attr<std::string>(_U(material))));

    Box    medium_box(x_medium.x(), x_medium.y(), x_medium.z());
    Volume medium_vol("Md", medium_box,
                      description.material(x_medium.attr<std::string>(_U(material))));

    Box    drop_box(x_drop.x(), x_drop.y(), x_drop.z());
    Volume drop_vol("DrPMT", drop_box,
                    description.material(x_drop.attr<std::string>(_U(material))));

    // Visualization
    if (x_container.hasAttr(_U(vis)))
        container_vol.setVisAttributes(description, x_container.visStr());
    if (x_medium.hasAttr(_U(vis)))
        medium_vol.setVisAttributes(description, x_medium.visStr());
    if (x_drop.hasAttr(_U(vis)))
        drop_vol.setVisAttributes(description, x_drop.visStr());

    // Mark drop as sensitive for Opticks sensor detection
    drop_vol.setSensitiveDetector(sens);

    // Place: drop inside medium, medium inside container
    PlacedVolume drop_pv   = medium_vol.placeVolume(drop_vol);
    drop_pv.addPhysVolID("drop", 1);

    PlacedVolume medium_pv = container_vol.placeVolume(medium_vol);
    medium_pv.addPhysVolID("medium", 1);

    // Place container into world
    PlacedVolume container_pv = description.pickMotherVolume(sdet).placeVolume(container_vol);
    container_pv.addPhysVolID("system", x_det.id());
    sdet.setPlacement(container_pv);

    // Attach optical border surface between drop and medium
    OpticalSurfaceManager surfMgr = description.surfaceManager();
    OpticalSurface surf = surfMgr.opticalSurface("/world/" + name + "#DropMediumSurf");
    if (surf.isValid()) {
        BorderSurface bsurf = BorderSurface(description, sdet, "DropMedium",
                                            surf, drop_pv, medium_pv);
        bsurf.isValid();
        printout(INFO, "Raindrop", "Attached border surface DropMedium");
    }

    // Attach skin surface with EFFICIENCY to drop volume for Opticks sensor detection
    OpticalSurface skinSurf = surfMgr.opticalSurface("/world/" + name + "#DropSkinSurf");
    if (skinSurf.isValid()) {
        SkinSurface ssf = SkinSurface(description, sdet, "DropSkin", skinSurf, drop_vol);
        ssf.isValid();
        printout(INFO, "Raindrop", "Attached skin surface DropSkin with EFFICIENCY");
    }

    return sdet;
}

DECLARE_DETELEMENT(DD4hep_Raindrop, create_raindrop)
