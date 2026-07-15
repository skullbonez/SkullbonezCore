# Wide-Call Desc-Struct Pass — Retire The 12-To-20 Argument Tail

Date: 2026-07-15
Status: Active — 2/5 tasks complete
Impact area: `Runtime/Render/RuntimeRenderer`, `Runtime/Replay/*` call sites,
`Runtime/Editor/RunEditorTracer`, the wide-invocation inventory report
Owner: runtime shell / replay presentation

## Problem And Evidence

The 2026-07-15 round-4 signature work fixed the InputRouter plumbing calls but
blanket-blessed the arity tail with boilerplate keep-reasons. The worst rows
of `Agentic/Reports/2026-07-15-runtime-wide-invocation-inventory.md` (all
line refs pre-date the PhysicsWorld campaign; re-resolve at T1):

1. `BuildRuntimeRenderInputs` — **20 arguments** to construct an inputs
   struct (`Runtime/Render/RuntimeRenderer.cpp:2329`): the parameter list is
   the struct definition restated.
2. `BeginReplayPredictionJob` — **19 arguments**
   (`Runtime/Replay/ReplayPrediction.cpp:3752`).
3. `BuildReplayProbeVisualProjection` — 13
   (`Runtime/Replay/ReplayValidation.cpp:2928`).
4. `AddReplayFutureNodeToNodes` — 12 (`ReplayPrediction.cpp:2458`),
   `AppendReplayRibbonVertex` — 12 (`Runtime/Editor/RunEditorTracer.cpp:744`),
   `ApplyReplayRestoreEditorPlaceEvent` — 12 (`ReplayValidation.cpp:1355`).

The 2026-07-15 round-4 claims review ruled the copy-pasted keep-reasons a
documentation-honesty defect; this plan is the code-side answer for the tail
that is genuinely construction-of-a-record, not hot-loop value plumbing.

## Goal

Every targeted function takes one named desc/record struct (plus at most two
operation-specific scalars), populated at the call site with designated
initializers so every value is labeled. Call sites become self-documenting;
no behavior, ordering, or float change; all baselines and goldens unchanged.

## Non-Goals

- No wholesale sweep of all 190 kept inventory rows: scope is rows with
  **≥ 12 arguments** whose parameters are record construction. Hot-loop rows
  where a struct build would add per-iteration copies are re-examined and
  either converted or given an *individual* measured keep-reason (no
  boilerplate).
- No replay behavior, artifact-format, prediction, or presentation changes;
  the 200-box golden manifest is untouchable (MASTER rule 11 — any refresh
  needs explicit owner approval, and this plan must not need one).
- No ownership moves; this is signature shape only.

## Tasks

- [x] T1 — Re-resolve the ≥12-arg rows against the post-PhysicsWorld tip
      (line numbers moved), confirm each row's classification
      (record-construction vs hot-loop), and define the desc structs adjacent
      to their owners (`RuntimeRenderInputsDesc` is likely just direct
      designated-init of the existing `RuntimeRenderInputs`; replay jobs get
      `ReplayPredictionJobDesc` etc.). Commit the target table as an addendum
      section in the wide-invocation inventory report (same file, dated
      section — the report stays the single arity ledger).
      Evidence: the dated addendum reruns the balanced-token scan over the
      current tip and records 41 invoked names at ≥12 arguments. Ten are true
      record construction: one render row for T2 and nine UI/replay/editor
      rows for T3, each with its final adjacent record placement. The other 31
      rows have individual hot-path, live-owner boundary, diagnostic API, or
      normalization-factory reasons. Current count shifts from the Run shrink
      are reconciled (`ExecutePending` 22, `Load` 23). Documentation-only; no
      repository validation required.
- [x] T2 — Render side: replace the `BuildRuntimeRenderInputs` 20-arg call
      with direct designated-initializer construction (or a desc struct if
      derivation logic exists inside the builder — record which). Mapped
      gate: this is render-stream code ⇒ `tools\validate_dx12_renderer.bat`
      + `tools\run_graphics_stress.bat 1`.
      Evidence: the restating builder is deleted and the existing nested
      render records are designated directly with all 20 original expressions
      and no new type/copy. Profile build and formatting passed; the DX12 gate
      exited 0 in 51.94 s with zero InfoQueue errors and matching screenshots;
      graphics stress exited 0 in 62.72 s with empty stderr and zero memory
      reconciliation delta. Comment audit 1/1; no baseline refresh.
- [ ] T3 — Replay/editor side: convert the ≥12-arg replay/editor rows to desc
      structs with designated initializers, byte-identical argument values.
      Mapped gate: `Runtime/Replay/*` changes ⇒
      `tools\validate_replay_visual_fidelity.bat` **in addition to** the
      normal gate (one engine process, one prediction generation, unchanged
      golden — a second engine launch is an immediate failure), plus
      `tools\validate_tests.bat` for the replay test lanes.
- [ ] T4 — Inventory truth pass: update every touched row in the inventory
      report with its new arg count; re-run the report's balanced-token
      arity scan to regenerate exact max counts; any surviving ≥12-arg row
      gets an individual, non-boilerplate keep-reason naming the concrete
      cost of conversion (e.g. measured per-iteration copy in a hot loop).
- [ ] T5 — Final gates: `tools\validate_full.bat` (multiple areas), plus
      confirmation that no golden, screenshot, or physics baseline changed
      anywhere in the plan (git history is the proof). Comment audit over
      touched files.

## Dependencies And Decisions

- Independent of the Init and Run-shrink plans (different files); if run
  concurrently with the Run shrink, T2 rebases on its `RunRender.cpp` edits.
- Owner ruling 2026-07-15: threshold is ≥12 args for conversion; 7-11-arg
  rows keep their existing inventory dispositions for now.
- MASTER rule 11 applies to T3 verbatim: one mega-gate invocation, no golden
  refresh, no second engine process.

## Acceptance

- Zero ≥12-argument invocations remain in `Runtime/` without an individual
  measured keep-reason; the inventory report's regenerated scan is the proof.
- Every converted call site uses designated initializers (field names
  visible at the call site).
- DX12, replay-fidelity, tests, and full gates pass with zero baseline or
  golden changes.

## Validation

- T2: `tools\validate_dx12_renderer.bat` + `tools\run_graphics_stress.bat 1`.
- T3: `tools\validate_replay_visual_fidelity.bat` + `tools\validate_tests.bat`.
- T5: `tools\validate_full.bat`. All outputs pasted at closure.
