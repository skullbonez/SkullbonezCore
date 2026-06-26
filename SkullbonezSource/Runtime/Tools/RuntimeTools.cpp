/*
File: SkullbonezSource/Runtime/Tools/RuntimeTools.cpp
Purpose:
  Provides the runtime tool state ownership boundary.
*/
#include "RuntimeTools.h"

#include "../../GameObjects/GameModel.h"
#include "../../Physics/CollisionShape.h"
#include "../../World/Terrain.h"

#include <algorithm>
#include <cmath>

namespace SkullbonezCore::Basics
{
namespace
{
float LauncherModelRadius( const GameObjects::GameModel& model )
{
    return (std::max)( Math::CollisionDetection::GetShapeBoundingRadius( model.GetCollisionShape() ), 1.0f );
}

bool IntersectRaySphere( const Math::Vector::Vector3& rayOrigin,
                         const Math::Vector::Vector3& rayDirection,
                         const Math::Vector::Vector3& center,
                         float radius,
                         float& outT )
{
    const Math::Vector::Vector3 m = rayOrigin - center;
    const float b = m * rayDirection;
    const float c = ( m * m ) - radius * radius;
    if ( c > 0.0f && b > 0.0f )
    {
        return false;
    }

    const float discriminant = b * b - c;
    if ( discriminant < 0.0f )
    {
        return false;
    }

    outT = -b - sqrtf( discriminant );
    if ( outT < 0.0f )
    {
        outT = 0.0f;
    }
    return true;
}
} // namespace

RunRayCastTestState& RuntimeTools::RayCastTest()
{
    return m_rayCastTest;
}

const RunRayCastTestState& RuntimeTools::RayCastTest() const
{
    return m_rayCastTest;
}

void RuntimeTools::ClearRayCastTestLines()
{
    m_rayCastTest.lines = {};
    m_rayCastTest.nextLine = 0;
}

void RuntimeTools::AddRayCastTestLine( const Math::Vector::Vector3& start, const Math::Vector::Vector3& end, bool hit )
{
    if ( !m_rayCastTest.visualizeRays )
    {
        return;
    }

    RunRayCastTestLine& line =
        m_rayCastTest.lines[static_cast<std::size_t>( m_rayCastTest.nextLine ) % RunRayCastTestState::MAX_LINES];
    line.start = start;
    line.end = end;
    line.ageSeconds = 0.0f;
    line.active = true;
    line.hit = hit;
    m_rayCastTest.nextLine = ( m_rayCastTest.nextLine + 1 ) % static_cast<int>( RunRayCastTestState::MAX_LINES );
}

void RuntimeTools::TickRayCastTestLines( float dt )
{
    if ( dt <= 0.0f )
    {
        return;
    }

    for ( RunRayCastTestLine& line : m_rayCastTest.lines )
    {
        if ( line.active )
        {
            line.ageSeconds += dt;
        }
    }
}

bool RuntimeTools::TryRayCastTestHit( const std::vector<GameObjects::GameModel>& models,
                                      const Math::Vector::Vector3& rayOrigin,
                                      const Math::Vector::Vector3& rayDirection,
                                      float maxDistance,
                                      int& outIndex,
                                      float& outT ) const
{
    outIndex = -1;
    outT = maxDistance;

    for ( int i = 0; i < static_cast<int>( models.size() ); ++i )
    {
        const GameObjects::GameModel& model = models[static_cast<size_t>( i )];
        const float radius = LauncherModelRadius( model );
        float rayT = 0.0f;
        if ( IntersectRaySphere( rayOrigin, rayDirection, model.GetPosition(), radius, rayT ) && rayT <= maxDistance &&
             rayT < outT )
        {
            outIndex = i;
            outT = rayT;
        }
    }

    return outIndex >= 0;
}

bool RuntimeTools::TryLauncherTerrainHit( Geometry::Terrain* terrain,
                                          const Math::Vector::Vector3& rayOrigin,
                                          const Math::Vector::Vector3& rayDirection,
                                          float maxDistance,
                                          float& outT ) const
{
    outT = maxDistance;
    if ( !terrain )
    {
        return false;
    }

    constexpr int RAY_STEPS = 192;
    bool hasPrevious = false;
    float previousT = 0.0f;
    float previousDiff = 0.0f;

    for ( int step = 0; step <= RAY_STEPS; ++step )
    {
        const float t = maxDistance * static_cast<float>( step ) / static_cast<float>( RAY_STEPS );
        const Math::Vector::Vector3 sample = rayOrigin + rayDirection * t;
        if ( !terrain->IsInBounds( sample.x, sample.z ) )
        {
            continue;
        }

        const float terrainY = terrain->GetTerrainHeightAt( sample.x, sample.z );
        const float diff = sample.y - terrainY;
        if ( fabsf( diff ) <= 0.01f )
        {
            outT = t;
            return true;
        }

        if ( hasPrevious && previousDiff > 0.0f && diff <= 0.0f )
        {
            float lowT = previousT;
            float highT = t;
            for ( int refine = 0; refine < 12; ++refine )
            {
                const float midT = ( lowT + highT ) * 0.5f;
                const Math::Vector::Vector3 mid = rayOrigin + rayDirection * midT;
                if ( !terrain->IsInBounds( mid.x, mid.z ) )
                {
                    lowT = midT;
                    continue;
                }
                const float midTerrainY = terrain->GetTerrainHeightAt( mid.x, mid.z );
                const float midDiff = mid.y - midTerrainY;
                if ( midDiff > 0.0f )
                {
                    lowT = midT;
                }
                else
                {
                    highT = midT;
                }
            }
            outT = highT;
            return true;
        }

        hasPrevious = true;
        previousT = t;
        previousDiff = diff;
    }

    return false;
}

LauncherLaser& RuntimeTools::Laser()
{
    return m_laser;
}

const LauncherLaser& RuntimeTools::Laser() const
{
    return m_laser;
}

RunMousePickupState& RuntimeTools::MousePickup()
{
    return m_mousePickup;
}

const RunMousePickupState& RuntimeTools::MousePickup() const
{
    return m_mousePickup;
}

RunEditorPlacementState& RuntimeTools::Editor()
{
    return m_editor;
}

const RunEditorPlacementState& RuntimeTools::Editor() const
{
    return m_editor;
}

RunEditorTracer& RuntimeTools::EditorTracer()
{
    return m_editorTracer;
}

const RunEditorTracer& RuntimeTools::EditorTracer() const
{
    return m_editorTracer;
}
} // namespace SkullbonezCore::Basics
