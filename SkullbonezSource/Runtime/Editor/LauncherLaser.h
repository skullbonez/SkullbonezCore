/*
File: SkullbonezSource/Runtime/Editor/LauncherLaser.h
Purpose:
  Owns launcher-mode laser ribbon shots and their transient render resources.

Mental model:
  The laser is display-only feedback for launcher-mode ray shots. It never changes
  physics state; callers provide the already-computed hit/miss segment.

Glossary:
  Billboard: Camera-facing quad built from a world-space segment and view
    direction.
  Ribbon: Thin render strip used for the laser core and glow.
  Render resource factory: Renderer capability borrowed only while creating
    laser-owned shader resources.
  Snapshot: Compact replay record of visible launcher feedback.
  Shader handle: Runtime id that resolves to renderer-owned shader state.

Invariants:
  - LauncherLaser owns only transient render feedback.
  - Replay snapshots must preserve enough state to restore visible shots.

Related:
  - SkullbonezSource/Runtime/Editor/LauncherLaser.cpp
  - SkullbonezSource/Runtime/Editor/LauncherTools.cpp
*/
#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

#include "../../Maths/Matrix4.h"
#include "../../Maths/Vector3.h"

namespace SkullbonezCore
{
namespace Rendering
{
class IRenderResourceFactory;
class IShader;
} // namespace Rendering
namespace Assets
{
class AssetSystem;
} // namespace Assets

namespace Basics
{
struct LauncherLaserShotSnapshot
{
    Math::Vector::Vector3 start = Math::Vector::ZERO_VECTOR;
    Math::Vector::Vector3 end = Math::Vector::ZERO_VECTOR;
    Math::Vector::Vector3 cameraRight = Math::Vector::Vector3( 1.0f, 0.0f, 0.0f );
    Math::Vector::Vector3 cameraUp = Math::Vector::Vector3( 0.0f, 1.0f, 0.0f );
    float ageSeconds = 0.0f;
    float lifetimeSeconds = 0.18f;
    bool active = false;
    bool hit = false;
};

class LauncherLaser
{
  private:
    struct Shot
    {
        Math::Vector::Vector3 start = Math::Vector::ZERO_VECTOR;
        Math::Vector::Vector3 end = Math::Vector::ZERO_VECTOR;
        Math::Vector::Vector3 cameraRight = Math::Vector::Vector3( 1.0f, 0.0f, 0.0f );
        Math::Vector::Vector3 cameraUp = Math::Vector::Vector3( 0.0f, 1.0f, 0.0f );
        float ageSeconds = 0.0f;
        float lifetimeSeconds = 0.18f;
        bool active = false;
        bool hit = false;
    };

    static constexpr std::size_t MAX_SHOTS = 32;
    static constexpr int MAX_VERTICES = static_cast<int>( MAX_SHOTS * 96 );

    std::array<Shot, MAX_SHOTS> m_shots = {};
    int m_nextShot = 0;
    std::vector<float> m_vertices;
    std::unique_ptr<Rendering::IShader> m_shader;
    uint32_t m_dynamicVB = 0;

    void EnsureResources( Assets::AssetSystem& assets, Rendering::IRenderResourceFactory& renderResources );
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
    void EmitBillboardQuad( const Math::Vector::Vector3& center,
                            const Math::Vector::Vector3& right,
                            const Math::Vector::Vector3& up,
                            float halfWidth,
                            float halfHeight,
                            float r,
                            float g,
                            float bl,
                            float alpha );
    void EmitShot( const Shot& shot );

  public:
    LauncherLaser();
    ~LauncherLaser();

    void ResetResources();
    void Clear();
    void Fire( const Math::Vector::Vector3& rayOrigin,
               const Math::Vector::Vector3& rayDirection,
               const Math::Vector::Vector3& cameraUp,
               float distance,
               bool hit );
    void Update( float dt );
    bool HasActiveShots() const;
    void CaptureShots( std::vector<LauncherLaserShotSnapshot>& outShots, int& outNextShot ) const;
    void RestoreShots( const std::vector<LauncherLaserShotSnapshot>& shots, int nextShot );
    void Render( const Math::Transformation::Matrix4& viewProjection,
                 const Math::Vector::Vector3& cameraEye,
                 const Math::Vector::Vector3& cameraUp,
                 Assets::AssetSystem& assets,
                 Rendering::IRenderResourceFactory& renderResources );
};
} // namespace Basics
} // namespace SkullbonezCore
