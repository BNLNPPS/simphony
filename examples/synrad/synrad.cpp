// synrad.cpp — GPU synchrotron-radiation soft X-ray transport example.
//
// Loads a tessellated SR beam-chamber GDML (the SynradBenchmark tunnel: a
// straight drift, a 10 mrad arc and a second drift, fused into one closed
// tessellated solid), generates X-ray photons flying down the chamber (or
// reads them from a .npy file) and transports them on the GPU: photons fly
// at c through the vacuum and reflect-or-absorb at every vacuum->wall contact
// with tabulated grazing-incidence Cu reflectivity, the reflect-or-absorb model
// of the SynradG4 benchmark GammaReflectionProcess
// (https://github.com/eicorg/SynradBenchmark). See qudarap/qgxs.h.
//
// The default pencil beam enters the first drift along +z: the chamber bends
// away under it, so the photons graze the outer arc wall at mrad angles and
// skate downstream through reflection chains until absorbed. Photons killed
// at the wall are the hits: sphoton rows (pos+time | mom | pol | E_keV in
// the wavelength slot) saved to synrad_hits.npy.
//
// Usage:
//   synrad -g synrad_bench.gdml [-n N] [-s SEED]
//          [-r SIGMA_NM] [-T T_UM] [-U ZLO,ZHI]
//          [-I x,y,z,dx,dy,dz,emin_keV,emax_keV] [-f FAN_MRAD]
//          [-i input_photons.npy] [-o OUTDIR]
//
//   -r/-T         : RMS roughness [nm] / autocorrelation length [um] of the
//                   Debye-Waller specular/diffuse split (defaults 50 / 10)
//   -U ZLO,ZHI    : absorber end planes [mm] (default 25.0,49975.0 = the
//                   benchmark tunnel end plates)
//   -I ...        : photon gun: start position [mm], direction, log-uniform
//                   energy range [keV] (default 0,0,100,0,0,1,0.3,19.4)
//   -f FAN_MRAD   : Gaussian angular spread of the gun [mrad] (default 0.5)
//   -i FILE.npy   : (N,4,4) float32 sphoton input photons instead of the gun

#include <chrono>
#include <cstdlib>
#include <string>

#include "synrad_gun.h"

#include "sysrap/OPTICKS_LOG.hh"
#include "sysrap/SEventConfig.hh"
#include "sysrap/SEvt.hh"

#include "g4cx/G4CXOpticks.hh"
#include "qudarap/QGXS.hh"
#include "qudarap/QSim.hh"

static void usage(const char* prog)
{
    std::cerr << "Usage: " << prog
              << " -g GDML [-n N] [-s SEED] [-r SIGMA_NM] [-T T_UM]"
                 " [-U ZLO,ZHI] [-I x,y,z,dx,dy,dz,emin_keV,emax_keV] [-f FAN_MRAD]"
                 " [-i input_photons.npy] [-o OUTDIR]\n" ;
}

static int RunGPU(const std::string& gdml, NP* ip, int argc, char** argv,
                  double sigma_mm, double T_mm, const double* U,
                  const std::string& outdir)
{
    int n = ip->shape[0] ;

    // route the tessellated chamber solids to the triangulated geometry path
    // (U4Solid only creates an AABB placeholder for G4TessellatedSolid)
    setenv("stree__force_triangulate_solid", "SynradBenchEnvelope_pmesh,SynradBenchEnvelopeFine_pmesh", 0);

    OPTICKS_LOG(argc, argv);

    SEventConfig::SetEventMode("Hit");
    SEventConfig::SetHitMask("AB");        // gamma-mode wall kills carry BULK_ABSORB
    SEventConfig::SetMaxPhoton(n);
    // OPTICKS_MAX_BOUNCE/OPTICKS_MAX_SLOT envs are read at static init, so a
    // setenv here would be ignored: use the setters. Grazing reflection chains
    // reach tens of bounces (default 31 truncates), and the slot heuristic
    // otherwise sizes the photon buffers to ~87% of total VRAM.
    if( getenv("OPTICKS_MAX_BOUNCE") == nullptr ) SEventConfig::SetMaxBounce(100);
    if( getenv("OPTICKS_MAX_SLOT")   == nullptr ) SEventConfig::SetMaxSlot(n);

    // instanciating an SEvt runs SEventConfig::Initialize (CUDA device probe);
    // without it G4CXOpticks::SetGeometry sees HasDevice()==false and silently
    // skips the whole CSGOptiX/QSim GPU setup
    SEvt::CreateOrReuse_ECPU();

    G4CXOpticks* gx = G4CXOpticks::SetGeometry(gdml.c_str());

    QGXS qgxs_{ float(sigma_mm), float(T_mm), float(U[0]), float(U[1]) };
    QSim* qs = QSim::Get();
    if( qs == nullptr ){ std::cerr << "FATAL: no QSim -- GPU setup was skipped (no CUDA device?)\n" ; return 2 ; }
    qs->setGXS(qgxs_.d_gxs);

    SEvt::SetInputPhoton(ip);

    auto t0 = std::chrono::steady_clock::now();
    gx->simulate(0, false);
    auto t1 = std::chrono::steady_clock::now();
    double t_s = std::chrono::duration<double>(t1 - t0).count();

    SEvt* sev = SEvt::Get_EGPU();
    int64_t nhit = SEvt::GetNumHit_EGPU();

    std::vector<sphoton> hits(size_t(nhit > 0 ? nhit : 0));
    for(int64_t i=0 ; i < nhit ; i++) sev->getHit(hits[i], unsigned(i));

    std::string path = outdir + "/synrad_hits.npy" ;
    SaveHits(hits, path);
    Summary("synrad", n, hits, U, t_s, path);
    return 0 ;
}

int main(int argc, char** argv)
{
    std::string gdml ;
    std::string input ;
    std::string outdir = "." ;
    int      n = 1000000 ;
    unsigned seed = 42 ;
    double   sigma_nm = 50. ;
    double   T_um = 10. ;
    double   U[2] = { 25.0, 49975.0 } ;                    // benchmark tunnel end plates, mm
    double   I[8] = { 0., 0., 100., 0., 0., 1., 0.3, 19.4 } ;
    double   fan_mrad = 0.5 ;

    for(int i=1 ; i < argc ; i++)
    {
        std::string arg = argv[i] ;
        if(      arg == "-g" && i+1 < argc ) gdml = argv[++i] ;
        else if( arg == "-i" && i+1 < argc ) input = argv[++i] ;
        else if( arg == "-o" && i+1 < argc ) outdir = argv[++i] ;
        else if( arg == "-n" && i+1 < argc ) n = atoi(argv[++i]) ;
        else if( arg == "-s" && i+1 < argc ) seed = unsigned(atoi(argv[++i])) ;
        else if( arg == "-r" && i+1 < argc ) sigma_nm = atof(argv[++i]) ;
        else if( arg == "-T" && i+1 < argc ) T_um = atof(argv[++i]) ;
        else if( arg == "-f" && i+1 < argc ) fan_mrad = atof(argv[++i]) ;
        else if( arg == "-U" && i+1 < argc ) sscanf(argv[++i], "%lf,%lf", U, U+1) ;
        else if( arg == "-I" && i+1 < argc ) sscanf(argv[++i], "%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf", I, I+1, I+2, I+3, I+4, I+5, I+6, I+7) ;
        else if( arg == "-h" || arg == "--help" ){ usage(argv[0]) ; return 0 ; }
        else { std::cerr << "Unknown argument: " << arg << "\n" ; usage(argv[0]) ; return 1 ; }
    }
    if( gdml.empty() ){ usage(argv[0]) ; return 1 ; }

    NP* ip = nullptr ;
    if( !input.empty() )
    {
        ip = NP::Load(input.c_str());
        if( ip == nullptr || ip->shape.size() != 3 ){ std::cerr << "FATAL: failed to load " << input << "\n" ; return 3 ; }
        n = ip->shape[0] ;
    }
    else
    {
        ip = GeneratePhotons(n, seed, I, fan_mrad);   // same seed -> identical photons in both arms
    }

    double sigma_mm = sigma_nm*1e-6 ;
    double T_mm = T_um*1e-3 ;

    return RunGPU(gdml, ip, argc, argv, sigma_mm, T_mm, U, outdir) ;
}
