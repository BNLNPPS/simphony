/*
 * Copyright (c) 2019 Opticks Team. All Rights Reserved.
 *
 * This file is part of Opticks
 * (see https://bitbucket.org/simoncblyth/opticks).
 *
 * Licensed under the Apache License, Version 2.0 (the "License"); 
 * you may not use this file except in compliance with the License.  
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software 
 * distributed under the License is distributed on an "AS IS" BASIS, 
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  
 * See the License for the specific language governing permissions and 
 * limitations under the License.
 */

// TEST=SFrameTest om-t 

#include <string>
#include "OPTICKS_LOG.hh"
#include "SStackFrame.h"

std::string linux_stack = R"(
/home/blyth/local/opticks/lib64/libSysRap.so(+0x10495) [0x7fffe54cf495] 
/home/blyth/local/opticks/lib64/libSysRap.so(_ZN10SBacktrace4DumpEv+0x1b) [0x7fffe54cf823] 
/home/blyth/local/opticks/lib64/libCFG4.so(_ZN10CMixMaxRng4flatEv+0x138) [0x7ffff7955f84] 
/home/blyth/local/opticks/externals/lib64/libG4processes.so(_ZN12G4VEmProcess36PostStepGetPhysicalInteractionLengthERK7G4TrackdP16G4ForceCondition+0x2ce) [0x7ffff1e3f21a] 
/home/blyth/local/opticks/externals/lib64/libG4tracking.so(_ZN10G4VProcess12PostStepGPILERK7G4TrackdP16G4ForceCondition+0x42) [0x7ffff36ff9b2] 
/home/blyth/local/opticks/externals/lib64/libG4tracking.so(_ZN17G4SteppingManager24DefinePhysicalStepLengthEv+0x127) [0x7ffff36fe161] 
/home/blyth/local/opticks/externals/lib64/libG4tracking.so(_ZN17G4SteppingManager8SteppingEv+0x1c2) [0x7ffff36fb410] 
/home/blyth/local/opticks/externals/lib64/libG4tracking.so(_ZN17G4TrackingManager15ProcessOneTrackEP7G4Track+0x284) [0x7ffff3707236] 
/home/blyth/local/opticks/externals/lib64/libG4event.so(_ZN14G4EventManager12DoProcessingEP7G4Event+0x4ce) [0x7ffff397fd46] 
/home/blyth/local/opticks/externals/lib64/libG4event.so(_ZN14G4EventManager15ProcessOneEventEP7G4Event+0x2e) [0x7ffff3980572] 
/home/blyth/local/opticks/externals/lib64/libG4run.so(_ZN12G4RunManager15ProcessOneEventEi+0x57) [0x7ffff3c82665] 
/home/blyth/local/opticks/externals/lib64/libG4run.so(_ZN12G4RunManager11DoEventLoopEiPKci+0x59) [0x7ffff3c824d7] 
/home/blyth/local/opticks/externals/lib64/libG4run.so(_ZN12G4RunManager6BeamOnEiPKci+0xc1) [0x7ffff3c81d2d] 
/home/blyth/local/opticks/lib/CerenkovMinimal() [0x41a014] 
/home/blyth/local/opticks/lib/CerenkovMinimal() [0x419ed1] 
/home/blyth/local/opticks/lib/CerenkovMinimal() [0x4098bd] 
/usr/lib64/libc.so.6(__libc_start_main+0xf5) [0x7fffe00e1445] 
/home/blyth/local/opticks/lib/CerenkovMinimal() [0x409629] 
)";

int main(int  argc, char** argv )
{
    OPTICKS_LOG(argc, argv);

    const char *lines = linux_stack.c_str();
    LOG(info) << std::endl << lines  ; 


    std::istringstream iss(lines);
    std::string line ;
    while (getline(iss, line, '\n'))
    {   
        if(line.empty()) continue ; 

        //std::cout << "[" << line << "]" << std::endl ; 

        SStackFrame f((char*)line.c_str());
        f.dump(); 
    }   

    return 0 ; 
}
