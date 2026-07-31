/*
File: SkullbonezSource/Rendering/RenderInstanceStore.cpp
Purpose:
  Builds model-order render instance snapshots from physics and presentation state.

Summary:
  Refresh copies renderer-facing values after gameplay/physics have committed.
  Body pose and shape come from physics stores; material and contact flash alpha
  come from explicit presentation records. It does not allocate GPU resources;
  it records the CPU-side draw intent that a future render snapshot can consume.

Invariants:
  - Records stay in scene model order and render handles mirror model indices
    until render owns a separate allocation id.
  - Creation appends all three render-side rows without allocation after one
    caller-owned cross-store preflight.
  - Refresh snapshots CPU draw intent only; it does not create or destroy GPU
    resources.

Related:
  - SkullbonezSource/Rendering/RenderInstanceStore.h
  - Agentic/Reference/engine-glossary.md
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
using SkullbonezCore::Physics::PhysicsBodyHotState;
using SkullbonezCore::Physics::PhysicsBodyOrientation;
using SkullbonezCore::Physics::PhysicsBodyPosition;
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

bool PoseMatchesCurrentEndpoint( const RenderInstanceRecord& record, const Vector3& position, const Quaternion& orientation )
{

    if ( record.currentPosition.x != position.x || record.currentPosition.y != position.y ||
         record.currentPosition.z != position.z )
    {
        return false;
    }

    float recordX = 0.0f;
    float recordY = 0.0f;
    float recordZ = 0.0f;
    float recordW = 1.0f;
    float bodyX = 0.0f;
    float bodyY = 0.0f;
    float bodyZ = 0.0f;
    float bodyW = 1.0f;
    record.currentOrientation.GetComponents( recordX, recordY, recordZ, recordW );
    orientation.GetComponents( bodyX, bodyY, bodyZ, bodyW );
    return recordX == bodyX && recordY == bodyY && recordZ == bodyZ && recordW == bodyW;
}

void ResetPoseHistory( RenderInstanceRecord& record, const Vector3& position, const Quaternion& orientation )
{
    record.previousPosition = position;
    record.currentPosition = position;
    record.previousOrientation = orientation;
    record.currentOrientation = orientation;
    record.poseHistoryValid = true;
}

Vector3 InterpolatePosition( const RenderInstanceRecord& record, float alpha )
{
    const float t = std::clamp( alpha, 0.0f, 1.0f );
    return record.previousPosition + ( record.currentPosition - record.previousPosition ) * t;
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
    m_presentationRecords.reserve( SkullbonezCore::Scene::Capacity::MAX_SCENE_OBJECTS );
    m_instances.reserve( SkullbonezCore::Scene::Capacity::MAX_SCENE_OBJECTS );
    m_modelInstanceHandles.reserve( SkullbonezCore::Scene::Capacity::MAX_SCENE_OBJECTS );
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
           m_modelInstanceHandles.size() == expected && m_presentationRecords.size() < m_presentationRecords.capacity() &&
           m_instances.size() < m_instances.capacity() && m_modelInstanceHandles.size() < m_modelInstanceHandles.capacity();
}

void RenderInstanceStore::CommitCreationRow( const RenderInstancePresentationRecord& presentation,
                                             const PhysicsBodyRecord& body, const PhysicsBodyHotState& hotState,
                                             const ColliderRecord& collider, int expectedIndex )
{

    if ( !CanAppendCreationRow( expectedIndex ) || !body.handle.IsValid() || !collider.handle.IsValid() ||
         collider.body != body.handle || collider.sceneObjectId != body.sceneObjectId )
    {
        SB_FATAL( "Rendering/RenderInstanceStore",
                  "Invalid preflighted creation commit. expected=%d presentation=%zu instances=%zu handles=%zu "
                  "body_valid=%d collider_valid=%d body_id=%u collider_id=%u",
                  expectedIndex, m_presentationRecords.size(), m_instances.size(), m_modelInstanceHandles.size(),
                  body.handle.IsValid() ? 1 : 0, collider.handle.IsValid() ? 1 : 0, body.sceneObjectId.value,
                  collider.sceneObjectId.value );
    }

    const uint32_t modelIndex = static_cast<uint32_t>( expectedIndex );
    RenderInstanceRecord record;
    record.handle = MakeRenderInstanceHandleForModelIndex( modelIndex );
    record.sceneObjectId = body.sceneObjectId;
    record.modelMatrix = BuildRenderModelMatrix( hotState.position, hotState.orientation, collider );
    record.material = presentation.material;
    record.boundingRadius = collider.boundingRadius;
    record.shapeKind = ShapeKindFromCollider( collider.shapeKind );
    record.shadowCasterStream = presentation.shadowCasterStream;
    record.editorVisible = presentation.editorVisible;
    record.isFixed = hotState.fixed;
    record.fixedContactAlpha = presentation.fixedContactAlpha;
    ResetPoseHistory( record, hotState.position, hotState.orientation );

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


std::span<const RenderInstancePresentationRecord> RenderInstanceStore::PresentationRecords() const
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

bool RenderInstanceStore::SetEditorVisible( int modelIndex, bool visible )
{
    RenderInstancePresentationRecord* presentation = MutablePresentationRecordForModelIndex( modelIndex );

    if ( !presentation || modelIndex < 0 || modelIndex >= Count() )
    {
        return false;
    }

    presentation->editorVisible = visible;
    m_instances[static_cast<std::size_t>( modelIndex )].editorVisible = visible;
    return true;
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
        record.fixedContactAlpha = ContactAlpha( record.fixedContactSeconds, 0.5f );
    }
}


void RenderInstanceStore::Clear()
{
    m_presentationRecords.clear();
    m_instances.clear();
    m_modelInstanceHandles.clear();
}


void RenderInstanceStore::BeginPhysicsStepPoseCapture( const PhysicsBodyStore& bodyStore )
{

    if ( bodyStore.Count() != Count() )
    {
        SB_FATAL( "Rendering/RenderInstanceStore", "Physics-step pose preflight requires matching rows. bodies=%d render=%d",
                  bodyStore.Count(), Count() );
    }

    const auto bodies = bodyStore.Records();
    const auto hotFields = bodyStore.HotFields();

    for ( int index = 0; index < bodyStore.Count(); ++index )
    {
        RenderInstanceRecord& record = m_instances[static_cast<std::size_t>( index )];
        const PhysicsBodyRecord& body = bodies[static_cast<std::size_t>( index )];

        // Hazard: upper-layer commands can teleport a body between
        // fixed ticks. Collapse both endpoints before stepping so presentation
        // never blends across that discontinuity.
        const Vector3 position = PhysicsBodyPosition( hotFields, static_cast<std::size_t>( index ) );
        const Quaternion orientation = PhysicsBodyOrientation( hotFields, static_cast<std::size_t>( index ) );

        if ( !record.poseHistoryValid || record.sceneObjectId != body.sceneObjectId ||
             !PoseMatchesCurrentEndpoint( record, position, orientation ) )
        {
            ResetPoseHistory( record, position, orientation );
        }
    }
}


void RenderInstanceStore::CompletePhysicsStepPoseCapture( const PhysicsBodyStore& bodyStore )
{

    if ( bodyStore.Count() != Count() )
    {
        SB_FATAL( "Rendering/RenderInstanceStore", "Physics-step pose commit requires matching rows. bodies=%d render=%d",
                  bodyStore.Count(), Count() );
    }

    const auto bodies = bodyStore.Records();
    const auto hotFields = bodyStore.HotFields();

    for ( int index = 0; index < bodyStore.Count(); ++index )
    {
        RenderInstanceRecord& record = m_instances[static_cast<std::size_t>( index )];
        const PhysicsBodyRecord& body = bodies[static_cast<std::size_t>( index )];

        if ( !record.poseHistoryValid || record.sceneObjectId != body.sceneObjectId )
        {
            ResetPoseHistory( record, PhysicsBodyPosition( hotFields, static_cast<std::size_t>( index ) ),
                              PhysicsBodyOrientation( hotFields, static_cast<std::size_t>( index ) ) );

            continue;
        }

        record.previousPosition = record.currentPosition;
        record.previousOrientation = record.currentOrientation;
        record.currentPosition = PhysicsBodyPosition( hotFields, static_cast<std::size_t>( index ) );
        record.currentOrientation = PhysicsBodyOrientation( hotFields, static_cast<std::size_t>( index ) );
    }
}


void RenderInstanceStore::Refresh( const PhysicsBodyStore& bodyStore, const ColliderStore& colliderStore,
                                   float presentationAlpha )
{
    Refresh( m_presentationRecords, bodyStore, colliderStore, presentationAlpha );
}


void RenderInstanceStore::Refresh( const std::vector<RenderInstancePresentationRecord>& presentation,
                                   const PhysicsBodyStore& bodyStore, const ColliderStore& colliderStore,
                                   float presentationAlpha )
{
    Refresh( presentation.empty() ? nullptr : presentation.data(), static_cast<int>( presentation.size() ), bodyStore,
             colliderStore, presentationAlpha );
}


void RenderInstanceStore::Refresh( const RenderInstancePresentationRecord* presentation, int presentationCount,
                                   const PhysicsBodyStore& bodyStore, const ColliderStore& colliderStore,
                                   float presentationAlpha )
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

    const auto bodies = bodyStore.Records();
    const auto hotFields = bodyStore.HotFields();
    const auto colliders = colliderStore.Records();

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
        const bool bodyIdentityChanged = record.sceneObjectId != body.sceneObjectId;
        record.handle = MakeRenderInstanceHandleForModelIndex( modelIndex );
        record.sceneObjectId = body.sceneObjectId;

        // A mismatch here did not pass through the fixed-step capture boundary:
        // scene load, spawn, teleport, state restore, or scrub changed the pose.
        // Collapse history so the discontinuity is visible immediately.
        const Vector3 bodyPosition = PhysicsBodyPosition( hotFields, index );
        const Quaternion bodyOrientation = PhysicsBodyOrientation( hotFields, index );

        if ( !record.poseHistoryValid || bodyIdentityChanged ||
             !PoseMatchesCurrentEndpoint( record, bodyPosition, bodyOrientation ) )
        {
            ResetPoseHistory( record, bodyPosition, bodyOrientation );
        }

        // Why: deterministic/fixed-step and capture frames intentionally use
        // alpha 1. Avoid quaternion normalization across every row on that
        // common validation path while preserving the exact same endpoint.
        const bool useCurrentEndpoint = presentationAlpha >= 1.0f;
        const Vector3 presentedPosition = useCurrentEndpoint ? record.currentPosition
                                                             : InterpolatePosition( record, presentationAlpha );

        const Quaternion presentedOrientation = useCurrentEndpoint
                                                    ? record.currentOrientation
                                                    : Math::Orientation::NlerpShortest( record.previousOrientation,
                                                                                        record.currentOrientation,
                                                                                        presentationAlpha );

        record.modelMatrix = BuildRenderModelMatrix( presentedPosition, presentedOrientation, collider );
        record.material = presentationRecord.material;
        record.boundingRadius = collider.boundingRadius;
        record.shapeKind = ShapeKindFromCollider( collider.shapeKind );
        record.shadowCasterStream = presentationRecord.shadowCasterStream;
        record.editorVisible = presentationRecord.editorVisible;
        record.isFixed = hotFields.fixed[index] != 0u;
        record.fixedContactAlpha = presentationRecord.fixedContactAlpha;
        m_modelInstanceHandles[index] = record.handle;
    }
}


bool RenderInstanceStore::TryGetPresentationPose( int modelIndex, float presentationAlpha, Vector3& outPosition,
                                                  Quaternion& outOrientation ) const
{

    if ( modelIndex < 0 || modelIndex >= Count() )
    {
        return false;
    }

    const RenderInstanceRecord& record = m_instances[static_cast<std::size_t>( modelIndex )];

    if ( !record.poseHistoryValid )
    {
        return false;
    }

    if ( presentationAlpha >= 1.0f )
    {
        outPosition = record.currentPosition;
        outOrientation = record.currentOrientation;
    }
    else
    {
        outPosition = InterpolatePosition( record, presentationAlpha );
        outOrientation = Math::Orientation::NlerpShortest( record.previousOrientation, record.currentOrientation,
                                                           presentationAlpha );
    }

    return true;
}


bool RenderInstanceStore::OverridePose( int modelIndex, Physics::PhysicsSceneObjectId sceneObjectId, const Vector3& position,
                                        const Quaternion& orientation, const ColliderStore& colliderStore )
{

    if ( modelIndex < 0 || modelIndex >= static_cast<int>( m_instances.size() ) )
    {
        return false;
    }

    const auto colliders = colliderStore.Records();

    if ( modelIndex >= static_cast<int>( colliders.size() ) )
    {
        return false;
    }

    RenderInstanceRecord& record = m_instances[static_cast<std::size_t>( modelIndex )];

    if ( record.sceneObjectId != sceneObjectId )
    {
        return false;
    }

    const ColliderRecord& collider = colliders[static_cast<std::size_t>( modelIndex )];
    record.modelMatrix = BuildRenderModelMatrix( position, orientation, collider );
    record.boundingRadius = collider.boundingRadius;
    record.shapeKind = ShapeKindFromCollider( collider.shapeKind );
    return true;
}


int RenderInstanceStore::Count() const
{
    return static_cast<int>( m_instances.size() );
}


RenderInstanceHandle RenderInstanceStore::HandleForModelIndex( int modelIndex ) const
{

    if ( modelIndex < 0 || modelIndex >= static_cast<int>( m_modelInstanceHandles.size() ) )
    {
        return RenderInstanceHandle {};
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


std::span<const RenderInstanceRecord> RenderInstanceStore::Records() const
{
    return m_instances;
}


std::size_t RenderInstanceStore::RecordCapacity() const
{
    return m_instances.capacity();
}
