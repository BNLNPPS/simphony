#include <cassert>
#include <cmath>
#include <vector>

#include "G4Material.hh"
#include "G4MaterialPropertiesTable.hh"
#include "G4MaterialPropertyVector.hh"
#include "G4SystemOfUnits.hh"

#include "OPTICKS_LOG.hh"
#include "U4Material.hh"

int main(int argc, char** argv)
{
    OPTICKS_LOG(argc, argv);

    const std::vector<G4double> energies = {2.*eV, 3.*eV};
    const std::vector<G4double> absorptionValues = {10.*m, 20.*m};
    const std::vector<G4double> probabilities = {0.25, 0.50};
    const std::vector<G4double> emissionValues = {1.0, 2.0};

    G4Material* material = new G4Material(
        "U4MaterialLegacyReemissionTestMaterial",
        1., 1.01*g/mole, 1.*g/cm3);

    G4MaterialPropertiesTable* mpt = new G4MaterialPropertiesTable;
    mpt->AddProperty(
        "ABSLENGTH",
        new G4MaterialPropertyVector(energies, absorptionValues));
    mpt->AddProperty(
        "REEMISSIONPROB",
        new G4MaterialPropertyVector(energies, probabilities),
        true);
    G4MaterialPropertyVector* scintComponent =
        new G4MaterialPropertyVector(energies, emissionValues);
    mpt->AddProperty("SCINTILLATIONCOMPONENT1", scintComponent);
    mpt->AddConstProperty("SCINTILLATIONTIMECONSTANT1", 7.*ns);
    material->SetMaterialPropertiesTable(mpt);

    assert(U4Material::ConvertLegacyReemissionToWLS(material));
    assert(mpt->GetProperty("REEMISSIONPROB") == nullptr);

    G4MaterialPropertyVector* absorption = mpt->GetProperty("ABSLENGTH");
    G4MaterialPropertyVector* wlsAbsorption = mpt->GetProperty("WLSABSLENGTH");
    G4MaterialPropertyVector* wlsComponent = mpt->GetProperty("WLSCOMPONENT");

    assert(absorption);
    assert(wlsAbsorption);
    assert(wlsComponent);
    assert(wlsComponent != scintComponent);
    assert(mpt->ConstPropertyExists("WLSTIMECONSTANT"));
    assert(std::abs(mpt->GetConstProperty("WLSTIMECONSTANT") - 7.*ns) < 1e-12*ns);

    assert(std::abs(absorption->Value(2.*eV) - (10.*m/0.75)) < 1e-9*m);
    assert(std::abs(wlsAbsorption->Value(2.*eV) - 40.*m) < 1e-9*m);
    assert(std::abs(absorption->Value(3.*eV) - 40.*m) < 1e-9*m);
    assert(std::abs(wlsAbsorption->Value(3.*eV) - 40.*m) < 1e-9*m);

    assert(!U4Material::ConvertLegacyReemissionToWLS(material));
    return 0;
}
