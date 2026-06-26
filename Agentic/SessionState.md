# SkullbonezCore Session State

Keep this file short. Put detailed history in task-specific plans, reports, or
audits when it is still useful.

## Current State

| Field | Value |
|-------|-------|
| Branch | `nightrunner-26th-July` in worktree `C:\SkullbonezCore` |
| Last committed milestone | Runtime run composition-root shrink launcher helper slice is validated for commit: ray-test line clear/add/tick and launcher model/terrain hit tests moved into `RuntimeTools`, five private `Run.h` declarations were deleted, and `tools/check_runtime_boundaries.py` now ratchets the `Run.h` private method count. |
| Active objective | Continue `Agentic/Plans/run-composition-root-shrink-plan.md` with the repo-local orchestrator skill; the plan remains active after this helper slice. |
| Pending work | Next launcher shrink slice should move or delete the still-live `Run::` fire dispatch, laser/projectile behavior, and launcher repro snapshot helpers. Do not skip an independent rubber-duck review before validation and commit. |
| Blockers | None known. |
| Orchestrator policy | The old `Agentic/Orchestrator` JSON policy/queue/state-machine path was removed; use the `orchestrator` skill instead. |
| Worktree expectation | Do not assume cleanliness; run `git status --short --branch` before editing or committing. |
| Validation | Physics body-store aggressive ownership is covered by `tools\validate_physics.bat` (`TestOutput\validation\physics_body_store_validate_physics.log`) and `tools\validate_perf.bat` (`TestOutput\validation\physics_body_store_validate_perf_rerun.log`). The launcher helper shrink slice is covered by `tools\validate_fast.bat` (`TestOutput\validation\run_composition_launcher_tools_validate_fast_final.log`) and `tools\validate_full.bat` (`TestOutput\validation\run_composition_launcher_tools_validate_full.log`); fast and full gates passed with 0-warning/error builds, DX12 validation errors 0, matching screenshots, and byte-exact physics CSV. |

## Active Notes

- This workspace expects Windows x64, VS2022 C++ tools, Python, Pillow, and Git
  for build and validation work.
- `git` may not be on PATH in fresh shells. Run `tools\find_git.bat` or use the
  validation scripts, which call it where needed.
- Repository validation scripts are pre-commit/PR gates, not as-you-go checks.
  During implementation, run only targeted builds, launches, focused tests, or
  inspections that answer the current fix question.
- DX12 is the only runtime renderer. OpenGL and DX11 backend files and shader
  families are retired; final parity evidence is archived under
  `Agentic/Reports/2026-06-15/final-legacy-renderer-parity/`.
- Do not commit, push, merge, or submit PRs on `main` without explicit user
  confirmation.
- Do not kill `SKULLBONEZ_CORE.exe` by name. Kill only by PID from a process you
  launched.
- Time user-requested work and report elapsed wall-clock time in the final
  answer or handoff.
- Implementing work from `Agentic/Plans` defaults to
  `Agentic/Skills/orchestrator/SKILL.md`.
- Runtime run decomposition Phase 2C removed direct `Run&` ownership from
  `RuntimeRenderer` and render passes, but `RuntimeRenderHost` is intentionally
  still a broad bridge over Run-owned editor, replay, scene/UI, physics-debug,
  timing, world, and model state. Later phases should narrow those services
  instead of treating the host as a final renderer boundary.
- Runtime run decomposition Phase 4C now routes authored scene setup and ragdoll
  joint/sleep commands through `Physics::PhysicsEngine`; ragdoll body creation
  still uses `GameModelCollection` until model construction itself is lifted out
  of collection ownership.
- Runtime run decomposition Phase 4C launcher migration added
  `PhysicsEngine::ApplyBodyImpulse()` and routes launcher laser/projectile wake
  operations through the physics facade while preserving collection-backed body
  storage.
- Runtime run decomposition Phase 4C editor-tool migration routes mouse-pickup
  impulses, gizmo motion wakeups, and placement wake/sleep commands through
  `PhysicsEngine`; editor shape/pose mutation still happens on `GameModel`
  during this compatibility slice.
- Runtime run decomposition Phase 4C frame migration routes replay-applied
  editor transform wakeups and restore-target physics stepping through
  `PhysicsEngine`, leaving broader replay snapshot ownership for the replay
  runtime phase.
- Runtime run decomposition Phase 4C replay migration routes replay sample
  restore, solver snapshot capture/restore, prediction stepping, velocity-edit
  wakeups, and prediction diagnostics suppression through `PhysicsEngine`; a
  dedicated `ReplayRuntime` remains the Phase 5 owner boundary.
- Runtime run decomposition Phase 4C scene initial impulse migration added
  `PhysicsEngine::SetPendingBodyImpulse()` and routes authored/generated scene
  initial force setup through the physics facade while preserving model creation
  order and collection-backed body storage.
- Runtime run decomposition Phase 4D collection-boundary cleanup removes
  obsolete `GameModelCollection` friendship for snapshot writing, diagnostics,
  persistent contact solving, and sleep propagation by routing those readers
  through named collection APIs.
- Runtime run decomposition Phase 4D SkullScope diagnostics cleanup replaces
  SkullScope friendship across `GameModelCollection`, `PhysicsEngine`,
  `PhysicsScene`, and `PhysicsWorld` with a read-only
  `PhysicsWorld::DiagnosticsView` exposed through the physics facade.
- Phase 4D SkullScope diagnostics validation: Profile build 13.74s
  (`TestOutput\validation\phase4d_skullscope_view_profile_build.log`), format
  6.52s, physics 69.04s, full 22.33s, SkullScope query baseline refresh 31.90s,
  and final deep physics 74.10s
  (`TestOutput\validation\phase4d_skullscope_view_validate_physics_deep.log`).
  The refreshed `physics_query_varied.json` only adds the existing
  `object_friction_coeff` runtime diagnostics config field to stored query
  summaries. SkullScope trace accounting is logged in
  `TestOutput\validation\phase4d_skullscope_view_query_sizes.log`.
- Runtime run decomposition Phase 4D solver-context cleanup removes the remaining
  `PhysicsWorld` helper friendships by passing explicit persistent-solver and
  sleep-propagation contexts, and by routing regression diagnostics through
  `PhysicsWorld::DiagnosticsView`.
- Phase 4D solver-context validation: Profile build 56.02s
  (`TestOutput\validation\phase4d_solver_context_profile_build.log`), format
  6.54s, physics 234.55s, full 22.24s, and perf 19.69s. `validate_perf`
  completed with advisory whole-frame `physics_bench` warnings; reviewed markers
  show `Frame/Physics` at -4.2% avg and persistent contacts at +3.3% avg,
  within noise for the touched solver path.
- Runtime run decomposition Phase 5 replay-owner slice introduces
  `Runtime\Replay\ReplayRuntime` as the owner of the presentation replay,
  solver replay, event recorder, and live branch provenance. `Run` no longer
  stores those recorder fields directly; compatibility accessors keep legacy
  replay callers stable for later extraction slices. The project-filter
  validator now recognizes `ReplayRuntime` under `Runtime\Replay`.
- Phase 5 replay-owner validation: Profile build 12.96s
  (`TestOutput\validation\phase5_replay_runtime_owner_profile_build_rerun.log`),
  project filters 0.72s, format 6.54s, fast gate 234.78s, and full gate 24.81s
  (`TestOutput\validation\phase5_replay_runtime_owner_validate_full_rerun.log`).
  Initial checks caught and fixed missed multi-line replay exporter references
  and the missing project-filter rule before the final gates passed.
- Runtime run decomposition Phase 5 capture/event slice moves recording
  configuration, solver hash-log path derivation, timeline reset, branch/event
  stamping for capture, event recording, and replay save/export delegation into
  `ReplayRuntime`. `Run` still builds scene/world/model capture inputs and still
  has compatibility recorder accessors for scrub/restore/render/path code that
  will move in later Phase 5 slices.
- Phase 5 capture/event validation: Profile build 37.40s
  (`TestOutput\validation\phase5_replay_runtime_capture_profile_build.log`),
  format 6.61s, and full gate 247.38s
  (`TestOutput\validation\phase5_replay_runtime_capture_validate_full.log`).
  Full gate passed project filters, Profile/Debug builds with 0 warnings/errors,
  DX12 validation errors 0 with screenshots matching baselines, and byte-exact
  `physics_regression_solver.csv`.
- Runtime run decomposition Phase 5 render-state slice moves presentation,
  solver, and prediction render pose override/restore logic into
  `ReplayRuntime`, including the hidden-unmatched-body behavior and render pose
  backups. `Run::Render()` now has one replay render-state apply call and one
  restore call around `RuntimeRenderer::RenderFrame()`. The replay prediction
  body/frame structs also live in the replay subsystem header so
  `ReplayRuntime` can consume prediction poses directly.
- Phase 5 render-state validation: Profile build 37.68s
  (`TestOutput\validation\phase5_replay_render_state_profile_build.log`),
  format 6.65s, and full gate 56.92s
  (`TestOutput\validation\phase5_replay_render_state_validate_full.log`). Full
  gate passed project filters, Profile/Debug builds with 0 warnings/errors,
  DX12 validation errors 0 with screenshots matching baselines, and byte-exact
  `physics_regression_solver.csv`.
- Runtime run decomposition Phase 5 render-owned-state slice moves replay focus
  mask storage and replay launcher visual backup storage into `ReplayRuntime`.
  `Run` still computes the mask and copies live launcher visual state as the
  compatibility bridge while the remaining scrub/prediction state moves later
  in Phase 5.
- Phase 5 render-owned-state validation: Profile build 37.11s
  (`TestOutput\validation\phase5_replay_render_owned_state_profile_build.log`),
  format 6.54s, and full gate 55.22s
  (`TestOutput\validation\phase5_replay_render_owned_state_validate_full.log`).
  Full gate passed project filters, Profile/Debug builds with 0 warnings/errors,
  DX12 validation errors 0 with screenshots matching baselines, and byte-exact
  `physics_regression_solver.csv`. Rubber-duck reviewer Carver reported no
  blocking code defect and identified the full gate as the only required
  evidence before commit.
- Runtime run decomposition Phase 5 replay-state-owner slice moves replay
  interaction state definitions and stored instances into `ReplayRuntime`.
  `Run` call sites now reach loaded presentation, scrubber, camera/path,
  prediction, cause tree, and velocity-edit state through replay-runtime
  accessors; `RuntimeCameraMode.h` holds the shared camera mode needed by
  replay camera restore state.
- Phase 5 replay-state-owner validation: final Profile build 37.34s
  (`TestOutput\validation\phase5_replay_state_owner_profile_build_final.log`),
  final format 6.56s, final project-filter check 0.69s, fast gate 119.24s
  (`TestOutput\validation\phase5_replay_state_owner_validate_fast.log`), and
  full gate 24.39s
  (`TestOutput\validation\phase5_replay_state_owner_validate_full.log`). Full
  gate passed project filters, Profile/Debug builds with 0 warnings/errors,
  DX12 validation errors 0 with screenshots matching baselines, and byte-exact
  `physics_regression_solver.csv`. Rubber-duck reviewer Godel reported no
  blocking code defect; after the follow-up setter change, Godel confirmed the
  editor velocity-edit ownership leak was fixed.
- Runtime run decomposition Phase 5 ghost-request slice makes
  `ReplayRuntime` produce replay prediction ghost draw requests while
  `RunRender` remains the actual draw-call consumer. The request producer owns
  prediction-frame sampling, body-id/ragdoll filtering, alpha selection, and
  request buffer reuse for the ghost overlay path.
- Phase 5 ghost-request validation: final format 6.62s, final Profile build
  117.34s
  (`TestOutput\validation\phase5_replay_ghost_requests_profile_build_rerun.log`),
  and full gate 131.89s
  (`TestOutput\validation\phase5_replay_ghost_requests_validate_full.log`).
  Full gate passed project filters, Profile/Debug builds with 0 warnings/errors,
  DX12 validation errors 0 with screenshots matching baselines, and byte-exact
  `physics_regression_solver.csv`. Rubber-duck reviewer Dirac found no behavior
  defect in sampling, alpha, body filtering, or draw-state restore path; the
  suggested request-buffer reserve was added before validation.
- Runtime run decomposition Phase 6 launcher/ray-test slice adds
  `RuntimeTools` and moves launcher ray-test state plus `LauncherLaser`
  ownership out of `Run`. Existing launcher, replay visual capture/restore,
  render-host binding, and backend resource reset behavior remains on the
  compatibility path through `RuntimeTools` accessors.
- Phase 6 launcher/ray-test validation: final format 6.64s, project-filter
  check 0.71s, final Profile build 118.23s
  (`TestOutput\validation\phase6_runtime_tools_launcher_profile_build_rerun.log`),
  fast gate 120.48s
  (`TestOutput\validation\phase6_runtime_tools_launcher_validate_fast.log`), and
  full gate 25.22s
  (`TestOutput\validation\phase6_runtime_tools_launcher_validate_full.log`).
  Full gate passed project filters, Profile/Debug builds with 0 warnings/errors,
  DX12 validation errors 0 with screenshots matching baselines, and byte-exact
  `physics_regression_solver.csv`. Rubber-duck reviewer Socrates found no
  behavior defect and confirmed the slice satisfies the first one-tool-at-a-time
  Phase 6 extraction while manipulator/editor ownership remains for later slices.
- Runtime run decomposition Phase 6 mouse-pickup/manipulator slice moves
  `RunMousePickupState` into `RuntimeTools` and routes manipulator picking,
  target update, capture release, physics-step impulse, angular-velocity
  preservation, render-host binding, and overlay reads through the
  compatibility accessor.
- Phase 6 mouse-pickup validation: final format 6.68s, project-filter check
  0.70s, final Profile build 117.53s
  (`TestOutput\validation\phase6_runtime_tools_mouse_pickup_profile_build_rerun.log`),
  and full gate 132.49s
  (`TestOutput\validation\phase6_runtime_tools_mouse_pickup_validate_full.log`).
  Full gate passed project filters, Profile/Debug builds with 0 warnings/errors,
  DX12 validation errors 0 with screenshots matching baselines, and byte-exact
  `physics_regression_solver.csv`. Rubber-duck reviewer Gibbs found no blocking
  defect and confirmed mouse capture release, picking, target update, angular
  velocity preservation, and impulse application stayed mechanically equivalent.
- Runtime run decomposition Phase 6 editor/tracer slice moves
  `RunEditorPlacementState` and `RunEditorTracer` ownership into
  `RuntimeTools`. `Run` now reaches editor placement, gizmo drag/selection,
  replay overlay suppression, editor tracer lines, and render-host binding
  through `RuntimeTools` accessors while the render host continues to borrow
  the same editor state view for draw-time passes.
- Phase 6 editor/tracer validation: final format 6.56s, final Profile build
  117.81s
  (`TestOutput\validation\phase6_runtime_tools_editor_profile_build_rerun.log`),
  and full gate 131.71s
  (`TestOutput\validation\phase6_runtime_tools_editor_validate_full.log`).
  Full gate passed project filters, Profile/Debug builds with 0 warnings/errors,
  DX12 validation errors 0 with screenshots matching baselines, and byte-exact
  `physics_regression_solver.csv`. Rubber-duck reviewer Gauss found no blocking
  defect and confirmed the render-host lifetime, input mode resolution, replay
  overlay, and editor placement restore paths stayed behavior-preserving.
- Runtime run decomposition Phase 7 diagnostics-owner slice adds
  `DiagnosticsRuntime` as the owner for the existing capture and diagnostics
  controllers. `Run` now reaches screenshot automation, perf logging, and
  SkullScope physics diagnostics through `m_diagnosticsRuntime` while preserving
  existing controller APIs, artifact formatting, output paths, and
  `EngineContext` borrowed binding types.
- Phase 7 diagnostics-owner validation: final format 6.56s, project-filter
  check 0.69s, final Profile build 117.93s
  (`TestOutput\validation\phase7_diagnostics_runtime_owner_profile_build_rerun.log`),
  fast gate 120.57s
  (`TestOutput\validation\phase7_diagnostics_runtime_owner_validate_fast.log`),
  and full gate 24.79s
  (`TestOutput\validation\phase7_diagnostics_runtime_owner_validate_full.log`).
  Full gate passed project filters, Profile/Debug builds with 0 warnings/errors,
  DX12 validation errors 0 with screenshots matching baselines, and byte-exact
  `physics_regression_solver.csv`. Rubber-duck reviewer Boole found no blocking
  defect and confirmed EngineContext lifetimes, screenshot automation,
  perf-log open/close/flush behavior, SkullScope path flow, and project/filter
  metadata stayed behavior-preserving.
- Runtime run decomposition Phase 7 diagnostics-facade/UI-stress slice moves
  deterministic UI stress state into `DiagnosticsRuntime` and routes
  perf-log, screenshot, replay-probe, and SkullScope diagnostic entry points
  through the diagnostics owner. `RuntimeDiagnostics` static helpers are now
  hidden behind diagnostics-layer methods for `Run` call sites.
- Phase 7 diagnostics-facade/UI-stress validation: final format 6.74s, final
  Profile build 118.90s
  (`TestOutput\validation\phase7_diagnostics_runtime_facade_profile_build_rerun.log`),
  full gate 132.26s
  (`TestOutput\validation\phase7_diagnostics_runtime_facade_validate_full.log`),
  UI stress gate 15.86s
  (`TestOutput\validation\phase7_diagnostics_runtime_facade_validate_ui_stress.log`),
  and demo stress gate 16.86s
  (`TestOutput\validation\phase7_diagnostics_runtime_facade_validate_demo_stress.log`).
  Full gate passed project filters, Profile/Debug builds with 0 warnings/errors,
  DX12 validation errors 0 with screenshots matching baselines, and byte-exact
  `physics_regression_solver.csv`; both stress gates reported DX12 validation
  errors 0. Rubber-duck reviewer Ohm found no blocking defect and confirmed the
  UI stress LCG/action path and scene/CLI stress setup stayed mechanically
  equivalent.
- Runtime run decomposition Phase 8 boundary-lock slice removes remaining pure
  `Run` forwarding wrappers for replay recorders and scene object population,
  documents the remaining composition-root state in `Run.h`, and adds
  `tools\validate_runtime_boundaries.bat` plus
  `tools\check_runtime_boundaries.py`. `validate_fast`, `validate_full`, and
  `validate_select runtime-boundaries` now fail if `Run.h` regains render pass
  classes, replay recorder fields, tool transient fields, scene population
  helper declarations, or if runtime subsystem headers store `Run` directly.
- Phase 8 boundary-lock validation: final runtime-boundary check 0.17s
  (`TestOutput\validation\phase8_runtime_boundaries_post_review_fix_rerun.log`),
  expanded synthetic boundary self-test caught the wrapped field declarations
  and stored `Run` pointer/reference forms reviewers called out, final fast gate
  12.64s
  (`TestOutput\validation\phase8_runtime_boundary_validate_fast_final.log`),
  `validate_select runtime-boundaries` 3.59s
  (`TestOutput\validation\phase8_runtime_boundary_validate_select_runtime_boundaries_final.log`),
  and final full gate 22.14s
  (`TestOutput\validation\phase8_runtime_boundary_validate_full_final.log`).
  Full gate passed project filters, runtime boundaries, Profile/Debug builds
  with 0 warnings/errors, DX12 validation errors 0 with screenshots matching
  baselines, and byte-exact `physics_regression_solver.csv`. Rubber-duck
  reviewers Mendel, Popper, and Hegel found no remaining blocking defect after
  Popper's false-negative findings were fixed. Residual include cleanup remains
  limited by current by-value composition-root members.

## Current Work Items

| Item | Status | Notes |
|------|--------|-------|
| DX12-only renderer retirement | Done | Archived in `Agentic/Plans/Done/dx12-only-renderer-retirement-plan.md`; DX12 is the production renderer and DX12-only validation is the safety net. |
| Render resource lifetime | Done | Archived in `Agentic/Plans/Done/render-resource-lifetime-plan.md`; current lifecycle phases, release hooks, source records, and device-lost diagnostics are in place. |
| Render pipeline extraction | Done | Archived in `Agentic/Plans/Done/render-pipeline-extraction-plan.md`; pass bodies and resources live outside the former monolithic frame renderer. |
| Shader architecture cleanup | Done | Archived in `Agentic/Plans/Done/shader-architecture-cleanup-plan.md`; object material contracts, typed upload paths, shader contract checking, and the `t4` material table landed on `main`. |
| DX12 descriptor/upload/root-signature cleanup | Done | Archived in `Agentic/Plans/Done/dx12-descriptor-upload-root-signature-plan.md`; ordinary raster ABI is `b0 + t0..t4` with named descriptor/upload accounting. |
| Material system v1 object slice | Done | Archived in `Agentic/Plans/Done/material-system-v1-implementation-plan.md`; named material assets and terrain/water/post unification should be new focused work. |
| Agent documentation alignment | Done | Archived in `Agentic/Plans/Done/agent-docs-alignment-plan.md`; startup, dirty-worktree, scoped instruction, review, and agent-orchestration guidance are now centralized. |
| Agent orchestrator skill | Active | The old JSON/Python control plane is retired. `Agentic/Skills/orchestrator/SKILL.md` is the active coordinator contract for plan queues, fresh worker agents, rubber-duck review agents, validation, commits, pushes, and handoffs. |
| Runtime interaction controller | Done | Archived in `Agentic/Plans/Done/runtime-interaction-controller-plan.md`; central workspace/owner policy now coordinates Inspect/Edit/Replay/Launcher/Manipulator transitions, stepping, and stale interaction cleanup. |
| Physics playground refactor and prefix cleanup | Done | Completed on `major-refactor` through `Agentic/Plans/physics-playground-refactor-and-file-prefix-cleanup-plan.md`; source prefix cleanup, module folders, runtime/physics/render/editor boundaries, dead-code audit, and full comment pass are committed and pushed. |
| Catto physics solver finalisation | Done | Persistent Catto rows, terrain shared row pipeline, SkullScope query support, and updated deterministic baselines are on `main`. |
| Post-PR73 roadmap follow-up | Done | Runtime extraction review fixes and validation report are recorded in `Agentic/Reports/2026-06-16/post-pr73-roadmap-review-fixes/validation-report.md`. |
| Water rendering cleanup | Active plan | `Agentic/Plans/water-rendering-cleanup-plan.md` remains the focused renderer plan for water material/intersection quality work. |
| Render graph completion | Active plan | `Agentic/Plans/dx12-render-graph-completion-plan.md` remains the focused DX12 resource-state ownership plan. |
| Architecture pass follow-up | Active reference | `Agentic/Plans/architecture_pass_2026-06-02.md` remains the broad checkpoint for runtime, physics data, asset, parser, and render graph boundaries. |
| Authoritative replay rollback | Done | Archived in `Agentic/Plans/Done/authoritative-replay-rollback-plan.md`; legacy replay paths were intentionally retained. |

## Known Bugs

| Bug | Area | Status |
|-----|------|--------|
| Water renders through back faces of spheres when intersecting the water surface. | Rendering / Water | Mitigated but not fully solved; continue through `Agentic/Plans/water-rendering-cleanup-plan.md`. |

Additional bug notes live in `Agentic/Bugs.md`.

## Validation Map

Use `AGENTS.md` as the source of truth. These are targeted pre-commit/PR gates,
not routine iteration steps.

| Change | Validation |
|--------|------------|
| Documentation-only | No validation required |
| Small non-render code refactor | `tools\validate_fast.bat` |
| Renderer backend, shaders, screenshots, visual baselines | `tools\validate_dx12_renderer.bat` |
| DX12 renderer gate or validation tooling | `tools\validate_fast.bat`, then `tools\validate_dx12_renderer.bat` |
| Physics, collision, solver, determinism | `tools\validate_physics.bat` |
| Broad physics baseline, bullet sweep, or SkullScope diagnostics | `tools\validate_physics_deep.bat` |
| Performance-sensitive hot path | `tools\validate_perf.bat` |
| Broad or uncertain scope | `tools\validate_full.bat` |

## Key Paths

| Purpose | Path |
|---------|------|
| Source | `SkullbonezSource/` |
| Scenes | `SkullbonezData/scenes/` |
| Shaders | `SkullbonezData/shaders/` |
| Baselines | `TestOutput/baselines/` |
| Validation scripts | `tools/` |
| Runtime reference | `Agentic/Reference/runtime-reference.md` |
| Physics overview | `Agentic/Reference/physics-overview.md` |
