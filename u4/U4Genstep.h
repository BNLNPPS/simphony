#pragma once

#include <array>
#include <cstddef>

#include "G4StepStatus.hh"
#include "G4Types.hh"

#include "U4_API_EXPORT.hh"

class G4MaterialPropertiesTable;
class G4ProcessVector;
class G4Scintillation;
class G4SteppingManager;
class G4Track;

/**
 * Shared helpers for reconstructing gensteps from official Geant4 optical processes.
 */
struct U4_API U4Genstep
{
    struct ProcessRange
    {
        G4ProcessVector* processes{nullptr};
        std::size_t      count{0};
    };

    struct ScintillationPlan
    {
        G4int                   num_components{0};
        G4double                yield_sum{0.};
        std::array<G4int, 3>    counts{};
        std::array<G4double, 3> times{};

        G4int photonCount() const
        {
            return counts[0] + counts[1] + counts[2];
        }
    };

    static ProcessRange Processes(G4SteppingManager* stepping_manager);
    static ProcessRange Processes(
        G4StepStatus     status,
        G4ProcessVector* at_rest,
        G4ProcessVector* post_step);

    static ScintillationPlan Scintillation(
        const G4Track*                   track,
        const G4Scintillation*           scintillation,
        const G4MaterialPropertiesTable* properties,
        G4int                            total_photons);

    static bool ValidateScintillation(
        const G4Scintillation* scintillation,
        const char*            origin);
};
