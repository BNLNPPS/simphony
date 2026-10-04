#include <stdexcept>
#include <vector>

#include "G4DynamicParticle.hh"
#include "G4Electron.hh"
#include "G4MaterialPropertiesTable.hh"
#include "G4ProcessVector.hh"
#include "G4Scintillation.hh"
#include "G4StepStatus.hh"
#include "G4SystemOfUnits.hh"
#include "G4Track.hh"

#include "U4Genstep.h"

namespace
{
void Require(bool condition, const char* message)
{
    if (!condition)
        throw std::runtime_error(message);
}

void AddSpectrum(G4MaterialPropertiesTable& mpt, const char* key)
{
    const std::vector<G4double> energies{2. * eV, 3. * eV};
    const std::vector<G4double> intensities{1., 1.};
    mpt.AddProperty(key, energies, intensities);
}

void TestProcessSelection()
{
    G4ProcessVector at_rest(2);
    G4ProcessVector post_step(5);

    const U4Genstep::ProcessRange at_rest_range =
        U4Genstep::Processes(fAtRestDoItProc, &at_rest, &post_step);
    Require(at_rest_range.processes == &at_rest, "at-rest process vector was not selected");
    Require(at_rest_range.count == 2, "at-rest process count was not selected");

    const U4Genstep::ProcessRange post_step_range =
        U4Genstep::Processes(fPostStepDoItProc, &at_rest, &post_step);
    Require(post_step_range.processes == &post_step, "post-step process vector was not selected");
    Require(post_step_range.count == 5, "post-step process count was not selected");

    const U4Genstep::ProcessRange missing = U4Genstep::Processes(nullptr);
    Require(missing.processes == nullptr, "null stepping manager returned a process vector");
    Require(missing.count == 0, "null stepping manager returned a process count");
}

void TestTwoComponents()
{
    G4MaterialPropertiesTable mpt;
    AddSpectrum(mpt, "SCINTILLATIONCOMPONENT1");
    AddSpectrum(mpt, "SCINTILLATIONCOMPONENT2");
    mpt.AddConstProperty("SCINTILLATIONYIELD1", 0.25);
    mpt.AddConstProperty("SCINTILLATIONYIELD2", 0.75);
    mpt.AddConstProperty("SCINTILLATIONTIMECONSTANT1", 10. * ns);
    mpt.AddConstProperty("SCINTILLATIONTIMECONSTANT2", 20. * ns);

    G4Scintillation                    scintillation;
    const U4Genstep::ScintillationPlan plan =
        U4Genstep::Scintillation(nullptr, &scintillation, &mpt, 11);

    Require(plan.num_components == 2, "two-component spectrum was not detected");
    Require(plan.counts[0] == 2, "first two-component count is incorrect");
    Require(plan.counts[1] == 9, "second component did not receive the remainder");
    Require(plan.counts[2] == 0, "inactive third component received photons");
    Require(plan.times[0] == 10. * ns, "first time constant is incorrect");
    Require(plan.times[1] == 20. * ns, "second time constant is incorrect");
    Require(plan.photonCount() == 11, "two-component photon count was not preserved");
}

void TestThreeComponents()
{
    G4MaterialPropertiesTable mpt;
    AddSpectrum(mpt, "SCINTILLATIONCOMPONENT1");
    AddSpectrum(mpt, "SCINTILLATIONCOMPONENT2");
    AddSpectrum(mpt, "SCINTILLATIONCOMPONENT3");
    mpt.AddConstProperty("SCINTILLATIONYIELD1", 0.2);
    mpt.AddConstProperty("SCINTILLATIONYIELD2", 0.3);
    mpt.AddConstProperty("SCINTILLATIONYIELD3", 0.5);
    mpt.AddConstProperty("SCINTILLATIONTIMECONSTANT1", 10. * ns);
    mpt.AddConstProperty("SCINTILLATIONTIMECONSTANT2", 20. * ns);
    mpt.AddConstProperty("SCINTILLATIONTIMECONSTANT3", 30. * ns);

    G4Scintillation                    scintillation;
    const U4Genstep::ScintillationPlan plan =
        U4Genstep::Scintillation(nullptr, &scintillation, &mpt, 11);

    Require(plan.num_components == 3, "three-component spectrum was not detected");
    Require(plan.counts[0] == 2, "first three-component count is incorrect");
    Require(plan.counts[1] == 3, "second three-component count is incorrect");
    Require(plan.counts[2] == 5, "third three-component count is incorrect");
    Require(plan.times[0] == 10. * ns, "first time constant is incorrect");
    Require(plan.times[1] == 20. * ns, "second time constant is incorrect");
    Require(plan.times[2] == 30. * ns, "third time constant is incorrect");
    Require(plan.photonCount() == 10, "three-component truncation does not match Geant4");
}

void TestParticleSpecificComponents()
{
    G4MaterialPropertiesTable mpt;
    AddSpectrum(mpt, "SCINTILLATIONCOMPONENT1");
    AddSpectrum(mpt, "SCINTILLATIONCOMPONENT2");
    mpt.AddConstProperty("SCINTILLATIONTIMECONSTANT1", 10. * ns);
    mpt.AddConstProperty("SCINTILLATIONTIMECONSTANT2", 20. * ns);
    mpt.AddConstProperty("ELECTRONSCINTILLATIONYIELD1", 0.4);
    mpt.AddConstProperty("ELECTRONSCINTILLATIONYIELD2", 0.6);
    mpt.AddConstProperty("ELECTRONSCINTILLATIONTIMECONSTANT1", 15. * ns);

    G4Scintillation scintillation;
    scintillation.SetScintillationByParticleType(true);
    G4Track track(
        new G4DynamicParticle(G4Electron::Definition(), G4ThreeVector(0., 0., 1.), 1. * MeV),
        0.,
        G4ThreeVector());

    const U4Genstep::ScintillationPlan plan =
        U4Genstep::Scintillation(&track, &scintillation, &mpt, 10);

    Require(plan.counts[0] == 4, "particle-specific first yield was not used");
    Require(plan.counts[1] == 6, "particle-specific second yield was not used");
    Require(plan.times[0] == 15. * ns, "particle-specific time constant was not used");
    Require(plan.times[1] == 20. * ns, "standard time fallback was not used");
}
} // namespace

int main()
{
    TestProcessSelection();
    TestTwoComponents();
    TestThreeComponents();
    TestParticleSpecificComponents();
    return 0;
}
