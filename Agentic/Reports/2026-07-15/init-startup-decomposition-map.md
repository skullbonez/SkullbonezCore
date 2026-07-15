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

## Final T5 Reconciliation

The tables below preserve the T1 assignment that governed the mechanical T2
and T3 extractions. T4 initially redistributed generic parser families to hit
an approximate size target. The independent T5 ownership review rejected that
shape because ordinary flag and build-lane parsing had crossed into the probe
owner. Remediation restored generic parsing to `StartupCommandLine`; only
value-bearing run/replay/UI-stress/graphics-stress launch policy moved to
`StartupLaunchResolution`.

| Final owner | Original functions | New seam wrappers | Final implementation lines |
|---|---:|---:|---:|
| `StartupCrashLogging.cpp` | 4 | 0 | 231 |
| `StartupCommandLine.cpp` | 44 | 4 | 1,181 |
| `StartupProbeHarnesses.cpp` | 4 | 0 | 516 |
| `StartupLaunchResolution.cpp` | 26 | 0 | 962 |
| `Init.cpp` | 7 | 0 | 453 |
| Deleted dead private helpers | 2 | 0 | N/A |
| **Total** | **87 / 87 reconciled** | **4 public token/path wrappers** | |

The two deleted original helpers are the `CliValueDirective` overload of
`FindValueDirective` and its `ApplyCliValueDirectives` template. Moving their
only run-value table consumer to launch resolution left them with no call site;
their private row type was deleted with them. The config-specific overload and
template remain command-line-owned and live.

The 14 original command-line functions whose final owner is launch resolution
are `ApplyLiveStyleControlDir`, `ApplySceneSnapshotOutPath`,
`ApplyMemoryDumpPath`, `ApplyInteractionScriptPath`,
`ApplyInteractionReportPath`, `ApplyReplayHashLogPath`,
`ApplyReplaySaveProbePath`, `ApplyReplayLoadPath`,
`ApplyReplayLoadProbePath`, `ApplyReplayRestoreFileProbePath`,
`ApplyReplayRestoreTargetFileProbePath`,
`ApplyReplayRestoreBranchFileProbePath`,
`ApplyReplayRestoreFailureFileProbePath`, and
`ApplyRunCliValueDirectives`. This is cohesive launch policy: the table writes
caller-owned values consumed directly by `RunApp` or converted by
`BuildRunStartupOverrides`; generic presence-only flags and build-lane
validation remain command-line policy.

Final header APIs are crash **1**, command line **14** (the original 10 plus
four pure token/path seam wrappers), probes **3**, and launch resolution **4**
(the original three plus `ApplyRunCliValueDirectives`). `CliFlagDirective`,
`ConfigCliValueDirective`, and build-lane validators are private to the
command-line translation unit. `RunCliValueDirective` is private to launch
resolution. Probe internals expose no parser table or validator; the now-unused
generic `CliValueDirective` machinery was deleted at closure.

## T1 Function Assignment (Historical Extraction Map)

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
- T1 header API functions were crash **1**, command line **10**, probes **3**,
  and launch resolution **3**. The final API reconciliation above supersedes
  these extraction-time totals.
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
- T2 and T3 were verbatim moves. The final T5 reconciliation above records the
  reviewed T4 redistribution while keeping function names, call positions,
  string literals, exit codes, directive order, and startup sequencing frozen.

Validation: documentation-only inventory; no repository validation required.

## T2 Extraction Evidence

- `StartupCrashLogging`: 4/4 mapped definitions moved; 231 lines after format.
- `StartupCommandLine`: 60/60 mapped definitions moved; 1,873 lines after
  format. T4 must redistribute the launch/probe-owned directive families so
  the final four-unit `~900`-line acceptance bound remains honest.
- `Init.cpp`: 1,530 lines after T2; the launch-resolution header is an explicit
  temporary seam until its mapped bodies move in T3.
- Behavior proof: the ordered 319-string literal inventory in the moved CLI
  definitions is identical to the pre-move source, and the extracted function
  names reconcile 60/60 against this report.
- Registration proof: `tools\validate_project_filters.bat` passed with 704
  project items and 704 filter items across the production projects.
- Gate: `tools\validate_full.bat` passed in 314.66 s with zero-warning
  Profile/Automation/Debug builds, all CPU suites and runtime lanes, zero DX12
  validation errors, committed screenshot matches, and a 44,401-line
  byte-exact physics baseline match.
- Comment audit: 7/7 touched source-bearing files inspected, 0 deferred.
  No baseline or golden refresh.

## T3 Extraction Evidence

- `StartupProbeHarnesses`: 4/4 mapped functions moved; 546 implementation
  lines after formatting and public-contract comments.
- `StartupLaunchResolution`: 12/12 mapped functions moved; 541 implementation
  lines after formatting and public-contract comments.
- `Init.cpp`: 568 lines after T3, already inside the final `~600` bound.
- Behavior proof: mechanical comparison against commit `07350f4d` covered all
  16 moved definitions and their 146 string literals in order with zero
  mismatches.
- Registration proof: the broad gate reconciled 707 project items with 707
  filter items across the production projects.
- Gate: `tools\validate_full.bat` passed in 311.71 s with zero-warning
  Profile/Automation/Debug builds, all CPU suites and runtime lanes, zero DX12
  validation errors, committed screenshot matches, and a 44,401-line
  byte-exact physics baseline match.
- Comment audit: 5/5 touched source-bearing files inspected, 0 deferred.
  No baseline or golden refresh.

## T4 Residue And Boundary Evidence

- The first T4 implementation reached `Init.cpp` **453**,
  `StartupCommandLine.cpp` **824**, `StartupCrashLogging.cpp` **231**,
  `StartupLaunchResolution.cpp` **960**, and
  `StartupProbeHarnesses.cpp` **880**. Independent review later rejected this
  balance because generic parser policy had crossed into the probe owner.
  Remediation produced the authoritative final counts in the reconciliation
  table above and revised the approximate owner cap to 1,250 lines.
- `Init.cpp` retains the Windows entry point, process console/COM sequencing,
  worker/config bring-up, window and DX12 construction, `Run` lifetime,
  startup failure reporting, and shutdown order. Parser, probe, path/suite,
  and crash implementation residue is absent.
- Cross-unit proof: an implementation-include search under `Runtime/Startup`
  found no `.cpp` or `.inl` include. Units communicate only through the four
  focused owner headers; directive tables and validation helpers remain
  translation-unit private.
- Behavior proof against T3 commit `0c90c928`: 29 literal-bearing redistributed
  functions and 210 ordered string literals compared with zero sequence
  mismatches. After
  excluding preprocessor include names, the complete command/launch/probe
  runtime-literal multiset is unchanged at 473/473 with zero differences.
- Focused Profile build passed in 13.39 s with zero warnings and zero errors
  after explicit qualification replaced the former transitive `Hardware`
  namespace reach.
- Gate history: the first `tools\validate_full.bat` attempt stopped after
  13.18 s at the header-format post-pass, before compilation or runtime lanes.
  After the targeted header alignment fix, the retry passed in 284.47 s with
  707/707 project/filter items, zero-warning Profile/Automation/Debug builds,
  all CPU and runtime lanes, zero DX12 validation errors, committed screenshot
  matches, and the 44,401-line byte-exact physics baseline.
- The initial T4 comment audit inspected 7/7 source-bearing files with 0
  deferred. T5 repeats the audit over all ten whole-plan source/tool files
  after ownership remediation. No baseline or golden refresh.

## T5 Closure Evidence

The first independent review rejected the size-balanced T4 shape because
generic flag parsing and build-lane validation had crossed into the probe
owner. Remediation moved those tables and helpers back to command-line parsing,
deleted the unused generic value-table machinery, restored the probe header to
its three real synchronous probes, and reconciled this report. The focused
follow-up found no blocking issue.

Final behavior proofs against T3 commit `0c90c928`:

- `ParseCommandLine` is token-for-token identical after whitespace
  normalization: **4,575 / 4,575 normalized characters**.
- The 29 literal-bearing functions affected by T4 retain **210 / 210 ordered
  literals** with zero sequence mismatches.
- The complete command/launch/probe runtime-literal multiset remains
  **473 / 473** with zero differences.
- No Startup implementation includes another `.cpp` or `.inl`; only focused
  headers cross owner boundaries.

Final full gate:

```text
tools\validate_full.bat
VALIDATE_FULL: DEFAULT GATE PASSED
Build succeeded. 0 Warning(s), 0 Error(s).
DX12 validation errors: 0
PASS: DX12 screenshots match committed baselines.
PASS: physics_regression_varied.csv (44401 lines, byte-exact match)
exit 0; elapsed 302.49 s
```

The final gate reconciled 707 project items and 707 filter items, ran all CPU,
Automation, replay/prediction, DX12, and physics lanes, and used the exact
post-review source. Earlier T5 gate runs predated ownership remediation and are
not closure evidence.

Manual standalone probe from the final Debug executable:

```text
Command: Debug\SKULLBONEZ_CORE.exe --physics-standalone-smoke
[platform-profiler] Platform profiler marker emission enabled.
[physics-standalone-smoke] bodies=2 steps=4 final_position=(3.000000,9.000000,-2.000000) final_velocity=(2.000000,-4.000000,0.000000) secondary_position=(-4.000000,8.000000,1.500000) secondary_velocity=(-1.000000,-7.000000,0.500000) secondary_step=pass lifecycle_checks=pass contacts=2 contact_hash=0x5DBDF5257E90EA9B runtime_mirror_checks=pass hash=0xB3B6CEEFB6CCAFFA
[physics-runtime-handle-smoke] bodies=2 colliders=2 render_instances=2 point_joints=1 handle_a=(0,1) store_handles=pass render_mirror=pass joint_handles=pass collider_refresh=pass reorder_state=pass creation_atomic=pass deletion_atomic=pass mutation_handle=pass
PASS: standalone physics and runtime handle mirror smoke matched expected state.
stderr: empty
exit 0; elapsed 1.03 s
```

Manual bad-flag case from the same executable. The test closed only the owned
`Command line parse failed` dialog so the program returned its natural code:

```text
Command: Debug\SKULLBONEZ_CORE.exe --renderer invalid
stdout: empty
stderr:
ERROR: --renderer expects dx12. GL and DX11 are retired runtime choices.
FATAL: --renderer expects dx12. GL and DX11 are retired runtime choices.
exit 1; elapsed 0.53 s
```

Whole-plan comment audit scope was generated from
`git diff --name-only ed2e1c800736d43b7ddd58deeda8a0aa737faeb5..HEAD`
plus the final uncommitted remediation. Inspected **10 / 10** source-bearing
files, **0 deferred**: `Init.cpp`, all eight Startup `.cpp`/`.h` owner files,
and `tools/validate_project_filters.py`. Learning headers, compatibility/order
invariants, and local lifetime/hazard comments are present; no wording remains
for human approval.

### Rubber-Duck Accounting

| Plan | Duck run | Reviewer | Reason | Prompt chars | Response chars | Tokens | Elapsed | Verdict | Follow-up |
|---|---|---|---|---:|---:|---|---|---|---|
| `init-startup-decomposition` | `init-startup-decomposition-duck-01` | `/root/init_startup_duck_01` | Initial closure review | 1,198 | 4,104 | n/a | 4.7 min | Two ownership/map blockers | Remediation required |
| `init-startup-decomposition` | `init-startup-decomposition-duck-02` | `/root/init_startup_duck_01` | Follow-up after remediation | 954 | 1,851 | n/a | ~2 min | No blockers; two small residues | Residues removed before final gate |

No baseline, golden, screenshot reference, scene, shader, or physics CSV was
changed or refreshed.
