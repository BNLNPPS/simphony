#include <cassert>
#include <cstring>
#include <memory>
#include <stdexcept>

#include "OpticksGenstep.h"
#include "SGenstep.h"

void test_retired_codes()
{
    for (unsigned type = 1; type <= 5; ++type)
    {
        assert(std::strcmp(OpticksGenstep_::Name(type), OpticksGenstep_::INVALID_) == 0);
        assert(!OpticksGenstep_::IsValid(type));
        assert(!OpticksGenstep_::IsCerenkov(type));
        assert(!OpticksGenstep_::IsScintillation(type));
        assert(!OpticksGenstep_::IsExpected(type));
        assert(OpticksGenstep_::GenstepToPhotonFlag(type) == NAN_ABORT);
    }
}

void test_current_optical_codes()
{
    static_assert(OpticksGenstep_TORCH == 6, "genstep protocol values must remain stable");
    static_assert(OpticksGenstep_CERENKOV == 15, "genstep protocol values must remain stable");
    static_assert(OpticksGenstep_SCINTILLATION == 16, "genstep protocol values must remain stable");

    assert(OpticksGenstep_::Type("CERENKOV") == OpticksGenstep_CERENKOV);
    assert(OpticksGenstep_::Type("SCINTILLATION") == OpticksGenstep_SCINTILLATION);
    assert(OpticksGenstep_::IsCerenkov(OpticksGenstep_CERENKOV));
    assert(OpticksGenstep_::IsCerenkov(OpticksGenstep_G4Cerenkov_modified));
    assert(OpticksGenstep_::IsScintillation(OpticksGenstep_SCINTILLATION));
    assert(OpticksGenstep_::GenstepToPhotonFlag(OpticksGenstep_CERENKOV) == CERENKOV);
    assert(OpticksGenstep_::GenstepToPhotonFlag(OpticksGenstep_SCINTILLATION) == SCINTILLATION);
}

void test_launch_capacity()
{
    std::vector<sslice>       slices;
    const std::unique_ptr<NP> fitting(SGenstep::MakeTestArray(std::vector<int>{750, 750}));
    const size_t              count = SGenstep::GetGenstepSlices(slices, fitting.get(), 1000);
    if (count != 1500 || slices.size() != 2 || !slices[0].matches(0, 1, 0, 750) || !slices[1].matches(1, 2, 750, 750))
        throw std::runtime_error("Expected two 750-photon launches with a 1000-slot capacity");

    const std::unique_ptr<NP> oversized(SGenstep::MakeTestArray(std::vector<int>{10000}));
    slices.clear();
    try
    {
        SGenstep::GetGenstepSlices(slices, oversized.get(), 1000);
    }
    catch (const std::length_error&)
    {
        return;
    }
    throw std::runtime_error("Expected a 10000-photon genstep to be rejected for a 1000-slot launch");
}

int main()
{
    test_retired_codes();
    test_current_optical_codes();
    test_launch_capacity();
    return 0;
}
