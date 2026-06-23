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

Invariants:
  - Physics-visible behavior must remain deterministic; byte-exact baselines
  are the validation contract.

Related:
  - SkullbonezSource/Physics/TornadoField.cpp
  - Agentic/Reference/physics-overview.md
  - Agentic/Reference/comment-style-guide.md
*/
#pragma once


#include <vector>
#include "../Maths/Matrix4.h"
#include "../Maths/Vector3.h"


namespace SkullbonezCore
{
namespace Physics
{
struct TornadoFieldConfig
{
    bool enabled = false;
    bool visualizeVelocityField = false;
    Math::Vector::Vector3 center = Math::Vector::Vector3( 620.0f, 25.0f, 615.0f );
    float radius = 210.0f;
    float height = 140.0f;
    float inwardAcceleration = 120.0f;
    float swirlAcceleration = 170.0f;
    float liftAcceleration = 78.0f;
    float ejectAcceleration = 260.0f;
    float ejectUpAcceleration = 70.0f;
    float ejectBand = 0.88f;
    float minCaptureSeconds = 1.50f;
    float ejectCooldownSeconds = 2.25f;
    float maxDeltaVelocity = 24.0f;
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

    Math::Vector::Vector3 SampleAcceleration( const Math::Vector::Vector3& position ) const;
    void RenderVectors( const Math::Transformation::Matrix4& viewProj );

  private:
    TornadoFieldConfig m_config;
    std::vector<float> m_lineData;
};
} // namespace Physics
} // namespace SkullbonezCore
