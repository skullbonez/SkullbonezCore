/*
File: SkullbonezSource/Rendering/RenderInstanceStore.cpp
Purpose:
  Builds model-order render instance snapshots from physics and presentation state.

Mental model:
  Refresh copies renderer-facing values after gameplay/physics have committed.
  Body pose and shape come from physics stores; material and contact flash alpha
  come from explicit presentation records. It does not allocate GPU resources;
  it records the CPU-side draw intent that a future render snapshot can consume.

Glossary:
  Render instance: One draw-facing object record with transform and material
    intent.
  Material intent: Engine-level material choice before a renderer maps it to
    shaders, textures, or descriptor rows.
  Replay body id: Stable per-scene id shared with physics/replay records.
  Contact highlight: Render-only feedback alpha copied from presentation state
    after gameplay/physics feedback has advanced.

Invariants:
  - Records stay in scene model order and render handles mirror model indices
    until render owns a separate allocation id.
  - Creation appends all three render-side rows without allocation after one
    caller-owned cross-store preflight.
  - Refresh snapshots CPU draw intent only; it does not create or destroy GPU
    resources.

Related:
  - SkullbonezSource/Rendering/RenderInstanceStore.h
*/
#include "RenderInstanceStore.h"

#include <cassert>
#include <algorithm>
#include <cstddef>

#include "../Core/Common.h"
#include "../Core/FatalError.h"
#include "../Maths/Matrix4.h"
#include "../Physics/ColliderStore.h"
#include "../Physics/PhysicsBodyStore.h"

using SkullbonezCore::Math::CollisionDetection::GetShapeModelMatrix;
using SkullbonezCore::Math::Orientation::Quaternion;
using SkullbonezCore::Math::Transformation::Matrix4;
using SkullbonezCore::Math::Vector::Vector3;
using SkullbonezCore::Physics::ColliderRecord;
using SkullbonezCore::Physics::ColliderShapeKind;
using SkullbonezCore::Physics::ColliderStore;
using SkullbonezCore::Physics::PhysicsBodyRecord;
using SkullbonezCore::Physics::PhysicsBodyStore;
using SkullbonezCore::Rendering::RenderInstanceHandle;
using SkullbonezCore::Rendering::RenderInstancePresentationRecord;
using SkullbonezCore::Rendering::RenderInstanceRecord;
using SkullbonezCore::Rendering::RenderInstanceShapeKind;
using SkullbonezCore::Rendering::RenderInstanceStore;


namespace
{
Matrix4 BuildRenderModelMatrix( const Vector3& position, const Quaternion& orientation, const ColliderRecord& collider )
{
    const Matrix4 rotation = Matrix4::FromQuaternion( orientation );
    if ( collider.shapeKind == ColliderShapeKind::ConvexHull )
    {
        // Why: convex hull draw code transforms authored hull vertices directly.
        // Keep the legacy T * R body matrix here so moving renderers to this
        // snapshot does not add the collision-shape scale/offset a second time.
        return Matrix4::Translate( position ) * rotation;
    }
    return GetShapeModelMatrix( collider.shape, position, rotation );
}

Matrix4 BuildPhysicsModelMatrix( const PhysicsBodyRecord& body, const ColliderRecord& collider )
{
    return BuildRenderModelMatrix( body.position, body.orientation, collider );
}

RenderInstanceShapeKind ShapeKindFromCollider( ColliderShapeKind shapeKind )
{
    switch ( shapeKind )
    {
    case ColliderShapeKind::Sphere:
        return RenderInstanceShapeKind::Sphere;
    case ColliderShapeKind::Box:
        return RenderInstanceShapeKind::Box;
    case ColliderShapeKind::ConvexHull:
        return RenderInstanceShapeKind::ConvexHull;
    }
    return RenderInstanceShapeKind::Sphere;
}

float ContactAlpha( float seconds, float fadeSeconds )
{
    return fadeSeconds > 0.0f ? std::clamp( seconds / fadeSeconds, 0.0f, 1.0f ) : 0.0f;
}

void TickContactSeconds( float& seconds, float deltaSeconds )
{
    if ( seconds > 0.0f && deltaSeconds > 0.0f )
    {
        seconds = (std::max)( 0.0f, seconds - deltaSeconds );
    }
}
} // namespace


RenderInstanceStore::RenderInstanceStore()
{
    m_presentationRecords.reserve( MAX_GAME_MODELS );
    m_instances.reserve( MAX_GAME_MODELS );
    m_modelInstanceHandles.reserve( MAX_GAME_MODELS );
}


void RenderInstanceStore::ReservePresentationCapacity( std::size_t capacity )
{
    m_presentationRecords.reserve( capacity );
}

bool RenderInstanceStore::CanAppendCreationRow( int expectedCount ) const
{
    if ( expectedCount < 0 )
    {
        return false;
    }
    const std::size_t expected = static_cast<std::size_t>( expectedCount );
    return m_presentationRecords.size() == expected && m_instances.size() == expected &&
           m_modelInstanceHandles.size() == expected &&
           m_presentationRecords.size() < m_presentationRecords.capacity() &&
           m_instances.size() < m_instances.capacity() &&
           m_modelInstanceHandles.size() < m_modelInstanceHandles.capacity();
}

void RenderInstanceStore::CommitCreationRow( const RenderInstancePresentationRecord& presentation,
                                             const PhysicsBodyRecord& body,
                                             const ColliderRecord& collider,
                                             int expectedIndex )
{
    if ( !CanAppendCreationRow( expectedIndex ) || !body.handle.IsValid() || !collider.handle.IsValid() ||
         collider.body != body.handle || collider.sceneObjectId.value != body.sceneObjectId.value )
    {
        SB_FATAL( "Rendering/RenderInstanceStore",
                  "Invalid preflighted creation commit. expected=%d presentation=%zu instances=%zu handles=%zu "
                  "body_valid=%d collider_valid=%d body_id=%u collider_id=%u",
                  expectedIndex,
                  m_presentationRecords.size(),
                  m_instances.size(),
                  m_modelInstanceHandles.size(),
                  body.handle.IsValid() ? 1 : 0,
                  collider.handle.IsValid() ? 1 : 0,
                  body.sceneObjectId.value,
                  collider.sceneObjectId.value );
    }

    const uint32_t modelIndex = static_cast<uint32_t>( expectedIndex );
    RenderInstanceRecord record;
    record.handle = MakeRenderInstanceHandleForModelIndex( modelIndex );
    record.replayBodyId = body.replayBodyId;
    record.modelMatrix = BuildPhysicsModelMatrix( body, collider );
    record.material = presentation.material;
    record.boundingRadius = collider.boundingRadius;
    record.shapeKind = ShapeKindFromCollider( collider.shapeKind );
    record.isFixed = body.isFixed;
    record.fixedContactAlpha = presentation.fixedContactAlpha;
    record.audioContactAlpha = presentation.audioContactAlpha;

    // Invariant: CanAppendCreationRow proves all three pushes reuse existing
    // reservations, so no partial render row can result from allocation failure.
    m_presentationRecords.push_back( presentation );
    m_instances.push_back( record );
    m_modelInstanceHandles.push_back( record.handle );
}


bool RenderInstanceStore::DestroyCreationRowAtSwapLast( int modelIndex )
{
    if ( modelIndex < 0 || modelIndex >= Count() || modelIndex >= PresentationCount() )
    {
        return false;
    }

    const std::size_t row = static_cast<std::size_t>( modelIndex );
    const std::size_t last = m_instances.size() - 1u;
    // Invariant: the moved row receives its dense render handle immediately;
    // retaining the old row-derived handle would redirect later draw lookups.
    if ( row != last )
    {
        m_presentationRecords[row] = std::move( m_presentationRecords[last] );
        m_instances[row] = std::move( m_instances[last] );
        const RenderInstanceHandle movedHandle = MakeRenderInstanceHandleForModelIndex( static_cast<uint32_t>( row ) );
        m_instances[row].handle = movedHandle;
        m_modelInstanceHandles[row] = movedHandle;
    }
    m_presentationRecords.pop_back();
    m_instances.pop_back();
    m_modelInstanceHandles.pop_back();
    return true;
}


bool RenderInstanceStore::ResizePresentationRecords( int presentationCount )
{
    if ( presentationCount < 0 )
    {
        return false;
    }
    m_presentationRecords.resize( static_cast<std::size_t>( presentationCount ) );
    return true;
}


RenderInstancePresentationRecord* RenderInstanceStore::MutablePresentationRecordForModelIndex( int modelIndex )
{
    if ( modelIndex < 0 || modelIndex >= static_cast<int>( m_presentationRecords.size() ) )
    {
        return nullptr;
    }
    return &m_presentationRecords[static_cast<std::size_t>( modelIndex )];
}


const std::vector<RenderInstancePresentationRecord>& RenderInstanceStore::PresentationRecords() const
{
    return m_presentationRecords;
}

int RenderInstanceStore::PresentationCount() const
{
    return static_cast<int>( m_presentationRecords.size() );
}

std::size_t RenderInstanceStore::PresentationCapacity() const
{
    return m_presentationRecords.capacity();
}

uint64_t RenderInstanceStore::PresentationCapacityBytes() const
{
    return static_cast<uint64_t>( m_presentationRecords.capacity() ) * sizeof( RenderInstancePresentationRecord );
}

void RenderInstanceStore::NotifyFixedContact( int modelIndex, float highlightSeconds )
{
    RenderInstancePresentationRecord* record = MutablePresentationRecordForModelIndex( modelIndex );
    if ( record && highlightSeconds > record->fixedContactSeconds )
    {
        record->fixedContactSeconds = highlightSeconds;
        record->fixedContactAlpha = ContactAlpha( record->fixedContactSeconds, 0.5f );
    }
}

void RenderInstanceStore::NotifyAudioContact( int modelIndex, float highlightSeconds )
{
    RenderInstancePresentationRecord* record = MutablePresentationRecordForModelIndex( modelIndex );
    if ( record && highlightSeconds > record->audioContactSeconds )
    {
        record->audioContactSeconds = highlightSeconds;
        record->audioContactAlpha = ContactAlpha( record->audioContactSeconds, 0.1f );
    }
}

void RenderInstanceStore::TickContactFeedback( int modelCount, float deltaSeconds )
{
    // Invariant: feedback follows the same dense rows as render presentation;
    // swap-last deletion moves both timers with the affected scene object.
    const int tickCount = (std::min)( (std::max)( modelCount, 0 ), PresentationCount() );
    for ( int index = 0; index < tickCount; ++index )
    {
        RenderInstancePresentationRecord& record = m_presentationRecords[static_cast<std::size_t>( index )];
        TickContactSeconds( record.fixedContactSeconds, deltaSeconds );
        TickContactSeconds( record.audioContactSeconds, deltaSeconds );
        record.fixedContactAlpha = ContactAlpha( record.fixedContactSeconds, 0.5f );
        record.audioContactAlpha = ContactAlpha( record.audioContactSeconds, 0.1f );
    }
}


void RenderInstanceStore::Clear()
{
    m_presentationRecords.clear();
    m_instances.clear();
    m_modelInstanceHandles.clear();
}


void RenderInstanceStore::Refresh( const PhysicsBodyStore& bodyStore, const ColliderStore& colliderStore )
{
    Refresh( m_presentationRecords, bodyStore, colliderStore );
}


void RenderInstanceStore::Refresh( const std::vector<RenderInstancePresentationRecord>& presentation,
                                   const PhysicsBodyStore& bodyStore,
                                   const ColliderStore& colliderStore )
{
    Refresh( presentation.empty() ? nullptr : presentation.data(),
             static_cast<int>( presentation.size() ),
             bodyStore,
             colliderStore );
}


void RenderInstanceStore::Refresh( const RenderInstancePresentationRecord* presentation,
                                   int presentationCount,
                                   const PhysicsBodyStore& bodyStore,
                                   const ColliderStore& colliderStore )
{
    if ( bodyStore.Count() != presentationCount || colliderStore.Count() != presentationCount )
    {
        assert( bodyStore.Count() == presentationCount );
        assert( colliderStore.Count() == presentationCount );
        // Hazard: rebuilding presentation state here would hide a broken owner
        // refresh. Fail closed so Debug catches the topology bug and release
        // builds do not draw stale model-owned poses.
        Clear();
        return;
    }
    assert( presentation != nullptr || presentationCount == 0 );

    const auto& bodies = bodyStore.Records();
    const auto& colliders = colliderStore.Records();

    // Invariant: render instance handles intentionally mirror model slots until
    // a future renderer-facing allocation owner replaces them with render ids.
    m_instances.resize( static_cast<std::size_t>( presentationCount ) );
    m_modelInstanceHandles.resize( static_cast<std::size_t>( presentationCount ) );
    for ( int i = 0; i < presentationCount; ++i )
    {
        const std::size_t index = static_cast<std::size_t>( i );
        const PhysicsBodyRecord& body = bodies[index];
        const ColliderRecord& collider = colliders[index];
        const RenderInstancePresentationRecord& presentationRecord = presentation[index];
        RenderInstanceRecord& record = m_instances[index];
        const uint32_t modelIndex = static_cast<uint32_t>( i );
        record.handle = MakeRenderInstanceHandleForModelIndex( modelIndex );
        record.replayBodyId = body.replayBodyId;
        record.modelMatrix = BuildPhysicsModelMatrix( body, collider );
        record.material = presentationRecord.material;
        record.boundingRadius = collider.boundingRadius;
        record.shapeKind = ShapeKindFromCollider( collider.shapeKind );
        record.isFixed = body.isFixed;
        record.fixedContactAlpha = presentationRecord.fixedContactAlpha;
        record.audioContactAlpha = presentationRecord.audioContactAlpha;
        m_modelInstanceHandles[index] = record.handle;
    }
}


bool RenderInstanceStore::OverridePose( int modelIndex,
                                        uint32_t replayBodyId,
                                        const Vector3& position,
                                        const Quaternion& orientation,
                                        const ColliderStore& colliderStore )
{
    if ( modelIndex < 0 || modelIndex >= static_cast<int>( m_instances.size() ) )
    {
        return false;
    }

    const auto& colliders = colliderStore.Records();
    if ( modelIndex >= static_cast<int>( colliders.size() ) )
    {
        return false;
    }

    RenderInstanceRecord& record = m_instances[static_cast<std::size_t>( modelIndex )];
    if ( record.replayBodyId != replayBodyId )
    {
        return false;
    }

    const ColliderRecord& collider = colliders[static_cast<std::size_t>( modelIndex )];
    record.modelMatrix = BuildRenderModelMatrix( position, orientation, collider );
    record.boundingRadius = collider.boundingRadius;
    record.shapeKind = ShapeKindFromCollider( collider.shapeKind );
    return true;
}


const RenderInstanceRecord* RenderInstanceStore::Data() const
{
    return m_instances.empty() ? nullptr : m_instances.data();
}


int RenderInstanceStore::Count() const
{
    return static_cast<int>( m_instances.size() );
}


bool RenderInstanceStore::Empty() const
{
    return m_instances.empty();
}


RenderInstanceHandle RenderInstanceStore::HandleForModelIndex( int modelIndex ) const
{
    if ( modelIndex < 0 || modelIndex >= static_cast<int>( m_modelInstanceHandles.size() ) )
    {
        return RenderInstanceHandle{};
    }

    return m_modelInstanceHandles[static_cast<std::size_t>( modelIndex )];
}


int RenderInstanceStore::ModelIndexForHandle( RenderInstanceHandle handle ) const
{
    if ( !Contains( handle ) )
    {
        return -1;
    }

    return static_cast<int>( handle.index );
}


bool RenderInstanceStore::Contains( RenderInstanceHandle handle ) const
{
    if ( !handle.IsValid() || handle.generation != RENDER_INSTANCE_INITIAL_HANDLE_GENERATION )
    {
        return false;
    }
    if ( handle.index >= m_instances.size() )
    {
        return false;
    }

    return m_instances[static_cast<std::size_t>( handle.index )].handle == handle;
}


const std::vector<RenderInstanceRecord>& RenderInstanceStore::Records() const
{
    return m_instances;
}
