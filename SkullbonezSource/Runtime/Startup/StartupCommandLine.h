/*
File: StartupCommandLine.h
Purpose:
  Publishes the parsed startup value packet and the small command-line API used
  by process entry, launch resolution, and early-exit probes.

Summary:
  Startup tokenization happens once. The resulting CommandLineView is borrowed
  read-only by specialized startup units, while ParsedArgs owns every parsed
  value until WinMain hands it to Run.

Glossary:
  Command-line view: Token vector produced from the raw WinMain string.
  Parsed arguments: Process-start policy values resolved before runtime owners
    are constructed.
  Alias spelling: Dashed or underscored option name retained for CLI
    compatibility.

Invariants:
  - Existing flag spellings, defaults, messages, directive order, and exit
    behavior are compatibility surface and must remain byte-identical.
  - ParsedArgs owns all path buffers consumed during synchronous startup.
  - Helpers never retain a CommandLineView or ParsedArgs reference.

Related:
  - StartupCommandLine.cpp
  - StartupLaunchResolution.h
  - StartupProbeHarnesses.h
  - Agentic/Reference/runtime-reference.md
*/
#pragma once

#include "../Replay/ReplayRecorder.h"
#include "../RunLaunchOptions.h"

#include <cstddef>
#include <string>
#include <vector>

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
struct CommandLineView
{
    std::vector<std::string> tokens;
};

struct ParsedArgs
{
    // Parsed command-line state. Defaults here are part of startup behavior:
    // validation scripts, desktop shortcuts, and scene automation all rely on
    // omitted flags producing these exact policies.
    std::vector<std::string> sceneList;
    bool isSuiteOrSceneMode = false;
    float timeScaleOverride = 0.0f; // 0 = not set
    bool fixedStep = false;
    unsigned int seedOverride = 0;  // 0 = not set
    bool noWater = false;
    bool noSleep = false;
    bool noContactAudio = false;
    bool contactAudioSmoke = false;
    bool hasTornadoOverride = false;
    bool tornadoEnabled = false;
    bool tornadoVectors = false;
    bool hasCinematicRenderingOverride = false;
    bool cinematicRendering = false;
    bool hasCinematicShadowsOverride = false;
    bool cinematicShadows = false;
    bool interactiveRun = false;
    int frameCountOverride = -1;
    bool sceneLoadOnly = false;
    bool demoHeroStyle = false;
    bool uiStress = false;
    unsigned int uiStressSeed = 0x7F4A7C15u;
    int uiStressActions = 5;
    bool graphicsStress = false;
    unsigned int graphicsStressSeed = 0xC11E2026u;
    int graphicsStressActions = 12;
    int graphicsStressSceneIntervalFrames = 45;
    int graphicsStressMemoryIntervalFrames = 1800;
    Allocation::RuntimeAllocationGuardMode allocationGuardMode = Allocation::RuntimeAllocationGuardMode::Off;
    bool replayRecording = true;
    bool replayExplicit = false;
    int replaySeconds = REPLAY_PAST_BUFFER_SECONDS;
    bool replayScrubProbe = false;
    float replayScrubProbeNormalized = 0.25f;
    bool replayRestoreProbe = false;
    float replayRestoreProbeNormalized = 0.25f;
    bool replaySaveProbe = false;
    char replaySaveProbePath[260] = {};
    bool replayLoad = false;
    bool replayLoadProbe = false;
    char replayLoadPath[260] = {};
    bool replayRestoreFileProbe = false;
    char replayRestoreFileProbePath[260] = {};
    bool replayRestoreTargetFileProbe = false;
    char replayRestoreTargetFileProbePath[260] = {};
    bool replayRestoreBranchFileProbe = false;
    char replayRestoreBranchFileProbePath[260] = {};
    bool replayRestoreFailureFileProbe = false;
    char replayRestoreFailureFileProbePath[260] = {};
    char replayHashLogPath[260] = {};
    char liveStyleControlDir[260] = {};
    char sceneSnapshotOutPath[260] = {};
    char memoryDumpPath[260] = {};
    char interactionScriptPath[260] = {};
    char interactionReportPath[260] = {};
    bool suppressExitDialog = false;
    bool automationWindowHidden = false;
    bool showProfiler = false;
    bool hideTopText = false;
    bool showBroadphaseVisualizer = false;
    bool workerSelfTest = false;
    GeneratedObjectTypeOverride objectTypeOverride = GeneratedObjectTypeOverride::Mixed;
    bool hasPhysicsDebugFlagsOverride = false;
    uint32_t physicsDebugFlagsOverride = Physics::PHYSICS_DEBUG_NONE;
    bool hasPhysicsDebugTransparentOverride = false;
    bool physicsDebugTransparentOverride = false;
    bool hasPhysicsDebugAlphaOverride = false;
    float physicsDebugAlphaOverride = 0.28f;
    bool hasPhysicsDebugContactLingerOverride = false;
    float physicsDebugContactLingerOverride = 0.45f;
#ifdef _DEBUG
    char physicsRegressionLogOverride[256] = {};
    char physicsCollisionTimeLogOverride[256] = {};
    char physicsDiagnosticsPath[256] = {};
#endif
    bool physicsDiagnosticsRequested = false;
    bool fixedStepForcedByPhysicsDiagnostics = false;
    bool dumpConfig = false;
    bool dumpAssets = false;
#if defined( SKULLBONEZ_PROFILE_ENABLED ) && defined( SKULLBONEZ_PLATFORM_PROFILER_PIX )
    bool platformProfilerMarkers = true;
#else
    bool platformProfilerMarkers = false;
#endif
    bool platformProfilerMarkersExplicit = false;
};

// Records and prints the first recoverable startup parse failure. The formatted
// message remains available through GetCommandLineError for WinMain reporting.
bool FailCommandLineParse( const char* fmt, ... );
const char* GetCommandLineError();

// Owns token strings in the returned value; callers may borrow c_str pointers
// only while the CommandLineView remains alive and unmodified.
CommandLineView TokenizeCommandLine( const char* cmdLine );

// These lookup helpers return pointers into CommandLineView-owned strings and
// never retain the view.
bool IsOptionValueMissing( const char* value );
const char* FindOptionValue( const CommandLineView& commandLine, const char* optionName );
const char* FindOptionValue( const CommandLineView& commandLine, const char* dashedName, const char* underscoredName );
bool HasOption( const CommandLineView& commandLine, const char* optionName );
bool ParseFloatToken( const char* value, float& out );
bool ParseOptionalOnOffValue( const char* value, bool& out );

// Pure token/path helpers shared by responsibility owners. They retain no
// command-line pointers and report errors through FailCommandLineParse.
bool CopyCommandLinePath( const char* value, const char* optionName, char* outPath, size_t outPathSize );
bool ParseIntCommandLineToken( const char* value, int& out );
bool ParseUnsignedCommandLineToken( const char* value, unsigned int& out );
bool ParseAllocationGuardCommandLineToken( const char* value, Allocation::RuntimeAllocationGuardMode& out );

// Fills caller-owned config/argument state synchronously. False means WinMain
// must report GetCommandLineError and stop before constructing runtime owners.
bool ParseCommandLine( const CommandLineView& commandLine, Core::EngineConfig& config, ParsedArgs& out );

} // namespace Startup
} // namespace Runtime
} // namespace SkullbonezCore
