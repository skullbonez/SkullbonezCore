// Owns a bounded, world-addressed deformation cache and GPU patch staging.
// Physics and replay retain their own authority; this owner stores only visual values.
#pragma once
#include "GrassDeformation.h"
#include "../../Maths/Frustum.h"
#include "../../Core/Config.h"
#include <span>
namespace SkullbonezCore::Physics
{
class ColliderStore;
}
namespace SkullbonezCore::Rendering
{
class RenderInstanceStore;
}
namespace SkullbonezCore::Geometry
{
class Terrain;
}
namespace SkullbonezCore::Runtime
{
class GrassPresentation
{
  public:
    static constexpr std::size_t CELL_CAPACITY = 65536;
    static constexpr std::size_t PATCH_CAPACITY = 12288;
    static constexpr std::size_t PATCH_FLOATS = 30;
    static constexpr std::size_t VIEW_CAPACITY = 4;
    static constexpr uint32_t ROOT_TEST_CAPACITY = 131072;
    static constexpr float CELL_SIZE = 0.5f;
    static constexpr float BLADE_HEIGHT = 0.35f;
    static constexpr double RECOVERY_TICKS = 360.0;
    void Configure( const Core::GrassRenderConfig& settings );
    void EnableFixture( uint64_t generation, bool enabled ) noexcept
    {
        m_fixtureGeneration = enabled ? generation : 0;
    }
    bool FixtureEnabled( uint64_t generation ) const noexcept
    {
        return m_fixtureGeneration != 0 && m_fixtureGeneration == generation;
    }
    double RecoverySeconds() const noexcept
    {
        return m_settings.recoverySeconds;
    }
    void SetScene( const GrassTimeCursor::Context& context, bool enabled, float waterHeight );
    bool Enabled() const noexcept
    {
        return m_enabled;
    }
    void
    CaptureLive( const Rendering::RenderInstanceStore& instances, const Physics::ColliderStore& colliders, Geometry::Terrain& terrain, const GrassTimeCursor::Context& context, float waterHeight );
    void RefreshHeld( const Rendering::RenderInstanceStore& instances, const Physics::ColliderStore& colliders, Geometry::Terrain& terrain );
    std::span<const float> Prepare( Geometry::Terrain& terrain,
                                    const Math::Vector::Vector3& eye,
                                    bool historyAvailable,
                                    const Math::Vector::Vector3& lightDirection,
                                    const Math::Vector::Vector3& lightTint,
                                    const Math::Transformation::Matrix4& viewProjection );
    uint64_t Tick() const noexcept
    {
        return m_tick;
    }
    uint32_t RootTests() const noexcept
    {
        return m_rootTests;
    }
    uint32_t DroppedCells() const noexcept
    {
        return m_droppedCells;
    }
    std::size_t PatchCount() const noexcept
    {
        return m_patchCount;
    }
    std::size_t StampedCells() const noexcept
    {
        return m_stampedCells;
    }
    bool HistoryAvailable() const noexcept
    {
        return m_historyAvailable && m_viewHistoryAvailable && m_droppedCells == 0;
    }
    void Reset( const GrassTimeCursor::Context& context );
    GrassTimeCursor::Update SelectHistory( const GrassTimeCursor::Context& context, uint64_t tick );
    void SetSampleTime( uint64_t tick, float physicsDt, float waterHeight );
    void StampSwept( const GrassFootprint& current, const GrassFootprint* previous, Geometry::Terrain& terrain );
    void MarkHistoryMissing() noexcept
    {
        m_historyAvailable = false;
    }
    float CompressionAt( float x, float z ) const;
    uint32_t SourceAt( float x, float z ) const;


  private:
    void CapturePoses( const Rendering::RenderInstanceStore& instances, const Physics::ColliderStore& colliders, Geometry::Terrain& terrain, bool sweep );
    struct Cell
    {
        int x = 0;
        int z = 0;
        bool occupied = false;
        GrassDeformationCell deformation;
    };
    Cell* Find( int x, int z, bool create );
    const Cell* Find( int x, int z ) const;
    void Stamp( const GrassFootprint& footprint, Geometry::Terrain& terrain, float radius );
    void AppendPatch( Geometry::Terrain& terrain, int x, int z, float spacing, float coverage );
    void AppendRegion( Geometry::Terrain& terrain, const Math::Vector::Vector3& eye, int x, int z, int step );
    std::array<Cell, CELL_CAPACITY> m_cells {};
    struct PatchView
    {
        std::array<float, PATCH_CAPACITY * PATCH_FLOATS> records {};
        std::size_t count = 0;
        int centerX = 0, centerZ = 0;
        uint64_t placementVersion = 0, fieldVersion = 0, lastUse = 0;
        bool historyAvailable = false;
        Math::Transformation::Matrix4 viewProjection;
    };
    void UpdatePatchPressure( std::span<float, PATCH_FLOATS> record );
    std::array<PatchView, VIEW_CAPACITY> m_views {};
    std::size_t m_activeView = 0;
    uint64_t m_placementVersion = 1, m_fieldVersion = 1, m_viewUse = 0;
    Math::Visibility::Frustum m_frustum;
    Math::Vector::Vector3 m_lightDirection;
    Math::Vector::Vector3 m_lightTint;
    GrassTimeCursor::Context m_context;
    GrassTimeCursor m_historyCursor;
    double m_recoveryTicks = RECOVERY_TICKS;
    uint64_t m_tick = 0;
    uint32_t m_droppedCells = 0;
    uint32_t m_rootTests = 0;
    std::size_t m_patchCount = 0;
    std::size_t m_stampedCells = 0;
    float m_waterHeight = -10000.0f;
    float m_detailRange = 24.0f;
    Core::GrassRenderConfig m_settings;
    uint64_t m_fixtureGeneration = 0;
    bool m_contextValid = false;
    bool m_enabled = false;
    bool m_historyAvailable = true;
    bool m_viewHistoryAvailable = true;
    bool m_needsHeldRefresh = true;
};
static_assert( sizeof( GrassPresentation ) <= 12u * 1024u * 1024u, "Each discardable grass owner must fit its 12 MiB resident budget" );
} // namespace SkullbonezCore::Runtime
