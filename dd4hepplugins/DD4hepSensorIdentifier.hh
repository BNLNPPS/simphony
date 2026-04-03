#pragma once
/**
DD4hepSensorIdentifier.hh
===========================

Custom sensor identifier for DD4hep geometries.

Unlike the default U4SensorIdentifierDefault which relies on
GLOBAL_SENSOR_BOUNDARY_LIST env var for non-instanced geometries,
this implementation directly checks G4VSensitiveDetector on volumes.

This works for DD4hep geometries where sensitive detectors are
explicitly set via DetElement::setSensitiveDetector().
**/

#include <iostream>
#include <vector>

#include "G4PVPlacement.hh"
#include "G4VSensitiveDetector.hh"

#include "U4SensorIdentifier.h"

struct DD4hepSensorIdentifier : public U4SensorIdentifier
{
    int level = 0 ;
    int counter = 0 ;  // auto-increment sensor ID (1-based; 0 means "not a sensor" in opticks)

    void setLevel(int _level) override { level = _level ; }

    /**
    getGlobalIdentity
    -------------------
    Checks if the physical volume has a G4VSensitiveDetector attached.
    Returns a unique 1-based sensor_id, or -1 if not sensitive.

    Note: opticks treats sensor_id == 0 as "not a sensor", so IDs must be >= 1.
    PV copy numbers are not reliable (e.g. dRICH SiPMs all have copyNo=0).
    **/
    int getGlobalIdentity(const G4VPhysicalVolume* pv,
                          const G4VPhysicalVolume* /*ppv*/) override
    {
        if (!pv) return -1 ;

        const G4LogicalVolume* lv = pv->GetLogicalVolume() ;
        G4VSensitiveDetector* sd = lv->GetSensitiveDetector() ;

        if (!sd) return -1 ;

        int sensor_id = ++counter ;  // 1-based unique ID

        if (level > 0)
            std::cout << "DD4hepSensorIdentifier::getGlobalIdentity"
                      << " sensor_id " << sensor_id
                      << " sd " << sd->GetName()
                      << " pv " << pv->GetName()
                      << std::endl ;

        return sensor_id ;
    }

    /**
    getInstanceIdentity
    ---------------------
    Same as default: recursively search for G4VSensitiveDetector
    within the instance subtree.
    **/
    int getInstanceIdentity(const G4VPhysicalVolume* instance_outer_pv) const override
    {
        if (!instance_outer_pv) return -1 ;

        std::vector<const G4VPhysicalVolume*> sdpv ;
        FindSD_r(sdpv, instance_outer_pv, 0) ;

        if (sdpv.empty()) return -1 ;

        const G4PVPlacement* pvp =
            dynamic_cast<const G4PVPlacement*>(instance_outer_pv) ;
        return pvp ? pvp->GetCopyNo() : 0 ;
    }

    static void FindSD_r(std::vector<const G4VPhysicalVolume*>& sdpv,
                         const G4VPhysicalVolume* pv, int depth)
    {
        const G4LogicalVolume* lv = pv->GetLogicalVolume() ;
        G4VSensitiveDetector* sd = lv->GetSensitiveDetector() ;
        if (sd) sdpv.push_back(pv) ;
        for (size_t i = 0; i < size_t(lv->GetNoDaughters()); i++)
            FindSD_r(sdpv, lv->GetDaughter(i), depth + 1) ;
    }
};
