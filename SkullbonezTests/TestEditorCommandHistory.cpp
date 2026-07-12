/*
File: SkullbonezTests/TestEditorCommandHistory.cpp
Purpose:
  Proves fixed editor history push, undo, redo, branching, overflow, and clear.

Mental model:
  Tests drive only the cursor owner; scene-command application is covered at
  the editor integration boundary.

Glossary:
  Branch truncation: A new command after undo removes all redoable commands.

Invariants:
  - Capacity remains exactly 64 and overflow retains the newest commands.
  - Pending queries do not commit cursor movement.

Related:
  - SkullbonezSource/Runtime/Editor/EditorCommandHistory.h
  - Agentic/Reports/2026-07-12/editor-undo-redo-closure.md
*/
#include "../ThirdPtySource/doctest/doctest.h"

#include "../SkullbonezSource/Runtime/Editor/EditorCommandHistory.h"

using namespace SkullbonezCore::Basics;

namespace
{
EditorCommandEntry MakeEntry( uint32_t id )
{
    EditorCommandEntry entry;
    entry.kind = EditorCommandKind::Transform;
    entry.transformCount = 1;
    entry.transforms[0].sceneObjectId.value = id;
    return entry;
}
} // namespace

TEST_CASE( "EditorCommandHistory supports push undo redo and branch truncation" )
{
    EditorCommandHistory history;
    history.Push( MakeEntry( 1 ) );
    history.Push( MakeEntry( 2 ) );
    REQUIRE( history.PendingUndo() != nullptr );
    CHECK( history.PendingUndo()->transforms[0].sceneObjectId.value == 2u );
    CHECK( history.CommitUndo() );
    CHECK( history.UndoDepth() == 1 );
    CHECK( history.RedoDepth() == 1 );
    REQUIRE( history.PendingRedo() != nullptr );
    CHECK( history.PendingRedo()->transforms[0].sceneObjectId.value == 2u );
    CHECK( history.CommitRedo() );

    CHECK( history.CommitUndo() );
    history.Push( MakeEntry( 3 ) );
    CHECK( history.UndoDepth() == 2 );
    CHECK( history.RedoDepth() == 0 );
    CHECK( history.PendingRedo() == nullptr );
    CHECK( history.PendingUndo()->transforms[0].sceneObjectId.value == 3u );
}

TEST_CASE( "EditorCommandHistory drops oldest on overflow and clear resets depth" )
{
    EditorCommandHistory history;
    for ( uint32_t id = 1; id <= EDITOR_COMMAND_HISTORY_CAPACITY + 3; ++id )
    {
        history.Push( MakeEntry( id ) );
    }
    CHECK( history.StoredCount() == EDITOR_COMMAND_HISTORY_CAPACITY );
    CHECK( history.UndoDepth() == EDITOR_COMMAND_HISTORY_CAPACITY );
    REQUIRE( history.PendingUndo() != nullptr );
    CHECK( history.PendingUndo()->transforms[0].sceneObjectId.value == EDITOR_COMMAND_HISTORY_CAPACITY + 3 );

    for ( std::size_t index = 0; index < EDITOR_COMMAND_HISTORY_CAPACITY - 1; ++index )
    {
        REQUIRE( history.CommitUndo() );
    }
    REQUIRE( history.PendingUndo() != nullptr );
    CHECK( history.PendingUndo()->transforms[0].sceneObjectId.value == 4u );

    history.Clear();
    CHECK( history.UndoDepth() == 0 );
    CHECK( history.RedoDepth() == 0 );
    CHECK( history.PendingUndo() == nullptr );
}

TEST_CASE( "EditorCommandHistory invalidates both branches for an edit without an inverse" )
{
    EditorCommandHistory history;
    history.Push( MakeEntry( 1 ) );
    history.Push( MakeEntry( 2 ) );
    REQUIRE( history.CommitUndo() );
    REQUIRE( history.UndoDepth() == 1 );
    REQUIRE( history.RedoDepth() == 1 );

    history.InvalidateForNonUndoableEdit();
    CHECK( history.UndoDepth() == 0 );
    CHECK( history.RedoDepth() == 0 );
    CHECK( history.StoredCount() == 0 );
}
