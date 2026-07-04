/*
File: SkullbonezSource/GameObjects/GameModelCollectionPhysicsAdapter.cpp
Purpose:
  Implements the legacy GameModel-to-physics-handle command bridge.

Mental model:
  This file is deliberately small migration glue. Runtime, scene, editor, and
  replay call sites can keep their current model-index inputs while the bridge
  resolves those inputs to physics handles before entering PhysicsEngine.

Glossary:
  Handle conversion: Translation from a legacy scene/model identity into the
    public PhysicsBodyHandle accepted by the physics facade.
  PhysicsModelAccess: Stack-owned owner facade used after handle conversion so
    physics commands do not require GameModelCollection inheritance.
  No-op rejection: Invalid legacy identity resolves to an invalid handle and
    does not call into PhysicsEngine.

Invariants:
  - Model-index validation happens before asking PhysicsBodyStore for a body
    handle.
  - Scene-object lookup scans current model order only to find identity; it does
    not lend out the backing vector.
  - PhysicsEngine remains the only object that mutates physics state.

Related:
  - SkullbonezSource/GameObjects/GameModelCollectionPhysicsAdapter.h
  - SkullbonezSource/GameObjects/GameModelCollection.cpp
*/
#include "GameModelCollectionPhysicsAdapter.h"

#include <cstddef>
#include <vector>

#include "GameModel.h"
#include "GameModelCollection.h"
#include "../Physics/PhysicsBodyStore.h"
#include "../Physics/PhysicsEngine.h"

using SkullbonezCore::GameObjects::GameModel;
using SkullbonezCore::GameObjects::GameModelCollectionPhysicsAdapter;
using SkullbonezCore::Physics::PhysicsBodyHandle;
using SkullbonezCore::Physics::PhysicsModelAccess;
using SkullbonezCore::Physics::PhysicsSceneObjectId;


GameModelCollectionPhysicsAdapter::GameModelCollectionPhysicsAdapter( GameModelCollection& collection )
    : m_collection( collection )
{
}


PhysicsBodyHandle GameModelCollectionPhysicsAdapter::BodyHandleForModelIndex( int modelIndex ) const
{
    if ( modelIndex < 0 || modelIndex >= m_collection.GetModelCount() )
    {
        return PhysicsBodyHandle{};
    }

    PhysicsModelAccess modelAccess( m_collection );
    // Invariant: handle lookup only imports GameModel body data when topology
    // changed. Same-count body state belongs to PhysicsBodyStore after explicit
    // editor/replay commits and should not be reloaded here.
    if ( m_collection.m_physicsEngine.BodyStore().Count() != m_collection.GetModelCount() )
    {
        m_collection.m_physicsEngine.RefreshBodyStore( modelAccess );
    }
    return m_collection.m_physicsEngine.BodyStore().HandleForModelIndex( modelIndex );
}


PhysicsBodyHandle
GameModelCollectionPhysicsAdapter::BodyHandleForSceneObjectId( PhysicsSceneObjectId sceneObjectId ) const
{
    if ( !sceneObjectId.IsValid() )
    {
        return PhysicsBodyHandle{};
    }

    // Invariant: scene object ids are derived from replay body ids during this
    // migration. A duplicate identity is ambiguous, so fail closed instead of
    // choosing the first vector slot and hiding an authoring/replay bug.
    bool foundMatch = false;
    PhysicsBodyHandle matchedBody;
    const std::vector<GameModel>& models = m_collection.Models();
    for ( int modelIndex = 0; modelIndex < static_cast<int>( models.size() ); ++modelIndex )
    {
        const GameModel& model = models[static_cast<std::size_t>( modelIndex )];
        if ( SkullbonezCore::Physics::MakePhysicsSceneObjectIdFromReplayBodyId( model.GetReplayBodyId() ) ==
             sceneObjectId )
        {
            if ( foundMatch )
            {
                return PhysicsBodyHandle{};
            }
            foundMatch = true;
            matchedBody = BodyHandleForModelIndex( modelIndex );
        }
    }

    return matchedBody;
}


void GameModelCollectionPhysicsAdapter::WakeBodyForModelIndex( int modelIndex ) const
{
    const PhysicsBodyHandle body = BodyHandleForModelIndex( modelIndex );
    if ( !body.IsValid() )
    {
        return;
    }

    PhysicsModelAccess modelAccess( m_collection );
    m_collection.m_physicsEngine.WakeBody( modelAccess, body );
}


void GameModelCollectionPhysicsAdapter::SeedBodyAsleepForModelIndex( int modelIndex ) const
{
    const PhysicsBodyHandle body = BodyHandleForModelIndex( modelIndex );
    if ( !body.IsValid() )
    {
        return;
    }

    m_collection.m_physicsEngine.SeedBodyAsleep( body );
}


void GameModelCollectionPhysicsAdapter::ApplyBodyImpulseForModelIndex(
    int modelIndex,
    const Math::Vector::Vector3& impulse,
    const Math::Vector::Vector3& localApplicationPoint ) const
{
    const PhysicsBodyHandle body = BodyHandleForModelIndex( modelIndex );
    if ( !body.IsValid() )
    {
        return;
    }

    PhysicsModelAccess modelAccess( m_collection );
    m_collection.m_physicsEngine.ApplyBodyImpulse( modelAccess, body, impulse, localApplicationPoint );
}


void GameModelCollectionPhysicsAdapter::SetPendingBodyImpulseForModelIndex(
    int modelIndex,
    const Math::Vector::Vector3& impulse,
    const Math::Vector::Vector3& localApplicationPoint ) const
{
    const PhysicsBodyHandle body = BodyHandleForModelIndex( modelIndex );
    if ( !body.IsValid() )
    {
        return;
    }

    PhysicsModelAccess modelAccess( m_collection );
    m_collection.m_physicsEngine.SetPendingBodyImpulse( modelAccess, body, impulse, localApplicationPoint );
}
