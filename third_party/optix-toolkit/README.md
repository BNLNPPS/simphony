Vendored NVIDIA OptiX Toolkit headers.

Source repository: <https://github.com/NVIDIA/optix-toolkit>

Pinned upstream release:
<https://github.com/NVIDIA/optix-toolkit/releases/tag/v1.3.0>

Pinned upstream tag:
`v1.3.0`

Vendored files:

| Local file | Upstream file |
| --- | --- |
| `OptiXToolkit/ShaderUtil/vec_math.h` | `ShaderUtil/include/OptiXToolkit/ShaderUtil/vec_math.h` |
| `OptiXToolkit/ShaderUtil/Preprocessor.h` | `ShaderUtil/include/OptiXToolkit/ShaderUtil/Preprocessor.h` |
| `LICENSE.txt` | `LICENSE.txt` |

The headers were compared against the pinned upstream release after download.

License:

The upstream repository is published under `BSD-3-Clause`. The vendored
headers include SPDX license identifiers, and the upstream `LICENSE.txt` is
included here to preserve the BSD-3 license conditions and disclaimer required
for redistribution.

Simphony compatibility:

Project-local compatibility code is kept in `sysrap/scuda.h`. That header
includes `OptiXToolkit/ShaderUtil/vec_math.h` and retains only the local API
surface that is not provided by the upstream OptiX Toolkit header.
