/*
File: SceneSaveOperations.h
Purpose:
  Declares runtime Scene entry policies that persist complete scene-owner
  publications to caller-selected paths.

Summary:
  Scene validates explicit load-only and editable-replacement destinations,
  then composes the same complete world, session, and presentation publications
  immediately before serialization. Numbered editor path policy stays with the
  Editor caller and RuntimeFileWriter.

Glossary:
  Load-only save: Command-line scene-load probe that optionally writes the
    fully loaded scene without entering the frame loop.
  Editable replacement: Save of a live editable scene to its active authored
    path before a later cold scene load can replace its stores.

Invariants:
  - No operation retains a publication or borrowed store after returning.
  - Every successful entry serializes world, session, and presentation values;
    no entry can construct a partial SceneSaveRequest.
  - The focused SceneSnapshotWriter tests execute both Scene policies and the
    Editor-owned numbered-path composition.

Related:
  - SkullbonezSource/Scene/SceneSnapshotWriter.h
  - SkullbonezSource/Runtime/Editor/EditorTools.cpp
  - SkullbonezSource/Runtime/App/Run.cpp
  - SkullbonezSource/Runtime/Scene/SceneController.Load.cpp
  - Agentic/Reference/engine-glossary.md
*/
#pragma once

#include "../../Core/SbResult.h"
#include "../../Scene/SceneSnapshotWriter.h"

namespace SkullbonezCore
{
namespace Runtime
{
// Saves the scene-load-only publication to the caller's explicit CLI path.
Core::SbResult SaveSceneLoadOnlySnapshot( Core::SbDiagnosticStore& diagnostics, const char* path,
                                          const GameObjects::SceneWorldSaveState& world,
                                          const GameObjects::SceneSessionSaveState& session,
                                          const GameObjects::PresentationSaveState& presentation );

// Saves a live editable scene to its active authored path before replacement.
Core::SbResult SaveEditableSceneBeforeReplacement( Core::SbDiagnosticStore& diagnostics, const char* activeScenePath,
                                                   const GameObjects::SceneWorldSaveState& world,
                                                   const GameObjects::SceneSessionSaveState& session,
                                                   const GameObjects::PresentationSaveState& presentation );
} // namespace Runtime
} // namespace SkullbonezCore
