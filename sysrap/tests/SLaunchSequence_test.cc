/**
SLaunchSequence_test.cc
=========================

SLaunchSequence_test


**/

#include <iostream>
#include "SLaunchSequence.h"

int main(int argc, char** argv)
{
    SLaunchSequence seq(1000000) ; 
    std::cout << seq.desc() << std::endl ; 

    return 0 ; 
}
