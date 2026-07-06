Vendored NVIDIA OptiX Toolkit headers.

Source repository: <https://github.com/NVIDIA/optix-toolkit>

Pinned upstream release:
<https://github.com/NVIDIA/optix-toolkit/releases/tag/v1.3.0>

Pinned upstream tag:
`v1.3.0`

Compatibility backport:

`OptiXToolkit/ShaderUtil/vec_math.h` includes a local backport of upstream
commit `7acd88bc272a23067a36dc396ca835db145882fd`, which guards CUDA 13-only
`_16a` vector helpers behind `CUDART_VERSION >= 13000`. Without this guard,
CUDA 12 builds fail because types such as `double4_16a` are not defined.

Backport reference:
<https://github.com/NVIDIA/optix-toolkit/commit/7acd88bc272a23067a36dc396ca835db145882fd>

Backport commit:
`7acd88bc272a23067a36dc396ca835db145882fd`

Vendored files:

| Local file | Upstream file |
| --- | --- |
| `OptiXToolkit/ShaderUtil/vec_math.h` | `ShaderUtil/include/OptiXToolkit/ShaderUtil/vec_math.h` |
| `OptiXToolkit/ShaderUtil/Preprocessor.h` | `ShaderUtil/include/OptiXToolkit/ShaderUtil/Preprocessor.h` |
| `LICENSE.txt` | `LICENSE.txt` |

`Preprocessor.h` and `LICENSE.txt` match the pinned upstream release
byte-for-byte. `vec_math.h` is otherwise from the pinned upstream release plus
the CUDA 12 compatibility guard described above.

License:

The upstream repository is published under `BSD-3-Clause`. The vendored
headers include SPDX license identifiers, and the upstream `LICENSE.txt` is
included here to preserve the BSD-3 license conditions and disclaimer required
for redistribution.

Simphony compatibility:

Project-local compatibility code is kept in `sysrap/scuda.h`. That header
includes `OptiXToolkit/ShaderUtil/vec_math.h` and retains only the local API
surface that is not provided by the upstream OptiX Toolkit header. For CUDA 12,
it also restores the legacy narrowing overloads `make_float2(float3)`,
`make_float2(float4)`, and `make_float3(float4)` because the compatibility
backport guards those overloads together with adjacent CUDA 13-only `_16a`
helpers.
