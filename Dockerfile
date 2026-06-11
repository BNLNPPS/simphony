# syntax=docker/dockerfile:latest

ARG OS=ubuntu24.04
ARG CUDA_VERSION=13.0.2
ARG OPTIX_VERSION=9.0.0
ARG GEANT4_VERSION=11.4.1
ARG CMAKE_VERSION=4.2.1
ARG SPACK_BUILDCACHE_MIRROR=oci://ghcr.io/bnlnpps/simphony-spack-buildcache
ARG SPACK_TARGET=x86_64_v2

FROM nvidia/cuda:${CUDA_VERSION}-devel-${OS} AS base

ARG OPTIX_VERSION
ARG GEANT4_VERSION
ARG CMAKE_VERSION
ARG CMAKE_BUILD_JOBS

ENV DEBIAN_FRONTEND=noninteractive

RUN apt update \
 && apt install -y g++ gcc gzip tar python3 python-is-python3 python3-pip curl git \
 && apt clean \
 && rm -rf /var/lib/apt/lists/*

RUN apt update \
 && apt install -y libssl-dev \
    nlohmann-json3-dev \
    libglfw3-dev libglu1-mesa-dev libxmu-dev libglew-dev libglm-dev \
    qt6-base-dev libxerces-c-dev libexpat1-dev \
 && apt clean \
 && rm -rf /var/lib/apt/lists/*

RUN curl -fsSL https://github.com/Kitware/CMake/releases/download/v${CMAKE_VERSION}/cmake-${CMAKE_VERSION}-linux-x86_64.tar.gz \
    | tar -xz --strip-components=1 -C /usr/local

RUN mkdir -p /opt/clhep/src && curl -sL https://gitlab.cern.ch/CLHEP/CLHEP/-/archive/CLHEP_2_4_7_2/CLHEP-CLHEP_2_4_7_2.tar.gz | tar -xz --strip-components 1 -C /opt/clhep/src \
 && cmake -S /opt/clhep/src -B /opt/clhep/build \
 && cmake --build /opt/clhep/build --parallel "${CMAKE_BUILD_JOBS:-$(nproc)}" --target install \
 && rm -fr /opt/clhep

RUN mkdir -p /opt/geant4/src && curl -sL https://github.com/Geant4/geant4/archive/refs/tags/v${GEANT4_VERSION}.tar.gz | tar -xz --strip-components 1 -C /opt/geant4/src \
 && cmake -S /opt/geant4/src -B /opt/geant4/build -DGEANT4_USE_SYSTEM_CLHEP=ON -DGEANT4_USE_OPENGL_X11=ON -DGEANT4_USE_QT=ON -DGEANT4_USE_QT_QT6=ON -DGEANT4_USE_GDML=ON -DGEANT4_INSTALL_DATA=ON -DGEANT4_BUILD_MULTITHREADED=ON \
 && cmake --build /opt/geant4/build --parallel "${CMAKE_BUILD_JOBS:-$(nproc)}" --target install \
 && rm -fr /opt/geant4

RUN mkdir -p /opt/plog/src && curl -sL https://github.com/SergiusTheBest/plog/archive/refs/tags/1.1.11.tar.gz | tar -xz --strip-components 1 -C /opt/plog/src \
 && cmake -S /opt/plog/src -B /opt/plog/build \
 && cmake --build /opt/plog/build --parallel "${CMAKE_BUILD_JOBS:-$(nproc)}" --target install \
 && rm -fr /opt/plog

RUN mkdir -p /opt/optix && curl -sL https://github.com/NVIDIA/optix-dev/archive/refs/tags/v${OPTIX_VERSION}.tar.gz | tar -xz --strip-components 1 -C /opt/optix

RUN curl -LsSf https://astral.sh/uv/install.sh | env UV_INSTALL_DIR=/usr/local/bin sh

SHELL ["/bin/bash", "-l", "-c"]

# Set up non-interactive shells by sourcing all of the scripts in /etc/profile.d/
RUN cat <<"EOF" > /etc/bash.nonint
if [ -d /etc/profile.d ]; then
  for i in /etc/profile.d/*.sh; do
    if [ -r $i ]; then
      . $i
    fi
  done
  unset i
fi
EOF

RUN cat /etc/bash.nonint >> /etc/bash.bashrc

ENV BASH_ENV=/etc/bash.nonint
ENV OPTICKS_PREFIX=/opt/simphony
ENV OPTICKS_HOME=/workspaces/simphony
ENV OPTICKS_BUILD=/opt/simphony/build
ENV LD_LIBRARY_PATH=${OPTICKS_PREFIX}/lib:${LD_LIBRARY_PATH}
ENV VIRTUAL_ENV=${OPTICKS_HOME}/.venv
ENV PATH=${OPTICKS_PREFIX}/bin:${VIRTUAL_ENV}/bin:${PATH}
ENV NVIDIA_DRIVER_CAPABILITIES=graphics,compute,utility

WORKDIR $OPTICKS_HOME

# Install Python dependencies
COPY pyproject.toml uv.lock $OPTICKS_HOME/
COPY optiphy $OPTICKS_HOME/optiphy
RUN uv sync


FROM base AS release

ARG CMAKE_BUILD_JOBS

COPY . $OPTICKS_HOME

RUN cmake -S $OPTICKS_HOME -B $OPTICKS_BUILD -DCMAKE_INSTALL_PREFIX=$OPTICKS_PREFIX -DCMAKE_BUILD_TYPE=Release \
 && cmake --build $OPTICKS_BUILD --parallel "${CMAKE_BUILD_JOBS:-$(nproc)}" --target install


FROM base AS develop

ARG CMAKE_BUILD_JOBS

RUN apt update && apt install -y x11-apps mesa-utils vim

COPY . $OPTICKS_HOME

RUN cmake -S $OPTICKS_HOME -B $OPTICKS_BUILD -DCMAKE_INSTALL_PREFIX=$OPTICKS_PREFIX -DCMAKE_BUILD_TYPE=Debug \
 && cmake --build $OPTICKS_BUILD --parallel "${CMAKE_BUILD_JOBS:-$(nproc)}" --target install


FROM nvidia/cuda:${CUDA_VERSION}-devel-${OS} AS spack-base

ARG OPTIX_VERSION
ARG GEANT4_VERSION
ARG SPACK_BUILDCACHE_MIRROR
ARG SPACK_TARGET
ARG SPACK_VERSION=1.1.1

ENV DEBIAN_FRONTEND=noninteractive

# Install Spack package manager
RUN apt update \
 && apt install -y build-essential ca-certificates coreutils curl gfortran git gpg lsb-release unzip zip python3 \
 && apt clean \
 && rm -rf /var/lib/apt/lists/*

RUN mkdir -p /opt/spack && curl -fsSL https://github.com/spack/spack/archive/v${SPACK_VERSION}.tar.gz | tar -xz --strip-components 1 -C /opt/spack
RUN echo "source /opt/spack/share/spack/setup-env.sh" > /etc/profile.d/z09_source_spack_setup.sh
RUN echo "source /etc/profile.d/z09_source_spack_setup.sh" >> /etc/bash.bashrc

ENV BASH_ENV=/etc/profile.d/z09_source_spack_setup.sh
ENV OPTICKS_HOME=/workspaces/simphony
ENV OPTIX_VERSION=${OPTIX_VERSION}
ENV GEANT4_VERSION=${GEANT4_VERSION}
ENV SPACK_BUILDCACHE_MIRROR=${SPACK_BUILDCACHE_MIRROR}
ENV SPACK_TARGET=${SPACK_TARGET}
ENV PATH=/opt/spack/bin:${PATH}
ENV NVIDIA_DRIVER_CAPABILITIES=graphics,compute,utility

WORKDIR ${OPTICKS_HOME}

SHELL ["/bin/bash", "-l", "-c"]

RUN mkdir -p /root/.spack \
 && cat > /root/.spack/packages.yaml <<EOF
packages:
  all:
    target: [${SPACK_TARGET}]
EOF


FROM spack-base AS spack-no-env

WORKDIR ${OPTICKS_HOME}

# Intentionally track the latest Spack repo state in CI to catch packaging regressions.
RUN spack repo update -b develop builtin
RUN spack repo add https://github.com/BNLNPPS/spack-packages
RUN spack mirror add --unsigned simphony-buildcache "${SPACK_BUILDCACHE_MIRROR}"
RUN spack external find --not-buildable --path /usr/local/cuda cuda
# Print out concretized specs
RUN spack spec --fresh -Il simphony ^geant4@${GEANT4_VERSION} ^optix-dev@${OPTIX_VERSION}
# Prefer dependency binaries when they exist, but let PR validation fall back to
# source builds if the mirror lags behind the latest Spack package metadata.
RUN spack install --only=dependencies --fresh --use-buildcache auto simphony ^geant4@${GEANT4_VERSION} ^optix-dev@${OPTIX_VERSION}
RUN spack install --fresh --use-buildcache package:never,dependencies:auto simphony ^geant4@${GEANT4_VERSION} ^optix-dev@${OPTIX_VERSION}
RUN spack clean -a && rm -rf /root/.cache
# Once simphony is installed it can be loaded in the user environment
# $ spack load simphony
# $ spack load --sh simphony >> /etc/profile.d/z10_load_simphony_from_spack.sh
RUN set -euo pipefail \
 && spack find --format "{name}{@version} {/hash}" simphony \
 && prefix="$(spack location -i simphony)" \
 && echo "Installed prefix: $prefix" \
 && test -d "$prefix" \
 && (test -f "$prefix/.spack/spec.json" || test -f "$prefix/.spack/spec.yaml")

FROM develop AS default
