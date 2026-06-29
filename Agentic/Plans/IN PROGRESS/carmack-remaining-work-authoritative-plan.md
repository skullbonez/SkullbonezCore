# Carmack Remaining Work Authoritative TODO

Status: In progress

Short description: finish the remaining Carmack architecture work after the
parallel rubber-duck review by closing the stale evidence gaps, resolving the
perf gate, hardening the physics boundary, and turning render-graph resource
ownership from diagnostic scaffolding into live production behavior.

This file supersedes the old Carmack open-todos handoff and the four archived
Carmack source plans as the active task queue. Use the older plans only as
historical context. Check items here only after the current branch contains the
work and the evidence path is recorded beside the item.

Historical references:

- `Agentic/Plans/Done/carmack-open-todos-agent-handoff-plan.md`
- `Agentic/Plans/Done/carmack-global-service-lifetime-plan.md`
- `Agentic/Plans/Done/carmack-physics-standalone-boundary-plan.md`
- `Agentic/Plans/Done/carmack-render-backend-capability-plan.md`
- `Agentic/Plans/Done/carmack-render-graph-resource-ownership-plan.md`

Known useful evidence to preserve:

- `TestOutput/validation/agent_logs/carmack_validate_full_final.log` passed the
  broad gate with DX12 validation errors 0 and byte-exact physics.
- `TestOutput/validation/agent_logs/carmack_validate_physics_after_parallel_threshold.log`
  passed after the parallel-threshold change.
- `Agentic/Reports/2026-06-29/carmack-handoff/perf-validation-note.md`
  records that absolute perf budgets pass, but the relative perf gate still
  fails.

## Agent Instructions

- Follow the repository Agent Startup Contract before editing.
- Run `git status --short --branch` and treat pre-existing dirty files as
  user-owned.
- Work one phase at a time. Do not attempt to close every Carmack area in one
  diff unless explicitly asked.
- For source-bearing edits, apply `Agentic/Reference/comment-style-guide.md`
  and run the comment-style audit over touched source before reporting done.
- For documentation-only updates to this plan, no repository validation is
  required.
- For code changes, defer formal repository validation until the PR/commit gate
  unless a focused build or probe answers a specific implementation question.

## Phase 0 - Evidence Reconciliation

- [ ] Regenerate the global-service classification CSV and summary from the
  final current source tree so they no longer report stale hits such as old
  `TextureCollection.cpp` direct `Gfx()` access.
  Evidence:
- [ ] Record which remaining global-service hits are allowed bootstrap,
  shutdown, OS callback bridge, diagnostics, or test/tool access, and which are
  still normal-path debt.
  Evidence:
- [ ] Treat the old Carmack source plans and the old open-todos handoff as
  archived history. Do not chase their unchecked boxes unless this file points
  back to a specific item.
  Evidence:
- [ ] Add final evidence paths to this file as work completes so the next agent
  can trust this file without reopening every historical plan.
  Evidence:

## Phase 1 - Perf Gate Closure

- [ ] Investigate the remaining `tools\validate_perf.bat` failure recorded in
  `TestOutput/validation/agent_logs/carmack_validate_perf_after_parallel_threshold.log`.
  The current known failures are the `PHYSICS_BENCH` relative `Frame`,
  `Frame/Render`, `Frame/VsyncWait`, and memory start/restart/end deltas.
  Evidence:
- [ ] Decide whether the perf deltas are a real regression, measurement noise,
  or an intentional baseline shift. Fix the code for real regressions; update
  baselines only when the slower numbers are intentional and reviewed.
  Evidence:
- [ ] Preserve the passing absolute-budget evidence, but do not use it as final
  perf closure while the relative gate still exits nonzero.
  Evidence:
- [ ] Produce either a clean `tools\validate_perf.bat` log or an explicit
  reviewed waiver/baseline-update note for the remaining relative failures.
  Evidence:

## Phase 2 - Global Service Lifetime

- [ ] Audit the regenerated classification and identify any remaining
  unauthorized normal runtime path, render pass, asset lookup, or diagnostics
  global access.
  Evidence:
- [ ] Route remaining unauthorized render-pass backend access through explicit
  render contexts or capability interfaces.
  Evidence:
- [ ] Route remaining unauthorized asset, texture, camera, window, skybox,
  worker, config, profiler, or graphics-service access through borrowed
  runtime-owned services or explicit context parameters.
  Evidence:
- [ ] Keep service contexts borrowed and lifetime-annotated. Do not introduce a
  new global service locator under a friendlier name.
  Evidence:
- [ ] Ratchet `tools/check_runtime_boundaries.py` allowlists or guardrails so
  removed normal-path global access cannot silently return.
  Evidence:

## Phase 3 - Physics Standalone Boundary

- [ ] Decide and document the final standalone boundary contract: either make
  `PhysicsBodyStore`/`PhysicsScene` the step authority, or explicitly narrow
  the accepted compatibility bridge through `PhysicsModelAccess`.
  Evidence:
- [ ] If strict standalone authority is still required, stop loading from and
  writing back to model-backed `PhysicsModelAccess` every step; keep model sync
  at explicit boundary points.
  Evidence:
- [ ] If the compatibility bridge is accepted, document the invariant clearly
  and adjust guardrails/tests so future work cannot mistake it for full
  standalone ownership.
  Evidence:
- [ ] Add or extend smoke coverage for standalone physics API lifecycle,
  runtime handle mirrors, store handles, render mirrors, and joint handles.
  Evidence:
- [ ] Add a sharper invariant or test for collider-store freshness when
  same-count authoring/edit paths change shape, restitution, material, or other
  collider-affecting data.
  Evidence:

## Phase 4 - Render Backend Capability

- [ ] Reconfirm `IRenderBackend` remains a methodless temporary facade and does
  not regain direct render operations.
  Evidence:
- [ ] Reconfirm runtime render pass bodies stay on narrow contexts and do not
  regain direct `Gfx()` or `GfxRayTracing()` access.
  Evidence:
- [ ] Reconfirm the runtime-boundary checker blocks raytracing inheritance,
  direct facade methods, and pass-body global backend access.
  Evidence:
- [ ] Resolve any render-backend issues found by the regenerated
  global-service classification. The rubber-duck review found the mechanics
  mostly satisfied, so this phase should remain narrow unless new evidence says
  otherwise.
  Evidence:

## Phase 5 - Render Graph Resource Ownership

- [ ] Promote graph-created transient resources from diagnostic skeleton output
  into at least one live production frame path that writes to and/or samples a
  graph-owned transient during rendering.
  Evidence:
- [ ] Ensure the production graph-owned transient is exercised by DX12
  screenshot validation and does not appear only as `DeclarationOnly` skeleton
  metadata.
  Evidence:
- [ ] Fix or prove backend transient pool reuse for compatible non-overlapping
  transient resources allocated in the same compile. The reviewed risk is that
  `usedThisCompile` prevents legitimate same-compile aliasing.
  Evidence:
- [ ] Add focused materializer/compiler coverage or diagnostic evidence proving
  compatible non-overlapping graph transients share a pool slot without
  unbounded growth.
  Evidence:
- [ ] Move the relevant resource-state/barrier ownership for graph-owned
  resources into the graph planner/backend materializer path, with descriptor
  lifetime and shutdown accounting visible in diagnostics.
  Evidence:
- [ ] Keep backend-owned legacy resources working while the graph-owned path is
  introduced; do not remove legacy lifetime hooks until validation proves the
  replacement path.
  Evidence:

## Phase 6 - Comment Audit And Final Validation

- [ ] Run the comment-style audit for every touched source-bearing file in the
  final implementation slice and record the result here.
  Evidence:
- [ ] Run `git diff --check` before final handoff.
  Evidence:
- [ ] Run the smallest required validation for each final code area touched:
  `tools\validate_physics.bat` for physics boundary changes,
  `tools\validate_dx12_renderer.bat` for render-graph/DX12 changes, and
  `tools\validate_perf.bat` for hot-path or allocation-sensitive changes.
  Evidence:
- [ ] Run the final broad gate once the branch is ready for PR/commit handoff:
  `tools\validate_full.bat`.
  Evidence:
- [ ] Rubber-duck this authoritative plan against the final branch state before
  declaring the Carmack work complete.
  Evidence:

## Definition Of Done

- [ ] This plan has no unchecked implementation, evidence, validation, or audit
  items.
- [ ] The global-service classification is regenerated from the final source
  and reconciled against this plan.
- [ ] `tools\validate_perf.bat` passes, or the remaining perf deltas have an
  explicit reviewed waiver/baseline-update note.
- [ ] `tools\validate_full.bat` passes on the final branch state.
- [ ] Any touched source-bearing files have comment-audit evidence.
- [ ] The final handoff names this plan as the source of truth and treats the
  older Carmack plans as archived history only.
