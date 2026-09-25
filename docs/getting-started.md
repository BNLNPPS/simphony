# Getting started

This guide walks through the main ways to set up, build, test, and run Simphony.
The Dev Container is the easiest place to start because it brings the complete
toolchain with it. If you already manage the dependencies yourself, you can
skip ahead to [building directly on the host](#build-directly-on-the-host). The
later sections cover published Docker and Apptainer/Singularity images and a
test job on NERSC Perlmutter.

## Develop and test with the Dev Container

The repository's [Dev Container
configuration](../.devcontainer/devcontainer.json) gives you a consistent
development environment without installing CUDA, OptiX, or Geant4 directly on
the host.

### Install the host tools

Before you begin, make sure [Docker
Engine](https://docs.docker.com/engine/install/) is installed. Then install [Dev
Container CLI](https://github.com/devcontainers/cli) 0.82 or newer with npm:

```shell
npm install -g @devcontainers/cli
```

If Node.js and npm are not installed, use the [standalone
installer](https://github.com/devcontainers/cli). You only need
a CUDA-capable NVIDIA GPU and the [NVIDIA Container
Toolkit](https://docs.nvidia.com/datacenter/cloud-native/container-toolkit/latest/install-guide.html)
when you are ready to run GPU-backed code. The Dev Container CLI detects the
NVIDIA runtime and requests GPU access automatically.

### Get the source

Clone the repository and enter the checkout:

```shell
git clone https://github.com/BNLNPPS/simphony.git
cd simphony
```

If needed, switch to an existing branch with `git switch existing-branch`, or
create one with `git switch -c new-branch`.

### Choose dependency versions

If the default dependency versions meet your needs, skip this section and
continue to [Start the environment](#start-the-environment).

The `.devcontainer/.env.defaults` file contains the default OS and toolchain
used to build the Dev Container. To use another supported combination or
experiment with a new dependency version, put your overrides in the `.env.local`
file at the repository root. During `devcontainer up`, the initialization step
combines the defaults and local overrides into the `.env.compose` file that
Compose reads.

The defaults match the `base` alias in the [published container
matrix](../README.md#published-container-images). Start with another `base`
combination from this matrix when possible.

After editing `.env.local`, recreate the container so it uses the new values:

```shell
devcontainer up --remove-existing-container
```

When trying versions outside the matrix, first confirm that the corresponding
CUDA image and OptiX, Geant4, and CMake releases exist. Also check that the host
NVIDIA driver supports the selected CUDA version. The first build may take
longer because a matching published build cache may not be available.

### Start the environment

From the repository root, start the environment and open a shell:

```shell
devcontainer up
devcontainer exec bash
```

The first `devcontainer up` builds the environment. Later runs reuse the image,
so getting back to work is much faster.

Also recreate the container after changing the `Dockerfile` or `.devcontainer`
configuration. This is useful when you want to discard container-local state
and start fresh:

```shell
devcontainer up --remove-existing-container
```

You do not need to recreate the container after switching source branches. The
checkout remains mounted directly into the environment.

The default Compose project name includes the system user and checkout
directory name, keeping users and checkouts from colliding when they share a
Docker daemon. To use a different name, append it to `.env.local` before
starting the container:

```shell
echo "COMPOSE_PROJECT_NAME=simphony-${USER}-my_cool_feature" >> .env.local
```

Replace `my_cool_feature` with a short unique name for that checkout.

This configuration also works with IDEs and tools that support the [Development
Container Specification](https://containers.dev/). When using VS Code, its Dev
Containers extension also installs the recommended extensions.

### Build and test

Once the shell opens, configure, build, and run the full test suite:

```shell
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build
```

Your source and build files stay in the host checkout, so they remain available
after the container stops. For a quicker iteration loop, list the tests, build
only the target you are changing, and run a relevant test:

```shell
ctest --test-dir build -N
cmake --build build --target simg4ox
ctest --test-dir build -R raindrop
ctest --test-dir build -R '^Integration\.simg4ox_multithread$'
```

The `simg4ox_multithread` integration test runs five optical-photon events
with two Geant4 CPU workers and validates the merged CPU/GPU hit arrays. See
[the simg4ox example](../examples/README.md#example-4-simg4ox-g4--gpu-validation)
for serial and MT command lines and the supplied `tests/run_mt.mac` macro.

The full suite includes GPU-backed tests. You can build without a GPU, but
running those tests requires a compatible NVIDIA driver and GPU access from the
container.

## Build directly on the host

If you prefer to manage the toolchain yourself, install:

- CUDA 12.1+
- NVIDIA OptiX 7+
- Geant4 11.3+
- CMake 3.22+
- Python 3.10+

Build Geant4 with multithreading enabled to use `simg4ox --threads N` with
`N>1`. The project container images already use a multithreaded Geant4 build;
`--threads 1` remains available for serial validation.

With those dependencies available, clone, build, and test the project:

```shell
git clone https://github.com/BNLNPPS/simphony.git
cd simphony
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build
```

Before running GPU workloads, make sure the installed driver supports your
OptiX version. The minimum driver versions are:

| OptiX version | Release date  | Minimum driver required |
|---            |---:           |---                      |
| 9.1.0         | December 2025 | 590                     |
| 9.0.0         | February 2025 | 570                     |
| 8.1.0         | October 2024  | 555                     |
| 8.0.0         | August 2023   | 535                     |
| 7.7.0         | March 2023    | 530.41                  |
| 7.6.0         | October 2022  | 522.25                  |
| 7.5.0         | June 2022     | 515.48                  |
| 7.4.0         | November 2021 | 495.89                  |
| 7.3.0         | April 2021    | 465.84                  |
| 7.2.0         | October 2020  | 455.28                  |
| 7.1.0         | June 2020     | 450                     |
| 7.0.0         | August 2019   | 435.80                  |

See NVIDIA's [OptiX download
page](https://developer.nvidia.com/designworks/optix/downloads/legacy) for the
release details.

## Run with Docker

To try the latest published release and confirm that GPU access works:

```shell
docker run --rm --gpus all ghcr.io/bnlnpps/simphony simg4ox -g tests/geom/raindrop.gdml -m tests/run.mac
```

This uses the default serial run manager. To exercise Geant4 MT in the
container, use `tests/run_mt.mac` and append `--threads N`.

To test an image built from your current checkout instead:

```shell
docker build -t simphony:develop .
docker run --rm --gpus all simphony:develop simg4ox -g tests/geom/raindrop.gdml -m tests/run.mac
```

As with the published image, select `tests/run_mt.mac` and pass `--threads N`
for an MT run.

For day-to-day development, the Dev Container is more convenient because source
edits are available immediately without rebuilding the image.

## Run with Apptainer or Singularity

On systems that provide Apptainer, run the same published release with:

```shell
apptainer exec --nv docker://ghcr.io/bnlnpps/simphony simg4ox -g /workspaces/simphony/tests/geom/raindrop.gdml -m /workspaces/simphony/tests/run.mac
```

For MT, use the absolute macro path
`/workspaces/simphony/tests/run_mt.mac` and add `--threads N`.

Use `singularity` in place of `apptainer` on systems that provide the older
command name.
