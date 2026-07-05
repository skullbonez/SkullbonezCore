/*
File: SkullbonezSource/GameObjects/GameModel.cpp
Purpose:
  Defines one renderable and optionally simulated object in the scene.

Mental model:
  Runtime code connects authored scene data, input, simulation, render
  backends, and validation-oriented launch modes. Follow who owns state and
  when that state changes.

Glossary:
  Buoyancy: Upward force from displaced water, applied through the center of
  buoyancy instead of the model origin.
  Center of buoyancy: World-space average location of displaced water. It is the
  lever arm that lets water roll a hull toward a stable pose.
  Wet sample: Fixed interior sample point used to estimate local water exposure
  without storing per-frame dynamic data.
  Righting torque: Corrective spin produced by buoyancy so long bodies settle on
  a side and broad flat bodies settle like rafts.
  Audio contact highlight: Short render-only flash that marks objects that
  actually emitted contact audio.
  OBB (Oriented Bounding Box): Box with rotation, used for exact object-space
  collision tests.
  CCD (Continuous Collision Detection): Swept collision test that asks whether
  objects hit during a tick, not only where they end the tick.
  Physics material: Per-object friction and drag coefficients consumed by the
    body integrator, collision shape, and fluid-force cache.
  Body simulation limit: Scalar cap enforced by the body before solver rows see
    velocity state.
  Contact policy: Geometry thresholds that decide when terrain is close enough
    to count as contact and when bounce response may be applied.
  Validation gate: Repository script that proves a class of changes before
  commit or PR.

Invariants:
  - GameModel owns per-object physics/render data, but multi-body response is
    finalized by PhysicsWorld and the persistent contact solver.
  - Collision-shape scalar caches must be refreshed whenever the authoritative
    shape changes.

Related:
  - SkullbonezSource/GameObjects/GameModel.h
  - Agentic/Reference/runtime-reference.md
  - Agentic/Reference/comment-style-guide.md
*/
#include "GameModel.h"
#include "../Physics/CollisionShape.h"
#include "../World/TerrainSupportClassifier.h"
#include "../Core/Profiler.h"
#include <algorithm>
#include <cmath>
#include <type_traits>


using namespace SkullbonezCore::GameObjects;
using namespace SkullbonezCore::Math;
using namespace SkullbonezCore::Environment;
using namespace SkullbonezCore::Geometry;
using namespace SkullbonezCore::Math::CollisionDetection;
using namespace SkullbonezCore::Math::Orientation;
using namespace SkullbonezCore::Math::Transformation;
using namespace SkullbonezCore::Math::Vector;
using namespace SkullbonezCore::Physics;
namespace Rendering = SkullbonezCore::Rendering;

namespace
{
float HighlightAlpha( float seconds, float fadeSeconds )
{
    if ( fadeSeconds <= 0.0f )
    {
        return 0.0f;
    }
    return std::clamp( seconds / fadeSeconds, 0.0f, 1.0f );
}

void TickHighlightSeconds( float& seconds, float dt )
{
    if ( seconds <= 0.0f || dt <= 0.0f )
    {
        return;
    }

    seconds = (std::max)( 0.0f, seconds - dt );
}
} // namespace

SkullbonezCore::Physics::PhysicsMaterial
SkullbonezCore::Physics::PhysicsMaterial::FromConfig( const SkullbonezCore::Basics::EngineConfig& config )
{
    PhysicsMaterial material;
    material.frictionCoefficient = config.frictionCoeff;
    material.sphereDragCoefficient = config.sphereDragCoeff;
    return material;
}

SkullbonezCore::Physics::BodySimulationLimits
SkullbonezCore::Physics::BodySimulationLimits::FromConfig( const SkullbonezCore::Basics::EngineConfig& config )
{
    BodySimulationLimits limits;
    limits.angularVelocityLimit = config.velocityLimit;
    return limits;
}

SkullbonezCore::Physics::ContactPolicy
SkullbonezCore::Physics::ContactPolicy::FromConfig( const SkullbonezCore::Basics::EngineConfig& config )
{
    ContactPolicy policy;
    policy.contactEpsilon = config.contactEpsilon;
    policy.terrainContactThreshold = config.terrainContactThreshold;
    policy.restitutionThreshold = config.contactRestitutionThreshold;
    return policy;
}

GameModel::GameModel( WorldEnvironment* pWorldEnv,
                      const Vector3& vPosition,
                      const Vector3& vRotationalInertia,
                      float fMass )
{
    if ( !pWorldEnv )
    {
        throw std::runtime_error( "Invalid world environment pointer supplied.  (GameModel::GameModel)" );
    }

    m_worldEnvironment = pWorldEnv;
    m_physicsInfo.SetPosition( vPosition );
    m_physicsInfo.SetRotationalInertia( vRotationalInertia );
    m_physicsInfo.SetMass( fMass );
    m_physicsInfo.SetFrictionCoefficient( m_physicsMaterial.frictionCoefficient );

    // Immutable body properties are read repeatedly in broadphase/narrowphase and
    // terrain response. Cache them once at construction to keep hot loops on plain
    // scalar loads instead of repeated getter/ratio work.
    m_ballPhysics.mass = fMass;
    m_ballPhysics.invMass = 1.0f / fMass;
    m_ballPhysics.rotationalInertia = vRotationalInertia;
    m_ballPhysics.invRotationalInertia =
        Vector3( 1.0f / vRotationalInertia.x, 1.0f / vRotationalInertia.y, 1.0f / vRotationalInertia.z );
    m_ballPhysics.radius = 0.0f;
    m_ballPhysics.radiusSq = 0.0f;
    m_ballPhysics.volume = 0.0f;
    m_ballPhysics.invVolume = 0.0f;
    m_ballPhysics.projectedSurfaceArea = 0.0f;
    m_ballPhysics.dragCoefficient = 0.0f;

    m_terrain = 0;

    m_projectedSurfaceArea = 0.0f;
    m_dragCoefficient = 0.0f;
    m_fixedContactHighlightSeconds = 0.0f;
    m_audioContactHighlightSeconds = 0.0f;
    m_renderTintR = 1.0f;
    m_renderTintG = 1.0f;
    m_renderTintB = 1.0f;
    m_renderColorOverride = 0.0f;
    m_renderMaterial = Rendering::MakeRenderMaterialFromLegacyTint( m_renderTintR,
                                                                    m_renderTintG,
                                                                    m_renderTintB,
                                                                    m_renderColorOverride );
    SetContactMaterial( "default" );
    m_isFixed = false;
    m_releasesFromFixedOnContact = false;
    m_contactReleaseImpulseThreshold = 1.0f;
    m_name[0] = '\0';
}


void GameModel::BuildSpherePhysicsCache( float radius )
{
    // These values never change after the shape is chosen, so cache them once.
    // The fixed-step physics loop reads radius, volume, and drag constantly; a
    // simple scalar load is cheaper and easier to reason about than recomputing
    // the same formulas in every force/collision path.
    if ( radius <= 0.0f )
    {
        throw std::runtime_error(
            "Bounding sphere radius must be greater than zero.  (GameModel::BuildSpherePhysicsCache)" );
    }

    m_ballPhysics.radius = radius;
    m_ballPhysics.radiusSq = radius * radius;
    m_ballPhysics.volume = FOUR_OVER_THREE * _PI * m_ballPhysics.radiusSq * radius;
    m_ballPhysics.invVolume = 1.0f / m_ballPhysics.volume;
    m_ballPhysics.projectedSurfaceArea = _PI * m_ballPhysics.radiusSq;
    m_ballPhysics.dragCoefficient = m_physicsMaterial.sphereDragCoefficient;
}


const BoundingSphere& GameModel::GetBoundingSphere() const
{
    return std::get<BoundingSphere>( m_boundingVolume );
}


BoundingSphere& GameModel::GetBoundingSphere()
{
    return std::get<BoundingSphere>( m_boundingVolume );
}


bool GameModel::IsSphere() const
{
    return std::holds_alternative<BoundingSphere>( m_boundingVolume );
}


bool GameModel::IsBox() const
{
    return std::holds_alternative<BoundingBox>( m_boundingVolume );
}


bool GameModel::IsConvexHull() const
{
    return std::holds_alternative<ConvexHullShape>( m_boundingVolume );
}


bool GameModel::UsesWorldInertia() const
{
    return !IsSphere();
}


const char* GameModel::GetShapeName() const
{
    if ( IsBox() )
    {
        return "box";
    }
    if ( IsConvexHull() )
    {
        return "convex_hull";
    }
    return "sphere";
}


void GameModel::SetFixed( bool isFixed )
{
    m_isFixed = isFixed;
    if ( m_isFixed )
    {
        // Fixed bodies are immovable collision participants. They can be hit and
        // shown in debug visuals, but they do not accumulate forces or velocity.
        m_physicsInfo.SetLinearVelocity( Vector::ZERO_VECTOR );
        m_physicsInfo.SetAngularVelocity( Vector::ZERO_VECTOR );
    }
}


bool GameModel::IsFixed() const
{
    return m_isFixed;
}


void GameModel::SetContactReleaseOnImpact( bool enabled, float impulseThreshold )
{
    m_releasesFromFixedOnContact = enabled;
    m_contactReleaseImpulseThreshold = (std::max)( 0.0f, impulseThreshold );
}


bool GameModel::ReleasesFromFixedOnContact() const
{
    return m_releasesFromFixedOnContact;
}


float GameModel::GetContactReleaseImpulseThreshold() const
{
    return m_contactReleaseImpulseThreshold;
}


void GameModel::NotifyFixedContact( float highlightSeconds )
{
    if ( highlightSeconds > m_fixedContactHighlightSeconds )
    {
        m_fixedContactHighlightSeconds = highlightSeconds;
    }
}


void GameModel::NotifyAudioContact( float highlightSeconds )
{
    if ( highlightSeconds > m_audioContactHighlightSeconds )
    {
        m_audioContactHighlightSeconds = highlightSeconds;
    }
}


void GameModel::TickFixedContactHighlight( float dt )
{
    TickHighlightSeconds( m_fixedContactHighlightSeconds, dt );
    TickHighlightSeconds( m_audioContactHighlightSeconds, dt );
}


float GameModel::GetFixedContactHighlightAlpha() const
{
    static constexpr float FADE_SECONDS = 0.5f;
    return HighlightAlpha( m_fixedContactHighlightSeconds, FADE_SECONDS );
}


float GameModel::GetAudioContactHighlightAlpha() const
{
    static constexpr float FADE_SECONDS = 0.1f;
    return HighlightAlpha( m_audioContactHighlightSeconds, FADE_SECONDS );
}


float GameModel::GetFixedContactHighlightSeconds() const
{
    return m_fixedContactHighlightSeconds;
}


void GameModel::SetFixedContactHighlightSeconds( float seconds )
{
    m_fixedContactHighlightSeconds = (std::max)( 0.0f, seconds );
}


void GameModel::SetCoefficientRestitution( float fCoefficientRestitution )
{
    m_physicsInfo.SetCoefficientRestitution( fCoefficientRestitution );
}


void GameModel::SetName( const char* name )
{
    strncpy_s( m_name, sizeof( m_name ), name, _TRUNCATE );
}


const char* GameModel::GetName() const
{
    return m_name;
}


void GameModel::SetRenderTint( float tintR, float tintG, float tintB, float colorOverride )
{
    m_renderTintR = tintR;
    m_renderTintG = tintG;
    m_renderTintB = tintB;
    m_renderColorOverride = colorOverride;
    m_renderMaterial = Rendering::MakeRenderMaterialFromLegacyTint( tintR, tintG, tintB, colorOverride );
}


void GameModel::GetRenderTint( float& tintR, float& tintG, float& tintB, float& colorOverride ) const
{
    tintR = m_renderTintR;
    tintG = m_renderTintG;
    tintB = m_renderTintB;
    colorOverride = m_renderColorOverride;
}


void GameModel::SetRenderMaterial( const Rendering::RenderMaterial& material )
{
    m_renderMaterial = material;
    m_renderTintR = material.baseColor[0];
    m_renderTintG = material.baseColor[1];
    m_renderTintB = material.baseColor[2];
    m_renderColorOverride = Rendering::RenderMaterialLegacyInstanceMode( material );
}


const Rendering::RenderMaterial& GameModel::GetRenderMaterial() const
{
    return m_renderMaterial;
}


void GameModel::SetContactMaterial( const char* materialName )
{
    const char* safeName = ( materialName && materialName[0] != '\0' ) ? materialName : "default";
    strncpy_s( m_contactMaterialName, sizeof( m_contactMaterialName ), safeName, _TRUNCATE );
    m_contactMaterialId = HashStr( m_contactMaterialName );
}


const char* GameModel::GetContactMaterialName() const
{
    return m_contactMaterialName;
}


uint32_t GameModel::GetContactMaterialId() const
{
    return m_contactMaterialId;
}


float GameModel::GetBoundingRadius()
{
    return m_ballPhysics.radius;
}


Vector3 GameModel::GetOrientationUp()
{
    // World-space up vector of the ball (local +Y after physics orientation).
    //
    // DERIVATION:
    //   GetModelMatrix() uses T * FromQuaternion(q) * Scale.
    //   The world-space up vector is col1 of FromQuaternion(q), which is:
    //     col1 = (xy2+wz2,  1-(xx2+zz2),  yz2-wx2)
    //          = (2(qx·qy + qw·qz),  1 - 2(qx² + qz²),  2(qy·qz - qw·qx))
    //
    //   This avoids building the full 4×4 matrix and extracting a column from it.

    float qx, qy, qz, qw;
    m_physicsInfo.GetOrientation().GetComponents( qx, qy, qz, qw );
    return Vector3( 2.0f * ( qx * qy + qw * qz ),        // col1[0] = xy2+wz2
                    1.0f - 2.0f * ( qx * qx + qz * qz ), // col1[1] = 1-(xx2+zz2)
                    2.0f * ( qy * qz - qw * qx ) );      // col1[2] = yz2-wx2
}


void GameModel::AddBoundingSphere( float fRadius )
{
    BuildSpherePhysicsCache( fRadius );
    m_boundingVolume = BoundingSphere( fRadius, Vector::ZERO_VECTOR, m_physicsMaterial.sphereDragCoefficient );
    UpdateModelInfo();
}


void GameModel::ApplyRuntimeConfig( const Basics::EngineConfig& config )
{
    ApplyPhysicsMaterial( PhysicsMaterial::FromConfig( config ) );
    ApplyBodySimulationLimits( BodySimulationLimits::FromConfig( config ) );
    ApplyContactPolicy( ContactPolicy::FromConfig( config ) );
}


void GameModel::ApplyPhysicsMaterial( const Physics::PhysicsMaterial& material )
{
    m_physicsMaterial = material;
    m_physicsInfo.SetFrictionCoefficient( m_physicsMaterial.frictionCoefficient );
    if ( BoundingSphere* sphere = std::get_if<BoundingSphere>( &m_boundingVolume ) )
    {
        sphere->SetDragCoefficient( m_physicsMaterial.sphereDragCoefficient );
        m_ballPhysics.dragCoefficient = m_physicsMaterial.sphereDragCoefficient;
        CalculateDragCoefficient();
    }
}


void GameModel::ApplyBodySimulationLimits( const Physics::BodySimulationLimits& limits )
{
    m_bodySimulationLimits = limits;
    m_physicsInfo.SetAngularVelocityLimit( m_bodySimulationLimits.angularVelocityLimit );
}


void GameModel::ApplyContactPolicy( const Physics::ContactPolicy& policy )
{
    m_contactPolicy = policy;
}


float GameModel::GetAngularVelocityLimit() const
{
    return m_bodySimulationLimits.angularVelocityLimit;
}


float GameModel::GetContactEpsilon() const
{
    return m_contactPolicy.contactEpsilon;
}


Terrain* GameModel::GetTerrain() const
{
    return m_terrain;
}


void GameModel::AddBoundingBox( const Vector3& halfExtents )
{
    if ( halfExtents.x <= 0.0f || halfExtents.y <= 0.0f || halfExtents.z <= 0.0f )
    {
        throw std::runtime_error(
            "Bounding box half-extents must all be greater than zero.  (GameModel::AddBoundingBox)" );
    }

    // Compute box inertia tensor: I_xx = m/3 * (hy² + hz²) for half-extents
    // Inertia is "rotational mass": a long box is harder to spin around some
    // axes than others, so boxes need three separate inertia values.
    float mass = m_physicsInfo.GetMass();
    float hx2 = halfExtents.x * halfExtents.x;
    float hy2 = halfExtents.y * halfExtents.y;
    float hz2 = halfExtents.z * halfExtents.z;
    float mOver3 = mass / 3.0f;
    Vector3 inertia( mOver3 * ( hy2 + hz2 ), mOver3 * ( hx2 + hz2 ), mOver3 * ( hx2 + hy2 ) );

    // Populate physics cache using bounding radius as "radius" (for broadphase)
    // Use the center-to-corner distance as the broadphase radius. That preserves
    // the cheap old sphere-style culling while exact box contact geometry remains
    // in the narrowphase manifold code.
    float boundRadius = sqrtf( hx2 + hy2 + hz2 );
    m_ballPhysics.radius = boundRadius;
    m_ballPhysics.radiusSq = boundRadius * boundRadius;
    m_ballPhysics.volume = 8.0f * halfExtents.x * halfExtents.y * halfExtents.z;
    m_ballPhysics.invVolume = 1.0f / m_ballPhysics.volume;
    m_ballPhysics.projectedSurfaceArea = ( 4.0f * halfExtents.x * halfExtents.y + 4.0f * halfExtents.x * halfExtents.z +
                                           4.0f * halfExtents.y * halfExtents.z ) /
                                         3.0f;
    m_ballPhysics.dragCoefficient = 1.05f;
    m_ballPhysics.mass = mass;
    m_ballPhysics.invMass = 1.0f / mass;
    m_ballPhysics.rotationalInertia = inertia;
    m_ballPhysics.invRotationalInertia = Vector3( 1.0f / inertia.x, 1.0f / inertia.y, 1.0f / inertia.z );

    m_physicsInfo.SetRotationalInertia( inertia );
    m_boundingVolume = BoundingBox( halfExtents, Vector::ZERO_VECTOR );
    UpdateModelInfo();
}


void GameModel::AddConvexHull( const ConvexHullShape& hull )
{
    const float radius = hull.GetBoundingRadius();
    if ( radius <= 0.0f )
    {
        throw std::runtime_error(
            "Convex hull bounding radius must be greater than zero.  (GameModel::AddConvexHull)" );
    }

    const float mass = m_physicsInfo.GetMass();
    const Vector3 inertia = hull.ComputeBoxApproxInertia( mass );
    m_ballPhysics.radius = radius;
    m_ballPhysics.radiusSq = radius * radius;
    m_ballPhysics.volume = hull.GetVolume();
    m_ballPhysics.invVolume = 1.0f / m_ballPhysics.volume;
    m_ballPhysics.projectedSurfaceArea = hull.GetProjectedSurfaceArea();
    m_ballPhysics.dragCoefficient = hull.GetDragCoefficient();
    m_ballPhysics.mass = mass;
    m_ballPhysics.invMass = 1.0f / mass;
    m_ballPhysics.rotationalInertia = inertia;
    m_ballPhysics.invRotationalInertia = Vector3( 1.0f / inertia.x, 1.0f / inertia.y, 1.0f / inertia.z );

    m_physicsInfo.SetRotationalInertia( inertia );
    m_boundingVolume = hull;
    UpdateModelInfo();
}


bool GameModel::ScaleCollisionShapeAxisFromBase( const CollisionShape& baseShape,
                                                 int axis,
                                                 float factor,
                                                 CollisionShape* outScaledShape )
{
    if ( axis < 0 || axis > 2 || !std::isfinite( factor ) || factor <= 0.0f )
    {
        return false;
    }

    factor = std::clamp( factor, 0.05f, 20.0f );
    const float mass = m_physicsInfo.GetMass();
    // Why: editor/replay scale commits need the exact variant that this cache
    // rebuild produced. Returning it here avoids a later GameModelCollection
    // reread just to reconstruct the same collider descriptor.

    if ( const BoundingSphere* sphere = std::get_if<BoundingSphere>( &baseShape ) )
    {
        const float radius = (std::max)( 0.25f, sphere->GetRadius() * factor );
        const float moment = 0.4f * mass * radius * radius;
        const Vector3 inertia( moment, moment, moment );
        const BoundingSphere scaledSphere( radius, sphere->GetPosition(), sphere->GetDragCoefficient() );
        m_boundingVolume = scaledSphere;
        if ( outScaledShape )
        {
            *outScaledShape = scaledSphere;
        }
        BuildSpherePhysicsCache( radius );
        m_ballPhysics.mass = mass;
        m_ballPhysics.invMass = 1.0f / mass;
        m_ballPhysics.rotationalInertia = inertia;
        m_ballPhysics.invRotationalInertia = Vector3( 1.0f / inertia.x, 1.0f / inertia.y, 1.0f / inertia.z );
        m_physicsInfo.SetRotationalInertia( inertia );
        UpdateModelInfo();
        return true;
    }

    if ( const BoundingBox* box = std::get_if<BoundingBox>( &baseShape ) )
    {
        Vector3 halfExtents = box->GetHalfExtents();
        if ( axis == 0 )
        {
            halfExtents.x = (std::max)( 0.25f, halfExtents.x * factor );
        }
        else if ( axis == 1 )
        {
            halfExtents.y = (std::max)( 0.25f, halfExtents.y * factor );
        }
        else
        {
            halfExtents.z = (std::max)( 0.25f, halfExtents.z * factor );
        }

        const float hx2 = halfExtents.x * halfExtents.x;
        const float hy2 = halfExtents.y * halfExtents.y;
        const float hz2 = halfExtents.z * halfExtents.z;
        const float mOver3 = mass / 3.0f;
        const Vector3 inertia( mOver3 * ( hy2 + hz2 ), mOver3 * ( hx2 + hz2 ), mOver3 * ( hx2 + hy2 ) );
        const float boundRadius = sqrtf( hx2 + hy2 + hz2 );
        m_ballPhysics.radius = boundRadius;
        m_ballPhysics.radiusSq = boundRadius * boundRadius;
        m_ballPhysics.volume = 8.0f * halfExtents.x * halfExtents.y * halfExtents.z;
        m_ballPhysics.invVolume = 1.0f / m_ballPhysics.volume;
        m_ballPhysics.projectedSurfaceArea =
            ( 4.0f * halfExtents.x * halfExtents.y + 4.0f * halfExtents.x * halfExtents.z +
              4.0f * halfExtents.y * halfExtents.z ) /
            3.0f;
        m_ballPhysics.dragCoefficient = 1.05f;
        m_ballPhysics.mass = mass;
        m_ballPhysics.invMass = 1.0f / mass;
        m_ballPhysics.rotationalInertia = inertia;
        m_ballPhysics.invRotationalInertia = Vector3( 1.0f / inertia.x, 1.0f / inertia.y, 1.0f / inertia.z );
        m_physicsInfo.SetRotationalInertia( inertia );
        const BoundingBox scaledBox( halfExtents, box->GetPosition() );
        m_boundingVolume = scaledBox;
        if ( outScaledShape )
        {
            *outScaledShape = scaledBox;
        }
        UpdateModelInfo();
        return true;
    }

    if ( const ConvexHullShape* hullBase = std::get_if<ConvexHullShape>( &baseShape ) )
    {
        ConvexHullShape hull = *hullBase;
        hull.ScaleAxis( axis, factor );
        const float radius = hull.GetBoundingRadius();
        if ( radius <= TOLERANCE )
        {
            return false;
        }

        const Vector3 inertia = hull.ComputeBoxApproxInertia( mass );
        m_ballPhysics.radius = radius;
        m_ballPhysics.radiusSq = radius * radius;
        m_ballPhysics.volume = hull.GetVolume();
        m_ballPhysics.invVolume = 1.0f / m_ballPhysics.volume;
        m_ballPhysics.projectedSurfaceArea = hull.GetProjectedSurfaceArea();
        m_ballPhysics.dragCoefficient = hull.GetDragCoefficient();
        m_ballPhysics.mass = mass;
        m_ballPhysics.invMass = 1.0f / mass;
        m_ballPhysics.rotationalInertia = inertia;
        m_ballPhysics.invRotationalInertia = Vector3( 1.0f / inertia.x, 1.0f / inertia.y, 1.0f / inertia.z );
        m_physicsInfo.SetRotationalInertia( inertia );
        m_boundingVolume = hull;
        if ( outScaledShape )
        {
            *outScaledShape = hull;
        }
        UpdateModelInfo();
        return true;
    }

    return false;
}


float GameModel::GetDragCoefficient()
{
    return m_dragCoefficient;
}


float GameModel::GetProjectedSurfaceArea()
{
    return m_projectedSurfaceArea;
}


const Vector3& GameModel::GetVelocity()
{
    return m_physicsInfo.GetVelocity();
}


const Vector3& GameModel::GetVelocity() const
{
    return m_physicsInfo.GetVelocity();
}


void GameModel::UpdateModelInfo()
{
    CalculateVolume();
    CalculateDragCoefficient();
    CalculateProjectedSurfaceArea();
}


Matrix4 GameModel::GetModelMatrix()
{
    // Natural model transform: T(worldPos) * FromQuaternion(q) * Scale(size).
    // Sphere mesh local frame is pre-rotated at build time, so no runtime visual
    // yaw compatibility shim is required.
    Matrix4 rotation = Matrix4::FromQuaternion( m_physicsInfo.GetOrientation() );
    Vector3 pos = m_physicsInfo.GetPosition();
    return std::visit( [&]( auto& shape ) { return shape.GetModelMatrix( pos, rotation ); }, m_boundingVolume );
}


void GameModel::CalculateProjectedSurfaceArea()
{
    m_projectedSurfaceArea = m_ballPhysics.projectedSurfaceArea;
}


void GameModel::CalculateDragCoefficient()
{
    m_dragCoefficient = m_ballPhysics.dragCoefficient;
}


float GameModel::GetVolume()
{
    return m_ballPhysics.volume;
}


void GameModel::CalculateVolume()
{
    m_physicsInfo.SetVolume( m_ballPhysics.volume );
}


float GameModel::GetMass() const
{
    return m_ballPhysics.mass;
}


float GameModel::GetInvertedMass() const
{
    if ( m_isFixed )
    {
        return 0.0f;
    }
    return m_ballPhysics.invMass;
}


const Vector3& GameModel::GetAngularVelocity()
{
    return m_physicsInfo.GetAngularVelocity();
}


const Vector3& GameModel::GetAngularVelocity() const
{
    return m_physicsInfo.GetAngularVelocity();
}


void GameModel::SetTerrain( Terrain* pTerrain )
{
    m_terrain = pTerrain;
}


const Vector3& GameModel::GetPosition()
{
    return m_physicsInfo.GetPosition();
}


const Vector3& GameModel::GetPosition() const
{
    // ENGINE-SPECIFIC:
    //   Const access lets the narrowphase manifold builder inspect immutable
    //   GameModels without opening write access to physics state.
    return m_physicsInfo.GetPosition();
}


float GameModel::GetSubmergedVolumePercent()
{
    return CalculateBuoyancySample().submergedVolumePercent;
}

// Concept: buoyancy samples are deliberately approximate and allocation-free.
//
// Spheres use an analytic submerged-volume formula because their orientation is
// irrelevant. Boxes and convex hulls use a fixed 3x3x3 set of interior points.
// That is coarse, but it gives the water a stable side-to-side lever arm for
// logs, planks, and rafts without transforming every hull vertex every frame.
// Physics validation treats the exact sample count and ordering as byte-exact
// behavior, so changes here require a baseline refresh from the final build.
GameModel::BuoyancySample GameModel::CalculateBuoyancySample()
{
    const float fluidSurfaceHeight = m_worldEnvironment->GetFluidSurfaceHeight();
    const Vector3 bodyPosition = m_physicsInfo.GetPosition();
    Transformation::RotationMatrix rotMat = m_physicsInfo.GetOrientationMatrix();
    BuoyancySample sample;
    sample.centerOfBuoyancy = bodyPosition;

    auto addWetPoint = [&sample]( const Vector3& point, float weight, Vector3& weightedSum, float& wetWeight )
    {
        // Wetness is a normalized exposure, not mass or volume. The total is
        // converted to a 0..1 submerged fraction after all fixed samples run.
        weight = std::clamp( weight, 0.0f, 1.0f );
        if ( weight <= TOLERANCE )
        {
            return;
        }

        weightedSum += point * weight;
        wetWeight += weight;
        // Invariant: the current sampler has 27 points. MAX_WET_POINTS leaves a
        // little room for future shape samplers while keeping the frame hot path
        // on stack/inline storage instead of heap vectors or hash maps.
        if ( sample.wetPointCount < BuoyancySample::MAX_WET_POINTS )
        {
            sample.wetPoints[sample.wetPointCount] = point;
            sample.wetWeights[sample.wetPointCount] = weight;
            ++sample.wetPointCount;
        }
    };

    auto finishWeightedSample = [&sample]( const Vector3& weightedSum, float wetWeight, const Vector3& fallback )
    {
        sample.wetWeightTotal = wetWeight;
        if ( wetWeight <= TOLERANCE )
        {
            sample.centerOfBuoyancy = fallback;
            sample.submergedVolumePercent = 0.0f;
            return;
        }

        sample.centerOfBuoyancy = weightedSum / wetWeight;
        sample.submergedVolumePercent = std::clamp( wetWeight / 27.0f, 0.0f, 1.0f );
    };

    auto terrainWaterScale = [&]( const Vector3& worldPoint, float sampleBand ) -> float
    {
        // Why: a point cannot displace water that is physically blocked by land.
        // This is the shoreline/jetty case: the wet end of a log can float, but
        // the end resting on a ramp should not receive fake lift from "water"
        // inside the terrain.
        if ( !m_terrain || !m_terrain->IsInBounds( worldPoint.x, worldPoint.z ) )
        {
            return 1.0f;
        }

        const float terrainHeight = m_terrain->GetTerrainHeightAt( worldPoint.x, worldPoint.z );
        if ( terrainHeight >= fluidSurfaceHeight - TOLERANCE )
        {
            return 0.0f;
        }

        const float clearanceAboveTerrain = worldPoint.y - terrainHeight;
        if ( clearanceAboveTerrain <= TOLERANCE )
        {
            return 0.0f;
        }

        return std::clamp( clearanceAboveTerrain / (std::max)( sampleBand, TOLERANCE ), 0.0f, 1.0f );
    };

    auto sampleOrientedBoxVolume = [&]( const Vector3& localCenter, const Vector3& halfExtents )
    {
        // Concept: treat boxes and hulls as a cheap oriented volume for water.
        //
        // For boxes the extents are exact. For convex hulls the inertia extents
        // preserve the authored body's long/flat character without walking every
        // hull face or rebuilding a clipped water polygon during gameplay.
        const Vector3 center = bodyPosition + ( rotMat * localCenter );
        const float verticalExtent = rotMat.SupportExtentY( halfExtents );
        sample.centerOfBuoyancy = center;

        if ( verticalExtent <= TOLERANCE || fluidSurfaceHeight <= center.y - verticalExtent )
        {
            return;
        }

        static constexpr float SAMPLE_COORDS[3] = { -0.6666667f, 0.0f, 0.6666667f };
        // The band softens waterline crossing so a sample near the surface fades
        // in instead of snapping on/off, which would show up as jitter in logs.
        const float sampleBand = (std::max)( 0.25f, verticalExtent * 0.5f );
        Vector3 weightedSum = Vector::ZERO_VECTOR;
        float wetWeight = 0.0f;
        for ( float sx : SAMPLE_COORDS )
        {
            for ( float sy : SAMPLE_COORDS )
            {
                for ( float sz : SAMPLE_COORDS )
                {
                    const Vector3 local =
                        localCenter + Vector3( halfExtents.x * sx, halfExtents.y * sy, halfExtents.z * sz );
                    const Vector3 worldPoint = bodyPosition + ( rotMat * local );
                    const float depth = fluidSurfaceHeight - worldPoint.y;
                    // Fully submerged volumes get exact sample weight 1.0. Near
                    // the surface, depth is mapped through sampleBand so the
                    // center of buoyancy can move smoothly across the body.
                    const float waterWetness = fluidSurfaceHeight >= center.y + verticalExtent
                                                   ? 1.0f
                                                   : std::clamp( 0.5f + depth / sampleBand, 0.0f, 1.0f );
                    const float wetness = waterWetness * terrainWaterScale( worldPoint, sampleBand );
                    addWetPoint( worldPoint, wetness, weightedSum, wetWeight );
                }
            }
        }

        finishWeightedSample( weightedSum, wetWeight, center );
    };

    std::visit(
        [&]( const auto& shape )
        {
            using ShapeT = std::decay_t<decltype( shape )>;

            if constexpr ( std::is_same_v<ShapeT, BoundingSphere> )
            {
                // Spheres have no preferred orientation, so only submerged
                // volume matters. Do not add wet points here: the shared water
                // damping in WorldEnvironment already slows linear motion, and
                // point damping on a sphere made balls stick to water like glue.
                const Vector3 center = bodyPosition + ( rotMat * shape.GetPosition() );
                sample.centerOfBuoyancy = center;
                const float radius = shape.GetRadius();
                const float fluidHeightRelativeToCenter = fluidSurfaceHeight - center.y;

                if ( fluidHeightRelativeToCenter <= -radius )
                {
                    sample.submergedVolumePercent = 0.0f;
                    return;
                }
                if ( fluidHeightRelativeToCenter >= radius )
                {
                    sample.submergedVolumePercent = 1.0f;
                    return;
                }

                const float yValue = fluidHeightRelativeToCenter + radius;
                sample.submergedVolumePercent = std::clamp(
                    ( ONE_OVER_THREE * _PI * ( ( 3.0f * radius ) - yValue ) * yValue * yValue ) / shape.GetVolume(),
                    0.0f,
                    1.0f );
            }
            else if constexpr ( std::is_same_v<ShapeT, BoundingBox> )
            {
                sampleOrientedBoxVolume( shape.GetPosition(), shape.GetHalfExtents() );
            }
            else
            {
                // Convex hull buoyancy uses the inertia volume as its runtime
                // proxy. The real hull still owns collision; this proxy only
                // decides how water applies lift and damping.
                sampleOrientedBoxVolume( shape.GetPosition(), shape.GetInertiaHalfExtents() );
            }
        },
        m_boundingVolume );

    return sample;
}


// Concept: righting torque chooses which local axis should point upward.
//
// A floating sphere has no preferred "up", but an anisotropic object does. The
// stable axis is selected from the thinnest local dimensions: a log's thin axes
// make it roll onto its side, while a flat plank/raft prefers its thin thickness
// axis upward. The torque is intentionally approximate and damped by submersion
// because this runs every physics tick.
Vector3 GameModel::CalculateBuoyancyRightingTorque( float buoyancyForce, float submergedVolumePercent )
{
    if ( m_isFixed || IsSphere() || buoyancyForce <= TOLERANCE || submergedVolumePercent <= TOLERANCE )
    {
        return Vector::ZERO_VECTOR;
    }

    const Vector3& inertia = m_ballPhysics.rotationalInertia;
    float maxInertia = (std::max)( inertia.x, (std::max)( inertia.y, inertia.z ) );
    float minInertia = inertia.y;
    minInertia = (std::min)( minInertia, inertia.x );
    minInertia = (std::min)( minInertia, inertia.z );
    if ( maxInertia <= TOLERANCE )
    {
        return Vector::ZERO_VECTOR;
    }

    const float anisotropy = ( maxInertia - minInertia ) / maxInertia;
    if ( anisotropy < 0.08f )
    {
        // Nearly isotropic objects should not be forced into an arbitrary pose.
        // This keeps rounded hulls and almost-cubes from behaving like planks.
        return Vector::ZERO_VECTOR;
    }

    Transformation::RotationMatrix rotMat = m_physicsInfo.GetOrientationMatrix();
    Vector3 stableHalfExtents( 1.0f, 1.0f, 1.0f );
    bool hasStableHalfExtents = false;
    std::visit(
        [&]( const auto& shape )
        {
            using ShapeT = std::decay_t<decltype( shape )>;
            if constexpr ( std::is_same_v<ShapeT, BoundingBox> )
            {
                stableHalfExtents = shape.GetHalfExtents();
                hasStableHalfExtents = true;
            }
            else if constexpr ( std::is_same_v<ShapeT, ConvexHullShape> )
            {
                stableHalfExtents = shape.GetInertiaHalfExtents();
                hasStableHalfExtents = true;
            }
            else
            {
                (void)shape;
            }
        },
        m_boundingVolume );
    if ( !hasStableHalfExtents )
    {
        return Vector::ZERO_VECTOR;
    }

    const float minThickness =
        (std::min)( stableHalfExtents.x, (std::min)( stableHalfExtents.y, stableHalfExtents.z ) );
    const float maxThickness =
        (std::max)( stableHalfExtents.x, (std::max)( stableHalfExtents.y, stableHalfExtents.z ) );
    if ( minThickness <= TOLERANCE || maxThickness <= TOLERANCE )
    {
        return Vector::ZERO_VECTOR;
    }

    auto terrainSupportFactor = [&]() -> float
    {
        // Why: terrain support should reduce water's authority to roll a body.
        // A half-grounded log or jetty beam is partly constrained by the ramp; if
        // water still applied full righting torque, the dry end would pivot and
        // bounce instead of sliding or settling against the terrain.
        if ( !m_terrain )
        {
            return 0.0f;
        }

        int closeSamples = 0;
        int terrainSamples = 0;
        const Vector3 position = m_physicsInfo.GetPosition();
        const float supportGap = m_contactPolicy.contactEpsilon + Physics::BOX_TERRAIN_VERTEX_SUPPORT_SLACK;
        std::visit(
            [&]( const auto& shape )
            {
                using ShapeT = std::decay_t<decltype( shape )>;
                if constexpr ( std::is_same_v<ShapeT, BoundingSphere> )
                {
                    (void)shape;
                }
                else if constexpr ( std::is_same_v<ShapeT, BoundingBox> )
                {
                    const Vector3& he = shape.GetHalfExtents();
                    for ( int corner = 0; corner < 8; ++corner )
                    {
                        const Vector3 local = shape.GetPosition() + Physics::GetBoxTerrainLocalCorner( he, corner );
                        const Vector3 worldPoint = position + ( rotMat * local );
                        if ( !m_terrain->IsInBounds( worldPoint.x, worldPoint.z ) )
                        {
                            continue;
                        }

                        ++terrainSamples;
                        const float terrainHeight = m_terrain->GetTerrainHeightAt( worldPoint.x, worldPoint.z );
                        if ( worldPoint.y - terrainHeight <= supportGap )
                        {
                            ++closeSamples;
                        }
                    }
                }
                else
                {
                    const Vector3 hullCenter = position + ( rotMat * shape.GetPosition() );
                    const uint16_t vertexCount = shape.GetVertexCount();
                    for ( uint16_t v = 0; v < vertexCount; ++v )
                    {
                        const Vector3 worldPoint = hullCenter + ( rotMat * shape.GetVertex( v ) );
                        if ( !m_terrain->IsInBounds( worldPoint.x, worldPoint.z ) )
                        {
                            continue;
                        }

                        ++terrainSamples;
                        const float terrainHeight = m_terrain->GetTerrainHeightAt( worldPoint.x, worldPoint.z );
                        if ( worldPoint.y - terrainHeight <= supportGap )
                        {
                            ++closeSamples;
                        }
                    }
                }
            },
            m_boundingVolume );

        if ( terrainSamples <= 0 || closeSamples <= 0 )
        {
            return 0.0f;
        }
        return std::clamp( static_cast<float>( closeSamples ) / 3.0f, 0.0f, 1.0f );
    };

    Vector3 stableWorldAxis = Vector::ZERO_VECTOR;
    float bestAxisScore = -1.0f;
    const Vector3 localAxes[3] = {
        Vector3( 1.0f, 0.0f, 0.0f ),
        Vector3( 0.0f, 1.0f, 0.0f ),
        Vector3( 0.0f, 0.0f, 1.0f ),
    };
    const float localInertia[3] = { inertia.x, inertia.y, inertia.z };
    const float localThickness[3] = { stableHalfExtents.x, stableHalfExtents.y, stableHalfExtents.z };
    for ( int axisIndex = 0; axisIndex < 3; ++axisIndex )
    {
        const float candidateInertia = localInertia[axisIndex];
        const float candidateThickness = localThickness[axisIndex];
        // Only axes from the thinnest dimension family are eligible. The 20%
        // tolerance lets slightly imperfect authored hulls behave like their
        // intended primitive instead of flipping between almost-equal axes.
        if ( candidateThickness > minThickness * 1.20f )
        {
            continue;
        }

        Vector3 candidateWorldAxis = rotMat * localAxes[axisIndex];
        const float axisLengthSq = Vector::VectorMagSquared( candidateWorldAxis );
        if ( axisLengthSq <= TOLERANCE * TOLERANCE )
        {
            continue;
        }

        candidateWorldAxis /= sqrtf( axisLengthSq );
        if ( candidateWorldAxis.y < 0.0f )
        {
            candidateWorldAxis = -candidateWorldAxis;
        }

        const float verticalAlignment = std::clamp( candidateWorldAxis.y, 0.0f, 1.0f );
        const float thicknessPreference = minThickness / (std::max)( candidateThickness, TOLERANCE );
        const float inertiaPreference = candidateInertia / maxInertia;
        // Score mostly follows the axis already closest to world up so righting
        // is stable. Thickness and inertia are small tie-breakers that preserve
        // the object's authored long/flat character.
        const float score = verticalAlignment + thicknessPreference * 0.20f + inertiaPreference * 0.03f;
        if ( score > bestAxisScore )
        {
            bestAxisScore = score;
            stableWorldAxis = candidateWorldAxis;
        }
    }

    if ( bestAxisScore < 0.0f )
    {
        return Vector::ZERO_VECTOR;
    }

    const Vector3 worldUp( 0.0f, 1.0f, 0.0f );
    Vector3 correctionAxis = Vector::CrossProduct( stableWorldAxis, worldUp );
    const float errorSq = Vector::VectorMagSquared( correctionAxis );
    if ( errorSq <= TOLERANCE * TOLERANCE )
    {
        return Vector::ZERO_VECTOR;
    }

    const float error = sqrtf( errorSq );
    correctionAxis /= error;

    const float gravityMagnitude = fabsf( m_worldEnvironment->GetGravity() );
    const float weight = m_ballPhysics.mass * gravityMagnitude;
    const float cappedLift = (std::min)( buoyancyForce, weight * 6.0f );
    const float waterCoupling = sqrtf( std::clamp( submergedVolumePercent, 0.0f, 1.0f ) );
    // Terrain contact never disables buoyancy, it only limits the righting
    // torque. Linear lift and drag still act on the wet end, so a shoreline log
    // can slide into the water instead of being locked in place.
    const float supportBlend = 1.0f - terrainSupportFactor() * 0.85f;
    const float torqueMagnitude = cappedLift * m_ballPhysics.radius * anisotropy * waterCoupling * supportBlend * error;
    return correctionAxis * torqueMagnitude;
}


const Quaternion& GameModel::GetOrientation() const
{
    return m_physicsInfo.GetOrientation();
}


const Vector3& GameModel::GetRotationalInertia() const
{
    return m_ballPhysics.rotationalInertia;
}


const Vector3& GameModel::GetInvertedRotationalInertia() const
{
    if ( m_isFixed )
    {
        return Vector::ZERO_VECTOR;
    }
    return m_ballPhysics.invRotationalInertia;
}


const CollisionShape& GameModel::GetCollisionShape() const
{
    // ENGINE-SPECIFIC:
    //   The manifold builder dispatches on the variant shape type. Returning the
    //   CollisionShape by const reference keeps that dispatch explicit and avoids
    //   reintroducing broadphase-radius guesses into narrowphase code.
    return m_boundingVolume;
}


float GameModel::GetCoefficientRestitution() const
{
    return m_physicsInfo.GetCoefficientRestitution();
}


float GameModel::GetFrictionCoefficient() const
{
    return m_physicsInfo.GetFrictionCoefficient();
}


void GameModel::SetLinearVelocity( const Vector3& v )
{
    if ( m_isFixed )
    {
        m_physicsInfo.SetLinearVelocity( Vector::ZERO_VECTOR );
        return;
    }
    m_physicsInfo.SetLinearVelocity( v );
}


void GameModel::SetAngularVelocity( const Vector3& v )
{
    if ( m_isFixed )
    {
        m_physicsInfo.SetAngularVelocity( Vector::ZERO_VECTOR );
        return;
    }
    m_physicsInfo.SetAngularVelocity( v );
}


void GameModel::SetPosition( const Vector3& pos )
{
    m_physicsInfo.SetPosition( pos );
}


void GameModel::SetOrientation( const Quaternion& q )
{
    m_physicsInfo.SetOrientation( q );
}
