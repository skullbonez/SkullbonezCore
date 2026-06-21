/*
File: SkullbonezSource/DrawCallTrace.h
Purpose:
  Declares fixed-capacity renderer draw-call attribution diagnostics.

Mental model:
  Draw submission code records cheap CPU-side facts about GPU draw calls. Render
  pass code opens stable scopes so the profiler UI can explain the frame total
  without allocating, adding GPU timestamp queries, or creating per-draw markers.

Glossary:
  Draw call: One GPU command-list draw submission, such as DrawInstanced or a
    dynamic vertex-buffer flush.
  Trace scope: Stable render-pass or batch name that receives draw counts until
    its scope exits.
  Snapshot: Previous-frame copy read by the UI while the renderer records the
    next frame.

Invariants:
  - Per-draw recording stays allocation-free and does not create GPU timers.
  - Scope names must be string literals or otherwise stable for the frame.

Related:
  - SkullbonezSource/DrawCallTrace.cpp
  - SkullbonezSource/IRenderBackend.h
  - Agentic/Plans/draw-call-trace-tree-plan.md
*/
#pragma once

#include <cstdint>

namespace SkullbonezCore
{
namespace Rendering
{

enum class DrawCallKind
{
    Mesh,
    InstancedMesh,
    DynamicVertexBuffer,
    DebugLines,
    Unknown
};

struct DrawCallRecord
{
    DrawCallKind kind = DrawCallKind::Unknown;
    const char* label = nullptr;
    int vertexCount = 0;
    int instanceCount = 1;
};

struct DrawCallTraceNode
{
    const char* name = nullptr;
    const char* leafName = nullptr;
    uint32_t hash = 0;
    int parentIndex = -1;
    int depth = 0;
    int drawCallCount = 0;
    int vertexCount = 0;
    int instanceCount = 0;
};

struct DrawCallTraceSnapshot
{
    const DrawCallTraceNode* nodes = nullptr;
    int nodeCount = 0;
    int nodeOverflowCount = 0;
    int eventCount = 0;
    int eventOverflowCount = 0;
    int scopeMismatchCount = 0;
};

class DrawCallTrace
{
  public:
    static constexpr int MAX_DRAW_TRACE_NODES = 128;
    static constexpr int MAX_DRAW_TRACE_EVENTS = 512;
    static constexpr int MAX_DRAW_TRACE_DEPTH = 32;
    static constexpr int MAX_DRAW_TRACE_NAME_CHARS = 112;

    DrawCallTrace();

    void BeginFrame();
    void PushScope( const char* fullPathOrLeaf, uint32_t hash );
    void PopScope( uint32_t hash );
    void RecordDrawCall( const DrawCallRecord& record );

    DrawCallTraceSnapshot Snapshot() const;

  private:
    DrawCallTraceNode m_nodes[MAX_DRAW_TRACE_NODES] = {};
    char m_nodeNames[MAX_DRAW_TRACE_NODES][MAX_DRAW_TRACE_NAME_CHARS] = {};
    int m_nodeCount = 0;
    int m_currentNodeIndex = -1;
    int m_scopeStack[MAX_DRAW_TRACE_DEPTH] = {};
    int m_scopeStackDepth = 0;
    int m_nodeOverflowCount = 0;
    int m_eventCount = 0;
    int m_eventOverflowCount = 0;
    int m_scopeMismatchCount = 0;

    DrawCallTraceNode m_snapshotNodes[MAX_DRAW_TRACE_NODES] = {};
    char m_snapshotNodeNames[MAX_DRAW_TRACE_NODES][MAX_DRAW_TRACE_NAME_CHARS] = {};
    int m_snapshotNodeCount = 0;
    int m_snapshotNodeOverflowCount = 0;
    int m_snapshotEventCount = 0;
    int m_snapshotEventOverflowCount = 0;
    int m_snapshotScopeMismatchCount = 0;

    void PublishSnapshot();
    void ResetCurrentFrame();
    int EnsureRootNode();
    int EnsurePathNode( const char* path, uint32_t fullHash );
    int EnsureRelativeNode( const char* leaf );
    int AppendNode( const char* path, int pathLength, uint32_t hash, int parentIndex, int depth );
    int FindNode( uint32_t hash ) const;
    void PushCurrentNode( int nodeIndex );
    static uint32_t HashStringRange( const char* text, int length );
    static bool HasPathSeparator( const char* text );
    static int PathDepth( const char* text, int length );
    static int LeafOffset( const char* text );
};

} // namespace Rendering
} // namespace SkullbonezCore
