/*
File: SkullbonezSource/Physics/TornadoField.h
Purpose:
  Computes a procedural tornado force field for generated physics scenes.

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
  Command context: Borrowed render-frame interface used only by debug vector
    drawing; force sampling remains renderer-independent.

Invariants:
  - Physics-visible behavior must remain deterministic; byte-exact baselines
  are the validation contract.

Related:
  - SkullbonezSource/Physics/TornadoField.cpp
  - Agentic/Reference/physics-overview.md
  - Agentic/Reference/comment-style-guide.md
*/
#pragma once


#include <cstddef>
#include <vector>
#include "../Maths/Matrix4.h"
#include "../Maths/Vector3.h"


namespace SkullbonezCore
{
namespace Rendering
{
class IRenderCommandContext;
}
namespace Physics
{
struct TornadoFieldConfig
{
    bool enabled = false;
    bool visualizeVelocityField = false;
    Math::Vector::Vector3 center = Math::Vector::Vector3( 620.0f, 25.0f, 615.0f );
    float radius = 210.0f;
    float height = 140.0f;
    float inwardAcceleration = 150.0f;
    float swirlAcceleration = 185.0f;
    float liftAcceleration = 64.0f;
    float ejectAcceleration = 260.0f;
    float ejectUpAcceleration = 70.0f;
    float ejectBand = 0.96f;
    float minCaptureSeconds = 2.50f;
    float ejectCooldownSeconds = 3.50f;
    float maxDeltaVelocity = 24.0f;
};

struct TornadoVortexConfig
{
    TornadoFieldConfig field;
    float spawnSeconds = 0.0f;
    float timeToLiveSeconds = 0.0f;
    float growSeconds = 2.0f;
    float shrinkSeconds = 2.0f;
    float driftRadius = 0.0f;
    float driftSpeed = 0.0f;
    float driftPhase = 0.0f;
    float repulsionRadius = 0.0f;
    float repulsionStrength = 0.0f;
};

struct TornadoSystemConfig
{
    bool enabled = false;
    bool visualizeVelocityField = false;
    std::vector<TornadoVortexConfig> vortices;
};

struct TornadoActiveVortex
{
    TornadoFieldConfig field;
    float strength = 0.0f;
    float ageSeconds = 0.0f;
    int sourceIndex = -1;
};

class TornadoField
{
  public:
    TornadoField();

    void SetConfig( const TornadoFieldConfig& config );
    const TornadoFieldConfig& GetConfig() const
    {
        return m_config;
    }

    static Math::Vector::Vector3 SampleAccelerationForConfig( const TornadoFieldConfig& config,
                                                              const Math::Vector::Vector3& position );
    Math::Vector::Vector3 SampleAcceleration( const Math::Vector::Vector3& position ) const;
    // Debug visualization only; physics force sampling does not require a renderer.
    void RenderVectors( Rendering::IRenderCommandContext& renderCommands,
                        const Math::Transformation::Matrix4& viewProj );
    std::size_t DynamicMemoryBytes() const;

  private:
    TornadoFieldConfig m_config;
    std::vector<float> m_lineData;
};

class TornadoSystem
{
  public:
    void SetConfig( const TornadoSystemConfig& config );
    const TornadoSystemConfig& GetConfig() const
    {
        return m_config;
    }
    bool IsEnabled() const;
    void ResetElapsedSeconds();
    void SetElapsedSeconds( float seconds );
    float GetElapsedSeconds() const
    {
        return m_elapsedSeconds;
    }
    void Tick( float dt );
    const std::vector<TornadoActiveVortex>& ActiveVortices() const
    {
        return m_activeVortices;
    }
    Math::Vector::Vector3 SampleAcceleration( const Math::Vector::Vector3& position ) const;
    // Debug visualization only; active vortices borrow render commands for line submission.
    void RenderVectors( Rendering::IRenderCommandContext& renderCommands,
                        const Math::Transformation::Matrix4& viewProj );
    std::size_t DynamicMemoryBytes() const;

    static void BuildActiveVortices( const TornadoSystemConfig& config,
                                     float elapsedSeconds,
                                     std::vector<TornadoActiveVortex>& outVortices );

  private:
    TornadoSystemConfig m_config;
    float m_elapsedSeconds = 0.0f;
    std::vector<TornadoActiveVortex> m_activeVortices;
    TornadoField m_debugField;

    void RebuildActiveVortices();
};
} // namespace Physics
} // namespace SkullbonezCore
