#include "U4Genstep.h"

#include <string>

#include "G4Exception.hh"
#include "G4MaterialPropertiesTable.hh"
#include "G4ParticleDefinition.hh"
#include "G4ProcessVector.hh"
#include "G4Scintillation.hh"
#include "G4SteppingManager.hh"
#include "G4Track.hh"

U4Genstep::ProcessRange U4Genstep::Processes(G4SteppingManager* stepping_manager)
{
    if (stepping_manager == nullptr)
        return {};

    return Processes(
        stepping_manager->GetfStepStatus(),
        stepping_manager->GetfAtRestDoItVector(),
        stepping_manager->GetfPostStepDoItVector());
}

U4Genstep::ProcessRange U4Genstep::Processes(
    G4StepStatus     status,
    G4ProcessVector* at_rest,
    G4ProcessVector* post_step)
{
    G4ProcessVector* processes = status == fAtRestDoItProc ? at_rest : post_step;
    return {processes, processes ? processes->size() : 0};
}

U4Genstep::ScintillationPlan U4Genstep::Scintillation(
    const G4Track*                   track,
    const G4Scintillation*           scintillation,
    const G4MaterialPropertiesTable* properties,
    G4int                            total_photons)
{
    ScintillationPlan plan;
    if (scintillation == nullptr || properties == nullptr || total_photons <= 0)
        return plan;

    const G4int component_keys[3] = {
        kSCINTILLATIONCOMPONENT1,
        kSCINTILLATIONCOMPONENT2,
        kSCINTILLATIONCOMPONENT3};
    plan.num_components = properties->GetProperty(component_keys[2])   ? 3
                          : properties->GetProperty(component_keys[1]) ? 2
                          : properties->GetProperty(component_keys[0]) ? 1
                                                                       : 0;
    if (plan.num_components == 0)
        return plan;

    const char* standard_yield_keys[3] = {
        "SCINTILLATIONYIELD1",
        "SCINTILLATIONYIELD2",
        "SCINTILLATIONYIELD3"};
    const char* standard_time_keys[3] = {
        "SCINTILLATIONTIMECONSTANT1",
        "SCINTILLATIONTIMECONSTANT2",
        "SCINTILLATIONTIMECONSTANT3"};

    G4String particle_prefix;
    if (scintillation->GetScintillationByParticleType())
    {
        if (track == nullptr)
            return {};

        const G4ParticleDefinition* definition = track->GetParticleDefinition();
        const G4String&             name = definition->GetParticleName();
        particle_prefix = name == "proton" ? "PROTON" : name == "deuteron"                                            ? "DEUTERON"
                                                    : name == "triton"                                                ? "TRITON"
                                                    : name == "alpha"                                                 ? "ALPHA"
                                                    : name == "neutron" || definition->GetParticleType() == "nucleus" ? "ION"
                                                                                                                      : "ELECTRON";
    }

    G4double yields[3] = {1., 0., 0.};
    for (G4int component = 0; component < plan.num_components; ++component)
    {
        const G4String suffix = std::to_string(component + 1);
        const G4String yield_key = particle_prefix.empty() ? G4String(standard_yield_keys[component])
                                                           : particle_prefix + "SCINTILLATIONYIELD" + suffix;
        const G4String time_key = particle_prefix.empty() ? G4String(standard_time_keys[component])
                                                          : particle_prefix + "SCINTILLATIONTIMECONSTANT" + suffix;

        yields[component] = properties->ConstPropertyExists(yield_key) ? properties->GetConstProperty(yield_key)
                                                                       : (component == 0 ? 1. : 0.);
        plan.times[component] = properties->ConstPropertyExists(time_key) ? properties->GetConstProperty(time_key)
                                                                          : (properties->ConstPropertyExists(standard_time_keys[component]) ? properties->GetConstProperty(standard_time_keys[component])
                                                                                                                                            : 0.);
        plan.yield_sum += yields[component];
    }

    if (plan.yield_sum <= 0.)
        return plan;

    if (plan.num_components == 1)
    {
        plan.counts[0] = total_photons;
    }
    else if (plan.num_components == 2)
    {
        plan.counts[0] = G4int(yields[0] / plan.yield_sum * total_photons);
        plan.counts[1] = total_photons - plan.counts[0];
    }
    else
    {
        for (G4int component = 0; component < 3; ++component)
            plan.counts[component] =
                G4int(yields[component] / plan.yield_sum * total_photons);
    }

    for (G4int component = 0; component < plan.num_components; ++component)
    {
        if (properties->GetProperty(component_keys[component]) == nullptr)
            plan.counts[component] = 0;
    }
    return plan;
}

bool U4Genstep::ValidateScintillation(
    const G4Scintillation* scintillation,
    const char*            origin)
{
    if (scintillation == nullptr || !scintillation->GetFiniteRiseTime())
        return true;

    G4ExceptionDescription description;
    description
        << "Finite scintillation rise time cannot be represented by "
        << "the current scintillation genstep. Disable "
        << "/process/optical/scintillation/setFiniteRiseTime before running.";
    G4Exception(
        origin ? origin : "U4Genstep::ValidateScintillation",
        "U4Genstep001",
        FatalException,
        description);
    return false;
}
