/*
File: StartupLaunchResolution.h
Purpose:
  Publishes scene/suite, physics-debug, and final Run launch-policy resolution.

Summary:
  The command-line owner delegates path/policy resolution here, then WinMain
  converts the completed ParsedArgs packet into the value passed to Run.

Glossary:
  Launch resolution: Conversion of a CLI scene or suite token into the exact
    repository/file path and startup policy used by Run.
  Physics-debug override: Visualization-only startup request that must not alter
    solver state.

Invariants:
  - Scene/suite paths, diagnostics, defaults, and physics-debug messages remain
    byte-identical to the pre-split Init.cpp.
  - Functions mutate only caller-owned values and retain no token references.

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
struct RunStartupOverrides;

namespace Startup
{
struct CommandLineView;
struct ParsedArgs;

// Applies visualization-only physics-debug tokens to caller-owned parsed state.
bool ParsePhysicsDebugOverrides( const CommandLineView& commandLine, ParsedArgs& out );

// Resolves one mutually exclusive generated/hero/scene/suite choice into owned
// scene paths without retaining pointers into commandLine.
bool ParseSceneArgs( const CommandLineView& commandLine,
                     std::vector<std::string>& sceneList,
                     bool& isSuiteOrSceneMode );

// Converts parsed policy into the value consumed synchronously by RunApp.
RunStartupOverrides BuildRunStartupOverrides( const ParsedArgs& args );

} // namespace Startup
} // namespace Runtime
} // namespace SkullbonezCore
