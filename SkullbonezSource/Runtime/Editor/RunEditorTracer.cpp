/*
File: SkullbonezSource/Runtime/Editor/RunEditorTracer.cpp
Purpose:
  Implements runtime editor overlay tracer primitives and draw submission.

Summary:
  The tracer turns editor/replay tool state into transient colored lines and replay
  ribbons. It observes state prepared elsewhere and should not mutate selection,
  physics, or replay ownership.

Glossary:
  Tracer: Per-frame line builder for placement rays, gizmos, replay paths, and selection outlines.
  Gizmo: World-space translate, rotate, or scale affordance drawn over selected models.
  Selection outline: Shape-accurate wire outline drawn from explicit pose and
    collision-shape values supplied by the owning tool.
  Replay target marker: Replay overlay outline/ring drawn from explicit
    body-store pose and collider-store shape/radius values.
  Replay future marker: Shape-accurate downstream collision outline drawn at
    the latest visible predicted/retained pose, never from a broadphase radius substitute.
  Replay ribbon: Screen-space-width strip generated from replay path segments;
    its pixel shader gives every path an analytic edge and only selected paths
    an emphasis halo.
  FNV (Fowler-Noll-Vo): Small deterministic byte-stream hash used here for
    validation evidence, not for security.
  Placement ghost: Preview outline drawn before an editor placement commit; it
    must match the primitive bodies that placement will actually spawn.

Invariants:
  - Trace generation must stay transient; line buffers are cleared every frame by the caller.
  - Replay causal markers use priority overlay storage so expensive prediction
    paths can degrade without erasing already-revealed boxes.
  - The tracer owns fixed-capacity overlay buffers and must not allocate while
    building a frame.

Related:
  - SkullbonezSource/Runtime/Tools/RuntimeTools.h
  - SkullbonezSource/Runtime/Editor/EditorOverlayTools.h
  - SkullbonezSource/Runtime/Editor/EditorPlacementAssets.h
  - SkullbonezSource/Runtime/Editor/EditorTools.h
  - SkullbonezSource/Runtime/Editor/RunEditorTools.cpp
  - Agentic/Reference/comment-style-guide.md
*/
#include "EditorPlacementAssets.h"
#include "EditorTools.h"
#include "../Tools/RuntimeTools.h"
#include "../../Physics/CollisionShape.h"
#include "../../Physics/Ragdoll.h"
#include "../../Core/Config.h"
#include "../../Rendering/IRenderCommandContext.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

using namespace SkullbonezCore::Runtime;
using namespace SkullbonezCore::Runtime::RunInternal;
using namespace SkullbonezCore::Math::CollisionDetection;
using namespace SkullbonezCore::Math::Orientation;
using namespace SkullbonezCore::Math::Transformation;
using SkullbonezCore::Math::Vector::Vector3;
using SkullbonezCore::Physics::Ragdoll;
using Json = SkullbonezCore::Runtime::RunInternal::EditorPlacementJson;

namespace
{
constexpr std::size_t RUN_EDITOR_TRACER_LINE_FLOAT_CAPACITY = 262144;
constexpr std::size_t RUN_EDITOR_TRACER_PRIORITY_LINE_FLOAT_CAPACITY = 524288;
constexpr std::size_t RUN_EDITOR_TRACER_FLOATS_PER_LINE = 12;
// Why: the Stage-9 frozen prediction probe submitted 21,568 replay ribbon
// segments. The configured 27,000-segment/162,000-vertex ceiling adds 25.2%
// headroom; the 19-float adjacency payload uses 23.5 MiB across the depth-hint
// and visible passes, remaining inside the 32 MiB frame arena.
// Ordinary paths get 24,000 slots and causal priority evidence keeps 3,000.
constexpr std::size_t RUN_EDITOR_TRACER_REPLAY_RIBBON_SEGMENT_BUDGET = 27000;
constexpr std::size_t RUN_EDITOR_TRACER_REPLAY_RIBBON_ORDINARY_SEGMENT_CAPACITY = 24000;
constexpr std::size_t RUN_EDITOR_TRACER_REPLAY_RIBBON_PRIORITY_SEGMENT_CAPACITY =
    RUN_EDITOR_TRACER_REPLAY_RIBBON_SEGMENT_BUDGET - RUN_EDITOR_TRACER_REPLAY_RIBBON_ORDINARY_SEGMENT_CAPACITY;
constexpr std::size_t RUN_EDITOR_TRACER_REPLAY_RIBBON_FLOATS_PER_SEGMENT = 13;
constexpr std::size_t RUN_EDITOR_TRACER_REPLAY_RIBBON_ORDINARY_FLOAT_CAPACITY =
    RUN_EDITOR_TRACER_REPLAY_RIBBON_ORDINARY_SEGMENT_CAPACITY * RUN_EDITOR_TRACER_REPLAY_RIBBON_FLOATS_PER_SEGMENT;
constexpr std::size_t RUN_EDITOR_TRACER_REPLAY_RIBBON_PRIORITY_FLOAT_CAPACITY =
    RUN_EDITOR_TRACER_REPLAY_RIBBON_PRIORITY_SEGMENT_CAPACITY * RUN_EDITOR_TRACER_REPLAY_RIBBON_FLOATS_PER_SEGMENT;
constexpr std::size_t RUN_EDITOR_TRACER_REPLAY_RIBBON_FLOATS_PER_VERTEX = 19;
constexpr std::size_t RUN_EDITOR_TRACER_REPLAY_RIBBON_VERTICES_PER_SEGMENT = 6;
constexpr std::size_t RUN_EDITOR_TRACER_REPLAY_RIBBON_VERTEX_FLOAT_CAPACITY =
    RUN_EDITOR_TRACER_REPLAY_RIBBON_SEGMENT_BUDGET * RUN_EDITOR_TRACER_REPLAY_RIBBON_VERTICES_PER_SEGMENT *
    RUN_EDITOR_TRACER_REPLAY_RIBBON_FLOATS_PER_VERTEX;
constexpr float RUN_EDITOR_TRACER_REPLAY_LINE_OPACITY = 0.5f;
constexpr uint64_t REPLAY_TRAJECTORY_SUBMISSION_FNV_OFFSET = 1469598103934665603ull;
constexpr uint64_t REPLAY_TRAJECTORY_SUBMISSION_FNV_PRIME = 1099511628211ull;

void HashReplaySubmissionBytes( uint64_t& hash, const void* data, std::size_t byteCount )
{
    const uint8_t* bytes = static_cast<const uint8_t*>( data );
    for ( std::size_t i = 0; i < byteCount; ++i )
    {
        hash ^= static_cast<uint64_t>( bytes[i] );
        hash *= REPLAY_TRAJECTORY_SUBMISSION_FNV_PRIME;
    }
}

void HashReplaySubmissionFloatStream( const std::vector<float>& values, uint64_t& outHash, uint64_t& outBytes )
{
    outHash = REPLAY_TRAJECTORY_SUBMISSION_FNV_OFFSET;
    const uint64_t floatCount = static_cast<uint64_t>( values.size() );
    HashReplaySubmissionBytes( outHash, &floatCount, sizeof( floatCount ) );
    outBytes = floatCount * sizeof( float );
    if ( !values.empty() )
    {
        HashReplaySubmissionBytes( outHash, values.data(), static_cast<std::size_t>( outBytes ) );
    }
}

uint64_t HashReplaySubmissionCanonicalRecords( const std::vector<float>& values, std::size_t floatsPerRecord )
{
    uint64_t sum = 0;
    uint64_t mixedSum = 0;
    uint64_t recordCount = 0;
    for ( std::size_t index = 0; index + floatsPerRecord <= values.size(); index += floatsPerRecord )
    {
        uint64_t recordHash = REPLAY_TRAJECTORY_SUBMISSION_FNV_OFFSET;
        HashReplaySubmissionBytes( recordHash, values.data() + index, floatsPerRecord * sizeof( float ) );
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
    HashReplaySubmissionBytes( hash, &recordCount, sizeof( recordCount ) );
    HashReplaySubmissionBytes( hash, &sum, sizeof( sum ) );
    HashReplaySubmissionBytes( hash, &mixedSum, sizeof( mixedSum ) );
    return hash;
}

// Value payload encoded identically into each of a segment's six vertices.
// Vector references are consumed synchronously and never stored by the tracer.
struct ReplayRibbonVertexDesc
{
    const Vector3& previous;
    const Vector3& start;
    const Vector3& end;
    const Vector3& next;
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
    float alpha = 0.0f;
    float width = 0.0f;
    float edgeFeather = 0.0f;
    float emphasis = 0.0f;
};

void AppendReplayRibbonVertex( std::vector<float>& vertexData, const ReplayRibbonVertexDesc& desc )
{
    const Vector3& previous = desc.previous;
    const Vector3& start = desc.start;
    const Vector3& end = desc.end;
    const Vector3& next = desc.next;
    vertexData.push_back( start.x );
    vertexData.push_back( start.y );
    vertexData.push_back( start.z );
    vertexData.push_back( end.x );
    vertexData.push_back( end.y );
    vertexData.push_back( end.z );
    vertexData.push_back( desc.width );
    vertexData.push_back( desc.r );
    vertexData.push_back( desc.g );
    vertexData.push_back( desc.b );
    vertexData.push_back( desc.alpha );
    vertexData.push_back( desc.edgeFeather );
    vertexData.push_back( desc.emphasis );
    vertexData.push_back( previous.x );
    vertexData.push_back( previous.y );
    vertexData.push_back( previous.z );
    vertexData.push_back( next.x );
    vertexData.push_back( next.y );
    vertexData.push_back( next.z );
}
} // namespace


RunEditorTracer::RunEditorTracer()
{
    // Runtime allocation policy: overlay line storage is paid once during tool
    // construction. EmitLine refuses overflow so replay prediction, gizmos, and
    // target markers cannot grow this vector while render builds the frame.
    m_lineData.reserve( RUN_EDITOR_TRACER_LINE_FLOAT_CAPACITY );
    m_priorityLineData.reserve( RUN_EDITOR_TRACER_PRIORITY_LINE_FLOAT_CAPACITY );
    m_renderLineData.reserve( RUN_EDITOR_TRACER_LINE_FLOAT_CAPACITY + RUN_EDITOR_TRACER_PRIORITY_LINE_FLOAT_CAPACITY );
    m_replayRibbonSegments.reserve( RUN_EDITOR_TRACER_REPLAY_RIBBON_ORDINARY_FLOAT_CAPACITY );
    m_priorityReplayRibbonSegments.reserve( RUN_EDITOR_TRACER_REPLAY_RIBBON_PRIORITY_FLOAT_CAPACITY );
    m_replayRibbonVertexData.reserve( RUN_EDITOR_TRACER_REPLAY_RIBBON_VERTEX_FLOAT_CAPACITY );
}

void RunEditorTracer::Clear()
{
    m_lineData.clear();
    m_priorityLineData.clear();
    m_renderLineData.clear();
    m_replayRibbonSegments.clear();
    m_priorityReplayRibbonSegments.clear();
    m_replayRibbonVertexData.clear();
    ClearReplayTrajectoryStats();
    m_replaySubmissionStats = SkullbonezCore::Core::MainMemoryReplayTrajectorySubmissionStats{};
}

void RunEditorTracer::ClearReplayTrajectoryStats()
{
    m_replayTrajectoryStats = SkullbonezCore::Core::MainMemoryReplayTrajectoryStats{};
}


const SkullbonezCore::Core::MainMemoryReplayTrajectoryStats& RunEditorTracer::ReplayTrajectoryStats() const
{
    return m_replayTrajectoryStats;
}


void RunEditorTracer::RecordReplayRibbonDroppedSegments( SkullbonezCore::Core::MainMemoryReplayTrajectoryLane lane,
                                                         std::size_t count )
{
    const std::size_t laneIndex = static_cast<std::size_t>( lane );
    if ( laneIndex < SkullbonezCore::Core::MAIN_MEMORY_REPLAY_TRAJECTORY_LANE_COUNT )
    {
        m_replayTrajectoryStats.droppedSegments[laneIndex] += static_cast<uint64_t>( count );
    }
}

ReplayVisualPacket RunEditorTracer::BuildReplayVisualPacket( const Vector3& cameraEye, const Vector3& cameraUp )
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
    packet.combinedLines =
        m_priorityLineData.empty() ? std::span<const float>( m_lineData ) : std::span<const float>( m_renderLineData );
    packet.ordinaryLines = m_lineData;
    packet.priorityLines = m_priorityLineData;
    packet.ordinaryRibbonSegments = m_replayRibbonSegments;
    packet.priorityRibbonSegments = m_priorityReplayRibbonSegments;
    packet.expandedRibbonVertices = m_replayRibbonVertexData;
    packet.submission = m_replaySubmissionStats;
    return packet;
}


std::size_t RunEditorTracer::ReplayPathRibbonSegmentCapacityRemaining() const
{
    if ( m_replayRibbonSegments.size() >= m_replayRibbonSegments.capacity() )
    {
        return 0;
    }

    return ( m_replayRibbonSegments.capacity() - m_replayRibbonSegments.size() ) /
           RUN_EDITOR_TRACER_REPLAY_RIBBON_FLOATS_PER_SEGMENT;
}


void RunEditorTracer::EmitLineTo( std::vector<float>& lineData,
                                  const Vector3& a,
                                  const Vector3& b,
                                  float r,
                                  float g,
                                  float bl )
{
    if ( lineData.size() + RUN_EDITOR_TRACER_FLOATS_PER_LINE > lineData.capacity() )
    {
        return;
    }
    lineData.insert( lineData.end(), { a.x, a.y, a.z, r, g, bl, b.x, b.y, b.z, r, g, bl } );
}


void RunEditorTracer::EmitLine( const Vector3& a, const Vector3& b, float r, float g, float bl )
{
    EmitLineTo( m_lineData, a, b, r, g, bl );
}


void RunEditorTracer::EmitArrow( const Vector3& a, const Vector3& b, float r, float g, float bl )
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


void RunEditorTracer::EmitRing( const Vector3& center, int axis, float radius, float r, float g, float bl )
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


void RunEditorTracer::EmitSphereTo( std::vector<float>& lineData,
                                    const Vector3& center,
                                    float radius,
                                    float r,
                                    float g,
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


void RunEditorTracer::EmitSphere( const Vector3& center, float radius, float r, float g, float bl )
{
    EmitSphereTo( m_lineData, center, radius, r, g, bl );
}


void RunEditorTracer::EmitBoxTo( std::vector<float>& lineData,
                                 const Vector3& center,
                                 const Vector3& xAxis,
                                 const Vector3& yAxis,
                                 const Vector3& zAxis,
                                 float r,
                                 float g,
                                 float bl )
{
    const Vector3 corners[8] = {
        center - xAxis - yAxis - zAxis,
        center + xAxis - yAxis - zAxis,
        center + xAxis + yAxis - zAxis,
        center - xAxis + yAxis - zAxis,
        center - xAxis - yAxis + zAxis,
        center + xAxis - yAxis + zAxis,
        center + xAxis + yAxis + zAxis,
        center - xAxis + yAxis + zAxis,
    };

    static constexpr int kEdges[12][2] = {
        { 0, 1 },
        { 1, 2 },
        { 2, 3 },
        { 3, 0 },
        { 4, 5 },
        { 5, 6 },
        { 6, 7 },
        { 7, 4 },
        { 0, 4 },
        { 1, 5 },
        { 2, 6 },
        { 3, 7 },
    };
    for ( const auto& edge : kEdges )
    {
        EmitLineTo( lineData, corners[edge[0]], corners[edge[1]], r, g, bl );
    }
}


void RunEditorTracer::EmitBox( const Vector3& center,
                               const Vector3& xAxis,
                               const Vector3& yAxis,
                               const Vector3& zAxis,
                               float r,
                               float g,
                               float bl )
{
    EmitBoxTo( m_lineData, center, xAxis, yAxis, zAxis, r, g, bl );
}


void RunEditorTracer::EmitShapeOutlineTo( std::vector<float>& lineData,
                                          const Vector3& position,
                                          const Quaternion& orientation,
                                          const CollisionShape& shape,
                                          float r,
                                          float g,
                                          float b )
{
    Quaternion outlineOrientation = orientation;
    const RotationMatrix rot = outlineOrientation.GetOrientationMatrix();

    if ( const BoundingSphere* sphere = std::get_if<BoundingSphere>( &shape ) )
    {
        EmitSphereTo( lineData, position + rot * sphere->GetPosition(), sphere->GetBoundingRadius(), r, g, b );
        return;
    }
    if ( const BoundingBox* box = std::get_if<BoundingBox>( &shape ) )
    {
        const Vector3& he = box->GetHalfExtents();
        const Vector3 center = position + rot * box->GetPosition();
        EmitBoxTo( lineData,
                   center,
                   rot * Vector3( he.x, 0.0f, 0.0f ),
                   rot * Vector3( 0.0f, he.y, 0.0f ),
                   rot * Vector3( 0.0f, 0.0f, he.z ),
                   r,
                   g,
                   b );
        return;
    }
    if ( const ConvexHullShape* hull = std::get_if<ConvexHullShape>( &shape ) )
    {
        const Vector3 hullCenter = position + rot * hull->GetPosition();
        for ( uint16_t edgeIndex = 0; edgeIndex < hull->GetEdgeCount(); ++edgeIndex )
        {
            const ConvexHullEdge& edge = hull->GetEdge( edgeIndex );
            EmitLineTo( lineData,
                        hullCenter + rot * hull->GetVertex( edge.vertexA ),
                        hullCenter + rot * hull->GetVertex( edge.vertexB ),
                        r,
                        g,
                        b );
        }
    }
}


void RunEditorTracer::EmitShapeOutline( const Vector3& position,
                                        const Quaternion& orientation,
                                        const CollisionShape& shape,
                                        float r,
                                        float g,
                                        float b )
{
    EmitShapeOutlineTo( m_lineData, position, orientation, shape, r, g, b );
}


void RunEditorTracer::EmitReplayRibbonSegmentTo( std::vector<float>& ribbonData,
                                                 const Vector3& a,
                                                 const Vector3& b,
                                                 float r,
                                                 float g,
                                                 float bl,
                                                 const ReplayRibbonStyle& style,
                                                 SkullbonezCore::Core::MainMemoryReplayTrajectoryLane lane )
{
    if ( VectorMagSquared( b - a ) <= TOLERANCE * TOLERANCE )
    {
        return;
    }

    const std::size_t laneIndex = static_cast<std::size_t>( lane );
    const std::size_t combinedSegments = ( m_replayRibbonSegments.size() + m_priorityReplayRibbonSegments.size() ) /
                                         RUN_EDITOR_TRACER_REPLAY_RIBBON_FLOATS_PER_SEGMENT;
    if ( combinedSegments >= RUN_EDITOR_TRACER_REPLAY_RIBBON_SEGMENT_BUDGET ||
         ribbonData.size() + RUN_EDITOR_TRACER_REPLAY_RIBBON_FLOATS_PER_SEGMENT > ribbonData.capacity() )
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
}


void RunEditorTracer::EmitReplayRibbonGlowPairTo( std::vector<float>& ribbonData,
                                                  const Vector3& a,
                                                  const Vector3& b,
                                                  float r,
                                                  float g,
                                                  float bl,
                                                  const ReplayRibbonStyle& glow,
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


void RunEditorTracer::EmitReplayRibbonShapeOutlineTo( std::vector<float>& ribbonData,
                                                      const Vector3& position,
                                                      const Quaternion& orientation,
                                                      const CollisionShape& shape,
                                                      float r,
                                                      float g,
                                                      float b,
                                                      const ReplayRibbonStyle& style,
                                                      SkullbonezCore::Core::MainMemoryReplayTrajectoryLane lane )
{
    Quaternion outlineOrientation = orientation;
    const RotationMatrix rot = outlineOrientation.GetOrientationMatrix();

    if ( const BoundingSphere* sphere = std::get_if<BoundingSphere>( &shape ) )
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
    if ( const BoundingBox* box = std::get_if<BoundingBox>( &shape ) )
    {
        const Vector3& he = box->GetHalfExtents();
        const Vector3 center = position + rot * box->GetPosition();
        const Vector3 xAxis = rot * Vector3( he.x, 0.0f, 0.0f );
        const Vector3 yAxis = rot * Vector3( 0.0f, he.y, 0.0f );
        const Vector3 zAxis = rot * Vector3( 0.0f, 0.0f, he.z );
        const Vector3 corners[8] = {
            center - xAxis - yAxis - zAxis,
            center + xAxis - yAxis - zAxis,
            center + xAxis + yAxis - zAxis,
            center - xAxis + yAxis - zAxis,
            center - xAxis - yAxis + zAxis,
            center + xAxis - yAxis + zAxis,
            center + xAxis + yAxis + zAxis,
            center - xAxis + yAxis + zAxis,
        };
        static constexpr int kEdges[12][2] = {
            { 0, 1 },
            { 1, 2 },
            { 2, 3 },
            { 3, 0 },
            { 4, 5 },
            { 5, 6 },
            { 6, 7 },
            { 7, 4 },
            { 0, 4 },
            { 1, 5 },
            { 2, 6 },
            { 3, 7 },
        };
        for ( const auto& edge : kEdges )
        {
            EmitReplayRibbonSegmentTo( ribbonData, corners[edge[0]], corners[edge[1]], r, g, b, style, lane );
        }
        return;
    }
    if ( const ConvexHullShape* hull = std::get_if<ConvexHullShape>( &shape ) )
    {
        const Vector3 hullCenter = position + rot * hull->GetPosition();
        for ( uint16_t edgeIndex = 0; edgeIndex < hull->GetEdgeCount(); ++edgeIndex )
        {
            const ConvexHullEdge& edge = hull->GetEdge( edgeIndex );
            EmitReplayRibbonSegmentTo( ribbonData,
                                       hullCenter + rot * hull->GetVertex( edge.vertexA ),
                                       hullCenter + rot * hull->GetVertex( edge.vertexB ),
                                       r,
                                       g,
                                       b,
                                       style,
                                       lane );
        }
    }
}


void RunEditorTracer::BuildReplayRibbonVertices( const Vector3& cameraEye, const Vector3& cameraUp )
{
    static_cast<void>( cameraEye );
    static_cast<void>( cameraUp );

    m_replayRibbonVertexData.clear();

    auto appendRibbonData = [&]( const std::vector<float>& ribbonData )
    {
        for ( std::size_t i = 0; i + RUN_EDITOR_TRACER_REPLAY_RIBBON_FLOATS_PER_SEGMENT <= ribbonData.size();
              i += RUN_EDITOR_TRACER_REPLAY_RIBBON_FLOATS_PER_SEGMENT )
        {
            if ( m_replayRibbonVertexData.size() + RUN_EDITOR_TRACER_REPLAY_RIBBON_VERTICES_PER_SEGMENT *
                                                       RUN_EDITOR_TRACER_REPLAY_RIBBON_FLOATS_PER_VERTEX >
                 m_replayRibbonVertexData.capacity() )
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
            if ( i >= RUN_EDITOR_TRACER_REPLAY_RIBBON_FLOATS_PER_SEGMENT )
            {
                const std::size_t previousIndex = i - RUN_EDITOR_TRACER_REPLAY_RIBBON_FLOATS_PER_SEGMENT;
                const Vector3 previousEnd( ribbonData[previousIndex + 3],
                                           ribbonData[previousIndex + 4],
                                           ribbonData[previousIndex + 5] );
                const bool samePresentation = ribbonData[previousIndex + 9] == ribbonData[i + 9] &&
                                              ribbonData[previousIndex + 10] == ribbonData[i + 10] &&
                                              ribbonData[previousIndex + 11] == ribbonData[i + 11] &&
                                              ribbonData[previousIndex + 12] == ribbonData[i + 12];
                if ( samePresentation && VectorMagSquared( previousEnd - a ) <= TOLERANCE * TOLERANCE )
                {
                    previous = Vector3( ribbonData[previousIndex + 0],
                                        ribbonData[previousIndex + 1],
                                        ribbonData[previousIndex + 2] );
                }
            }
            const std::size_t nextIndex = i + RUN_EDITOR_TRACER_REPLAY_RIBBON_FLOATS_PER_SEGMENT;
            if ( nextIndex + RUN_EDITOR_TRACER_REPLAY_RIBBON_FLOATS_PER_SEGMENT <= ribbonData.size() )
            {
                const Vector3 nextStart( ribbonData[nextIndex + 0],
                                         ribbonData[nextIndex + 1],
                                         ribbonData[nextIndex + 2] );
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
                AppendReplayRibbonVertex( m_replayRibbonVertexData,
                                          ReplayRibbonVertexDesc{ .previous = previous,
                                                                  .start = a,
                                                                  .end = b,
                                                                  .next = next,
                                                                  .r = r,
                                                                  .g = g,
                                                                  .b = bl,
                                                                  .alpha = alpha,
                                                                  .width = width,
                                                                  .edgeFeather = edgeFeather,
                                                                  .emphasis = emphasis } );
            }
        }
    };

    // Invariant: ordinary replay paths may overflow without erasing causal
    // evidence. Priority ribbons are appended second; only the yellow entry box
    // remains on this ribbon path while rest/horizon boxes use priority lines.
    appendRibbonData( m_replayRibbonSegments );
    const std::size_t ordinaryVertexFloatCount = m_replayRibbonVertexData.size();
    appendRibbonData( m_priorityReplayRibbonSegments );

    m_replaySubmissionStats = SkullbonezCore::Core::MainMemoryReplayTrajectorySubmissionStats{};
    // Invariant: the fidelity probe observes the same ordered floats consumed
    // by the render commands. Empty streams still have a count-bearing hash so
    // absence cannot alias a skipped sample in the golden manifest.
    HashReplaySubmissionFloatStream( m_lineData,
                                     m_replaySubmissionStats.ordinaryLineHash,
                                     m_replaySubmissionStats.ordinaryLineBytes );
    m_replaySubmissionStats.ordinaryLineVertexCount = static_cast<uint32_t>( m_lineData.size() / 6u );
    HashReplaySubmissionFloatStream( m_priorityLineData,
                                     m_replaySubmissionStats.priorityLineHash,
                                     m_replaySubmissionStats.priorityLineBytes );
    m_replaySubmissionStats.priorityLineCanonicalHash = HashReplaySubmissionCanonicalRecords( m_priorityLineData, 12u );
    m_replaySubmissionStats.priorityLineVertexCount = static_cast<uint32_t>( m_priorityLineData.size() / 6u );
    HashReplaySubmissionFloatStream( m_replayRibbonSegments,
                                     m_replaySubmissionStats.ordinaryRibbonHash,
                                     m_replaySubmissionStats.ordinaryRibbonBytes );
    m_replaySubmissionStats.ordinaryRibbonSegmentCount =
        static_cast<uint32_t>( m_replayRibbonSegments.size() / RUN_EDITOR_TRACER_REPLAY_RIBBON_FLOATS_PER_SEGMENT );
    HashReplaySubmissionFloatStream( m_priorityReplayRibbonSegments,
                                     m_replaySubmissionStats.priorityRibbonHash,
                                     m_replaySubmissionStats.priorityRibbonBytes );
    m_replaySubmissionStats.priorityRibbonCanonicalHash =
        HashReplaySubmissionCanonicalRecords( m_priorityReplayRibbonSegments,
                                              RUN_EDITOR_TRACER_REPLAY_RIBBON_FLOATS_PER_SEGMENT );
    m_replaySubmissionStats.priorityRibbonSegmentCount = static_cast<uint32_t>(
        m_priorityReplayRibbonSegments.size() / RUN_EDITOR_TRACER_REPLAY_RIBBON_FLOATS_PER_SEGMENT );
    m_replaySubmissionStats.hasGeometry = !m_lineData.empty() || !m_priorityLineData.empty() ||
                                          !m_replayRibbonSegments.empty() || !m_priorityReplayRibbonSegments.empty();
    if ( !m_replayRibbonVertexData.empty() )
    {
        // Invariant: Stage-9 flicker validation hashes the exact float payload
        // submitted to DrawTransientColoredTriangles. It deliberately ignores
        // vector capacity and camera data because the trajectory-ribbon shader
        // performs camera-facing expansion from this stable segment payload.
        const std::size_t byteCount = m_replayRibbonVertexData.size() * sizeof( float );
        uint64_t hash = REPLAY_TRAJECTORY_SUBMISSION_FNV_OFFSET;
        const uint64_t floatCount = static_cast<uint64_t>( m_replayRibbonVertexData.size() );
        HashReplaySubmissionBytes( hash, &floatCount, sizeof( floatCount ) );
        HashReplaySubmissionBytes( hash, m_replayRibbonVertexData.data(), byteCount );
        m_replaySubmissionStats.vertexHash = hash;
        m_replaySubmissionStats.ordinaryVertexBytes = ordinaryVertexFloatCount * sizeof( float );
        m_replaySubmissionStats.ordinaryVertexCount =
            static_cast<uint32_t>( ordinaryVertexFloatCount / RUN_EDITOR_TRACER_REPLAY_RIBBON_FLOATS_PER_VERTEX );
        uint64_t ordinaryHash = REPLAY_TRAJECTORY_SUBMISSION_FNV_OFFSET;
        const uint64_t ordinaryFloatCount = static_cast<uint64_t>( ordinaryVertexFloatCount );
        HashReplaySubmissionBytes( ordinaryHash, &ordinaryFloatCount, sizeof( ordinaryFloatCount ) );
        if ( ordinaryVertexFloatCount > 0u )
        {
            HashReplaySubmissionBytes( ordinaryHash,
                                       m_replayRibbonVertexData.data(),
                                       ordinaryVertexFloatCount * sizeof( float ) );
        }
        m_replaySubmissionStats.ordinaryVertexHash = ordinaryHash;
        m_replaySubmissionStats.vertexBytes = static_cast<uint64_t>( byteCount );
        m_replaySubmissionStats.vertexCount = static_cast<uint32_t>(
            m_replayRibbonVertexData.size() / RUN_EDITOR_TRACER_REPLAY_RIBBON_FLOATS_PER_VERTEX );
        m_replaySubmissionStats.segmentCount = static_cast<uint32_t>(
            m_replaySubmissionStats.vertexCount / RUN_EDITOR_TRACER_REPLAY_RIBBON_VERTICES_PER_SEGMENT );
    }
}


void RunEditorTracer::AddPlacementRay( const Vector3& rayOrigin, const Vector3& hitPoint )
{
    EmitLine( rayOrigin, hitPoint, 0.25f, 0.80f, 1.0f );
}


void RunEditorTracer::AddPlacementGhost( int objectType,
                                         const Vector3& center,
                                         const Vector3& terrainPoint,
                                         const Vector3& placementScale,
                                         const Quaternion& orientation,
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
            const ConvexHullShape* hull = CachedEditorHullForAsset( part.hullAsset );
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
                          hullCenter + rotation * hull->GetVertex( edge.vertexB ),
                          ghostR,
                          ghostG,
                          ghostB );
            }
        }
        return;
    }
    if ( EditorBuildingDefinitionForType( type ) )
    {
        const Vector3 base = terrainPoint + rotation * Vector3( 0.0f, EDITOR_PLACEMENT_SURFACE_EPSILON, 0.0f );
        ForEachEditorBuildingPart(
            type,
            assets,
            [&]( const Json& part )
            {
                const Vector3 offset = EditorJsonVec3Or( part, "offset", Vector3( 0.0f, 0.0f, 0.0f ) );
                const Quaternion partOrientation = EditorBuildingPartOrientation( orientation, part );
                Quaternion partCopy = partOrientation;
                const RotationMatrix partRotation = partCopy.GetOrientationMatrix();
                const Vector3 bodyCenter = base + rotation * offset;
                const std::string primitiveType = EditorAssetPrimitiveType( part );
                if ( primitiveType == "convexHull" )
                {
                    const std::string hullPath = EditorJsonStringOr( part, "hull", "" );
                    const ConvexHullShape* hull = hullPath.empty() ? nullptr : CachedEditorBuildingHull( hullPath );
                    if ( !hull )
                    {
                        return;
                    }
                    const Vector3 hullCenter =
                        bodyCenter + partRotation * ( hull->GetAuthoredCenterOfMass() + hull->GetPosition() );
                    for ( uint16_t edgeIndex = 0; edgeIndex < hull->GetEdgeCount(); ++edgeIndex )
                    {
                        const ConvexHullEdge& edge = hull->GetEdge( edgeIndex );
                        EmitLine( hullCenter + partRotation * hull->GetVertex( edge.vertexA ),
                                  hullCenter + partRotation * hull->GetVertex( edge.vertexB ),
                                  ghostR,
                                  ghostG,
                                  ghostB );
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
                    EmitBox( bodyCenter,
                             partRotation * Vector3( halfExtents.x, 0.0f, 0.0f ),
                             partRotation * Vector3( 0.0f, halfExtents.y, 0.0f ),
                             partRotation * Vector3( 0.0f, 0.0f, halfExtents.z ),
                             ghostR,
                             ghostG,
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
            EmitBox( partCenter,
                     rotation * Vector3( part.halfX, 0.0f, 0.0f ),
                     rotation * Vector3( 0.0f, part.halfY, 0.0f ),
                     rotation * Vector3( 0.0f, 0.0f, part.halfZ ),
                     ghostR,
                     ghostG,
                     ghostB );
        }
        return;
    }

    switch ( type )
    {
    case SkullbonezCore::UI::EditorTab::OBJECT_BOX:
        EmitBox( center,
                 rotation * Vector3( scale.x, 0.0f, 0.0f ),
                 rotation * Vector3( 0.0f, scale.y, 0.0f ),
                 rotation * Vector3( 0.0f, 0.0f, scale.z ),
                 ghostR,
                 ghostG,
                 ghostB );
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
        if ( !TryBuildScaledEditorHullForType( type, scale, hull ) )
        {
            return;
        }
        const Vector3 hullCenter = center + rotation * hull.GetPosition();
        for ( uint16_t edgeIndex = 0; edgeIndex < hull.GetEdgeCount(); ++edgeIndex )
        {
            const ConvexHullEdge& edge = hull.GetEdge( edgeIndex );
            EmitLine( hullCenter + rotation * hull.GetVertex( edge.vertexA ),
                      hullCenter + rotation * hull.GetVertex( edge.vertexB ),
                      ghostR,
                      ghostG,
                      ghostB );
        }
        break;
    }
    }
}


void RunEditorTracer::AddRayCastTestLine( const Vector3& start, const Vector3& end, float alpha, bool hit )
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

void RunEditorTracer::AddReplayPathSegment( const Vector3& start,
                                            const Vector3& end,
                                            float r,
                                            float g,
                                            float b,
                                            SkullbonezCore::Core::MainMemoryReplayTrajectoryLane lane,
                                            float emphasis )
{
    ReplayRibbonStyle glow = REPLAY_PATH_STYLE;
    ReplayRibbonStyle core = REPLAY_PATH_STYLE;
    // Invariant: only the replay presentation owner may opt a segment into the
    // shader's halo and bloom-feed branch. All generic editor and non-selected
    // replay paths arrive through the zero-emphasis default.
    const float boundedEmphasis = std::clamp( emphasis, 0.0f, 1.0f );
    glow.emphasis = boundedEmphasis;
    core.emphasis = boundedEmphasis;
    EmitReplayRibbonGlowPairTo( m_replayRibbonSegments, start, end, r, g, b, glow, core, lane );
}


void RunEditorTracer::AddReplayCausalTrailSegment( const Vector3& start, const Vector3& end, float r, float g, float b )
{
    // Why: retained causal trails are the evidence attached to yellow/grey/ghost
    // boxes. They live with the priority ribbons so overflow in ordinary root
    // path rendering cannot leave a marker without its sampled route.
    const ReplayRibbonStyle glow = REPLAY_CAUSAL_STYLE;
    const ReplayRibbonStyle core = REPLAY_CAUSAL_STYLE;
    EmitReplayRibbonGlowPairTo( m_priorityReplayRibbonSegments,
                                start,
                                end,
                                r,
                                g,
                                b,
                                glow,
                                core,
                                SkullbonezCore::Core::MainMemoryReplayTrajectoryLane::RetainedTrail );
}


void RunEditorTracer::AddReplayBaselinePathSegment( const Vector3& start,
                                                    const Vector3& end,
                                                    float r,
                                                    float g,
                                                    float b )
{
    const ReplayRibbonStyle glow = REPLAY_BASELINE_STYLE;
    const ReplayRibbonStyle core = REPLAY_BASELINE_STYLE;
    EmitReplayRibbonGlowPairTo( m_replayRibbonSegments,
                                start,
                                end,
                                r,
                                g,
                                b,
                                glow,
                                core,
                                SkullbonezCore::Core::MainMemoryReplayTrajectoryLane::BaselineRoot );
}


void RunEditorTracer::AddReplayContactMarker( const Vector3& point, const Vector3& normal, float r, float g, float b )
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


void RunEditorTracer::AddReplayImpulseVector( const Vector3& point, const Vector3& impulse, float r, float g, float b )
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


void RunEditorTracer::AddReplayFutureTargetMarker( const Vector3& position,
                                                   const Quaternion& orientation,
                                                   const CollisionShape& shape,
                                                   int depth )
{
    const float depthFade = std::clamp( static_cast<float>( depth - 1 ) * 0.10f, 0.0f, 0.34f );
    const float r = std::clamp( 0.98f - depthFade * 0.55f, 0.52f, 1.0f );
    const float g = std::clamp( 0.72f - depthFade * 0.22f, 0.42f, 0.82f );
    const float b = std::clamp( 0.22f - depthFade * 0.12f, 0.10f, 0.32f );
    EmitShapeOutline( position, orientation, shape, r, g, b );
}


void RunEditorTracer::AddReplayCausalEntryMarker( const Vector3& position,
                                                  const Quaternion& orientation,
                                                  const CollisionShape& shape )
{
    // Why: yellow always means "joined the causal tree here". Keep it as the
    // only marker on the ribbon shader, but emit one logical segment style so
    // marker outlines do not double the retained ribbon budget.
    const ReplayRibbonStyle singlePass = REPLAY_MARKER_STYLE;
    EmitReplayRibbonShapeOutlineTo( m_priorityReplayRibbonSegments,
                                    position,
                                    orientation,
                                    shape,
                                    1.0f,
                                    0.85f,
                                    0.25f,
                                    singlePass,
                                    SkullbonezCore::Core::MainMemoryReplayTrajectoryLane::CausalMarker );
}


void RunEditorTracer::AddReplayCausalRestMarker( const Vector3& position,
                                                 const Quaternion& orientation,
                                                 const CollisionShape& shape )
{
    EmitShapeOutlineTo( m_priorityLineData, position, orientation, shape, 0.58f, 0.58f, 0.62f );
}


void RunEditorTracer::AddReplayCausalHorizonMarker( const Vector3& position,
                                                    const Quaternion& orientation,
                                                    const CollisionShape& shape )
{
    // Concept: horizon ghosts are not landings. They mark "this is where the
    // prediction buffer ends" for a body still mid-flight, so the color stays
    // distinct from grey resting boxes.
    EmitShapeOutlineTo( m_priorityLineData, position, orientation, shape, 0.45f, 0.92f, 1.0f );
}


void RunEditorTracer::AddReplayBaselineEntryMarker( const Vector3& position,
                                                    const Quaternion& orientation,
                                                    const CollisionShape& shape )
{
    // Concept: cold baseline markers are the old future's footprint. They stay
    // on the wire path so cyan boxes do not compete with selected-path halos.
    EmitShapeOutline( position, orientation, shape, 0.26f, 0.78f, 0.95f );
}


void RunEditorTracer::AddReplayBaselineRestMarker( const Vector3& position,
                                                   const Quaternion& orientation,
                                                   const CollisionShape& shape )
{
    EmitShapeOutline( position, orientation, shape, 0.18f, 0.62f, 0.78f );
}


void RunEditorTracer::AddReplayTargetMarker( const Vector3& position,
                                             const Quaternion& orientation,
                                             const CollisionShape& shape,
                                             float radius )
{
    AddSelectionOutline( position, orientation, shape );
    EmitRing( position, 1, (std::max)( 1.0f, radius ), 1.0f, 1.0f, 1.0f );
}


void RunEditorTracer::AddAttachedCameraTargetMarker( const Vector3& position,
                                                     const Quaternion& orientation,
                                                     const CollisionShape& shape,
                                                     float radius,
                                                     bool activeFollow )
{
    AddSelectionOutline( position, orientation, shape );
    radius = (std::max)( 1.0f, radius );
    const float r = activeFollow ? 0.16f : 1.0f;
    const float g = activeFollow ? 1.0f : 0.72f;
    const float b = activeFollow ? 0.92f : 0.24f;
    EmitRing( position, 1, radius, r, g, b );
    EmitRing( position, 0, radius * 0.68f, r, g, b );
}


void RunEditorTracer::AddSelectionOutline( const Vector3& position,
                                           const Quaternion& orientation,
                                           const CollisionShape& shape )
{
    constexpr float outlineR = 1.0f;
    constexpr float outlineG = 1.0f;
    constexpr float outlineB = 0.55f;
    EmitShapeOutline( position, orientation, shape, outlineR, outlineG, outlineB );
}


void RunEditorTracer::AddGizmo( const Vector3& origin,
                                float radius,
                                int hotTranslateAxis,
                                int hotRotationAxis,
                                int activeAxis,
                                bool activeRotation,
                                bool scaleMode,
                                bool activeScale )
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
            EmitBox( endpoint,
                     Vector3( handle, 0.0f, 0.0f ),
                     Vector3( 0.0f, handle, 0.0f ),
                     Vector3( 0.0f, 0.0f, handle ),
                     r,
                     g,
                     b );
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


void RunEditorTracer::AddReplayVelocityGizmo( const Vector3& origin,
                                              const Quaternion& orientation,
                                              const CollisionShape& shape,
                                              float radius,
                                              const Vector3& linearVelocity,
                                              const Vector3& angularVelocity,
                                              int hotLinearAxis,
                                              int hotAngularAxis,
                                              int activeAxis,
                                              bool activeAngular )
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
        EmitLine( origin - axisVector * ( baseLength * 0.24f ),
                  origin + axisVector * ( baseLength * 0.24f ),
                  r * 0.34f,
                  g * 0.34f,
                  b * 0.34f );
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


void RunEditorTracer::Render( const ReplayVisualPacket& packet,
                              const Matrix4& viewProjection,
                              Rendering::IRenderCommandContext& renderCommands )
{
    if ( !packet.HasGeometry() )
    {
        return;
    }

    if ( !packet.combinedLines.empty() )
    {
        // Invariant: combinedLines stores colored vertices as xyz/rgb floats; every
        // pair of vertices is one line segment consumed by DrawLinesColored.
        renderCommands.DrawLinesColored( packet.combinedLines.data(),
                                         static_cast<int>( packet.combinedLines.size() / 6u ),
                                         viewProjection.Data() );
    }

    if ( !packet.expandedRibbonVertices.empty() )
    {
        Rendering::BlendFactor blendSrc = Rendering::BlendFactor::One;
        Rendering::BlendFactor blendDst = Rendering::BlendFactor::Zero;
        const bool depthTestWasEnabled = renderCommands.IsDepthTestEnabled();
        const bool depthWriteWasEnabled = renderCommands.IsDepthWriteEnabled();
        const bool blendWasEnabled = renderCommands.IsBlendEnabled();
        const bool cullWasEnabled = renderCommands.IsCullFaceEnabled();
        renderCommands.GetBlendFunc( blendSrc, blendDst );

        // Concept: the first pass is a low-opacity depth hint with depth
        // testing disabled; the normal pass is depth-tested, so visible
        // strokes stay seated while occluded spans remain only faintly
        // readable behind scene geometry.
        renderCommands.SetDepthTest( false );
        renderCommands.SetDepthWrite( false );
        renderCommands.SetBlend( true );
        renderCommands.SetBlendFunc( Rendering::BlendFactor::SrcAlpha, Rendering::BlendFactor::One );
        renderCommands.SetCullFace( false );

        renderCommands.DrawTransientColoredTriangles(
            packet.expandedRibbonVertices.data(),
            static_cast<int>( packet.expandedRibbonVertices.size() /
                              RUN_EDITOR_TRACER_REPLAY_RIBBON_FLOATS_PER_VERTEX ),
            viewProjection.Data(),
            Rendering::TransientTriangleStyle::TrajectoryRibbonDepthHint );

        renderCommands.SetDepthTest( true );
        renderCommands.DrawTransientColoredTriangles(
            packet.expandedRibbonVertices.data(),
            static_cast<int>( packet.expandedRibbonVertices.size() /
                              RUN_EDITOR_TRACER_REPLAY_RIBBON_FLOATS_PER_VERTEX ),
            viewProjection.Data(),
            Rendering::TransientTriangleStyle::TrajectoryRibbon );

        renderCommands.SetCullFace( cullWasEnabled );
        renderCommands.SetBlendFunc( blendSrc, blendDst );
        renderCommands.SetBlend( blendWasEnabled );
        renderCommands.SetDepthWrite( depthWriteWasEnabled );
        renderCommands.SetDepthTest( depthTestWasEnabled );
    }
}
