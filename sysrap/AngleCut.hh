#pragma once

// Packed CSG parameters for azimuthal and polar angle cuts.
#include "SYSRAP_API_EXPORT.hh"
#include "plog/Severity.h"

union quad;

struct SYSRAP_API SPhiCut
{
    static const plog::Severity LEVEL;
    static void PrepareParam(quad& q0, double startPhi_pi, double deltaPhi_pi);
};

struct SYSRAP_API SThetaCut
{
    static const plog::Severity LEVEL;
    static void PrepareParam(quad& q0, quad& q1, double startTheta_pi, double deltaTheta_pi);
};
