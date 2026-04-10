#!/bin/bash -l 

default=remote
arg=${1:-$default}

opticks-switch-key $arg 

bin=CSGNodeTest 

if [ -n "$DEBUG" ]; then 
   gdb $bin
else
   $bin
fi 

