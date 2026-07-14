#include "OPTICKS_LOG.hh"
#include <iostream>
#include "SEvt.hh"

int main(int argc, char** argv)
{
    OPTICKS_LOG(argc, argv) ; 

    int g4state_rerun_id = SEventConfig::G4StateRerun();
    bool rerun = g4state_rerun_id > -1 ;
    const char* reldir = nullptr ; 

    SEvt* evt = nullptr ; 

    if(rerun == false)
    {  
        SEventConfig::SetEventReldir(reldir);  
        evt = SEvt::Create(0);        
    }   
    else
    {   
        evt = SEvt::LoadRelative(reldir) ;
    }   

    if(evt == nullptr) return 0 ; 
    LOG(info) << evt->desc() ; 
    LOG(info) << evt->descComponent() ; 

    const NP* g4state = evt->getG4State(); 
    LOG(info) << " SEvt::getG4State " << ( g4state ? g4state->sstr() : "-" ); 

    if(SEventConfig::_G4StateRerun > -1) 
    {
        std::cout 
            << "SEventConfig::_G4StateRerun " << SEventConfig::_G4StateRerun 
            << std::endl 
            << g4state->sliceArrayString<unsigned long>( SEventConfig::_G4StateRerun, -1 ) 
            << std::endl 
            ;  
    }

    if(evt->is_loaded)
    {
        SEventConfig::SetEventReldir("SEvtLoadTest"); 
        evt->save(); 
    }

    return 0 ; 
}
