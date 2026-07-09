/*
File: SkullbonezSource/Runtime/Debug/CollisionVisualizer.h
Purpose:
  Builds debug drawing for collision shapes and contact diagnostics.

Mental model:
  Physics is deterministic fixed-step state update. Units, contact ownership,
  solver stages, sleep policy, and baseline-sensitive behavior are the key
  reading anchors.

Glossary:
  Broadphase: Cheap collision pass that finds object pairs worth testing more
  precisely.
  Narrowphase: Precise collision pass that computes contact points, normals,
  and penetration.
  Manifold: Set of contact points and normals describing one colliding pair.
  Render resource factory: Renderer capability borrowed only while creating
    or destroying debug-only shader resources.
  Render command context: Renderer capability borrowed only while drawing a
    collision-visualizer frame.
  Render diagnostics: Renderer capability borrowed to name child draw-trace
    scopes without reopening global renderer access.
  Sleep group: Connected set of bodies that can stop simulating together once
  the solver decides motion is stable.

Invariants:
  - Physics-visible behavior must remain deterministic; byte-exact baselines
  are the validation contract.

Related:
  - SkullbonezSource/Runtime/Debug/CollisionVisualizer.cpp
  - Agentic/Reference/physics-overview.md
  - Agentic/Reference/comment-style-guide.md
*/
#pragma once


#include <array>
#include <cstdint>
#include <memory>
#include <vector>
#include "../../Rendering/IShader.h"
#include "../../Maths/Matrix4.h"


namespace SkullbonezCore
{
namespace Assets
{
class AssetSystem;
} // namespace Assets
namespace Rendering
{
class IRenderCommandContext;
class IRenderDiagnostics;
class IRenderResourceFactory;
class RenderInstanceStore;
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

struct CollisionVisualizerFrameView
{
    const PhysicsBodyStore& bodies;
    const ColliderStore& colliders;
    const Rendering::RenderInstanceStore& renderInstances;
    const std::vector<uint8_t>& collisionContacts;
    const std::vector<uint8_t>& sleepStates;
    const std::vector<int>& sleepIslandVisualIds;
    int modelCount = 0;
};

/* -- Collision Visualizer
---------------------------------------------------------------------------------------------------------------------------------------

    Solid-colour debug renderer for collision and sleep state. The visualizer is intentionally
    separate from the normal textured model renderer so the runtime can switch between the two
    without changing production materials.

    Layman version:
      This draws what the physics system thinks the collision volumes are. It is
      a read-only view over solver state, not an alternate collision system.
-----------------------------------------------------------------------------------------------------------------------------------------------------------------*/
class CollisionVisualizer
{
  private:
    struct TrackedModel
    {
        float collisionAmount = 0.0f;          // 1=red contact highlight, 0=green idle; decays after contact stops.
    };

    struct Color
    {
        float r, g, b, a;
    };

    static constexpr float FADE_DURATION = 0.5f;
    static constexpr int INSTANCE_FLOATS = 20; // mat4 + rgba
    // Invariant: mirrors ConvexHullShape::MAX_FACES/MAX_FACE_VERTICES without
    // including the hull definition in this debug visualizer header.
    static constexpr int HULL_MAX_TRIANGLE_VERTICES = 96 * ( 16 - 2 ) * 3;
    static constexpr int HULL_DYNAMIC_FLOATS_PER_VERTEX = 3 + 3 + INSTANCE_FLOATS;

    bool m_enabled = false;
    float m_alphaOverride = -1.0f;
    float m_clipPlane[4] = { 0.0f, 1.0f, 0.0f, 1.0e9f };

    std::unique_ptr<Rendering::IShader> m_shader;
    uint32_t m_sphereInstMesh = 0;
    uint32_t m_boxInstMesh = 0;
    uint32_t m_hullDynamicVB = 0;
    int m_sphereVertexCount = 0;
    int m_boxVertexCount = 0;

    std::vector<TrackedModel> m_models;
    std::vector<int> m_sleepGroupSizes;        // Per-island body counts used to color sleep/debug groups.
    std::vector<float> m_sphereInstanceData;   // CPU staging buffer for sphere instance matrices and colors.
    std::vector<float> m_boxInstanceData;      // CPU staging buffer for box instance matrices and colors.
    std::array<float, HULL_MAX_TRIANGLE_VERTICES * HULL_DYNAMIC_FLOATS_PER_VERTEX> m_hullDebugVertexData =
        {};                                    // CPU staging buffer for one convex-hull draw.

    void BuildSphereMesh( Rendering::IRenderResourceFactory& renderResources );
    void BuildBoxMesh( Rendering::IRenderResourceFactory& renderResources );
    void EnsureResources( Assets::AssetSystem& assets, Rendering::IRenderResourceFactory& renderResources );
    void AppendInstance( std::vector<float>& out, const Math::Transformation::Matrix4& model, const Color& color );
    Color ComputeModelColor( int modelIndex, const CollisionVisualizerFrameView& view ) const;
    void BuildSleepGroupSizes( const CollisionVisualizerFrameView& view );
    void DrawInstances( Rendering::IRenderCommandContext& renderCommands,
                        uint32_t mesh,
                        int vertexCount,
                        const std::vector<float>& instanceData );
    void DrawHullInstance( Rendering::IRenderCommandContext& renderCommands,
                           const Math::CollisionDetection::ConvexHullShape& hull,
                           const Math::Transformation::Matrix4& model,
                           const Color& color );

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
    void ResetResources( Rendering::IRenderResourceFactory* renderResources );
    void Update( float dt, const CollisionVisualizerFrameView& view );
    void Render( Assets::AssetSystem& assets,
                 Rendering::IRenderResourceFactory& renderResources,
                 Rendering::IRenderCommandContext& renderCommands,
                 Rendering::IRenderDiagnostics& renderDiagnostics,
                 const CollisionVisualizerFrameView& view,
                 const Math::Transformation::Matrix4& cameraView,
                 const Math::Transformation::Matrix4& proj,
                 const float lightPos[4] );
};
} // namespace Physics
} // namespace SkullbonezCore
