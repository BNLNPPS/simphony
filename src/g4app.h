#include <filesystem>
#include <vector>

#include "G4BooleanSolid.hh"
#include "G4Event.hh"
#include "G4GDMLParser.hh"
#include "G4LogicalVolumeStore.hh"
#include "G4OpBoundaryProcess.hh"
#include "G4OpticalPhoton.hh"
#include "G4PhysicalConstants.hh"
#include "G4PrimaryParticle.hh"
#include "G4PrimaryVertex.hh"
#include "G4SDManager.hh"
#include "G4SubtractionSolid.hh"
#include "G4SystemOfUnits.hh"
#include "G4ThreeVector.hh"
#include "G4Track.hh"
#include "G4TrackStatus.hh"
#include "G4UserEventAction.hh"
#include "G4UserSteppingAction.hh"
#include "G4UserTrackingAction.hh"
#include "G4VPhysicalVolume.hh"
#include "G4VProcess.hh"
#include "G4VUserDetectorConstruction.hh"
#include "G4VUserPrimaryGeneratorAction.hh"

#include "g4cx/G4CXOpticks.hh"
#include "sysrap/NP.hh"
#include "sysrap/SEvt.hh"
#include "sysrap/STrackInfo.h"
#include "sysrap/spho.h"
#include "sysrap/sphoton.h"
#include "u4/U4Random.hh"
#include "u4/U4StepPoint.hh"
#include "u4/U4Touchable.h"
#include "u4/U4Track.h"

#include "config.h"
#include "torch.h"

bool IsSubtractionSolid(G4VSolid *solid)
{
    if (!solid)
        return false;

    // Check if the solid is directly a G4SubtractionSolid
    if (dynamic_cast<G4SubtractionSolid *>(solid))
        return true;

    // If the solid is a Boolean solid, check its constituent solids
    G4BooleanSolid *booleanSolid = dynamic_cast<G4BooleanSolid *>(solid);
    if (booleanSolid)
    {
        G4VSolid *solidA = booleanSolid->GetConstituentSolid(0);
        G4VSolid *solidB = booleanSolid->GetConstituentSolid(1);

        // Recursively check the constituent solids
        if (IsSubtractionSolid(solidA) || IsSubtractionSolid(solidB))
            return true;
    }

    // For other solid types, return false
    return false;
}

std::string str_tolower(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
    return s;
}

struct PhotonHit : public G4VHit
{
    PhotonHit() = default;

    PhotonHit(G4double energy, G4double time, G4ThreeVector position, G4ThreeVector direction,
              G4ThreeVector polarization)
        : photon()
    {
        photon.pos = {static_cast<float>(position.x()), static_cast<float>(position.y()),
                      static_cast<float>(position.z())};
        photon.time = time;
        photon.mom = {static_cast<float>(direction.x()), static_cast<float>(direction.y()),
                      static_cast<float>(direction.z())};
        photon.pol = {static_cast<float>(polarization.x()), static_cast<float>(polarization.y()),
                      static_cast<float>(polarization.z())};
        photon.wavelength = h_Planck * c_light / (energy * CLHEP::eV);
    }

    // Print method
    void Print() override
    {
        G4cout << photon << G4endl;
    }

    // Member variables
    sphoton photon;
};

using PhotonHitsCollection = G4THitsCollection<PhotonHit>;

struct PhotonSD : public G4VSensitiveDetector
{
    PhotonSD(G4String name) : G4VSensitiveDetector(name), fHCID(-1)
    {
        G4String HCname = name + "_HC";
        collectionName.insert(HCname);
        G4cout << collectionName.size() << "   PhotonSD name:  " << name << " collection Name: " << HCname << G4endl;
    }

    void Initialize(G4HCofThisEvent *hce) override
    {
        fPhotonHitsCollection = new PhotonHitsCollection(SensitiveDetectorName, collectionName[0]);
        if (fHCID < 0)
        {
            G4cout << "PhotonSD::Initialize:  " << SensitiveDetectorName << "   " << collectionName[0] << G4endl;
            fHCID = G4SDManager::GetSDMpointer()->GetCollectionID(collectionName[0]);
        }
        hce->AddHitsCollection(fHCID, fPhotonHitsCollection);
    }

    G4bool ProcessHits(G4Step *aStep, G4TouchableHistory *) override
    {
        G4Track *track = aStep->GetTrack();

        // Only process optical photons
        if (track->GetDefinition() != G4OpticalPhoton::OpticalPhotonDefinition())
            return false;

        // Create a new hit (CopyNr is set to 0 as DetectorID is omitted)
        PhotonHit *hit = new PhotonHit(
            track->GetTotalEnergy(), track->GetGlobalTime(), aStep->GetPostStepPoint()->GetPosition(),
            aStep->GetPostStepPoint()->GetMomentumDirection(), aStep->GetPostStepPoint()->GetPolarization());

        fPhotonHitsCollection->insert(hit);
        track->SetTrackStatus(fStopAndKill);

        return true;
    }

    void EndOfEvent(G4HCofThisEvent *) override
    {
        G4int num_hits = fPhotonHitsCollection->entries();
        G4cout << "PhotonSD::EndOfEvent Number of PhotonHits: " << num_hits << G4endl;

        NP *hits = NP::Make<float>(num_hits, 4, 4);
        int i = 0;

        for (PhotonHit *hit : *fPhotonHitsCollection->GetVector())
        {
            float *photon_data = reinterpret_cast<float *>(&hit->photon);
            std::copy(photon_data, photon_data + 16, hits->values<float>() + (i++) * 16);
        }

        hits->save("g_hits.npy");
        delete hits;
    }

  private:
    PhotonHitsCollection *fPhotonHitsCollection{nullptr};
    G4int fHCID;
};

struct DetectorConstruction : G4VUserDetectorConstruction
{
    DetectorConstruction(std::filesystem::path gdml_file) : gdml_file_(gdml_file)
    {
    }

    G4VPhysicalVolume *Construct() override
    {
        parser_.Read(gdml_file_.string(), false);
        G4VPhysicalVolume *world = parser_.GetWorldVolume();

        G4CXOpticks::SetGeometry(world);

        return world;
    }

    void ConstructSDandField() override
    {
        G4cout << "ConstructSDandField is called." << G4endl;
        G4SDManager *SDman = G4SDManager::GetSDMpointer();

        const G4GDMLAuxMapType *auxmap = parser_.GetAuxMap();
        for (auto const &[logVol, listType] : *auxmap)
        {
            for (auto const &auxtype : listType)
            {
                if (auxtype.type == "SensDet")
                {
                    G4cout << "Attaching sensitive detector to logical volume: " << logVol->GetName() << G4endl;
                    G4String name = logVol->GetName() + "_PhotonDetector";
                    PhotonSD *aPhotonSD = new PhotonSD(name);
                    SDman->AddNewDetector(aPhotonSD);
                    logVol->SetSensitiveDetector(aPhotonSD);
                }
            }
        }
    }

  private:
    std::filesystem::path gdml_file_;
    G4GDMLParser parser_;
};

struct PrimaryGenerator : G4VUserPrimaryGeneratorAction
{
    gphox::Config cfg;
    SEvt *sev;

    PrimaryGenerator(const gphox::Config& cfg, SEvt *sev) : cfg(cfg), sev(sev)
    {
    }

    void GeneratePrimaries(G4Event *event) override
    {
        std::vector<sphoton> sphotons = generate_photons(cfg.torch);

        size_t num_floats = sphotons.size()*4*4;
        float* data = reinterpret_cast<float*>(sphotons.data());
        NP* photons = NP::MakeFromValues<float>(data, num_floats);

        photons->reshape({ static_cast<int64_t>(sphotons.size()), 4, 4});

        for (const sphoton& p : sphotons)
        {
            G4ThreeVector position_mm(p.pos.x, p.pos.y, p.pos.z);
            G4double time_ns = p.time;
            G4ThreeVector direction(p.mom.x, p.mom.y, p.mom.z);
            // direction = direction.unit();
            G4double wavelength_nm = p.wavelength;
            G4ThreeVector polarization(p.pol.x, p.pol.y, p.pol.z);

            G4PrimaryVertex *vertex = new G4PrimaryVertex(position_mm, time_ns);
            G4double kineticEnergy = h_Planck * c_light / (wavelength_nm * nm);

            G4PrimaryParticle *particle = new G4PrimaryParticle(G4OpticalPhoton::Definition());
            particle->SetKineticEnergy(kineticEnergy);
            particle->SetMomentumDirection(direction);
            particle->SetPolarization(polarization);

            vertex->SetPrimary(particle);
            event->AddPrimaryVertex(vertex);
        }

        sev->SetInputPhoton(photons);
    }
};

struct EventAction : G4UserEventAction
{
    SEvt *sev;

    EventAction(SEvt *sev) : sev(sev)
    {
    }

    void BeginOfEventAction(const G4Event *event) override
    {
        sev->beginOfEvent(event->GetEventID());
    }

    void EndOfEventAction(const G4Event *event) override
    {
        int eventID = event->GetEventID();
        sev->addEventConfigArray();
        sev->gather();
        sev->endOfEvent(eventID);

        // GPU-based simulation
        G4CXOpticks *gx = G4CXOpticks::Get();

        gx->simulate(eventID, false);
        cudaDeviceSynchronize();

        unsigned int num_hits = SEvt::GetNumHit(SEvt::EGPU);

        std::cout << "Opticks: NumHits:  " << num_hits << std::endl;

        SEvt *sev = SEvt::Get_EGPU();
        NP *hits = NP::Make<float>(num_hits, 4, 4);

        for (unsigned idx = 0; idx < num_hits; idx++)
        {
            sphoton *photon = reinterpret_cast<sphoton *>(hits->values<float>() + idx * 16);
            sev->getHit(*photon, idx);
        }

        hits->save("o_hits.npy");
        delete hits;

        gx->reset(eventID);
    }
};

void get_label(spho &ulabel, const G4Track *track)
{
    spho *label = STrackInfo::GetRef(track);
    assert(label && label->isDefined() && "all photons are expected to be labelled");

    std::array<int, spho::N> a_label;
    label->serialize(a_label);

    ulabel.load(a_label);
}

struct SteppingAction : G4UserSteppingAction
{
    SEvt *sev;

    SteppingAction(SEvt *sev) : sev(sev)
    {
    }

    void UserSteppingAction(const G4Step *step)
    {
        if (step->GetTrack()->GetDefinition() != G4OpticalPhoton::OpticalPhotonDefinition())
            return;

        const G4VProcess *process = step->GetPreStepPoint()->GetProcessDefinedStep();

        if (process == nullptr)
            return;

        const G4Track *track = step->GetTrack();
        G4VPhysicalVolume *pv = track->GetVolume();
        const G4VTouchable *touch = track->GetTouchable();

        spho ulabel = {};
        get_label(ulabel, track);

        const G4StepPoint *pre = step->GetPreStepPoint();
        const G4StepPoint *post = step->GetPostStepPoint();

        sev->checkPhotonLineage(ulabel);

        sphoton &current_photon = sev->current_ctx.p;

        if (current_photon.flagmask_count() == 1)
        {
            U4StepPoint::Update(current_photon, pre); // populate current_photon with pos, mom, pol, time, wavelength
            sev->pointPhoton(ulabel);                 // copying current into buffers
        }

        bool tir;
        unsigned flag = U4StepPoint::Flag<G4OpBoundaryProcess>(post, true, tir);
        bool is_detect_flag = OpticksPhoton::IsSurfaceDetectFlag(flag);

        current_photon.hitcount_iindex =
            is_detect_flag ? U4Touchable::ImmediateReplicaNumber(touch) : U4Touchable::AncestorReplicaNumber(touch);

        U4StepPoint::Update(current_photon, post);

        current_photon.set_flag(flag);

        sev->pointPhoton(ulabel);
    }
};

struct TrackingAction : G4UserTrackingAction
{
    const G4Track *transient_fSuspend_track = nullptr;
    SEvt *sev;

    TrackingAction(SEvt *sev) : sev(sev)
    {
    }

    void PreUserTrackingAction_Optical_FabricateLabel(const G4Track *track)
    {
        U4Track::SetFabricatedLabel(track);
        spho *label = STrackInfo::GetRef(track);
        assert(label);
    }

    void PreUserTrackingAction(const G4Track *track) override
    {
        if (track->GetDefinition() == G4OpticalPhoton::OpticalPhotonDefinition())
        {
            // Geant4 boundary updates optical velocity via ProposeVelocity, but the
            // track must honor the given velocity for post-boundary timing to match.
            G4Track *mutable_track = const_cast<G4Track *>(track);
            mutable_track->UseGivenVelocity(true);
        }

        spho *label = STrackInfo::GetRef(track);

        if (label == nullptr)
        {
            PreUserTrackingAction_Optical_FabricateLabel(track);
            label = STrackInfo::GetRef(track);
        }

        assert(label && label->isDefined());

        std::array<int, spho::N> a_label;
        label->serialize(a_label);

        spho ulabel = {};
        ulabel.load(a_label);

        U4Random::SetSequenceIndex(ulabel.id);

        bool resume_fSuspend = track == transient_fSuspend_track;

        if (ulabel.gen() == 0)
        {
            if (resume_fSuspend == false)
                sev->beginPhoton(ulabel);
            else
                sev->resumePhoton(ulabel);
        }
        else if (ulabel.gen() > 0)
        {
            if (resume_fSuspend == false)
                sev->rjoinPhoton(ulabel);
            else
                sev->rjoin_resumePhoton(ulabel);
        }
    }

    void PostUserTrackingAction(const G4Track *track) override
    {
        G4TrackStatus tstat = track->GetTrackStatus();

        bool is_fStopAndKill = tstat == fStopAndKill;
        bool is_fSuspend = tstat == fSuspend;
        bool is_fStopAndKill_or_fSuspend = is_fStopAndKill || is_fSuspend;

        assert(is_fStopAndKill_or_fSuspend);

        spho ulabel = {};
        get_label(ulabel, track);

        if (is_fStopAndKill)
        {
            U4Random::SetSequenceIndex(-1);
            sev->finalPhoton(ulabel);
            transient_fSuspend_track = nullptr;
        }
        else if (is_fSuspend)
        {
            transient_fSuspend_track = track;
        }
    }
};

struct G4App
{
    G4App(const gphox::Config& cfg, std::filesystem::path gdml_file)
        : sev(SEvt::CreateOrReuse_ECPU()), det_cons_(new DetectorConstruction(gdml_file)),
          prim_gen_(new PrimaryGenerator(cfg, sev)), event_act_(new EventAction(sev)), stepping_(new SteppingAction(sev)),
          tracking_(new TrackingAction(sev))
    {
    }

    //~G4App(){ G4CXOpticks::Finalize();}

    // Create "global" event
    SEvt *sev;

    G4VUserDetectorConstruction *det_cons_;
    G4VUserPrimaryGeneratorAction *prim_gen_;
    EventAction *event_act_;
    SteppingAction *stepping_;
    TrackingAction *tracking_;
};
