#pragma once
/**
qwls.h : GPU-side Wavelength Shifting
=========================================

Device-side struct for WLS wavelength sampling via ICDF texture lookup.
Supports multiple WLS materials indexed by material ID.

The ICDF texture layout:
- Each WLS material occupies 3 rows (standard, LHS HD, RHS HD)
- material_map[mat_idx] gives the base row for that material (-1 = no WLS)
- time_constants[wls_idx] gives the re-emission time constant in ns

Wavelength sampling uses the same HD (high-definition) technique as qscint.h:
- hd_factor=20: 20x resolution at extremes (u < 0.05 or u > 0.95)
- Normalized texture coordinates with linear interpolation

**/

#if defined(__CUDACC__) || defined(__CUDABE__)
#define QWLS_METHOD __device__
#else
#define QWLS_METHOD
#endif

struct qwls
{
    cudaTextureObject_t wls_tex; // ICDF texture: (num_wls*3, 4096, 1)
    unsigned hd_factor;          // 0, 10, or 20
    int *material_map;           // device ptr: mat_idx -> base ICDF row (-1 = no WLS)
    float *time_constants;       // device ptr: per-WLS-material time constant (ns)
    unsigned num_wls;            // number of WLS materials
    unsigned tex_height;         // total rows in texture = num_wls * 3

#if defined(__CUDACC__) || defined(__CUDABE__) || defined(MOCK_CURAND) || defined(MOCK_CUDA)

    QWLS_METHOD bool has_wls(unsigned mat_idx) const;
    QWLS_METHOD float wavelength(unsigned mat_idx, const float &u0) const;
    QWLS_METHOD float wavelength_at_row(unsigned base_row, const float &u0) const;
    QWLS_METHOD float time_constant(unsigned mat_idx) const;

#endif
};

#if defined(__CUDACC__) || defined(__CUDABE__) || defined(MOCK_CURAND) || defined(MOCK_CUDA)

/**
qwls::has_wls
---------------

Returns true if material at mat_idx has WLS properties.
The material_map holds -1 for non-WLS materials.

**/

inline QWLS_METHOD bool qwls::has_wls(unsigned mat_idx) const
{
    return material_map[mat_idx] >= 0;
}

/**
qwls::time_constant
---------------------

Returns the WLS re-emission time constant in ns for the given material.
Returns 0.f if material has no WLS (instant re-emission / no delay).

**/

inline QWLS_METHOD float qwls::time_constant(unsigned mat_idx) const
{
    int base_row = material_map[mat_idx];
    if (base_row < 0)
        return 0.f;
    unsigned wls_idx = base_row / 3;
    return time_constants[wls_idx];
}

/**
qwls::wavelength
-------------------

Sample a re-emitted wavelength from the WLS emission spectrum ICDF
for the material at mat_idx, using uniform random u0 in [0,1).

Returns 0.f if material has no WLS (should not happen in practice
as callers check has_wls first).

**/

inline QWLS_METHOD float qwls::wavelength(unsigned mat_idx, const float &u0) const
{
    int base_row = material_map[mat_idx];
    if (base_row < 0)
        return 0.f;
    return wavelength_at_row(unsigned(base_row), u0);
}

/**
qwls::wavelength_at_row
--------------------------

ICDF texture lookup with HD (high-definition) support.
base_row is the first of 3 rows for this WLS material:
  row 0: standard resolution  (full CDF range)
  row 1: LHS high-res         (0.00 -> 0.05 for hd_factor=20)
  row 2: RHS high-res         (0.95 -> 1.00 for hd_factor=20)

Uses normalized texture coordinates with linear interpolation,
matching the qscint.h implementation.

**/

inline QWLS_METHOD float qwls::wavelength_at_row(unsigned base_row, const float &u0) const
{
    float y0 = (float(base_row) + 0.5f) / float(tex_height);
    float y1 = (float(base_row + 1) + 0.5f) / float(tex_height);
    float y2 = (float(base_row + 2) + 0.5f) / float(tex_height);

    float wl;

    if (hd_factor == 0)
    {
        wl = tex2D<float>(wls_tex, u0, y0);
    }
    else if (hd_factor == 10)
    {
        if (u0 < 0.1f)
            wl = tex2D<float>(wls_tex, u0 * 10.f, y1);
        else if (u0 > 0.9f)
            wl = tex2D<float>(wls_tex, (u0 - 0.9f) * 10.f, y2);
        else
            wl = tex2D<float>(wls_tex, u0, y0);
    }
    else // hd_factor == 20
    {
        if (u0 < 0.05f)
            wl = tex2D<float>(wls_tex, u0 * 20.f, y1);
        else if (u0 > 0.95f)
            wl = tex2D<float>(wls_tex, (u0 - 0.95f) * 20.f, y2);
        else
            wl = tex2D<float>(wls_tex, u0, y0);
    }

    return wl;
}

#endif
