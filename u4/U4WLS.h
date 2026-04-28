#pragma once
/**
U4WLS.h : Wavelength Shifting ICDF Creation
===============================================

Creates inverse CDF textures for wavelength shifting (WLS) materials,
analogous to U4Scint.h for scintillation. Supports multiple WLS materials
by stacking ICDF rows into a single texture.

For each material with a WLSCOMPONENT property:
1. Integrates the emission spectrum to get a CDF
2. Inverts it at 4096 uniformly-spaced CDF values (3 resolutions for HD)
3. Extracts WLSTIMECONSTANT from the material properties table

The output arrays:
- icdf: shape (num_wls_mat*3, 4096, 1) — stacked HD ICDF rows
- mat_map: shape (num_total_mat,) int — maps material index to WLS row (-1 = no WLS)
- time_constants: shape (num_wls_mat,) float — per-WLS-material time constant

The G4 WLS process (G4OpWLS) uses these material properties:
- WLSABSLENGTH: absorption length as f(energy) — handled via boundary texture
- WLSCOMPONENT: emission spectrum as f(energy) — converted to ICDF here
- WLSTIMECONSTANT: re-emission time delay (scalar) — extracted here

**/

#include <cassert>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

#include "G4Material.hh"
#include "G4MaterialPropertiesTable.hh"
#include "G4MaterialPropertyVector.hh"
#include "G4PhysicalConstants.hh"
#include "G4SystemOfUnits.hh"

#include "NP.hh"
#include "NPFold.h"
#include "SLOG.hh"
#include "U4MaterialPropertyVector.h"
#include "U4Scint.h" // reuse Integral and CreateGeant4InterpolatedInverseCDF

struct U4WLS
{
    static constexpr const char *WLSCOMPONENT_KEY = "WLSCOMPONENT";
    static constexpr const char *WLSTIMECONSTANT_KEY = "WLSTIMECONSTANT";

    static U4WLS *Create(const std::vector<const G4Material *> &mats);

    const NP *icdf;           // (num_wls*3, 4096, 1) stacked HD ICDF for all WLS materials
    const NP *mat_map;        // (num_total_mat,) int: material idx -> base ICDF row, or -1
    const NP *time_constants; // (num_wls,) float: time constant per WLS material

    unsigned num_wls;
    unsigned num_mat;

    U4WLS(const std::vector<const G4Material *> &mats, const std::vector<int> &wls_indices,
          const std::vector<const G4MaterialPropertyVector *> &wls_components,
          const std::vector<double> &wls_time_consts);

    std::string desc() const;
};

/**
U4WLS::Create
---------------

Scans all materials for WLSCOMPONENT property. For each material
that has it, extracts the emission spectrum and time constant.

Returns nullptr if no WLS materials are found.

**/

inline U4WLS *U4WLS::Create(const std::vector<const G4Material *> &mats)
{
    std::vector<int> wls_indices;
    std::vector<const G4MaterialPropertyVector *> wls_components;
    std::vector<double> wls_time_consts;

    for (unsigned i = 0; i < mats.size(); i++)
    {
        const G4Material *mat = mats[i];
        G4MaterialPropertiesTable *mpt = mat->GetMaterialPropertiesTable();
        if (mpt == nullptr)
            continue;

        G4MaterialPropertyVector *wlscomp = mpt->GetProperty(WLSCOMPONENT_KEY);
        if (wlscomp == nullptr)
            continue;

        // Found a WLS material
        wls_indices.push_back(i);
        wls_components.push_back(wlscomp);

        // Extract time constant (scalar property, default 0 = instant re-emission)
        double tc = 0.0;
        if (mpt->ConstPropertyExists(WLSTIMECONSTANT_KEY))
        {
            tc = mpt->GetConstProperty(WLSTIMECONSTANT_KEY) / ns; // convert to ns
        }
        wls_time_consts.push_back(tc);
    }

    if (wls_indices.empty())
        return nullptr;

    return new U4WLS(mats, wls_indices, wls_components, wls_time_consts);
}

/**
U4WLS::U4WLS
--------------

Builds the ICDF texture data and material mapping arrays.

For each WLS material:
1. Integrate WLSCOMPONENT to get CDF (reuses U4Scint::Integral)
2. Build 3-layer HD ICDF (reuses U4Scint::CreateGeant4InterpolatedInverseCDF)
3. Stack into combined ICDF array

**/

inline U4WLS::U4WLS(const std::vector<const G4Material *> &mats, const std::vector<int> &wls_indices,
                    const std::vector<const G4MaterialPropertyVector *> &wls_components,
                    const std::vector<double> &wls_time_consts) :
    icdf(nullptr),
    mat_map(nullptr),
    time_constants(nullptr),
    num_wls(wls_indices.size()),
    num_mat(mats.size())
{
    assert(num_wls > 0);
    assert(wls_components.size() == num_wls);
    assert(wls_time_consts.size() == num_wls);

    int num_bins = 4096;
    int hd_factor = 20;

    // Build per-material ICDFs and stack them
    std::vector<const NP *> icdfs;
    for (unsigned w = 0; w < num_wls; w++)
    {
        const G4MaterialPropertyVector *comp = wls_components[w];
        const G4Material *mat = mats[wls_indices[w]];
        const char *matname = mat->GetName().c_str();

        // Integrate emission spectrum to get CDF
        G4MaterialPropertyVector *integral = U4Scint::Integral(comp);

        // Build 3-layer HD ICDF (wavelength values in nm)
        NP *one_icdf = U4Scint::CreateGeant4InterpolatedInverseCDF(integral, num_bins, hd_factor, matname,
                                                                   false /*energy_not_wavelength*/
        );

        assert(one_icdf);
        assert(one_icdf->has_shape(3, num_bins, 1));
        icdfs.push_back(one_icdf);
    }

    // Stack all ICDFs into a single array: (num_wls*3, 4096, 1)
    {
        NP *stacked = NP::Make<double>(num_wls * 3, num_bins, 1);
        double *dst = stacked->values<double>();
        for (unsigned w = 0; w < num_wls; w++)
        {
            const double *src = icdfs[w]->cvalues<double>();
            unsigned row_size = 3 * num_bins * 1;
            memcpy(dst + w * row_size, src, row_size * sizeof(double));
        }
        stacked->set_meta<int>("hd_factor", hd_factor);
        stacked->set_meta<int>("num_bins", num_bins);
        stacked->set_meta<int>("num_wls", num_wls);
        icdf = stacked;
    }

    // Build material index -> ICDF row mapping
    // For material i, mat_map[i] = base row in ICDF texture (0, 3, 6, ...)
    // or -1 if material has no WLS
    {
        NP *mm = NP::Make<int>(num_mat);
        int *mm_v = mm->values<int>();
        for (unsigned i = 0; i < num_mat; i++)
            mm_v[i] = -1;
        for (unsigned w = 0; w < num_wls; w++)
        {
            mm_v[wls_indices[w]] = w * 3; // base row for this WLS material's 3 HD layers
        }
        mat_map = mm;
    }

    // Build time constants array (in ns)
    {
        NP *tc = NP::Make<float>(num_wls);
        float *tc_v = tc->values<float>();
        for (unsigned w = 0; w < num_wls; w++)
        {
            tc_v[w] = float(wls_time_consts[w]);
        }
        time_constants = tc;
    }
}

inline std::string U4WLS::desc() const
{
    std::stringstream ss;
    ss << "U4WLS::desc" << " num_wls " << num_wls << " num_mat " << num_mat << " icdf " << (icdf ? icdf->sstr() : "-")
       << " mat_map " << (mat_map ? mat_map->sstr() : "-") << " time_constants "
       << (time_constants ? time_constants->sstr() : "-");
    return ss.str();
}
