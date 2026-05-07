#!/bin/bash -l 
usage(){ cat << EOU
QPMT_MockTest.sh 
==================

Testing GPU side code on CPU using MOCK_CURAND 

EOU
}

source $HOME/.opticks/GEOM/GEOM.sh 
SDIR=$(cd $(dirname $BASH_SOURCE) && pwd)
name=QPMT_MockTest

defarg="build_run_ana"
arg=${1:-$defarg}

export FOLD=/tmp/$name
mkdir -p $FOLD
bin=$FOLD/$name

cuda_prefix=/usr/local/cuda
CUDA_PREFIX=${CUDA_PREFIX:-$cuda_prefix}

vars="BASH_SOURCE SDIR FOLD GEOM bin name CUDA_PREFIX"


if [ "${arg/info}" != "$arg" ]; then 
    for var in $vars ; do printf "%20s : %s \n" "$var" "${!var}" ; done 
fi

if [ "${arg/build}" != "$arg" ]; then 
    gcc $name.cc ../QPMT.cc ../QProp.cc \
       -g \
       -std=c++11 -lstdc++ \
       -DMOCK_CURAND \
       -I.. \
       -I$OPTICKS_PREFIX/include/SysRap  \
       -I$CUDA_PREFIX/include \
       -I$OPTICKS_PREFIX/externals/glm/glm \
       -I$OPTICKS_PREFIX/externals/plog/include \
       -o $bin 

    [ $? -ne 0 ] && echo $msg build error && exit 1 
fi 


if [ "${arg/run}" != "$arg" ]; then 
    $bin
    [ $? -ne 0 ] && echo $msg run error && exit 2 
fi

if [ "${arg/dbg}" != "$arg" ]; then 
    gdb__ $bin
    [ $? -ne 0 ] && echo $msg dbg error && exit 3 
fi

if [ "${arg/ana}" != "$arg" ]; then 
    ${IPYTHON:-ipython} --pdb -i $SDIR/QPMTTest.py
    [ $? -ne 0 ] && echo $msg ana error && exit 4
fi

exit 0 
