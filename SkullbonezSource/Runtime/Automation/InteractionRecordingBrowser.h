/*
File: SkullbonezSource/Runtime/Automation/InteractionRecordingBrowser.h
Purpose:
  Declares recording discovery and visible playback launch operations.

Summary:
  Automation owns filesystem validation and child-process launch for recorded
  interaction manifests. UI retains only detached newest-first labels and emits
  an index; it never constructs a command line or starts a process.

Glossary:
  Recording catalog: Detached manifest paths and display labels published to UI.
  Playback launch: A child Automation process given one manifest plus evidence paths.

Invariants:
  - Only complete `interaction.json` manifests under TestOutput/recordings are listed.
  - Launch uses the Automation executable and writes report/trace evidence beside the manifest.
  - No shell parses the manifest path.

Related:
  - SkullbonezSource/Runtime/Automation/InteractionRecordingBrowser.cpp
  - SkullbonezSource/Runtime/Scene/SceneNavigationModel.h
  - Agentic/Reference/engine-glossary.md
*/
#pragma once

#include "../../Core/SbResult.h"

namespace SkullbonezCore
{
namespace Core
{
class SbDiagnosticStore;
}
namespace UI
{
struct InteractionRecordingBrowserState;
}
namespace Runtime
{
Core::SbResult LaunchInteractionRecording( Core::SbDiagnosticStore& diagnostics,
                                           const UI::InteractionRecordingBrowserState& recordings, int index );
}
} // namespace SkullbonezCore
