/*
File: SkullbonezSource/Runtime/Render/CollisionVisualizer.h
Purpose:
  Builds debug drawing for collision shapes and contact diagnostics.

Summary:
  Render owns collision fade state, GPU resources, and submission while
  Physics supplies only synchronous store and diagnostic views.

Glossary:
  Sleep group: Connected set of bodies that can stop simulating together once
  the solver decides motion is stable.

Invariants:
  - Physics-visible behavior must remain deterministic; byte-exact baselines
  are the validation contract.

Related:
  - SkullbonezSource/Runtime/Render/CollisionVisualizer.cpp
  - Agentic/Reference/physics-overview.md
  - Agentic/Reference/engine-glossary.md
*/
#pragma once


#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>
#include "../../Rendering/DX12/ShaderDX12.h"
#include "../../Maths/Matrix4.h"


namespace SkullbonezCore
{
namespace Assets
{
class AssetSystem;
} // namespace Assets
namespace Rendering
{
class Dx12GeometryOwner;
class Dx12Diagnostics;
class Dx12ResourceBuilder;
class Dx12GeometryOwner;
class RenderInstanceStore;
struct PassRasterStateBucket;
} // namespace Rendering

namespace Math
{
namespace CollisionDetection
{
class ConvexHullShape;
} // namespace CollisionDetection
} // namespace Math

namespace Physics
{
class ColliderStore;
class PhysicsBodyStore;
} // namespace Physics

namespace Runtime
{

struct CollisionVisualizerFrameView
{
    const Physics::PhysicsBodyStore& bodies;
    const Physics::ColliderStore& colliders;
    const Rendering::RenderInstanceStore& renderInstances;
    std::span<const uint8_t> collisionContacts;
    std::span<const uint8_t> sleepStates;
    std::span<const int> sleepIslandVisualIds;
    int modelCount = 0;
};

enum class CollisionVisualizerResourcePhase : uint8_t
{
    BackendInit,
    Render,
};

enum class CollisionVisualizerResourceAction : uint8_t
{
    Skip,
    Prepare,
    Draw,
};

constexpr CollisionVisualizerResourceAction ResolveCollisionVisualizerResourceAction(
    CollisionVisualizerResourcePhase phase, bool resourcesReady )
{
    if ( phase == CollisionVisualizerResourcePhase::BackendInit )
    {
        return resourcesReady ? CollisionVisualizerResourceAction::Skip
                              : CollisionVisualizerResourceAction::Prepare;
    }

    return resourcesReady ? CollisionVisualizerResourceAction::Draw : CollisionVisualizerResourceAction::Skip;
}

template <typename PrepareResources, typename ResourcesReady>
bool PrepareCollisionVisualizerResourcePhase( PrepareResources&& prepareResources, ResourcesReady&& resourcesReady )
{
    // BackendInit is the sole creation phase. The injected operations keep this
    // sequencing testable without granting Render an asset or resource builder.
    if ( ResolveCollisionVisualizerResourceAction( CollisionVisualizerResourcePhase::BackendInit, resourcesReady() ) ==
         CollisionVisualizerResourceAction::Prepare )
    {
        prepareResources();
    }

    return resourcesReady();
}

/*
Concept: Collision visualizer

    Solid-colour debug renderer for collision and sleep state. The visualizer is intentionally
    separate from the normal textured model renderer so the runtime can switch between the two
    without changing production materials.

    This draws what the physics system thinks the collision volumes are. It is
      a read-only view over solver state, not an alternate collision system.
*/
class CollisionVisualizer
{
  private:
    struct TrackedModel
    {
        float collisionAmount = 0.0f;                                                                          // 1=red contact highlight, 0=green idle; decays after contact stops.
    };

    struct Color
    {
        float r, g, b, a;
    };

    static constexpr float FADE_DURATION = 0.5f;
    static constexpr int INSTANCE_FLOATS = 20;                                                                 // mat4 + rgba

    // Invariant: mirrors ConvexHullShape::MAX_FACES/MAX_FACE_VERTICES without
    // including the hull definition in this debug visualizer header.
    static constexpr int HULL_MAX_TRIANGLE_VERTICES = 96 * ( 16 - 2 ) * 3;
    static constexpr int HULL_DYNAMIC_FLOATS_PER_VERTEX = 3 + 3 + INSTANCE_FLOATS;

    bool m_enabled = false;
    float m_alphaOverride = -1.0f;
    float m_clipPlane[4] = { 0.0f, 1.0f, 0.0f, 1.0e9f };

    std::unique_ptr<Rendering::ShaderDX12> m_shader;
    uint32_t m_sphereInstMesh = 0;
    uint32_t m_boxInstMesh = 0;
    uint32_t m_hullDynamicVB = 0;
    int m_sphereVertexCount = 0;
    int m_boxVertexCount = 0;

    std::vector<TrackedModel> m_models;
    std::vector<int> m_sleepGroupSizes;                                                                        // Per-island body counts used to color sleep/debug groups.
    std::vector<float> m_sphereInstanceData;                                                                   // CPU staging buffer for sphere instance matrices and colors.
    std::vector<float> m_boxInstanceData;                                                                      // CPU staging buffer for box instance matrices and colors.
    std::array<float, HULL_MAX_TRIANGLE_VERTICES * HULL_DYNAMIC_FLOATS_PER_VERTEX> m_hullDebugVertexData = {}; // CPU staging buffer for one convex-hull draw.

    void BuildSphereMesh( Rendering::Dx12GeometryOwner& renderGeometry );
    void BuildBoxMesh( Rendering::Dx12GeometryOwner& renderGeometry );
    void AppendInstance( std::vector<float>& out, const Math::Transformation::Matrix4& model, const Color& color );
    Color ComputeModelColor( int modelIndex, const CollisionVisualizerFrameView& view ) const;
    void BuildSleepGroupSizes( const CollisionVisualizerFrameView& view );
    void DrawInstances( Rendering::Dx12GeometryOwner& renderCommands, uint32_t mesh, int vertexCount,
                        const std::vector<float>& instanceData, const Rendering::PassRasterStateBucket& rasterState );
    void DrawHullInstance( Rendering::Dx12GeometryOwner& renderCommands,
                           const Math::CollisionDetection::ConvexHullShape& hull, const Math::Transformation::Matrix4& model,
                           const Color& color, const Rendering::PassRasterStateBucket& rasterState );

  public:
    CollisionVisualizer();
    ~CollisionVisualizer();

    void SetEnabled( bool enabled )
    {
        m_enabled = enabled;
    }
    bool IsEnabled() const
    {
        return m_enabled;
    }
    void Toggle()
    {
        m_enabled = !m_enabled;
    }
    void SetClipPlane( float x, float y, float z, float w );
    void SetAlphaOverride( float alpha );
    void ResetResources( Rendering::Dx12GeometryOwner* renderGeometry );
    void ResetTransientState();
    void EnsureGpuResources( Assets::AssetSystem& assets, Rendering::Dx12ResourceBuilder& renderResources,
                             Rendering::Dx12GeometryOwner& renderGeometry );
    bool ResourcesReady() const
    {
        return m_shader != nullptr && m_sphereInstMesh != 0 && m_boxInstMesh != 0 && m_hullDynamicVB != 0;
    }
    std::size_t DiagnosticTrackedModelCount() const
    {
        return m_models.size();
    }
    float DiagnosticCollisionAmount( std::size_t index ) const
    {
        return index < m_models.size() ? m_models[index].collisionAmount : 0.0f;
    }
    void Update( float dt, const CollisionVisualizerFrameView& view );
    void Render( Rendering::Dx12GeometryOwner& renderGeometry, Rendering::Dx12Diagnostics& renderDiagnostics,
                 const CollisionVisualizerFrameView& view, const Math::Transformation::Matrix4& cameraView,
                 const Math::Transformation::Matrix4& proj, const float lightPos[4] );
};
} // namespace Runtime
} // namespace SkullbonezCore
