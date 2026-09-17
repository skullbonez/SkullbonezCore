// Render-owned potential surface. Fixed storage is rebuilt from read-only body values.
#pragma once
#include "../../Maths/Vector3.h"
#include "../../Core/SceneCapacity.h"
#include "../../Scene/GravityFieldSettings.h"
#include <array>
#include <span>
namespace SkullbonezCore::Physics
{
class PhysicsBodyStore;
struct MutualGravitySettings;
} // namespace SkullbonezCore::Physics
namespace SkullbonezCore::Rendering
{
struct RenderInstanceRecord;
class RenderInstanceStore;
} // namespace SkullbonezCore::Rendering
namespace SkullbonezCore::Runtime
{
class GravityGridVisualizer
{
  public:
    static constexpr int POINTS = 49;
    static constexpr int LINE_FLOATS = 2 * POINTS * ( POINTS - 1 ) * 12;
    void Reset();
    void Update( const Physics::PhysicsBodyStore& bodies, const Physics::MutualGravitySettings& gravity, bool enabled );
    void UpdatePresented( const Physics::PhysicsBodyStore& bodies,
                          std::span<const Rendering::RenderInstanceRecord> instances,
                          const Physics::MutualGravitySettings& gravity,
                          const Scene::GravityFieldSettings& style );
    void SnapSpheres( Rendering::RenderInstanceStore& instances );
    float HeightAt( float x, float z ) const;
    int SnappedCount() const
    {
        return m_snappedCount;
    }
    uint32_t FirstSnappedId() const
    {
        return m_firstSnappedId;
    }
    Math::Vector::Vector3 FirstSnappedPosition() const
    {
        return m_firstSnappedPosition;
    }
    float FirstSnappedRadius() const
    {
        return m_firstSnappedRadius;
    }
    std::span<const float> Lines() const
    {
        return { m_lines.data(), m_lineCount };
    }
    uint32_t FirstSourceId() const
    {
        return m_firstSourceId;
    }
    Math::Vector::Vector3 FirstSourcePosition() const
    {
        return m_sourceCount ? Math::Vector::Vector3( m_sources[0].x, m_sources[0].y, m_sources[0].z ) : Math::Vector::ZERO_VECTOR;
    }
    int SourceCount() const
    {
        return m_sourceCount;
    }
    float MinimumHeight() const
    {
        return m_minimumHeight;
    }

  private:
    void Fit( const Physics::PhysicsBodyStore& bodies, const Physics::MutualGravitySettings& gravity );
    void BuildSurface( const Physics::MutualGravitySettings& gravity );
    void BuildLines();
    void FitPresentedSpheres( std::span<const Rendering::RenderInstanceRecord> instances );
    struct Source
    {
        float x, y, z, mass;
    };
    std::array<Source, Scene::Capacity::MAX_SCENE_OBJECTS> m_sources {};
    Scene::GravityFieldSettings m_style;
    void AppendLine( int first, int second, bool major );
    std::array<Math::Vector::Vector3, POINTS * POINTS> m_points {};
    std::array<float, LINE_FLOATS> m_lines {};
    Math::Vector::Vector3 m_center {};
    float m_extent = 1.0f;
    float m_fittedExtent = 1.0f;
    int m_snappedCount = 0;
    uint32_t m_firstSnappedId = 0;
    Math::Vector::Vector3 m_firstSnappedPosition {};
    float m_firstSnappedRadius = 0.0f;
    float m_referencePotential = 1.0f;
    float m_minimumHeight = 0.0f;
    std::size_t m_lineCount = 0;
    int m_sourceCount = 0;
    uint32_t m_firstSourceId = 0;
    bool m_fitted = false;
};
} // namespace SkullbonezCore::Runtime
