#!/bin/bash 
usage(){ cat << EOU

~/o/sysrap/tests/srng_test.sh 

EOU
}

cd $(dirname $(realpath $BASH_SOURCE))


cuda_prefix=/usr/local/cuda
CUDA_PREFIX=${CUDA_PREFIX:-$cuda_prefix}

name=srng_test
bin=/tmp/$name

rng=${RNG:-RNG_PHILOX}

gcc $name.cc \
    -I$CUDA_PREFIX/include \
    -I.. \
    -D$rng \
    -std=c++17 -lstdc++ -g -o $bin && $bin
