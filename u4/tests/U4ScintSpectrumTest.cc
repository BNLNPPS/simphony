/**
 * Regression tests for scintillation-spectrum compatibility with the Opticks
 * GPU representation.
 *
 * Geant4 permits each of `SCINTILLATIONCOMPONENT1`,
 * `SCINTILLATIONCOMPONENT2`, and `SCINTILLATIONCOMPONENT3` to use a distinct
 * emission spectrum. Opticks currently serializes one inverse CDF shared by
 * every timing component, so accepting distinct inputs would silently sample
 * the wrong wavelengths. These tests verify that secondary and tertiary
 * spectrum differences are rejected in both Debug and Release builds.
 */

#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>

#include "NP.hh"
#include "NPFold.h"
#include "U4Scint.h"

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
    std::cerr << "U4ScintSpectrumTest FAILED: " << message << std::endl;
    std::exit(EXIT_FAILURE);
}

/**
 * Creates a two-point serialized emission spectrum.
 *
 * @param secondValue intensity stored at the second energy
 * @return newly allocated double-precision property array
 */
NP* MakeSpectrum(double secondValue)
{
    NP*     spectrum = NP::Make<double>(2, 2);
    double* values = spectrum->values<double>();
    values[0] = 2.;
    values[1] = 1.;
    values[2] = 3.;
    values[3] = secondValue;
    return spectrum;
}

/**
 * Verifies rejection when `SCINTILLATIONCOMPONENT2` differs from component 1.
 */
void TestDistinctSecondComponentIsRejected()
{
    NPFold fold;
    fold.add("SCINTILLATIONCOMPONENT1", MakeSpectrum(2.));
    fold.add("SCINTILLATIONCOMPONENT2", MakeSpectrum(3.));

    bool rejected = false;
    try
    {
        U4Scint scint(&fold, "DistinctSecondComponent");
    }
    catch (const std::runtime_error& error)
    {
        rejected = std::string(error.what()).find("distinct scintillation component spectra") !=
                   std::string::npos;
    }
    Require(rejected, "distinct second component was accepted");
}

/**
 * Verifies rejection when `SCINTILLATIONCOMPONENT3` differs from components
 * 1 and 2.
 */
void TestDistinctThirdComponentIsRejected()
{
    NPFold fold;
    fold.add("SCINTILLATIONCOMPONENT1", MakeSpectrum(2.));
    fold.add("SCINTILLATIONCOMPONENT2", MakeSpectrum(2.));
    fold.add("SCINTILLATIONCOMPONENT3", MakeSpectrum(3.));

    bool rejected = false;
    try
    {
        U4Scint scint(&fold, "DistinctThirdComponent");
    }
    catch (const std::runtime_error& error)
    {
        rejected = std::string(error.what()).find("distinct scintillation component spectra") !=
                   std::string::npos;
    }
    Require(rejected, "distinct third component was accepted");
}

/**
 * Verifies that identical spectra remain supported for all three timing
 * components.
 */
void TestIdenticalComponentsAreAccepted()
{
    NPFold fold;
    fold.add("SCINTILLATIONCOMPONENT1", MakeSpectrum(2.));
    fold.add("SCINTILLATIONCOMPONENT2", MakeSpectrum(2.));
    fold.add("SCINTILLATIONCOMPONENT3", MakeSpectrum(2.));

    bool accepted = false;
    try
    {
        U4Scint scint(&fold, "IdenticalComponents");
        accepted = scint.icdf != nullptr;
    }
    catch (const std::runtime_error&)
    {
        accepted = false;
    }
    Require(accepted, "identical component spectra were rejected");
}

} // namespace

int main()
{
    TestDistinctSecondComponentIsRejected();
    TestDistinctThirdComponentIsRejected();
    TestIdenticalComponentsAreAccepted();
    return 0;
}
