/*
File: SceneSaveOperations.cpp
Purpose:
  Implements runtime scene-save path policy and complete publication
  composition for editor, load-only, and editable-replacement entry points.

Summary:
  Each Scene entry validates its caller-selected destination, composes one
  SceneSaveRequest from the three owner publications, and performs one
  synchronous write.

Invariants:
  - Path policy is entry-specific, while serialized owner coverage is identical.
  - A writer failure is returned through recoverable result with its original diagnostics.
  - No owner pointer, publication, or path borrow survives the operation.

Related:
  - SkullbonezSource/Runtime/Scene/SceneSaveOperations.h
  - SkullbonezSource/Runtime/Editor/EditorTools.cpp
  - SkullbonezSource/Scene/SceneSnapshotWriter.cpp
  - Agentic/Reference/engine-glossary.md
*/
#include "SceneSaveOperations.h"

#include "../../Core/SbDiagnosticStore.h"

using SkullbonezCore::GameObjects::PresentationSaveState;
using SkullbonezCore::GameObjects::SceneSaveRequest;
using SkullbonezCore::GameObjects::SceneSessionSaveState;
using SkullbonezCore::GameObjects::SceneSnapshotWriter;
using SkullbonezCore::GameObjects::SceneWorldSaveState;

namespace SkullbonezCore
{
namespace Runtime
{
namespace
{
Core::SbResult SaveCompletePublication( Core::SbDiagnosticStore& diagnostics, const char* path,
                                        const SceneWorldSaveState& world, const SceneSessionSaveState& session,
                                        const PresentationSaveState& presentation )
{
    // Why: these callers are production entry policies, not test adapters.
    // Centralizing only request composition makes a partial publication
    // impossible while each public operation retains its own path policy.
    return SceneSnapshotWriter::Save( diagnostics, SceneSaveRequest { path, world, session, presentation } );
}
} // namespace


Core::SbResult SaveSceneLoadOnlySnapshot( Core::SbDiagnosticStore& diagnostics, const char* path,
                                          const SceneWorldSaveState& world, const SceneSessionSaveState& session,
                                          const PresentationSaveState& presentation )
{
    if ( !path || path[0] == '\0' )
    {
        return diagnostics.Failure( "Runtime/SceneLoadOnly", "Scene snapshot output path is empty." );
    }

    return SaveCompletePublication( diagnostics, path, world, session, presentation );
}


Core::SbResult SaveEditableSceneBeforeReplacement( Core::SbDiagnosticStore& diagnostics, const char* activeScenePath,
                                                   const SceneWorldSaveState& world, const SceneSessionSaveState& session,
                                                   const PresentationSaveState& presentation )
{
    if ( !activeScenePath || activeScenePath[0] == '\0' )
    {
        return diagnostics.Failure( "Runtime/SceneController", "Editable scene has no active authored path to save." );
    }

    return SaveCompletePublication( diagnostics, activeScenePath, world, session, presentation );
}
} // namespace Runtime
} // namespace SkullbonezCore
