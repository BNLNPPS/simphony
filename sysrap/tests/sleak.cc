#include <cstring>
#include <filesystem>

#include "EventTiming.hh"
#include "EventTimingProfileReport.hh"
#include "NPFold.h"


struct sleak
{
    const NP* run ; 
    NP* runprof ;

    sleak();  
    std::string desc() const; 
    NPFold* serialize() const ; 
    void    import( const NPFold* fold ); 
    void save(const char* dir) const ; 
    static sleak* Load(const char* dir) ; 
};

inline sleak::sleak()
    :
    run(nullptr),
    runprof(nullptr)
{
}

inline std::string sleak::desc() const 
{
    std::stringstream ss ; 
    ss << "sleak::desc" << std::endl 
       << " run :" << ( run ? run->sstr() : "no-run" ) << std::endl 
       << " runprof :" << ( runprof ? runprof->sstr() : "no-runprof" ) << std::endl 
       ;
    std::string str = ss.str(); 
    return str ; 
}  


inline NPFold* sleak::serialize() const 
{
    NPFold* smry = new NPFold ;  
    if(run) smry->add("run", run ) ; 
    if(runprof) smry->add("runprof", runprof ) ; 
    return smry ; 
}
inline void sleak::import(const NPFold* smry) 
{
    const NP* saved_run = smry ? smry->get("run") : nullptr;
    const NP* saved_runprof = smry ? smry->get("runprof") : nullptr;
    run = saved_run ? saved_run->copy() : nullptr;
    runprof = saved_runprof ? saved_runprof->copy() : nullptr;
}
inline void sleak::save(const char* dir) const 
{
    NPFold* smry = serialize(); 
    smry->save_verbose(dir); 
}
inline sleak* sleak::Load(const char* dir) // static
{
    NPFold* smry = NPFold::Load(dir) ; 
    sleak* leak = new sleak ; 
    leak->import(smry) ; 
    return leak ; 
}

struct sleak_Creator
{
    bool                  VERBOSE;
    const char*           dirp;
    std::filesystem::path profile_path;
    const NPFold*         fold;
    bool                  fold_valid;
    const NP*             run;
    sleak*                leak;

    sleak_Creator(const char* dirp_, const char* profile_path_ = nullptr);
    std::string desc() const;
};

inline sleak_Creator::sleak_Creator(const char* dirp_, const char* profile_path_) :
    VERBOSE(getenv("sleak_Creator__VERBOSE") != nullptr),
    dirp(dirp_ ? strdup(dirp_) : nullptr),
    profile_path(profile_path_ ? std::filesystem::path(profile_path_) : std::filesystem::path(dirp_) / "event_timing_profile.csv"),
    fold(NPFold::LoadNoData(dirp)),
    fold_valid(NPFold::IsValid(fold)),
    run(fold_valid ? fold->get("run") : nullptr),
    leak(new sleak)
{
    leak->run = run ? run->copy() : nullptr; // NoData arrays must be copied before saving.

    const std::vector<EventTimingProfileRecord> records =
        std::filesystem::exists(profile_path)
            ? EventTimingProfile::ReadFile(profile_path)
            : std::vector<EventTimingProfileRecord>{};
    leak->runprof = records.empty()
                        ? nullptr
                        : EventTimingProfileReport::MakeEventTimingProfileArray(records);
}

inline std::string sleak_Creator::desc() const 
{
    std::stringstream ss ; 
    ss << "sleak_Creator::desc" << std::endl 
       << " dirp " << ( dirp ? dirp : "-" ) << std::endl 
       << " fold " << ( fold ? "YES" : "NO " ) << std::endl 
       << " fold_valid " << ( fold_valid ? "YES" : "NO " ) << std::endl 
       << " run :" << ( run ? run->sstr() : "no-run" ) << std::endl 
       ;
    std::string str = ss.str(); 
    return str ; 
}  

int main(int argc, char** argv)
{
    char*       argv0 = argv[0];
    const char* dirp = argc > 1 ? argv[1] : U::PWD();
    const char* profile_path = argc == 4 && std::strcmp(argv[2], "--event-timing-profile") == 0
                                   ? argv[3]
                                   : nullptr;
    if (dirp == nullptr)
        return 0;
    sleak_Creator creator(dirp, profile_path);
    std::cout << creator.desc() ; 
    if(!creator.fold_valid) return 1 ; 

    const NP* run = creator.run ; 
    std::cout << " run.desc " << run->desc() << std::endl ; 

    sleak* leak = creator.leak ; 

    //std::cout << "run->meta" << std::endl << run->meta << std::endl ; 
    std::cout << "leak->desc" << std::endl << leak->desc() << std::endl ; 
    std::cout << " saving ...  " << std::endl ; 
    leak->save("$SLEAK_FOLD"); 


    return 0 ; 
}

