/**
 * @file SensorIdentifier.h
 * Application-provided sensor identities used during geometry translation.
 */
#pragma once

class G4VPhysicalVolume;

namespace simphony
{
/**
 * Identifies sensors in a Geant4 geometry.
 *
 * Supply an implementation through GeometryOptions::sensor_identifier. The
 * runtime borrows it for its process lifetime. Return -1 for volumes without
 * sensors and a unique non-negative identity for each sensor.
 */
struct SensorIdentifier
{
    virtual ~SensorIdentifier() = default;

    /** Sets the diagnostic verbosity requested by the geometry translator. */
    virtual void setLevel(int level) = 0;

    /**
     * Identifies a sensor in the non-instanced remainder of the geometry.
     *
     * @param volume volume being inspected.
     * @param parent parent volume, or nullptr at the world.
     */
    virtual int getGlobalIdentity(
        const G4VPhysicalVolume* volume, const G4VPhysicalVolume* parent) = 0;

    /**
     * Identifies the sensor within a factorized instance subtree.
     *
     * @param outer_volume outer volume of the instance. At most one sensor
     * identity per instance is supported.
     */
    virtual int getInstanceIdentity(const G4VPhysicalVolume* outer_volume) const = 0;
};
} // namespace simphony
