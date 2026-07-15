# Init Startup Decomposition Function Map

Date: 2026-07-15
Source: `SkullbonezSource/Runtime/Init.cpp` at the `nightrunner-15th-july`
campaign start
Owner: runtime shell
Task: `init-startup-decomposition` T1

## Inventory Method

The inventory was generated from both of the plan-mandated views and then
reconciled manually:

- `codegraph node --symbols-only SkullbonezSource/Runtime/Init.cpp` reported
  99 symbols: 87 free functions, 10 structs, one type alias, and one local
  struct destructor.
- `rg -n "^[A-Za-z].*\(" SkullbonezSource/Runtime/Init.cpp` reported exactly
  the same 87 file-scope free-function definitions.
- The source-order pass classified every overload separately and checked the
  supporting types and file-local global at the end of this report.

`Header API` means the declaration must be visible to `Init.cpp` or another
new Startup unit. `Internal` means the definition remains private to its
assigned `.cpp` in an anonymous namespace. `Stays` means the function remains
in `Init.cpp`; every staying helper except `WinMain` remains anonymous-namespace
private.

## Function Assignment

### `Startup/StartupCrashLogging.{h,cpp}` (4)

| Original line | Function | Visibility | Reason |
|---:|---|---|---|
| 100 | `ExceptionCodeName` | Internal | SEH diagnostic spelling helper. |
| 126 | `WriteDebugCrashStack` | Internal | Debug crash-stack implementation. |
| 239 | `DebugUnhandledExceptionFilter` | Internal | Installed only by the crash logger API. |
| 260 | `InstallDebugCrashLogger` | Header API | The sole crash-logging call made by `WinMain`. |

### `Startup/StartupCommandLine.{h,cpp}` (60)

| Original line | Function | Visibility | Reason |
|---:|---|---|---|
| 278 | `FailCommandLineParse` | Header API | Shared error sink for command-line and launch-resolution parsing. |
| 289 | `GetCommandLineError` | Header API | Read by `WinMain` after parse failure. |
| 354 | `IsTokenWhitespace` | Internal | Tokenizer detail. |
| 359 | `TokenizeCommandLine` | Header API | Builds the immutable token view consumed by startup units. |
| 406 | `IsOptionToken` | Internal | Option scanner detail. |
| 411 | `IsOptionValueMissing` | Header API | Probe and launch units preserve existing missing-value behavior. |
| 416 | `OptionTokenMatches` | Internal | Option scanner detail. |
| 421 | `OptionTokenHasAssignedValue` | Internal | `--name=value` scanner detail. |
| 438 | `FindOptionValue(CommandLineView, optionName)` | Header API | Shared exact-spelling option lookup. |
| 460 | `FindOptionValue(CommandLineView, dashedName, underscoredName)` | Header API | Shared alias-aware option lookup. |
| 466 | `HasOption` | Header API | Shared flag presence test used by probes and launch parsing. |
| 480 | `TrimLineInPlace` | Internal | CLI text cleanup detail. |
| 495 | `ParseFloatToken` | Header API | Physics-debug launch overrides use the same strict float parser. |
| 514 | `ParseIntToken` | Internal | Command-line directive implementation. |
| 533 | `ParseUnsignedIntToken` | Internal | Command-line directive implementation. |
| 552 | `CopyOptionPath` | Internal | Command-line path-copy/error implementation. |
| 1141 | `HasFlagDirective` | Internal | Flag-table implementation. |
| 1146 | `FindValueDirective(CliValueDirective)` | Internal | Value-table implementation. |
| 1156 | `FindValueDirective(ConfigCliValueDirective)` | Internal | Config value-table implementation. |
| 1167 | `ApplyCliValueDirectives` | Internal | Template over the private value table. |
| 1183 | `ApplyConfigCliValueDirectives` | Internal | Template over the private config table. |
| 1199 | `ApplyCliFlagDirectives` | Internal | Private flag-table dispatcher. |
| 1329 | `ParseOnOffValue` | Internal | Generic CLI directive implementation. |
| 1355 | `ParseEnvironmentBool` | Internal | Environment compatibility parsing owned by the CLI pass. |
| 1376 | `ParseOptionalOnOffValue` | Header API | Physics-debug launch overrides preserve this optional-value grammar. |
| 1387 | `ParseAllocationGuardModeValue` | Internal | Allocation-guard directive implementation. |
| 1786 | `ParseRendererArg` | Internal | CLI validation for the DX12-only renderer option. |
| 1813 | `ApplyVsyncOverride` | Internal | CLI-to-config override. |
| 1833 | `ApplyCinematicShadowsOverride` | Internal | Config directive callback. |
| 1849 | `ApplyStartupCliValueDirectives` | Internal | Startup/config directive table. |
| 2026 | `ApplyLiveStyleControlDir` | Internal | Run-option directive callback. |
| 2045 | `ApplySceneSnapshotOutPath` | Internal | Run-option directive callback. |
| 2064 | `ApplyMemoryDumpPath` | Internal | Run-option directive callback. |
| 2076 | `ApplyInteractionScriptPath` | Internal | Run-option directive callback. |
| 2093 | `ApplyInteractionReportPath` | Internal | Run-option directive callback. |
| 2109 | `ApplyReplayHashLogPath` | Internal | Replay CLI directive callback. |
| 2127 | `ApplyReplaySaveProbePath` | Internal | Replay CLI directive callback. |
| 2149 | `ApplyReplayLoadPath` | Internal | Replay CLI directive callback. |
| 2168 | `ApplyReplayLoadProbePath` | Internal | Replay CLI directive callback. |
| 2182 | `ApplyReplayRestoreFileProbePath` | Internal | Replay CLI directive callback. |
| 2201 | `ApplyReplayRestoreTargetFileProbePath` | Internal | Replay CLI directive callback. |
| 2220 | `ApplyReplayRestoreBranchFileProbePath` | Internal | Replay CLI directive callback. |
| 2239 | `ApplyReplayRestoreFailureFileProbePath` | Internal | Replay CLI directive callback. |
| 2259 | `ApplyRunCliValueDirectives` | Internal | Run/replay/stress directive table. |
| 2517 | `ApplyGeneratedObjectOverride` | Internal | Generated-object CLI policy. |
| 2612 | `ValidatePhysicsRegressionLog` | Internal | Build-lane validation for a CLI option. |
| 2630 | `ValidatePhysicsCollisionTimeLog` | Internal | Build-lane validation for a CLI option. |
| 2648 | `ValidatePhysicsDiagnostics` | Internal | Build-lane validation for a CLI option. |
| 2665 | `ValidateReplayScrubProbe` | Internal | Replay option compatibility validation. |
| 2682 | `ValidateReplayRestoreProbe` | Internal | Replay option compatibility validation. |
| 2699 | `ValidateReplaySaveProbe` | Internal | Replay option compatibility validation. |
| 2714 | `ValidateReplayLoadProbe` | Internal | Replay option compatibility validation. |
| 2729 | `ValidateReplayRestoreFileProbe` | Internal | Replay option compatibility validation. |
| 2745 | `ValidateReplayRestoreTargetFileProbe` | Internal | Replay option compatibility validation. |
| 2761 | `ValidateReplayRestoreBranchFileProbe` | Internal | Replay option compatibility validation. |
| 2777 | `ValidateReplayRestoreFailureFileProbe` | Internal | Replay option compatibility validation. |
| 2798 | `ParsePhysicsRegressionLogOverride` | Internal | Debug-only CLI path parser. |
| 2817 | `ParsePhysicsCollisionTimeLogOverride` | Internal | Debug-only CLI path parser. |
| 2836 | `ParsePhysicsDiagnosticsPath` | Internal | Debug-only CLI path parser. |
| 2856 | `ParseCommandLine` | Header API | One top-level CLI pass called by `WinMain`. |

### `Startup/StartupProbeHarnesses.{h,cpp}` (4)

| Original line | Function | Visibility | Reason |
|---:|---|---|---|
| 573 | `HandleGenAtlas` | Header API | Early-exit atlas probe called by `WinMain`. |
| 631 | `RunPhysicsRuntimeHandleSmokeSample` | Internal | Implementation detail of the standalone physics probe. |
| 893 | `HandlePhysicsStandaloneSmoke` | Header API | Early-exit physics probe called by `WinMain`. |
| 2547 | `HandleContactAudioSmoke` | Header API | Early-exit audio probe called by `WinMain`. |

### `Startup/StartupLaunchResolution.{h,cpp}` (12)

| Original line | Function | Visibility | Reason |
|---:|---|---|---|
| 1408 | `SceneArgHasPathSyntax` | Internal | Scene path resolution detail. |
| 1415 | `SceneArgHasExtension` | Internal | Scene path resolution detail. |
| 1423 | `FileExistsForLaunch` | Internal | Scene/suite path resolution detail. |
| 1429 | `HeroSceneLaunchPath` | Internal | Built-in hero launch path detail. |
| 1435 | `ResolveSceneLaunchPath` | Internal | Used only while resolving scene arguments. |
| 1469 | `ResolveSuiteLaunchPath` | Internal | Used only while resolving suite arguments. |
| 1497 | `ParsePhysicsDebugMode` | Internal | Physics-debug launch policy detail. |
| 1543 | `ApplyPhysicsDebugComponentOverride` | Internal | Physics-debug launch policy detail. |
| 1577 | `ApplyPhysicsDebugFloatOverride` | Internal | Physics-debug launch policy detail. |
| 1603 | `ParsePhysicsDebugOverrides` | Header API | Called by the top-level command-line pass. |
| 1696 | `ParseSceneArgs` | Header API | Called by the top-level command-line pass. |
| 3109 | `BuildRunStartupOverrides` | Header API | Converts parsed launch policy for `RunApp` without re-parsing. |

### Stays in `Runtime/Init.cpp` (7)

| Original line | Function | Visibility | Reason |
|---:|---|---|---|
| 294 | `ReportStartupFailure` | Stays, internal | Top-level process/window startup failure reporting. |
| 318 | `IsStandardHandleRedirected` | Stays, internal | Console attachment detail owned by process entry. |
| 330 | `AttachParentConsole` | Stays, internal | Process console setup sequenced by `WinMain`. |
| 3069 | `InitRenderBackend` | Stays, internal | Top-level backend construction and capability wiring. |
| 3199 | `RunApp` | Stays, internal | Owns `Run` lifetime and top-level application execution. |
| 3310 | `CleanupWindow` | Stays, internal | Process shutdown order for input, backend, and window. |
| 3341 | `WinMain` | Stays, process entry | Windows entry point and startup/shutdown sequencer. |

## Supporting Symbol Placement

The 87-function reconciliation also fixes the placement of every supporting
file-scope symbol so extraction cannot create an unplanned shared-state bag.

| Original line | Symbol | Placement | Visibility |
|---:|---|---|---|
| 95 | `Json` | `StartupLaunchResolution.cpp` | Internal alias used only for suite JSON. |
| 97 | `g_commandLineError` | `StartupCommandLine.cpp` | Internal fixed error buffer. |
| 349 | `CommandLineView` | `StartupCommandLine.h` | Header value type shared read-only across startup units. |
| 611 | `PhysicsRuntimeHandleSmokeResult` | `StartupProbeHarnesses.cpp` | Internal probe result. |
| 994 | `ParsedArgs` | `StartupCommandLine.h` | Header value type filled once, then consumed by startup sequencing. |
| 1087 | `CliFlagDirective` | `StartupCommandLine.cpp` | Internal directive row. |
| 1098 | `CliValueDirective` | `StartupCommandLine.cpp` | Internal directive row. |
| 1105 | `ConfigCliValueDirective` | `StartupCommandLine.cpp` | Internal directive row. |
| 1115 | `PhysicsDebugComponentDirective` | `StartupLaunchResolution.cpp` | Internal physics-debug row. |
| 1122 | `PhysicsDebugFloatDirective` | `StartupLaunchResolution.cpp` | Internal physics-debug row. |
| 1134 | `GeneratedObjectOverrideDirective` | `StartupCommandLine.cpp` | Internal directive row. |
| 3210 | `ProfilerRenderDiagnosticsLifetime` and destructor | `Init.cpp`, local to `RunApp` | Stays as the narrow lifetime guard. |

## Reconciliation And Boundary Proof

- Assigned free functions: **87 / 87**.
- Target totals: crash logging **4**, command line **60**, probe harnesses
  **4**, launch resolution **12**, stays in `Init.cpp` **7**.
- Header API functions: crash **1**, command line **10**, probes **3**, launch
  resolution **3**. All other moved functions are anonymous-namespace
  internals.
- No header exposes directive tables, probe result state, JSON types, crash
  implementation details, or mutable global state.
- Permitted cross-unit dependencies are headers only:
  `StartupProbeHarnesses.cpp` and `StartupLaunchResolution.cpp` consume the
  command-line value/API header; `StartupCommandLine.cpp` consumes the launch
  resolution header for `ParseSceneArgs` and `ParsePhysicsDebugOverrides`.
- The reciprocal header use is declaration-only: `StartupLaunchResolution.h`
  forward-declares `CommandLineView` and `ParsedArgs`, while its `.cpp` includes
  `StartupCommandLine.h`. No implementation file includes another unit's
  private declarations or source.
- T2 and T3 are verbatim moves. Function names, call positions, string
  literals, exit codes, directive order, and startup sequencing are frozen by
  this map.

Validation: documentation-only inventory; no repository validation required.
