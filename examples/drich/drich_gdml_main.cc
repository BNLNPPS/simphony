// drich_gdml_main.cc
// Standalone (no-DD4hep) Geant4 driver loading dRICH from GDML and routing
// Cherenkov gensteps to Opticks GPU.
//
// Build (in simphony devcontainer where eic-opticks is built):
//   g++ -std=c++17 drich_gdml_main.cc \
//     $(geant4-config --cflags) $(geant4-config --libs) \
//     -I${OPTICKS_PREFIX}/include/eic-opticks \
//     -L${OPTICKS_PREFIX}/lib -lG4CX -lU4 -lQUDARap -lSysRap -lCSG -lCSGOptiX \
//     -o drich_gdml_main
//
// Run:
//   GDML_FILE=drich_ag03.gdml MULT=100 SEED=12345 ./drich_gdml_main
//
// Output: photon counts and hit counts per event, matching the DD4hep flow.

#include <cstdlib>
#include <iostream>
#include <fstream>
#include <vector>
#include <iomanip>
#include <cmath>
#include <algorithm>

#include <G4GDMLParser.hh>
#include <G4RunManager.hh>
#include <G4VUserDetectorConstruction.hh>
#include <G4VUserPrimaryGeneratorAction.hh>
#include <G4ParticleGun.hh>
#include <G4ParticleTable.hh>
#include <G4SystemOfUnits.hh>
#include <G4VPhysicalVolume.hh>
#include <G4VModularPhysicsList.hh>
#include <FTFP_BERT.hh>
#include <G4OpticalPhysics.hh>
#include <G4EmSaturation.hh>
#include <G4Material.hh>
#include <G4MaterialPropertiesTable.hh>
#include <G4PhysicsFreeVector.hh>
#include <G4MaterialTable.hh>
#include <G4ProductionCuts.hh>
#include <G4Region.hh>
#include <G4RegionStore.hh>
#include <G4ProductionCutsTable.hh>
#include <G4LogicalVolumeStore.hh>
#include <G4LogicalSkinSurface.hh>
#include <G4LogicalBorderSurface.hh>
#include <G4OpticalSurface.hh>
#include <G4VSensitiveDetector.hh>
#include <G4SDManager.hh>
#include <G4HCofThisEvent.hh>
#include <G4PVPlacement.hh>

#include <G4UserRunAction.hh>
#include <G4UserEventAction.hh>
#include <G4UserSteppingAction.hh>
#include <G4UserStackingAction.hh>
#include <G4ClassificationOfNewTrack.hh>
#include <G4OpticalPhoton.hh>
#include <G4OpBoundaryProcess.hh>
#include <G4ProcessManager.hh>
#include <G4ProcessVector.hh>
#include <map>
#include <tuple>
#include <G4Track.hh>
#include <G4Step.hh>
#include <G4Event.hh>
#include <Randomize.hh>

// eic-opticks
#include <G4CXOpticks.hh>
#include <SEvt.hh>
#include <SSim.hh>
#include <NP.hh>
#include <snam.h>
#include <SEventConfig.hh>
#include <U4Physics.hh>
#include <U4SensorIdentifier.h>

#include <set>

// --- detector construction ----------------------------------------------

// DD4hep exports GDML with property-table energies in its native unit (GeV).
// Geant4 reads GDML matrix values as raw G4-internal numbers (MeV-based).
// Result: every RINDEX/ABSLENGTH/EFFICIENCY/REFLECTIVITY spectrum has photon
// energies 1000x too small, pushing the spectrum out of any physical range
// and suppressing Cherenkov yield + photon detection. We rescale every
// property table's energy axis here, on materials AND optical surfaces.
static void RescaleMPT(G4MaterialPropertiesTable* mpt, double factor)
{
    if (!mpt)
        return;
    const std::vector<G4String>& names = mpt->GetMaterialPropertyNames();
    for (size_t k = 0; k < names.size(); ++k)
    {
        G4MaterialPropertyVector* pv = mpt->GetProperty(names[k].c_str());
        if (!pv)
            continue;
        G4PhysicsFreeVector* fv = dynamic_cast<G4PhysicsFreeVector*>(pv);
        if (!fv)
            continue;
        G4int n = fv->GetVectorLength();
        for (G4int j = 0; j < n; ++j)
        {
            G4double e = fv->Energy(j) * factor;
            G4double v = (*fv)[j];
            fv->PutValues(j, e, v);
        }
    }
}

static void RescaleDD4hepPropertyEnergies(double factor)
{
    // Materials
    G4MaterialTable* mt = G4Material::GetMaterialTable();
    if (mt)
    {
        for (size_t i = 0; i < mt->size(); ++i)
        {
            G4Material* mat = (*mt)[i];
            if (!mat)
                continue;
            RescaleMPT(mat->GetMaterialPropertiesTable(), factor);
        }
    }
    // Skin surfaces
    const G4LogicalSkinSurfaceTable* skinTab = G4LogicalSkinSurface::GetSurfaceTable();
    if (skinTab)
    {
        for (auto it = skinTab->begin(); it != skinTab->end(); ++it)
        {
            G4LogicalSkinSurface* s = it->second;
            if (!s)
                continue;
            G4OpticalSurface* os = dynamic_cast<G4OpticalSurface*>(s->GetSurfaceProperty());
            if (!os)
                continue;
            RescaleMPT(os->GetMaterialPropertiesTable(), factor);
        }
    }
    // Border surfaces
    const G4LogicalBorderSurfaceTable* borderTab = G4LogicalBorderSurface::GetSurfaceTable();
    if (borderTab)
    {
        for (auto it = borderTab->begin(); it != borderTab->end(); ++it)
        {
            G4LogicalBorderSurface* s = it->second;
            if (!s)
                continue;
            G4OpticalSurface* os = dynamic_cast<G4OpticalSurface*>(s->GetSurfaceProperty());
            if (!os)
                continue;
            RescaleMPT(os->GetMaterialPropertiesTable(), factor);
        }
    }
}

// Dummy sensitive detector that just registers each step. We only need its
// presence (and the GetSensitiveDetector() lookup) for U4SensorIdentifierDefault
// to recognise a logical volume as a sensor.
class DRICH_DummySD : public G4VSensitiveDetector
{
  public:
    DRICH_DummySD(const G4String& name) :
        G4VSensitiveDetector(name)
    {
    }
    void Initialize(G4HCofThisEvent*) override
    {
    }
    G4bool ProcessHits(G4Step*, G4TouchableHistory*) override
    {
        return true;
    }
};

// Set of LVs (by pointer) flagged as sensors during ConstructSDandField.
static std::set<const G4LogicalVolume*> gSensorLVs;

// Per-event CPU-side hit count (filled by DRICH_SteppingAction).
static long gCpuHitsThisEvent = 0;
static long gCpuOpKillsThisEvent = 0;  // diagnostic: any OpBoundary kill
static long gCpuOpAbsorbThisEvent = 0; // diagnostic: status==Absorption
static long gCpuOpDetectThisEvent = 0; // diagnostic: status==Detection
static long gCpuOpReflThisEvent = 0;   // diagnostic: status reflection-like

// Step-trace diagnostic: when STEP_TRACE_EVT == current event id, log
// every Nth step (STEP_TRACE_EVERY, default 1) for any track whose
// step number has exceeded STEP_TRACE_MIN. Per-event histogram of
// track step counts is printed at end of event regardless.
static int                                                                                           gCurrentEventID = -1;
static std::map<int, int>                                                                            gStepHistThisEvent; // bin (log2-ish) -> count
static std::map<int, std::tuple<std::string, int, int, double, double, double, double, std::string>> gMaxStepPerTrack;
// trackID -> (particle, steps, parentID, last_x_mm, last_y_mm, last_z_mm, ekin_keV, last_pv_name)

// Per-event CPU hit positions (world frame, mm). Filled iff DUMP_HITS env != 0.
static bool               gDumpHits = false;
static std::vector<float> gCpuHitX, gCpuHitY, gCpuHitZ, gCpuHitWL;
static long               gStepsAll = 0;             // diagnostic
static long               gStepsOptical = 0;         // diagnostic
static long               gStepsOpticalBoundary = 0; // diagnostic
static bool               gKillOptical = true;       // see DRICH_StackingAction
static bool               gFixSensorSurfaces = true; // add REFLECTIVITY=0 to EFFICIENCY surfaces

// U4SensorIdentifier that recognises any LV in gSensorLVs as a sensor.
// Used as a custom override of U4SensorIdentifierDefault so the dRICH GDML
// import path (no SDs from DD4hep, but skin surfaces with EFFICIENCY) maps
// onto an identifier the U4Tree machinery can consume.
struct DRICH_SensorIdentifier : public U4SensorIdentifier
{
    int level = 0;
    void setLevel(int _level) override
    {
        level = _level;
    }
    int getGlobalIdentity(const G4VPhysicalVolume* pv, const G4VPhysicalVolume*) override
    {
        if (!pv)
            return -1;
        const G4LogicalVolume* lv = pv->GetLogicalVolume();
        if (!lv)
            return -1;
        if (gSensorLVs.count(lv) == 0)
            return -1;
        const G4PVPlacement* pvp = dynamic_cast<const G4PVPlacement*>(pv);
        int                  copyno = pvp ? pvp->GetCopyNo() : 0;
        return copyno >= 0 ? copyno : 0;
    }
    int getInstanceIdentity(const G4VPhysicalVolume* pv) const override
    {
        if (!pv)
            return -1;
        const G4LogicalVolume* lv = pv->GetLogicalVolume();
        if (!lv)
            return -1;
        if (gSensorLVs.count(lv) == 0)
            return -1;
        const G4PVPlacement* pvp = dynamic_cast<const G4PVPlacement*>(pv);
        int                  copyno = pvp ? pvp->GetCopyNo() : 0;
        return copyno >= 0 ? copyno : 0;
    }
};

// GDML import gives us optical skin surfaces (e.g. SensorSurface_DRICH carrying
// the per-pixel EFFICIENCY) but no SensitiveDetector attachments — so the
// default Opticks sensor identifier (U4SensorIdentifierDefault::FindSD_r)
// returns -1 for every volume and zero hits are recorded.
//
// To match the DD4hep flow we walk every G4LogicalVolume, look at its skin
// surface (if any) and check whether the associated optical surface has an
// EFFICIENCY property. Any LV that does is genuine sensor — attach a no-op SD.
static void AttachSDsToOpticalEfficiencySurfaces()
{
    static DRICH_DummySD* sd = new DRICH_DummySD("DRICH_DummySD");
    G4SDManager::GetSDMpointer()->AddNewDetector(sd);

    G4LogicalVolumeStore* store = G4LogicalVolumeStore::GetInstance();
    int                   attached = 0;
    for (size_t i = 0; i < store->size(); ++i)
    {
        G4LogicalVolume* lv = (*store)[i];
        if (!lv)
            continue;
        if (lv->GetSensitiveDetector())
            continue;

        G4LogicalSkinSurface* skin = G4LogicalSkinSurface::GetSurface(lv);
        if (!skin)
            continue;
        G4OpticalSurface* os = dynamic_cast<G4OpticalSurface*>(skin->GetSurfaceProperty());
        if (!os)
            continue;
        G4MaterialPropertiesTable* mpt = os->GetMaterialPropertiesTable();
        if (!mpt)
            continue;
        G4MaterialPropertyVector* eff = mpt->GetProperty("EFFICIENCY");
        if (!eff || eff->GetVectorLength() == 0)
            continue;
        bool any_nonzero = false;
        for (G4int j = 0; j < eff->GetVectorLength(); ++j)
        {
            if ((*eff)[j] > 0.)
            {
                any_nonzero = true;
                break;
            }
        }
        if (!any_nonzero)
            continue;
        lv->SetSensitiveDetector(sd);
        gSensorLVs.insert(lv);
        ++attached;

        // CPU/GPU asymmetry fix on the sensor optical surface:
        //
        //   (a) Force REFLECTIVITY=0 so DoAbsorption runs (the GDML only has
        //       EFFICIENCY; default REFL=1 would make G4 reflect every photon
        //       and never call DoDetection -> CPU hits would be zero).
        //
        //   (b) Switch the surface model/type from glisur/dielectric_dielectric
        //       to unified/dielectric_metal. dielectric_dielectric does Fresnel
        //       refraction BEFORE the REFLECTIVITY/EFFICIENCY chain, so grazing
        //       photons Fresnel-reflect off the sensor and never get detected
        //       on CPU. Opticks GPU treats any EFFICIENCY>0 surface as a flat
        //       (detect, absorb, 0, 0) state — i.e. dielectric_metal-equivalent.
        //       Without this fix CPU/GPU ratio sits at ~0.75 here vs the
        //       DD4hep baseline ~1.0; with the fix CPU drops to match GPU.
        if (gFixSensorSurfaces)
        {
            if (!mpt->GetProperty("REFLECTIVITY"))
            {
                G4int                 n = eff->GetVectorLength();
                std::vector<G4double> energies(n), zeros(n, 0.);
                for (G4int j = 0; j < n; ++j) energies[j] = eff->Energy(j);
                mpt->AddProperty("REFLECTIVITY", energies.data(), zeros.data(), n);
            }
            os->SetModel(unified);
            os->SetType(dielectric_metal);
            os->SetFinish(polished);
        }
    }
    std::cout << "[main] attached dummy SD to " << attached << " LVs (sensors)\n";

    // Verify surface-type fix actually took effect — print the type of any
    // one sensor optical surface.
    {
        const G4LogicalSkinSurfaceTable* tab = G4LogicalSkinSurface::GetSurfaceTable();
        if (tab)
        {
            for (auto it = tab->begin(); it != tab->end(); ++it)
            {
                if (auto* s = it->second)
                {
                    if (auto* os = dynamic_cast<G4OpticalSurface*>(s->GetSurfaceProperty()))
                    {
                        auto* mpt2 = os->GetMaterialPropertiesTable();
                        if (mpt2 && mpt2->GetProperty("EFFICIENCY"))
                        {
                            std::cout << "[main] sample sensor surface: name=" << s->GetName()
                                      << " type=" << int(os->GetType())
                                      << " model=" << int(os->GetModel())
                                      << " finish=" << int(os->GetFinish())
                                      << " hasREFL=" << (mpt2->GetProperty("REFLECTIVITY") ? "yes" : "no")
                                      << "\n";
                            break;
                        }
                    }
                }
            }
        }
    }
}

class DRICH_GDML_Detector : public G4VUserDetectorConstruction
{
  public:
    DRICH_GDML_Detector(const char* gdml) :
        fGdml(gdml)
    {
    }
    G4VPhysicalVolume* Construct() override
    {
        G4GDMLParser parser;
        parser.Read(fGdml, /*validate=*/false);
        // DD4hep -> Geant4 unit reconciliation: GeV (DD4hep) -> MeV (Geant4).
        RescaleDD4hepPropertyEnergies(1000.0);
        return parser.GetWorldVolume();
    }
    void ConstructSDandField() override
    {
        AttachSDsToOpticalEfficiencySurfaces();
    }

  private:
    const char* fGdml;
};

// --- primary generator --------------------------------------------------
class DRICH_PrimaryGen : public G4VUserPrimaryGeneratorAction
{
  public:
    DRICH_PrimaryGen(double E_GeV, double dx, double dy, double dz, int mult) :
        fMult(mult)
    {
        fGun = new G4ParticleGun(1);
        // PARTICLE env: e-, mu-, pi-, kaon-, proton (default mu-)
        const char* pname = std::getenv("PARTICLE");
        if (!pname || !*pname)
            pname = "mu-";
        auto* part = G4ParticleTable::GetParticleTable()->FindParticle(pname);
        if (!part)
        {
            std::cerr << "[main] PARTICLE='" << pname << "' not found; falling back to mu-\n";
            part = G4ParticleTable::GetParticleTable()->FindParticle("mu-");
        }
        fGun->SetParticleDefinition(part);
        // MOMENTUM_GEV env: if set, gun uses momentum (GeV/c) instead of kinetic energy.
        // Useful because Cherenkov/PID grids are typically specified in momentum.
        if (const char* pm = std::getenv("MOMENTUM_GEV"))
        {
            const double p_GeV = std::atof(pm);
            const double m_MeV = part->GetPDGMass(); // MeV in CLHEP units
            const double p_MeV = p_GeV * 1000.0;     // GeV/c → MeV/c
            const double Etot_MeV = std::sqrt(p_MeV * p_MeV + m_MeV * m_MeV);
            const double Ekin_MeV = Etot_MeV - m_MeV;
            fGun->SetParticleEnergy(Ekin_MeV * CLHEP::MeV);
            std::cout << "[main] " << pname << " p=" << p_GeV << " GeV/c "
                      << "(KE=" << Ekin_MeV / 1000.0 << " GeV)\n";
        }
        else
        {
            fGun->SetParticleEnergy(E_GeV * GeV);
            std::cout << "[main] " << pname << " KE=" << E_GeV << " GeV\n";
        }
        fGun->SetParticlePosition(G4ThreeVector(0, 0, 0));
        fGun->SetParticleMomentumDirection(G4ThreeVector(dx, dy, dz));
    }
    void GeneratePrimaries(G4Event* evt) override
    {
        static int trace_evt = std::getenv("STEP_TRACE_EVT")
                                   ? std::atoi(std::getenv("STEP_TRACE_EVT"))
                                   : -1;
        if (trace_evt >= 0 && evt->GetEventID() == trace_evt)
        {
            std::cerr << "[diag] GeneratePrimaries START evt=" << evt->GetEventID()
                      << " mult=" << fMult << std::endl;
        }
        for (int i = 0; i < fMult; ++i) fGun->GeneratePrimaryVertex(evt);
        if (trace_evt >= 0 && evt->GetEventID() == trace_evt)
        {
            std::cerr << "[diag] GeneratePrimaries DONE evt=" << evt->GetEventID()
                      << std::endl;
        }
    }

  private:
    G4ParticleGun* fGun;
    int            fMult;
};

// --- stacking action to kill optical photons on CPU --------------------
// Default: Opticks already captured Cerenkov gensteps at PostStepDoIt time,
// so the secondary G4Tracks are pure waste (~150k photons per event, ~60x
// slower than not tracking them). Toggle with env KILL_OPTICAL=0 to keep
// them alive — needed for the GPU-vs-CPU hit-count comparison.
class DRICH_StackingAction : public G4UserStackingAction
{
  public:
    G4ClassificationOfNewTrack ClassifyNewTrack(const G4Track* tr) override
    {
        if (gKillOptical && tr->GetDefinition() == G4OpticalPhoton::Definition())
            return fKill;
        return fUrgent;
    }
};

// --- stepping action: count G4 CPU hits at sensor surfaces -------------
//
// G4OpBoundaryProcess sets status=Detection and proposes fStopAndKill when a
// photon hits a surface with REFLECTIVITY=0 + EFFICIENCY>0 and the
// efficiency draw succeeds. We catch that on the step where the kill
// propagated, and verify it happened at a sensor boundary by checking the
// pre/post LV of the step against gSensorLVs.
class DRICH_SteppingAction : public G4UserSteppingAction
{
  public:
    G4OpBoundaryProcess* fBP = nullptr;
    void cacheBoundary()
    {
        if (fBP)
            return;
        if (auto* g = G4ParticleTable::GetParticleTable()->FindParticle("opticalphoton"))
        {
            if (auto* pm = g->GetProcessManager())
            {
                G4ProcessVector* pvec = pm->GetPostStepProcessVector();
                for (G4int i = 0; pvec && i < pvec->size(); ++i)
                    if (auto* c = dynamic_cast<G4OpBoundaryProcess*>((*pvec)[i]))
                    {
                        fBP = c;
                        break;
                    }
            }
        }
    }
    void UserSteppingAction(const G4Step* step) override
    {
        ++gStepsAll;
        // SA call counter: independent of total steps, fires every 100k calls
        // to confirm SteppingAction is firing for the trace event.
        static int trace_evt_sa = std::getenv("STEP_TRACE_EVT")
                                      ? std::atoi(std::getenv("STEP_TRACE_EVT"))
                                      : -1;
        if (trace_evt_sa >= 0 && gCurrentEventID == trace_evt_sa && (gStepsAll % 100000) == 0)
        {
            std::cerr << "[sa] e=" << gCurrentEventID << " gStepsAll=" << gStepsAll
                      << " gStepsOptical=" << gStepsOptical
                      << " gStepsOpBnd=" << gStepsOpticalBoundary << std::endl;
        }
        // STEP_TRACE_ALL_AFTER=N: print every step BEFORE work begins, once
        // gStepsAll has passed N. Reveals what individual step is taking
        // forever when the loop appears stuck.
        static long trace_all_after = std::getenv("STEP_TRACE_ALL_AFTER")
                                          ? std::atol(std::getenv("STEP_TRACE_ALL_AFTER"))
                                          : -1;
        if (trace_all_after >= 0 && trace_evt_sa >= 0 && gCurrentEventID == trace_evt_sa && gStepsAll >= trace_all_after)
        {
            G4Track*                 tk0 = const_cast<G4Track*>(step->GetTrack());
            const G4ThreeVector&     p0 = step->GetPreStepPoint()->GetPosition();
            const G4VPhysicalVolume* pv0 = step->GetPreStepPoint()->GetPhysicalVolume();
            std::cerr << "[every] e=" << gCurrentEventID
                      << " step=" << gStepsAll
                      << " t=" << tk0->GetTrackID()
                      << " pid=" << tk0->GetParentID()
                      << " " << tk0->GetDefinition()->GetParticleName()
                      << " sn=" << tk0->GetCurrentStepNumber()
                      << " pre_pv=" << (pv0 ? pv0->GetName().c_str() : "(null)")
                      << " pre_pos=(" << p0.x() / CLHEP::mm << "," << p0.y() / CLHEP::mm << "," << p0.z() / CLHEP::mm << ")"
                      << " E_keV=" << tk0->GetKineticEnergy() / CLHEP::keV
                      << std::endl;
        }
        G4Track* track = const_cast<G4Track*>(step->GetTrack());
        // Watchdog: kill any track that exceeds MAX_STEP_WATCHDOG steps.
        // Default raised to 100000 (from 5000) per user request — some
        // legitimate optical photons traverse complex geometry with many
        // sub-mm boundary crossings, so 5000 was over-aggressive. 100k
        // is high enough to allow normal tracks to finish but catches
        // true infinite loops (oscillating between two coincident faces
        // in a degenerate CSG geometry, runaway secondaries, etc.).
        static int max_step_watchdog = std::getenv("MAX_STEP_WATCHDOG")
                                           ? std::atoi(std::getenv("MAX_STEP_WATCHDOG"))
                                           : 100000;
        // STUCK_DIAG=N — print one line for any track that hits step N
        // (must be < max_step_watchdog). Helps identify which specific
        // tracks dominate per-event wall time.
        static int stuck_diag = std::getenv("STUCK_DIAG")
                                    ? std::atoi(std::getenv("STUCK_DIAG"))
                                    : 0;
        if (stuck_diag > 0 && track->GetCurrentStepNumber() == stuck_diag)
        {
            const G4ThreeVector&     p = step->GetPostStepPoint()->GetPosition();
            const G4VPhysicalVolume* pv = step->GetPostStepPoint()->GetPhysicalVolume();
            const char*              pvname = pv ? pv->GetName().c_str() : "(null)";
            std::cerr << "[stuck] trackID=" << track->GetTrackID()
                      << " parentID=" << track->GetParentID()
                      << " particle=" << track->GetDefinition()->GetParticleName()
                      << " E_keV=" << track->GetKineticEnergy() / CLHEP::keV
                      << " pos=(" << p.x() << "," << p.y() << "," << p.z() << ")"
                      << " pv=" << pvname
                      << " step#=" << track->GetCurrentStepNumber()
                      << "\n";
        }
        if (track->GetCurrentStepNumber() > max_step_watchdog)
        {
            // Print the kill so we can see WHICH track triggered it.
            const G4ThreeVector&     p = step->GetPostStepPoint()->GetPosition();
            const G4VPhysicalVolume* pv = step->GetPostStepPoint()->GetPhysicalVolume();
            std::cerr << "[watchdog-kill] trackID=" << track->GetTrackID()
                      << " parentID=" << track->GetParentID()
                      << " particle=" << track->GetDefinition()->GetParticleName()
                      << " E_keV=" << track->GetKineticEnergy() / CLHEP::keV
                      << " pos=(" << p.x() << "," << p.y() << "," << p.z() << ")"
                      << " pv=" << (pv ? pv->GetName().c_str() : "(null)")
                      << " step#=" << track->GetCurrentStepNumber()
                      << "\n";
            track->SetTrackStatus(fStopAndKill);
            return;
        }
        // STEP_TRACE_EVT=N enables per-step diagnostics for event N.
        // STEP_TRACE_MIN=K (default 100) skips printing until track step > K.
        // STEP_TRACE_EVERY=M (default 1) prints every Mth step thereafter.
        // Per-track max step count + last position is always tracked for
        // end-of-event summary.
        static int trace_evt = std::getenv("STEP_TRACE_EVT")
                                   ? std::atoi(std::getenv("STEP_TRACE_EVT"))
                                   : -1;
        static int trace_min = std::getenv("STEP_TRACE_MIN")
                                   ? std::atoi(std::getenv("STEP_TRACE_MIN"))
                                   : 100;
        static int trace_every = std::getenv("STEP_TRACE_EVERY")
                                     ? std::atoi(std::getenv("STEP_TRACE_EVERY"))
                                     : 1;
        if (trace_evt >= 0 && gCurrentEventID == trace_evt)
        {
            int                      sn = track->GetCurrentStepNumber();
            int                      tid = track->GetTrackID();
            const G4ThreeVector&     pp = step->GetPostStepPoint()->GetPosition();
            const G4VPhysicalVolume* pv_pre = step->GetPreStepPoint()->GetPhysicalVolume();
            const G4VPhysicalVolume* pv_post = step->GetPostStepPoint()->GetPhysicalVolume();
            const char*              pn_pre = pv_pre ? pv_pre->GetName().c_str() : "(null)";
            const char*              pn_post = pv_post ? pv_post->GetName().c_str() : "(null)";
            // update per-track summary
            auto& rec = gMaxStepPerTrack[tid];
            std::get<0>(rec) = track->GetDefinition()->GetParticleName();
            std::get<1>(rec) = sn;
            std::get<2>(rec) = track->GetParentID();
            std::get<3>(rec) = pp.x() / CLHEP::mm;
            std::get<4>(rec) = pp.y() / CLHEP::mm;
            std::get<5>(rec) = pp.z() / CLHEP::mm;
            std::get<6>(rec) = track->GetKineticEnergy() / CLHEP::keV;
            std::get<7>(rec) = pn_post;
            if (sn > trace_min && (sn % trace_every) == 0)
            {
                const G4VProcess* p_def = step->GetPostStepPoint()->GetProcessDefinedStep();
                const char*       procname = p_def ? p_def->GetProcessName().c_str() : "(init)";
                std::cerr << "[stp] e=" << gCurrentEventID
                          << " t=" << tid
                          << " pid=" << track->GetParentID()
                          << " " << track->GetDefinition()->GetParticleName()
                          << " sn=" << sn
                          << " proc=" << procname
                          << " pv=" << pn_pre << "->" << pn_post
                          << " pos=(" << pp.x() / CLHEP::mm << "," << pp.y() / CLHEP::mm << "," << pp.z() / CLHEP::mm << ")"
                          << " E_keV=" << track->GetKineticEnergy() / CLHEP::keV
                          << "\n";
            }
            // STEP_TRACE_TICK=N: every N total steps, dump in-flight summary
            // (top 5 longest tracks, particle counts, total tracks).
            static long trace_tick = std::getenv("STEP_TRACE_TICK")
                                         ? std::atol(std::getenv("STEP_TRACE_TICK"))
                                         : 1000000;
            if (trace_tick > 0 && (gStepsAll % trace_tick) == 0)
            {
                std::map<std::string, long> pc;
                int                         top_t = -1, top_s = -1;
                std::string                 top_n, top_pv;
                int                         top_pid = -1;
                double                      top_x = 0, top_y = 0, top_z = 0, top_E = 0;
                for (auto& kv : gMaxStepPerTrack)
                {
                    const auto& r = kv.second;
                    pc[std::get<0>(r)]++;
                    if (std::get<1>(r) > top_s)
                    {
                        top_s = std::get<1>(r);
                        top_t = kv.first;
                        top_n = std::get<0>(r);
                        top_pid = std::get<2>(r);
                        top_x = std::get<3>(r);
                        top_y = std::get<4>(r);
                        top_z = std::get<5>(r);
                        top_E = std::get<6>(r);
                        top_pv = std::get<7>(r);
                    }
                }
                std::cerr << "[tick] e=" << gCurrentEventID
                          << " total_steps=" << gStepsAll
                          << " tracks_seen=" << gMaxStepPerTrack.size()
                          << " parts={";
                for (auto& kv : pc) std::cerr << kv.first << ":" << kv.second << " ";
                std::cerr << "} top: t=" << top_t << " pid=" << top_pid << " " << top_n
                          << " sn=" << top_s << " pv=" << top_pv
                          << " pos=(" << top_x << "," << top_y << "," << top_z << ")"
                          << " E_keV=" << top_E << "\n";
            }
        }
        if (gKillOptical)
            return;
        if (track->GetDefinition() != G4OpticalPhoton::Definition())
            return;
        ++gStepsOptical;

        if (!fBP)
            cacheBoundary();
        G4OpBoundaryProcess* bp = fBP;

        if (!bp)
        {
            static bool warned = false;
            if (!warned)
            {
                std::cerr << "[diag] NO G4OpBoundaryProcess on opticalphoton process list!\n";
                warned = true;
            }
            return;
        }

        auto status = bp->GetStatus();
        // Status starts as Undefined each step; only set to something when
        // the boundary process actually ran this step.
        if (status == Undefined || status == NotAtBoundary)
            return;
        ++gStepsOpticalBoundary;

        if (status == Detection)
            ++gCpuOpDetectThisEvent;
        if (status == Absorption)
            ++gCpuOpAbsorbThisEvent;
        if (status == FresnelReflection || status == TotalInternalReflection || status == LambertianReflection || status == LobeReflection || status == SpikeReflection || status == BackScattering)
            ++gCpuOpReflThisEvent;

        if (track->GetTrackStatus() != fStopAndKill)
            return;
        ++gCpuOpKillsThisEvent;

        // sensor LV check (pre OR post step)
        auto isSensor = [](const G4StepPoint* sp) {
            if (!sp)
                return false;
            const G4VPhysicalVolume* pv = sp->GetPhysicalVolume();
            if (!pv)
                return false;
            return gSensorLVs.count(pv->GetLogicalVolume()) > 0;
        };
        bool sensorBoundary = isSensor(step->GetPreStepPoint()) || isSensor(step->GetPostStepPoint());
        if (sensorBoundary && status == Detection)
        {
            ++gCpuHitsThisEvent;
            if (gDumpHits)
            {
                const G4ThreeVector& p = step->GetPostStepPoint()->GetPosition();
                gCpuHitX.push_back(static_cast<float>(p.x() / CLHEP::mm));
                gCpuHitY.push_back(static_cast<float>(p.y() / CLHEP::mm));
                gCpuHitZ.push_back(static_cast<float>(p.z() / CLHEP::mm));
                // wavelength in nm = h*c / E
                double E = track->GetKineticEnergy();
                double wl_nm = (E > 0) ? (CLHEP::h_Planck * CLHEP::c_light / E) / CLHEP::nm : 0.0;
                gCpuHitWL.push_back(static_cast<float>(wl_nm));
            }
        }
        if (gStepsOpticalBoundary == 1)
            std::cerr << "[diag] first OpBoundary fire: status=" << int(status)
                      << " sensorBoundary=" << sensorBoundary << "\n";
    }
};

// --- per-event Opticks driver -----------------------------------------
class DRICH_EventAction : public G4UserEventAction
{
  public:
    void BeginOfEventAction(const G4Event* evt) override
    {
        static int trace_evt = std::getenv("STEP_TRACE_EVT")
                                   ? std::atoi(std::getenv("STEP_TRACE_EVT"))
                                   : -1;
        if (trace_evt >= 0 && evt->GetEventID() == trace_evt)
        {
            std::cerr << "[diag] BeginOfEventAction evt=" << evt->GetEventID()
                      << std::endl;
        }
        SEvt::CreateOrReuse_EGPU();
        SEvt* sev = SEvt::Get_EGPU();
        if (sev)
            sev->beginOfEvent(evt->GetEventID());
        if (trace_evt >= 0 && evt->GetEventID() == trace_evt)
            std::cerr << "[diag] SEvt::beginOfEvent done evt=" << evt->GetEventID() << std::endl;
        gCpuHitsThisEvent = 0;
        gCpuOpKillsThisEvent = 0;
        gCpuOpAbsorbThisEvent = 0;
        gCpuOpDetectThisEvent = 0;
        gCpuOpReflThisEvent = 0;
        gStepsAll = 0;
        gStepsOptical = 0;
        gStepsOpticalBoundary = 0;
        gCurrentEventID = evt->GetEventID();
        gStepHistThisEvent.clear();
        gMaxStepPerTrack.clear();
        gCpuHitX.clear();
        gCpuHitY.clear();
        gCpuHitZ.clear();
        gCpuHitWL.clear();
    }
    void EndOfEventAction(const G4Event* evt) override
    {
        G4CXOpticks* gx = G4CXOpticks::Get();
        if (!gx)
            return;
        SEvt* sev = SEvt::Get_EGPU();
        if (!sev)
            return;
        int64_t ngs = sev->getNumGenstepFromGenstep();
        int64_t nph = sev->getNumPhotonFromGenstep();
        std::cout << "[evt " << evt->GetEventID() << "] gensteps=" << ngs
                  << " photons=" << nph << std::flush;
        // End-of-event per-track step summary, printed only when
        // STEP_TRACE_EVT matches the current event. Sort by max step count
        // descending and print top 20 (or all if STEP_TRACE_DUMPALL=1).
        static int trace_evt_ev = std::getenv("STEP_TRACE_EVT")
                                      ? std::atoi(std::getenv("STEP_TRACE_EVT"))
                                      : -1;
        if (trace_evt_ev >= 0 && evt->GetEventID() == trace_evt_ev && !gMaxStepPerTrack.empty())
        {
            std::vector<std::pair<int, int>> by_steps;
            by_steps.reserve(gMaxStepPerTrack.size());
            for (auto& kv : gMaxStepPerTrack)
                by_steps.emplace_back(kv.first, std::get<1>(kv.second));
            std::sort(by_steps.begin(), by_steps.end(),
                      [](auto& a, auto& b) { return a.second > b.second; });
            const bool dump_all = std::getenv("STEP_TRACE_DUMPALL") && std::atoi(std::getenv("STEP_TRACE_DUMPALL")) != 0;
            size_t     N = dump_all ? by_steps.size() : std::min<size_t>(20, by_steps.size());
            std::cerr << "[evt " << evt->GetEventID() << "] track-step summary"
                      << " total_tracks=" << gMaxStepPerTrack.size()
                      << " top" << N << ":\n";
            for (size_t i = 0; i < N; ++i)
            {
                int   tid = by_steps[i].first;
                auto& rec = gMaxStepPerTrack[tid];
                std::cerr << "  t=" << tid
                          << " pid=" << std::get<2>(rec)
                          << " " << std::get<0>(rec)
                          << " steps=" << std::get<1>(rec)
                          << " last_pv=" << std::get<7>(rec)
                          << " last_pos=(" << std::get<3>(rec) << ","
                          << std::get<4>(rec) << "," << std::get<5>(rec) << ")"
                          << " E_keV=" << std::get<6>(rec)
                          << "\n";
            }
            // also histogram bucket
            std::map<int, int> hist;
            for (auto& kv : gMaxStepPerTrack)
            {
                int s = std::get<1>(kv.second);
                int bin = (s <= 1) ? 0 : int(std::log2(double(s)));
                ++hist[bin];
            }
            std::cerr << "[evt " << evt->GetEventID() << "] step-count log2-hist (bin=2^bin..2^(bin+1)-1):\n";
            for (auto& kv : hist)
                std::cerr << "  bin=" << kv.first << " (>=2^" << kv.first << ") ntracks=" << kv.second << "\n";
        }
        if (ngs > 0)
        {
            gx->simulate(evt->GetEventID(), false);
            unsigned nhit = sev->getNumHit();
            std::cout << " gpu_hits=" << nhit
                      << " cpu_hits=" << gCpuHitsThisEvent
                      << " (cpu_OpDet=" << gCpuOpDetectThisEvent
                      << " cpu_OpAbs=" << gCpuOpAbsorbThisEvent
                      << " cpu_OpRefl=" << gCpuOpReflThisEvent
                      << " cpu_OpKills=" << gCpuOpKillsThisEvent
                      << " steps_all=" << gStepsAll
                      << " steps_optical=" << gStepsOptical
                      << " steps_op_bnd=" << gStepsOpticalBoundary
                      << ")" << std::endl;
            if (gDumpHits)
            {
                const int     evtid = evt->GetEventID();
                std::ofstream gh("gpu_hits.tsv", std::ios::app);
                for (unsigned i = 0; i < nhit; ++i)
                {
                    sphoton p;
                    sev->getHit(p, i);
                    gh << p.pos.x << "\t" << p.pos.y << "\t" << p.pos.z << "\t" << p.wavelength
                       << "\t" << p.flag() << "\t" << p.flagmask
                       << "\t" << p.boundary() << "\t" << p.identity << "\t" << p.index
                       << "\t" << evtid << "\n";
                }
                gh.close();
                std::ofstream ch("cpu_hits.tsv", std::ios::app);
                for (size_t i = 0; i < gCpuHitX.size(); ++i)
                    ch << gCpuHitX[i] << "\t" << gCpuHitY[i] << "\t" << gCpuHitZ[i]
                       << "\t" << (i < gCpuHitWL.size() ? gCpuHitWL[i] : 0.0f)
                       << "\t" << evtid << "\n";
                ch.close();
                std::cout << "[dump_hits] appended gpu_hits.tsv (+" << nhit
                          << ") cpu_hits.tsv (+" << gCpuHitX.size() << ")\n";

                // Also dump all photons (for forensics on mirror-band entries).
                if (std::getenv("DUMP_PHOTONS"))
                {
                    unsigned      nph = sev->getNumPhoton();
                    std::ofstream ph("all_photons.tsv");
                    ph << "# x_mm\ty_mm\tz_mm\twavelength_nm\tflag\tflagmask\tboundary\tidentity\thitcount\n";
                    for (unsigned i = 0; i < nph; ++i)
                    {
                        sphoton p;
                        sev->getPhoton(p, i);
                        ph << p.pos.x << "\t" << p.pos.y << "\t" << p.pos.z << "\t" << p.wavelength
                           << "\t" << p.flag() << "\t" << p.flagmask
                           << "\t" << p.boundary() << "\t" << p.identity
                           << "\t" << p.hitcount() << "\n";
                    }
                    ph.close();
                    std::cout << "[dump_hits] also wrote all_photons.tsv (" << nph << " rows)\n";
                }
            }
            sev->endOfEvent(evt->GetEventID());
            gx->reset(evt->GetEventID());
        }
        else
        {
            std::cout << " (no gensteps) cpu_hits=" << gCpuHitsThisEvent << std::endl;
            sev->endOfEvent(evt->GetEventID());
        }
    }
};

class DRICH_RunAction : public G4UserRunAction
{
  public:
    DRICH_RunAction(const char* gdml_path) :
        fGdmlPath(gdml_path)
    {
    }
    void BeginOfRunAction(const G4Run*) override
    {
        // SEvt MUST exist before SetGeometry for GPU detection
        SEvt::CreateOrReuse(SEvt::EGPU);
        // Install custom sensor identifier matching skinsurface EFFICIENCY LVs.
        G4CXOpticks::SetSensorIdentifier(new DRICH_SensorIdentifier());
        G4VPhysicalVolume* world = G4TransportationManager::GetTransportationManager()
                                       ->GetNavigatorForTracking()
                                       ->GetWorldVolume();
        G4CXOpticks::SetGeometry(world);

        // DUMP_BND=1: print the GPU boundary table — name + sampled d4 surface
        // state at boundary indices 0..min(N,32). Used to diagnose phantom
        // SURFACE_DETECT firing at boundaries that should not be sensors.
        if (std::getenv("DUMP_BND"))
        {
            SSim*     sim = SSim::Get();
            const NP* bnd = sim ? sim->get_bnd() : nullptr;
            const NP* op = sim ? sim->get(snam::OPTICAL) : nullptr;
            if (bnd && op)
            {
                int ni = bnd->shape[0];
                int nj = bnd->shape[1];
                int nk = bnd->shape[2];
                int nl = bnd->shape[3];
                int nm = bnd->shape[4];
                std::cout << "[bnd] shape (" << ni << "," << nj << "," << nk
                          << "," << nl << "," << nm << ")\n";
                std::cout << "[op]  shape (" << op->shape[0] << ","
                          << op->shape[1] << "," << op->shape[2] << ")\n";
                const double*                   bv = bnd->cvalues<double>();
                const int*                      ov = op->cvalues<int>();
                const std::vector<std::string>& names = bnd->names;
                int                             lhalf = std::getenv("DUMP_BND_LBIN")
                                                            ? std::atoi(std::getenv("DUMP_BND_LBIN"))
                                                            : nl / 2;
                int                             boundary_to_dump = std::getenv("DUMP_BND_IDX")
                                                                       ? std::atoi(std::getenv("DUMP_BND_IDX"))
                                                                       : -1;
                for (int i = 0; i < ni; ++i)
                {
                    if (boundary_to_dump >= 0 && i != boundary_to_dump)
                        continue;
                    std::cout << "[bnd] idx=" << std::setw(4) << i;
                    for (int j = 0; j <= 3; ++j)
                    {
                        for (int kk = 0; kk <= 1; ++kk)
                        {
                            int base = ((((i * nj + j) * nk) + kk) * nl + lhalf) * nm;
                            std::cout << " j=" << j << "k=" << kk << "(" << bv[base + 0] << ","
                                      << bv[base + 1] << "," << bv[base + 2] << "," << bv[base + 3] << ")";
                        }
                    }
                    std::cout << " name=" << (i < int(names.size()) ? names[i] : "-")
                              << "\n";
                }
            }
            else
            {
                std::cout << "[bnd] no SSim::get_bnd or get(optical)\n";
            }
        }

        // Silence G4 optical-process verbose noise (Scattering Photon!, etc.).
        if (auto* gamma = G4ParticleTable::GetParticleTable()->FindParticle("opticalphoton"))
        {
            if (auto* pm = gamma->GetProcessManager())
            {
                G4ProcessVector* pv = pm->GetProcessList();
                for (G4int i = 0; pv && i < pv->size(); ++i)
                    if ((*pv)[i])
                        (*pv)[i]->SetVerboseLevel(0);
            }
        }
        // Mu- gets verbose Cerenkov/Boundary too
        if (auto* mu = G4ParticleTable::GetParticleTable()->FindParticle("mu-"))
        {
            if (auto* pm = mu->GetProcessManager())
            {
                G4ProcessVector* pv = pm->GetProcessList();
                for (G4int i = 0; pv && i < pv->size(); ++i)
                    if ((*pv)[i])
                        (*pv)[i]->SetVerboseLevel(0);
            }
        }
    }
    void EndOfRunAction(const G4Run*) override
    {
        G4CXOpticks::Finalize();
    }

  private:
    const char* fGdmlPath;
};

// --- main --------------------------------------------------------------
int main(int argc, char** argv)
{
    const char* gdml = std::getenv("GDML_FILE");
    if (!gdml)
        gdml = "drich_ag03.gdml";

    int    seed = std::getenv("SEED") ? std::atoi(std::getenv("SEED")) : 12345;
    double eta = std::getenv("ETA") ? std::atof(std::getenv("ETA")) : 2.0;
    double phi_deg = std::getenv("PHI_DEG") ? std::atof(std::getenv("PHI_DEG")) : 180.0;
    double E = std::getenv("ENERGY_GEV") ? std::atof(std::getenv("ENERGY_GEV")) : 10.0;
    int    mult = std::getenv("MULT") ? std::atoi(std::getenv("MULT")) : 100;
    int    nev = std::getenv("NEVENTS") ? std::atoi(std::getenv("NEVENTS")) : 1;

    // KILL_OPTICAL=1 (default) → kill optical photons on stack (GPU-only, fast).
    // KILL_OPTICAL=0           → keep them, track on CPU, count G4-side hits.
    if (const char* s = std::getenv("KILL_OPTICAL"))
        gKillOptical = std::atoi(s) != 0;
    if (const char* s = std::getenv("DUMP_HITS"))
        gDumpHits = std::atoi(s) != 0;

    // U4Physics builds Shim G4OpRayleigh / G4OpAbsorption when compiled with
    // DEBUG_TAG; the base classes print "Scattering Photon!" per scatter if
    // verboseLevel > 0. That dominates CPU-mode runtime and floods stdout.
    // Silence via env: VERBOSE_OP=1 to re-enable.
    bool verbose_op = std::getenv("VERBOSE_OP") ? std::atoi(std::getenv("VERBOSE_OP")) : 0;

    double theta = 2.0 * std::atan(std::exp(-eta));
    double phi = phi_deg * M_PI / 180.0;
    double dx = std::sin(theta) * std::cos(phi);
    double dy = std::sin(theta) * std::sin(phi);
    double dz = std::cos(theta);

    std::cout << "[main] gdml=" << gdml << "\n";
    {
        const char* pname = std::getenv("PARTICLE");
        if (!pname || !*pname)
            pname = "mu-";
        const char* pmom = std::getenv("MOMENTUM_GEV");
        std::cout << "[main] gun: " << pname << "  ";
        if (pmom)
            std::cout << "p=" << pmom << " GeV/c";
        else
            std::cout << "KE=" << E << " GeV";
        std::cout << "  dir=(" << dx << "," << dy << "," << dz << ")  mult=" << mult << "  seed=" << seed << "\n";
    }

    CLHEP::HepRandom::setTheSeed(seed);

    G4RunManager* rm = new G4RunManager();
    rm->SetUserInitialization(new DRICH_GDML_Detector(gdml));

    // Physics list: U4Physics — provides Local_G4Cerenkov_modified which routes
    // gensteps into Opticks via U4::CollectGenstep_G4Cerenkov_modified.
    // FTFP_BERT + G4OpticalPhysics uses standard G4Cerenkov and never collects
    // Opticks gensteps, so no GPU photons are produced.
    rm->SetUserInitialization(new U4Physics());

    rm->SetUserAction(new DRICH_PrimaryGen(E, dx, dy, dz, mult));
    rm->SetUserAction(new DRICH_RunAction(gdml));
    rm->SetUserAction(new DRICH_EventAction());
    rm->SetUserAction(new DRICH_StackingAction());
    rm->SetUserAction(new DRICH_SteppingAction());

    rm->Initialize();

    // Raise EM production cuts so we don't accumulate delta-ray / brem
    // secondaries that each fire their own Cerenkov genstep — those add
    // event-to-event photon-count noise (seed 12349 produced 3246 photons
    // vs the typical ~1500, almost certainly a delta-ray). 1 cm range cut
    // means secondaries with range below 1 cm aren't promoted to tracks;
    // their energy deposit stays on the primary. We're studying mirror
    // optics with a single MIP-like muon — this is exactly the right
    // regime for high range cuts.
    {
        G4ProductionCuts* pc = new G4ProductionCuts();
        pc->SetProductionCut(1.0 * CLHEP::cm, "gamma");
        pc->SetProductionCut(1.0 * CLHEP::cm, "e-");
        pc->SetProductionCut(1.0 * CLHEP::cm, "e+");
        pc->SetProductionCut(1.0 * CLHEP::cm, "proton");
        if (G4Region* defRegion = G4RegionStore::GetInstance()->GetRegion("DefaultRegionForTheWorld", false))
        {
            defRegion->SetProductionCuts(pc);
        }
        G4ProductionCutsTable::GetProductionCutsTable()->UpdateCoupleTable(
            G4TransportationManager::GetTransportationManager()->GetNavigatorForTracking()->GetWorldVolume());
    }

    // Force verbose=0 on optical processes after initialization (G4OpRayleigh
    // and G4OpAbsorption default to 1 in some G4 versions).
    if (!verbose_op)
    {
        if (G4ParticleTable::GetParticleTable()->FindParticle("opticalphoton"))
        {
            auto* pmgr = G4ParticleTable::GetParticleTable()->FindParticle("opticalphoton")->GetProcessManager();
            if (pmgr)
            {
                G4ProcessVector* pv = pmgr->GetProcessList();
                for (G4int i = 0; pv && i < pv->size(); ++i)
                    (*pv)[i]->SetVerboseLevel(0);
            }
        }
    }
    // Truncate hit TSVs once at run start and write headers, so subsequent
    // per-event dumps can append. KEEP_HITS=1 disables truncation so a
    // bash-loop driver (one invocation per batch, with timeout) can
    // accumulate hits across many process restarts.
    bool keep_hits = std::getenv("KEEP_HITS") && std::atoi(std::getenv("KEEP_HITS")) != 0;
    if (gDumpHits && !keep_hits)
    {
        std::ofstream gh("gpu_hits.tsv", std::ios::trunc);
        gh << "# x_mm\ty_mm\tz_mm\twavelength_nm\tflag\tflagmask\tboundary\tidentity\tpidx\tevt\n";
        gh.close();
        std::ofstream ch("cpu_hits.tsv", std::ios::trunc);
        ch << "# x_mm\ty_mm\tz_mm\twavelength_nm\tevt\n";
        ch.close();
    }
    rm->BeamOn(nev);

    delete rm;
    return 0;
}
