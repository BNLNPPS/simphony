#include <iostream>
#include <string>
#include <vector>

#include "simphony/g4cx/G4CXOpticks.hh"
#include "simphony/sysrap/NP.hh"
#include "simphony/sysrap/sphoton.h"

#include "simphony/config.h"
#include "simphony/torch.h"

using namespace std;

int main(int argc, char **argv)
{
    gphox::Config config("dev");

    cout << config.torch.desc() << endl;

    vector<sphoton> phs = generate_photons(config.torch);

    size_t num_floats = phs.size() * 4 * 4;
    float *data = reinterpret_cast<float *>(phs.data());
    NP *photons = NP::MakeFromValues<float>(data, num_floats);

    photons->reshape({static_cast<int64_t>(phs.size()), 4, 4});
    photons->dump();
    photons->save("out/photons.npy");

    return EXIT_SUCCESS;
}
