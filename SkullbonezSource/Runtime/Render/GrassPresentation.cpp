#include "GrassPresentation.h"
#include "../../Physics/ColliderStore.h"
#include "../../Physics/BoundingBox.h"
#include "../../Physics/BoundingSphere.h"
#include "../../Rendering/RenderInstanceStore.h"
#include "../../World/Terrain.h"
#include "../../Core/Profiler.h"

namespace SkullbonezCore::Runtime
{
using Math::Vector::Vector3;
namespace
{
std::size_t CellHash( int x, int z )
{
    const uint32_t key = static_cast<uint32_t>( x ) * 73856093u ^ static_cast<uint32_t>( z ) * 19349663u;
    return key & ( GrassPresentation::CELL_CAPACITY - 1 );
}
GrassFootprint MakeFootprint( const Rendering::RenderInstanceRecord& instance, const Physics::ColliderRecord& collider )
{
    GrassFootprint result;
    result.sceneObjectId = instance.sceneObjectId.value;
    const auto rotation = instance.currentOrientation.GetOrientationMatrix();
    result.center = instance.currentPosition;
    result.orientation = instance.currentOrientation;
    result.axes = { rotation * Vector3( 1, 0, 0 ), rotation * Vector3( 0, 1, 0 ), rotation * Vector3( 0, 0, 1 ) };
    const Vector3 travel = instance.currentPosition - instance.previousPosition;
    result.bendX = travel.x;
    result.bendZ = travel.z;
    using namespace Math::CollisionDetection;
    if ( const auto* sphere = GetShapeIf<BoundingSphere>( &collider.shape ) )
    {
        const float radius = sphere->GetRadius();
        result.halfExtents = Vector3( radius, radius, radius );
        result.center += rotation * sphere->GetPosition();
    }
    else if ( const auto* box = GetShapeIf<BoundingBox>( &collider.shape ) )
    {
        result.shape = GrassFootprintShape::Box;
        result.halfExtents = box->GetHalfExtents();
        result.center += rotation * box->GetPosition();
    }
    else
    {
        result.sceneObjectId = 0;
    }
    return result;
}
} // namespace
void GrassPresentation::Configure( const Core::GrassRenderConfig& settings )
{
    if ( settings == m_settings )
    {
        return;
    }
    if ( settings.height != m_settings.height || settings.recoverySeconds != m_settings.recoverySeconds )
    {
        m_historyCursor.Reset();
        const auto tick = m_tick;
        Reset( m_context );
        m_tick = tick;
    }
    ++m_placementVersion;
    m_settings = settings;
    m_recoveryTicks = settings.recoverySeconds * 120.0;
}
void GrassPresentation::SetScene( const GrassTimeCursor::Context& context, bool enabled, float waterHeight )
{
    if ( !m_contextValid || context != m_context )
    {
        Reset( context );
    }
    m_enabled = enabled;
    if ( m_waterHeight != waterHeight )
    {
        ++m_placementVersion;
    }
    m_waterHeight = waterHeight;
    if ( !enabled )
    {
        m_patchCount = 0;
    }
}
void GrassPresentation::Reset( const GrassTimeCursor::Context& context )
{
    ++m_placementVersion;
    ++m_fieldVersion;
    for ( Cell& cell : m_cells )
    {
        cell.occupied = false;
    }
    m_context = context;
    m_contextValid = true;
    m_tick = 0;
    m_historyAvailable = true;
    m_viewHistoryAvailable = true;
    m_needsHeldRefresh = true;
    m_droppedCells = 0;
    m_stampedCells = 0;
    m_rootTests = 0;
    m_patchCount = 0;
}
GrassPresentation::Cell* GrassPresentation::Find( int x, int z, bool create )
{
    const std::size_t start = CellHash( x, z );
    Cell* expired = nullptr;
    // Bounded collision work. Full/clustered tables report pressure explicitly;
    // they never overwrite a still-visible trail or allocate a bigger table.
    for ( std::size_t probe = 0; probe < 64; ++probe )
    {
        Cell& cell = m_cells[( start + probe ) & ( CELL_CAPACITY - 1 )];
        if ( cell.occupied && cell.x == x && cell.z == z )
        {
            return &cell;
        }
        if ( !cell.occupied )
        {
            expired = expired ? expired : &cell;
            break;
        }
        if ( cell.deformation.Compression( static_cast<double>( m_tick ), m_recoveryTicks ) == 0.0f )
        {
            if ( !expired )
            {
                expired = &cell;
            }
        }
    }
    if ( create && expired )
    {
        *expired = {};
        expired->occupied = true;
        expired->x = x;
        expired->z = z;
        return expired;
    }
    if ( create )
    {
        ++m_droppedCells;
    }
    return nullptr;
}
const GrassPresentation::Cell* GrassPresentation::Find( int x, int z ) const
{
    const std::size_t start = CellHash( x, z );
    for ( std::size_t probe = 0; probe < 64; ++probe )
    {
        const Cell& cell = m_cells[( start + probe ) & ( CELL_CAPACITY - 1 )];
        if ( !cell.occupied )
        {
            return nullptr;
        }
        if ( cell.occupied && cell.x == x && cell.z == z )
        {
            return &cell;
        }
    }
    return nullptr;
}
void GrassPresentation::Stamp( const GrassFootprint& footprint, Geometry::Terrain& terrain, float radius )
{
    const int lowX = static_cast<int>( std::floor( ( footprint.center.x - radius ) / CELL_SIZE ) );
    const int highX = static_cast<int>( std::ceil( ( footprint.center.x + radius ) / CELL_SIZE ) );
    const int lowZ = static_cast<int>( std::floor( ( footprint.center.z - radius ) / CELL_SIZE ) );
    const int highZ = static_cast<int>( std::ceil( ( footprint.center.z + radius ) / CELL_SIZE ) );
    for ( int z = lowZ; z <= highZ; ++z )
    {
        for ( int x = lowX; x <= highX; ++x )
        {
            if ( m_rootTests >= ROOT_TEST_CAPACITY )
            {
                ++m_droppedCells;
                return;
            }
            ++m_rootTests;
            Vector3 root( x * CELL_SIZE, 0, z * CELL_SIZE );
            if ( !terrain.IsInBounds( root.x, root.z ) )
            {
                continue;
            }
            Vector3 normal;
            terrain.GetTerrainHeightAndNormalAt( root.x, root.z, root.y, normal );
            if ( root.y <= m_waterHeight || normal.y < 0.75f )
            {
                continue;
            }
            const float compression = GrassFootprintCompression( footprint, root, normal, m_settings.height );
            if ( compression <= 0.0f )
            {
                continue;
            }
            if ( Cell* cell = Find( x, z, true ) )
            {
                cell->deformation.Stamp( static_cast<double>( m_tick ), m_recoveryTicks, compression, footprint );
                ++m_stampedCells;
            }
        }
    }
}
void GrassPresentation::CaptureLive( const Rendering::RenderInstanceStore& instances,
                                     const Physics::ColliderStore& colliders,
                                     Geometry::Terrain& terrain,
                                     const GrassTimeCursor::Context& context,
                                     float waterHeight )
{
    PROFILE_SCOPED( "Frame/Physics/Step/Grass" );
    if ( !m_contextValid || context != m_context )
    {
        Reset( context );
    }
    if ( m_tick == 0 )
    {
        // Paused spawn previews hold current bodies down without contributing
        // pre-recording history. The first completed tick starts the same
        // explicitly clear finite field used by recorded reconstruction.
        for ( Cell& cell : m_cells )
        {
            cell.occupied = false;
        }
    }
    ++m_tick;
    ++m_fieldVersion;
    m_historyAvailable = true;
    m_waterHeight = waterHeight;
    CapturePoses( instances, colliders, terrain, true );
}
void GrassPresentation::RefreshHeld( const Rendering::RenderInstanceStore& instances, const Physics::ColliderStore& colliders, Geometry::Terrain& terrain )
{
    if ( m_needsHeldRefresh )
    {
        // A display edit or terrain reset can happen while paused. Restamp
        // current occupancy without advancing time or replaying old movement.
        CapturePoses( instances, colliders, terrain, false );
        ++m_fieldVersion;
    }
}
void GrassPresentation::CapturePoses( const Rendering::RenderInstanceStore& instances, const Physics::ColliderStore& colliders, Geometry::Terrain& terrain, bool sweep )
{
    m_needsHeldRefresh = false;
    m_stampedCells = 0;
    m_rootTests = 0;
    const auto records = instances.Records();
    const auto shapes = colliders.Records();
    const std::size_t count = (std::min)( records.size(), shapes.size() );
    for ( std::size_t row = 0; row < count; ++row )
    {
        if ( records[row].sceneObjectId != shapes[row].sceneObjectId )
        {
            continue;
        }
        GrassFootprint footprint = MakeFootprint( records[row], shapes[row] );
        if ( footprint.sceneObjectId == 0 )
        {
            continue;
        }
        const float radius = shapes[row].boundingRadius;
        // Unsupported pathological dimensions are observable capacity pressure,
        // not unbounded per-frame cell scans.
        if ( !std::isfinite( radius ) || radius > 16.0f )
        {
            ++m_droppedCells;
            continue;
        }
        if ( footprint.center.y - radius > terrain.GetMaxHeight() + m_settings.height )
        {
            continue;
        }
        GrassFootprint previous = footprint;
        const auto& instance = records[row];
        const auto oldRotation = instance.previousOrientation.GetOrientationMatrix();
        previous.orientation = instance.previousOrientation;
        previous.axes = { oldRotation * Vector3( 1, 0, 0 ), oldRotation * Vector3( 0, 1, 0 ), oldRotation * Vector3( 0, 0, 1 ) };
        previous.center = instance.previousPosition;
        using namespace Math::CollisionDetection;
        if ( const auto* sphere = GetShapeIf<BoundingSphere>( &shapes[row].shape ) )
        {
            previous.center += oldRotation * sphere->GetPosition();
        }
        if ( const auto* box = GetShapeIf<BoundingBox>( &shapes[row].shape ) )
        {
            previous.center += oldRotation * box->GetPosition();
        }
        StampSwept( footprint, sweep && instance.poseHistoryValid ? &previous : nullptr, terrain );
    }
}
GrassTimeCursor::Update GrassPresentation::SelectHistory( const GrassTimeCursor::Context& context, uint64_t tick )
{
    auto update = m_historyCursor.Select( context, static_cast<int64_t>( tick ) );
    if ( !m_historyAvailable && update == GrassTimeCursor::Update::Advance )
    {
        update = GrassTimeCursor::Update::Rebuild;
    }
    if ( update == GrassTimeCursor::Update::Rebuild )
    {
        Reset( context );
        m_historyAvailable = true;
    }
    return update;
}
void GrassPresentation::SetSampleTime( uint64_t tick, float physicsDt, float waterHeight )
{
    m_tick = tick;
    m_rootTests = 0;
    ++m_fieldVersion;
    m_recoveryTicks = m_settings.recoverySeconds / (std::max)( static_cast<double>( physicsDt ), 1.0e-6 );
    m_waterHeight = waterHeight;
}
float GrassPresentation::CompressionAt( float x, float z ) const
{
    const Cell* cell = Find( static_cast<int>( std::floor( x / CELL_SIZE ) ), static_cast<int>( std::floor( z / CELL_SIZE ) ) );
    return cell && HistoryAvailable() ? cell->deformation.Compression( static_cast<double>( m_tick ), m_recoveryTicks ) : 0.0f;
}
uint32_t GrassPresentation::SourceAt( float x, float z ) const
{
    const Cell* cell = Find( static_cast<int>( std::floor( x / CELL_SIZE ) ), static_cast<int>( std::floor( z / CELL_SIZE ) ) );
    return cell && CompressionAt( x, z ) > 0 ? cell->deformation.SourceId() : 0;
}
void GrassPresentation::StampSwept( const GrassFootprint& current, const GrassFootprint* previous, Geometry::Terrain& terrain )
{
    if ( m_rootTests >= ROOT_TEST_CAPACITY )
    {
        ++m_droppedCells;
        return;
    }
    const float radius = current.shape == GrassFootprintShape::Sphere ? current.halfExtents.x : std::sqrt( GrassDot( current.halfExtents, current.halfExtents ) );
    if ( !std::isfinite( radius ) || radius > 16.0f || radius <= 0.0f )
    {
        ++m_droppedCells;
        return;
    }
    const Vector3 travel = previous ? current.center - previous->center : Vector3( 0, 0, 0 );
    const float distance = std::sqrt( GrassDot( travel, travel ) );
    const bool continuous = previous && previous->sceneObjectId == current.sceneObjectId && previous->shape == current.shape && distance <= 32.0f;
    float dot = 1;
    if ( continuous )
    {
        float a[4], b[4];
        previous->orientation.GetComponents( a[0], a[1], a[2], a[3] );
        current.orientation.GetComponents( b[0], b[1], b[2], b[3] );
        dot = std::fabs( a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3] );
    }
    const float angularTravel = 2 * std::acos( std::clamp( dot, 0.0f, 1.0f ) ) * radius;
    const int steps = continuous ? std::clamp( static_cast<int>( std::ceil( ( distance + angularTravel ) / ( CELL_SIZE * .5f ) ) ), 1, 256 ) : 1;
    for ( int step = 1; step <= steps; ++step )
    {
        if ( m_rootTests >= ROOT_TEST_CAPACITY )
        {
            ++m_droppedCells;
            break;
        }
        GrassFootprint footprint = current;
        if ( continuous )
        {
            const float t = static_cast<float>( step ) / steps;
            footprint.center = previous->center + travel * t;
            const auto orientation = Math::Orientation::NlerpShortest( previous->orientation, current.orientation, t );
            const auto rotation = orientation.GetOrientationMatrix();
            footprint.axes = { rotation * Vector3( 1, 0, 0 ), rotation * Vector3( 0, 1, 0 ), rotation * Vector3( 0, 0, 1 ) };
            footprint.bendX = travel.x;
            footprint.bendZ = travel.z;
        }
        if ( footprint.center.y - radius <= terrain.GetMaxHeight() + m_settings.height )
        {
            Stamp( footprint, terrain, radius );
        }
    }
}
void GrassPresentation::AppendPatch( Geometry::Terrain& terrain, int x, int z, float spacing, float coverage )
{
    Vector3 root( x * CELL_SIZE, 0, z * CELL_SIZE );
    if ( !terrain.IsInBounds( root.x + spacing, root.z + spacing ) || !terrain.IsInBounds( root.x, root.z ) )
    {
        return;
    }
    Vector3 normal;
    terrain.GetTerrainHeightAndNormalAt( root.x, root.z, root.y, normal );
    if ( root.y <= m_waterHeight || normal.y < 0.75f || m_patchCount >= PATCH_CAPACITY )
    {
        return;
    }
    float heights[4] = { root.y, 0, 0, 0 };
    for ( int corner = 1; corner < 4; ++corner )
    {
        Vector3 cornerNormal;
        terrain.GetTerrainHeightAndNormalAt( root.x + ( corner & 1 ) * spacing, root.z + ( corner >> 1 ) * spacing, heights[corner], cornerNormal );
        if ( heights[corner] <= m_waterHeight || cornerNormal.y < .75f )
        {
            return;
        }
    }
    const float minHeight = *std::min_element( std::begin( heights ), std::end( heights ) );
    const float maxHeight = *std::max_element( std::begin( heights ), std::end( heights ) );
    const Vector3 center( root.x + spacing * .5f, ( minHeight + maxHeight ) * .5f, root.z + spacing * .5f );
    // Include every curved tip and a metre of shadow-caster margin. Culling
    // cannot remove a blade merely because its root is just outside the view.
    if ( !m_frustum.IntersectsSphere( center, spacing + ( maxHeight - minHeight ) * .5f + (std::max)( m_settings.height, spacing * .55f ) * 2, 1.0f ) )
    {
        return;
    }
    std::array<float, 8> bladeHeights {};
    for ( uint32_t blade = 0; blade < 8; ++blade )
    {
        const auto uv = GrassBladeUv( root.x, root.z, blade );
        Vector3 bladeNormal;
        terrain.GetTerrainHeightAndNormalAt( root.x + uv[0] * spacing, root.z + uv[1] * spacing, bladeHeights[blade], bladeNormal );
        if ( bladeHeights[blade] <= m_waterHeight || bladeNormal.y < .75f )
        {
            return;
        }
    }
    const std::array<float, PATCH_FLOATS> record { root.x,
                                                   root.y,
                                                   root.z,
                                                   normal.x,
                                                   normal.y,
                                                   normal.z,
                                                   spacing,
                                                   0,
                                                   0,
                                                   0,
                                                   0,
                                                   1,
                                                   0,
                                                   (std::max)( m_settings.height, spacing * .55f ),
                                                   (std::max)( .020f, spacing * .11f ),
                                                   coverage * m_settings.density,
                                                   m_lightDirection.x,
                                                   m_lightDirection.y,
                                                   m_lightDirection.z,
                                                   bladeHeights[0],
                                                   bladeHeights[1],
                                                   bladeHeights[2],
                                                   bladeHeights[3],
                                                   m_lightTint.x,
                                                   m_lightTint.y,
                                                   m_lightTint.z,
                                                   bladeHeights[4],
                                                   bladeHeights[5],
                                                   bladeHeights[6],
                                                   bladeHeights[7] };
    std::copy( record.begin(), record.end(), m_views[m_activeView].records.begin() + m_patchCount * PATCH_FLOATS );
    ++m_patchCount;
}
// Concept: A world-aligned quadtree covers the view without gaps between density levels.
// Each accepted leaf replaces its parent; camera distance controls spacing, while
// the fixed patch arena remains the hard submission bound for all four views.
void GrassPresentation::AppendRegion( Geometry::Terrain& terrain, const Vector3& eye, int x, int z, int step )
{
    if ( m_patchCount >= PATCH_CAPACITY )
    {
        return;
    }
    const float spacing = step * CELL_SIZE;
    const float worldX = x * CELL_SIZE, worldZ = z * CELL_SIZE;
    const auto bounds = terrain.GetXZBounds();
    if ( worldX >= bounds.m_xMax || worldZ >= bounds.m_zMax || worldX + spacing <= bounds.m_xMin || worldZ + spacing <= bounds.m_zMin )
    {
        return;
    }
    const Vector3 center( worldX + spacing * .5f, ( terrain.GetMinHeight() + terrain.GetMaxHeight() ) * .5f, worldZ + spacing * .5f );
    const float heightRange = ( terrain.GetMaxHeight() - terrain.GetMinHeight() ) * .5f;
    if ( !m_frustum.IntersectsSphere( center, spacing + heightRange + 24.0f ) )
    {
        return;
    }
    const float sampleX = std::clamp( center.x, bounds.m_xMin, bounds.m_xMax );
    const float sampleZ = std::clamp( center.z, bounds.m_zMin, bounds.m_zMax );
    const float height = terrain.GetTerrainHeightAt( sampleX, sampleZ );
    const float dx = eye.x - std::clamp( eye.x, worldX, worldX + spacing );
    const float dz = eye.z - std::clamp( eye.z, worldZ, worldZ + spacing );
    const float dy = eye.y - height;
    const float distance = std::sqrt( dx * dx + dy * dy + dz * dz );
    // Vary the split threshold in world space so equal-distance leaves do not
    // form visible rings. Crown area scales with spacing to preserve turf coverage.
    const float splitVariation = .8f + 1.6f * GrassBladeUv( worldX, worldZ, 0 )[0];
    const float detailRange = m_detailRange * splitVariation;
    const bool boundary = worldX < bounds.m_xMin || worldZ < bounds.m_zMin || worldX + spacing > bounds.m_xMax || worldZ + spacing > bounds.m_zMax;
    if ( step > 1 && ( boundary || distance < detailRange * step ) )
    {
        const int half = step / 2;
        for ( int child = 0; child < 4; ++child )
        {
            AppendRegion( terrain, eye, x + ( child & 1 ) * half, z + ( child >> 1 ) * half, half );
        }
        return;
    }
    const float farDistance = m_settings.distance * 64.0f;
    const float coverage = std::clamp( ( farDistance - distance ) / ( farDistance * .15f ), 0.0f, 1.0f );
    if ( coverage > 0 )
    {
        AppendPatch( terrain, x, z, spacing, coverage );
    }
}
void GrassPresentation::UpdatePatchPressure( std::span<float, PATCH_FLOATS> record )
{
    float strongest = 0;
    record[11] = m_settings.bend;
    record[12] = 0;
    // Coarse patches query the same half-unit deformation field as near blades.
    // There is no second deformation simulation and no camera-centred field limit.
    for ( int corner = 0; corner < 4; ++corner )
    {
        const int x = static_cast<int>( std::floor( ( record[0] + ( corner & 1 ) * record[6] ) / CELL_SIZE ) );
        const int z = static_cast<int>( std::floor( ( record[2] + ( corner >> 1 ) * record[6] ) / CELL_SIZE ) );
        const Cell* cell = HistoryAvailable() ? Find( x, z ) : nullptr;
        const float pressure = cell ? cell->deformation.Compression( static_cast<double>( m_tick ), m_recoveryTicks ) : 0.0f;
        record[7 + corner] = pressure;
        if ( pressure > strongest )
        {
            strongest = pressure;
            record[11] = cell->deformation.BendX() * m_settings.bend;
            record[12] = cell->deformation.BendZ() * m_settings.bend;
        }
    }
}
std::span<const float> GrassPresentation::Prepare( Geometry::Terrain& terrain,
                                                   const Vector3& eye,
                                                   bool historyAvailable,
                                                   const Vector3& lightDirection,
                                                   const Vector3& lightTint,
                                                   const Math::Transformation::Matrix4& viewProjection )
{
    PROFILE_SCOPED( "Frame/Render/GrassPrepare" );
    m_patchCount = 0;
    m_viewHistoryAvailable = historyAvailable;
    if ( m_settings.quality < .5f )
    {
        return {};
    }
    const int centerX = static_cast<int>( std::floor( eye.x / CELL_SIZE ) );
    const int centerZ = static_cast<int>( std::floor( eye.z / CELL_SIZE ) );
    if ( lightDirection.x != m_lightDirection.x || lightDirection.y != m_lightDirection.y || lightDirection.z != m_lightDirection.z || lightTint.x != m_lightTint.x || lightTint.y != m_lightTint.y ||
         lightTint.z != m_lightTint.z )
    {
        ++m_placementVersion;
    }
    m_lightDirection = lightDirection;
    m_lightTint = lightTint;
    ++m_viewUse;
    bool found = false;
    std::size_t oldest = 0;
    for ( std::size_t index = 0; index < VIEW_CAPACITY; ++index )
    {
        const auto& candidate = m_views[index];
        if ( candidate.placementVersion == m_placementVersion && candidate.centerX == centerX && candidate.centerZ == centerZ &&
             std::equal( std::begin( candidate.viewProjection.m ), std::end( candidate.viewProjection.m ), std::begin( viewProjection.m ) ) )
        {
            m_activeView = index;
            found = true;
            break;
        }
        if ( candidate.lastUse < m_views[oldest].lastUse )
        {
            oldest = index;
        }
    }
    if ( !found )
    {
        m_activeView = oldest;
    }
    auto& view = m_views[m_activeView];
    if ( !found )
    {
        m_frustum = Math::Visibility::Frustum::FromViewProjection( Math::Transformation::Matrix4(), viewProjection );
        // Begin with 64-unit roots across the bounded visible distance. Subdivision
        // preserves dense near grass and spends fewer crowns on distant terrain.
        constexpr int rootStep = 128;
        const int rootRadius = static_cast<int>( std::ceil( m_settings.distance ) );
        const int rootX = static_cast<int>( std::floor( eye.x / ( rootStep * CELL_SIZE ) ) );
        const int rootZ = static_cast<int>( std::floor( eye.z / ( rootStep * CELL_SIZE ) ) );
        m_detailRange = m_settings.distance * ( m_settings.quality < 1.5f ? .70f : 1.0f );
        // Invariant: A full arena must reduce detail across the whole view, never leave the
        // last rows of terrain bare. Each retry reduces the subdivision detail range.
        do
        {
            m_patchCount = 0;
            for ( int z = -rootRadius; z <= rootRadius; ++z )
            {
                for ( int x = -rootRadius; x <= rootRadius; ++x )
                {
                    AppendRegion( terrain, eye, ( rootX + x ) * rootStep, ( rootZ + z ) * rootStep, rootStep );
                }
            }
            m_detailRange *= .70f;
        } while ( m_patchCount == PATCH_CAPACITY && m_detailRange > 1.0f );
        view.count = m_patchCount;
        view.centerX = centerX;
        view.centerZ = centerZ;
        view.placementVersion = m_placementVersion;
        view.viewProjection = viewProjection;
    }
    m_patchCount = view.count;
    if ( !found || view.fieldVersion != m_fieldVersion || view.historyAvailable != HistoryAvailable() )
    {
        for ( std::size_t patch = 0; patch < view.count; ++patch )
        {
            UpdatePatchPressure( std::span<float, PATCH_FLOATS>( view.records.data() + patch * PATCH_FLOATS, PATCH_FLOATS ) );
        }
        view.fieldVersion = m_fieldVersion;
        view.historyAvailable = HistoryAvailable();
    }
    view.lastUse = m_viewUse;
    return { view.records.data(), view.count * PATCH_FLOATS };
}

} // namespace SkullbonezCore::Runtime
