/*
File: SceneSaveOperations.cpp
Purpose:
  Implements runtime scene-save path policy and complete publication
  composition for editor, load-only, and editable-replacement entry points.

Summary:
  Each entry validates or selects its destination, composes one SceneSaveRequest
  from the three owner publications, and performs one synchronous write.

Glossary:
  Numbered artifact: Editor snapshot whose sequence advances past existing
    files so an operator save never overwrites an earlier snapshot.
  Publication: Owner-produced save value; SceneWorld's publication borrows its
    stores only for the duration of this operation.

Invariants:
  - Path policy is entry-specific, while serialized owner coverage is identical.
  - A writer failure is returned through Lane R with its original diagnostics.
  - No owner pointer, publication, or path borrow survives the operation.

Related:
  - SkullbonezSource/Runtime/Scene/SceneSaveOperations.h
  - SkullbonezSource/Runtime/Tools/RuntimeFileWriter.cpp
  - SkullbonezSource/Scene/SceneSnapshotWriter.cpp
*/
#include "SceneSaveOperations.h"

#include "../Tools/RuntimeFileWriter.h"

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
Core::SbResult SaveCompletePublication( const char* path,
                                        const SceneWorldSaveState& world,
                                        const SceneSessionSaveState& session,
                                        const PresentationSaveState& presentation )
{
    // Why: these callers are production entry policies, not test adapters.
    // Centralizing only request composition makes a partial publication
    // impossible while each public operation retains its own path policy.
    return SceneSnapshotWriter::Save( SceneSaveRequest { path, world, session, presentation } );
}
} // namespace


bool TrySaveNextEditorSceneSnapshot( int& sequence,
                                     const SceneWorldSaveState& world,
                                     const SceneSessionSaveState& session,
                                     const PresentationSaveState& presentation,
                                     Core::SbResult& outSaveResult )
{
    char path[256] = {};
    if ( !RuntimeFileWriter::NextNumberedPath( path,
                                               sizeof( path ),
                                               "Scenes",
                                               "snapshot_",
                                               ".scene.json",
                                               sequence,
                                               100 ) )
    {
        return false;
    }

    outSaveResult = SaveCompletePublication( path, world, session, presentation );
    return true;
}


Core::SbResult SaveSceneLoadOnlySnapshot( const char* path,
                                          const SceneWorldSaveState& world,
                                          const SceneSessionSaveState& session,
                                          const PresentationSaveState& presentation )
{
    if ( !path || path[0] == '\0' )
    {
        return Core::SbResult::Failure( "Runtime/SceneLoadOnly", "Scene snapshot output path is empty." );
    }

    return SaveCompletePublication( path, world, session, presentation );
}


Core::SbResult SaveEditableSceneBeforeReplacement( const char* activeScenePath,
                                                   const SceneWorldSaveState& world,
                                                   const SceneSessionSaveState& session,
                                                   const PresentationSaveState& presentation )
{
    if ( !activeScenePath || activeScenePath[0] == '\0' )
    {
        return Core::SbResult::Failure( "Runtime/SceneController",
                                        "Editable scene has no active authored path to save." );
    }

    return SaveCompletePublication( activeScenePath, world, session, presentation );
}
} // namespace Runtime
} // namespace SkullbonezCore
