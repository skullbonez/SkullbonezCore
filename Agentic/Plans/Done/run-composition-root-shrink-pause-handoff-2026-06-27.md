# Run Composition Root Shrink Pause Handoff

Date: 2026-06-27
Branch: `nightrunner-26th-July`
Worktree: `C:\SkullbonezCore`
Status: Closed after final 2026-06-27 shrink pass; moved to Done with
`run-composition-root-shrink-plan.md`.

## Current Position

Before this handoff file was added, the branch was clean and synced with
`origin/nightrunner-26th-July`. After this file is created, the only expected
working-tree change is this untracked pause handoff.

Latest commits:

- `3ec37688 refactor: move perf log lifecycle into diagnostics`
- `6c752763 refactor: return scene coordinator intents`
- `ab57ccdf refactor: remove Run scene-control wrappers`

Latest completed slice:

- Removed `Run::TickPerfLog` from `Run.h` and `RunFrame.cpp`.
- `RunFrame` now calls `DiagnosticsRuntime::TickPerfLog(...)` directly with
  the same pass, frame, physics-time, and render-time context.
- Periodic perf memory checkpointing moved into
  `RuntimeDiagnostics::TickPerfLog(...)`.
- Scene-load perf-log reset/config/open behavior moved behind diagnostics APIs:
  `ResetPerfLogForSceneLoad`, `ConfigurePerfLogFlush`, `OpenScenePerfLog`, and
  `PerfTestActive`.
- `RunFrame` and `RunInput` now read perf-test state through
  `DiagnosticsRuntime::PerfTestActive()`.
- `tools/check_runtime_boundaries.py` lowered the `Run.h` private-method
  ratchet from 216 to 215.
- Boundary guardrails now reject `Run::TickPerfLog` and direct `RunScene`
  perf-log lifecycle field/file access from returning.

The main plan and session handoff were updated:

- `Agentic/Plans/run-composition-root-shrink-plan.md`
- `Agentic/SessionState.md`

## Validation Evidence

Diagnostics perf-log lifecycle slice validation passed:

- Targeted build: `tools\validate_build.bat Profile`
  - Log: `TestOutput\validation\agent_logs\perf_log_lifecycle_profile_build.log`
  - Result: passed, 0 warnings, 0 errors, 43.85s.
- Fast gate: `tools\validate_fast.bat`
  - Log: `TestOutput\validation\agent_logs\perf_log_lifecycle_validate_fast.log`
  - Result: formatting, project filters, runtime boundaries, and Profile/Debug
    builds passed.
- Direct runtime boundary gate:
  `python tools\check_runtime_boundaries.py --repo .`
  - Log:
    `TestOutput\validation\agent_logs\perf_log_lifecycle_runtime_boundaries.log`
  - Result: passed with 0 errors.
- Broad gate: `tools\validate_full.bat`
  - Log: `TestOutput\validation\agent_logs\perf_log_lifecycle_validate_full.log`
  - Result: project filters, runtime boundaries, Profile/Debug builds, DX12
    validation errors 0, screenshots matched baselines, and
    `physics_regression_solver.csv` matched byte-exactly.

Rubber-duck review:

- Reviewer Einstein found no blocking defect.
- Non-blocking note: the first `RunScene` guardrail rejected every `fopen_s`.
  That was narrowed to perf-log lifecycle access before validation.

## Pause State

After pushing `3ec37688`, a new Carmack-style architecture assessment agent was
started to pick the next slice. The user paused the task before the assessment
completed, and the agent was shut down. There is no in-flight worker or
background task to wait for.

No new architecture recommendation was accepted after `3ec37688`.

## What Is Left

Continue the `Run` shrink plan from `Agentic/Plans/run-composition-root-shrink-plan.md`.
The highest-value remaining categories are:

- Scene load ownership:
  - Finish moving reset/teardown/generated/authored setup phases out of
    `Run::LoadScene`.
  - Keep the composition root sequencing explicit, but move coherent scene
    policy and setup work into `Runtime/Scene/*`.
- Replay tool/helper ownership:
  - Continue moving replay UI/tool behavior into `ReplayRuntime`, especially
    helper clusters that still require direct `Run` declarations.
- Render-host splitting:
  - Split `RuntimeRenderHost` into narrower render-facing views so editor,
    replay, diagnostics, UI, and world/model rendering do not route through a
    broad host boundary.
- Shared cine/path cleanup:
  - Deduplicate scene browser, cinematic mode, and path-normalization helpers
    that were left as compatibility debt during the scene coordinator slices.
- Boundary ratchets:
  - For every shrink slice, delete at least one `Run.h` private method,
    lower/update the measured ratchet, and add a focused guardrail blocking the
    removed wrapper or callback surface from returning.

Final resolution:

- `2a5da3e1` localized render input builders and deleted two `Run.h`
  declarations.
- `5b5ca6a4` moved runtime window-size helpers and deleted two `Run.h`
  declarations.
- `4887ad5e` moved runtime cinematic helpers and deleted three `Run.h`
  declarations.
- The `Run.h` private-method ratchet is now 129.
- End-only rubber-duck review found no blocking issue in the pushed chunks or
  validation evidence. Broad scene-load and replay-tool ownership remains
  future architecture work, not unfinished work inside this closed handoff.

## Resume Recipe

1. Run the startup contract from `AGENTS.md`.
2. Confirm clean branch state:
   `git status --short --branch`.
3. Read the current section of
   `Agentic/Plans/run-composition-root-shrink-plan.md`.
4. Ask a fresh read-only architecture assessor:
   "You are John Carmack. Assess the engine architecture."
5. Pick one small slice that deletes real `Run.h` declarations.
6. Use the repo-local orchestrator skill:
   `Agentic/Skills/orchestrator/SKILL.md`.
7. Implement, run an independent rubber-duck review, fix blockers, validate,
   commit, push, and update handoff docs.

Validation selection on resume:

- Documentation-only updates: no validation required.
- Small runtime refactor: targeted `tools\validate_build.bat Profile`, rubber
  duck, `tools\validate_fast.bat`, direct
  `python tools\check_runtime_boundaries.py --repo .`, and
  `tools\validate_full.bat`.
- Scene/render/physics-specific changes should follow the mapping in
  `AGENTS.md`.
