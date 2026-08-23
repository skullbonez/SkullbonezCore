/*
File: SkullbonezSource/Runtime/Tools/LauncherLaser.cpp
Purpose:
  Updates launcher-mode laser shots as short-lived presentation facts.

Summary:
  Each shot is a fixed world-space segment. Rendering billboards a wide outer
  ribbon and a narrow hot core toward the current camera so the feedback stays
  visible even when fired straight out of the crosshair.

Glossary:
  Afterimage: Fading visual trail that remains briefly after the shot.

Invariants:
  - Laser shots are visual feedback only; physics impulses happen elsewhere.
  - Expired shots must stop drawing without changing launcher hit history.

Related:
  - SkullbonezSource/Runtime/Tools/LauncherLaser.h
  - SkullbonezSource/Runtime/Render/RuntimeRenderPasses.cpp
  - Agentic/Reference/engine-glossary.md
*/
#include "LauncherLaser.h"

#include <algorithm>
#include <cmath>

using namespace SkullbonezCore::Runtime;
using namespace SkullbonezCore::Math::Vector;

namespace
{
constexpr float LASER_LIFETIME_SECONDS = 0.34f;
constexpr float LASER_EMITTER_LEAD = 3.0f;
constexpr float LASER_EMITTER_DOWN_OFFSET = 1.25f;
constexpr float LASER_MIN_SEGMENT_LENGTH = 0.25f;

Vector3 NormalizeOr( const Vector3& value, const Vector3& fallback )
{
    const float lenSq = VectorMagSquared( value );

    if ( lenSq <= TOLERANCE * TOLERANCE )
    {
        return fallback;
    }

    return value * ( 1.0f / sqrtf( lenSq ) );
}
} // namespace

void LauncherLaser::Reset()
{
    m_shots = {};
    m_nextShot = 0;
}

void LauncherLaser::Fire( const Vector3& rayOrigin, const Vector3& rayDirection, const Vector3& cameraUp, float distance,
                          bool hit )
{
    const Vector3 forward = NormalizeOr( rayDirection, Vector3( 0.0f, 0.0f, 1.0f ) );
    const Vector3 up = NormalizeOr( cameraUp, Vector3( 0.0f, 1.0f, 0.0f ) );
    Vector3 right = CrossProduct( forward, up );

    if ( VectorMagSquared( right ) <= TOLERANCE * TOLERANCE )
    {
        right = CrossProduct( forward, Vector3( 0.0f, 0.0f, 1.0f ) );
    }

    right = NormalizeOr( right, Vector3( 1.0f, 0.0f, 0.0f ) );
    const Vector3 stableUp = NormalizeOr( CrossProduct( right, forward ), up );
    const float visualDistance = (std::max)( distance, LASER_MIN_SEGMENT_LENGTH );
    const float startLead = (std::min)( LASER_EMITTER_LEAD, visualDistance * 0.35f );
    const float downOffset = (std::min)( LASER_EMITTER_DOWN_OFFSET, startLead * 0.55f );

    Shot& shot = m_shots[static_cast<std::size_t>( m_nextShot ) % MAX_SHOTS];
    shot.start = rayOrigin + forward * startLead - up * downOffset;
    shot.end = rayOrigin + forward * visualDistance;
    shot.cameraRight = right;
    shot.cameraUp = stableUp;
    shot.ageSeconds = 0.0f;
    shot.lifetimeSeconds = LASER_LIFETIME_SECONDS;
    shot.active = true;
    shot.hit = hit;
    m_nextShot = ( m_nextShot + 1 ) % static_cast<int>( MAX_SHOTS );
}

void LauncherLaser::Update( float dt )
{
    if ( dt <= 0.0f )
    {
        return;
    }

    for ( Shot& shot : m_shots )
    {
        if ( !shot.active )
        {
            continue;
        }

        shot.ageSeconds += dt;

        if ( shot.ageSeconds >= shot.lifetimeSeconds )
        {
            shot.active = false;
        }
    }
}


bool LauncherLaser::HasActiveShots() const
{
    for ( const Shot& shot : m_shots )
    {
        if ( shot.active )
        {
            return true;
        }
    }

    return false;
}

std::span<const LauncherLaserShotSnapshot> LauncherLaser::PresentationShots() const
{
    return m_shots;
}


void LauncherLaser::CaptureShots( std::vector<LauncherLaserShotSnapshot>& outShots, int& outNextShot ) const
{
    outShots.clear();
    outShots.reserve( MAX_SHOTS );

    outShots.assign( m_shots.begin(), m_shots.end() );

    outNextShot = m_nextShot;
}


void LauncherLaser::RestoreShots( const std::vector<LauncherLaserShotSnapshot>& shots, int nextShot )
{
    m_shots = {};
    const std::size_t copyCount = (std::min)( shots.size(), MAX_SHOTS );

    for ( std::size_t i = 0; i < copyCount; ++i )
    {
        m_shots[i] = shots[i];
    }

    m_nextShot = nextShot % static_cast<int>( MAX_SHOTS );

    if ( m_nextShot < 0 )
    {
        m_nextShot += static_cast<int>( MAX_SHOTS );
    }
}
