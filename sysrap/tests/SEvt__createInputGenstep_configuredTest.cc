#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <unistd.h>

#include "SEvent.hh"
#include "SEvt.hh"
#include "config.h"

int main()
{
    namespace fs = std::filesystem;
    const fs::path directory = fs::temp_directory_path() /
                               ("ConfiguredGenstepTest-" + std::to_string(::getpid()));
    fs::create_directories(directory);
    std::ofstream(directory / "torch.json") << R"json({
      "event": {"running_mode": "SRM_TORCH", "start_index": 11, "maxslot": 1000},
      "torch": {
        "numphoton": 7, "trackid": 2, "matline": 3,
        "pos": [1, 2, 3], "time": 4,
        "mom": [0, 0, 1], "weight": 0.5,
        "pol": [0, 1, 0], "wavelength": 555,
        "zenith": [0.25, 0.75], "azimuth": [0.1, 0.9],
        "radius": 8, "distance": 9, "mode": 10, "type": "point"
      }
    })json";
    std::ofstream(directory / "defaults.json") << "{}\n";
    setenv("SIMPHONY_CONFIG_DIR", directory.c_str(), 1);

    // These diagnostic controls must not override the selected JSON torch.
    setenv("SEvent__MakeGenstep_num_ph", "999", 1);
    setenv("SEvent__MakeGenstep_num_gs", "3", 1);
    setenv("storch_FillGenstep_pos", "9,9,9", 1);
    setenv("storch_FillGenstep_wavelength", "999", 1);

    const simphony::Config initial("torch");
    const simphony::Config defaults("defaults");
    std::unique_ptr<NP>    default_genstep(SEvent::MakeTorchGenstep());
    if (!default_genstep || !default_genstep->has_shape(1, 6, 4) ||
        std::memcmp(default_genstep->bytes(), &default_torch, sizeof(storch)) != 0)
        throw std::runtime_error("Omitted JSON torch did not restore the typed default");

    const simphony::Config config("torch");
    SEvt*                  sev = SEvt::Create_EGPU();
    for (int index = 0; index < 2; ++index)
    {
        sev->setIndex(index);
        std::unique_ptr<NP> gs(sev->createInputGenstep_configured());
        if (!gs || !gs->has_shape(1, 6, 4))
            throw std::runtime_error("Configured torch did not produce one genstep");

        const auto* torch = reinterpret_cast<const storch*>(gs->cvalues<float>());
        if (torch->numphoton != 7 || torch->pos.x != 1.f || torch->pos.y != 2.f ||
            torch->pos.z != 3.f || torch->wavelength != 555.f ||
            std::memcmp(torch, &config.torch, sizeof(storch)) != 0)
            throw std::runtime_error("Generated torch differs from the selected JSON source");
        if (gs->get_meta<int>("index_arg", -1) != index ||
            gs->get_meta<int64_t>("num_ph", -1) != 7 ||
            gs->get_meta<int64_t>("num_gs", -1) != 1)
            throw std::runtime_error("Torch genstep event metadata is incorrect");

        const char*    fold = std::getenv("FOLD");
        const fs::path output_dir = fold && *fold ? fs::path(fold) : directory;
        fs::create_directories(output_dir);
        const fs::path output = output_dir / "gs.npy";
        gs->save(output.c_str());
        std::unique_ptr<NP> saved(NP::Load(output.c_str()));
        if (!saved || !saved->has_shape(1, 6, 4) ||
            std::memcmp(saved->bytes(), &config.torch, sizeof(storch)) != 0)
            throw std::runtime_error("Saved genstep differs from the configured torch");
    }
    fs::remove_all(directory);
    return 0;
}
