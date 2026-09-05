/**
 * Unit tests for converting legacy optical reemission properties to Geant4
 * wavelength-shifting properties.
 *
 * Here, legacy refers specifically to the `REEMISSIONPROB` property schema
 * used by the removed local scintillation process. In that schema, wavelength
 * shifting is expressed with an absorption length, `ABSLENGTH`, followed by
 * a conditional reemission probability. The conversion translates this
 * representation into competing ordinary-absorption and wavelength-shifting
 * lengths suitable for Geant4's `G4OpWLS` process:
 *
 * @code
 * ABSLENGTH    -> ABSLENGTH / (1 - REEMISSIONPROB)
 * WLSABSLENGTH -> ABSLENGTH / REEMISSIONPROB
 * @endcode
 *
 * These tests cover interpolated inputs on different energy grids, the zero-
 * and unit-probability limits, rejection of unsafe varying endpoints,
 * incomplete legacy definitions, preservation of authoritative WLS
 * properties, emission-spectrum cloning, time-constant propagation, removal
 * of consumed legacy properties, and idempotence.
 *
 * Requirements use an always-active failure helper rather than `assert`, so
 * the conversion calls and checks execute in both Debug and Release builds.
 */

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <vector>

#include "G4Material.hh"
#include "G4MaterialPropertiesTable.hh"
#include "G4MaterialPropertyVector.hh"
#include "G4SystemOfUnits.hh"

#include "OPTICKS_LOG.hh"
#include "U4Material.hh"

namespace
{
/**
 * Terminates the test executable with a diagnostic when a requirement fails.
 *
 * @param condition requirement result
 * @param message diagnostic printed when `condition` is false
 */
void Require(bool condition, const char* message)
{
    if (condition)
        return;
    std::cerr << "U4MaterialLegacyReemissionTest FAILED: " << message << std::endl;
    std::exit(EXIT_FAILURE);
}

/**
 * Compares length- or time-valued quantities using an absolute tolerance.
 *
 * @param lhs first value
 * @param rhs second value
 * @param tolerance maximum accepted absolute difference
 * @return `true` when the values differ by less than `tolerance`
 */
bool Close(G4double lhs, G4double rhs, G4double tolerance = 1e-9 * m)
{
    return std::abs(lhs - rhs) < tolerance;
}

/**
 * Constructs a minimal material with an initially empty property table.
 *
 * @param name material name
 * @return newly allocated material
 */
G4Material* MakeMaterial(const char* name)
{
    G4Material* material = new G4Material(name, 1., 1.01 * g / mole, 1. * g / cm3);
    material->SetMaterialPropertiesTable(new G4MaterialPropertiesTable);
    return material;
}

/**
 * Constructs a Geant4 material-property vector from parallel arrays.
 *
 * @param energies photon-energy coordinates
 * @param values property values corresponding to `energies`
 * @return newly allocated property vector
 */
G4MaterialPropertyVector* MakeProperty(
    const std::vector<G4double>& energies,
    const std::vector<G4double>& values)
{
    return new G4MaterialPropertyVector(energies, values);
}

/**
 * Verify competing absorption lengths on the union of mismatched grids.
 *
 * Also verifies emission-component cloning, time-constant propagation,
 * consumption of `REEMISSIONPROB`, and idempotence of the conversion.
 */
void TestCompetingLengthsAndMismatchedGrids()
{
    G4Material*                material = MakeMaterial("U4MaterialLegacyReemissionTestCompetingLengths");
    G4MaterialPropertiesTable* mpt = material->GetMaterialPropertiesTable();

    const std::vector<G4double> absorptionEnergies = {2. * eV, 4. * eV};
    const std::vector<G4double> absorptionValues = {10. * m, 20. * m};
    const std::vector<G4double> probabilityEnergies = {2. * eV, 3. * eV, 4. * eV};
    const std::vector<G4double> probabilities = {0.25, 0.5, 0.75};
    const std::vector<G4double> emissionValues = {1., 2.};

    G4MaterialPropertyVector* originalAbsorption =
        MakeProperty(absorptionEnergies, absorptionValues);
    G4MaterialPropertyVector* scintComponent =
        MakeProperty(absorptionEnergies, emissionValues);

    mpt->AddProperty("ABSLENGTH", originalAbsorption);
    mpt->AddProperty(
        "REEMISSIONPROB",
        MakeProperty(probabilityEnergies, probabilities),
        true);
    mpt->AddProperty("SCINTILLATIONCOMPONENT1", scintComponent);
    mpt->AddConstProperty("SCINTILLATIONTIMECONSTANT1", 7. * ns);

    const bool converted = U4Material::ConvertLegacyReemissionToWLS(material);
    Require(converted, "non-zero legacy properties were not converted");
    Require(mpt->GetProperty("REEMISSIONPROB") == nullptr, "legacy probability was retained");

    G4MaterialPropertyVector* absorption = mpt->GetProperty("ABSLENGTH");
    G4MaterialPropertyVector* wlsAbsorption = mpt->GetProperty("WLSABSLENGTH");
    G4MaterialPropertyVector* wlsComponent = mpt->GetProperty("WLSCOMPONENT");

    Require(absorption != nullptr, "converted ABSLENGTH is missing");
    Require(wlsAbsorption != nullptr, "converted WLSABSLENGTH is missing");
    Require(wlsComponent != nullptr, "converted WLSCOMPONENT is missing");
    Require(wlsComponent != scintComponent, "emission component was not cloned");
    Require(absorption->GetVectorLength() == 3, "energy grids were not merged");
    Require(wlsAbsorption->GetVectorLength() == 3, "WLS energy grid was not merged");
    Require(mpt->ConstPropertyExists("WLSTIMECONSTANT"), "WLSTIMECONSTANT is missing");
    Require(Close(mpt->GetConstProperty("WLSTIMECONSTANT"), 7. * ns, 1e-12 * ns),
            "WLSTIMECONSTANT was not copied");

    Require(Close(absorption->Value(2. * eV), 10. * m / 0.75), "ordinary absorption at 2 eV is wrong");
    Require(Close(wlsAbsorption->Value(2. * eV), 40. * m), "WLS absorption at 2 eV is wrong");
    Require(Close(absorption->Value(3. * eV), 30. * m), "interpolated ordinary absorption is wrong");
    Require(Close(wlsAbsorption->Value(3. * eV), 30. * m), "interpolated WLS absorption is wrong");
    Require(Close(absorption->Value(4. * eV), 80. * m), "ordinary absorption at 4 eV is wrong");
    Require(Close(wlsAbsorption->Value(4. * eV), 20. * m / 0.75), "WLS absorption at 4 eV is wrong");

    const bool convertedAgain = U4Material::ConvertLegacyReemissionToWLS(material);
    Require(!convertedAgain, "conversion is not idempotent");
}

/**
 * Verify that an all-zero reemission probability is consumed as a no-op.
 *
 * Ordinary absorption must retain its original vector and no WLS absorption
 * property should be introduced.
 */
void TestZeroProbabilityRemoval()
{
    G4Material*                 material = MakeMaterial("U4MaterialLegacyReemissionTestZeroProbability");
    G4MaterialPropertiesTable*  mpt = material->GetMaterialPropertiesTable();
    const std::vector<G4double> energies = {2. * eV, 3. * eV};
    const std::vector<G4double> absorptionValues = {10. * m, 20. * m};
    const std::vector<G4double> probabilities = {0., 0.};

    G4MaterialPropertyVector* absorption = MakeProperty(energies, absorptionValues);
    mpt->AddProperty("ABSLENGTH", absorption);
    mpt->AddProperty("REEMISSIONPROB", MakeProperty(energies, probabilities), true);

    const bool converted = U4Material::ConvertLegacyReemissionToWLS(material);
    Require(converted, "zero probability was not handled");
    Require(mpt->GetProperty("REEMISSIONPROB") == nullptr, "zero probability was not removed");
    Require(mpt->GetProperty("ABSLENGTH") == absorption, "zero probability changed ABSLENGTH");
    Require(mpt->GetProperty("WLSABSLENGTH") == nullptr, "zero probability created WLS absorption");
}

/**
 * Verify the unit-probability limit where every absorption becomes WLS.
 *
 * The WLS length must equal the original absorption length while the competing
 * ordinary-absorption length becomes effectively infinite.
 */
void TestUnitProbabilityConversion()
{
    G4Material*                 material = MakeMaterial("U4MaterialLegacyReemissionTestUnitProbability");
    G4MaterialPropertiesTable*  mpt = material->GetMaterialPropertiesTable();
    const std::vector<G4double> energies = {2. * eV, 3. * eV};
    const std::vector<G4double> absorptionValues = {10. * m, 20. * m};
    const std::vector<G4double> probabilities = {1., 1.};
    const std::vector<G4double> emissionValues = {1., 2.};

    mpt->AddProperty("ABSLENGTH", MakeProperty(energies, absorptionValues));
    mpt->AddProperty("REEMISSIONPROB", MakeProperty(energies, probabilities), true);
    mpt->AddProperty("SCINTILLATIONCOMPONENT1", MakeProperty(energies, emissionValues));

    const bool converted = U4Material::ConvertLegacyReemissionToWLS(material);
    Require(converted, "unit probability was not converted");
    Require(mpt->GetProperty("REEMISSIONPROB") == nullptr, "unit probability was retained");

    G4MaterialPropertyVector* absorption = mpt->GetProperty("ABSLENGTH");
    G4MaterialPropertyVector* wlsAbsorption = mpt->GetProperty("WLSABSLENGTH");
    Require(absorption != nullptr, "unit probability ordinary absorption is missing");
    Require(wlsAbsorption != nullptr, "unit probability WLS absorption is missing");
    Require(absorption->Value(2. * eV) > 1e100 * m,
            "unit probability ordinary absorption is not effectively infinite");
    Require(Close(wlsAbsorption->Value(2. * eV), 10. * m),
            "unit probability WLS absorption at 2 eV is wrong");
    Require(Close(wlsAbsorption->Value(3. * eV), 20. * m),
            "unit probability WLS absorption at 3 eV is wrong");
}

/**
 * Verify that an incomplete non-zero legacy definition is left intact.
 *
 * Without an emission spectrum the conversion must fail without partially
 * mutating or consuming the legacy material properties.
 */
void TestIncompleteDefinitionIsRetained()
{
    G4Material*                 material = MakeMaterial("U4MaterialLegacyReemissionTestIncomplete");
    G4MaterialPropertiesTable*  mpt = material->GetMaterialPropertiesTable();
    const std::vector<G4double> energies = {2. * eV, 3. * eV};
    const std::vector<G4double> absorptionValues = {10. * m, 20. * m};
    const std::vector<G4double> probabilities = {0.25, 0.5};

    G4MaterialPropertyVector* absorption = MakeProperty(energies, absorptionValues);
    G4MaterialPropertyVector* probability = MakeProperty(energies, probabilities);
    mpt->AddProperty("ABSLENGTH", absorption);
    mpt->AddProperty("REEMISSIONPROB", probability, true);

    const bool converted = U4Material::ConvertLegacyReemissionToWLS(material);
    Require(!converted, "incomplete non-zero definition was reported as converted");
    Require(mpt->GetProperty("REEMISSIONPROB") == probability, "failed conversion removed legacy probability");
    Require(mpt->GetProperty("ABSLENGTH") == absorption, "failed conversion changed ABSLENGTH");
    Require(mpt->GetProperty("WLSABSLENGTH") == nullptr, "failed conversion created WLS absorption");
}

/**
 * Verify that a varying probability reaching zero or one is left unconverted.
 *
 * A Geant4 property vector linearly interpolates its stored interaction
 * lengths. Conversion must therefore avoid adjoining a finite length to the
 * effectively infinite length required at either probability endpoint.
 */
void TestVaryingEndpointProbabilityIsRetained()
{
    const std::vector<G4double>              energies = {2. * eV, 4. * eV};
    const std::vector<G4double>              absorptionValues = {10. * m, 10. * m};
    const std::vector<G4double>              emissionValues = {1., 1.};
    const std::vector<std::vector<G4double>> probabilities = {
        {0., 0.5},
        {0.5, 1.}};
    const char* names[] = {
        "U4MaterialLegacyReemissionTestVaryingFromZero",
        "U4MaterialLegacyReemissionTestVaryingToOne"};

    for (std::size_t i = 0; i < probabilities.size(); ++i)
    {
        G4Material*                material = MakeMaterial(names[i]);
        G4MaterialPropertiesTable* mpt = material->GetMaterialPropertiesTable();
        G4MaterialPropertyVector*  absorption = MakeProperty(energies, absorptionValues);
        G4MaterialPropertyVector*  probability = MakeProperty(energies, probabilities[i]);

        mpt->AddProperty("ABSLENGTH", absorption);
        mpt->AddProperty("REEMISSIONPROB", probability, true);
        mpt->AddProperty(
            "SCINTILLATIONCOMPONENT1",
            MakeProperty(energies, emissionValues));

        const bool converted = U4Material::ConvertLegacyReemissionToWLS(material);
        Require(!converted, "varying endpoint probability was reported as converted");
        Require(mpt->GetProperty("REEMISSIONPROB") == probability,
                "rejected endpoint conversion removed legacy probability");
        Require(mpt->GetProperty("ABSLENGTH") == absorption,
                "rejected endpoint conversion changed ABSLENGTH");
        Require(mpt->GetProperty("WLSABSLENGTH") == nullptr,
                "rejected endpoint conversion created WLSABSLENGTH");
        Require(mpt->GetProperty("WLSCOMPONENT") == nullptr,
                "rejected endpoint conversion created WLSCOMPONENT");
    }
}

/**
 * Verify that existing WLS absorption data remains authoritative.
 *
 * Conversion may complete the missing WLS emission component and time
 * constant, but must not replace existing absorption vectors.
 */
void TestAuthoritativeWLSIsPreserved()
{
    G4Material*                 material = MakeMaterial("U4MaterialLegacyReemissionTestAuthoritativeWLS");
    G4MaterialPropertiesTable*  mpt = material->GetMaterialPropertiesTable();
    const std::vector<G4double> energies = {2. * eV, 3. * eV};
    const std::vector<G4double> absorptionValues = {10. * m, 20. * m};
    const std::vector<G4double> probabilities = {0.25, 0.5};
    const std::vector<G4double> emissionValues = {1., 2.};
    const std::vector<G4double> wlsAbsorptionValues = {30. * m, 40. * m};

    G4MaterialPropertyVector* absorption = MakeProperty(energies, absorptionValues);
    G4MaterialPropertyVector* wlsAbsorption = MakeProperty(energies, wlsAbsorptionValues);
    G4MaterialPropertyVector* scintComponent = MakeProperty(energies, emissionValues);
    mpt->AddProperty("ABSLENGTH", absorption);
    mpt->AddProperty("WLSABSLENGTH", wlsAbsorption);
    mpt->AddProperty("SCINTILLATIONCOMPONENT1", scintComponent);
    mpt->AddProperty("REEMISSIONPROB", MakeProperty(energies, probabilities), true);

    const bool converted = U4Material::ConvertLegacyReemissionToWLS(material);
    Require(converted, "authoritative WLS definition was not completed");
    Require(mpt->GetProperty("REEMISSIONPROB") == nullptr, "legacy probability survived authoritative WLS");
    Require(mpt->GetProperty("ABSLENGTH") == absorption, "authoritative WLS changed ABSLENGTH");
    Require(mpt->GetProperty("WLSABSLENGTH") == wlsAbsorption, "authoritative WLS absorption was replaced");
    Require(mpt->GetProperty("WLSCOMPONENT") != nullptr, "authoritative WLS component was not completed");
    Require(mpt->GetProperty("WLSCOMPONENT") != scintComponent, "authoritative WLS component was not cloned");
    Require(mpt->ConstPropertyExists("WLSTIMECONSTANT"), "authoritative WLS time constant is missing");
    Require(Close(mpt->GetConstProperty("WLSTIMECONSTANT"), 0., 1e-12 * ns),
            "default WLS time constant is not zero");
}
} // namespace

int main(int argc, char** argv)
{
    OPTICKS_LOG(argc, argv);

    TestCompetingLengthsAndMismatchedGrids();
    TestZeroProbabilityRemoval();
    TestUnitProbabilityConversion();
    TestIncompleteDefinitionIsRetained();
    TestAuthoritativeWLSIsPreserved();
    TestVaryingEndpointProbabilityIsRetained();
    return 0;
}
