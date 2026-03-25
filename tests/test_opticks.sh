#!/usr/bin/env bash

set -e

QCurandStateMonolithic_SPEC=1:0:0  $OPTICKS_BUILD/qudarap/tests/QCurandStateMonolithicTest
QCurandStateMonolithic_SPEC=3:0:0  $OPTICKS_BUILD/qudarap/tests/QCurandStateMonolithicTest
QCurandStateMonolithic_SPEC=10:0:0 $OPTICKS_BUILD/qudarap/tests/QCurandStateMonolithicTest

mv $HOME/.opticks/rngcache/RNG/QCurandStateMonolithic_1M_0_0.bin $HOME/.opticks/rngcache/RNG/QCurandState_1000000_0_0.bin
mv $HOME/.opticks/rngcache/RNG/QCurandStateMonolithic_3M_0_0.bin $HOME/.opticks/rngcache/RNG/QCurandState_3000000_0_0.bin
mv $HOME/.opticks/rngcache/RNG/QCurandStateMonolithic_10M_0_0.bin $HOME/.opticks/rngcache/RNG/QCurandState_10000000_0_0.bin

install -D $OPTICKS_HOME/tests/GEOM.sh $HOME/.opticks/GEOM/GEOM.sh

# Generate initial photons for tests
generate-input-photons

export GEOM=RaindropRockAirWater
export G4CXOpticks__setGeometry_saveGeometry=$HOME/.opticks/GEOM/$GEOM

ctest --test-dir $OPTICKS_BUILD
