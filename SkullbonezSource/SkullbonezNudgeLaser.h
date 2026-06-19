/*
File: SkullbonezSource/SkullbonezNudgeLaser.h
Purpose:
  Owns nudge-mode laser ribbon shots and their transient render resources.

Mental model:
  The laser is display-only feedback for camera ray nudges. It never changes
  physics state; callers provide the already-computed hit/miss segment.

Related:
  - SkullbonezSource/SkullbonezNudgeLaser.cpp
  - SkullbonezSource/SkullbonezRunInput.cpp
*/
#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

#include "SkullbonezMatrix4.h"
#include "SkullbonezVector3.h"

namespace SkullbonezCore
{
namespace Rendering
{
class IShader;
}

namespace Basics
{
class NudgeLaser
{
  private:
    struct Shot
    {
        Math::Vector::Vector3 start = Math::Vector::ZERO_VECTOR;
        Math::Vector::Vector3 end = Math::Vector::ZERO_VECTOR;
        float ageSeconds = 0.0f;
        float lifetimeSeconds = 0.18f;
        bool active = false;
        bool hit = false;
    };

    static constexpr std::size_t MAX_SHOTS = 32;
    static constexpr int MAX_VERTICES = static_cast<int>( MAX_SHOTS * 36 );

    std::array<Shot, MAX_SHOTS> m_shots = {};
    int m_nextShot = 0;
    std::vector<float> m_vertices;
    std::unique_ptr<Rendering::IShader> m_shader;
    uint32_t m_dynamicVB = 0;

    void EnsureResources();
    void EmitVertex( const Math::Vector::Vector3& p, float r, float g, float b, float a );
    void EmitQuad( const Math::Vector::Vector3& a,
                   const Math::Vector::Vector3& b,
                   const Math::Vector::Vector3& c,
                   const Math::Vector::Vector3& d,
                   float r,
                   float g,
                   float bl,
                   float alpha );
    void EmitRibbon( const Math::Vector::Vector3& a,
                     const Math::Vector::Vector3& b,
                     const Math::Vector::Vector3& widthAxis,
                     float halfWidth,
                     float r,
                     float g,
                     float bl,
                     float alpha );
    void EmitShot( const Shot& shot,
                   const Math::Vector::Vector3& cameraEye,
                   const Math::Vector::Vector3& cameraUp );

  public:
    NudgeLaser();
    ~NudgeLaser();

    void ResetResources();
    void Clear();
    void Fire( const Math::Vector::Vector3& rayOrigin,
               const Math::Vector::Vector3& rayDirection,
               const Math::Vector::Vector3& cameraUp,
               float distance,
               bool hit );
    void Update( float dt );
    void Render( const Math::Transformation::Matrix4& viewProjection,
                 const Math::Vector::Vector3& cameraEye,
                 const Math::Vector::Vector3& cameraUp );
};
} // namespace Basics
} // namespace SkullbonezCore
