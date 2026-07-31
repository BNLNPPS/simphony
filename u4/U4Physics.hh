#pragma once
/**
U4Physics.hh
============

This is intended solely for use from U4AppTest 

**/

#include <cstdlib>
#include <string>
#include <sstream>

#include "plog/Severity.h"
#include "G4VUserPhysicsList.hh"

class G4Cerenkov ;
class G4Scintillation ;
class G4OpWLS ;

#ifdef DEBUG_TAG
class ShimG4OpAbsorption ;
class ShimG4OpRayleigh ;
#else
class G4OpAbsorption ;
class G4OpRayleigh ;
#endif

class G4VProcess ; 
class G4ProcessManager ; 
class G4FastSimulationManagerProcess ; 


#include "U4_API_EXPORT.hh"

struct U4_API U4Physics : public G4VUserPhysicsList
{
    static const plog::Severity LEVEL ; 
    static int EInt(const char* key, const char* fallback="0"); 

    G4Cerenkov*       fCerenkov ;
    G4Scintillation*  fScintillation ;
    G4OpWLS*          fWLS ;

#ifdef DEBUG_TAG
    ShimG4OpAbsorption*   fAbsorption ;
    ShimG4OpRayleigh*     fRayleigh ;
#else
    G4OpAbsorption*       fAbsorption ;
    G4OpRayleigh*         fRayleigh ;
#endif

    G4VProcess*          fBoundary ; 
    G4FastSimulationManagerProcess*   fFastSim ;  

    std::string desc() const ; 
    static std::string Desc(); 
    static std::string Switches(); 

    U4Physics(); 

    void ConstructParticle();
    void ConstructProcess();
    void ConstructEM();
    void ConstructOp();
    void ConstructOp_opticalphoton(G4ProcessManager* pmanager, const G4String& particleName);
    static G4VProcess* CreateBoundaryProcess(); 

    static constexpr const char* _Cerenkov_DISABLE = "U4Physics__ConstructOp_Cerenkov_DISABLE" ; 
    static constexpr const char* _Scintillation_DISABLE = "U4Physics__ConstructOp_Scintillation_DISABLE" ; 
    static constexpr const char* _OpWLS_DISABLE = "U4Physics__ConstructOp_OpWLS_DISABLE" ;
    static constexpr const char* _OpAbsorption_DISABLE = "U4Physics__ConstructOp_OpAbsorption_DISABLE" ; 
    static constexpr const char* _OpRayleigh_DISABLE = "U4Physics__ConstructOp_OpRayleigh_DISABLE" ; 
    static constexpr const char* _OpBoundaryProcess_DISABLE = "U4Physics__ConstructOp_OpBoundaryProcess_DISABLE" ; 
    static constexpr const char* _OpBoundaryProcess_LASTPOST = "U4Physics__ConstructOp_OpBoundaryProcess_LASTPOST" ; 
    static constexpr const char* _FastSim_ENABLE = "U4Physics__ConstructOp_FastSim_ENABLE" ; 

    int Cerenkov_DISABLE = 0 ; 
    int Scintillation_DISABLE = 0 ; 
    int OpWLS_DISABLE = 0 ;
    int OpAbsorption_DISABLE = 0 ; 
    int OpRayleigh_DISABLE = 0 ; 
    int OpBoundaryProcess_DISABLE = 0 ; 
    int OpBoundaryProcess_LASTPOST = 0 ; 
    int FastSim_ENABLE = 0 ; 
};


