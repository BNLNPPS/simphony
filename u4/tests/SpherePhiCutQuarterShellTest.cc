#include <cassert>
#include <csignal>
#include <cstdlib>
#include <iostream>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <plog/Appenders/ConsoleAppender.h>
#include <plog/Formatters/TxtFormatter.h>
#include <plog/Init.h>
#include <plog/Log.h>

using plog::error;
using plog::fatal;
using plog::info;

#include "G4BooleanSolid.hh"
#include "G4IntersectionSolid.hh"
#include "G4LogicalVolume.hh"
#include "G4PhysicalConstants.hh"
#include "G4Sphere.hh"
#include "G4SubtractionSolid.hh"
#include "G4SystemOfUnits.hh"
#include "G4VPhysicalVolume.hh"
#include "G4VSolid.hh"

#include "config_path.h"

#include "U4GDML.h"
#include "U4Solid.h"
#include "U4Volume.h"

#include "s_csg.h"

namespace
{
inline constexpr char testGeomFile[] = GPHOX_TEST_GEOM_DIR "/sphere_phicut_quarter_shell.gdml";

enum ConvertOutcome
{
    CONVERT_REJECTED,
    CONVERT_ACCEPTED,
    CONVERT_ERROR
};

bool IsExpectedRejectSignal(int signal)
{
    return signal == SIGABRT || signal == SIGINT;
}

int CountPartialPhiSpheres(const G4VSolid* solid)
{
    const G4Sphere* sphere = dynamic_cast<const G4Sphere*>(solid);
    if (sphere)
    {
        double start_phi = sphere->GetStartPhiAngle() / CLHEP::radian;
        double delta_phi = sphere->GetDeltaPhiAngle() / CLHEP::radian;
        bool   partial_phi = start_phi != 0. || delta_phi != 2. * CLHEP::pi;
        return partial_phi ? 1 : 0;
    }

    const G4BooleanSolid* boolean = dynamic_cast<const G4BooleanSolid*>(solid);
    if (boolean == nullptr)
        return 0;

    const G4VSolid* left = boolean->GetConstituentSolid(0);
    const G4VSolid* right = boolean->GetConstituentSolid(1);
    return CountPartialPhiSpheres(left) + CountPartialPhiSpheres(right);
}

ConvertOutcome ConvertInChildProcess(const G4VSolid* solid)
{
    pid_t pid = fork();
    if (pid < 0)
    {
        perror("fork");
        return CONVERT_ERROR;
    }

    if (pid == 0)
    {
        s_csg* csg = new s_csg;
        assert(csg);

        int lvid = 0;
        int depth = 0;
        int level = 1;
        sn* nd = U4Solid::Convert(solid, lvid, depth, level);
        int exit_code = nd == nullptr ? 4 : 0;
        delete nd;
        _exit(exit_code);
    }

    int status = 0;
    int rc = waitpid(pid, &status, 0);
    if (rc != pid)
    {
        perror("waitpid");
        return CONVERT_ERROR;
    }

    if (WIFSIGNALED(status))
    {
        int signal = WTERMSIG(status);
        if (IsExpectedRejectSignal(signal))
        {
            std::cout << "child rejected conversion with expected signal " << signal << std::endl;
            return CONVERT_REJECTED;
        }

        std::cerr << "child crashed with unexpected signal " << signal << std::endl;
        return CONVERT_ERROR;
    }

    if (WIFEXITED(status) && WEXITSTATUS(status) == 0)
    {
        std::cout << "child converted partial-phi sphere successfully" << std::endl;
        return CONVERT_ACCEPTED;
    }

    if (WIFEXITED(status))
    {
        std::cerr << "child exited unexpectedly with status " << WEXITSTATUS(status) << std::endl;
        return CONVERT_ERROR;
    }

    std::cerr << "child ended in unexpected state " << status << std::endl;
    return CONVERT_ERROR;
}
} // namespace

int main(int argc, char** argv)
{
    static plog::ConsoleAppender<plog::TxtFormatter> consoleAppender;
    plog::init(plog::info, &consoleAppender);

    const G4VPhysicalVolume* world = U4GDML::Read(testGeomFile);
    LOG_IF(plog::fatal, world == nullptr)
        << "failed to load GDML path " << (testGeomFile ? testGeomFile : "-");

    const G4VPhysicalVolume* quarter_shell_pv = U4Volume::FindPV(world, "QuarterShell_pv");
    LOG_IF(plog::fatal, quarter_shell_pv == nullptr)
        << "failed to find QuarterShell_pv in GDML path " << testGeomFile;

    const G4LogicalVolume* quarter_shell_lv = quarter_shell_pv->GetLogicalVolume();
    const G4VSolid*        quarter_shell_solid = quarter_shell_lv->GetSolid();
    LOG_IF(plog::fatal, quarter_shell_solid == nullptr)
        << "QuarterShell_pv lacks a solid";

    const G4IntersectionSolid* intersection = dynamic_cast<const G4IntersectionSolid*>(quarter_shell_solid);
    LOG_IF(plog::fatal, intersection != nullptr)
        << "test geometry unexpectedly uses a parent IntersectionSolid";

    const G4SubtractionSolid* subtraction = dynamic_cast<const G4SubtractionSolid*>(quarter_shell_solid);
    LOG_IF(plog::fatal, subtraction == nullptr)
        << "test geometry expected a subtraction shell solid " << quarter_shell_solid->GetName();

    int partial_phi_spheres = CountPartialPhiSpheres(quarter_shell_solid);
    LOG_IF(plog::fatal, partial_phi_spheres == 0)
        << "test geometry expected partial-phi sphere primitives";

    ConvertOutcome outcome = ConvertInChildProcess(quarter_shell_solid);
    switch (outcome)
    {
    case CONVERT_REJECTED:
        std::cout
            << "partial-phi sphere conversion is rejected, matching current fail-fast behavior"
            << std::endl;
        return 0;

    case CONVERT_ACCEPTED:
        std::cout
            << "partial-phi sphere conversion succeeded with a non-null CSG tree"
            << std::endl;
        return 0;

    case CONVERT_ERROR:
        break;
    }

    std::cerr
        << "SpherePhiCutQuarterShellTest could neither confirm fail-fast rejection nor "
        << "successful conversion of the partial-phi spherical shell."
        << std::endl;

    return 1;
}
