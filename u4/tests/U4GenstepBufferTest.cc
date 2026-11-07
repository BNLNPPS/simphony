#include <cstdlib>
#include <future>
#include <stdexcept>
#include <vector>

#include "G4DynamicParticle.hh"
#include "G4Electron.hh"
#include "G4Material.hh"
#include "G4Step.hh"
#include "G4SystemOfUnits.hh"
#include "G4Track.hh"
#include "OpticksGenstep.h"
#include "SEvt.hh"
#include "U4.hh"
#include "squad.h"

namespace
{
void Require(bool condition, const char* message)
{
    if (!condition)
        throw std::runtime_error(message);
}

std::vector<quad6> Collect(const G4Material* material, int photons)
{
    G4Track track(new G4DynamicParticle(G4Electron::Definition(), {0., 0., 1.}, 1. * MeV),
                  0., {1., 2., 3.});
    G4Step  step;
    step.SetTrack(&track);
    track.SetStep(&step);
    track.SetTrackID(17);
    step.SetStepLength(2. * mm);
    step.GetPreStepPoint()->SetMaterial(const_cast<G4Material*>(material));
    step.GetPreStepPoint()->SetPosition({1., 2., 3.});
    step.GetPostStepPoint()->SetPosition({1., 2., 5.});
    step.GetPreStepPoint()->SetVelocity(200. * mm / ns);
    step.GetPostStepPoint()->SetVelocity(150. * mm / ns);
    std::vector<quad6> buffer;
    for (int i = 0; i < 32; ++i)
    {
        U4::CollectGenstep_G4Cerenkov_modified(
            &track, &step, photons, 1.1, 2. * eV, 3. * eV, 0.9, 0.19, 10., 12., &buffer);
        U4::CollectGenstep_Scintillation(&track, &step, photons + 1, 0, 12. * ns, &buffer);
    }
    return buffer;
}
} // namespace

int main()
{
    Require(!SEvt::Exists(SEvt::EGPU) && !SEvt::Exists(SEvt::ECPU), "Unexpected global event");
    G4Electron::Definition();
    G4Material                                   material("buffer_test", 1., 1. * g / mole, 1. * g / cm3);
    std::vector<std::future<std::vector<quad6>>> workers;
    for (int worker = 0; worker < 4; ++worker)
        workers.push_back(std::async(std::launch::async, [&material, worker] {
            return Collect(&material, 3 + worker);
        }));
    for (int worker = 0; worker < 4; ++worker)
    {
        const auto buffer = workers[worker].get();
        Require(buffer.size() == 64, "A worker lost or shared gensteps");
        for (int i = 0; i < 64; ++i)
        {
            const auto& genstep = buffer[i];
            Require(genstep.gentype() == (i % 2 == 0 ? OpticksGenstep_G4Cerenkov_modified
                                                     : OpticksGenstep_SCINTILLATION),
                    "Incorrect genstep process");
            Require(genstep.numphoton() == 3 + worker + i % 2, "Worker photon counts were mixed");
            Require(genstep.matline() == material.GetIndex() + SEvt::G4_INDEX_OFFSET,
                    "Material mapping must wait until GPU submission");
            Require(genstep.q0.i.y == 17, "Track identity was lost");
            Require(genstep.q1.f.x == 1.f && genstep.q2.f.z == 2.f, "Step geometry was lost");
        }
    }
    Require(!SEvt::Exists(SEvt::EGPU) && !SEvt::Exists(SEvt::ECPU), "Collection created global events");
    setenv(U4::CollectGenstep_G4Cerenkov_modified_DISABLE, "1", 1);
    setenv(U4::CollectGenstep_Scintillation_DISABLE, "1", 1);
    Require(Collect(&material, 3).empty(), "Disabled processes still collected gensteps");
    unsetenv(U4::CollectGenstep_G4Cerenkov_modified_DISABLE);
    unsetenv(U4::CollectGenstep_Scintillation_DISABLE);
    return 0;
}
