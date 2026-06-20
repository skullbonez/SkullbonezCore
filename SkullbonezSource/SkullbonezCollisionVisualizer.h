/*
File: SkullbonezSource/SkullbonezCollisionVisualizer.h
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

Invariants:
  - Physics-visible behavior must remain deterministic; byte-exact baselines
  are the validation contract.

Related:
  - SkullbonezSource/SkullbonezCollisionVisualizer.cpp
  - Agentic/Reference/physics-overview.md
  - Agentic/Reference/comment-style-guide.md
*/
#pragma once


#include <cstdint>
#include <memory>
#include <vector>
#include "SkullbonezIShader.h"
#include "SkullbonezMatrix4.h"


namespace SkullbonezCore
{
namespace GameObjects
{
class GameModel;
class GameModelCollection;
} // namespace GameObjects

namespace Math
{
namespace CollisionDetection
{
class ConvexHullShape;
} // namespace CollisionDetection
} // namespace Math

namespace Physics
{
/* -- Collision Visualizer ---------------------------------------------------------------------------------------------------------------------------------------

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
        float collisionAmount = 0.0f; // 1=red, 0=green; decays over FADE_DURATION when contact stops
    };

    struct Color
    {
        float r, g, b, a;
    };

    struct HullMeshResource
    {
        uint64_t hash = 0;
        uint32_t mesh = 0;
        int vertexCount = 0;
    };

    struct HullInstance
    {
        uint32_t mesh = 0;
        int vertexCount = 0;
        Math::Transformation::Matrix4 model;
        Color color = {};
    };

    static constexpr float FADE_DURATION = 0.5f;
    static constexpr int INSTANCE_FLOATS = 20; // mat4 + rgba

    bool m_enabled = false;
    float m_alphaOverride = -1.0f;
    float m_clipPlane[4] = { 0.0f, 1.0f, 0.0f, 1.0e9f };

    std::unique_ptr<Rendering::IShader> m_shader;
    uint32_t m_sphereInstMesh = 0;
    uint32_t m_boxInstMesh = 0;
    int m_sphereVertexCount = 0;
    int m_boxVertexCount = 0;

    std::vector<TrackedModel> m_models;
    std::vector<int> m_sleepGroupSizes;
    std::vector<float> m_sphereInstanceData;
    std::vector<float> m_boxInstanceData;
    std::vector<HullMeshResource> m_hullMeshes;
    std::vector<HullInstance> m_hullInstances;

    void BuildSphereMesh();
    void BuildBoxMesh();
    void EnsureResources();
    void AppendInstance( std::vector<float>& out, const Math::Transformation::Matrix4& model, const Color& color );
    uint32_t GetOrCreateHullMesh( const Math::CollisionDetection::ConvexHullShape& hull, int& outVertexCount );
    Color ComputeModelColor( int modelIndex, GameObjects::GameModelCollection& models ) const;
    void BuildSleepGroupSizes( GameObjects::GameModelCollection& models );
    void DrawInstances( uint32_t mesh, int vertexCount, const std::vector<float>& instanceData );
    void DrawHullInstance( const HullInstance& instance );

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
    void Render( GameObjects::GameModelCollection& models, const Math::Transformation::Matrix4& view, const Math::Transformation::Matrix4& proj, const float lightPos[4] );
};
} // namespace Physics
} // namespace SkullbonezCore
