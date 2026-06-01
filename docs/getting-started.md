# Getting started

This guide covers the system requirements, build steps, container workflows,
and a sample NERSC batch job for running Simphony.

## Prerequisites

Building from source requires the software stack listed below. Running
Simphony and the GPU-backed tests additionally requires a CUDA-capable NVIDIA
GPU.

- CUDA 12.1+
- NVIDIA OptiX 7+
- Geant4 11.3+
- CMake 3.18+
- Python 3.10+

OptiX releases have specific [minimum NVIDIA driver
requirements](https://developer.nvidia.com/designworks/optix/downloads/legacy):

| OptiX version | Release date  | Minimum driver required |
|---            |---:           |---                      |
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

Optionally, if you plan to develop or run the simulation in a containerized
environment, ensure that your system has the following tools installed:

- [Docker Engine](https://docs.docker.com/engine/install/)
- NVIDIA container toolkit ([installation guide](https://docs.nvidia.com/datacenter/cloud-native/container-toolkit/latest/install-guide.html))

## Build

```shell
git clone https://github.com/BNLNPPS/simphony.git
cmake -S simphony -B build
cmake --build build
```

## Docker

Try the latest published release image and verify that GPU-enabled code runs on
your machine:

```shell
docker run --rm --gpus all ghcr.io/bnlnpps/simphony bash -lc 'simg4ox -g tests/geom/raindrop.gdml -m tests/run.mac'
```

Build the latest `simphony` image by hand:

```shell
docker build -t ghcr.io/bnlnpps/simphony:latest https://github.com/BNLNPPS/simphony.git
```

Build the development image locally:

```shell
docker build -t ghcr.io/bnlnpps/simphony:develop --target=develop .
```

On a reasonably provisioned machine, building the `develop` image can take
under 10 minutes.

The repository also includes a ready-to-use devcontainer configuration in
`.devcontainer/devcontainer.json` for IDE-based development workflows.

Example commands for interactive and non-interactive tests:

```shell
docker run --rm -it -v $HOME/.Xauthority:/root/.Xauthority -e DISPLAY=$DISPLAY --net=host ghcr.io/bnlnpps/simphony:develop

docker run --rm -it -v $HOME:/esi -v $HOME/simphony:/workspaces/simphony -e DISPLAY=$DISPLAY -e HOME=/esi --net=host ghcr.io/bnlnpps/simphony:develop

docker run ghcr.io/bnlnpps/simphony bash -c 'simg4ox -g tests/geom/sphere_leak.gdml -m tests/run.mac -c sphere_leak'
```

## Singularity

Run the latest published release image with the same smoke test:

```shell
singularity exec --nv docker://ghcr.io/bnlnpps/simphony:latest bash -lc 'simg4ox -g tests/geom/raindrop.gdml -m tests/run.mac'
```

```shell
singularity run --nv -B simphony-prefix/:/opt/simphony -B simphony:/workspaces/simphony docker://ghcr.io/bnlnpps/simphony:develop
```

## Running a test job at NERSC (Perlmutter)

To submit a test run of `simphony` on Perlmutter, use the following example.
Update any placeholder values as needed.

```shell
sbatch scripts/submit.sh
```

```bash
#!/bin/bash

#SBATCH -N 1                    # number of nodes
#SBATCH -C gpu                  # constraint: use GPU partition
#SBATCH -G 1                    # request 1 GPU
#SBATCH -q regular              # queue
#SBATCH -J simphony             # job name
#SBATCH --mail-user=<USER_EMAIL>
#SBATCH --mail-type=ALL
#SBATCH -A m4402                # allocation account
#SBATCH -t 00:05:00             # time limit (hh:mm:ss)

# Path to your image on Perlmutter
IMAGE="docker:bnlnpps/simphony:develop"
CMD='cd /src/simphony && simg4ox -g $OPTICKS_HOME/tests/geom/sphere_leak.gdml -m $OPTICKS_HOME/tests/run.mac -c sphere_leak'

# Launch the container using Shifter
srun -n 1 -c 8 --cpu_bind=cores -G 1 --gpu-bind=single:1 shifter --image=$IMAGE /bin/bash -l -c "$CMD"
```
