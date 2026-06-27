/*
File: SkullbonezSource/Physics/PhysicsBodyStore.h
Purpose:
  Owns deterministic body-order mutable physics state during simulation.

Mental model:
  GameModelCollection is still the compatibility adapter, but physics-facing
  body data now has an explicit owner. The store mirrors body index order and
  replay ids so future migrations can keep solver ordering stable while moving
  callers off GameModel.

Glossary:
  Body: Simulated object state consumed by the physics step.
  Sleep: Optimization that skips stable bodies until contact or user action
    wakes them.
  Inverse mass: Reciprocal mass value; zero means an immovable body.
  Replay body id: Stable per-scene id used by replay and diagnostics.

Invariants:
  - Body records stay in GameModelCollection physics model order.
  - Store refreshes load compatibility GameModel state into the physics-owned
    body records before a step.
  - Store writeback is a named compatibility bridge for legacy render, replay,
    tool, terrain, and shape code that still reads GameModel state.

Related:
  - SkullbonezSource/Physics/PhysicsBodyStore.cpp
  - SkullbonezSource/Physics/PhysicsScene.h
  - Agentic/Plans/physics-playground-refactor-and-file-prefix-cleanup-plan.md
*/
#pragma once

#include <cstdint>
#include <vector>

#include "PhysicsHandles.h"
#include "../Maths/Quaternion.h"
#include "../Maths/Vector3.h"

namespace SkullbonezCore
{
namespace GameObjects
{
class GameModel;
}

namespace Physics
{
class PhysicsModelAccess;
class PhysicsModelMutableRange;

struct PhysicsBodyRecord
{
    PhysicsBodyHandle handle;                          // Stable body handle paired with the legacy model slot.
    PhysicsSceneObjectId sceneObjectId;                // Scene-local id currently mirrored from replay body id.
    int legacyModelIndex = -1;                         // Compatibility lookup back to GameModelCollection order.
    uint32_t replayBodyId = 0;                         // Stable replay-facing body id for this scene.
    Math::Vector::Vector3 position = Math::Vector::ZERO_VECTOR;
    Math::Orientation::Quaternion orientation = Math::Orientation::IDENTITY_QUATERNION;
    Math::Vector::Vector3 linearVelocity = Math::Vector::ZERO_VECTOR;
    Math::Vector::Vector3 angularVelocity = Math::Vector::ZERO_VECTOR;
    Math::Vector::Vector3 rotationalInertia = Math::Vector::ZERO_VECTOR;
    Math::Vector::Vector3 invRotationalInertia = Math::Vector::ZERO_VECTOR;
    Math::Vector::Vector3 pendingImpulse = Math::Vector::ZERO_VECTOR;
    Math::Vector::Vector3 pendingImpulseApplicationPoint = Math::Vector::ZERO_VECTOR;
    float mass = 0.0f;                                 // Authoring mass; fixed bodies still report mass.
    float invMass = 0.0f;                              // Solver inverse mass; fixed bodies use zero.
    bool isFixed = false;                              // True for immovable collision bodies.
    bool isSleeping = false;                           // Physics-owned sleep flag mirrored to diagnostics by model index.
    bool hasPendingImpulse = false;                    // One-shot impulse waiting for the next force integration pass.
};

class PhysicsBodyStore
{
  public:
    PhysicsBodyStore();

    void Clear();
    void Refresh( std::vector<GameObjects::GameModel>& models, const std::vector<uint8_t>& sleepStates );
    void Refresh( PhysicsModelAccess& modelAccess, const std::vector<uint8_t>& sleepStates );
    void LoadFromModels( std::vector<GameObjects::GameModel>& models, const std::vector<uint8_t>& sleepStates );
    void LoadFromModels( PhysicsModelMutableRange models, const std::vector<uint8_t>& sleepStates );
    void LoadFromModels( PhysicsModelAccess& modelAccess, const std::vector<uint8_t>& sleepStates );
    void LoadFromModelAccess( PhysicsModelAccess& modelAccess, const std::vector<uint8_t>& sleepStates );
    void ClearPendingImpulses();
    void WriteBackToModels( std::vector<GameObjects::GameModel>& models ) const;
    void WriteBackToModels( PhysicsModelMutableRange models ) const;
    void WriteBackToModels( PhysicsModelAccess& modelAccess ) const;
    void WriteBackToModelAt( std::vector<GameObjects::GameModel>& models, int modelIndex ) const;
    void WriteBackToModelAt( PhysicsModelMutableRange models, int modelIndex ) const;
    void WriteBackToModelAt( PhysicsModelAccess& modelAccess, int modelIndex ) const;
    void WriteBackToModelAccess( PhysicsModelAccess& modelAccess ) const;
    void WriteBackToModelAccessAt( PhysicsModelAccess& modelAccess, int modelIndex ) const;
    void CaptureMutableStateFromModelAt( std::vector<GameObjects::GameModel>& models, int modelIndex );
    void CaptureMutableStateFromModelAt( PhysicsModelMutableRange models, int modelIndex );
    void CaptureMutableStateFromModelAt( PhysicsModelAccess& modelAccess, int modelIndex );
    void CaptureMutableStateFromModelAccessAt( PhysicsModelAccess& modelAccess, int modelIndex );
    void CopySleepStatesFrom( const std::vector<uint8_t>& sleepStates );
    void CopySleepStatesTo( std::vector<uint8_t>& sleepStates ) const;

    const PhysicsBodyRecord* Data() const;
    int Count() const;
    bool Empty() const;
    PhysicsBodyHandle HandleForModelIndex( int modelIndex ) const;
    int ModelIndexForHandle( PhysicsBodyHandle handle ) const;
    bool Contains( PhysicsBodyHandle handle ) const;
    const std::vector<PhysicsBodyRecord>& Records() const;
    std::vector<PhysicsBodyRecord>& MutableRecords();
    PhysicsBodyRecord* MutableRecordForModelIndex( int modelIndex );
    const PhysicsBodyRecord* RecordForModelIndex( int modelIndex ) const;
    bool WakeBody( int modelIndex );
    bool SeedBodyAsleep( int modelIndex );
    bool SetPendingBodyImpulse( int modelIndex,
                                const Math::Vector::Vector3& impulse,
                                const Math::Vector::Vector3& localApplicationPoint );
    bool ApplyBodyImpulse( int modelIndex,
                           const Math::Vector::Vector3& impulse,
                           const Math::Vector::Vector3& localApplicationPoint );
    bool IntegrateBodyPose( std::vector<GameObjects::GameModel>& models, int modelIndex, float deltaSeconds );
    bool IntegrateBodyPose( PhysicsModelMutableRange models, int modelIndex, float deltaSeconds );
    bool ApplyCompatibilityForces( std::vector<GameObjects::GameModel>& models, int modelIndex, float deltaSeconds );
    bool ApplyCompatibilityForces( PhysicsModelMutableRange models, int modelIndex, float deltaSeconds );
    bool IntegrateBodyPose( PhysicsModelAccess& modelAccess, int modelIndex, float deltaSeconds );
    bool ApplyCompatibilityForces( PhysicsModelAccess& modelAccess, int modelIndex, float deltaSeconds );

  private:
    std::vector<PhysicsBodyRecord> m_bodies;           // Body records in GameModelCollection index order.
    std::vector<PhysicsBodyHandle> m_modelBodyHandles; // Legacy model index to body handle map.
};
} // namespace Physics
} // namespace SkullbonezCore
