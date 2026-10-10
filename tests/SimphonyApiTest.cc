#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

#include "G4GDMLParser.hh"

#include "simphony.h"

namespace
{
struct TranslationFailure : std::runtime_error
{
    TranslationFailure() :
        std::runtime_error("injected sensor translation failure")
    {
    }
};

struct SensorIdentifier : simphony::SensorIdentifier
{
    int  calls = 0;
    bool fail_translation = false;

    void setLevel(int) override
    {
    }

    int getGlobalIdentity(const G4VPhysicalVolume*, const G4VPhysicalVolume*) override
    {
        ++calls;
        if (fail_translation)
            throw TranslationFailure();
        return -1;
    }

    int getInstanceIdentity(const G4VPhysicalVolume*) const override
    {
        return -1;
    }
};

void require(bool condition, const std::string& message)
{
    if (!condition)
        throw std::runtime_error(message);
}

template <typename Exception, typename Function>
void require_throws(Function&& function, const std::string& message)
{
    try
    {
        function();
    }
    catch (const Exception&)
    {
        return;
    }
    throw std::runtime_error(message);
}

void require_negative_event_ids_rejected()
{
    require_throws<std::invalid_argument>(
        [] { simphony::simulate(-1); },
        "simulate(-1) did not throw std::invalid_argument");
    require_throws<std::invalid_argument>(
        [] { simphony::reset(-1); },
        "reset(-1) did not throw std::invalid_argument");
}
} // namespace

int main(int argc, char** argv)
{
    try
    {
        require(argc == 3 || (argc == 4 && std::string(argv[3]) == "--fail-translation"),
                "usage: SimphonyApiTest GEOMETRY.gdml OUTPUT_DIRECTORY [--fail-translation]");

        require(!simphony::is_initialized(), "API unexpectedly initialized at process start");
        require(!simphony::has_gpu(), "API unexpectedly has a GPU backend at process start");
        require_throws<std::logic_error>(
            [] { simphony::simulate(0); },
            "simulate before initialize did not throw std::logic_error");
        require_throws<std::invalid_argument>(
            [] { simphony::initialize(nullptr); },
            "initialize(nullptr) did not throw std::invalid_argument");
        require(!simphony::is_initialized(), "failed initialization changed API state");
        require_negative_event_ids_rejected();

        G4GDMLParser parser;
        parser.Read(argv[1], false);
        const G4VPhysicalVolume* world = parser.GetWorldVolume();
        require(world != nullptr, "GDML parser returned a null world");

        SensorIdentifier                identifier;
        const simphony::GeometryOptions options{
            .sensor_identifier = &identifier,
            .gpu = simphony::GpuRequirement::Disabled,
        };

        if (argc == 4)
        {
            identifier.fail_translation = true;
            require_throws<TranslationFailure>(
                [&] { simphony::initialize(world, options); },
                "initialize did not propagate the translation failure");
            require(!simphony::is_initialized() && !simphony::has_gpu(),
                    "failed translation published an initialized runtime");

            identifier.fail_translation = false;
            require_throws<std::logic_error>(
                [&] { simphony::initialize(world, options); },
                "retry after partial geometry translation did not throw std::logic_error");
            auto changed_options = options;
            changed_options.sensor_identifier = nullptr;
            changed_options.gpu = simphony::GpuRequirement::Required;
            require_throws<std::logic_error>(
                [&] { simphony::initialize(world, changed_options); },
                "retry with different options after partial translation was not rejected");
            require(!simphony::is_initialized() && !simphony::has_gpu(),
                    "rejected retry changed the runtime state");
            return 0;
        }

        simphony::initialize(world, options);

        require(identifier.calls > 0, "custom sensor identifier was not used during translation");
        require(simphony::is_initialized(), "initialize did not update API state");
        require(!simphony::has_gpu(), "Disabled policy unexpectedly created a GPU backend");
        require_negative_event_ids_rejected();

        simphony::initialize(world, options);
        require_throws<std::logic_error>(
            [] { simphony::simulate(7, false); },
            "simulate without a GPU backend did not throw std::logic_error");
        require_throws<std::logic_error>(
            [] { simphony::reset(7); },
            "reset without a pending event did not throw std::logic_error");

        auto changed_identifier = options;
        changed_identifier.sensor_identifier = nullptr;
        require_throws<std::logic_error>(
            [world, changed_identifier] { simphony::initialize(world, changed_identifier); },
            "reinitialization with a different sensor identifier did not throw std::logic_error");

        auto changed_options = options;
        changed_options.gpu = simphony::GpuRequirement::Optional;
        require_throws<std::logic_error>(
            [world, changed_options] { simphony::initialize(world, changed_options); },
            "reinitialization with changed options did not throw std::logic_error");

        const std::filesystem::path output = argv[2];
        std::filesystem::remove_all(output);
        simphony::save_geometry(output);

        require(std::filesystem::is_regular_file(output / "origin.gdml"),
                "save_geometry did not write origin.gdml");
        require(std::filesystem::is_regular_file(output / "CSGFoundry" / "solid.npy"),
                "save_geometry did not write CSGFoundry/solid.npy");
    }
    catch (const std::exception& error)
    {
        std::cerr << "SimphonyApiTest: " << error.what() << '\n';
        return 1;
    }

    return 0;
}
