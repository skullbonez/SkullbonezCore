/*
File: SkullbonezSource/Runtime/Editor/EditorCommandHistory.cpp
Purpose:
  Implements fixed editor history cursor, branch, overflow, and clear rules.

Summary:
  The prefix before the cursor is applied; the suffix after it is redoable.
  Push overwrites the suffix, while full-capacity push shifts the bounded array
  once and keeps the newest 64 commands.

Glossary:
  Redo suffix: Stored commands after the cursor that are not currently applied.
  Overflow shift: Fixed 63-entry copy that discards the oldest command.
  Clean cursor: History position represented by the last successful authored save.

Invariants:
  - Pending queries never move the cursor.
  - Only successful scene application calls CommitUndo or CommitRedo.
  - No heap-backed container or growth operation is used.

Related:
  - SkullbonezSource/Runtime/Editor/EditorCommandHistory.h
  - SkullbonezTests/TestEditorCommandHistory.cpp
*/
#include "EditorCommandHistory.h"

namespace SkullbonezCore
{
namespace Runtime
{
bool TryCaptureEditorPrimitiveShape( const Math::CollisionDetection::CollisionShapeReference& shape,
                                     EditorPrimitiveShapeSnapshot& outSnapshot )
{
    using namespace Math::CollisionDetection;
    outSnapshot = {};

    if ( const BoundingSphere* sphere = GetShapeIf<BoundingSphere>( &shape ) )
    {
        outSnapshot.kind = EditorPrimitiveShapeKind::Sphere;
        outSnapshot.dimensions.x = sphere->GetRadius();
        outSnapshot.localPosition = sphere->GetPosition();
        outSnapshot.dragCoefficient = sphere->GetDragCoefficient();
        return true;
    }

    if ( const BoundingBox* box = GetShapeIf<BoundingBox>( &shape ) )
    {
        outSnapshot.kind = EditorPrimitiveShapeKind::Box;
        outSnapshot.dimensions = box->GetHalfExtents();
        outSnapshot.localPosition = box->GetPosition();
        return true;
    }

    return false;
}


bool TryBuildEditorPrimitiveShape( const EditorPrimitiveShapeSnapshot& snapshot,
                                   Math::CollisionDetection::CollisionShape& outShape )
{
    using namespace Math::CollisionDetection;

    if ( snapshot.kind == EditorPrimitiveShapeKind::Sphere )
    {
        outShape = BoundingSphere( snapshot.dimensions.x, snapshot.localPosition, snapshot.dragCoefficient );
        return true;
    }

    if ( snapshot.kind == EditorPrimitiveShapeKind::Box )
    {
        outShape = BoundingBox( snapshot.dimensions, snapshot.localPosition );
        return true;
    }

    return false;
}


void EditorCommandHistory::Clear()
{
    m_count = 0;
    m_cursor = 0;
    m_cleanCursor = 0;
}


void EditorCommandHistory::InvalidateForNonUndoableEdit()
{

    // Hazard: retaining either side of history across an edit with no inverse
    // would let a later undo/redo apply facts captured for a different world.
    Clear();
    m_cleanCursor = EDITOR_COMMAND_HISTORY_CAPACITY + 1;
}


void EditorCommandHistory::Push( const EditorCommandEntry& entry )
{

    if ( entry.kind == EditorCommandKind::None )
    {
        return;
    }

    // Hazard: a branch from before the clean cursor removes the only route
    // back to the saved state, so equality with a future cursor is no longer
    // meaningful until the next successful save.

    if ( m_cleanCursor <= EDITOR_COMMAND_HISTORY_CAPACITY && m_cleanCursor > m_cursor )
    {
        m_cleanCursor = EDITOR_COMMAND_HISTORY_CAPACITY + 1;
    }

    m_count = m_cursor;

    if ( m_count < EDITOR_COMMAND_HISTORY_CAPACITY )
    {
        m_entries[m_count++] = entry;
        m_cursor = m_count;
        return;
    }

    // Invariant: overflow is bounded editor work. The oldest inverse command
    // is discarded without allocating or changing the remaining order.

    if ( m_cleanCursor == 0 )
    {
        m_cleanCursor = EDITOR_COMMAND_HISTORY_CAPACITY + 1;
    }
    else if ( m_cleanCursor <= EDITOR_COMMAND_HISTORY_CAPACITY )
    {
        --m_cleanCursor;
    }

    for ( std::size_t index = 1; index < EDITOR_COMMAND_HISTORY_CAPACITY; ++index )
    {
        m_entries[index - 1] = m_entries[index];
    }

    m_entries[EDITOR_COMMAND_HISTORY_CAPACITY - 1] = entry;
    m_count = EDITOR_COMMAND_HISTORY_CAPACITY;
    m_cursor = m_count;
}


const EditorCommandEntry* EditorCommandHistory::PendingUndo() const
{
    return m_cursor > 0 ? &m_entries[m_cursor - 1] : nullptr;
}


const EditorCommandEntry* EditorCommandHistory::PendingRedo() const
{
    return m_cursor < m_count ? &m_entries[m_cursor] : nullptr;
}


bool EditorCommandHistory::CommitUndo()
{

    if ( m_cursor == 0 )
    {
        return false;
    }

    --m_cursor;
    return true;
}


bool EditorCommandHistory::CommitRedo()
{

    if ( m_cursor >= m_count )
    {
        return false;
    }

    ++m_cursor;
    return true;
}


void EditorCommandHistory::MarkClean()
{
    m_cleanCursor = m_cursor;
}


bool EditorCommandHistory::IsDirty() const
{
    return m_cleanCursor > EDITOR_COMMAND_HISTORY_CAPACITY || m_cursor != m_cleanCursor;
}


std::size_t EditorCommandHistory::UndoDepth() const
{
    return m_cursor;
}


std::size_t EditorCommandHistory::RedoDepth() const
{
    return m_count - m_cursor;
}


std::size_t EditorCommandHistory::StoredCount() const
{
    return m_count;
}
} // namespace Runtime
} // namespace SkullbonezCore
