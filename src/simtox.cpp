#include <iostream>
#include <string>
#include <vector>

#include "sysrap/NP.hh"
#include "sysrap/SEvent.hh"
#include "sysrap/sphoton.h"
#include "sysrap/srng.h"
#include "sysrap/storch.h"
#include "sysrap/storchtype.h"

#include "config.h"
#include "torch.h"

#include <curand_kernel.h>

using namespace std;

int main(int argc, char **argv)
{
    simphony::Config config("dev");

    cout << config.torch.desc() << endl;

    vector<sphoton> phs = generate_photons(config.torch);

    size_t num_floats = phs.size()*4*4;
    float* data = reinterpret_cast<float*>(phs.data());
    NP* photons = NP::MakeFromValues<float>(data, num_floats);

    photons->reshape({ static_cast<int64_t>(phs.size()), 4, 4});
    photons->dump();
    photons->save("out/photons.npy");

    return EXIT_SUCCESS;
}
