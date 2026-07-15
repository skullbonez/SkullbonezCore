/*
File: StartupLaunchResolution.h
Purpose:
  Publishes scene/suite and physics-debug launch-policy parsing to the top-level
  command-line pass.

Summary:
  During T2 the existing implementations remain in Init.cpp so the CLI owner can
  call their final header seam. T3 moves those bodies verbatim into the matching
  StartupLaunchResolution.cpp owner.

Glossary:
  Launch resolution: Conversion of a CLI scene or suite token into the exact
    repository/file path and startup policy used by Run.
  Physics-debug override: Visualization-only startup request that must not alter
    solver state.

Invariants:
  - Scene/suite paths, diagnostics, and physics-debug messages remain identical.
  - These functions mutate only the caller-owned ParsedArgs and scene list.

Related:
  - StartupCommandLine.h
  - StartupLaunchResolution.cpp
  - Agentic/Plans/TODO/init-startup-decomposition.md
*/
#pragma once

#include <string>
#include <vector>

namespace SkullbonezCore
{
namespace Runtime
{
namespace Startup
{
struct CommandLineView;
struct ParsedArgs;

// Applies visualization-only physics debug options to caller-owned ParsedArgs.
bool ParsePhysicsDebugOverrides( const CommandLineView& commandLine, ParsedArgs& out );

// Resolves the mutually exclusive generated/hero/scene/suite launch choice and
// appends owned paths to sceneList without retaining the token view.
bool ParseSceneArgs( const CommandLineView& commandLine,
                     std::vector<std::string>& sceneList,
                     bool& isSuiteOrSceneMode );

} // namespace Startup
} // namespace Runtime
} // namespace SkullbonezCore
