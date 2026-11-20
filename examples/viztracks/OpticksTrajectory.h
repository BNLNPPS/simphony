#pragma once

#include <vector>

#include "G4ThreeVector.hh"
#include "G4VTrajectory.hh"
#include "G4VTrajectoryPoint.hh"

struct OpticksTrajectoryPoint : public G4VTrajectoryPoint
{
    G4ThreeVector fPosition;

    OpticksTrajectoryPoint(const G4ThreeVector &pos) : fPosition(pos) {}
    ~OpticksTrajectoryPoint() override = default;

    const G4ThreeVector GetPosition() const override { return fPosition; }
};

struct OpticksTrajectory : public G4VTrajectory
{
    G4int fTrackID;
    G4ThreeVector fInitialMomentum;
    std::vector<OpticksTrajectoryPoint *> fPoints;

    OpticksTrajectory(G4int trackID, const G4ThreeVector &initialMomentum)
        : fTrackID(trackID), fInitialMomentum(initialMomentum)
    {
    }

    ~OpticksTrajectory() override
    {
        for (auto *p : fPoints)
            delete p;
    }

    void AddPoint(const G4ThreeVector &pos) { fPoints.push_back(new OpticksTrajectoryPoint(pos)); }

    G4int GetTrackID() const override { return fTrackID; }
    G4int GetParentID() const override { return 0; }
    G4String GetParticleName() const override { return "opticalphoton"; }
    G4double GetCharge() const override { return 0.; }
    G4int GetPDGEncoding() const override { return 0; }
    G4ThreeVector GetInitialMomentum() const override { return fInitialMomentum; }
    G4int GetPointEntries() const override { return G4int(fPoints.size()); }
    G4VTrajectoryPoint *GetPoint(G4int i) const override { return fPoints[i]; }
    void AppendStep(const G4Step *) override {}
    void MergeTrajectory(G4VTrajectory *) override {}
};
