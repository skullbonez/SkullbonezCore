/*
File: SkullbonezSource/Physics/Debug/CollisionVisualizer.h
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
    debug-only shader resources.
  Sleep group: Connected set of bodies that can stop simulating together once
  the solver decides motion is stable.

Invariants:
  - Physics-visible behavior must remain deterministic; byte-exact baselines
  are the validation contract.

Related:
  - SkullbonezSource/Physics/Debug/CollisionVisualizer.cpp
  - Agentic/Reference/physics-overview.md
  - Agentic/Reference/comment-style-guide.md
*/
#pragma once


#include <cstdint>
#include <memory>
#include <vector>
#include "../../Rendering/IShader.h"
#include "../../Maths/Matrix4.h"


namespace SkullbonezCore
{
namespace GameObjects
{
class GameModel;
class GameModelCollection;
} // namespace GameObjects
namespace Assets
{
class AssetSystem;
} // namespace Assets
namespace Rendering
{
class IRenderResourceFactory;
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

    void BuildSphereMesh();
    void BuildBoxMesh();
    void EnsureResources( Assets::AssetSystem& assets, Rendering::IRenderResourceFactory& renderResources );
    void AppendInstance( std::vector<float>& out, const Math::Transformation::Matrix4& model, const Color& color );
    Color ComputeModelColor( int modelIndex, GameObjects::GameModelCollection& models ) const;
    void BuildSleepGroupSizes( GameObjects::GameModelCollection& models );
    void DrawInstances( uint32_t mesh, int vertexCount, const std::vector<float>& instanceData );
    void DrawHullInstance( const Math::CollisionDetection::ConvexHullShape& hull,
                           const Math::Transformation::Matrix4& model,
                           const Color& color );

  public:
    CollisionVisualizer() = default;
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
    void ResetResources();
    void Update( float dt, GameObjects::GameModelCollection& models );
    void Render( Assets::AssetSystem& assets,
                 Rendering::IRenderResourceFactory& renderResources,
                 GameObjects::GameModelCollection& models,
                 const Math::Transformation::Matrix4& view,
                 const Math::Transformation::Matrix4& proj,
                 const float lightPos[4] );
};
} // namespace Physics
} // namespace SkullbonezCore
