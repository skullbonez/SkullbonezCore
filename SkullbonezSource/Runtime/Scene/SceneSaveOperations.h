/*
File: SceneSaveOperations.h
Purpose:
  Declares the three runtime entry policies that persist complete scene-owner
  publications.

Summary:
  Editor saves choose a collision-free numbered artifact, scene-load-only saves
  require an explicit output path, and editable-scene replacement overwrites
  the active authored path. Each operation composes the same complete world,
  session, and presentation publications immediately before serialization.

Glossary:
  Save publication: Detached owner-produced value containing that owner's
    persisted fields; the world publication borrows stable stores synchronously.
  Load-only save: Command-line scene-load probe that optionally writes the
    fully loaded scene without entering the frame loop.
  Editable replacement: Save of a live editable scene to its active authored
    path before a later cold scene load can replace its stores.

Invariants:
  - No operation retains a publication or borrowed store after returning.
  - Every successful entry serializes world, session, and presentation values;
    no entry can construct a partial SceneSaveRequest.
  - The focused SceneSnapshotWriter tests execute all three production policies.

Related:
  - SkullbonezSource/Scene/SceneSnapshotWriter.h
  - SkullbonezSource/Runtime/Editor/EditorTools.cpp
  - SkullbonezSource/Runtime/App/Run.cpp
  - SkullbonezSource/Runtime/Scene/SceneController.Load.cpp
*/
#pragma once

#include "../../Core/SbResult.h"
#include "../../Scene/SceneSnapshotWriter.h"

namespace SkullbonezCore
{
namespace Runtime
{
// Returns false only when the numbered editor path could not be selected. When
// true, outSaveResult reports the synchronous serializer result.
bool TrySaveNextEditorSceneSnapshot( int& sequence,
                                     const GameObjects::SceneWorldSaveState& world,
                                     const GameObjects::SceneSessionSaveState& session,
                                     const GameObjects::PresentationSaveState& presentation,
                                     Core::SbResult& outSaveResult );

// Saves the scene-load-only publication to the caller's explicit CLI path.
Core::SbResult SaveSceneLoadOnlySnapshot( const char* path,
                                          const GameObjects::SceneWorldSaveState& world,
                                          const GameObjects::SceneSessionSaveState& session,
                                          const GameObjects::PresentationSaveState& presentation );

// Saves a live editable scene to its active authored path before replacement.
Core::SbResult SaveEditableSceneBeforeReplacement( const char* activeScenePath,
                                                   const GameObjects::SceneWorldSaveState& world,
                                                   const GameObjects::SceneSessionSaveState& session,
                                                   const GameObjects::PresentationSaveState& presentation );
} // namespace Runtime
} // namespace SkullbonezCore
