/*
File: SkullbonezSource/Rendering/RenderInstanceStore.h
Purpose:
  Owns a render-facing snapshot of physics transforms and material intent.

Summary:
  Rendering still consumes model-order draw records through the existing
  renderer, but transform authority is physics-owned. The snapshot keeps model
  index order so future RenderSceneSnapshot work can compare output without
  changing pass order.

Glossary:
  Presentation record: Cold render-facing material, label, and highlight values
    copied from the model owner before physics/store projection.
  Bounds radius: Conservative sphere radius used by shadow fitting and other
    render culling without borrowing the physics/model body stream.
  Shape kind: Cheap render-facing discriminator copied from collider metadata.
  RenderSceneSnapshot: Future immutable frame input consumed by render passes.
  Pose history: Previous/current completed solver endpoints stored in the
    existing fixed-capacity instance row for allocation-free presentation.

Invariants:
  - Instance order mirrors scene/model slot order so draw order stays stable.
  - Presentation records are the only model-owned values the store needs during
    refresh; physics/collider stores supply transforms and bounds.
  - Store refreshes do not touch GPU resources or renderer lifetime.
  - Scene creation appends presentation, instance, and handle rows together
    only after the caller has preflighted the cross-owner transaction.
  - A discontinuity collapses both pose endpoints before it can be rendered.

Related:
  - SkullbonezSource/Rendering/RenderInstanceStore.cpp
  - SkullbonezSource/Physics/PhysicsEngine.h
  - Agentic/Reference/engine-glossary.md
*/
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "../Maths/Matrix4.h"
#include "../Maths/Quaternion.h"
#include "../Physics/PhysicsHandles.h"
#include "RenderMaterial.h"

namespace SkullbonezCore
{
namespace Math
{
namespace Vector
{
class Vector3;
}
} // namespace Math

namespace Physics
{
class ColliderStore;
class PhysicsBodyStore;
struct ColliderRecord;
struct PhysicsBodyHotState;
struct PhysicsBodyRecord;
} // namespace Physics

namespace Rendering
{
inline constexpr uint32_t INVALID_RENDER_INSTANCE_HANDLE_INDEX = 0xffffffffu;
inline constexpr uint32_t RENDER_INSTANCE_INITIAL_HANDLE_GENERATION = 1u;

struct RenderInstanceHandle
{
    uint32_t index = INVALID_RENDER_INSTANCE_HANDLE_INDEX;
    uint32_t generation = 0;

    bool IsValid() const
    {
        return index != INVALID_RENDER_INSTANCE_HANDLE_INDEX && generation != 0;
    }
};

inline RenderInstanceHandle MakeRenderInstanceHandleForModelIndex( uint32_t modelIndex )
{
    RenderInstanceHandle handle;
    handle.index = modelIndex;
    handle.generation = RENDER_INSTANCE_INITIAL_HANDLE_GENERATION;
    return handle;
}

inline bool operator==( const RenderInstanceHandle& lhs, const RenderInstanceHandle& rhs )
{
    return lhs.index == rhs.index && lhs.generation == rhs.generation;
}

inline bool operator!=( const RenderInstanceHandle& lhs, const RenderInstanceHandle& rhs )
{
    return !( lhs == rhs );
}

enum class RenderInstanceShapeKind : uint8_t
{
    Sphere,
    Box,
    ConvexHull
};

enum class ShadowCasterStream : uint8_t
{
    None,
    Sphere,
    Box,
    Pine,
    ConvexHull
};

struct RenderInstanceRecord
{
    RenderInstanceHandle handle;                                         // Stable render handle paired with the model slot.
    Physics::PhysicsSceneObjectId sceneObjectId;                         // Stable cross-system identity paired with this instance.
    Math::Transformation::Matrix4 modelMatrix;                           // World transform used by object rendering.
    RenderMaterial material;                                             // Backend-neutral material intent.
    float boundingRadius = 0.0f;                                         // Conservative render/shadow bounds radius.
    RenderInstanceShapeKind shapeKind = RenderInstanceShapeKind::Sphere; // Cheap draw-path shape discriminator.
    ShadowCasterStream shadowCasterStream = ShadowCasterStream::None;    // Owner-prepared opaque submission bin.
    bool editorVisible = true;                                           // Session-only hierarchy visibility; false suppresses raster/shadow submission.
    bool isFixed = false;                                                // Fixed bodies can receive contact-highlight tinting.
    float fixedContactAlpha = 0.0f;                                      // Render-only red contact feedback strength.
    Math::Vector::Vector3 previousPosition = Math::Vector::ZERO_VECTOR;  // Solver pose before the latest completed fixed tick.
    Math::Vector::Vector3 currentPosition = Math::Vector::ZERO_VECTOR;   // Solver pose after the latest completed fixed tick.
    Math::Orientation::Quaternion
        previousOrientation = Math::Orientation::IDENTITY_QUATERNION;    // Orientation paired with previousPosition.
    Math::Orientation::Quaternion
        currentOrientation = Math::Orientation::IDENTITY_QUATERNION;     // Orientation paired with currentPosition.
    bool poseHistoryValid = false;                                       // Both endpoints belong to this live body row.
};

struct RenderInstancePresentationRecord
{
    RenderMaterial material;                                             // Backend-neutral material intent.
    ShadowCasterStream shadowCasterStream = ShadowCasterStream::None;    // Scene-owner stream choice copied into the draw row.
    bool editorVisible = true;                                           // Scene editor visibility copied into the prepared draw row.
    char displayName[64] = {};                                           // Presentation/debug label paired with the model slot.
    bool simpleRagdollPart = false;                                      // Presentation filter metadata copied from scene grouping.
    float fixedContactAlpha = 0.0f;                                      // Render-only red contact feedback strength.
    float fixedContactSeconds = 0.0f;                                    // Seconds remaining for fixed-body contact feedback.
};

class RenderInstanceStore
{
  public:
    RenderInstanceStore();

    void ReservePresentationCapacity( std::size_t capacity );
    bool CanAppendCreationRow( int expectedCount ) const;
    void CommitCreationRow( const RenderInstancePresentationRecord& presentation, const Physics::PhysicsBodyRecord& body,
                            const Physics::PhysicsBodyHotState& hotState, const Physics::ColliderRecord& collider,
                            int expectedIndex );

    // Scene deletion compacts presentation, instance, and handle rows together.
    bool DestroyCreationRowAtSwapLast( int modelIndex );
    bool ResizePresentationRecords( int presentationCount );
    RenderInstancePresentationRecord* MutablePresentationRecordForModelIndex( int modelIndex );
    std::span<const RenderInstancePresentationRecord> PresentationRecords() const;
    int PresentationCount() const;
    std::size_t PresentationCapacity() const;
    uint64_t PresentationCapacityBytes() const;
    void NotifyFixedContact( int modelIndex, float highlightSeconds );

    // Updates both paired presentation rows immediately; false means the dense
    // model row is stale or outside the active scene topology.
    bool SetEditorVisible( int modelIndex, bool visible );
    void TickContactFeedback( int modelCount, float deltaSeconds );
    void Clear();
    void BeginPhysicsStepPoseCapture( const Physics::PhysicsBodyStore& bodyStore );
    void CompletePhysicsStepPoseCapture( const Physics::PhysicsBodyStore& bodyStore );
    void Refresh( const Physics::PhysicsBodyStore& bodyStore, const Physics::ColliderStore& colliderStore,
                  float presentationAlpha = 1.0f );
    void Refresh( const std::vector<RenderInstancePresentationRecord>& presentation,
                  const Physics::PhysicsBodyStore& bodyStore, const Physics::ColliderStore& colliderStore,
                  float presentationAlpha = 1.0f );
    void Refresh( const RenderInstancePresentationRecord* presentation, int presentationCount,
                  const Physics::PhysicsBodyStore& bodyStore, const Physics::ColliderStore& colliderStore,
                  float presentationAlpha = 1.0f );

    // Applies a one-frame presentation pose, such as a scrub or simulation preview,
    // without writing that pose into PhysicsBodyStore or authoring storage.
    bool OverridePose( int modelIndex, Physics::PhysicsSceneObjectId sceneObjectId, const Math::Vector::Vector3& position,
                       const Math::Orientation::Quaternion& orientation, const Physics::ColliderStore& colliderStore );
    bool TryGetPresentationPose( int modelIndex, float presentationAlpha, Math::Vector::Vector3& outPosition,
                                 Math::Orientation::Quaternion& outOrientation ) const;

    int Count() const;
    RenderInstanceHandle HandleForModelIndex( int modelIndex ) const;
    int ModelIndexForHandle( RenderInstanceHandle handle ) const;
    bool Contains( RenderInstanceHandle handle ) const;

    // Lifetime: read spans borrow scene-order rows and expire on scene mutation
    // or store destruction; callers must not retain them across frames.
    std::span<const RenderInstanceRecord> Records() const;
    std::size_t RecordCapacity() const;

  private:
    std::vector<RenderInstancePresentationRecord>
        m_presentationRecords;                                           // Render-facing material/highlight values keyed by model slot.
    std::vector<RenderInstanceRecord> m_instances;                       // Render records in scene/model slot order.
    std::vector<RenderInstanceHandle> m_modelInstanceHandles;            // Model index to render handle map.
};
} // namespace Rendering
} // namespace SkullbonezCore
