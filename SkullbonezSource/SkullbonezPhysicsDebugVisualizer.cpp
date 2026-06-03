#include "SkullbonezPhysicsDebugVisualizer.h"

#include <algorithm>
#include <cmath>
#include "SkullbonezCollisionShape.h"
#include "SkullbonezGameModel.h"
#include "SkullbonezGameModelCollection.h"
#include "SkullbonezIRenderBackend.h"
#include "SkullbonezQuaternion.h"

using namespace SkullbonezCore::GameObjects;
using namespace SkullbonezCore::Math::CollisionDetection;
using namespace SkullbonezCore::Math::Orientation;
using namespace SkullbonezCore::Math::Transformation;
using namespace SkullbonezCore::Math::Vector;
using namespace SkullbonezCore::Physics;
using namespace SkullbonezCore::Rendering;

namespace
{
float ShapeAxisLength( GameModel& model, int axis )
{
    const CollisionShape& shape = model.GetCollisionShape();
    if ( const BoundingBox* box = std::get_if<BoundingBox>( &shape ) )
    {
        const Vector3& he = box->GetHalfExtents();
        float extent = axis == 0 ? he.x : ( axis == 1 ? he.y : he.z );
        return (std::max)( 1.0f, extent * 1.35f );
    }
    return (std::max)( 1.0f, model.GetBoundingRadius() * 1.35f );
}
} // namespace

PhysicsDebugVisualizer::TrackedContact* PhysicsDebugVisualizer::FindTrackedContact( const PhysicsDebugContact& contact )
{
    for ( TrackedContact& tracked : m_trackedContacts )
    {
        if ( tracked.contact.bodyA == contact.bodyA &&
             tracked.contact.bodyB == contact.bodyB &&
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

    Vector3 side = fabsf( dir.y ) < 0.8f ? CrossProduct( dir, Vector3( 0.0f, 1.0f, 0.0f ) ) : CrossProduct( dir, Vector3( 1.0f, 0.0f, 0.0f ) );
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

void PhysicsDebugVisualizer::EmitRingXZ( const Vector3& center, float radius, float yOffset, float r, float g, float bl )
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

void PhysicsDebugVisualizer::EmitObjectAxes( GameModelCollection& models )
{
    int count = models.GetModelCount();
    for ( int i = 0; i < count; ++i )
    {
        GameModel& model = models.GetModelAtIndex( i );
        Vector3 center = model.GetPosition();
        Quaternion orientation = model.GetOrientation();
        RotationMatrix rot = orientation.GetOrientationMatrix();
        Vector3 axes[3] = {
            rot * Vector3( 1.0f, 0.0f, 0.0f ),
            rot * Vector3( 0.0f, 1.0f, 0.0f ),
            rot * Vector3( 0.0f, 0.0f, 1.0f ),
        };

        EmitArrow( center, center + axes[0] * ShapeAxisLength( model, 0 ), 1.0f, 0.05f, 0.04f );
        EmitArrow( center, center + axes[1] * ShapeAxisLength( model, 1 ), 0.05f, 0.9f, 0.12f );
        EmitArrow( center, center + axes[2] * ShapeAxisLength( model, 2 ), 0.08f, 0.35f, 1.0f );
    }
}

void PhysicsDebugVisualizer::EmitContacts( GameModelCollection& models )
{
    for ( const TrackedContact& tracked : m_trackedContacts )
    {
        const PhysicsDebugContact& contact = tracked.contact;
        const float fade = ContactFade( tracked );
        float size = 0.35f + (std::min)( contact.penetration, 2.0f ) * 0.25f;
        float normalLen = 2.5f + (std::min)( contact.penetration, 4.0f ) * 0.8f + (std::min)( contact.normalImpulse, 8.0f ) * 0.08f;
        EmitCross( contact.point, size, 1.0f * fade, 0.95f * fade, 0.15f * fade );
        EmitArrow( contact.point, contact.point + contact.normal * normalLen, 0.0f, 0.9f * fade, 1.0f * fade );
        EmitLine( contact.point, contact.point + contact.tangent1 * 1.25f, 1.0f * fade, 0.45f * fade, 0.05f * fade );
        EmitLine( contact.point, contact.point + contact.tangent2 * 1.25f, 1.0f * fade, 0.45f * fade, 0.05f * fade );
        if ( contact.bodyA >= 0 && contact.bodyB >= 0 && contact.bodyA < models.GetModelCount() && contact.bodyB < models.GetModelCount() )
        {
            Vector3 a = models.GetModelAtIndex( contact.bodyA ).GetPosition();
            Vector3 b = models.GetModelAtIndex( contact.bodyB ).GetPosition();
            EmitLine( a, b, 0.45f * fade, 0.45f * fade, 0.45f * fade );
        }
    }
}

void PhysicsDebugVisualizer::EmitSleepState( GameModelCollection& models )
{
    const std::vector<uint8_t>& sleepStates = models.GetSleepStates();
    const std::vector<uint8_t>& supportedStates = models.GetSleepSupportedStates();
    const std::vector<uint8_t>& inhibitedStates = models.GetSleepInhibitedStates();
    int count = models.GetModelCount();
    for ( int i = 0; i < count; ++i )
    {
        GameModel& model = models.GetModelAtIndex( i );
        Vector3 center = model.GetPosition();
        float radius = (std::max)( 1.0f, model.GetBoundingRadius() * 1.15f );
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
            EmitLine( center + Vector3( 0.0f, radius * 0.5f, 0.0f ), center + Vector3( 0.0f, radius * 1.35f, 0.0f ), 1.0f, 0.25f, 0.05f );
        }
    }
}

void PhysicsDebugVisualizer::SetContactLingerSeconds( float seconds )
{
    m_contactLingerSeconds = (std::max)( 0.0f, (std::min)( seconds, 5.0f ) );
}

void PhysicsDebugVisualizer::Update( float dt, GameModelCollection& models )
{
    if ( ( m_flags & PHYSICS_DEBUG_CONTACTS ) == 0 )
    {
        m_trackedContacts.clear();
        return;
    }

    const std::vector<PhysicsDebugContact>& contacts = models.GetPhysicsDebugContacts();
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
                        []( const TrackedContact& tracked )
                        {
                            return tracked.remainingSeconds <= 0.0f;
                        } ),
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

void PhysicsDebugVisualizer::Render( GameModelCollection& models, const Matrix4& viewProj )
{
    if ( m_flags == PHYSICS_DEBUG_NONE || models.GetModelCount() <= 0 )
    {
        return;
    }

    m_lineData.clear();
    if ( ( m_flags & PHYSICS_DEBUG_AXES ) != 0 )
    {
        EmitObjectAxes( models );
    }
    if ( ( m_flags & PHYSICS_DEBUG_CONTACTS ) != 0 )
    {
        EmitContacts( models );
    }
    if ( ( m_flags & PHYSICS_DEBUG_SLEEP ) != 0 )
    {
        EmitSleepState( models );
    }

    if ( !m_lineData.empty() )
    {
        Gfx().DrawLinesColored( m_lineData.data(), static_cast<int>( m_lineData.size() / 6 ), viewProj.Data() );
    }
}
