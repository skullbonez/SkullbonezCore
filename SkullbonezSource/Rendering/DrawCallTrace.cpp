/*
File: SkullbonezSource/Rendering/DrawCallTrace.cpp
Purpose:
  Implements fixed-capacity draw-call attribution storage for renderer diagnostics.

Summary:
  The renderer builds a small tree while command lists are recorded. Each draw
  increments the current leaf and every ancestor, then BeginFrame publishes a
  previous-frame snapshot for the profiler UI.

Glossary:
  FNV (Fowler-Noll-Vo): Small string hash used here to identify stable scope
    paths without storing dynamic lookup tables.
  Leaf: Final component of a slash-delimited trace path, such as Water in
    Frame/Render/Water.
  Overflow: Count of trace nodes or draw events that exceeded the fixed budget;
    totals remain partial instead of allocating in the hot path.

Invariants:
  - Current-frame arrays and snapshot arrays are separate so UI reads do not see
    half-updated render data.
  - Capacity overflow is reported explicitly and never grows storage mid-frame.

Related:
  - SkullbonezSource/Rendering/DrawCallTrace.h
*/
#include "DrawCallTrace.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace SkullbonezCore
{
namespace Rendering
{

namespace
{
constexpr uint32_t FNV_OFFSET = 2166136261u;
constexpr uint32_t FNV_PRIME = 16777619u;
constexpr const char* ROOT_NODE_NAME = "Frame";
} // namespace

DrawCallTrace::DrawCallTrace()
{
    ResetCurrentFrame();
}


void DrawCallTrace::BeginFrame()
{
    PublishSnapshot();
    ResetCurrentFrame();
}


void DrawCallTrace::PushScope( const char* fullPathOrLeaf, uint32_t hash )
{
    if ( !fullPathOrLeaf || fullPathOrLeaf[0] == '\0' )
    {
        ++m_scopeMismatchCount;
        return;
    }

    int nodeIndex = -1;
    if ( HasPathSeparator( fullPathOrLeaf ) )
    {
        nodeIndex = EnsurePathNode( fullPathOrLeaf, hash );
    }
    else
    {
        nodeIndex = EnsureRelativeNode( fullPathOrLeaf );
    }

    PushCurrentNode( nodeIndex );
}


void DrawCallTrace::PopScope( uint32_t hash )
{
    if ( m_currentNodeIndex >= 0 )
    {
        const DrawCallTraceNode& node = m_nodes[m_currentNodeIndex];
        const uint32_t leafHash =
            node.leafName ? HashStringRange( node.leafName, static_cast<int>( strlen( node.leafName ) ) ) : 0u;
        if ( node.hash != hash && leafHash != hash )
        {
            ++m_scopeMismatchCount;
        }
    }
    else
    {
        ++m_scopeMismatchCount;
    }

    if ( m_scopeStackDepth > 0 )
    {
        m_currentNodeIndex = m_scopeStack[--m_scopeStackDepth];
    }
    else
    {
        m_currentNodeIndex = EnsureRootNode();
    }
}


void DrawCallTrace::RecordDrawCall( const DrawCallRecord& record )
{
    if ( m_eventCount < MAX_DRAW_TRACE_EVENTS )
    {
        ++m_eventCount;
    }
    else
    {
        ++m_eventOverflowCount;
    }

    const int rootIndex = EnsureRootNode();
    int nodeIndex = m_currentNodeIndex >= 0 ? m_currentNodeIndex : rootIndex;
    const int instances = (std::max)( 1, record.instanceCount );
    const int vertices = (std::max)( 0, record.vertexCount ) * instances;

    while ( nodeIndex >= 0 && nodeIndex < m_nodeCount )
    {
        DrawCallTraceNode& node = m_nodes[nodeIndex];
        ++node.drawCallCount;
        node.vertexCount += vertices;
        node.instanceCount += instances;
        nodeIndex = node.parentIndex;
    }
}


DrawCallTraceSnapshot DrawCallTrace::Snapshot() const
{
    DrawCallTraceSnapshot snapshot;
    snapshot.nodes = m_snapshotNodes;
    snapshot.nodeCount = m_snapshotNodeCount;
    snapshot.nodeOverflowCount = m_snapshotNodeOverflowCount;
    snapshot.eventCount = m_snapshotEventCount;
    snapshot.eventOverflowCount = m_snapshotEventOverflowCount;
    snapshot.scopeMismatchCount = m_snapshotScopeMismatchCount;
    return snapshot;
}


void DrawCallTrace::PublishSnapshot()
{
    m_snapshotNodeCount = m_nodeCount;
    m_snapshotNodeOverflowCount = m_nodeOverflowCount;
    m_snapshotEventCount = m_eventCount;
    m_snapshotEventOverflowCount = m_eventOverflowCount;
    m_snapshotScopeMismatchCount = m_scopeMismatchCount;

    for ( int i = 0; i < m_nodeCount; ++i )
    {
        m_snapshotNodes[i] = m_nodes[i];
        memcpy( m_snapshotNodeNames[i], m_nodeNames[i], MAX_DRAW_TRACE_NAME_CHARS );
        m_snapshotNodes[i].name = m_snapshotNodeNames[i];
        m_snapshotNodes[i].leafName = m_snapshotNodeNames[i] + LeafOffset( m_snapshotNodeNames[i] );
    }
}


void DrawCallTrace::ResetCurrentFrame()
{
    m_nodeCount = 0;
    m_currentNodeIndex = -1;
    m_scopeStackDepth = 0;
    m_nodeOverflowCount = 0;
    m_eventCount = 0;
    m_eventOverflowCount = 0;
    m_scopeMismatchCount = 0;
    for ( int i = 0; i < MAX_DRAW_TRACE_NODES; ++i )
    {
        m_nodes[i] = DrawCallTraceNode();
        m_nodeNames[i][0] = '\0';
    }
    EnsureRootNode();
}


int DrawCallTrace::EnsureRootNode()
{
    int rootIndex = FindNode( HashStringRange( ROOT_NODE_NAME, 5 ) );
    if ( rootIndex >= 0 )
    {
        return rootIndex;
    }
    rootIndex = AppendNode( ROOT_NODE_NAME, 5, HashStringRange( ROOT_NODE_NAME, 5 ), -1, 0 );
    if ( rootIndex >= 0 && m_currentNodeIndex < 0 )
    {
        m_currentNodeIndex = rootIndex;
    }
    return rootIndex;
}


int DrawCallTrace::EnsurePathNode( const char* path, uint32_t fullHash )
{
    int parentIndex = -1;
    int nodeIndex = -1;
    for ( int i = 0; path[i] != '\0'; ++i )
    {
        if ( path[i + 1] != '\0' && path[i + 1] != '/' )
        {
            continue;
        }

        const int pathLength = i + 1;
        const uint32_t hash = path[i + 1] == '\0' ? fullHash : HashStringRange( path, pathLength );
        nodeIndex = FindNode( hash );
        if ( nodeIndex < 0 )
        {
            nodeIndex = AppendNode( path, pathLength, hash, parentIndex, PathDepth( path, pathLength ) );
        }
        if ( nodeIndex < 0 )
        {
            return parentIndex >= 0 ? parentIndex : EnsureRootNode();
        }
        parentIndex = nodeIndex;
    }
    return nodeIndex >= 0 ? nodeIndex : EnsureRootNode();
}


int DrawCallTrace::EnsureRelativeNode( const char* leaf )
{
    const int parentIndex = m_currentNodeIndex >= 0 ? m_currentNodeIndex : EnsureRootNode();
    char fullPath[MAX_DRAW_TRACE_NAME_CHARS] = {};

    if ( parentIndex >= 0 && m_nodes[parentIndex].name && m_nodes[parentIndex].name[0] != '\0' )
    {
        snprintf( fullPath, sizeof( fullPath ), "%s/%s", m_nodes[parentIndex].name, leaf );
    }
    else
    {
        snprintf( fullPath, sizeof( fullPath ), "%s", leaf );
    }

    const int fullLength = static_cast<int>( strlen( fullPath ) );
    const uint32_t hash = HashStringRange( fullPath, fullLength );
    int nodeIndex = FindNode( hash );
    if ( nodeIndex >= 0 )
    {
        return nodeIndex;
    }
    return AppendNode( fullPath, fullLength, hash, parentIndex, PathDepth( fullPath, fullLength ) );
}


int DrawCallTrace::AppendNode( const char* path, int pathLength, uint32_t hash, int parentIndex, int depth )
{
    if ( m_nodeCount >= MAX_DRAW_TRACE_NODES )
    {
        ++m_nodeOverflowCount;
        return -1;
    }

    const int nodeIndex = m_nodeCount++;
    const int copyLength = (std::min)( pathLength, MAX_DRAW_TRACE_NAME_CHARS - 1 );
    memcpy( m_nodeNames[nodeIndex], path, static_cast<size_t>( copyLength ) );
    m_nodeNames[nodeIndex][copyLength] = '\0';

    DrawCallTraceNode& node = m_nodes[nodeIndex];
    node.name = m_nodeNames[nodeIndex];
    node.leafName = m_nodeNames[nodeIndex] + LeafOffset( m_nodeNames[nodeIndex] );
    node.hash = hash;
    node.parentIndex = parentIndex;
    node.depth = depth;
    return nodeIndex;
}


int DrawCallTrace::FindNode( uint32_t hash ) const
{
    for ( int i = 0; i < m_nodeCount; ++i )
    {
        if ( m_nodes[i].hash == hash )
        {
            return i;
        }
    }
    return -1;
}


void DrawCallTrace::PushCurrentNode( int nodeIndex )
{
    if ( m_scopeStackDepth < MAX_DRAW_TRACE_DEPTH )
    {
        m_scopeStack[m_scopeStackDepth++] = m_currentNodeIndex;
    }
    else
    {
        ++m_scopeMismatchCount;
    }
    m_currentNodeIndex = nodeIndex >= 0 ? nodeIndex : EnsureRootNode();
}


uint32_t DrawCallTrace::HashStringRange( const char* text, int length )
{
    uint32_t hash = FNV_OFFSET;
    for ( int i = 0; i < length && text[i] != '\0'; ++i )
    {
        hash = ( hash ^ static_cast<uint32_t>( text[i] ) ) * FNV_PRIME;
    }
    return hash;
}


bool DrawCallTrace::HasPathSeparator( const char* text )
{
    for ( int i = 0; text && text[i] != '\0'; ++i )
    {
        if ( text[i] == '/' )
        {
            return true;
        }
    }
    return false;
}


int DrawCallTrace::PathDepth( const char* text, int length )
{
    int depth = 0;
    for ( int i = 0; i < length && text[i] != '\0'; ++i )
    {
        if ( text[i] == '/' )
        {
            ++depth;
        }
    }
    return depth;
}


int DrawCallTrace::LeafOffset( const char* text )
{
    int offset = 0;
    for ( int i = 0; text && text[i] != '\0'; ++i )
    {
        if ( text[i] == '/' )
        {
            offset = i + 1;
        }
    }
    return offset;
}

} // namespace Rendering
} // namespace SkullbonezCore
