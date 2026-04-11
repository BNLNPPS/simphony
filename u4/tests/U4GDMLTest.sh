#!/bin/bash -l 

usage(){  cat << EOU 

U4GDML round-trip read/write
----------------------------

Reads an existing GDML file and writes it back out via U4GDML.

EOU
}

default=/tmp/$USER/opticks/ntds3/G4CXOpticks/origin.gdml
GDMLPATH=${GDMLPATH:-$default}

if [ ! -f "$GDMLPATH" ]; then 
   echo $BASH_SOURCE GDMLPATH $GDMLPATH does not exit 
   exit 1
fi 

U4GDMLTest $GDMLPATH
#U4GDMLTest 



[ $? -ne 0 ] && echo $BASH_SOURCE run error && exit 1 

exit 0 



