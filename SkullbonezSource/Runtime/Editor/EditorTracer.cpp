/*
File: SkullbonezSource/Runtime/Editor/EditorTracer.cpp
Purpose:
  Implements runtime editor overlay tracer primitives and draw submission.

Summary:
  The tracer turns editor/replay tool state into transient colored lines and
  replay ribbons. A prediction-owned tracer may also retain append-only compact
  ribbon chunks across frames; both forms observe state prepared elsewhere and

  do not mutate selection, physics, or replay ownership.

Glossary:
  Tracer: Per-frame line builder for placement rays, gizmos, replay paths, and selection outlines.
  Selection outline: Shape-accurate wire outline drawn from explicit pose and
    collision-shape values supplied by the owning tool.
  Replay future marker: Shape-accurate downstream collision outline drawn at
    the latest visible predicted/retained pose, never from a broadphase radius substitute.
  Placement ghost: Preview outline drawn before an editor placement commit; it
    must match the primitive bodies that placement will actually spawn.

Invariants:
  - Frame-local trace buffers are cleared every frame; the dedicated prediction
    tracer clears retained chunks only when its generation is invalidated.
  - Replay causal markers use priority overlay storage so expensive prediction
    paths can degrade without erasing already-revealed boxes.
  - The tracer owns fixed-capacity overlay buffers and must not allocate while
    building a frame.
  - Line, depth-hint, and visible-ribbon draws carry separate immutable raster
    buckets; overlay submission never borrows the prior pass's state.

Related:
  - SkullbonezSource/Runtime/Tools/RuntimeTools.h
  - SkullbonezSource/Runtime/Editor/EditorOverlayTools.h
  - SkullbonezSource/Runtime/Editor/EditorPlacementAssets.h
  - SkullbonezSource/Runtime/Editor/EditorTools.h
  - SkullbonezSource/Runtime/Editor/EditorInteractionTools.cpp
  - Agentic/Reference/engine-glossary.md
*/
#include "EditorPlacementAssets.h"
#include "EditorTools.h"
#include "../Tools/RuntimeTools.h"
#include "../../Physics/CollisionShape.h"
#include "../../Physics/Ragdoll.h"
#include "../../Core/Config.h"
#include "../../Core/ByteView.h"
#include "../../Rendering/RenderCommandTypes.h"
#include "../../Rendering/DX12/RenderBackendDX12.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

using namespace SkullbonezCore::Runtime;
using namespace SkullbonezCore::Math::CollisionDetection;
using namespace SkullbonezCore::Math::Orientation;
using namespace SkullbonezCore::Math::Transformation;
using SkullbonezCore::Math::Vector::Vector3;
using SkullbonezCore::Physics::Ragdoll;
using Json = SkullbonezCore::Runtime::EditorPlacementJson;

namespace
{
constexpr std::size_t EDITOR_TRACER_LINE_FLOAT_CAPACITY = 262144;
constexpr std::size_t EDITOR_TRACER_PRIORITY_LINE_FLOAT_CAPACITY = 524288;
constexpr std::size_t EDITOR_TRACER_FLOATS_PER_LINE = 12;

// Why: the Stage-9 frozen prediction probe submitted 21,568 replay ribbon
// segments. The configured 27,000-segment/162,000-vertex ceiling adds 25.2%
// headroom; the 19-float adjacency payload uses 23.5 MiB across the depth-hint
// and visible passes, remaining inside the 32 MiB frame arena.
// Ordinary paths get 24,000 slots and causal priority evidence keeps 3,000.
constexpr std::size_t EDITOR_TRACER_REPLAY_RIBBON_SEGMENT_BUDGET = 27000;
constexpr std::size_t EDITOR_TRACER_REPLAY_RIBBON_ORDINARY_SEGMENT_CAPACITY = 24000;
constexpr std::size_t
    EDITOR_TRACER_REPLAY_RIBBON_PRIORITY_SEGMENT_CAPACITY = EDITOR_TRACER_REPLAY_RIBBON_SEGMENT_BUDGET -
                                                            EDITOR_TRACER_REPLAY_RIBBON_ORDINARY_SEGMENT_CAPACITY;

constexpr std::size_t EDITOR_TRACER_REPLAY_RIBBON_FLOATS_PER_SEGMENT = 13;
constexpr std::size_t
    EDITOR_TRACER_REPLAY_RIBBON_ORDINARY_FLOAT_CAPACITY = EDITOR_TRACER_REPLAY_RIBBON_ORDINARY_SEGMENT_CAPACITY *
                                                          EDITOR_TRACER_REPLAY_RIBBON_FLOATS_PER_SEGMENT;

constexpr std::size_t
    EDITOR_TRACER_REPLAY_RIBBON_PRIORITY_FLOAT_CAPACITY = EDITOR_TRACER_REPLAY_RIBBON_PRIORITY_SEGMENT_CAPACITY *
                                                          EDITOR_TRACER_REPLAY_RIBBON_FLOATS_PER_SEGMENT;

constexpr std::size_t EDITOR_TRACER_REPLAY_RIBBON_FLOATS_PER_VERTEX = 19;
constexpr std::size_t EDITOR_TRACER_REPLAY_RIBBON_VERTICES_PER_SEGMENT = 6;
constexpr std::size_t
    EDITOR_TRACER_REPLAY_RIBBON_ORDINARY_VERTEX_FLOAT_CAPACITY = EDITOR_TRACER_REPLAY_RIBBON_ORDINARY_SEGMENT_CAPACITY *
                                                                 EDITOR_TRACER_REPLAY_RIBBON_VERTICES_PER_SEGMENT *
                                                                 EDITOR_TRACER_REPLAY_RIBBON_FLOATS_PER_VERTEX;

constexpr std::size_t
    EDITOR_TRACER_REPLAY_RIBBON_PRIORITY_VERTEX_FLOAT_CAPACITY = EDITOR_TRACER_REPLAY_RIBBON_PRIORITY_SEGMENT_CAPACITY *
                                                                 EDITOR_TRACER_REPLAY_RIBBON_VERTICES_PER_SEGMENT *
                                                                 EDITOR_TRACER_REPLAY_RIBBON_FLOATS_PER_VERTEX;

constexpr float EDITOR_TRACER_REPLAY_LINE_OPACITY = 0.5f;
constexpr uint64_t REPLAY_TRAJECTORY_SUBMISSION_FNV_OFFSET = 1469598103934665603ull;
constexpr uint64_t REPLAY_TRAJECTORY_SUBMISSION_FNV_PRIME = 1099511628211ull;
constexpr SkullbonezCore::Rendering::PassRasterStateBucket REPLAY_RIBBON_DEPTH_HINT_RASTER = SkullbonezCore::Rendering::
    MakePassRasterStateBucket( 0,
                               { false, false, true, SkullbonezCore::Rendering::BlendFactor::SrcAlpha,
                                 SkullbonezCore::Rendering::BlendFactor::One, SkullbonezCore::Rendering::CullMode::None } );

constexpr SkullbonezCore::Rendering::PassRasterStateBucket REPLAY_RIBBON_VISIBLE_RASTER = SkullbonezCore::Rendering::
    MakePassRasterStateBucket( 1,
                               { true, false, true, SkullbonezCore::Rendering::BlendFactor::SrcAlpha,
                                 SkullbonezCore::Rendering::BlendFactor::One, SkullbonezCore::Rendering::CullMode::None } );

constexpr SkullbonezCore::Rendering::PassRasterStateBucket REPLAY_LINE_RASTER = SkullbonezCore::Rendering::
    MakePassRasterStateBucket( 2,
                               { false, false, false, SkullbonezCore::Rendering::BlendFactor::One,
                                 SkullbonezCore::Rendering::BlendFactor::Zero, SkullbonezCore::Rendering::CullMode::None } );

void HashReplaySubmissionBytes( uint64_t& hash, SkullbonezCore::Core::ByteView bytes )
{

    for ( uint8_t byte : bytes )
    {
        hash ^= static_cast<uint64_t>( byte );
        hash *= REPLAY_TRAJECTORY_SUBMISSION_FNV_PRIME;
    }
}

void HashReplaySubmissionFloatStream( const std::vector<float>& values, uint64_t& outHash, uint64_t& outBytes )
{
    outHash = REPLAY_TRAJECTORY_SUBMISSION_FNV_OFFSET;
    const uint64_t floatCount = static_cast<uint64_t>( values.size() );
    HashReplaySubmissionBytes( outHash, SkullbonezCore::Core::ObjectBytes( floatCount ) );
    outBytes = floatCount * sizeof( float );

    if ( !values.empty() )
    {
        HashReplaySubmissionBytes( outHash, SkullbonezCore::Core::ObjectBytes( std::span<const float>( values ) ) );
    }
}

uint64_t HashReplaySubmissionFloatStreams( std::span<const float> first, std::span<const float> second )
{
    uint64_t hash = REPLAY_TRAJECTORY_SUBMISSION_FNV_OFFSET;
    const uint64_t floatCount = static_cast<uint64_t>( first.size() + second.size() );
    HashReplaySubmissionBytes( hash, SkullbonezCore::Core::ObjectBytes( floatCount ) );

    if ( !first.empty() )
    {
        HashReplaySubmissionBytes( hash, SkullbonezCore::Core::ObjectBytes( first ) );
    }

    if ( !second.empty() )
    {
        HashReplaySubmissionBytes( hash, SkullbonezCore::Core::ObjectBytes( second ) );
    }

    return hash;
}

uint64_t HashReplaySubmissionCanonicalRecords( const std::vector<float>& values, std::size_t floatsPerRecord )
{
    uint64_t sum = 0;
    uint64_t mixedSum = 0;
    uint64_t recordCount = 0;

    for ( std::size_t index = 0; index + floatsPerRecord <= values.size(); index += floatsPerRecord )
    {
        uint64_t recordHash = REPLAY_TRAJECTORY_SUBMISSION_FNV_OFFSET;
        HashReplaySubmissionBytes( recordHash, SkullbonezCore::Core::ObjectBytes( std::span<const float>( values ).subspan( index, floatsPerRecord ) ) );

        sum += recordHash;

        // A second commutative moment prevents permutations from mattering
        // while duplicate records and individual-bit mutations remain visible.
        recordHash ^= recordHash >> 30u;
        recordHash *= 0xBF58476D1CE4E5B9ull;
        recordHash ^= recordHash >> 27u;
        recordHash *= 0x94D049BB133111EBull;
        recordHash ^= recordHash >> 31u;
        mixedSum += recordHash;
        ++recordCount;
    }

    uint64_t hash = REPLAY_TRAJECTORY_SUBMISSION_FNV_OFFSET;
    HashReplaySubmissionBytes( hash, SkullbonezCore::Core::ObjectBytes( recordCount ) );
    HashReplaySubmissionBytes( hash, SkullbonezCore::Core::ObjectBytes( sum ) );
    HashReplaySubmissionBytes( hash, SkullbonezCore::Core::ObjectBytes( mixedSum ) );
    return hash;
}

} // namespace


EditorTracer::EditorTracer( SkullbonezCore::Core::SbDiagnosticStore& resultDiagnostics )
    : m_resultDiagnostics( resultDiagnostics )
{

    // Runtime allocation policy: overlay line storage is paid once during tool
    // construction. EmitLine refuses overflow so replay prediction, gizmos, and
    // target markers cannot grow this vector while render builds the frame.
    m_lineData.reserve( EDITOR_TRACER_LINE_FLOAT_CAPACITY );
    m_priorityLineData.reserve( EDITOR_TRACER_PRIORITY_LINE_FLOAT_CAPACITY );
    m_renderLineData.reserve( EDITOR_TRACER_LINE_FLOAT_CAPACITY + EDITOR_TRACER_PRIORITY_LINE_FLOAT_CAPACITY );
    m_replayRibbonSegments.reserve( EDITOR_TRACER_REPLAY_RIBBON_ORDINARY_FLOAT_CAPACITY );
    m_priorityReplayRibbonSegments.reserve( EDITOR_TRACER_REPLAY_RIBBON_PRIORITY_FLOAT_CAPACITY );
    m_replayRibbonVertexData.reserve( EDITOR_TRACER_REPLAY_RIBBON_ORDINARY_VERTEX_FLOAT_CAPACITY );
    m_priorityReplayRibbonVertexData.reserve( EDITOR_TRACER_REPLAY_RIBBON_PRIORITY_VERTEX_FLOAT_CAPACITY );
}

bool EditorTracer::SetReplayTrajectoryAppearance( const Core::ReplayTrajectoryAppearanceConfig& appearance )
{
    const auto boundedStyle = []( float width, float alpha, float edgeFeather )
    {
        return ReplayRibbonStyle { std::clamp( width, 1.0f, 6.0f ), std::clamp( alpha, 0.05f, 1.0f ),
                                   std::clamp( edgeFeather, 0.25f, 1.25f ), 0.0f };
    };

    const ReplayRibbonStyle path = boundedStyle( appearance.futureWidth, appearance.futureAlpha,
                                                 appearance.futureEdgeFeather );

    const ReplayRibbonStyle causal = boundedStyle( appearance.causalWidth, appearance.causalAlpha,
                                                   appearance.causalEdgeFeather );

    const ReplayRibbonStyle baseline = boundedStyle( appearance.baselineWidth, appearance.baselineAlpha,
                                                     appearance.baselineEdgeFeather );

    const ReplayRibbonStyle marker = boundedStyle( appearance.markerWidth, appearance.markerAlpha,
                                                   appearance.markerEdgeFeather );

    const float selectedEmphasis = std::clamp( appearance.selectedEmphasis, 0.0f, 1.0f );
    const auto sameStyle = []( const ReplayRibbonStyle& a, const ReplayRibbonStyle& b )
    { return a.width == b.width && a.alpha == b.alpha && a.edgeFeather == b.edgeFeather && a.emphasis == b.emphasis; };

    if ( m_replayTrajectoryAppearanceInitialized && sameStyle( path, m_replayPathStyle ) &&
         sameStyle( causal, m_replayCausalStyle ) && sameStyle( baseline, m_replayBaselineStyle ) &&
         sameStyle( marker, m_replayMarkerStyle ) && selectedEmphasis == m_replaySelectedEmphasis )
    {
        return false;
    }

    m_replayPathStyle = path;
    m_replayCausalStyle = causal;
    m_replayBaselineStyle = baseline;
    m_replayMarkerStyle = marker;
    m_replaySelectedEmphasis = selectedEmphasis;
    m_replayTrajectoryAppearanceInitialized = true;
    return true;
}


void EditorTracer::Clear()
{
    m_lineData.clear();
    m_priorityLineData.clear();
    m_renderLineData.clear();
    m_replayRibbonSegments.clear();
    m_priorityReplayRibbonSegments.clear();
    m_replayRibbonVertexData.clear();
    m_priorityReplayRibbonVertexData.clear();
    m_expandedOrdinarySegmentCount = 0;
    m_expandedPrioritySegmentCount = 0;
    ClearReplayTrajectoryStats();
    m_replaySubmissionStats = SkullbonezCore::Core::MainMemoryReplayTrajectorySubmissionStats {};
}

void EditorTracer::ClearReplayTrajectoryStats()
{
    m_replayTrajectoryStats = SkullbonezCore::Core::MainMemoryReplayTrajectoryStats {};
}


const SkullbonezCore::Core::MainMemoryReplayTrajectoryStats& EditorTracer::ReplayTrajectoryStats() const
{
    return m_replayTrajectoryStats;
}


void EditorTracer::RecordReplayRibbonDroppedSegments( SkullbonezCore::Core::MainMemoryReplayTrajectoryLane lane,
                                                      std::size_t count )
{
    const std::size_t laneIndex = static_cast<std::size_t>( lane );

    if ( laneIndex < SkullbonezCore::Core::MAIN_MEMORY_REPLAY_TRAJECTORY_LANE_COUNT )
    {
        m_replayTrajectoryStats.droppedSegments[laneIndex] += static_cast<uint64_t>( count );
    }
}

ReplayVisualPacket EditorTracer::BuildReplayVisualPacket( const Vector3& cameraEye, const Vector3& cameraUp )
{
    m_renderLineData.clear();

    if ( !m_priorityLineData.empty() )
    {

        // Invariant: the packet's combined stream is the exact single line
        // submission consumed below. Ordinary and priority spans remain
        // separate so first-difference diagnostics retain their owner lane.
        m_renderLineData.insert( m_renderLineData.end(), m_lineData.begin(), m_lineData.end() );
        m_renderLineData.insert( m_renderLineData.end(), m_priorityLineData.begin(), m_priorityLineData.end() );
    }

    BuildReplayRibbonVertices( cameraEye, cameraUp );

    ReplayVisualPacket packet;
    packet.header.cameraEye = cameraEye;
    packet.header.cameraUp = cameraUp;
    packet.combinedLines = m_priorityLineData.empty() ? std::span<const float>( m_lineData )
                                                      : std::span<const float>( m_renderLineData );

    packet.ordinaryLines = m_lineData;
    packet.priorityLines = m_priorityLineData;
    packet.ordinaryRibbonSegments = m_replayRibbonSegments;
    packet.priorityRibbonSegments = m_priorityReplayRibbonSegments;
    packet.expandedRibbonVertices = m_replayRibbonVertexData;
    packet.priorityExpandedRibbonVertices = m_priorityReplayRibbonVertexData;
    packet.submission = m_replaySubmissionStats;
    return packet;
}


std::size_t EditorTracer::ReplayPathRibbonSegmentCapacityRemaining() const
{

    if ( m_replayRibbonSegments.size() >= m_replayRibbonSegments.capacity() )
    {
        return 0;
    }

    return ( m_replayRibbonSegments.capacity() - m_replayRibbonSegments.size() ) /
           EDITOR_TRACER_REPLAY_RIBBON_FLOATS_PER_SEGMENT;
}

std::size_t EditorTracer::ReplayPriorityRibbonSegmentCapacityRemaining() const
{

    if ( m_priorityReplayRibbonSegments.size() >= m_priorityReplayRibbonSegments.capacity() )
    {
        return 0;
    }

    return ( m_priorityReplayRibbonSegments.capacity() - m_priorityReplayRibbonSegments.size() ) /
           EDITOR_TRACER_REPLAY_RIBBON_FLOATS_PER_SEGMENT;
}


void EditorTracer::EmitLineTo( std::vector<float>& lineData, const Vector3& a, const Vector3& b, float r, float g, float bl )
{

    if ( lineData.size() + EDITOR_TRACER_FLOATS_PER_LINE > lineData.capacity() )
    {
        return;
    }

    lineData.insert( lineData.end(), { a.x, a.y, a.z, r, g, bl, b.x, b.y, b.z, r, g, bl } );
    ++m_replayGeometryRevision;
}


void EditorTracer::EmitLine( const Vector3& a, const Vector3& b, float r, float g, float bl )
{
    EmitLineTo( m_lineData, a, b, r, g, bl );
}


void EditorTracer::EmitArrow( const Vector3& a, const Vector3& b, float r, float g, float bl )
{
    EmitLine( a, b, r, g, bl );

    Vector3 dir = b - a;
    const float len = VectorMag( dir );

    if ( len <= TOLERANCE )
    {
        return;
    }

    dir /= len;

    Vector3 side = fabsf( dir.y ) < 0.8f ? CrossProduct( dir, Vector3( 0.0f, 1.0f, 0.0f ) )
                                         : CrossProduct( dir, Vector3( 1.0f, 0.0f, 0.0f ) );

    const float sideLen = VectorMag( side );

    if ( sideLen <= TOLERANCE )
    {
        return;
    }

    side /= sideLen;

    const float head = (std::min)( len * 0.25f, 2.0f );
    const Vector3 base = b - dir * head;
    EmitLine( b, base + side * ( head * 0.45f ), r, g, bl );
    EmitLine( b, base - side * ( head * 0.45f ), r, g, bl );
}


void EditorTracer::EmitRing( const Vector3& center, int axis, float radius, float r, float g, float bl )
{
    constexpr int segments = 64;
    const Vector3 basisA = EditorRotationRingBasisA( axis );
    const Vector3 basisB = EditorRotationRingBasisB( axis );
    Vector3 previous = center + basisA * radius;

    for ( int i = 1; i <= segments; ++i )
    {
        const float theta = static_cast<float>( i ) * ( 2.0f * _PI / static_cast<float>( segments ) );
        const Vector3 next = center + basisA * ( cosf( theta ) * radius ) + basisB * ( sinf( theta ) * radius );
        EmitLine( previous, next, r, g, bl );
        previous = next;
    }
}


void EditorTracer::EmitSphereTo( std::vector<float>& lineData, const Vector3& center, float radius, float r, float g,
                                 float bl )
{
    constexpr int segments = 32;

    for ( int plane = 0; plane < 3; ++plane )
    {
        Vector3 previous;

        for ( int i = 0; i <= segments; ++i )
        {
            const float theta = static_cast<float>( i ) * ( 2.0f * _PI / static_cast<float>( segments ) );
            const float c = cosf( theta ) * radius;
            const float s = sinf( theta ) * radius;
            Vector3 next = center;

            if ( plane == 0 )
            {
                next.x += c;
                next.z += s;
            }
            else if ( plane == 1 )
            {
                next.x += c;
                next.y += s;
            }
            else
            {
                next.y += c;
                next.z += s;
            }

            if ( i > 0 )
            {
                EmitLineTo( lineData, previous, next, r, g, bl );
            }

            previous = next;
        }
    }
}


void EditorTracer::EmitSphere( const Vector3& center, float radius, float r, float g, float bl )
{
    EmitSphereTo( m_lineData, center, radius, r, g, bl );
}


void EditorTracer::EmitBoxTo( std::vector<float>& lineData, const Vector3& center, const Vector3& xAxis,
                              const Vector3& yAxis, const Vector3& zAxis, float r, float g, float bl )
{
    const Vector3 corners[8] = {
        center - xAxis - yAxis - zAxis, center + xAxis - yAxis - zAxis, center + xAxis + yAxis - zAxis,
        center - xAxis + yAxis - zAxis, center - xAxis - yAxis + zAxis, center + xAxis - yAxis + zAxis,
        center + xAxis + yAxis + zAxis, center - xAxis + yAxis + zAxis,
    };

    static constexpr int kEdges[12][2] = {
        { 0, 1 }, { 1, 2 }, { 2, 3 }, { 3, 0 }, { 4, 5 }, { 5, 6 },
        { 6, 7 }, { 7, 4 }, { 0, 4 }, { 1, 5 }, { 2, 6 }, { 3, 7 },
    };

    for ( const auto& edge : kEdges )
    {
        EmitLineTo( lineData, corners[edge[0]], corners[edge[1]], r, g, bl );
    }
}


void EditorTracer::EmitBox( const Vector3& center, const Vector3& xAxis, const Vector3& yAxis, const Vector3& zAxis, float r,
                            float g, float bl )
{
    EmitBoxTo( m_lineData, center, xAxis, yAxis, zAxis, r, g, bl );
}


void EditorTracer::EmitShapeOutlineTo( std::vector<float>& lineData, const Vector3& position, const Quaternion& orientation,
                                       const CollisionShapeReference& shape, float r, float g, float b )
{
    Quaternion outlineOrientation = orientation;
    const RotationMatrix rot = outlineOrientation.GetOrientationMatrix();

    if ( const BoundingSphere* sphere = GetShapeIf<BoundingSphere>( &shape ) )
    {
        EmitSphereTo( lineData, position + rot * sphere->GetPosition(), sphere->GetBoundingRadius(), r, g, b );
        return;
    }

    if ( const BoundingBox* box = GetShapeIf<BoundingBox>( &shape ) )
    {
        const Vector3& he = box->GetHalfExtents();
        const Vector3 center = position + rot * box->GetPosition();
        EmitBoxTo( lineData, center, rot * Vector3( he.x, 0.0f, 0.0f ), rot * Vector3( 0.0f, he.y, 0.0f ),
                   rot * Vector3( 0.0f, 0.0f, he.z ), r, g, b );

        return;
    }

    if ( const ConvexHullShape* hull = GetShapeIf<ConvexHullShape>( &shape ) )
    {
        const Vector3 hullCenter = position + rot * hull->GetPosition();

        for ( uint16_t edgeIndex = 0; edgeIndex < hull->GetEdgeCount(); ++edgeIndex )
        {
            const ConvexHullEdge& edge = hull->GetEdge( edgeIndex );
            EmitLineTo( lineData, hullCenter + rot * hull->GetVertex( edge.vertexA ),
                        hullCenter + rot * hull->GetVertex( edge.vertexB ), r, g, b );
        }
    }
}


void EditorTracer::EmitShapeOutline( const Vector3& position, const Quaternion& orientation,
                                     const CollisionShapeReference& shape, float r, float g, float b )
{
    EmitShapeOutlineTo( m_lineData, position, orientation, shape, r, g, b );
}


void EditorTracer::EmitReplayRibbonSegmentTo( std::vector<float>& ribbonData, const Vector3& a, const Vector3& b, float r,
                                              float g, float bl, const ReplayRibbonStyle& style,
                                              SkullbonezCore::Core::MainMemoryReplayTrajectoryLane lane )
{

    if ( VectorMagSquared( b - a ) <= TOLERANCE * TOLERANCE )
    {
        return;
    }

    const std::size_t laneIndex = static_cast<std::size_t>( lane );
    const std::size_t combinedSegments = ( m_replayRibbonSegments.size() + m_priorityReplayRibbonSegments.size() ) /
                                         EDITOR_TRACER_REPLAY_RIBBON_FLOATS_PER_SEGMENT;

    if ( combinedSegments >= EDITOR_TRACER_REPLAY_RIBBON_SEGMENT_BUDGET ||
         ribbonData.size() + EDITOR_TRACER_REPLAY_RIBBON_FLOATS_PER_SEGMENT > ribbonData.capacity() )
    {

        if ( laneIndex < SkullbonezCore::Core::MAIN_MEMORY_REPLAY_TRAJECTORY_LANE_COUNT )
        {
            RecordReplayRibbonDroppedSegments( lane );
        }

        return;
    }

    if ( laneIndex < SkullbonezCore::Core::MAIN_MEMORY_REPLAY_TRAJECTORY_LANE_COUNT )
    {
        ++m_replayTrajectoryStats.emittedSegments[laneIndex];
    }

    // Invariant: replay ribbon storage is reserved during tracer construction.
    // Explicit appends keep the steady-gameplay path inside that fixed budget.
    ribbonData.push_back( a.x );
    ribbonData.push_back( a.y );
    ribbonData.push_back( a.z );
    ribbonData.push_back( b.x );
    ribbonData.push_back( b.y );
    ribbonData.push_back( b.z );
    ribbonData.push_back( r );
    ribbonData.push_back( g );
    ribbonData.push_back( bl );
    ribbonData.push_back( style.width );
    ribbonData.push_back( style.alpha );
    ribbonData.push_back( style.edgeFeather );
    ribbonData.push_back( style.emphasis );
    ++m_replayGeometryRevision;
}


void EditorTracer::EmitReplayRibbonGlowPairTo( std::vector<float>& ribbonData, const Vector3& a, const Vector3& b, float r,
                                               float g, float bl, const ReplayRibbonStyle& glow,
                                               const ReplayRibbonStyle& core,
                                               SkullbonezCore::Core::MainMemoryReplayTrajectoryLane lane )
{

    // Why: legacy callers still supply two style records, but the vector ribbon
    // owns edge coverage and optional selection halo in one pixel-shader pass.
    // Merge the strongest hints so each logical segment consumes one fixed-
    // budget ribbon record.
    ReplayRibbonStyle singlePass = glow;
    singlePass.alpha = (std::max)( glow.alpha, core.alpha );
    singlePass.edgeFeather = (std::max)( glow.edgeFeather, core.edgeFeather );
    singlePass.emphasis = (std::max)( glow.emphasis, core.emphasis );
    EmitReplayRibbonSegmentTo( ribbonData, a, b, r, g, bl, singlePass, lane );
}


void EditorTracer::EmitReplayRibbonShapeOutlineTo( std::vector<float>& ribbonData, const Vector3& position,
                                                   const Quaternion& orientation, const CollisionShapeReference& shape,
                                                   float r, float g, float b, const ReplayRibbonStyle& style,
                                                   SkullbonezCore::Core::MainMemoryReplayTrajectoryLane lane )
{
    Quaternion outlineOrientation = orientation;
    const RotationMatrix rot = outlineOrientation.GetOrientationMatrix();

    if ( const BoundingSphere* sphere = GetShapeIf<BoundingSphere>( &shape ) )
    {
        constexpr int segments = 32;
        const Vector3 center = position + rot * sphere->GetPosition();
        const float radius = sphere->GetBoundingRadius();

        for ( int plane = 0; plane < 3; ++plane )
        {
            Vector3 previous;

            for ( int i = 0; i <= segments; ++i )
            {
                const float theta = static_cast<float>( i ) * ( 2.0f * _PI / static_cast<float>( segments ) );
                const float c = cosf( theta ) * radius;
                const float s = sinf( theta ) * radius;
                Vector3 next = center;

                if ( plane == 0 )
                {
                    next.x += c;
                    next.z += s;
                }
                else if ( plane == 1 )
                {
                    next.x += c;
                    next.y += s;
                }
                else
                {
                    next.y += c;
                    next.z += s;
                }

                if ( i > 0 )
                {
                    EmitReplayRibbonSegmentTo( ribbonData, previous, next, r, g, b, style, lane );
                }

                previous = next;
            }
        }

        return;
    }

    if ( const BoundingBox* box = GetShapeIf<BoundingBox>( &shape ) )
    {
        const Vector3& he = box->GetHalfExtents();
        const Vector3 center = position + rot * box->GetPosition();
        const Vector3 xAxis = rot * Vector3( he.x, 0.0f, 0.0f );
        const Vector3 yAxis = rot * Vector3( 0.0f, he.y, 0.0f );
        const Vector3 zAxis = rot * Vector3( 0.0f, 0.0f, he.z );
        const Vector3 corners[8] = {
            center - xAxis - yAxis - zAxis, center + xAxis - yAxis - zAxis, center + xAxis + yAxis - zAxis,
            center - xAxis + yAxis - zAxis, center - xAxis - yAxis + zAxis, center + xAxis - yAxis + zAxis,
            center + xAxis + yAxis + zAxis, center - xAxis + yAxis + zAxis,
        };

        static constexpr int kEdges[12][2] = {
            { 0, 1 }, { 1, 2 }, { 2, 3 }, { 3, 0 }, { 4, 5 }, { 5, 6 },
            { 6, 7 }, { 7, 4 }, { 0, 4 }, { 1, 5 }, { 2, 6 }, { 3, 7 },
        };

        for ( const auto& edge : kEdges )
        {
            EmitReplayRibbonSegmentTo( ribbonData, corners[edge[0]], corners[edge[1]], r, g, b, style, lane );
        }

        return;
    }

    if ( const ConvexHullShape* hull = GetShapeIf<ConvexHullShape>( &shape ) )
    {
        const Vector3 hullCenter = position + rot * hull->GetPosition();

        for ( uint16_t edgeIndex = 0; edgeIndex < hull->GetEdgeCount(); ++edgeIndex )
        {
            const ConvexHullEdge& edge = hull->GetEdge( edgeIndex );
            EmitReplayRibbonSegmentTo( ribbonData, hullCenter + rot * hull->GetVertex( edge.vertexA ),
                                       hullCenter + rot * hull->GetVertex( edge.vertexB ), r, g, b, style, lane );
        }
    }
}


void EditorTracer::BuildReplayRibbonVertices( const Vector3& cameraEye, const Vector3& cameraUp )
{
    static_cast<void>( cameraEye );
    static_cast<void>( cameraUp );

    auto updateRibbonData = [&]( const std::vector<float>& ribbonData,
                                std::vector<float>& vertexData, std::size_t& expandedSegmentCount )
    {
        const std::size_t sourceSegmentCount = ribbonData.size() / EDITOR_TRACER_REPLAY_RIBBON_FLOATS_PER_SEGMENT;

        if ( sourceSegmentCount < expandedSegmentCount )
        {
            vertexData.clear();
            expandedSegmentCount = 0;
        }

        if ( sourceSegmentCount == expandedSegmentCount )
        {
            return;
        }

        // The previously open segment gains one next-adjacency point when a
        // command is appended. Re-expand that tail plus only the new suffix;
        // every earlier vertex remains byte-for-byte retained.
        const std::size_t firstSegment = expandedSegmentCount > 0u ? expandedSegmentCount - 1u : 0u;
        const std::size_t retainedFloatCount = firstSegment * EDITOR_TRACER_REPLAY_RIBBON_VERTICES_PER_SEGMENT *
                                               EDITOR_TRACER_REPLAY_RIBBON_FLOATS_PER_VERTEX;

        vertexData.erase( vertexData.begin() + static_cast<std::ptrdiff_t>( retainedFloatCount ), vertexData.end() );

        for ( std::size_t i = firstSegment * EDITOR_TRACER_REPLAY_RIBBON_FLOATS_PER_SEGMENT;
              i + EDITOR_TRACER_REPLAY_RIBBON_FLOATS_PER_SEGMENT <= ribbonData.size();
              i += EDITOR_TRACER_REPLAY_RIBBON_FLOATS_PER_SEGMENT )
        {

            if ( vertexData.size() +
                     EDITOR_TRACER_REPLAY_RIBBON_VERTICES_PER_SEGMENT * EDITOR_TRACER_REPLAY_RIBBON_FLOATS_PER_VERTEX >
                 vertexData.capacity() )
            {
                return;
            }

            const Vector3 a( ribbonData[i + 0], ribbonData[i + 1], ribbonData[i + 2] );
            const Vector3 b( ribbonData[i + 3], ribbonData[i + 4], ribbonData[i + 5] );
            const float r = ribbonData[i + 6];
            const float g = ribbonData[i + 7];
            const float bl = ribbonData[i + 8];
            const float width = (std::max)( 0.02f, ribbonData[i + 9] );
            const float alpha = std::clamp( ribbonData[i + 10], 0.0f, 1.0f );
            const float edgeFeather = std::clamp( ribbonData[i + 11], 0.02f, 1.25f );
            const float emphasis = std::clamp( ribbonData[i + 12], 0.0f, 1.0f );

            Vector3 previous = a;
            Vector3 next = b;

            // Concept: adjacent trajectory segments share their outer points so
            // the shader can compute one screen-space join normal at the common
            // sample. Matching the complete style prevents unrelated path lanes
            // that merely touch at a collision point from being welded together;
            // color is intentionally excluded because it grades along one path.

            if ( i >= EDITOR_TRACER_REPLAY_RIBBON_FLOATS_PER_SEGMENT )
            {
                const std::size_t previousIndex = i - EDITOR_TRACER_REPLAY_RIBBON_FLOATS_PER_SEGMENT;
                const Vector3 previousEnd( ribbonData[previousIndex + 3], ribbonData[previousIndex + 4],
                                           ribbonData[previousIndex + 5] );

                const bool samePresentation = ribbonData[previousIndex + 9] == ribbonData[i + 9] &&
                                              ribbonData[previousIndex + 10] == ribbonData[i + 10] &&
                                              ribbonData[previousIndex + 11] == ribbonData[i + 11] &&
                                              ribbonData[previousIndex + 12] == ribbonData[i + 12];

                if ( samePresentation && VectorMagSquared( previousEnd - a ) <= TOLERANCE * TOLERANCE )
                {
                    previous = Vector3( ribbonData[previousIndex + 0], ribbonData[previousIndex + 1],
                                        ribbonData[previousIndex + 2] );
                }
            }

            const std::size_t nextIndex = i + EDITOR_TRACER_REPLAY_RIBBON_FLOATS_PER_SEGMENT;

            if ( nextIndex + EDITOR_TRACER_REPLAY_RIBBON_FLOATS_PER_SEGMENT <= ribbonData.size() )
            {
                const Vector3 nextStart( ribbonData[nextIndex + 0], ribbonData[nextIndex + 1], ribbonData[nextIndex + 2] );
                const bool samePresentation = ribbonData[nextIndex + 9] == ribbonData[i + 9] &&
                                              ribbonData[nextIndex + 10] == ribbonData[i + 10] &&
                                              ribbonData[nextIndex + 11] == ribbonData[i + 11] &&
                                              ribbonData[nextIndex + 12] == ribbonData[i + 12];

                if ( samePresentation && VectorMagSquared( nextStart - b ) <= TOLERANCE * TOLERANCE )
                {
                    next = Vector3( ribbonData[nextIndex + 3], ribbonData[nextIndex + 4], ribbonData[nextIndex + 5] );
                }
            }

            // Each emitted vertex carries the same adjacency-aware payload.
            // SV_VertexID still selects the endpoint and side in the shader.

            for ( int vertex = 0; vertex < 6; ++vertex )
            {

                // Why: all six shader-expanded vertices carry the same segment
                // record. Emitting that record here keeps the wire layout
                // visible without inventing a one-call parameter descriptor.
                vertexData.push_back( a.x );
                vertexData.push_back( a.y );
                vertexData.push_back( a.z );
                vertexData.push_back( b.x );
                vertexData.push_back( b.y );
                vertexData.push_back( b.z );
                vertexData.push_back( width );
                vertexData.push_back( r );
                vertexData.push_back( g );
                vertexData.push_back( bl );
                vertexData.push_back( alpha );
                vertexData.push_back( edgeFeather );
                vertexData.push_back( emphasis );
                vertexData.push_back( previous.x );
                vertexData.push_back( previous.y );
                vertexData.push_back( previous.z );
                vertexData.push_back( next.x );
                vertexData.push_back( next.y );
                vertexData.push_back( next.z );
            }
        }
        expandedSegmentCount = sourceSegmentCount;
    };

    // Invariant: ordinary replay paths may overflow without erasing causal
    // evidence. Priority ribbons are appended second; only the yellow entry box
    // remains on this ribbon path while rest/horizon boxes use priority lines.
    updateRibbonData( m_replayRibbonSegments, m_replayRibbonVertexData, m_expandedOrdinarySegmentCount );
    const std::size_t ordinaryVertexFloatCount = m_replayRibbonVertexData.size();
    updateRibbonData( m_priorityReplayRibbonSegments, m_priorityReplayRibbonVertexData, m_expandedPrioritySegmentCount );

    m_replaySubmissionStats = SkullbonezCore::Core::MainMemoryReplayTrajectorySubmissionStats {};

    // Invariant: the fidelity probe observes the same ordered floats consumed
    // by the render commands. Empty streams still have a count-bearing hash so
    // absence cannot alias a skipped sample in the golden manifest.
    HashReplaySubmissionFloatStream( m_lineData, m_replaySubmissionStats.ordinaryLineHash,
                                     m_replaySubmissionStats.ordinaryLineBytes );

    m_replaySubmissionStats.ordinaryLineVertexCount = static_cast<uint32_t>( m_lineData.size() / 6u );
    HashReplaySubmissionFloatStream( m_priorityLineData, m_replaySubmissionStats.priorityLineHash,
                                     m_replaySubmissionStats.priorityLineBytes );

    m_replaySubmissionStats.priorityLineCanonicalHash = HashReplaySubmissionCanonicalRecords( m_priorityLineData, 12u );
    m_replaySubmissionStats.priorityLineVertexCount = static_cast<uint32_t>( m_priorityLineData.size() / 6u );
    HashReplaySubmissionFloatStream( m_replayRibbonSegments, m_replaySubmissionStats.ordinaryRibbonHash,
                                     m_replaySubmissionStats.ordinaryRibbonBytes );

    m_replaySubmissionStats.ordinaryRibbonSegmentCount = static_cast<uint32_t>( m_replayRibbonSegments.size() / EDITOR_TRACER_REPLAY_RIBBON_FLOATS_PER_SEGMENT );

    HashReplaySubmissionFloatStream( m_priorityReplayRibbonSegments, m_replaySubmissionStats.priorityRibbonHash,
                                     m_replaySubmissionStats.priorityRibbonBytes );

    m_replaySubmissionStats.priorityRibbonCanonicalHash = HashReplaySubmissionCanonicalRecords( m_priorityReplayRibbonSegments, EDITOR_TRACER_REPLAY_RIBBON_FLOATS_PER_SEGMENT );

    m_replaySubmissionStats.priorityRibbonSegmentCount = static_cast<uint32_t>( m_priorityReplayRibbonSegments.size() / EDITOR_TRACER_REPLAY_RIBBON_FLOATS_PER_SEGMENT );

    m_replaySubmissionStats.hasGeometry = !m_lineData.empty() || !m_priorityLineData.empty() ||
                                          !m_replayRibbonSegments.empty() || !m_priorityReplayRibbonSegments.empty();

    if ( !m_replayRibbonVertexData.empty() || !m_priorityReplayRibbonVertexData.empty() )
    {

        // Invariant: Stage-9 flicker validation hashes the exact float payload
        // submitted to DrawTransientColoredTriangles. It deliberately ignores
        // vector capacity and camera data because the trajectory-ribbon shader
        // performs camera-facing expansion from this stable segment payload.
        const std::size_t combinedVertexFloatCount = m_replayRibbonVertexData.size() +
                                                     m_priorityReplayRibbonVertexData.size();

        const std::size_t byteCount = combinedVertexFloatCount * sizeof( float );
        m_replaySubmissionStats.vertexHash = HashReplaySubmissionFloatStreams( m_replayRibbonVertexData,
                                                                               m_priorityReplayRibbonVertexData );

        m_replaySubmissionStats.ordinaryVertexBytes = ordinaryVertexFloatCount * sizeof( float );
        m_replaySubmissionStats.ordinaryVertexCount = static_cast<uint32_t>( ordinaryVertexFloatCount /
                                                                             EDITOR_TRACER_REPLAY_RIBBON_FLOATS_PER_VERTEX );

        uint64_t ordinaryHash = REPLAY_TRAJECTORY_SUBMISSION_FNV_OFFSET;
        const uint64_t ordinaryFloatCount = static_cast<uint64_t>( ordinaryVertexFloatCount );
        HashReplaySubmissionBytes( ordinaryHash, SkullbonezCore::Core::ObjectBytes( ordinaryFloatCount ) );

        if ( ordinaryVertexFloatCount > 0u )
        {
            HashReplaySubmissionBytes( ordinaryHash,
                                       SkullbonezCore::Core::ObjectBytes( std::span<const float>( m_replayRibbonVertexData )
                                                                              .first( ordinaryVertexFloatCount ) ) );
        }

        m_replaySubmissionStats.ordinaryVertexHash = ordinaryHash;
        m_replaySubmissionStats.vertexBytes = static_cast<uint64_t>( byteCount );
        m_replaySubmissionStats.vertexCount = static_cast<uint32_t>( combinedVertexFloatCount /
                                                                     EDITOR_TRACER_REPLAY_RIBBON_FLOATS_PER_VERTEX );

        m_replaySubmissionStats.segmentCount = static_cast<uint32_t>( m_replaySubmissionStats.vertexCount /
                                                                      EDITOR_TRACER_REPLAY_RIBBON_VERTICES_PER_SEGMENT );
    }
}


void EditorTracer::AddPlacementRay( const Vector3& rayOrigin, const Vector3& hitPoint )
{
    EmitLine( rayOrigin, hitPoint, 0.25f, 0.80f, 1.0f );
}


void EditorTracer::AddPlacementGhost( int objectType, const Vector3& center, const Vector3& terrainPoint,
                                      const Vector3& placementScale, const Quaternion& orientation,
                                      const Assets::AssetSystem& assets )
{
    const int type = std::clamp( objectType, 0, SkullbonezCore::UI::EditorTab::OBJECT_TYPE_COUNT - 1 );
    const Vector3 scale = EditorClampPlacementScale( type, placementScale );
    Quaternion orientationCopy = orientation;
    const RotationMatrix rotation = orientationCopy.GetOrientationMatrix();
    constexpr float ghostR = 0.25f;
    constexpr float ghostG = 1.0f;
    constexpr float ghostB = 0.85f;

    if ( const EditorTreeDefinition* tree = EditorTreeDefinitionForType( type ) )
    {
        const Vector3 base = terrainPoint + rotation * Vector3( 0.0f, EDITOR_PLACEMENT_SURFACE_EPSILON, 0.0f );

        for ( int partIndex = 0; partIndex < tree->partCount; ++partIndex )
        {
            const EditorTreePartDefinition& part = tree->parts[partIndex];
            const ConvexHullShape* hull = CachedEditorHullForAsset( m_resultDiagnostics, part.hullAsset );

            if ( !hull )
            {
                continue;
            }

            const Vector3 hullCenter = base + rotation * ( Vector3( part.offsetX, part.offsetY, part.offsetZ ) +
                                                           HullAuthoredLocalOffset( *hull ) );

            for ( uint16_t edgeIndex = 0; edgeIndex < hull->GetEdgeCount(); ++edgeIndex )
            {
                const ConvexHullEdge& edge = hull->GetEdge( edgeIndex );
                EmitLine( hullCenter + rotation * hull->GetVertex( edge.vertexA ),
                          hullCenter + rotation * hull->GetVertex( edge.vertexB ), ghostR, ghostG, ghostB );
            }
        }

        return;
    }

    if ( EditorBuildingDefinitionForType( type ) )
    {
        const Vector3 base = terrainPoint + rotation * Vector3( 0.0f, EDITOR_PLACEMENT_SURFACE_EPSILON, 0.0f );
        ForEachEditorBuildingPart( type, assets,
                                   [&]( const Json& part )
                                   {
                                       const Vector3 offset = EditorJsonVec3Or( part, "offset",
                                                                                Vector3( 0.0f, 0.0f, 0.0f ) );

                                       const Quaternion partOrientation = EditorBuildingPartOrientation( orientation, part );

                                       Quaternion partCopy = partOrientation;
                                       const RotationMatrix partRotation = partCopy.GetOrientationMatrix();
                                       const Vector3 bodyCenter = base + rotation * offset;
                                       const std::string primitiveType = EditorAssetPrimitiveType( part );

                                       if ( primitiveType == "convexHull" )
                                       {
                                           const std::string hullPath = EditorJsonStringOr( part, "hull", "" );
                                           const ConvexHullShape* hull = hullPath.empty()
                                                                             ? nullptr
                                                                             : CachedEditorBuildingHull( m_resultDiagnostics,
                                                                                                         hullPath );

                                           if ( !hull )
                                           {
                                               return;
                                           }

                                           const Vector3 hullCenter = bodyCenter +
                                                                      partRotation * ( hull->GetAuthoredCenterOfMass() +
                                                                                       hull->GetPosition() );

                                           for ( uint16_t edgeIndex = 0; edgeIndex < hull->GetEdgeCount(); ++edgeIndex )
                                           {
                                               const ConvexHullEdge& edge = hull->GetEdge( edgeIndex );
                                               EmitLine( hullCenter + partRotation * hull->GetVertex( edge.vertexA ),
                                                         hullCenter + partRotation * hull->GetVertex( edge.vertexB ), ghostR,
                                                         ghostG, ghostB );
                                           }

                                           return;
                                       }

                                       if ( primitiveType == "box" )
                                       {
                                           Vector3 halfExtents;

                                           if ( !TryReadEditorBoxHalfExtents( part, halfExtents ) )
                                           {
                                               return;
                                           }

                                           EmitBox( bodyCenter, partRotation * Vector3( halfExtents.x, 0.0f, 0.0f ),
                                                    partRotation * Vector3( 0.0f, halfExtents.y, 0.0f ),
                                                    partRotation * Vector3( 0.0f, 0.0f, halfExtents.z ), ghostR, ghostG,
                                                    ghostB );

                                           return;
                                       }

                                       if ( primitiveType == "sphere" )
                                       {
                                           float radius = 0.0f;

                                           if ( TryReadEditorSphereRadius( part, radius ) )
                                           {
                                               EmitSphere( bodyCenter, radius, ghostR, ghostG, ghostB );
                                           }
                                       }
                                   } );

        return;
    }

    if ( const EditorHouseDefinition* house = EditorHouseDefinitionForType( type ) )
    {
        const Vector3 base = terrainPoint + rotation * Vector3( 0.0f, EDITOR_PLACEMENT_SURFACE_EPSILON, 0.0f );

        for ( int partIndex = 0; partIndex < house->partCount; ++partIndex )
        {
            const EditorHousePartDefinition& part = house->parts[partIndex];
            const Vector3 partCenter = base + rotation * Vector3( part.offsetX, part.offsetY, part.offsetZ );
            EmitBox( partCenter, rotation * Vector3( part.halfX, 0.0f, 0.0f ), rotation * Vector3( 0.0f, part.halfY, 0.0f ),
                     rotation * Vector3( 0.0f, 0.0f, part.halfZ ), ghostR, ghostG, ghostB );
        }

        return;
    }

    switch ( type )
    {
    case SkullbonezCore::UI::EditorTab::OBJECT_BOX:
        EmitBox( center, rotation * Vector3( scale.x, 0.0f, 0.0f ), rotation * Vector3( 0.0f, scale.y, 0.0f ),
                 rotation * Vector3( 0.0f, 0.0f, scale.z ), ghostR, ghostG, ghostB );

        break;
    case SkullbonezCore::UI::EditorTab::OBJECT_BALL:
        EmitSphere( center, scale.x, ghostR, ghostG, ghostB );
        break;
    case SkullbonezCore::UI::EditorTab::OBJECT_SPHERE:
        EmitSphere( center, scale.x, ghostR, ghostG, ghostB );
        break;
    case SkullbonezCore::UI::EditorTab::OBJECT_RAGDOLL:
    case SkullbonezCore::UI::EditorTab::OBJECT_RAGDOLL_SLEEP:
        Ragdoll::AddPreviewLines( m_lineData, terrainPoint, scale.x, orientation, ghostR, ghostG, ghostB );
        break;
    default:
    {
        ConvexHullShape hull;

        if ( !TryBuildScaledEditorHullForType( m_resultDiagnostics, type, scale, hull ) )
        {
            return;
        }

        const Vector3 hullCenter = center + rotation * hull.GetPosition();

        for ( uint16_t edgeIndex = 0; edgeIndex < hull.GetEdgeCount(); ++edgeIndex )
        {
            const ConvexHullEdge& edge = hull.GetEdge( edgeIndex );
            EmitLine( hullCenter + rotation * hull.GetVertex( edge.vertexA ),
                      hullCenter + rotation * hull.GetVertex( edge.vertexB ), ghostR, ghostG, ghostB );
        }

        break;
    }
    }
}


void EditorTracer::AddRayCastTestLine( const Vector3& start, const Vector3& end, float alpha, bool hit )
{
    alpha = std::clamp( alpha, 0.0f, 1.0f );

    if ( alpha <= 0.0f )
    {
        return;
    }

    const float r = hit ? 1.0f : 0.35f;
    const float g = hit ? 0.34f : 0.72f;
    const float b = hit ? 0.12f : 1.0f;
    EmitLine( start, end, r * alpha, g * alpha, b * alpha );
}

void EditorTracer::AddReplayPathSegment( const Vector3& start, const Vector3& end, float r, float g, float b,
                                         SkullbonezCore::Core::MainMemoryReplayTrajectoryLane lane, float emphasis )
{
    ReplayRibbonStyle glow = m_replayPathStyle;
    ReplayRibbonStyle core = m_replayPathStyle;

    // Invariant: only the replay presentation owner may opt a segment into the
    // shader's halo and bloom-feed branch. All generic editor and non-selected
    // replay paths arrive through the zero-emphasis default.
    const float boundedEmphasis = std::clamp( emphasis, 0.0f, 1.0f ) * m_replaySelectedEmphasis;
    glow.emphasis = boundedEmphasis;
    core.emphasis = boundedEmphasis;
    EmitReplayRibbonGlowPairTo( m_replayRibbonSegments, start, end, r, g, b, glow, core, lane );
}


void EditorTracer::AddReplayCausalTrailSegment( const Vector3& start, const Vector3& end, float r, float g, float b )
{

    // Why: retained causal trails are the evidence attached to yellow/grey/ghost
    // boxes. They live with the priority ribbons so overflow in ordinary root
    // path rendering cannot leave a marker without its sampled route.
    const ReplayRibbonStyle glow = m_replayCausalStyle;
    const ReplayRibbonStyle core = m_replayCausalStyle;
    EmitReplayRibbonGlowPairTo( m_priorityReplayRibbonSegments, start, end, r, g, b, glow, core,
                                SkullbonezCore::Core::MainMemoryReplayTrajectoryLane::RetainedTrail );
}


void EditorTracer::AddReplayBaselinePathSegment( const Vector3& start, const Vector3& end, float r, float g, float b,
                                                 float opacity )
{
    ReplayRibbonStyle glow = m_replayBaselineStyle;
    ReplayRibbonStyle core = m_replayBaselineStyle;
    glow.alpha *= std::clamp( opacity, 0.0f, 1.0f );
    core.alpha = glow.alpha;
    EmitReplayRibbonGlowPairTo( m_replayRibbonSegments, start, end, r, g, b, glow, core,
                                SkullbonezCore::Core::MainMemoryReplayTrajectoryLane::BaselineRoot );
}


void EditorTracer::AddReplayContactMarker( const Vector3& point, const Vector3& normal, float r, float g, float b )
{
    constexpr float crossSize = 0.55f;
    EmitLine( point - Vector3( crossSize, 0.0f, 0.0f ), point + Vector3( crossSize, 0.0f, 0.0f ), r, g, b );
    EmitLine( point - Vector3( 0.0f, crossSize, 0.0f ), point + Vector3( 0.0f, crossSize, 0.0f ), r, g, b );
    EmitLine( point - Vector3( 0.0f, 0.0f, crossSize ), point + Vector3( 0.0f, 0.0f, crossSize ), r, g, b );

    if ( VectorMagSquared( normal ) > TOLERANCE * TOLERANCE )
    {
        EmitArrow( point, point + normal * 1.8f, r, g, b );
    }
}


void EditorTracer::AddReplayImpulseVector( const Vector3& point, const Vector3& impulse, float r, float g, float b )
{
    const float magSq = VectorMagSquared( impulse );

    if ( magSq <= TOLERANCE * TOLERANCE )
    {
        return;
    }

    Vector3 direction = impulse;
    const float magnitude = sqrtf( magSq );
    direction /= magnitude;
    const float length = std::clamp( sqrtf( magnitude ) * 3.0f, 1.8f, 12.0f );
    EmitArrow( point, point + direction * length, r, g, b );
}


void EditorTracer::AddReplayCausalEntryMarker( const Vector3& position, const Quaternion& orientation,
                                               const CollisionShapeReference& shape )
{

    // Why: yellow always means "joined the causal tree here". Keep it as the
    // only marker on the ribbon shader, but emit one logical segment style so
    // marker outlines do not double the retained ribbon budget.
    const ReplayRibbonStyle singlePass = m_replayMarkerStyle;
    EmitReplayRibbonShapeOutlineTo( m_priorityReplayRibbonSegments, position, orientation, shape, 1.0f, 0.85f, 0.25f,
                                    singlePass, SkullbonezCore::Core::MainMemoryReplayTrajectoryLane::CausalMarker );
}


void EditorTracer::AddReplayCausalRestMarker( const Vector3& position, const Quaternion& orientation,
                                              const CollisionShapeReference& shape )
{
    EmitShapeOutlineTo( m_priorityLineData, position, orientation, shape, 0.58f, 0.58f, 0.62f );
}


void EditorTracer::AddReplayCausalHorizonMarker( const Vector3& position, const Quaternion& orientation,
                                                 const CollisionShapeReference& shape )
{

    // Concept: horizon ghosts are not landings. They mark "this is where the
    // prediction buffer ends" for a body still mid-flight, so the color stays
    // distinct from grey resting boxes.
    EmitShapeOutlineTo( m_priorityLineData, position, orientation, shape, 0.45f, 0.92f, 1.0f );
}


void EditorTracer::AddReplayBaselineEntryMarker( const Vector3& position, const Quaternion& orientation,
                                                 const CollisionShapeReference& shape )
{

    // Concept: cold baseline markers are the old future's footprint. They stay
    // on the wire path so cyan boxes do not compete with selected-path halos.
    EmitShapeOutline( position, orientation, shape, 0.26f, 0.78f, 0.95f );
}


void EditorTracer::AddReplayBaselineRestMarker( const Vector3& position, const Quaternion& orientation,
                                                const CollisionShapeReference& shape )
{
    EmitShapeOutline( position, orientation, shape, 0.18f, 0.62f, 0.78f );
}


void EditorTracer::AddReplayTargetMarker( const Vector3& position, const Quaternion& orientation,
                                          const CollisionShapeReference& shape, float radius )
{
    AddSelectionOutline( position, orientation, shape );
    EmitRing( position, 1, (std::max)( 1.0f, radius ), 1.0f, 1.0f, 1.0f );
}


void EditorTracer::AddAttachedCameraTargetMarker( const Vector3& position, const Quaternion& orientation,
                                                  const CollisionShapeReference& shape, float radius, bool activeFollow )
{
    AddSelectionOutline( position, orientation, shape );
    radius = (std::max)( 1.0f, radius );
    const float r = activeFollow ? 0.16f : 1.0f;
    const float g = activeFollow ? 1.0f : 0.72f;
    const float b = activeFollow ? 0.92f : 0.24f;
    EmitRing( position, 1, radius, r, g, b );
    EmitRing( position, 0, radius * 0.68f, r, g, b );
}


void EditorTracer::AddSelectionOutline( const Vector3& position, const Quaternion& orientation,
                                        const CollisionShapeReference& shape )
{
    constexpr float outlineR = 1.0f;
    constexpr float outlineG = 1.0f;
    constexpr float outlineB = 0.55f;
    EmitShapeOutline( position, orientation, shape, outlineR, outlineG, outlineB );
}


void EditorTracer::AddGizmo( const Vector3& origin, float radius, int hotTranslateAxis, int hotRotationAxis, int activeAxis,
                             bool activeRotation, bool scaleMode, bool activeScale )
{

    // Concept: Translate and scale share axis lines, while rotate owns rings.
    // Keeping both in one tracer method makes hover/active color priority
    // identical for editor placement and replay velocity overlays.
    const float length = EditorGizmoAxisLength( radius );

    for ( int axis = 0; axis < 3; ++axis )
    {
        float r = axis == 0 ? 1.0f : 0.08f;
        float g = axis == 1 ? 0.95f : 0.10f;
        float b = axis == 2 ? 1.0f : 0.08f;

        if ( ( activeScale || ( !scaleMode && !activeRotation ) ) && activeAxis == axis )
        {
            r = 1.0f;
            g = 1.0f;
            b = 0.15f;
        }
        else if ( hotTranslateAxis == axis )
        {
            r = (std::min)( 1.0f, r + 0.45f );
            g = (std::min)( 1.0f, g + 0.45f );
            b = (std::min)( 1.0f, b + 0.45f );
        }

        const Vector3 axisVector = EditorAxisVector( axis );
        const Vector3 endpoint = origin + axisVector * length;

        if ( scaleMode || activeScale )
        {
            const float handle = (std::max)( 0.75f, length * 0.045f );
            EmitLine( origin, endpoint, r, g, b );
            EmitBox( endpoint, Vector3( handle, 0.0f, 0.0f ), Vector3( 0.0f, handle, 0.0f ), Vector3( 0.0f, 0.0f, handle ),
                     r, g, b );
        }
        else
        {
            EmitArrow( origin, endpoint, r, g, b );
        }
    }

    if ( scaleMode || activeScale )
    {
        return;
    }

    const float ringRadius = EditorGizmoRotationRadius( radius );

    for ( int axis = 0; axis < 3; ++axis )
    {
        float r = axis == 0 ? 1.0f : 0.08f;
        float g = axis == 1 ? 0.95f : 0.10f;
        float b = axis == 2 ? 1.0f : 0.08f;

        if ( activeRotation && activeAxis == axis )
        {
            r = 1.0f;
            g = 1.0f;
            b = 0.15f;
        }
        else if ( hotRotationAxis == axis )
        {
            r = (std::min)( 1.0f, r + 0.45f );
            g = (std::min)( 1.0f, g + 0.45f );
            b = (std::min)( 1.0f, b + 0.45f );
        }

        EmitRing( origin, axis, ringRadius, r, g, b );
    }
}


void EditorTracer::AddReplayVelocityGizmo( const Vector3& origin, const Quaternion& orientation,
                                           const CollisionShapeReference& shape, float radius, const Vector3& linearVelocity,
                                           const Vector3& angularVelocity, int hotLinearAxis, int hotAngularAxis,
                                           int activeAxis, bool activeAngular )
{
    AddSelectionOutline( origin, orientation, shape );

    const float baseLength = ReplayVelocityLinearBaseLength( radius );

    for ( int axis = 0; axis < 3; ++axis )
    {
        const Vector3 axisVector = EditorAxisVector( axis );
        const float component = ReplayVelocityAxisComponent( linearVelocity, axis );
        const float heat = std::clamp( fabsf( component ) / REPLAY_VELOCITY_EDIT_LINEAR_MAX, 0.0f, 1.0f );
        const bool hot = hotLinearAxis == axis;
        const bool active = !activeAngular && activeAxis == axis;
        float r = 0.0f;
        float g = 0.0f;
        float b = 0.0f;
        ReplayVelocityAxisColor( axis, heat, hot, active, r, g, b );

        const float axisT = ReplayVelocityLinearVisualAxisT( radius, component );
        const Vector3 endpoint = origin + axisVector * axisT;
        EmitLine( origin - axisVector * ( baseLength * 0.24f ), origin + axisVector * ( baseLength * 0.24f ), r * 0.34f,
                  g * 0.34f, b * 0.34f );

        EmitArrow( origin, endpoint, r, g, b );
    }

    for ( int axis = 0; axis < 3; ++axis )
    {
        const float component = ReplayVelocityAxisComponent( angularVelocity, axis );
        const float heat = std::clamp( fabsf( component ) / REPLAY_VELOCITY_EDIT_ANGULAR_MAX, 0.0f, 1.0f );
        const bool hot = hotAngularAxis == axis;
        const bool active = activeAngular && activeAxis == axis;
        float r = 0.0f;
        float g = 0.0f;
        float b = 0.0f;
        ReplayVelocityAxisColor( axis, heat, hot, active, r, g, b );
        EmitRing( origin, axis, ReplayVelocityAngularVisualRadius( radius, component ), r, g, b );
    }
}


void EditorTracer::Render( const ReplayVisualPacket& packet, const Matrix4& viewProjection,
                           Rendering::Dx12GeometryOwner& renderCommands )
{

    if ( !packet.HasGeometry() )
    {
        return;
    }

    const Rendering::RetainedGeometryStreamToken retainedStream = { packet.retainedPredictionStreamId,
                                                                    packet.retainedPredictionRevision };

    if ( !packet.retainedPredictionOrdinaryLines.empty() )
    {
        renderCommands.DrawRetainedLinesColored( packet.retainedPredictionOrdinaryLines, retainedStream, false,
                                                 viewProjection, REPLAY_LINE_RASTER );
    }

    if ( !packet.retainedPredictionPriorityLines.empty() )
    {
        renderCommands.DrawRetainedLinesColored( packet.retainedPredictionPriorityLines, retainedStream, true,
                                                 viewProjection, REPLAY_LINE_RASTER );
    }

    if ( !packet.combinedLines.empty() )
    {

        // Invariant: combinedLines stores colored vertices as xyz/rgb floats; every
        // pair of vertices is one line segment consumed by DrawLinesColored.
        renderCommands.DrawLinesColored( packet.combinedLines, viewProjection, REPLAY_LINE_RASTER );
    }

    if ( !packet.retainedPredictionRibbonVertices.empty() )
    {

        // The retained lane owns a frame-fenced GPU buffer. Stream/revision
        // changes refresh the affected slot; stable frames submit these two
        // draws without reserving or copying geometry upload memory.
        renderCommands.DrawRetainedGeometryRibbon( packet.retainedPredictionRibbonVertices, retainedStream, false,
                                                   viewProjection,
                                                   Rendering::TransientTriangleStyle::InstancedRibbonDepthHint,
                                                   REPLAY_RIBBON_DEPTH_HINT_RASTER );
    }

    if ( !packet.retainedPredictionPriorityRibbonVertices.empty() )
    {
        renderCommands.DrawRetainedGeometryRibbon( packet.retainedPredictionPriorityRibbonVertices, retainedStream, true,
                                                   viewProjection,
                                                   Rendering::TransientTriangleStyle::InstancedRibbonDepthHint,
                                                   REPLAY_RIBBON_DEPTH_HINT_RASTER );
    }

    if ( !packet.retainedPredictionRibbonRanges.empty() )
    {
        renderCommands.DrawRetainedGeometryRanges( packet.retainedPredictionCompactRibbonRecords,
                                                   packet.retainedPredictionRibbonRanges, retainedStream, viewProjection,
                                                   Rendering::TransientTriangleStyle::InstancedRibbonDepthHint,
                                                   REPLAY_RIBBON_DEPTH_HINT_RASTER );
    }

    if ( !packet.retainedPredictionRibbonVertices.empty() )
    {
        renderCommands.DrawRetainedGeometryRibbon( packet.retainedPredictionRibbonVertices, retainedStream, false,
                                                   viewProjection, Rendering::TransientTriangleStyle::InstancedRibbon,
                                                   REPLAY_RIBBON_VISIBLE_RASTER );
    }

    if ( !packet.retainedPredictionPriorityRibbonVertices.empty() )
    {
        renderCommands.DrawRetainedGeometryRibbon( packet.retainedPredictionPriorityRibbonVertices, retainedStream, true,
                                                   viewProjection, Rendering::TransientTriangleStyle::InstancedRibbon,
                                                   REPLAY_RIBBON_VISIBLE_RASTER );
    }

    if ( !packet.retainedPredictionRibbonRanges.empty() )
    {
        renderCommands.DrawRetainedGeometryRanges( packet.retainedPredictionCompactRibbonRecords,
                                                   packet.retainedPredictionRibbonRanges, retainedStream, viewProjection,
                                                   Rendering::TransientTriangleStyle::InstancedRibbon,
                                                   REPLAY_RIBBON_VISIBLE_RASTER );
    }

    if ( !packet.expandedRibbonVertices.empty() || !packet.priorityExpandedRibbonVertices.empty() )
    {

        // Concept: the first pass is a low-opacity depth hint with depth
        // testing disabled; the normal pass is depth-tested, so visible
        // strokes stay seated while occluded spans remain only faintly
        // readable behind scene geometry.

        if ( !packet.expandedRibbonVertices.empty() )
        {
            renderCommands.DrawTransientColoredTriangles( packet.expandedRibbonVertices, viewProjection,
                                                          Rendering::TransientTriangleStyle::InstancedRibbonDepthHint,
                                                          REPLAY_RIBBON_DEPTH_HINT_RASTER );
        }

        if ( !packet.priorityExpandedRibbonVertices.empty() )
        {
            renderCommands.DrawTransientColoredTriangles( packet.priorityExpandedRibbonVertices, viewProjection,
                                                          Rendering::TransientTriangleStyle::InstancedRibbonDepthHint,
                                                          REPLAY_RIBBON_DEPTH_HINT_RASTER );
        }

        if ( !packet.expandedRibbonVertices.empty() )
        {
            renderCommands.DrawTransientColoredTriangles( packet.expandedRibbonVertices, viewProjection,
                                                          Rendering::TransientTriangleStyle::InstancedRibbon,
                                                          REPLAY_RIBBON_VISIBLE_RASTER );
        }

        if ( !packet.priorityExpandedRibbonVertices.empty() )
        {
            renderCommands.DrawTransientColoredTriangles( packet.priorityExpandedRibbonVertices, viewProjection,
                                                          Rendering::TransientTriangleStyle::InstancedRibbon,
                                                          REPLAY_RIBBON_VISIBLE_RASTER );
        }
    }
}
