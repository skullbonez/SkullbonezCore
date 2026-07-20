/*
File: SkullbonezSource/Runtime/Debug/PhysicsDebugVisualizer.cpp
Purpose:
  Draws physics contacts, axes, sleep state, and pipeline diagnostics.

Summary:
  PhysicsDebugVisualizer.cpp draws physics contacts, axes, sleep state, and
  pipeline diagnostics. As an implementation unit, keep edits anchored on
  deterministic physics, diagnostics, or world-state flow and on the
  glossary/invariants below.

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
  - SkullbonezSource/Runtime/Debug/PhysicsDebugVisualizer.h
  - SkullbonezSource/Physics/PhysicsDebugData.h
  - Agentic/Reference/physics-overview.md
  - Agentic/Reference/comment-style-guide.md
*/
#include "PhysicsDebugVisualizer.h"

#include <algorithm>
#include <cmath>
#include <variant>
#include "../../Physics/ColliderStore.h"
#include "../../Physics/CollisionShape.h"
#include "../../Physics/PhysicsBodyStore.h"
#include "../../Rendering/IRenderCommandContext.h"
#include "../../Maths/Quaternion.h"
#include "../../World/Terrain.h"

using namespace SkullbonezCore::Math::CollisionDetection;
using namespace SkullbonezCore::Math::Orientation;
using namespace SkullbonezCore::Math::Transformation;
using namespace SkullbonezCore::Math::Vector;
using namespace SkullbonezCore::Physics;
using namespace SkullbonezCore::Rendering;
using namespace SkullbonezCore::Geometry;

namespace
{
constexpr int PIPELINE_STAGE_COUNT = static_cast<int>( PhysicsPipelineStage::Count );
constexpr PassRasterStateBucket PHYSICS_DEBUG_LINE_RASTER =
    MakePassRasterStateBucket( 0, false, false, false, BlendFactor::One, BlendFactor::Zero, CullMode::None );

float ShapeAxisLength( const ColliderRecord& collider, int axis )
{
    // Scale local-axis arrows to the shape. Boxes use their true half-extent on
    // the selected axis; spheres use bounding radius for all axes.
    const CollisionShape& shape = collider.shape;
    if ( const BoundingBox* box = std::get_if<BoundingBox>( &shape ) )
    {
        const Vector3& he = box->GetHalfExtents();
        float extent = axis == 0 ? he.x : ( axis == 1 ? he.y : he.z );
        return (std::max)( 1.0f, extent * 1.35f );
    }
    return (std::max)( 1.0f, collider.boundingRadius * 1.35f );
}

void PipelineStageColor( PhysicsPipelineStage stage, float& r, float& g, float& b )
{
    // Stable colors make pipeline stepping readable across frames. These colors
    // are diagnostic labels only; they do not encode any solver math.
    switch ( stage )
    {
    case PhysicsPipelineStage::BroadphaseCandidate:
        r = 0.35f;
        g = 0.65f;
        b = 1.0f;
        break;
    case PhysicsPipelineStage::SleepPrunedPair:
        r = 0.35f;
        g = 0.28f;
        b = 0.8f;
        break;
    case PhysicsPipelineStage::WakeDecision:
        r = 0.25f;
        g = 1.0f;
        b = 0.55f;
        break;
    case PhysicsPipelineStage::SweptObjectHit:
        r = 1.0f;
        g = 0.78f;
        b = 0.16f;
        break;
    case PhysicsPipelineStage::SweptObjectMiss:
        r = 0.35f;
        g = 0.35f;
        b = 0.35f;
        break;
    case PhysicsPipelineStage::TerrainHit:
        r = 0.35f;
        g = 1.0f;
        b = 0.25f;
        break;
    case PhysicsPipelineStage::TerrainManifold:
        r = 0.25f;
        g = 0.95f;
        b = 0.70f;
        break;
    case PhysicsPipelineStage::TerrainRow:
        r = 0.12f;
        g = 0.72f;
        b = 1.0f;
        break;
    case PhysicsPipelineStage::ManifoldRow:
        r = 0.0f;
        g = 0.92f;
        b = 1.0f;
        break;
    case PhysicsPipelineStage::WarmStart:
        r = 1.0f;
        g = 0.45f;
        b = 0.10f;
        break;
    case PhysicsPipelineStage::SolverIteration:
        r = 1.0f;
        g = 0.95f;
        b = 0.12f;
        break;
    case PhysicsPipelineStage::VelocityWriteback:
        r = 0.50f;
        g = 0.88f;
        b = 1.0f;
        break;
    case PhysicsPipelineStage::PositionCorrection:
        r = 1.0f;
        g = 0.20f;
        b = 0.10f;
        break;
    case PhysicsPipelineStage::CacheStore:
        r = 0.90f;
        g = 0.58f;
        b = 1.0f;
        break;
    case PhysicsPipelineStage::SleepSupportEdge:
        r = 0.42f;
        g = 1.0f;
        b = 0.38f;
        break;
    case PhysicsPipelineStage::SleepIslandDecision:
        r = 0.70f;
        g = 0.50f;
        b = 1.0f;
        break;
    default:
        r = 1.0f;
        g = 1.0f;
        b = 1.0f;
        break;
    }
}
} // namespace

// Contact debug rows are produced by the solver only for the current physics
// step, but a one-frame manifold is too easy to miss visually.  The visualizer
// tracks contacts by body pair + feature id and lets them fade after the solver
// stops reporting them.  This is display state only; it never feeds back into
// physics, sleeping, or collision response.
PhysicsDebugVisualizer::TrackedContact* PhysicsDebugVisualizer::FindTrackedContact( const PhysicsDebugContact& contact )
{
    for ( TrackedContact& tracked : m_trackedContacts )
    {
        if ( tracked.contact.bodyA == contact.bodyA && tracked.contact.bodyB == contact.bodyB &&
             tracked.contact.featureId == contact.featureId )
        {
            return &tracked;
        }
    }
    return nullptr;
}

float PhysicsDebugVisualizer::ContactFade( const TrackedContact& contact ) const
{
    if ( contact.lifetimeSeconds <= TOLERANCE )
    {
        return 1.0f;
    }

    float fade = contact.remainingSeconds / contact.lifetimeSeconds;
    if ( fade < 0.0f )
    {
        return 0.0f;
    }
    if ( fade > 1.0f )
    {
        return 1.0f;
    }
    return fade;
}

void PhysicsDebugVisualizer::EmitLine( const Vector3& a, const Vector3& b, float r, float g, float bl )
{
    m_lineData.insert( m_lineData.end(), { a.x, a.y, a.z, r, g, bl, b.x, b.y, b.z, r, g, bl } );
}

void PhysicsDebugVisualizer::EmitCross( const Vector3& p, float size, float r, float g, float bl )
{
    EmitLine( Vector3( p.x - size, p.y, p.z ), Vector3( p.x + size, p.y, p.z ), r, g, bl );
    EmitLine( Vector3( p.x, p.y - size, p.z ), Vector3( p.x, p.y + size, p.z ), r, g, bl );
    EmitLine( Vector3( p.x, p.y, p.z - size ), Vector3( p.x, p.y, p.z + size ), r, g, bl );
}

void PhysicsDebugVisualizer::EmitArrow( const Vector3& a, const Vector3& b, float r, float g, float bl )
{
    EmitLine( a, b, r, g, bl );

    Vector3 dir = b - a;
    float len = VectorMag( dir );
    if ( len <= TOLERANCE )
    {
        return;
    }
    dir /= len;

    Vector3 side = fabsf( dir.y ) < 0.8f ? CrossProduct( dir, Vector3( 0.0f, 1.0f, 0.0f ) )
                                         : CrossProduct( dir, Vector3( 1.0f, 0.0f, 0.0f ) );
    float sideLen = VectorMag( side );
    if ( sideLen <= TOLERANCE )
    {
        return;
    }
    side /= sideLen;

    float head = (std::min)( len * 0.25f, 1.5f );
    Vector3 base = b - dir * head;
    EmitLine( b, base + side * ( head * 0.45f ), r, g, bl );
    EmitLine( b, base - side * ( head * 0.45f ), r, g, bl );
}

void PhysicsDebugVisualizer::EmitRingXZ( const Vector3& center,
                                         float radius,
                                         float yOffset,
                                         float r,
                                         float g,
                                         float bl )
{
    constexpr int segments = 24;
    float y = center.y + yOffset;
    Vector3 prev( center.x + radius, y, center.z );
    for ( int i = 1; i <= segments; ++i )
    {
        float t = static_cast<float>( i ) * ( 2.0f * _PI / static_cast<float>( segments ) );
        Vector3 next( center.x + cosf( t ) * radius, y, center.z + sinf( t ) * radius );
        EmitLine( prev, next, r, g, bl );
        prev = next;
    }
}

void PhysicsDebugVisualizer::EmitObjectAxes( const PhysicsDebugFrameView& view )
{
    const auto& bodies = view.bodies.Records();
    const auto hotFields = view.bodies.HotFields();
    const auto& colliders = view.colliders.Records();
    const int count =
        (std::min)( view.modelCount,
                    (std::min)( static_cast<int>( bodies.size() ), static_cast<int>( colliders.size() ) ) );
    for ( int i = 0; i < count; ++i )
    {
        const ColliderRecord& collider = colliders[static_cast<std::size_t>( i )];
        const std::size_t bodyIndex = static_cast<std::size_t>( i );
        Vector3 center = PhysicsBodyPosition( hotFields, bodyIndex );
        Quaternion orientation = PhysicsBodyOrientation( hotFields, bodyIndex );
        RotationMatrix rot = orientation.GetOrientationMatrix();
        Vector3 axes[3] = {
            rot * Vector3( 1.0f, 0.0f, 0.0f ),
            rot * Vector3( 0.0f, 1.0f, 0.0f ),
            rot * Vector3( 0.0f, 0.0f, 1.0f ),
        };

        EmitArrow( center, center + axes[0] * ShapeAxisLength( collider, 0 ), 1.0f, 0.05f, 0.04f );
        EmitArrow( center, center + axes[1] * ShapeAxisLength( collider, 1 ), 0.05f, 0.9f, 0.12f );
        EmitArrow( center, center + axes[2] * ShapeAxisLength( collider, 2 ), 0.08f, 0.35f, 1.0f );
    }
}

void PhysicsDebugVisualizer::EmitConvexHullWireframes( const PhysicsDebugFrameView& view )
{
    const auto& bodies = view.bodies.Records();
    const auto hotFields = view.bodies.HotFields();
    const auto& colliders = view.colliders.Records();
    const int count =
        (std::min)( view.modelCount,
                    (std::min)( static_cast<int>( bodies.size() ), static_cast<int>( colliders.size() ) ) );
    for ( int i = 0; i < count; ++i )
    {
        const ColliderRecord& collider = colliders[static_cast<std::size_t>( i )];
        const ConvexHullShape* hull = std::get_if<ConvexHullShape>( &collider.shape );
        if ( !hull )
        {
            continue;
        }

        const std::size_t bodyIndex = static_cast<std::size_t>( i );
        Quaternion orientation = PhysicsBodyOrientation( hotFields, bodyIndex );
        RotationMatrix rot = orientation.GetOrientationMatrix();
        const Vector3 center = PhysicsBodyPosition( hotFields, bodyIndex ) + rot * hull->GetPosition();
        for ( uint16_t edgeIndex = 0; edgeIndex < hull->GetEdgeCount(); ++edgeIndex )
        {
            const ConvexHullEdge& edge = hull->GetEdge( edgeIndex );
            const Vector3 a = center + rot * hull->GetVertex( edge.vertexA );
            const Vector3 b = center + rot * hull->GetVertex( edge.vertexB );
            EmitLine( a, b, 1.0f, 0.72f, 0.10f );
        }
    }
}

void PhysicsDebugVisualizer::EmitContacts( const PhysicsDebugFrameView& view )
{
    // Yellow cross = contact point. Cyan arrow = normal push direction. Orange
    // lines = the two sideways friction axes. A gray body-to-body line helps
    // locate which pair produced the row.
    for ( const TrackedContact& tracked : m_trackedContacts )
    {
        const PhysicsDebugContact& contact = tracked.contact;
        const float fade = ContactFade( tracked );
        float size = 0.35f + (std::min)( contact.penetration, 2.0f ) * 0.25f;
        float normalLen =
            2.5f + (std::min)( contact.penetration, 4.0f ) * 0.8f + (std::min)( contact.normalImpulse, 8.0f ) * 0.08f;
        EmitCross( contact.point, size, 1.0f * fade, 0.95f * fade, 0.15f * fade );
        EmitArrow( contact.point, contact.point + contact.normal * normalLen, 0.0f, 0.9f * fade, 1.0f * fade );
        EmitLine( contact.point, contact.point + contact.tangent1 * 1.25f, 1.0f * fade, 0.45f * fade, 0.05f * fade );
        EmitLine( contact.point, contact.point + contact.tangent2 * 1.25f, 1.0f * fade, 0.45f * fade, 0.05f * fade );
        const auto& bodies = view.bodies.Records();
        const auto hotFields = view.bodies.HotFields();
        if ( contact.bodyA >= 0 && contact.bodyB >= 0 && contact.bodyA < static_cast<int>( bodies.size() ) &&
             contact.bodyB < static_cast<int>( bodies.size() ) )
        {
            Vector3 a = PhysicsBodyPosition( hotFields, static_cast<std::size_t>( contact.bodyA ) );
            Vector3 b = PhysicsBodyPosition( hotFields, static_cast<std::size_t>( contact.bodyB ) );
            EmitLine( a, b, 0.45f * fade, 0.45f * fade, 0.45f * fade );
        }
    }
}

void PhysicsDebugVisualizer::EmitSleepState( const PhysicsDebugFrameView& view )
{
    // Purple marks sleeping bodies, green marks credible support, and orange
    // marks sleep inhibition. This helps distinguish "touching" from "allowed
    // to sleep," which are intentionally different policies.
    const auto sleepStates = view.sleepStates;
    const auto supportedStates = view.sleepSupportedStates;
    const auto inhibitedStates = view.sleepInhibitedStates;
    const auto& bodies = view.bodies.Records();
    const auto hotFields = view.bodies.HotFields();
    const auto& colliders = view.colliders.Records();
    const int count =
        (std::min)( view.modelCount,
                    (std::min)( static_cast<int>( bodies.size() ), static_cast<int>( colliders.size() ) ) );
    for ( int i = 0; i < count; ++i )
    {
        const ColliderRecord& collider = colliders[static_cast<std::size_t>( i )];
        Vector3 center = PhysicsBodyPosition( hotFields, static_cast<std::size_t>( i ) );
        float radius = (std::max)( 1.0f, collider.boundingRadius * 1.15f );
        bool sleeping = i < static_cast<int>( sleepStates.size() ) && sleepStates[i] != 0;
        bool supported = i < static_cast<int>( supportedStates.size() ) && supportedStates[i] != 0;
        bool inhibited = i < static_cast<int>( inhibitedStates.size() ) && inhibitedStates[i] != 0;

        if ( sleeping )
        {
            EmitRingXZ( center, radius, 0.15f, 0.45f, 0.25f, 1.0f );
            EmitCross( center, radius * 0.18f, 0.45f, 0.25f, 1.0f );
        }
        if ( supported )
        {
            EmitRingXZ( center, radius * 0.82f, -radius * 0.9f, 0.1f, 1.0f, 0.25f );
        }
        if ( inhibited )
        {
            EmitRingXZ( center, radius * 0.62f, radius * 0.9f, 1.0f, 0.25f, 0.05f );
            EmitLine( center + Vector3( 0.0f, radius * 0.5f, 0.0f ),
                      center + Vector3( 0.0f, radius * 1.35f, 0.0f ),
                      1.0f,
                      0.25f,
                      0.05f );
        }
    }
}

void PhysicsDebugVisualizer::EmitPipelineStage( const PhysicsDebugFrameView& view )
{
    const std::vector<PhysicsPipelineRecord>& records = view.pipelineTrace;
    if ( records.empty() || PIPELINE_STAGE_COUNT <= 0 )
    {
        return;
    }

    int stageIndex = m_pipelineStageCursor % PIPELINE_STAGE_COUNT;
    if ( stageIndex < 0 )
    {
        stageIndex += PIPELINE_STAGE_COUNT;
    }
    const PhysicsPipelineStage selectedStage = static_cast<PhysicsPipelineStage>( stageIndex );
    float r = 1.0f;
    float g = 1.0f;
    float b = 1.0f;
    PipelineStageColor( selectedStage, r, g, b );

    int emitted = 0;
    const auto& bodies = view.bodies.Records();
    const auto hotFields = view.bodies.HotFields();
    for ( const PhysicsPipelineRecord& record : records )
    {
        if ( record.stage != selectedStage )
        {
            continue;
        }

        const bool hasA = record.bodyA >= 0 && record.bodyA < static_cast<int>( bodies.size() );
        const bool hasB = record.bodyB >= 0 && record.bodyB < static_cast<int>( bodies.size() );
        if ( hasA && hasB )
        {
            Vector3 a = PhysicsBodyPosition( hotFields, static_cast<std::size_t>( record.bodyA ) );
            Vector3 bPos = PhysicsBodyPosition( hotFields, static_cast<std::size_t>( record.bodyB ) );
            EmitLine( a, bPos, r * 0.55f, g * 0.55f, b * 0.55f );
        }

        Vector3 p = record.point;
        if ( hasA && VectorMagSquared( p ) <= TOLERANCE * TOLERANCE )
        {
            p = PhysicsBodyPosition( hotFields, static_cast<std::size_t>( record.bodyA ) );
        }
        const float scale = 0.24f + (std::min)( fabsf( record.scalarA ), 4.0f ) * 0.05f;
        EmitCross( p, scale, r, g, b );

        if ( VectorMagSquared( record.normal ) > TOLERANCE * TOLERANCE )
        {
            float normalLen = 1.0f + (std::min)( fabsf( record.scalarB ), 6.0f ) * 0.08f;
            EmitArrow( p, p + record.normal * normalLen, r, g, b );
        }

        ++emitted;
        if ( emitted >= 512 )
        {
            break;
        }
    }
}

void PhysicsDebugVisualizer::EmitTerrainContactProbe( const PhysicsDebugFrameView& view, Geometry::Terrain* terrain )
{
    if ( !terrain )
    {
        return;
    }

    const auto& bodies = view.bodies.Records();
    const auto hotFields = view.bodies.HotFields();
    const auto& colliders = view.colliders.Records();
    const int count =
        (std::min)( view.modelCount,
                    (std::min)( static_cast<int>( bodies.size() ), static_cast<int>( colliders.size() ) ) );
    for ( int i = 0; i < count; ++i )
    {
        const ColliderRecord& collider = colliders[static_cast<std::size_t>( i )];
        if ( !std::holds_alternative<BoundingSphere>( collider.shape ) )
        {
            continue;
        }

        const Vector3 center = PhysicsBodyPosition( hotFields, static_cast<std::size_t>( i ) );
        if ( !terrain->IsInBounds( center.x, center.z ) )
        {
            continue;
        }

        float terrainHeight = 0.0f;
        Plane terrainPlane;
        terrain->GetTerrainHeightAndPlaneAt( center.x, center.z, terrainHeight, terrainPlane );
        if ( terrainPlane.m_normal.y < 0.0f )
        {
            terrainPlane.m_normal = terrainPlane.m_normal * -1.0f;
            terrainPlane.m_distance *= -1.0f;
        }

        const Triangle polygon = terrain->LocatePolygon( center.x, center.z );
        const float radius = (std::max)( 1.0f, collider.boundingRadius );
        const float surfaceLift = std::clamp( radius * 0.035f, 0.12f, 0.65f );
        const Vector3 lift = terrainPlane.m_normal * surfaceLift;
        const Vector3 a = polygon.v1 + lift;
        const Vector3 b = polygon.v2 + lift;
        const Vector3 c = polygon.v3 + lift;
        const Vector3 contact( center.x, terrainHeight, center.z );
        const Vector3 liftedContact = contact + lift;

        // Lime triangle = exact terrain polygon picked by LocatePolygon at the
        // sphere center X/Z. Magenta line = old vertical sphere-center contact
        // probe used by the terrain response path. Yellow cross = probe endpoint.
        EmitLine( a, b, 0.20f, 1.0f, 0.15f );
        EmitLine( b, c, 0.20f, 1.0f, 0.15f );
        EmitLine( c, a, 0.20f, 1.0f, 0.15f );
        EmitLine( center, contact, 1.0f, 0.10f, 0.90f );
        EmitCross( liftedContact, (std::min)( radius * 0.18f, 2.0f ), 1.0f, 0.95f, 0.05f );
    }
}

void PhysicsDebugVisualizer::SetContactLingerSeconds( float seconds )
{
    m_contactLingerSeconds = (std::max)( 0.0f, (std::min)( seconds, 5.0f ) );
}

void PhysicsDebugVisualizer::SetPipelineStageCursor( int cursor )
{
    m_pipelineStageCursor = cursor;
}

void PhysicsDebugVisualizer::Update( float dt, const PhysicsDebugFrameView& view )
{
    // The C-key mode is bitmask based: axes, contacts, and sleep state can be
    // shown independently or together.  If contacts are disabled, discard the
    // linger cache immediately so re-enabling starts from live solver rows.
    if ( ( m_flags & PHYSICS_DEBUG_CONTACTS ) == 0 )
    {
        m_trackedContacts.clear();
        return;
    }

    const std::vector<PhysicsDebugContact>& contacts = view.debugContacts;
    if ( m_contactLingerSeconds <= 0.0f )
    {
        m_trackedContacts.clear();
        m_trackedContacts.reserve( contacts.size() );
        for ( const PhysicsDebugContact& contact : contacts )
        {
            TrackedContact tracked;
            tracked.contact = contact;
            m_trackedContacts.push_back( tracked );
        }
        return;
    }

    for ( TrackedContact& tracked : m_trackedContacts )
    {
        tracked.remainingSeconds -= (std::max)( 0.0f, dt );
    }
    m_trackedContacts.erase(
        std::remove_if( m_trackedContacts.begin(),
                        m_trackedContacts.end(),
                        []( const TrackedContact& tracked ) { return tracked.remainingSeconds <= 0.0f; } ),
        m_trackedContacts.end() );

    for ( const PhysicsDebugContact& contact : contacts )
    {
        TrackedContact* tracked = FindTrackedContact( contact );
        if ( tracked )
        {
            tracked->contact = contact;
            tracked->remainingSeconds = m_contactLingerSeconds;
            tracked->lifetimeSeconds = m_contactLingerSeconds;
            continue;
        }

        TrackedContact newTracked;
        newTracked.contact = contact;
        newTracked.remainingSeconds = m_contactLingerSeconds;
        newTracked.lifetimeSeconds = m_contactLingerSeconds;
        m_trackedContacts.push_back( newTracked );
    }
}

void PhysicsDebugVisualizer::Render( const PhysicsDebugFrameView& view,
                                     const Matrix4& viewProj,
                                     IRenderCommandContext& renderCommands,
                                     bool supportsDebugLines,
                                     Geometry::Terrain* terrain )
{
    if ( m_flags == PHYSICS_DEBUG_NONE || view.modelCount <= 0 || !supportsDebugLines )
    {
        return;
    }

    m_lineData.clear();
    // Each enabled layer writes into one retained CPU line buffer, then uploads a
    // single dynamic vertex stream.  That keeps debug rendering cheap enough to
    // leave on while investigating solver state in large scenes.
    if ( ( m_flags & PHYSICS_DEBUG_AXES ) != 0 )
    {
        EmitObjectAxes( view );
        EmitConvexHullWireframes( view );
    }
    if ( ( m_flags & PHYSICS_DEBUG_CONTACTS ) != 0 )
    {
        EmitContacts( view );
    }
    if ( ( m_flags & PHYSICS_DEBUG_SLEEP ) != 0 )
    {
        EmitSleepState( view );
    }
    if ( ( m_flags & PHYSICS_DEBUG_PIPELINE ) != 0 )
    {
        EmitPipelineStage( view );
    }
    if ( ( m_flags & PHYSICS_DEBUG_TERRAIN_CONTACT ) != 0 )
    {
        EmitTerrainContactProbe( view, terrain );
    }

    if ( !m_lineData.empty() )
    {
        // Why: DebugOverlayPass resolves renderer readiness once per frame; this
        // visualizer only owns physics diagnostic geometry.
        renderCommands.DrawLinesColored( m_lineData, viewProj, PHYSICS_DEBUG_LINE_RASTER );
    }
}
