# Simphony

[![Build](https://github.com/BNLNPPS/simphony/actions/workflows/build-push.yaml/badge.svg?branch=main)](https://github.com/BNLNPPS/simphony/actions/workflows/build-push.yaml)
[![Release](https://github.com/BNLNPPS/simphony/actions/workflows/release.yaml/badge.svg?event=push)](https://github.com/BNLNPPS/simphony/actions/workflows/release.yaml)
[![Latest Release](https://img.shields.io/github/v/release/BNLNPPS/simphony)](https://github.com/BNLNPPS/simphony/releases)
[![GHCR Package](https://img.shields.io/badge/GHCR-simphony-2088FF?logo=docker&logoColor=white)](https://github.com/BNLNPPS/simphony/pkgs/container/simphony)

Simphony is a GPU-accelerated optical photon transport framework that couples
NVIDIA OptiX with Geant4 for detector simulation workflows. It imports GDML
detector geometries, offloads optical photon propagation to NVIDIA GPUs, and
provides example applications for Cerenkov, scintillation, torch-driven, and
file-driven photon transport studies.

The project builds on Simon Blyth's original
[Opticks](https://simoncblyth.bitbucket.io/opticks/) work and adapts that
approach for current OptiX- and Geant4-based simulation workflows.


## Quick start

```shell
git clone https://github.com/BNLNPPS/simphony.git
cmake -S simphony -B build
cmake --build build
```

To build from source, install CUDA 12.1+, NVIDIA OptiX 7+, and Geant4 11.3+.
A CUDA-capable NVIDIA GPU is required only to run Simphony and its GPU-backed
examples.

Simphony can also be installed via Spack as a dependency:

```shell
spack repo add https://github.com/BNLNPPS/spack-packages
spack install simphony
```

To try the latest release image and verify that GPU-enabled code runs on your
machine, you can use either Docker or Apptainer/Singularity:

```shell
docker run --rm --gpus all ghcr.io/bnlnpps/simphony simg4ox -g tests/geom/raindrop.gdml -m tests/run.mac
apptainer exec --nv docker://ghcr.io/bnlnpps/simphony simg4ox -g /workspaces/simphony/tests/geom/opticks_raindrop.gdml -m /workspaces/simphony/tests/run.mac
```

For local development, you can build the `develop` image yourself or use the
repository's [devcontainer configuration](.devcontainer/devcontainer.json). On
a reasonably provisioned machine, building the `develop` image can take under 10
minutes. See [Getting started](docs/getting-started.md) for Docker, Singularity,
and development-environment details.

## Supported Images

The GitHub Actions build matrix currently publishes the following container
combinations to GHCR. Tag entries link to the
[Simphony package page](https://github.com/BNLNPPS/simphony/pkgs/container/simphony).

| Target | OS | CUDA | OptiX | Geant4 | Alias | Tag |
|---|---|---:|---:|---:|---|---|
| `release` | `ubuntu24.04` | `13.2.0` | `9.1.0` | `11.4.1` | | [cuda13.2.0-release-ubuntu24.04-optix9.1.0-geant411.4.1-cmake4.3.1](https://github.com/BNLNPPS/simphony/pkgs/container/simphony) |
| `release` | `ubuntu24.04` | `13.0.2` | `9.0.0` | `11.4.1` | `latest` | [cuda13.0.2-release-ubuntu24.04-optix9.0.0-geant411.4.1-cmake4.2.1](https://github.com/BNLNPPS/simphony/pkgs/container/simphony) |
| `release` | `ubuntu22.04` | `12.1.1` | `8.0.0` | `11.3.2` | | [cuda12.1.1-release-ubuntu22.04-optix8.0.0-geant411.3.2-cmake3.22.1](https://github.com/BNLNPPS/simphony/pkgs/container/simphony) |
| `develop` | `ubuntu24.04` | `13.0.2` | `9.0.0` | `11.4.1` | `develop` | [cuda13.0.2-develop-ubuntu24.04-optix9.0.0-geant411.4.1-cmake4.2.1](https://github.com/BNLNPPS/simphony/pkgs/container/simphony) |
| `develop` | `ubuntu24.04` | `12.5.1` | `9.0.0` | `11.4.1` | | [cuda12.5.1-develop-ubuntu24.04-optix9.0.0-geant411.4.1-cmake3.28.3](https://github.com/BNLNPPS/simphony/pkgs/container/simphony) |
| `develop` | `ubuntu22.04` | `12.1.1` | `8.0.0` | `11.3.2` | | [cuda12.1.1-develop-ubuntu22.04-optix8.0.0-geant411.3.2-cmake3.22.1](https://github.com/BNLNPPS/simphony/pkgs/container/simphony) |


## Documentation

- [Getting started](docs/getting-started.md)
- [Physics and simulation inputs](docs/physics-and-inputs.md)
- [Performance and debugging](docs/performance-and-debugging.md)
- [Examples](examples/README.md)
