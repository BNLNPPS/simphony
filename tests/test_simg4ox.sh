#!/usr/bin/env bash

USER=fakeuser GEOM=fakegeom simg4ox -g $OPTICKS_HOME/tests/geom/raindrop.gdml -m $OPTICKS_HOME/tests/run.mac
python $OPTICKS_HOME/tests/compare_ab.py
