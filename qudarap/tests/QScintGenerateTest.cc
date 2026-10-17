#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "NP.hh"
#include "scuda.h"
#include "sphoton.h"
#include "squad.h"
#include "srngcpu.h"
#include "sscint.h"
using RNG = srngcpu;

#include "stexture.h"
MockTextureManager* MockTextureManager::INSTANCE = nullptr;

#include "qscint.h"

namespace
{
void require(bool condition, const char* expression, int line)
{
    if (condition)
        return;
    std::fprintf(stderr, "%s:%d requirement failed: %s\n", __FILE__, line, expression);
    std::exit(EXIT_FAILURE);
}

#define REQUIRE(expression) require((expression), #expression, __LINE__)

qscint makeScintillationSampler()
{
    NP* spectrum = NP::Make<float>(3, 4, 4);
    std::fill_n(spectrum->values<float>(), spectrum->num_values(), 420.f);

    qscint scintillation = {};
    scintillation.scint_tex = MockTextureManager::Add(spectrum);
    scintillation.hd_factor = 0;
    return scintillation;
}

void test_stationary_genstep_has_finite_time(const qscint& scintillation, RNG& rng)
{
    sscint genstep = {};
    genstep.pos = make_float3(1.f, 2.f, 3.f);
    genstep.time = 10.f;
    genstep.charge = -1.f;
    genstep.step_length = 0.f;
    genstep.meanVelocity = 0.f;
    genstep.ScintillationTime = 4.f;

    sphoton photon = {};
    scintillation.generate(photon, rng, reinterpret_cast<const quad6&>(genstep), 0, 0);

    REQUIRE(std::isfinite(photon.time));
    REQUIRE(photon.time > genstep.time);
}
} // namespace

int main()
{
    qscint scintillation = makeScintillationSampler();
    RNG    rng;
    rng.set_fake(0.5);

    test_stationary_genstep_has_finite_time(scintillation, rng);
    std::puts("QScintGenerateTest: PASS");
    return 0;
}
