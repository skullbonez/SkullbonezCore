# SkullbonezCore Session State

Keep this file short. Put detailed history in task-specific plans, reports, or
audits when it is still useful.

## Current State

| Field | Value |
|-------|-------|
| Branch | `nightrunner-26th-July` in worktree `C:\SkullbonezCore` |
| Last committed milestone | Runtime run composition-root shrink scene world override ownership slice is validated for commit: `ApplyUIWorldOverride` is removed from `Run`; `RuntimeTuning` owns live world/fluid mutation with explicit `WorldEnvironment&` and `ReplayRuntime&`; and the `Run.h` private-method ratchet is 144. |
| Active objective | Continue `Agentic/Plans/run-composition-root-shrink-plan.md` with the repo-local orchestrator skill; launcher/editor slices, scene reset/load-begin/control/context-builder/generated-camera-wrapper/terrain-world/default-persistence/create-scene/world-override slices, diagnostics slices including memory-dump wrapper removal and UI stress RNG helper local ownership, replay scrubber/ghost host slices, replay prediction job-state ownership, replay prediction capture helper file-local ownership, replay prediction lifecycle helper file-local ownership, replay path-state wrapper removal, replay cause-tree body lookup ownership, replay cause-tree dead focus wrapper removal, replay cause-tree camera activation local ownership, replay inspection camera update wrapper removal, replay scrubber reset ownership, replay loaded-presentation scrubber arming ownership, replay camera focus clear ownership, scene browser index helper ownership, scene browser refresh helper ownership, scene runtime style helper ownership, replay event frame cursor wrapper removal, replay event record wrapper removal, replay generated-scene config wrapper removal, replay physics capture wrapper removal, replay world override event ownership, replay launcher config event ownership, replay launcher fire event ownership, replay editor place event ownership, replay editor transform event ownership, replay render-state helper local ownership, replay launcher visual sample RuntimeTools ownership, replay sample comparison helper local ownership, replay presentation picker helper local ownership, replay scrubber save helper local ownership, replay restore event helper local ownership, replay velocity target ownership, replay velocity hit helper file-local ownership, replay velocity edit toggle ownership, replay velocity apply helper file-local ownership, replay velocity drag helper local ownership, editable scene snapshot helper local ownership, scene tornado defaults helper local ownership, and tornado sync helper local ownership are complete. |
| Pending work | Continue run-shrink work with remaining scene load teardown/generated/authored setup phases, generated-model count override ownership, solver-object count override ownership, remaining replay tool/helper ownership, render-host splitting, and shared cine/path helper cleanup. Per current user instruction, defer rubber-duck review until the end and move plans to `Done/` only after that final review is satisfied. |
| Blockers | None known. |
| Orchestrator policy | The old `Agentic/Orchestrator` JSON policy/queue/state-machine path was removed; use the `orchestrator` skill instead. |
| Worktree expectation | Do not assume cleanliness; run `git status --short --branch` before editing or committing. |
| Validation | Scene world override ownership validation: direct `check_runtime_boundaries.py` (`TestOutput\validation\agent_logs\world_override_runtime_boundaries.log`) passed with 0 errors in 3.5s; targeted Profile build (`TestOutput\validation\agent_logs\world_override_profile_build.log`) passed in 39.8s with 0 warnings and 0 errors; `tools\validate_fast.bat` (`TestOutput\validation\agent_logs\world_override_validate_fast.log`) passed in 49.5s; and `tools\validate_full.bat` (`TestOutput\validation\agent_logs\world_override_validate_full.log`) passed in 28.6s. Evidence includes formatting clean, project filters clean, runtime-boundary 0 errors, Profile/Debug 0-warning builds, DX12 validation errors 0 with screenshots matching baselines, and byte-exact `physics_regression_solver.csv`. |

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
- Runtime run composition shrink editor placement slice moved terrain
  hit/preview/commit helpers to `EditorTools`; replay restore now refreshes the
  body store and clears pending impulses after applying serialized body state,
  and replay v2 tooling understands snapshot version 2 tornado-system data.
- Runtime run composition shrink scene reset preserve/restore slice moves
  `SceneRuntimeResetSnapshot`, reset capture/restore, and scene UI override
  clearing out of `Run`/`RunInternal` into `Runtime/Scene/SceneRuntimeReset`.
  `Run::LoadScene` still sequences the load, but now passes an explicit
  `SceneRuntimeResetContext` into scene-owned helpers. Guardrails lower the
  `Run.h` private-method ratchet to 227, block the old `Run.h` helper
  declarations, block `Run::...SceneRuntimeReset...` source definitions, block
  the snapshot returning to `RunInternal.h`, and teach project-filter validation
  that `SceneRuntimeReset.*` belongs under `Runtime/Scene`. Rubber-duck reviewer
  Heisenberg found no behavior blocker after the split; the initial coordinator
  coupling/API-shape concern was addressed by moving the helpers into a
  dedicated reset module with non-const mutation context.
- Scene reset preserve/restore validation: initial targeted Profile builds
  passed in 43.97s and 43.59s
  (`TestOutput\validation\agent_logs\scene_reset_context_profile_build.log`,
  `TestOutput\validation\agent_logs\scene_reset_module_profile_build.log`).
  Final gates passed: `tools\validate_fast.bat` 56.66s
  (`TestOutput\validation\agent_logs\scene_reset_module_validate_fast.log`),
  direct `check_runtime_boundaries.py` 0.62s
  (`TestOutput\validation\agent_logs\scene_reset_module_check_runtime_boundaries.log`),
  direct `validate_project_filters.py` 0.77s
  (`TestOutput\validation\agent_logs\scene_reset_module_validate_project_filters.log`),
  and `tools\validate_full.bat` 25.00s
  (`TestOutput\validation\agent_logs\scene_reset_module_validate_full.log`).
  Full gate passed project filters, runtime boundaries, Profile/Debug builds,
  DX12 renderer validation with 0 InfoQueue errors and matching screenshots,
  and byte-exact `physics_regression_solver.csv`.
- Runtime run composition shrink scene load-begin slice moves the first phase of
  `Run::LoadScene` into `Runtime/Scene/SceneRuntimeLoad`: queue index
  validation, interactive-run policy, automation-exit suppression, preserve vs.
  clear reset-state selection, GPU flush-before-teardown, `SceneController`
  begin-load bookkeeping, active scene-path handoff, and cine-browser selection.
  `Run` no longer declares or defines `HasSceneQueueEntry`,
  `HasCurrentSceneQueueEntry`, or `CurrentSceneQueuePath`; split Run files now
  read `m_sceneController` directly. Guardrails lower the `Run.h`
  private-method ratchet to 224, block those old wrapper declarations and
  definitions, and teach project-filter validation that `SceneRuntimeLoad.*`
  belongs under `Runtime/Scene`. Rubber-duck reviewer Curie found no blocking
  behavior defect; non-blocking residual risks are duplicated cine/path
  normalization and the intentionally narrow wrapper-name guardrail.
- Scene load-begin validation: targeted Profile build after formatting passed
  in 42.98s
  (`TestOutput\validation\agent_logs\scene_load_begin_profile_build_post_format.log`).
  Final gates passed: `tools\validate_fast.bat` 89.71s
  (`TestOutput\validation\agent_logs\scene_load_begin_validate_fast.log`),
  direct `check_runtime_boundaries.py` 0.66s
  (`TestOutput\validation\agent_logs\scene_load_begin_runtime_boundaries.log`),
  direct `validate_project_filters.py` 0.75s
  (`TestOutput\validation\agent_logs\scene_load_begin_project_filters.log`),
  and `tools\validate_full.bat` 25.84s
  (`TestOutput\validation\agent_logs\scene_load_begin_validate_full.log`).
  Full gate passed project filters, runtime boundaries, Profile/Debug builds,
  DX12 renderer validation with 0 InfoQueue errors and matching screenshots,
  and byte-exact `physics_regression_solver.csv`.
- Runtime run composition shrink diagnostics perf-memory wrapper slice removes
  `Run::LogPerfMemory`. Periodic and scene-start memory checkpoints now call
  `DiagnosticsRuntime::LogPerfMemory` directly, and scene-load end/checkpoint,
  pending flush, and close behavior is owned by
  `RuntimeDiagnostics::ClosePerfLogWithMemoryCheckpoint` through the
  diagnostics controller/runtime facade. `RunScene` still owns perf-log open and
  raw `PerfLog()` setup; that remains scene-load lifecycle debt for a later
  slice. Guardrails lower the `Run.h` private-method ratchet to 223 and block
  `Run::LogPerfMemory` declarations/definitions from returning. Rubber-duck
  reviewer Averroes found no blocking source defect or ordering drift; the only
  commit blocker was missing final validation evidence, now resolved.
- Diagnostics perf-memory wrapper validation: targeted Profile build passed in
  43.75s
  (`TestOutput\validation\agent_logs\perf_memory_wrapper_profile_build.log`).
  Final gates passed: `tools\validate_fast.bat` 50.74s
  (`TestOutput\validation\agent_logs\perf_memory_wrapper_validate_fast.log`),
  direct `check_runtime_boundaries.py` 0.70s
  (`TestOutput\validation\agent_logs\perf_memory_wrapper_runtime_boundaries.log`),
  and `tools\validate_full.bat` 25.43s
  (`TestOutput\validation\agent_logs\perf_memory_wrapper_validate_full.log`).
  Full gate passed project filters, runtime boundaries, Profile/Debug builds,
  DX12 renderer validation with 0 InfoQueue errors and matching screenshots,
  and byte-exact `physics_regression_solver.csv`.
- Runtime run composition shrink scene-control wrapper slice removes
  `LoadSceneFromBrowserIndex`, `LoadDemoSceneFromUI`,
  `ApplyAdjacentCinematicMode`, `LoadAdjacentSceneFromBrowser`,
  `ResetCurrentScene`, and `AdvanceScene` from `Run`. Scene-browser,
  demo-scene, adjacent-scene, reset, and scene-advance call sites now call
  `SceneRuntimeCoordinator` directly with the same paths, preserve flags,
  perf-test state, perf pass reference, and interactive-run preserve flag. The
  `Run.h` private-method ratchet is now 217 and boundary guardrails reject
  those wrapper declarations/definitions from returning. Rubber-duck reviewer
  Chandrasekhar found no blocking behavior defect; the remaining non-blocking
  risk is that guardrails are exact-name checks, with renamed wrappers covered
  only by the broader method-count ratchet and review.
- Scene-control wrapper validation: targeted Profile build passed in 41.73s
  (`TestOutput\validation\agent_logs\scene_control_wrapper_profile_build.log`).
  Final gates passed: `tools\validate_fast.bat` in about 46.2s
  (`TestOutput\validation\agent_logs\scene_control_wrapper_validate_fast.log`),
  direct `check_runtime_boundaries.py` in 0.79s
  (`TestOutput\validation\agent_logs\scene_control_wrapper_runtime_boundaries.log`),
  and `tools\validate_full.bat` in 25.08s
  (`TestOutput\validation\agent_logs\scene_control_wrapper_validate_full.log`).
  Full gate passed project filters, runtime boundaries, Profile/Debug builds,
  DX12 renderer validation with 0 InfoQueue errors and matching screenshots,
  and byte-exact `physics_regression_solver.csv`.
- Runtime run composition shrink scene coordinator intent slice removes
  `SceneRuntimeCoordinatorCallbacks` and
  `Run::BuildSceneRuntimeCoordinatorCallbacks`. `SceneRuntimeCoordinator` now
  stores only `SceneController&` and returns `SceneRuntimeControlAction` values
  for load, clear-automation, and cinematic-style intents. Existing `Run`
  call sites execute those actions locally, preserving the old
  `EnterInteractiveSceneRun`, `LoadScene`, clear-automation,
  `ApplyCinematicModeFromBrowserIndex`, reset, and advance/no-next behavior
  without adding a new `Run` helper. The `Run.h` private-method ratchet is now
  216, and boundary guardrails reject the callback builder/state from
  returning. Rubber-duck reviewer Popper found no blocking behavior defect;
  remaining non-blocking risks are exact-name callback guardrails and duplicated
  local action executors.
- Scene coordinator intent validation: targeted Profile build passed in 42.43s
  (`TestOutput\validation\agent_logs\scene_coordinator_intent_profile_build.log`).
  Final gates passed: `tools\validate_fast.bat` in about 45.3s
  (`TestOutput\validation\agent_logs\scene_coordinator_intent_validate_fast.log`),
  direct `check_runtime_boundaries.py` in 0.82s
  (`TestOutput\validation\agent_logs\scene_coordinator_intent_runtime_boundaries.log`),
  and `tools\validate_full.bat` in 25.31s
  (`TestOutput\validation\agent_logs\scene_coordinator_intent_validate_full.log`).
  Full gate passed project filters, runtime boundaries, Profile/Debug builds,
  DX12 renderer validation with 0 InfoQueue errors and matching screenshots,
  and byte-exact `physics_regression_solver.csv`.
- Runtime run composition shrink diagnostics perf-log lifecycle slice removes
  `Run::TickPerfLog`. `RunFrame` now calls
  `DiagnosticsRuntime::TickPerfLog` directly, periodic memory checkpoints moved
  into `RuntimeDiagnostics::TickPerfLog`, and scene-load perf-log
  reset/config/open behavior moved behind diagnostics runtime/controller APIs.
  `RunFrame` and `RunInput` now read perf-test state through
  `DiagnosticsRuntime::PerfTestActive()`. The `Run.h` private-method ratchet is
  now 215, and boundary guardrails reject `Run::TickPerfLog` and direct
  `RunScene` perf-log lifecycle field/file access from returning. Rubber-duck
  reviewer Einstein found no blocking defect; the only non-blocking finding was
  a broad `fopen_s` guardrail, which was tightened before final validation.
- Diagnostics perf-log lifecycle validation: targeted Profile build passed in
  43.85s
  (`TestOutput\validation\agent_logs\perf_log_lifecycle_profile_build.log`).
  Final gates passed: `tools\validate_fast.bat` in about 47.7s
  (`TestOutput\validation\agent_logs\perf_log_lifecycle_validate_fast.log`),
  direct `check_runtime_boundaries.py` in 0.89s
  (`TestOutput\validation\agent_logs\perf_log_lifecycle_runtime_boundaries.log`),
  and `tools\validate_full.bat` in 24.84s
  (`TestOutput\validation\agent_logs\perf_log_lifecycle_validate_full.log`).
  Full gate passed project filters, runtime boundaries, Profile/Debug builds,
  DX12 renderer validation with 0 InfoQueue errors and matching screenshots,
  and byte-exact `physics_regression_solver.csv`.
- Runtime run composition shrink editor gizmo slice moved editor transform
  gizmo math and selected-object transform mutation to `EditorTools`; no
  dedicated automated live translate/rotate/scale drag smoke exists yet, though
  interaction-click and replay artifact gates cover selection/visibility and
  encoded editor-transform replay samples.
- Runtime run composition shrink editor UI/mode command slice moves unfocused
  editor reset, manipulation clear, placement-mode state selection, editor
  keyboard shortcut capture, editor mode state entry/exit, static-placement
  toggles, terrain-align toggles, and object-type selection into `EditorTools`.
  `RunInput` still applies interaction transitions, world-owner selection,
  camera labels, fly-camera enter/exit/reset, cursor ownership, mouse release,
  and runtime input action reporting. The `Run.h` private-method ratchet is now
  233 and blocks the old editor UI/mode helper names from returning.
- Editor UI/mode command validation: targeted Profile build 7.85s
  (`TestOutput\validation\run_composition_editor_ui_profile_build.log`), fast
  gate 87.20s (`TestOutput\validation\run_composition_editor_ui_validate_fast.log`),
  runtime-boundary gate 0.51s
  (`TestOutput\validation\run_composition_editor_ui_validate_runtime_boundaries.log`),
  interaction-click gate 6.54s
  (`TestOutput\validation\run_composition_editor_ui_validate_interaction_clicks.log`),
  and full gate 24.08s
  (`TestOutput\validation\run_composition_editor_ui_validate_full.log`). Full
  gate passed project filters, runtime boundaries, Profile/Debug builds with 0
  warnings/errors, DX12 validation errors 0 with screenshots matching baselines,
  and byte-exact `physics_regression_solver.csv`. Rubber-duck reviewer Confucius
  found no blocking behavior parity defect.
- Runtime run composition shrink editor overlay/preview slice moves editor
  interaction preview refresh and deterministic tool-overlay trace construction
  into `EditorOverlayTools` helpers implemented beside the editor math. `Run`
  still owns mouse-ray building, invalid-selection command dispatch,
  attached-camera target resolution, replay overlay append, tracer render, and
  launcher laser render. The `Run.h` private-method ratchet is now 231 and
  blocks editor overlay/interaction-preview helper names from returning.
- Editor overlay/preview validation: targeted Profile build 6.69s
  (`TestOutput\validation\profile_build_editor_overlay.log`), runtime-boundary
  gate 0.51s
  (`TestOutput\validation\validate_runtime_boundaries_editor_overlay.log`),
  fast gate 13.17s
  (`TestOutput\validation\validate_fast_editor_overlay_final.log`),
  interaction-click gate 7.10s
  (`TestOutput\validation\validate_interaction_clicks_editor_overlay.log`),
  and DX12 renderer gate 16.64s
  (`TestOutput\validation\validate_dx12_renderer_editor_overlay_final.log`).
  Gates passed with 0-warning/error builds, runtime-boundary 0 errors, project
  filters clean, live inspect-gizmo and replay-prediction click reports passing,
  DX12 validation errors 0, and screenshots matching committed baselines.
  Rubber-duck reviewer Locke found no extraction behavior blocker; the first
  review blocked on dirty DX12 build-log evidence and an under-matching guardrail
  regex, both fixed before final validation.
- Runtime run composition shrink replay render-query owner slice moves loaded
  replay presentation sampling, current scrub sample/frame queries, and replay
  focus-mask building into `ReplayRuntime`. `RuntimeRenderHost` now borrows a
  single replay owner instead of six replay sub-state bindings and no longer
  callback-bounces replay sample/focus queries into `Run`. The boundary checker
  blocks the old helper names, old host replay bindings/callbacks, and now
  counts pointer/ref-return private declarations; the widened `Run.h` ratchet is
  measured at 235.
- Replay render-query validation: final Profile build 1.21s
  (`TestOutput\validation\replay_render_query_profile_build.log`), fast gate
  52.58s (`TestOutput\validation\replay_render_query_validate_fast.log`),
  runtime-boundary gate 0.47s
  (`TestOutput\validation\replay_render_query_validate_runtime_boundaries.log`),
  replay artifact gate 22.97s
  (`TestOutput\validation\replay_render_query_validate_replay_v2_artifact.log`),
  interaction-click gate 6.93s
  (`TestOutput\validation\replay_render_query_validate_interaction_clicks.log`),
  DX12 renderer gate 16.46s
  (`TestOutput\validation\replay_render_query_validate_dx12_renderer.log`), and
  full gate 23.01s
  (`TestOutput\validation\replay_render_query_validate_full.log`). Gates passed
  with formatting clean, runtime-boundary 0 errors, Profile/Debug 0-warning
  builds, replay save/restore/query checks, interaction reports passing, DX12
  validation errors 0 with screenshots matching baselines, and byte-exact
  `physics_regression_solver.csv`. Rubber-duck reviewer Copernicus first
  blocked on the pointer-return ratchet gap, then confirmed the fix and found no
  new blocker.
- Runtime run composition shrink replay scrubber timeline owner slice moves
  scrubber timeline math and track-position mutation out of `RunInternal.h`
  into `ReplayRuntime`. `Run` still dispatches scrubber input and overlay
  drawing, but those paths now call replay-owner APIs. `Run::ShouldRenderReplayScrubber`
  and `RuntimeRenderHostCallbacks::shouldRenderReplayScrubber` are removed;
  `RuntimeRenderHost` asks `ReplayRuntime::ShouldRenderScrubber(...)`
  directly. The boundary checker now blocks the removed `Run.h` wrapper, the
  removed host callback field, and reintroduced `RunInternal.h` scrubber helper
  definitions including `static inline` variants; the `Run.h` private-method
  ratchet is now 234.
- Replay scrubber timeline owner validation: final Profile build 40.89s
  (`TestOutput\validation\replay_scrubber_owner_profile_build.log`), fast gate
  51.03s (`TestOutput\validation\replay_scrubber_owner_validate_fast.log`),
  runtime-boundary gate 0.52s
  (`TestOutput\validation\replay_scrubber_owner_validate_runtime_boundaries.log`),
  replay artifact gate 22.75s
  (`TestOutput\validation\replay_scrubber_owner_validate_replay_v2_artifact.log`),
  interaction-click gate 6.45s
  (`TestOutput\validation\replay_scrubber_owner_validate_interaction_clicks.log`),
  DX12 renderer gate 16.74s
  (`TestOutput\validation\replay_scrubber_owner_validate_dx12_renderer.log`),
  and full gate 23.07s
  (`TestOutput\validation\replay_scrubber_owner_validate_full.log`). Gates
  passed with formatting clean, runtime-boundary 0 errors, Profile/Debug
  0-warning builds, replay save/restore/query checks, interaction reports
  passing, DX12 validation errors 0 with screenshots matching baselines, and
  byte-exact `physics_regression_solver.csv`. Rubber-duck reviewer Arendt found
  no blocking defect; the non-blocking guardrail regex concern was tightened
  before final validation.
- Runtime run composition shrink replay prediction ghost host slice removes
  `Run::RenderReplayPredictionGhosts` and the render-host
  `renderReplayPredictionGhosts` callback bridge. `RuntimeRenderHost` now draws
  replay prediction ghosts directly through `ReplayRuntime` draw requests,
  `GameModelCollection` model data, and its existing texture-selection service.
  The draw body lives out-of-line in `Runtime/Render/RuntimeRenderHost.cpp` so
  the central host header stays lean. The boundary checker blocks the removed
  `Run.h` helper, the removed callback typedef/field, and lowers the `Run.h`
  private-method ratchet to 233.
- Replay prediction ghost host validation: final Profile build 40.28s
  (`TestOutput\validation\replay_prediction_ghost_host_profile_build.log`),
  fast gate 48.25s
  (`TestOutput\validation\replay_prediction_ghost_host_validate_fast.log`),
  runtime-boundary gate 0.51s
  (`TestOutput\validation\replay_prediction_ghost_host_validate_runtime_boundaries.log`),
  replay artifact gate 22.63s
  (`TestOutput\validation\replay_prediction_ghost_host_validate_replay_v2_artifact.log`),
  DX12 renderer gate 17.39s
  (`TestOutput\validation\replay_prediction_ghost_host_validate_dx12_renderer.log`),
  and full gate 23.09s
  (`TestOutput\validation\replay_prediction_ghost_host_validate_full.log`).
  Gates passed with formatting clean, project filters clean, runtime-boundary 0
  errors, Profile/Debug 0-warning builds, replay save/load/restore/query
  checks, DX12 validation errors 0 with screenshots matching baselines, and
  byte-exact `physics_regression_solver.csv`. Rubber-duck reviewer Aquinas
  found no blocking defect; the non-blocking header-churn concern was fixed by
  moving the implementation out of the header before final validation.
- Runtime run composition shrink replay cause-tree row owner slice moves
  cause-tree row construction from `Run::BuildReplayCauseTreeRows` into
  `ReplayRuntime::BuildCauseTreeRows`. Replay cause-tree input and overlay
  callers now ask the replay owner to build rows while continuing to own UI
  window placement/scroll clamping. The boundary checker blocks both the old
  `BuildReplayCauseTreeRows` name and renamed `BuildCauseTreeRows` wrappers on
  `Run`, including synthetic header and source self-tests; the `Run.h`
  private-method ratchet is now 232.
- Replay cause-tree row owner validation: targeted Profile build 43.02s
  (`TestOutput\validation\agent_logs\cause_tree_replay_runtime_profile_build.log`),
  fast gate 49.66s
  (`TestOutput\validation\agent_logs\cause_tree_replay_runtime_validate_fast.log`),
  runtime-boundary gate 0.55s
  (`TestOutput\validation\agent_logs\cause_tree_replay_runtime_validate_runtime_boundaries.log`),
  replay artifact gate 22.99s
  (`TestOutput\validation\agent_logs\cause_tree_replay_runtime_validate_replay_v2_artifact.log`),
  interaction-click gate 6.80s
  (`TestOutput\validation\agent_logs\cause_tree_replay_runtime_validate_interaction_clicks.log`),
  DX12 renderer gate 16.69s
  (`TestOutput\validation\agent_logs\cause_tree_replay_runtime_validate_dx12_renderer.log`),
  and full gate 22.91s
  (`TestOutput\validation\agent_logs\cause_tree_replay_runtime_validate_full.log`).
  Gates passed with formatting clean, runtime-boundary 0 errors, Profile/Debug
  0-warning builds, replay save/load/restore/query checks, interaction reports
  passing, DX12 validation errors 0 with screenshots matching baselines, and
  byte-exact `physics_regression_solver.csv`. Rubber-duck reviewer Peirce found
  no blocking defect; the non-blocking guardrail naming concern was tightened
  before final validation.
- Runtime run composition shrink replay scrubber overlay host slice removes
  `Run::RenderReplayScrubberOverlay`, `Run::RenderReplayCauseTreeOverlay`, and
  the render-host `renderReplayScrubberOverlay` callback bridge.
  `RuntimeRenderHost` now renders the replay scrubber and cause-tree overlays
  directly through its replay/runtime state bindings. The boundary checker
  blocks the removed `Run.h` declarations, removed `Run::RenderReplay...`
  source definitions, and removed host callback field; the `Run.h`
  private-method ratchet is now 230.
- Replay scrubber overlay host validation: targeted Profile build 40.73s
  (`TestOutput\validation\agent_logs\replay_scrubber_host_profile_build.log`),
  fast gate 48.28s
  (`TestOutput\validation\agent_logs\replay_scrubber_host_validate_fast.log`),
  runtime-boundary gate 0.60s
  (`TestOutput\validation\agent_logs\replay_scrubber_host_validate_runtime_boundaries.log`),
  replay artifact gate 22.88s
  (`TestOutput\validation\agent_logs\replay_scrubber_host_validate_replay_v2_artifact.log`),
  interaction-click gate 6.47s
  (`TestOutput\validation\agent_logs\replay_scrubber_host_validate_interaction_clicks.log`),
  DX12 renderer gate 16.63s
  (`TestOutput\validation\agent_logs\replay_scrubber_host_validate_dx12_renderer.log`),
  and full gate 23.06s
  (`TestOutput\validation\agent_logs\replay_scrubber_host_validate_full.log`).
  Gates passed with formatting clean, runtime-boundary 0 errors, Profile/Debug
  0-warning builds, replay save/load/restore/query checks, interaction reports
  passing, DX12 validation errors 0 with screenshots matching baselines, and
  byte-exact `physics_regression_solver.csv`. Rubber-duck reviewer Linnaeus
  found no blocking code defect; the remaining non-blocking architecture risk is
  that replay overlay drawing still lives in `RunUiTextPass.cpp` and uses
  `RunInternal.h` helpers, making that the next shrink target.
- Runtime run composition shrink replay overlay layout/renderer slice moves
  replay scrubber and cause-tree rectangle helpers from `RunInternal.h` to
  `ReplayOverlayLayout`, and moves the scrubber/cause-tree drawing bodies from
  `RunUiTextPass.cpp` to `ReplayOverlayRenderer`. `RuntimeRenderHost` now builds
  a replay overlay render context and forwards to replay-owned drawing functions;
  `RunUiTextPass` only invokes the host pass hook. Runtime boundary guardrails
  now block reintroduced replay overlay layout helpers in `RunInternal.h` and
  replay overlay render definitions in `RunUiTextPass.cpp`; project-filter
  validation recognizes the new replay overlay source/header prefixes.
- Replay overlay layout/renderer validation: fast gate 61.74s
  (`TestOutput\validation\agent_logs\replay_overlay_renderer_validate_fast.log`),
  direct runtime-boundary check 0.55s
  (`TestOutput\validation\agent_logs\replay_overlay_renderer_check_runtime_boundaries_py.log`),
  runtime-boundary gate 0.58s
  (`TestOutput\validation\agent_logs\replay_overlay_renderer_validate_runtime_boundaries.log`),
  direct project-filter check 0.77s
  (`TestOutput\validation\agent_logs\replay_overlay_renderer_validate_project_filters_py.log`),
  replay artifact gate 23.31s
  (`TestOutput\validation\agent_logs\replay_overlay_renderer_validate_replay_v2_artifact.log`),
  interaction-click gate 6.54s
  (`TestOutput\validation\agent_logs\replay_overlay_renderer_validate_interaction_clicks.log`),
  DX12 renderer gate about 17.14s
  (`TestOutput\validation\agent_logs\replay_overlay_renderer_validate_dx12_renderer.log`),
  and full gate 23.50s
  (`TestOutput\validation\agent_logs\replay_overlay_renderer_validate_full.log`).
  The full sequence took 134.13s and passed with formatting clean, project
  filters clean, runtime-boundary 0 errors, Profile/Debug 0-warning builds,
  replay save/load/restore/query checks, interaction reports passing, DX12
  validation errors 0 with screenshots matching baselines, and byte-exact
  `physics_regression_solver.csv`. Rubber-duck reviewer Aristotle found no code
  blocker; validation then exposed the missing project-filter prefix, which was
  fixed before the final gate. Final rubber-duck reviewer Galileo found that the
  `RunUiTextPass.cpp` guardrail only blocked the old host-method overlay
  definitions, not the new replay free-function renderers; the regex and
  synthetic self-tests were tightened, then `validate_fast`, direct
  `check_runtime_boundaries.py`, and `validate_runtime_boundaries` reran in
  14.91s and passed.
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
