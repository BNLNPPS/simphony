/**
 * @file simphony.h
 * Provides the public interface to the process-wide Simphony geometry and GPU runtime.
 *
 * Initialize once before simulating or saving geometry. The runtime supports
 * one geometry and one pending event; there is no shutdown or geometry-replacement
 * operation. State-changing calls are serialized, but callers must also
 * coordinate access to shared Geant4 and lower-level simulation state.
 */

#pragma once

#include <filesystem>

#include "SensorIdentifier.h"

class G4VPhysicalVolume;

namespace simphony
{
/** Selects the GPU availability policy for initialize(). */
enum class GpuRequirement
{
    /** Require a GPU backend; fail if no CUDA-capable GPU is available. */
    Required,
    /** Create a GPU backend if available; otherwise convert geometry only. */
    Optional,
    /** Convert geometry without attempting GPU backend initialization. */
    Disabled,
};

/** Specifies geometry translation and GPU initialization options. */
struct GeometryOptions
{
    /**
     * Borrowed sensor identifier, or nullptr to use the default identifier.
     *
     * @note A supplied identifier must remain valid for the runtime's lifetime.
     */
    SensorIdentifier* sensor_identifier = nullptr;

    /** GPU availability policy; a GPU is required by default. */
    GpuRequirement gpu = GpuRequirement::Required;
};

/**
 * Translates a Geant4 world and initializes the selected backend.
 *
 * Repeating this call with the same world pointer, sensor identifier pointer,
 * and GPU policy is a no-op. Geometry is not saved automatically.
 *
 * If geometry translation or backend creation throws, its exception propagates
 * and subsequent initialize() calls are rejected because lower-level global
 * state may be partially populated. Restart the process to try again.
 * Validation failures before runtime construction can be retried.
 *
 * @param world non-null, fully constructed world. Ownership remains with the
 * caller; keep the geometry alive and unchanged for the runtime's lifetime.
 * @param options translation options, copied by the runtime without taking
 * ownership of the sensor identifier.
 * @throws std::invalid_argument if world is nullptr.
 * @throws std::logic_error if already initialized with different inputs or a
 * previous geometry/backend initialization failed.
 * @throws std::runtime_error if a required GPU is unavailable or runtime
 * creation reports failure.
 */
void initialize(
    const G4VPhysicalVolume* world,
    const GeometryOptions&   options = {});

/**
 * Queries initialization status.
 *
 * @return whether initialize() has completed successfully.
 */
bool is_initialized() noexcept;

/**
 * Queries GPU backend availability.
 *
 * @return whether this runtime has a GPU backend. False before initialization
 * and after geometry-only initialization; this is not a hardware probe.
 */
bool has_gpu() noexcept;

/**
 * Simulates an event using the GPU backend and its prepared event inputs.
 *
 * @param event_id non-negative event identifier forwarded to the backend.
 * @param reset if true, clean up the event before returning. If false (the
 * default), retain event data for retrieval and call reset(event_id) afterward,
 * before simulating another event.
 * @throws std::invalid_argument if event_id is negative.
 * @throws std::logic_error if uninitialized, no GPU backend exists, or a previous
 * event is awaiting reset.
 */
void simulate(int event_id, bool reset = false);

/**
 * Cleans up the event retained by simulate(event_id, false).
 *
 * @param event_id non-negative identifier of the pending event; must match the
 * simulate() call.
 * @throws std::invalid_argument if event_id is negative.
 * @throws std::logic_error if uninitialized, no GPU backend exists, no event is
 * pending, or event_id does not match the pending event.
 * @note This resets event data, not the runtime or its geometry.
 */
void reset(int event_id);

/**
 * Saves the initialized geometry, with or without a GPU backend.
 *
 * @param directory non-empty destination for origin.gdml and the CSGFoundry/
 * export, including associated simulation data. Use a fresh destination to
 * avoid mixing files from different exports.
 * @throws std::invalid_argument if directory is empty.
 * @throws std::logic_error if the runtime is uninitialized.
 */
void save_geometry(const std::filesystem::path& directory);
} // namespace simphony
