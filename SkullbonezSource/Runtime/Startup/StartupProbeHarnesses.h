/*
File: StartupProbeHarnesses.h
Purpose:
  Publishes the three synchronous early-exit startup probes used by WinMain.

Summary:
  Callers pass the already-tokenized command line or parsed startup values and
  receive whether a probe claimed the launch plus its exact process exit code.

Glossary:
  Claimed launch: True when a probe option was present and ordinary startup must
    stop after returning outExitCode.
  Early-exit probe: Bounded validation or generation mode that does not enter
    the application run loop.

Invariants:
  - Functions retain no references to command-line, parsed, or config storage.
  - False leaves ordinary startup sequencing in control.
  - True always publishes the probe's final process exit code.

Related:
  - StartupProbeHarnesses.cpp
  - StartupCommandLine.h
  - Agentic/Reports/2026-07-15/init-startup-decomposition-map.md
*/
#pragma once

namespace SkullbonezCore
{
namespace Core
{
class EngineConfig;
}

namespace Runtime
{
namespace Startup
{
struct CommandLineView;
struct ParsedArgs;

// Applies the fixed flag-directive table in compatibility order.
void ApplyProbeFlagDirectives( const CommandLineView& commandLine, ParsedArgs& out );

// Validates build-lane probe options and resolves Debug-only output paths in
// the same order used by the original top-level parser.
bool ValidateProbeCommandLineOptions( const CommandLineView& commandLine, ParsedArgs& out );

// True means atlas generation claimed startup and outExitCode is final.
bool HandleGenAtlas( const CommandLineView& commandLine, int& outExitCode );

// True means the isolated physics/runtime-handle smoke claimed startup and
// ordinary owner construction must not continue.
bool HandlePhysicsStandaloneSmoke( const CommandLineView& commandLine, int& outExitCode );

// Uses already-loaded config values without retaining either input. True means
// contact-audio smoke claimed startup and outExitCode is final.
bool HandleContactAudioSmoke( const ParsedArgs& args, const Core::EngineConfig& config, int& outExitCode );

} // namespace Startup
} // namespace Runtime
} // namespace SkullbonezCore
