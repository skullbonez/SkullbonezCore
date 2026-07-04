/*
File: SkullbonezSource/Rendering/RenderInstanceStore.h
Purpose:
  Owns a render-facing snapshot of physics transforms and material intent.

Mental model:
  Rendering still consumes model-order draw records through the existing
  renderer, but transform authority is physics-owned. The snapshot keeps model
  index order so future RenderSceneSnapshot work can compare output without
  changing pass order.

Glossary:
  Render instance: CPU-side record describing one model's draw transform and
    material intent.
  Material intent: Renderer-neutral description of surface style and texture
    selection.
  Contact highlight: Render-only feedback alpha for red fixed-body hits or
    white contact-audio flashes.
  Bounds radius: Conservative sphere radius used by shadow fitting and other
    render culling without borrowing the physics/model body stream.
  Shape kind: Cheap render-facing discriminator copied from collider metadata.
  RenderSceneSnapshot: Future immutable frame input consumed by render passes.
  Replay body id: Stable per-scene id shared with physics/replay records.

Invariants:
  - Instance order mirrors GameModelCollection so draw order stays stable.
  - Store refreshes do not touch GPU resources or renderer lifetime.

Related:
  - SkullbonezSource/Rendering/RenderInstanceStore.cpp
  - SkullbonezSource/Physics/PhysicsScene.h
*/
#pragma once

#include <cstdint>
#include <vector>

#include "../Maths/Matrix4.h"
#include "RenderMaterial.h"

namespace SkullbonezCore
{
namespace GameObjects
{
class GameModel;
}

namespace Physics
{
class ColliderStore;
class PhysicsBodyStore;
} // namespace Physics

namespace Rendering
{
inline constexpr uint32_t INVALID_RENDER_INSTANCE_HANDLE_INDEX = 0xffffffffu;
inline constexpr uint32_t RENDER_INSTANCE_COMPATIBILITY_HANDLE_GENERATION = 1u;

struct RenderInstanceHandle
{
    uint32_t index = INVALID_RENDER_INSTANCE_HANDLE_INDEX;
    uint32_t generation = 0;

    bool IsValid() const
    {
        return index != INVALID_RENDER_INSTANCE_HANDLE_INDEX && generation != 0;
    }
};

inline RenderInstanceHandle MakeCompatibilityRenderInstanceHandle( uint32_t modelIndex )
{
    RenderInstanceHandle handle;
    handle.index = modelIndex;
    handle.generation = RENDER_INSTANCE_COMPATIBILITY_HANDLE_GENERATION;
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

struct RenderInstanceRecord
{
    RenderInstanceHandle handle;                                         // Stable render handle paired with the legacy model slot.
    uint32_t replayBodyId = 0;                                           // Stable replay-facing body id paired with this instance.
    Math::Transformation::Matrix4 modelMatrix;                           // World transform used by object rendering.
    RenderMaterial material;                                             // Backend-neutral material intent.
    float boundingRadius = 0.0f;                                         // Conservative render/shadow bounds radius.
    RenderInstanceShapeKind shapeKind = RenderInstanceShapeKind::Sphere; // Cheap draw-path shape discriminator.
    bool isFixed = false;                                                // Fixed bodies can receive contact-highlight tinting.
    float fixedContactAlpha = 0.0f;                                      // Render-only red contact feedback strength.
    float audioContactAlpha = 0.0f;                                      // Render-only white audio-emitter feedback strength.
};

class RenderInstanceStore
{
  public:
    RenderInstanceStore();

    void Clear();
    void Refresh( std::vector<GameObjects::GameModel>& models,
                  const Physics::PhysicsBodyStore& bodyStore,
                  const Physics::ColliderStore& colliderStore );
    void Refresh( GameObjects::GameModel* models,
                  int modelCount,
                  const Physics::PhysicsBodyStore& bodyStore,
                  const Physics::ColliderStore& colliderStore );
    // Applies a one-frame presentation pose, such as replay scrub/prediction,
    // without writing that pose into PhysicsBodyStore.
    void OverridePoseFromModel( int modelIndex, GameObjects::GameModel& model );

    const RenderInstanceRecord* Data() const;
    int Count() const;
    bool Empty() const;
    RenderInstanceHandle HandleForModelIndex( int modelIndex ) const;
    int ModelIndexForHandle( RenderInstanceHandle handle ) const;
    bool Contains( RenderInstanceHandle handle ) const;
    const std::vector<RenderInstanceRecord>& Records() const;

  private:
    std::vector<RenderInstanceRecord> m_instances;                       // Render records in GameModelCollection index order.
    std::vector<RenderInstanceHandle> m_modelInstanceHandles;            // Legacy model index to render handle map.
};
} // namespace Rendering
} // namespace SkullbonezCore
