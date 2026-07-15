# Init Startup Decomposition — Split The 3,495-Line Startup File By Responsibility

Date: 2026-07-15
Status: Active — 0/5 tasks complete
Impact area: `Runtime/Init.cpp`, new `Runtime/Startup/*` translation units,
project/filter files, startup CLI behavior (must not change)
Owner: runtime shell

## Problem And Evidence

`SkullbonezSource/Runtime/Init.cpp` is 3,495 lines (tip `ece58a62`) mixing at
least four unrelated responsibilities in one flat file of free functions:

1. SEH crash handling and debug crash logging (`ExceptionCodeName`,
   `WriteDebugCrashStack`, `DebugUnhandledExceptionFilter`,
   `InstallDebugCrashLogger`, ~lines 100-277).
2. A hand-rolled CLI layer: tokenizer, option matching, value/flag directive
   tables, parse-error reporting, and typed value parsers
   (`TokenizeCommandLine`, `FindOptionValue`, `ApplyCliValueDirectives`,
   `ParseFloatToken`/`ParseIntToken`/`ParseOnOffValue`, ~lines 278-560 and
   1141-1400).
3. Standalone probe/smoke harnesses launched from CLI (`HandleGenAtlas`,
   `RunPhysicsRuntimeHandleSmokeSample`, `HandlePhysicsStandaloneSmoke`,
   ~lines 573-1140).
4. Scene/suite launch resolution and startup policy assembly
   (`ResolveSceneLaunchPath`, `ResolveSuiteLaunchPath`, `ParseSceneArgs`,
   physics-debug override parsing, worker-pool bring-up around line 3413).

The 2026-07-15 god-object review flagged this (finding: startup god file).
The round-3 exclusion that parked "Init.cpp decomposition" was lifted by the
owner on 2026-07-15 when this plan was commissioned; that ruling supersedes
the round-3 record.

Unlike `PhysicsWorld`, `Init.cpp` is free functions, not a class — so a
by-responsibility file split IS the correct decomposition here, and the
god-object TU-split prohibition does not apply. What is prohibited is
cross-module reach: each new unit must expose a small header API and keep its
internals in an anonymous namespace.

## Goal

`Init.cpp` shrinks to program entry, top-level argument orchestration, and
startup sequencing (~600 lines or less), with four new units under
`SkullbonezSource/Runtime/Startup/`: crash logging, CLI parsing,
CLI probe harnesses, and launch resolution. Identical startup behavior:
every CLI flag, error message, exit code, and probe output is byte-identical.

## Non-Goals

- No CLI syntax, message, default, or exit-code changes of any kind.
- No new abstractions: free functions move under namespaces; no classes,
  registries, or option frameworks are introduced.
- No behavior-bearing reordering of startup sequencing in `Init.cpp`.
- No physics/render source changes (the smoke harness moves verbatim).

## Tasks

- [ ] T1 — Inventory and target map. Enumerate every free function in
      `Init.cpp` with `rg -n '^[A-Za-z].*\('` plus manual reconciliation, and
      commit the assignment table (function → target unit or stays) to
      `Agentic/Reports/2026-07-15/init-startup-decomposition-map.md`. Four
      targets: `Startup/StartupCrashLogging.{h,cpp}`,
      `Startup/StartupCommandLine.{h,cpp}`,
      `Startup/StartupProbeHarnesses.{h,cpp}`,
      `Startup/StartupLaunchResolution.{h,cpp}`. The map marks which
      functions become header API vs anonymous-namespace internals.
- [ ] T2 — Extract crash logging + CLI parsing units (verbatim moves, headers
      per the map, vcxproj/filters in the same commit, learning headers per
      the comment guide). `Init.cpp` includes the new headers; no logic edits.
- [ ] T3 — Extract probe harnesses + launch resolution units the same way.
      The probe harnesses keep their exact output strings and exit codes;
      grep-proof the moved string literals are unchanged.
- [ ] T4 — Residue pass: `Init.cpp` retains entry point, argument
      orchestration, worker-pool/config bring-up, and startup failure
      reporting; record the final line counts in the map report. Confirm no
      new unit includes another new unit's internals (headers only).
- [ ] T5 — Final gate. `Init*` maps to `tools\validate_full.bat`. Because the
      broad gate exercises the CLI paths (scene load, probes, automation
      smoke), it doubles as the behavior proof; additionally run one manual
      `--physics-standalone-smoke`-style probe and one bad-flag error case and
      paste both outputs to show identical strings/exit codes. Comment audit
      over all touched files.

## Dependencies And Decisions

- Owner ruling 2026-07-15: round-3 parking of Init decomposition is lifted;
  free-function file split by responsibility is the approved shape.
- Independent of the other two 2026-07-15 campaigns; safe to run first
  (cold path, no determinism exposure).

## Acceptance

- `Init.cpp` ≤ ~600 lines; four Startup units each ≤ ~900 lines; map report
  reconciles every original function.
- Grep-proof: no crash/CLI/probe/resolution logic remains in `Init.cpp`
  beyond orchestration calls; no cross-unit internal includes.
- `validate_full` passes; probe and error-case outputs byte-identical.

## Validation

- `tools\validate_full.bat` plus the two recorded manual CLI probes, output
  pasted at closure. No baselines may change.
