// synrad_g4.cpp — Geant4 reference mode of the synrad example.
//
// Transports the SAME photons as synrad.cpp (same seed, same gun in
// synrad_gun.h) with a plain Geant4 application built the ordinary way,
// with no custom transport machinery: the geometry is
// the SynradBenchmark DetectorConstruction (analytic CSG solids, dipole
// field, absorber end plates) and the only gamma process is the SynradG4
// GammaReflectionProcess reflect-or-absorb logic evaluated on the same
// compiled-in Cu table, in double precision, with the diffuse perturbation
// done by native CLHEP setTheta/setPhi and the reflected track continued
// IN PLACE via ProposeMomentumDirection — the way the reference
// implementation does it. Wall and absorber kills are recorded as sphoton
// rows to synrad_g4_hits.npy in the same layout as the GPU mode, and the
// summary prints LOST, the number of photons that entered transport but
// were never killed at a surface (must be 0 for a valid comparison).
//
// -g tess replaces the two straight drifts by closed G4TessellatedSolid
// meshes (rectangular rings every ~15 mm, CAD-like facet counts, generated
// in code) while the arc stays analytic — the standard CPU-side way to run
// a meshed chamber, for the mesh-navigation-cost comparison. The tess mode
// is driven by its native workload, the -e electron mode: stock
// G4SynchrotronRadiation in the dipole field illuminates the drift walls,
// and LOST stays 0. The tess solids extend 1 um INTO the analytic arc so
// that entry does not cross an exactly coincident junction face (G4
// relocation into a G4TessellatedSolid fails there, and the photon would
// be lost at the junction); the vacuum-in-vacuum overlap gives a clean entry.
//
// For the transport-only Delta timing on the electron mode run once more
// with --killphotons (SR gammas stack-killed at birth): BeamOn(full) -
// BeamOn(killphotons) = pure photon transport.
//
// Usage:
//   synrad_g4 [-g analytic|tess] [-n N] [-s SEED]
//             [-r SIGMA_NM] [-T T_UM]
//             [-I x,y,z,dx,dy,dz,emin_keV,emax_keV] [-f FAN_MRAD]
//             [-i input_photons.npy] [-o OUTDIR]
//             [-e NELECTRON] [--killphotons]

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "G4Box.hh"
#include "G4Trd.hh"
#include "G4Tubs.hh"
#include "G4UnionSolid.hh"
#include "G4SubtractionSolid.hh"
#include "G4TessellatedSolid.hh"
#include "G4TriangularFacet.hh"
#include "G4LogicalVolume.hh"
#include "G4PVPlacement.hh"
#include "G4NistManager.hh"
#include "G4SystemOfUnits.hh"
#include "G4FieldManager.hh"
#include "G4UniformMagField.hh"
#include "G4RunManager.hh"
#include "G4VUserDetectorConstruction.hh"
#include "G4VUserPhysicsList.hh"
#include "G4VUserPrimaryGeneratorAction.hh"
#include "G4VUserActionInitialization.hh"
#include "G4UserStackingAction.hh"
#include "G4UserSteppingAction.hh"
#include "G4VDiscreteProcess.hh"
#include "G4ProcessManager.hh"
#include "G4Gamma.hh"
#include "G4Electron.hh"
#include "G4ParticleGun.hh"
#include "G4Event.hh"
#include "G4PrimaryVertex.hh"
#include "G4PrimaryParticle.hh"
#include "G4Track.hh"
#include "G4Step.hh"
#include "G4Navigator.hh"
#include "G4TransportationManager.hh"
#include "G4VUserTrackInformation.hh"
#include "Randomize.hh"

#include "G4EmBuilder.hh"
#include "G4eMultipleScattering.hh"
#include "G4eIonisation.hh"
#include "G4eBremsstrahlung.hh"
#include "G4SynchrotronRadiation.hh"
#include "G4StepLimiter.hh"

#include "synrad_gun.h"

// same compiled-in Cu reflectivity table as the GPU mode
#include "qudarap/qgxs_synradg4.h"

static void usage(const char* prog)
{
    std::cerr << "Usage: " << prog
              << " [-g analytic|tess] [-n N] [-s SEED] [-r SIGMA_NM] [-T T_UM]"
                 " [-I x,y,z,dx,dy,dz,emin_keV,emax_keV] [-f FAN_MRAD]"
                 " [-i input_photons.npy] [-o OUTDIR] [-e NELECTRON] [--killphotons]"
                 " [-B births.npy]\n" ;
}

// ---------------------------------------------------------------- counters
static long g_gen = 0 ;        // photons entering transport (>= 30 eV)
static long g_stackkill = 0 ;  // < 30 eV stack kills (reference behaviour)
static long g_refl = 0 ;       // total reflections
static bool g_killphotons = false ;
static bool g_recordbirths = false ;   // -B: record SR gammas at creation, kill after
static std::vector<sphoton> g_hits ;
static std::vector<sphoton> g_births ;

struct ReflInfo : public G4VUserTrackInformation { } ;   // "has reflected" tag

// ------------------------------------------------- reference reflection math
// closest-index pair with the reference FindClosestIndexesInVec edge-clamp
// semantics, double-precision twin of qgxs.h::qsg_closest
static void CuClosest( const float* v, int n, double val, int& i1, int& i2 )
{
    int lo = 0, hi = n ;
    while( lo < hi ){ int mid = (lo + hi)/2 ; if( double(v[mid]) < val ) lo = mid + 1 ; else hi = mid ; }
    i2 = lo ;
    if( lo == n ) { i2 = n - 1 ; }
    else if( double(v[i2]) == val ){ i1 = i2 ; return ; }
    i1 = i2 ? i2 - 1 : i2 ;
    i1 = ( val > double(v[i2]) ) ? i2 : i1 ;
}

// bilinear log10-log10 interpolation of the Cu table, flat-clamped
static double CuReflProb( double graz_rad, double E_eV )
{
    int ai, aj, ei, ej ;
    CuClosest( QSG_ANGLE, QSG_NA,    graz_rad, ai, aj );
    CuClosest( QSG_CU_E,  QSG_CU_NE, E_eV,     ei, ej );
    const float* P = QSG_CU_P ;
    double pii = P[ai*QSG_CU_NE+ei], pji = P[aj*QSG_CU_NE+ei], pij = P[ai*QSG_CU_NE+ej], pjj = P[aj*QSG_CU_NE+ej] ;
    double p1, p2 ;
    if( ej == ei ){ p1 = pii ; p2 = pji ; }
    else
    {
        double le = std::log10(E_eV) ;
        double k1 = (pij - pii)/(QSG_CU_LOGE[ej] - QSG_CU_LOGE[ei]) ; p1 = pii + k1*(le - QSG_CU_LOGE[ei]) ;
        double k2 = (pjj - pji)/(QSG_CU_LOGE[ej] - QSG_CU_LOGE[ei]) ; p2 = pji + k2*(le - QSG_CU_LOGE[ei]) ;
    }
    if( aj == ai ) return p1 ;
    double k = (p2 - p1)/(QSG_LOG_ANGLE[aj] - QSG_LOG_ANGLE[ai]) ;
    return p1 + k*(std::log10(graz_rad) - QSG_LOG_ANGLE[ai]) ;
}

// -------------------------------------------- reflection process (in place)
// GammaReflectionProcess type 3: tabulated kill roll, Debye-Waller
// specular/diffuse split, CLHEP setTheta/setPhi perturbation, and the
// reflected track continued on the same track via ProposeMomentumDirection.
struct NaturalReflectProcess : public G4VDiscreteProcess
{
    double sigma_mm, inv_tau ;
    NaturalReflectProcess(double sigma_mm_, double inv_tau_)
        : G4VDiscreteProcess("GammaReflection", fUserDefined),
          sigma_mm(sigma_mm_), inv_tau(inv_tau_) {}

    G4double GetMeanFreePath(const G4Track&, G4double, G4ForceCondition*) override { return DBL_MAX ; }
    G4double PostStepGetPhysicalInteractionLength(const G4Track&, G4double, G4ForceCondition* cond) override
    { *cond = Forced ; return DBL_MAX ; }

    void Kill(const G4Track& trk, const G4ThreeVector& pos, const G4ThreeVector& dir)
    {
        sphoton h ;
        memset(&h, 0, sizeof(sphoton));
        h.pos.x = float(pos.x()/mm) ; h.pos.y = float(pos.y()/mm) ; h.pos.z = float(pos.z()/mm) ;
        h.time = float(trk.GetGlobalTime()/ns) ;
        h.mom.x = float(dir.x()) ; h.mom.y = float(dir.y()) ; h.mom.z = float(dir.z()) ;
        h.pol.x = 1.f ;
        h.wavelength = float(trk.GetKineticEnergy()/keV) ;
        h.flagmask = BULK_ABSORB | ( trk.GetUserInformation() ? BOUNDARY_REFLECT : 0u ) ;
        g_hits.push_back(h);
        aParticleChange.ProposeTrackStatus(fStopAndKill);
    }

    G4VParticleChange* PostStepDoIt(const G4Track& trk, const G4Step& step) override
    {
        aParticleChange.Initialize(trk);
        const G4StepPoint* post = step.GetPostStepPoint() ;
        if( post->GetStepStatus() != fGeomBoundary ) return &aParticleChange ;
        const G4VPhysicalVolume* postPV = post->GetPhysicalVolume() ;
        const G4VPhysicalVolume* prePV  = step.GetPreStepPoint()->GetPhysicalVolume() ;
        if( postPV == nullptr || prePV == nullptr ) return &aParticleChange ;

        G4ThreeVector pos = post->GetPosition() ;
        G4ThreeVector dir = trk.GetMomentumDirection() ;

        // absorber plates kill unconditionally (reference behaviour)
        if( postPV->GetName().find("abs_") != std::string::npos ){ Kill(trk, pos, dir) ; return &aParticleChange ; }

        // only when leaving the vacuum or the magnet and entering the world
        if( postPV->GetName() != "world" ) return &aParticleChange ;
        if( prePV->GetName().find("vac_") == std::string::npos &&
            prePV->GetName().find("mag_") == std::string::npos ) return &aParticleChange ;

        G4bool valid = false ;
        G4Navigator* nav = G4TransportationManager::GetTransportationManager()->GetNavigatorForTracking() ;
        G4ThreeVector n = nav->GetGlobalExitNormal(pos, &valid) ;   // out of the vacuum, into the wall
        if( !valid ){ Kill(trk, pos, dir) ; return &aParticleChange ; }

        double sin_g = std::min(std::abs(dir.dot(n)), 1.) ;
        double graz  = std::asin(sin_g) ;
        double E_keV = trk.GetKineticEnergy()/keV ;

        double prob = CuReflProb( graz, E_keV*1000. ) ;
        double kiz  = sin_g*E_keV/1.9732698e-7 ;                    // hbar*c in keV*mm
        double argw = 2.*(kiz*sigma_mm)*(kiz*sigma_mm) ;
        double probSpec = argw < 700. ? std::exp(-argw) : 0. ;      // Debye-Waller specular fraction

        G4ThreeVector rdir = dir - 2.*dir.dot(n)*n ;                // specular
        if( G4UniformRand() > probSpec && inv_tau > 0. )            // diffuse: perturb GLOBAL theta/phi
        {
            double alpha  = std::acos(sin_g) ;                      // angle(mom, outward normal)
            double sig_th = 2.9267*inv_tau ;
            double sig_ph = ( 2.80657*std::pow(alpha, -1.00238) - 1.00293*std::pow(alpha, 1.2266) )*inv_tau ;
            if( sig_ph < 0. ) sig_ph = 0. ;
            double th0 = rdir.theta(), ph0 = rdir.phi() ;
            for(int i=0 ; i < 1000 ; i++)
            {
                G4ThreeVector cand(rdir);
                cand.setTheta( th0 + G4RandGauss::shoot(0., sig_th) );   // native CLHEP pole fold
                cand.setPhi(   ph0 + G4RandGauss::shoot(0., sig_ph) );
                if( cand.dot(n) < 0. ){ rdir = cand ; break ; }
                // sampled into the surface: retry; keep unperturbed specular after 1000 tries
            }
        }

        if( G4UniformRand() > prob ){ Kill(trk, pos, dir) ; return &aParticleChange ; }  // absorbed at the wall

        // reflect in place, exactly as the reference GammaReflectionProcess
        g_refl += 1 ;
        if( trk.GetUserInformation() == nullptr )
            const_cast<G4Track&>(trk).SetUserInformation(new ReflInfo);
        aParticleChange.ProposeMomentumDirection(rdir.unit());
        return &aParticleChange ;
    }
};

// -------------------------------------------------------- tessellated drifts
// Closed rectangular-ring meshes of the two drift solids, world frame,
// ~15 mm ring pitch (2668 / 21332 facets), ends capped, closed.
struct Ring { G4ThreeVector c, ex ; double h ; };

static const double OLAP = 0.001 ;   // mm, overlap into the analytic arc

static std::vector<double> Subdiv(double L, double step)
{
    int nseg = std::max(1, int(std::lround(L/step)));
    std::vector<double> s(nseg + 1);
    for(int i=0 ; i <= nseg ; i++) s[i] = L*i/nseg ;
    return s ;
}

static std::vector<Ring> Drift1Rings()
{
    std::vector<Ring> r ;
    for(double z : Subdiv(5000.0, 15.0)) r.push_back({ {0,0,z}, {1,0,0}, 25.0 });
    r.back().c += G4ThreeVector(0, 0, OLAP);
    return r ;
}

static std::vector<Ring> Drift2Rings(G4ThreeVector& dv_out)
{
    const double A = 0.010 ;
    G4ThreeVector dv(-std::sin(A), 0, std::cos(A));                  // drift2 axis
    G4ThreeVector ex( std::cos(A), 0, std::sin(A));
    G4ThreeVector P0(-5000.0*std::sin(A/2.), 0, 5000.0 + 5000.0*std::cos(A/2.));
    struct Seg { double L, h0, h1 ; };
    const Seg segs[5] = { {10000,25,25}, {5000,25,20}, {5000,20,20}, {5000,20,10}, {15000,10,10} };
    std::vector<Ring> r ;
    double s0 = 0. ;
    for(const Seg& g : segs)
    {
        std::vector<double> ss = Subdiv(g.L, 15.0);
        for(size_t i = r.empty() ? 0 : 1 ; i < ss.size() ; i++)
        {
            double h = g.h0 + (g.h1 - g.h0)*ss[i]/g.L ;
            r.push_back({ P0 + (s0 + ss[i])*dv, ex, h });
        }
        s0 += g.L ;
    }
    r.front().c -= OLAP*dv ;
    dv_out = dv ;
    return r ;
}

// inA/inB: directions pointing INTO the pipe at the start/end caps, used as
// orientation references so every facet normal points out of the vacuum
static G4TessellatedSolid* MakeTess(const G4String& name, const std::vector<Ring>& rings,
                                    const G4ThreeVector& inA, const G4ThreeVector& inB)
{
    const G4ThreeVector ey(0,1,0);
    std::vector<G4ThreeVector> v ; v.reserve(rings.size()*4);
    for(const Ring& r : rings)
    {
        v.push_back(r.c + r.h*r.ex + r.h*ey);
        v.push_back(r.c + r.h*r.ex - r.h*ey);
        v.push_back(r.c - r.h*r.ex - r.h*ey);
        v.push_back(r.c - r.h*r.ex + r.h*ey);
    }
    G4TessellatedSolid* ts = new G4TessellatedSolid(name);
    std::map<std::pair<int,int>, int> edges ;
    long nfac = 0 ;
    auto add = [&](int i, int j, int k, const G4ThreeVector& ref)
    {
        if( (v[j]-v[i]).cross(v[k]-v[i]).dot((v[i]+v[j]+v[k])/3. - ref) < 0. ) std::swap(j, k);
        ts->AddFacet(new G4TriangularFacet(v[i], v[j], v[k], ABSOLUTE));
        nfac += 1 ;
        int t[3] = { i, j, k } ;
        for(int e=0 ; e < 3 ; e++)
            edges[{ std::min(t[e], t[(e+1)%3]), std::max(t[e], t[(e+1)%3]) }] += 1 ;
    };
    for(size_t r=0 ; r+1 < rings.size() ; r++)
    {
        G4ThreeVector mid = (rings[r].c + rings[r+1].c)/2. ;
        int A = 4*int(r), B = A + 4 ;
        for(int k=0 ; k < 4 ; k++)
        {
            int k2 = (k + 1) % 4 ;
            add(A+k, A+k2, B+k2, mid);
            add(A+k, B+k2, B+k,  mid);
        }
    }
    int A = 0, B = 4*int(rings.size() - 1);
    G4ThreeVector refA = rings.front().c + inA, refB = rings.back().c + inB ;
    add(A+0, A+1, A+2, refA); add(A+0, A+2, A+3, refA);
    add(B+0, B+1, B+2, refB); add(B+0, B+2, B+3, refB);
    for(const auto& e : edges)
        if( e.second != 2 ){ std::cerr << "FATAL: " << name << " not closed\n" ; exit(2) ; }
    ts->SetSolidClosed(true);
    G4cout << "[synrad-g4] " << name << " " << nfac << " facets" << G4endl ;
    return ts ;
}

// -------------------------------------------------------------- geometry
// the SynradBenchmark DetectorConstruction: drift1 box + drift2 union chain
// fused into one vacuum union, analytic dipole arc with the field, absorber
// end plates with the pipe apertures carved
struct Detector : public G4VUserDetectorConstruction
{
    bool tess ;
    Detector(bool tess_) : tess(tess_) {}

    G4VPhysicalVolume* Construct() override
    {
        G4Material* vac = G4NistManager::Instance()->FindOrBuildMaterial("G4_Galactic");

        G4VSolid* world_box = new G4Box("world", 2*m, 2*m, 100*m);
        G4LogicalVolume* world_log = new G4LogicalVolume(world_box, vac, "world");
        G4VPhysicalVolume* world_phys =
            new G4PVPlacement(0, {}, world_log, "world", 0, false, 0, true);

        G4double D1_L = 5*m, B1_L = 5*m, B1_A = 0.010*rad ;
        G4double B1_R = (B1_L/2.)/std::sin(B1_A/2.);

        G4Box* drift1_box = new G4Box("drift1_box", 25*mm, 25*mm, D1_L/2.);
        G4VSolid* drift2 = new G4Box("drift2_box", 25*mm, 25*mm, 5*m);
        drift2 = new G4UnionSolid("d2u1", drift2, new G4Trd("d2t1",25*mm,20*mm,25*mm,20*mm,2.5*m), 0, {0,0,7500});
        drift2 = new G4UnionSolid("d2u2", drift2, new G4Box("d2b2",20*mm,20*mm,2.5*m),             0, {0,0,12500});
        drift2 = new G4UnionSolid("d2u3", drift2, new G4Trd("d2t3",20*mm,10*mm,20*mm,10*mm,2.5*m), 0, {0,0,17500});
        drift2 = new G4UnionSolid("d2u4", drift2, new G4Box("d2b4",10*mm,10*mm,7.5*m),             0, {0,0,27500});

        if( !tess )
        {
            G4RotationMatrix* rot = new G4RotationMatrix(); rot->rotateY(B1_A);
            G4VSolid* pipe = new G4UnionSolid("beampipeVac_solid", drift1_box, drift2, rot,
                G4ThreeVector( -(B1_L)*std::sin(B1_A/2.) - (5*m)*std::sin(B1_A), 0,
                                D1_L/2 + (B1_L)*std::cos(B1_A/2.) + (5*m)*std::cos(B1_A)));
            G4LogicalVolume* lv = new G4LogicalVolume(pipe, vac, "beampipeVac");
            new G4PVPlacement(0, {0,0,D1_L/2.}, lv, "vac_beampipe", world_log, false, 0, true);
        }
        else
        {
            G4ThreeVector dv ;
            G4LogicalVolume* bp1 = new G4LogicalVolume(
                MakeTess("drift1_tess", Drift1Rings(), {0,0,1}, {0,0,-1}), vac, "bp1");
            std::vector<Ring> r2 = Drift2Rings(dv);
            G4LogicalVolume* bp2 = new G4LogicalVolume(
                MakeTess("drift2_tess", r2, dv, -dv), vac, "bp2");
            new G4PVPlacement(0, {}, bp1, "vac_beampipe1", world_log, false, 0, true);
            new G4PVPlacement(0, {}, bp2, "vac_beampipe2", world_log, false, 0, true);
        }

        // dipole arc vacuum + field
        G4Tubs* dip = new G4Tubs("dipole_tube", B1_R-25*mm, B1_R+25*mm, 25*mm, 0, B1_A);
        G4LogicalVolume* dip_log = new G4LogicalVolume(dip, vac, "dipole_log");
        double mass = G4Electron::Electron()->GetPDGMass() ;
        double gamma = 35225.121 ;
        G4double By = ((3.33564*(mass/GeV)*std::sqrt(gamma*gamma - 1.0))/
                       ((B1_L/m)/(2.*std::sin(B1_A/2.))))*tesla ;
        G4MagneticField* fld = new G4UniformMagField(G4ThreeVector(0,-By,0));
        G4FieldManager* fm = new G4FieldManager(fld);
        fm->SetDetectorField(fld); fm->CreateChordFinder(fld);
        dip_log->SetFieldManager(fm, true);
        G4RotationMatrix* drot = new G4RotationMatrix(); drot->rotateX(-M_PI/2.);
        // no overlap check in tess mode: the drifts overlap the arc by the
        // designed 1 um (vacuum in vacuum), which the check would report
        new G4PVPlacement(drot, {-B1_R,0,D1_L}, dip_log, "mag_dipole", world_log, false, 0, !tess);

        // absorber end plates, pipe apertures carved
        G4Box* abs_box = new G4Box("abs_box", 0.5*m, 0.5*m, 2.5*cm);
        G4VSolid* a0 = new G4SubtractionSolid("abs_solid_0", abs_box, drift1_box, 0, {0,0,D1_L/2.});
        G4VSolid* a1 = new G4SubtractionSolid("abs_solid_1", abs_box,
            new G4Box("endbox",10.2*mm,10.2*mm,5*cm), 0, {-25.0*mm, 0, 0});
        new G4PVPlacement(0, {0,0,0},        new G4LogicalVolume(a0, vac, "abs_log_0"), "abs_0", world_log, false, 0, true);
        new G4PVPlacement(0, {-0.4*m,0,50*m},new G4LogicalVolume(a1, vac, "abs_log_1"), "abs_1", world_log, false, 0, true);

        return world_phys ;
    }
};

// -------------------------------------------------------------- physics
struct Physics : public G4VUserPhysicsList
{
    bool electrons ; double sigma_mm, inv_tau ;
    Physics(bool e, double s, double it) : electrons(e), sigma_mm(s), inv_tau(it) {}
    void ConstructParticle() override
    {
        G4EmBuilder::ConstructMinimalEmSet();
    }
    void ConstructProcess() override
    {
        AddTransportation();
        G4Gamma::Gamma()->GetProcessManager()->AddDiscreteProcess(
            new NaturalReflectProcess(sigma_mm, inv_tau));
        if( electrons )
        {
            G4ProcessManager* pm = G4Electron::Electron()->GetProcessManager();
            pm->AddProcess(new G4eMultipleScattering, -1, 1, -1);
            pm->AddProcess(new G4eIonisation,         -1, 2,  1);
            pm->AddProcess(new G4eBremsstrahlung,     -1, 3,  2);
            pm->AddProcess(new G4SynchrotronRadiation,-1,-1,  3);
            pm->AddProcess(new G4StepLimiter,         -1,-1,  4);
        }
    }
};

// ---------------------------------------------- stacking: 30 eV cut + counters
struct Stacking : public G4UserStackingAction
{
    G4ClassificationOfNewTrack ClassifyNewTrack(const G4Track* trk) override
    {
        if( trk->GetParticleDefinition() == G4Gamma::Gamma() && trk->GetParentID() != 0 )
        {
            if( trk->GetTotalEnergy() < 30.0*eV ){ g_stackkill += 1 ; return fKill ; }
            g_gen += 1 ;
            if( g_recordbirths )
            {
                // record the SR photon as emitted (sphoton layout:
                // pos+time | direction | polarization | E_keV) and kill it:
                // a generation-only run whose births feed -i transport
                sphoton p = {} ;
                const G4ThreeVector& x = trk->GetPosition() ;
                const G4ThreeVector& d = trk->GetMomentumDirection() ;
                const G4ThreeVector& q = trk->GetPolarization() ;
                p.pos.x = float(x.x()/mm) ; p.pos.y = float(x.y()/mm) ; p.pos.z = float(x.z()/mm) ;
                p.time  = float(trk->GetGlobalTime()/ns) ;
                p.mom.x = float(d.x()) ; p.mom.y = float(d.y()) ; p.mom.z = float(d.z()) ;
                p.pol.x = float(q.x()) ; p.pol.y = float(q.y()) ; p.pol.z = float(q.z()) ;
                p.wavelength = float(trk->GetTotalEnergy()/keV) ;
                g_births.push_back(p) ;
                return fKill ;
            }
            if( g_killphotons ) return fKill ;   // Delta method: kill at birth
        }
        return fUrgent ;
    }
};

// kill the primary e- when it leaves the vacuum (reference behaviour)
struct Stepping : public G4UserSteppingAction
{
    void UserSteppingAction(const G4Step* step) override
    {
        G4Track* trk = step->GetTrack() ;
        if( trk->GetParentID() != 0 ) return ;
        if( trk->GetParticleDefinition() != G4Electron::Electron() ) return ;
        const G4StepPoint* post = step->GetPostStepPoint() ;
        if( post->GetStepStatus() != fGeomBoundary ) return ;
        const G4VPhysicalVolume* pv = post->GetPhysicalVolume() ;
        if( pv && ( pv->GetName() == "world" || pv->GetName().find("abs_") != std::string::npos ) )
            trk->SetTrackStatus(fStopAndKill);
    }
};

// ------------------------------------------------------------------ guns
// photon primaries fed from the shared NP array (one event)
struct PhotonFeeder : public G4VUserPrimaryGeneratorAction
{
    const NP* ip ;
    PhotonFeeder(const NP* ip_) : ip(ip_) {}
    void GeneratePrimaries(G4Event* evt) override
    {
        const sphoton* pp = reinterpret_cast<const sphoton*>(ip->cvalues<float>()) ;
        int n = ip->shape[0] ;
        for(int i=0 ; i < n ; i++)
        {
            const sphoton& p = pp[i] ;
            auto* vtx = new G4PrimaryVertex( G4ThreeVector(p.pos.x, p.pos.y, p.pos.z)*mm, double(p.time)*ns );
            auto* prt = new G4PrimaryParticle( G4Gamma::Gamma() );
            prt->SetKineticEnergy( double(p.wavelength)*keV );
            prt->SetMomentumDirection( G4ThreeVector(p.mom.x, p.mom.y, p.mom.z) );
            vtx->SetPrimary(prt);
            evt->AddPrimaryVertex(vtx);
            g_gen += 1 ;
        }
    }
};

struct ElectronGun : public G4VUserPrimaryGeneratorAction
{
    G4ParticleGun* gun ;
    ElectronGun()
    {
        gun = new G4ParticleGun(1);
        double mass = G4Electron::Electron()->GetPDGMass() ;
        double gamma = 35225.121 ;                          // the reference beam
        gun->SetParticleDefinition(G4Electron::Electron());
        gun->SetParticleEnergy( mass*(gamma - 1.0) );
        gun->SetParticleMomentumDirection({0,0,1});
        gun->SetParticlePosition({0,0,0});
    }
    void GeneratePrimaries(G4Event* evt) override { gun->GeneratePrimaryVertex(evt); }
};

struct Actions : public G4VUserActionInitialization
{
    G4VUserPrimaryGeneratorAction* gun ; bool electrons ;
    Actions(G4VUserPrimaryGeneratorAction* g, bool e) : gun(g), electrons(e) {}
    void Build() const override
    {
        SetUserAction(gun);
        SetUserAction(new Stacking);
        if( electrons ) SetUserAction(new Stepping);
    }
};

// ------------------------------------------------------------------ main
int main(int argc, char** argv)
{
    std::string geo = "analytic" ;
    std::string input ;
    std::string births ;
    std::string outdir = "." ;
    int      n = 1000000 ;
    long     nel = 0 ;
    unsigned seed = 42 ;
    double   sigma_nm = 50. ;
    double   T_um = 10. ;
    double   U[2] = { 25.0, 49975.0 } ;                    // end-plate z, for the summary
    double   I[8] = { 0., 0., 100., 0., 0., 1., 0.3, 19.4 } ;
    double   fan_mrad = 0.5 ;

    for(int i=1 ; i < argc ; i++)
    {
        std::string arg = argv[i] ;
        if(      arg == "-g" && i+1 < argc ) geo = argv[++i] ;
        else if( arg == "-i" && i+1 < argc ) input = argv[++i] ;
        else if( arg == "-o" && i+1 < argc ) outdir = argv[++i] ;
        else if( arg == "-n" && i+1 < argc ) n = atoi(argv[++i]) ;
        else if( arg == "-e" && i+1 < argc ) nel = atol(argv[++i]) ;
        else if( arg == "-s" && i+1 < argc ) seed = unsigned(atoi(argv[++i])) ;
        else if( arg == "-r" && i+1 < argc ) sigma_nm = atof(argv[++i]) ;
        else if( arg == "-T" && i+1 < argc ) T_um = atof(argv[++i]) ;
        else if( arg == "-f" && i+1 < argc ) fan_mrad = atof(argv[++i]) ;
        else if( arg == "-I" && i+1 < argc ) sscanf(argv[++i], "%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf", I, I+1, I+2, I+3, I+4, I+5, I+6, I+7) ;
        else if( arg == "-B" && i+1 < argc ) { births = argv[++i] ; g_recordbirths = true ; }
        else if( arg == "--killphotons" )    g_killphotons = true ;
        else if( arg == "-h" || arg == "--help" ){ usage(argv[0]) ; return 0 ; }
        else { std::cerr << "Unknown argument: " << arg << "\n" ; usage(argv[0]) ; return 1 ; }
    }
    if( geo != "analytic" && geo != "tess" ){ usage(argv[0]) ; return 1 ; }
    bool electrons = ( nel > 0 ) ;

    NP* ip = nullptr ;
    if( !electrons )
    {
        if( !input.empty() )
        {
            ip = NP::Load(input.c_str());
            if( ip == nullptr || ip->shape.size() != 3 ){ std::cerr << "FATAL: failed to load " << input << "\n" ; return 3 ; }
            n = ip->shape[0] ;
        }
        else
        {
            ip = GeneratePhotons(n, seed, I, fan_mrad);   // same seed -> identical photons in both modes
        }
    }

    double sigma_mm = sigma_nm*1e-6 ;
    double T_mm = T_um*1e-3 ;
    double inv_tau = T_mm > 0. ? sigma_mm/T_mm : 0. ;

    G4Random::setTheSeed(seed);
    g_hits.reserve(electrons ? 0 : n);

    G4RunManager* run = new G4RunManager ;
    run->SetUserInitialization(new Detector(geo == "tess"));
    run->SetUserInitialization(new Physics(electrons, sigma_mm, inv_tau));
    run->SetUserInitialization(new Actions(
        electrons ? (G4VUserPrimaryGeneratorAction*)new ElectronGun
                  : (G4VUserPrimaryGeneratorAction*)new PhotonFeeder(ip), electrons));
    run->Initialize();

    auto t0 = std::chrono::steady_clock::now();
    run->BeamOn( electrons ? int(nel) : 1 );
    auto t1 = std::chrono::steady_clock::now();
    double t_s = std::chrono::duration<double>(t1 - t0).count();

    if( g_recordbirths )
    {
        SaveHits(g_births, births);
        printf("synrad-g4-births: electrons %ld -> %ld SR photons >=30eV (%.2f/e-) stackkill<30eV %ld -> %s\n",
               nel, long(g_births.size()), nel > 0 ? double(g_births.size())/double(nel) : 0.,
               g_stackkill, births.c_str());
    }

    std::string path = outdir + "/synrad_g4_hits.npy" ;
    SaveHits(g_hits, path);
    Summary("synrad-g4", g_gen, g_hits, U, t_s, path);
    printf("synrad-g4-stats: geometry %s%s%s | generated %ld stackkill<30eV %ld recorded %ld LOST %ld | reflections %ld\n",
           geo.c_str(), electrons ? " electrons" : "", g_killphotons ? " killphotons" : "",
           g_gen, g_stackkill, long(g_hits.size()),
           g_killphotons ? 0 : g_gen - long(g_hits.size()), g_refl);
    delete run ;
    return 0 ;
}
