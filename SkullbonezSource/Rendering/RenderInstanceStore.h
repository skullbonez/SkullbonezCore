/*
File: SkullbonezSource/Rendering/RenderInstanceStore.h
Purpose:
  Owns a render-facing snapshot of model transforms and material intent.

Mental model:
  Rendering still consumes GameModelCollection through the existing renderer,
  but render instance data now has a named store boundary. The snapshot keeps
  model index order so future RenderSceneSnapshot work can compare output
  without changing pass order.

Glossary:
  Render instance: CPU-side record describing one model's draw transform and
    material intent.
  Material intent: Renderer-neutral description of surface style and texture
    selection.
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

namespace Rendering
{
struct RenderInstanceRecord
{
    uint32_t replayBodyId = 0;                     // Stable replay-facing body id paired with this instance.
    Math::Transformation::Matrix4 modelMatrix;     // World transform used by object rendering.
    RenderMaterial material;                       // Backend-neutral material intent.
    bool isFixed = false;                          // Fixed bodies can receive contact-highlight tinting.
    float fixedContactAlpha = 0.0f;                // Render-only red contact feedback strength.
};

class RenderInstanceStore
{
  public:
    RenderInstanceStore();

    void Clear();
    void Refresh( std::vector<GameObjects::GameModel>& models );

    const RenderInstanceRecord* Data() const;
    int Count() const;
    bool Empty() const;
    const std::vector<RenderInstanceRecord>& Records() const;

  private:
    std::vector<RenderInstanceRecord> m_instances; // Render records in GameModelCollection index order.
};
} // namespace Rendering
} // namespace SkullbonezCore
