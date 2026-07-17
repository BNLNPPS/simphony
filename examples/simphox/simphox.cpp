#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "simphony/sysrap/NP.hh"
#include "simphony/sysrap/sphoton.h"

#include "simphony/sysrap/config.h"
#include "simphony/sysrap/torch.h"

using namespace std;

int main(int argc, char** argv)
{
    bool                   use_gpu = false;
    constexpr unsigned int seed = 42;
    unsigned int           num_photons = 0;

    for (int i = 1; i < argc; i++)
    {
        string arg = argv[i];
        if (arg == "--cpu")
        {
            use_gpu = false;
        }
        else if (arg == "--gpu")
        {
            use_gpu = true;
        }
        else if (arg == "--num-photons" && i + 1 < argc)
        {
            num_photons = static_cast<unsigned int>(stoul(argv[++i]));
        }
        else if (arg == "-h" || arg == "--help")
        {
            cout << "Usage: simphox [--cpu] [--gpu] [--num-photons N]" << endl;
            return EXIT_SUCCESS;
        }
        else
        {
            cerr << "Unknown argument: " << arg << endl;
            return EXIT_FAILURE;
        }
    }

    simphony::Config config("dev");

    cout << config.torch.desc() << endl;
    cout << "backend " << (use_gpu ? "gpu" : "cpu") << endl;
    cout << "seed " << seed << endl;

    vector<sphoton> phs;
    if (!use_gpu)
    {
        phs = generate_photons(config.torch, num_photons, seed);
    }
    else
    {
        try
        {
            phs = generate_photons_gpu(config.torch, num_photons, seed);
        }
        catch (const exception& err)
        {
            cerr << "GPU photon generation failed: " << err.what() << endl;
            return EXIT_FAILURE;
        }
    }

    size_t num_floats = phs.size() * 4 * 4;
    float* data = reinterpret_cast<float*>(phs.data());
    NP*    photons = NP::MakeFromValues<float>(data, num_floats);

    photons->reshape({static_cast<int64_t>(phs.size()), 4, 4});
    photons->dump();
    photons->save("out/photons.npy");

    return EXIT_SUCCESS;
}
