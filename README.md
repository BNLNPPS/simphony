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

### Build from source

If CUDA 12.1+, NVIDIA OptiX 7+, Geant4 11.3+, CMake 3.22+, and Python 3.10+
are already installed, you can build and test Simphony directly:

```shell
git clone https://github.com/BNLNPPS/simphony.git
cd simphony
cmake -S . -B build
cmake --build build
ctest --test-dir build
```

You can edit and build without a GPU. Running Simphony and its GPU-backed tests
requires a CUDA-capable NVIDIA GPU.

### Use the Dev Container

For a ready-made environment, first install [Docker
Engine](https://docs.docker.com/engine/install/). Then install the [Dev Container
CLI](https://github.com/devcontainers/cli) and start the environment:

```shell
npm install -g @devcontainers/cli
git clone https://github.com/BNLNPPS/simphony.git
cd simphony
devcontainer up
devcontainer exec bash
```

The source tree is mounted into the container. Once inside, use the same CMake
commands above and rerun only the relevant build and tests as you work.

The `.devcontainer/.env.defaults` file selects the default OS and toolchain
versions. See [Choose dependency
versions](docs/getting-started.md#choose-dependency-versions) to override them
locally.

To run GPU-backed code, install the [NVIDIA Container
Toolkit](https://docs.nvidia.com/datacenter/cloud-native/container-toolkit/latest/install-guide.html)
on the host. The Dev Container CLI detects the NVIDIA runtime and requests GPU
access automatically.

### Try a published image

To take the latest release for a quick test drive, use Docker or Apptainer:

```shell
docker run --rm --gpus all ghcr.io/bnlnpps/simphony simg4ox -g tests/geom/raindrop.gdml -m tests/run.mac
apptainer exec --nv docker://ghcr.io/bnlnpps/simphony simg4ox -g /workspaces/simphony/tests/geom/raindrop.gdml -m /workspaces/simphony/tests/run.mac
```

### Install with Spack

Simphony is also available through the BNLNPPS Spack repository:

```shell
spack repo add https://github.com/BNLNPPS/spack-packages
spack install simphony
```

See [Getting started](docs/getting-started.md) for targeted test commands,
container lifecycle tips, native builds, and NERSC usage.

## Documentation

- [Getting started](docs/getting-started.md)
- [Physics](docs/physics.md)
- [Simulation inputs and outputs](docs/inputs-outputs.md)
- [Performance and debugging](docs/performance-and-debugging.md)
- [Geometry guidance](docs/geometry-requirements.md)
- [Examples](examples/README.md)

## Published container images

The project publishes reusable `base` images for development environments and
Docker build-cache warmup, along with versioned `release` and `develop` images.
Pushes to `main` update the versioned tags, while tagged releases update the
`latest` alias. Every tag below links to the [Simphony package
page](https://github.com/BNLNPPS/simphony/pkgs/container/simphony).

| Target | OS | CUDA | OptiX | Geant4 | CMake | Alias | Tag |
|---|---|---:|---:|---:|---:|---|---|
| `base` | `ubuntu26.04` | `13.3.0` | `9.1.0` | `11.4.2` | `4.3.4` | | [cuda13.3.0-base-ubuntu26.04-optix9.1.0-geant411.4.2-cmake4.3.4](https://github.com/BNLNPPS/simphony/pkgs/container/simphony) |
| `base` | `ubuntu24.04` | `13.0.3` | `9.0.0` | `11.4.2` | `4.2.1` | `base` | [cuda13.0.3-base-ubuntu24.04-optix9.0.0-geant411.4.2-cmake4.2.1](https://github.com/BNLNPPS/simphony/pkgs/container/simphony) |
| `base` | `ubuntu24.04` | `12.5.1` | `9.0.0` | `11.4.1` | `3.28.3` | | [cuda12.5.1-base-ubuntu24.04-optix9.0.0-geant411.4.1-cmake3.28.3](https://github.com/BNLNPPS/simphony/pkgs/container/simphony) |
| `base` | `ubuntu22.04` | `12.1.1` | `8.0.0` | `11.3.2` | `3.22.1` | | [cuda12.1.1-base-ubuntu22.04-optix8.0.0-geant411.3.2-cmake3.22.1](https://github.com/BNLNPPS/simphony/pkgs/container/simphony) |
| `release` | `ubuntu26.04` | `13.3.0` | `9.1.0` | `11.4.2` | `4.3.4` | | [cuda13.3.0-release-ubuntu26.04-optix9.1.0-geant411.4.2-cmake4.3.4](https://github.com/BNLNPPS/simphony/pkgs/container/simphony) |
| `release` | `ubuntu24.04` | `13.0.3` | `9.0.0` | `11.4.2` | `4.2.1` | `latest` | [cuda13.0.3-release-ubuntu24.04-optix9.0.0-geant411.4.2-cmake4.2.1](https://github.com/BNLNPPS/simphony/pkgs/container/simphony) |
| `release` | `ubuntu22.04` | `12.1.1` | `8.0.0` | `11.3.2` | `3.22.1` | | [cuda12.1.1-release-ubuntu22.04-optix8.0.0-geant411.3.2-cmake3.22.1](https://github.com/BNLNPPS/simphony/pkgs/container/simphony) |
| `develop` | `ubuntu24.04` | `13.0.3` | `9.0.0` | `11.4.2` | `4.2.1` | `develop` | [cuda13.0.3-develop-ubuntu24.04-optix9.0.0-geant411.4.2-cmake4.2.1](https://github.com/BNLNPPS/simphony/pkgs/container/simphony) |
| `develop` | `ubuntu24.04` | `12.5.1` | `9.0.0` | `11.4.1` | `3.28.3` | | [cuda12.5.1-develop-ubuntu24.04-optix9.0.0-geant411.4.1-cmake3.28.3](https://github.com/BNLNPPS/simphony/pkgs/container/simphony) |
| `develop` | `ubuntu22.04` | `12.1.1` | `8.0.0` | `11.3.2` | `3.22.1` | | [cuda12.1.1-develop-ubuntu22.04-optix8.0.0-geant411.3.2-cmake3.22.1](https://github.com/BNLNPPS/simphony/pkgs/container/simphony) |
