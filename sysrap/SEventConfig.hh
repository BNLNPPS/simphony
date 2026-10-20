#pragma once
#include <string>
#include <vector>
#include <limits>
#include <cstdint>
#include "plog/Severity.h"
#include "SYSRAP_API_EXPORT.hh"

struct NP ;
struct storch;

struct scontext ;
struct salloc ;

/**
SEventConfig
==============

This is for configuration that is important and/or where
the settings have impact across multiple classes/structs.

Note that geometry related things are configured in SGeoConfig.

For settings that are local to individual structs or mainly for
shortterm debug : **do not implement here**. Instead
implement local config locally using constexpr envvar keys
inside the structs.


Usage
------

Usage is widespread, some of the more pertinent places:

SEvt::SEvt
   invokes SEventConfig::Initialize
   (HMM: run twice for for A-B testing when have multiple SEvt)

QEvt::gatherComponent
   guided by SEventConfig::GatherComp


Canonical configuration
-----------------------

Applications should construct ``simphony::Config`` from a named JSON file.
``Config::Apply`` synchronizes its typed ``event`` values into this low-level,
process-wide facade before event creation. See ``docs/configuration.md`` for
the schema and defaults.

Several values have dependents. Set them through ``Config`` before creating an
``SEvt``; changing a basis setting after initialization does not necessarily
recompute all dependent values:

* ``event.event_mode`` derives gather/save masks and diagnostic buffer limits.
* ``event.mode_lite`` and ``event.mode_merge`` select photon and hit variants.
* ``event.maxslot: 0`` is replaced by a device-memory heuristic once a CUDA
  device is selected.



TODO : client->server settings/metadata
----------------------------------------

1. client needs to pass its settings across to server, either:

   * key:value pairs in HTTP headers
   * genstep metadata [favor this as its persistable]

2. some settings/metadata that cannot be changed like geometry digest
   needs to be checked for match between client and server with asserts
   when it does not match

   * test client/server uses "secret" string


Settings
------------

EventMode
    JSON ``event.event_mode`` configures what is gathered and persisted.
RunningMode
    JSON ``event.running_mode`` configures how running is done, for example
    default, saved-Geant4-state, or rerun-Geant4-state workflows.

HitMask (JSON ``event.hit_mask``)
    is a comma-delimited string that determines which subset of photons is
    downloaded into the "hit" array.
    Default is SD.  Settings that could be used::

        SD : SURFACE_DETECT
        EC : EFFICIENCY_COLLECT
        EX : EFFICIENCY_CULL

MaxSimtrace
    JSON ``event.max_simtrace`` limits simtrace allocations.

MaxSlot (JSON ``event.maxslot``)
    maximum CUDA launch slots

    With ``event.maxslot: 0`` SEventConfig::HeuristicMaxSlot_Rounded is
    used to set MaxSlot to a VRAM appropriate value.

    For a non-zero value the specified value is used.

    For large numbers of photons the value will determine how many
    launches are done.


Record and diagnostic buffer limits
    are derived from ``event.event_mode``. They are not independent runtime
    settings.

MaxTime (JSON ``event.max_time``, ns)
    truncation to simulation in addition to MaxBounce



+-------------------------------+-----------------------------------------+---------------------------------------+
| Method                        |  Default                                | JSON key                              |
+===============================+=========================================+=======================================+
| SEventConfig::OutFold         | "$DefaultOutputDir"                     | event.output_dir                      |
+-------------------------------+-----------------------------------------+---------------------------------------+


EventReldir
    Controls the relative event output directory. The canonical configuration
    is ``event.event_reldir`` in the selected JSON file.

**/

struct SYSRAP_API SEventConfig
{
    static const plog::Severity LEVEL ;
    static constexpr const int MISSING_INDEX = std::numeric_limits<int>::max() ;

    static int  RecordLimit(); // sseq::SLOTS typically 32
    static void LIMIT_Check();
    static void ORDER_Check();
    static std::string Desc();

    //static std::string DescHitMask();
    static std::string HitMaskLabel();


    // [TODO : RECONSIDER OUTDIR OUTNAME MECHANICS FOLLOWING SEVT LAYOUT
    static const char* OutDir( const char* reldir);
    static const char* OutDir();
    static const char* OutPath( const char* stem, int index, const char* ext, bool unique );
    static const char* OutPath( const char* reldir, const char* stem, int index, const char* ext, bool unique );
    static std::string DescOutPath(  const char* stem, int index, const char* ext, bool unique ) ;
    // ]TODO

    static constexpr const int64_t G = 1000000000 ;
    static constexpr const int64_t M = 1000000 ;
    static constexpr const int64_t K = 1000 ;

    static constexpr const char* WITH_STATE_LIMIT = "M3" ;
    static constexpr const char* NO_STATE_LIMIT = "G1000" ;  // 1000 billion
    static constexpr const char* NO_STATE_LIMIT_GENSTEP = "M10" ; // adhoc

    static int         IntegrationMode();
    static bool        GPU_Simulation() ; // 1 or 3
    static bool        CPU_Simulation() ; // 2 or 3

    static const char* EventMode();
    static const char* DeviceName();
    static bool        HasDevice();

    static int         RunningMode();
    static const char* RunningModeLabel();
    static const storch& Torch();
    static int StartIndex();

    static bool IsRunningModeDefault();
    static bool IsRunningModeG4StateSave();
    static bool IsRunningModeG4StateRerun();

    static bool IsRunningModeTorch();
    static bool IsRunningModeInputPhoton();
    static bool IsRunningModeInputGenstep();
    static bool IsRunningModeGun();

    static int         EventSkipahead();
    static const char* G4StateSpec();
    static int         G4StateRerun();
    static int G4StateRerunConfigured();

    static int64_t MaxCurand();
    static int64_t MaxSlot();

    static int64_t MaxGenstep();
    static int64_t MaxPhoton();
    static int64_t MaxSimtrace();

    static int64_t MaxBounce();
    static float MaxTime();

    static int64_t MaxRecord();  // full photon step record
    static int64_t MaxRec();     // compressed photon step record
    static int64_t MaxAux();     // auxiliary photon step record, used for debug


    // MOST OF THE BELOW MAX ARE ACTUALLY JUST BOOLEAN SWITCHES ...
    static int64_t MaxSup();     // supplementry photon level info
    static int64_t MaxSeq();     // seqhis slots
    static int64_t MaxPrd();
    static int64_t MaxTag();
    static int64_t MaxFlat();

    static int64_t ModeLite();
    static int64_t ModeMerge();
    static float   MergeWindow();

    static float MaxExtentDomain() ;
    static float MaxTimeDomain() ;

    static const char* OutFold();
    static const char* OutName();
    static const char* EventReldir();
    static unsigned HitMask();

    static unsigned GatherComp();
    static unsigned SaveComp();

    static float PropagateEpsilon();
    static float PropagateEpsilon0();
    static unsigned PropagateEpsilon0Mask();
    static std::string PropagateEpsilon0MaskLabel();
    static float PropagateRefineDistance();
    static unsigned PropagateRefine();

    static const char* _InputGenstepPath(int idx=-1);
    static const char* InputGenstep(int idx=-1);
    static bool InputGenstepPathExists(int idx);


    static const char* InputPhoton();
    static const char* InputPhotonFrame();
    static float       InputPhotonChangeTime();
    static const char* InputPhotonRecordTime();
    static const char* InputPhotonRecordSlice();

    static int RGMode();
    static bool IsRGModeRender();
    static bool IsRGModeSimtrace();
    static bool IsRGModeSimulate();
    static bool IsRGModeTest();

    static const char* RGModeLabel();

    static std::string DescGatherComp();
    static std::string DescSaveComp();

    static bool HasComp_(unsigned comp, const char* q_names, char q_delim=',');
    static bool HasSaveComp(  const char* q_names, char q_delim=',');
    static bool HasGatherComp(const char* q_names, char q_delim=',');




    static void GatherCompList( std::vector<unsigned>& gather_comp ) ;
    static int NumGatherComp();

    static void SaveCompList( std::vector<unsigned>& save_comp ) ;
    static int NumSaveComp();

    static constexpr const char* DebugHeavy = "DebugHeavy" ;
    static constexpr const char* DebugLite = "DebugLite" ;
    static constexpr const char* Nothing = "Nothing" ;
    static constexpr const char* Minimal = "Minimal" ;
    static constexpr const char* Hit = "Hit" ;
    static constexpr const char* HitPhoton = "HitPhoton" ;
    static constexpr const char* HitPhotonSeq = "HitPhotonSeq" ;
    static constexpr const char* HitSeq = "HitSeq" ;

    static void SetDebugHeavy();
    static void SetDebugLite();
    static void SetNothing();
    static void SetMinimal();
    static void SetHit();
    static void SetHitPhoton();
    static void SetHitPhotonSeq();
    static void SetHitSeq();

    static bool IsDebugHeavy();
    static bool IsDebugLite();
    static bool IsNothing();
    static bool IsMinimal();
    static bool IsHit();
    static bool IsHitPhoton();
    static bool IsHitPhotonSeq();
    static bool IsHitSeq();

    static bool IsMinimalOrNothing();

    static std::string DescEventMode() ;
    static std::string DescEventModeCheck() ;

    static void SetIntegrationMode(int mode);   // IntegrationMode configures the integration of Opticks and Framework
    static void SetEventMode(const char* mode);   // EventMode configures what will be persisted, ie what is in the SEvt
    static void SetRunningMode(const char* mode); // RunningMode configures how running is done, eg Default/DefaultSaveG4State/RerunG4State/Torch
    static void SetTorch(const storch& torch);

    static void SetStartIndex(int index0);
    static void SetNumEvent(int nevt);            // NumEvent is used by some tests

    static void SetEventSkipahead(int offset);
    static void SetG4StateSpec(const char* spec);
    static void SetG4StateRerun(int id);

    static void SetMaxCurand( int max_curand);
    static void SetMaxSlot(   int max_slot);

    static void SetMaxGenstep(int max_genstep);
    static void SetMaxPhoton( int max_photon);
    static void SetMaxSimtrace(int64_t max_simtrace);

    static void SetMaxBounce( int max_bounce);
    static void SetMaxTime(   float max_time );

    static void SetMaxRecord( int max_record);
    static void SetMaxRec(    int max_rec);
    static void SetMaxAux(    int max_aux);
    static void SetMaxSup(    int max_sup);
    static void SetMaxSeq(    int max_seq);
    static void SetMaxPrd(    int max_prd);
    static void SetMaxTag(    int max_tag);
    static void SetMaxFlat(   int max_flat);

    static void SetModeLite(  int mode );
    static void SetModeMerge( int mode );
    static void SetMergeWindow( float merge_window_ns );

    static void SetMaxExtentDomain( float max_extent);
    static void SetMaxTimeDomain(   float max_time );




    static void SetOutFold( const char* out_fold);
    static void SetOutName( const char* out_name);
    static void SetEventReldir( const char* evt_reldir);
    static void SetHitMask(const char* abrseq, char delim=',' );

    static void SetRGMode( const char* rg_mode) ;
    static void SetRGModeSimulate() ;
    static void SetRGModeSimtrace() ;
    static void SetRGModeRender() ;
    static void SetRGModeTest() ;

    static void SetPropagateEpsilon( float eps) ;
    static void SetPropagateEpsilon0( float eps) ;
    static void SetPropagateEpsilon0Mask( const char* abrseq, char delim=',' ) ;
    static void SetPropagateRefineDistance( float refine_distance ) ;
    static void SetPropagateRefine( unsigned refine ) ;

    static void SetInputGenstep(const char* input_genstep);
    static void SetInputPhoton(const char* input_photon);
    static void SetInputPhotonFrame(const char* input_photon_frame);
    static void SetInputPhotonChangeTime( float t0 ) ;
    static void SetInputPhotonRecordTime( const char* record_time ) ;
    static void SetInputPhotonRecordSlice( const char* iprs ) ;

    static void SetGatherComp_(unsigned mask);
    static void SetGatherComp(const char* names, char delim=',') ;
    static bool GatherRecord();

    static void SetSaveComp_(unsigned mask);
    static void SetSaveComp(const char* names, char delim=',') ;
    static bool IsCompConfigured();

    // STATIC VALUES SET EARLY FROM COMPILED DEFAULTS, THEN UPDATED BY Config::Apply

    static int         _IntegrationModeDefault ;
    static const char* _EventModeDefault ;
    static const char* _RunningModeDefault ;
    static int         _StartIndexDefault ;
    static int         _NumEventDefault ;

    static int         _EventSkipaheadDefault ;
    static const char* _G4StateSpecDefault ;
    static const char* _G4StateSpecNotes ;
    static int         _G4StateRerunDefault ;

    static const char* _MaxBounceNotes ;
    static const char* _MaxTimeNotes ;

    static const char* _MaxSlotDefault ;

    static const char* _MaxGenstepDefault ;
    static const char* _MaxSimtraceDefault ;

    static float _MaxTimeDefault  ;
    static int _MaxBounceDefault ;
    static int _MaxRecordDefault ;
    static int _MaxRecDefault ;
    static int _MaxAuxDefault ;
    static int _MaxSupDefault ;
    static int _MaxSeqDefault ;
    static int _MaxPrdDefault ;
    static int _MaxTagDefault ;
    static int _MaxFlatDefault ;

    static int _ModeLiteDefault ;
    static int _ModeMergeDefault ;
    static float _MergeWindowDefault ;

    static float _MaxExtentDomainDefault ;
    static float _MaxTimeDomainDefault  ;

    static const char* _OutFoldDefault ;
    static const char* _EventReldirDefault ;
    static const char* _HitMaskDefault ;

    static float       _PropagateEpsilonDefault  ;
    static float       _PropagateEpsilon0Default  ;
    static const char* _PropagateEpsilon0MaskDefault ;
    static float       _PropagateRefineDistanceDefault  ;
    static unsigned    _PropagateRefineDefault  ;

    static const char* _InputGenstepDefault ;
    static const char* _InputPhotonDefault ;
    static const char* _InputPhotonFrameDefault ;
    static float       _InputPhotonChangeTimeDefault ;
    static const char* _InputPhotonRecordTimeDefault ;
    static const char* _InputPhotonRecordSliceDefault ;

    static int         _IntegrationMode ;
    static const char* _EventMode ;
    static const char* _DeviceName ;
    static int         _RunningMode ;
    static storch      _Torch;
    static int         _StartIndex ;
    static int         _NumEvent ;

    static int               NumEvent();          // some tests use event count and need to detect last event
    static int               EventIndex(int idx) ;
    static int               EventIndexArg(int index) ;
    static bool              IsFirstEvent(int idx);   // 0-based idx (such as Geant4 eventID)
    static bool              IsLastEvent(int idx);    // 0-based idx (such as Geant4 eventID)

    static int         _EventSkipahead ;
    static const char* _G4StateSpec ;
    static int         _G4StateRerun ;

    static int64_t _MaxCurand ;
    static int64_t _MaxSlot ;

    static int64_t _MaxGenstep ;
    static int64_t _MaxPhoton ;
    static int64_t _MaxSimtrace ;

    static float _MaxTime  ;
    static int _MaxBounce ;
    static int _MaxRecord ;
    static int _MaxRec ;
    static int _MaxAux ;
    static int _MaxSup ;
    static int _MaxSeq ;
    static int _MaxPrd ;
    static int _MaxTag ;
    static int _MaxFlat ;

    static int _ModeLite ;
    static int _ModeMerge ;
    static float _MergeWindow ;

    static float _MaxExtentDomain ;
    static float _MaxTimeDomain  ;

    static const char* _OutFold ;
    static const char* _OutName ;
    static const char* _EventReldir ;
    static unsigned _HitMask ;
    static int _RGMode ;

    static unsigned _GatherComp ;
    static unsigned _SaveComp ;

    static float _PropagateEpsilon ;
    static float _PropagateEpsilon0 ;
    static unsigned _PropagateEpsilon0Mask ;
    static float _PropagateRefineDistance ;
    static unsigned _PropagateRefine ;
    static int      _SerInterval;
    static int      _SerHint;

    static const char* _InputGenstep ;
    static const char* _InputPhoton ;
    static const char* _InputPhotonFrame ;
    static float       _InputPhotonChangeTime ;
    static const char* _InputPhotonRecordTime ;
    static const char* _InputPhotonRecordSlice ;

    static scontext* CONTEXT ;
    static salloc*   ALLOC ;
    static std::string GetGPUMeta();

    static int   Initialize_COUNT ;
    static void  Initialize_Meta();
    static int   Initialize();

    static const char* DebugHeavy_NOTE    ;
    static const char* DebugLite_NOTE     ;
    static const char* Nothing_NOTE       ;
    static const char* Minimal_NOTE       ;
    static const char* Hit_NOTE           ;
    static const char* HitPhoton_NOTE     ;
    static const char* HitPhotonSeq_NOTE  ;
    static const char* HitSeq_NOTE        ;
    static const char* EventMode_NOTE()   ;

    static void  Initialize_Comp();

    static unsigned PhotonComp();
    static unsigned PhotonCompOne();
    static const char* PhotonCompOneName();

    static unsigned HitComp();             // potentially bitwise combination of multiple comp
    static unsigned HitCompOne();          // single bit hit comp : the canonical one decided by ModeLite ModeMerge
    static const char* HitCompOneName();


    static void  Initialize_Comp_Simulate_(unsigned& gather_mask, unsigned& save_mask );
    static void  Initialize_Comp_Simtrace_(unsigned& gather_mask, unsigned& save_mask );
    static void  Initialize_Comp_Render_(  unsigned& gather_mask, unsigned& save_mask );


    static constexpr const char* NAME = "SEventConfig.npy" ;
    static NP* Serialize();
    static void Save(const char* dir) ;

    static void SetDevice( size_t totalGlobalMem_bytes, std::string name );
    static void SetDeviceName( const char* name );

    static size_t HeuristicMaxSlot(         size_t totalGlobalMem_bytes, int lite, int merge );
    static size_t HeuristicMaxSlot_Rounded( size_t totalGlobalMem_bytes, int lite, int merge );
    static std::string DescDevice(size_t totalGlobalMem_bytes, std::string name );

    static salloc*   AllocEstimate(int _max_slot=0);
    static uint64_t  AllocEstimateTotal(int _max_slot=0);



};
