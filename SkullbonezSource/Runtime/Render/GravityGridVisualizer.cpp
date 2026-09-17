#include "GravityGridVisualizer.h"
#include "../../Physics/PhysicsBodyStore.h"
#include "../../Rendering/RenderInstanceStore.h"
#include "../../Physics/PhysicsWorldForces.h"
#include "../../Core/FatalError.h"
#include <algorithm>
#include <cmath>
#include <cfloat>

namespace SkullbonezCore::Runtime
{
using Math::Vector::Vector3;
void GravityGridVisualizer::Reset()
{
    m_fitted = false;
    m_lineCount = 0;
    m_sourceCount = 0;
    m_firstSourceId = 0;
    m_minimumHeight = 0.0f;
}

void GravityGridVisualizer::Fit( const Physics::PhysicsBodyStore& bodies, const Physics::MutualGravitySettings& gravity )
{
    const auto hot = bodies.HotFields();
    Vector3 low( FLT_MAX, FLT_MAX, FLT_MAX );
    Vector3 high( -FLT_MAX, -FLT_MAX, -FLT_MAX );
    double totalMass = 0.0;
    double weightedY = 0.0;
    for ( int i = 0; i < bodies.Count(); ++i )
    {
        const float mass = bodies.Records()[i].mass;
        if ( mass <= 0.0f )
        {
            continue;
        }
        const Vector3 p = Physics::PhysicsBodyPosition( hot, i );
        low.x = (std::min)( low.x, p.x );
        low.z = (std::min)( low.z, p.z );
        high.x = (std::max)( high.x, p.x );
        high.z = (std::max)( high.z, p.z );
        totalMass += mass;
        weightedY += mass * static_cast<double>( p.y );
    }
    if ( totalMass <= 0.0 )
    {
        return;
    }
    m_extent = (std::max)( 16.0f, (std::max)( high.x - low.x, high.z - low.z ) * 0.8f );
    m_center = Vector3( ( low.x + high.x ) * 0.5f, static_cast<float>( weightedY / totalMass ) - m_extent * 0.08f, ( low.z + high.z ) * 0.5f );
    m_referencePotential = (std::max)( 0.000001f, static_cast<float>( gravity.gravitationalConstant * totalMass / m_extent ) );
    // Keep the plane and display scale fixed for the scene. Moving bodies must
    // deform the surface, not pump the grid bounds or renormalize every well.
    m_fitted = true;
}

void GravityGridVisualizer::BuildSurface( const Physics::MutualGravitySettings& gravity )
{
    const float softeningSquared = (std::max)( 0.000001f, gravity.softeningLength * gravity.softeningLength );
    m_minimumHeight = FLT_MAX;
    for ( int z = 0; z < POINTS; ++z )
    {
        for ( int x = 0; x < POINTS; ++x )
        {
            Vector3 p( m_center.x + m_extent * ( 2.0f * x / ( POINTS - 1 ) - 1.0f ), m_center.y, m_center.z + m_extent * ( 2.0f * z / ( POINTS - 1 ) - 1.0f ) );
            double potential = 0.0;
            for ( int body = 0; body < m_sourceCount; ++body )
            {
                const float mass = m_sources[body].mass;
                if ( mass <= 0.0f )
                {
                    continue;
                }
                const Vector3 delta = p - Vector3( m_sources[body].x, m_sources[body].y, m_sources[body].z );
                const float distanceSquared = delta.x * delta.x + delta.y * delta.y + delta.z * delta.z + softeningSquared;
                potential += gravity.gravitationalConstant * static_cast<double>( mass ) / std::sqrt( distanceSquared );
            }
            // This is an illustrative potential-height map, not physical space
            // curvature. A smooth bounded display keeps close wells finite without flat clamped floors.
            const float potentialHeight = static_cast<float>( std::log1p( potential / m_referencePotential ) );
            const float depth = 3.0f * potentialHeight / ( 3.0f + potentialHeight );
            p.y += m_style.height - m_extent * 0.32f * depth;
            m_points[z * POINTS + x] = p;
            m_minimumHeight = (std::min)( m_minimumHeight, p.y );
        }
    }
}

void GravityGridVisualizer::AppendLine( int first, int second, bool major )
{
    for ( const int index : { first, second } )
    {
        const Vector3& p = m_points[index];
        const float edge = (std::max)( std::abs( p.x - m_center.x ), std::abs( p.z - m_center.z ) ) / m_extent;
        const float fade = 0.12f + 0.88f * std::clamp( ( 1.0f - edge ) * 5.0f, 0.0f, 1.0f );
        const float brightness = major ? 1.0f : 0.52f;
        m_lines[m_lineCount++] = p.x;
        m_lines[m_lineCount++] = p.y;
        m_lines[m_lineCount++] = p.z;
        const Vector3 color = m_style.color == 1 ? Vector3( 1.0f, 0.44f, 0.08f ) : m_style.color == 2 ? Vector3( 0.62f, 0.65f, 0.68f ) : Vector3( 0.24f, 0.57f, 0.67f );
        m_lines[m_lineCount++] = color.x * brightness * fade;
        m_lines[m_lineCount++] = color.y * brightness * fade;
        m_lines[m_lineCount++] = color.z * brightness * fade;
    }
}

void GravityGridVisualizer::Update( const Physics::PhysicsBodyStore& bodies, const Physics::MutualGravitySettings& gravity, bool enabled )
{
    m_lineCount = 0;
    m_sourceCount = 0;
    m_firstSourceId = 0;
    m_minimumHeight = 0.0f;
    if ( !enabled || !gravity.enabled || gravity.gravitationalConstant <= 0.0f || bodies.Count() == 0 )
    {
        return;
    }
    if ( !m_fitted )
    {
        Fit( bodies, gravity );
    }
    if ( !m_fitted )
    {
        return;
    }
    m_style = {};
    for ( int i = 0; i < bodies.Count(); ++i )
    {
        const auto p = Physics::PhysicsBodyPosition( bodies.HotFields(), i );
        const float mass = bodies.Records()[i].mass;
        if ( mass > 0 )
        {
            m_sources[m_sourceCount++] = { p.x, p.y, p.z, mass };
        }
    }
    BuildSurface( gravity );
    BuildLines();
}

void GravityGridVisualizer::UpdatePresented( const Physics::PhysicsBodyStore& bodies,
                                             std::span<const Rendering::RenderInstanceRecord> instances,
                                             const Physics::MutualGravitySettings& gravity,
                                             const Scene::GravityFieldSettings& style )
{
    m_lineCount = 0;
    m_sourceCount = 0;
    m_firstSourceId = 0;
    m_minimumHeight = 0;
    if ( !gravity.enabled || gravity.gravitationalConstant <= 0 || bodies.Count() == 0 )
    {
        return;
    }
    if ( !m_fitted )
    {
        Fit( bodies, gravity );
    }
    if ( !m_fitted )
    {
        return;
    }
    m_style = style;
    // Invariant: App has already substituted the selected historical/future pose.
    // Join by stable identity; presentation must never feed back into Physics.
    if ( instances.size() > m_sources.size() )
    {
        SB_FATAL( "GravityGrid", "Presented body count exceeds scene capacity." );
    }
    for ( std::size_t row = 0; row < instances.size(); ++row )
    {
        const auto& instance = instances[row];
        const auto handle = bodies.HandleForSceneObjectId( instance.sceneObjectId, static_cast<int>( row ) );
        const auto* body = bodies.RecordForHandle( handle );
        if ( !body || body->mass <= 0 || !instance.editorVisible )
        {
            continue;
        }
        const auto& matrix = instance.modelMatrix;
        if ( m_sourceCount == 0 )
        {
            m_firstSourceId = instance.sceneObjectId.value;
        }
        m_sources[m_sourceCount++] = { matrix.m[12], matrix.m[13], matrix.m[14], body->mass };
    }
    if ( m_sourceCount == 0 )
    {
        return;
    }
    BuildSurface( gravity );
    BuildLines();
}

void GravityGridVisualizer::BuildLines()
{
    // All vertices and line storage belong to this fixed-capacity renderer
    // member; toggling or updating never requests gameplay allocation.
    for ( int z = 0; z < POINTS; ++z )
    {
        for ( int x = 0; x < POINTS; ++x )
        {
            const int index = z * POINTS + x;
            if ( x + 1 < POINTS )
            {
                AppendLine( index, index + 1, z % 4 == 0 );
            }
            if ( z + 1 < POINTS )
            {
                AppendLine( index, index + POINTS, x % 4 == 0 );
            }
        }
    }
}
} // namespace SkullbonezCore::Runtime
