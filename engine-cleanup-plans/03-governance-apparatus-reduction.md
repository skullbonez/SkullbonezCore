# 03 — Governance Apparatus Removal

Date: 2026-07-08
Status: In Progress (owner approved deletion on 2026-07-09)
Priority: P1
Owner: Repo maintainer
Source issue: audit iss-04 + iss-12 (severity 4/3)

## Decision (definitive)

**Remove the entire regex-based enforcement apparatus.** This is not a "trim it
down" plan — the frozen-count linter goes away completely. The only survivor is
one genuine product invariant (DX12-only renderer), re-expressed as a real
pass/fail check with no frozen number. Everything else is deleted and replaced
by code review plus the behavioral tests in
`Agentic/Plans/TODO/behavioral-test-depth.md` (the successor to closed plan
05 — **prerequisite satisfied 2026-07-09:** its P1 and P4 tests exist before
step 2.1 lands; its P5 injected-bug drill is the honest sign-off for the
deletion).

Rationale: a check whose value is a frozen count of today's debt
(`MAX_SOURCE_THROW_TOKENS = 294`, `MAX_RUN_PRIVATE_MEMBER_FIELDS = 41`, …) is not
an invariant — it just says "don't change what exists," breaks on reformatting,
and must be hand-edited every time guarded code moves. For a solo engine that is
pure tax.

## Owner Decision - 2026-07-09

The owner approved this plan. Delete the regex boundary checker and the frozen
`MAX_*` ratchet apparatus, and update `AGENTS.md` accordingly when the relevant
implementation steps run. Keep real product safety checks as simple pass/fail
validation where needed, especially DX12-only enforcement. Do not replace the
old spelling/ratchet checker with new vocabulary policing.

## Problem (evidence)

- [`tools/check_runtime_boundaries.py`](../tools/check_runtime_boundaries.py) is
  **16,090 lines / 808 KB**, 389 functions — ~4× the largest source file, ~6.5×
  the whole test suite. Its checks are frozen debt-ratchets plus one-off regex
  fences around past refactors.
- **275 tracked `.md` files / ~86K lines (~42% of the repo)**: a formal
  Done/Failed/Rejected plan lifecycle, 79 dated reports, 13 `HANDOFF-*.md`, **two**
  parallel plan trees (`Agentic/Plans/` + `fable_plans/`), and a Comment Quality
  Gate that mandates learning headers even on trivial batch wrappers.

## Scope — what is deleted vs kept

**Deleted outright:**

- The whole file `tools/check_runtime_boundaries.py`.
- Every frozen `MAX_*` budget (throw tokens, Run field/method counts,
  global-service census, render-pass-host field accesses, etc.).
- Every per-refactor `check_*_guardrails` regex fence.
- All invocations: `tools\validate_fast.bat`, `.githooks/`,
  `.pre-commit-config.yaml`, and any CI step that runs the checker.
- The `AGENTS.md` gates that exist only to be regex-enforced: **Migration
  Artifact Gate**, the **throw-ban ratchet** (see plan 04), the **Hot-Path
  inheritance count**, and allocation prose the checker cannot enforce (see plan
  07).

**Kept — as a real check, not regex counting:**

- **DX12-only renderer exclusivity.** Re-expressed as a ~15-line boolean presence
  check (no OpenGL/DX11 runtime dependency), or folded into the existing
  `Agentic/Tests/Dx12ArchUnitTests`. Pass/fail, no frozen number. *(If the owner
  prefers, drop even this and rely on review.)*

**Related plans:** plan 04 deletes the throw ratchet; plan 07 deletes the regex
allocation checker. After all three land, no frozen-count regex enforcement
exists anywhere in `tools/`.

## Step-by-step implementation

**This edits the project contract (`AGENTS.md`) and deletes tooling.** Step 0.1
requires human sign-off before any deletion.

- [x] **0.1 (SIGN-OFF GATE — stop for a human).** Get explicit owner approval to
  delete the checker and edit `AGENTS.md`. Until approved, do only the read-only
  inventory in 0.2.
  - Approved by owner steering on 2026-07-09. Continue from the current branch
    and worktree; do not ask again for this gate.
- [x] **0.2** Inventory: confirm which real product invariants (if any) the
  checker nominally protects that are worth re-expressing. Default outcome: only
  DX12-only exclusivity survives. No code change; record the list here.

  Completed 2026-07-09:
  - Survivor to re-express in step 1.1: **DX12-only runtime renderer
    exclusivity**. The engine may keep accepting `--renderer dx12` and retired
    compatibility wrappers such as `validate_renderers.bat`, but no OpenGL/DX11
    runtime backend, runtime renderer choice, or validation launch should return.
    This should be a small boolean pass/fail check, preferably in
    `Agentic/Tests/Dx12ArchUnitTests`, not a frozen regex budget.
  - Real policy owned elsewhere, not by this checker: runtime allocation policy
    remains global zero-allocation-by-default with the replay-only allocator
    exception, but Plan 07 and `tools/check_allocation_policy.py` own that gate.
  - Do **not** preserve as checker invariants: source throw counts,
    `Run` member/method counts, stored model-index counts, render-pass host field
    counts, global-service census, `IRenderBackend`/`RenderBackendDX12::Get`
    tombstones, `GameModelCollection` physics dependency census, RenderGraph
    ownership vocabulary fences, migration-artifact spelling fences, source
    inheritance budgets, replay/source-layout fences, and interaction-state
    spelling checks. These are review/test/plan-follow-through concerns, not
    product invariants, and should disappear with the regex apparatus.
  - Evidence commands used: `rg` over `tools/check_runtime_boundaries.py` for
    `MAX_*` constants/check functions, `rg` for checker invocations, and a
    focused scan of existing DX12 architecture tests and renderer validation
    wrappers. CodeGraph was attempted first for the checker survey but did not
    surface the large Python file cleanly, so the inventory used targeted text
    queries after that.
- [x] **1.1** *(after 0.1)* Re-express the DX12-only check as a standalone
  boolean check (or a `Dx12ArchUnitTests` case). Gate: `validate_tests`. Commit.

  Completed 2026-07-09:
  - Added `SkullbonezSource/Runtime/RunLaunchOptions.Renderer.h` as the single
    runtime renderer option table used by launch parsing, so the invariant is a
    concrete typed surface instead of a regex scan.
  - Added `SkullbonezTests/TestDx12OnlyRuntime.cpp` to assert the runtime launch
    table exposes exactly one renderer option, `dx12`, with only the `d3d12`
    alias. This preserves DX12-only exclusivity while still allowing the
    compatibility alias.
  - Touched-file comment audit inspected 3 source-bearing files with 0 deferred:
    `SkullbonezSource/Runtime/Init.cpp`,
    `SkullbonezSource/Runtime/RunLaunchOptions.Renderer.h`, and
    `SkullbonezTests/TestDx12OnlyRuntime.cpp`.
  - Validation: `tools\validate_tests.bat` passed in 00:00:04.2783435
    (`VALIDATE_TESTS: ALL PASSED`, 68/68 doctest cases and 1611/1611 assertions)
    after the final header name. `tools\validate_full.bat` passed in
    00:01:11.6163450 with project filters clean, runtime boundaries 0 errors,
    Profile/Debug builds 0 warnings/errors, DX12 validation errors 0,
    screenshots matching baselines, and `physics_regression_solver.csv`
    byte-exact. A prior `validate_full` attempt failed only because the first
    header name lacked a project-filter rule; renaming to
    `RunLaunchOptions.Renderer.h` aligned with the existing project filters.
- [x] **1.2** Delete `tools/check_runtime_boundaries.py` and every invocation of
  it (`validate_fast.bat`, `.githooks/`, `.pre-commit-config.yaml`, CI). Build +
  run `validate_fast` to prove nothing depends on it. Commit.

  Completed 2026-07-09:
  - Deleted `tools/check_runtime_boundaries.py` and
    `tools/validate_runtime_boundaries.bat`.
  - Removed the wrapper from `tools\validate_fast.bat`,
    `tools\validate_full.bat`, and `tools\validate_select.bat`; removed the
    deleted target from `tools\README.md`; updated `Agentic/README.md` so it no
    longer describes the deleted checker as the live inheritance ratchet.
  - Confirmed `.githooks`, `.pre-commit-config.yaml`, `.github`, and `tools/`
    have no live `check_runtime_boundaries`, `validate_runtime_boundaries`,
    `runtime-boundaries`, or `runtime_boundaries` references after the deletion.
  - Touched-file comment audit inspected 3 edited source-bearing tool scripts
    with 0 deferred: `tools\validate_fast.bat`, `tools\validate_full.bat`, and
    `tools\validate_select.bat`. The old Python checker and wrapper script were
    deleted rather than remediated.
  - Validation: `tools\validate_fast.bat` passed in 00:00:18.9508581 after the
    deletion (`VALIDATE_FAST: ALL PASSED`, Profile/Debug ready, unit tests
    passed). `tools\validate_select.bat project-filters` passed in
    00:00:05.3469357. `tools\validate_full.bat` passed in 00:00:28.5120019 with
    project filters clean, Profile/Debug builds 0 warnings/errors, DX12
    validation errors 0, screenshots matching baselines, and
    `physics_regression_solver.csv` byte-exact.
- [x] **2.1** Strip the regex-enforced gates from `AGENTS.md` (Migration Artifact
  Gate, throw ratchet, hot-path inheritance count, unenforceable allocation
  prose). Replace with: "enforced by code review + the tests in
  `Agentic/Plans/TODO/behavioral-test-depth.md`." Do not land this step until
  that plan's P1 (solver-stage tests) and P4 (replay round-trip test) exist.
  Commit.

  Prerequisite completed 2026-07-09:
  - `Agentic/Plans/TODO/behavioral-test-depth.md` P1 now has named
    solver-stage tests for warm-start cache reuse, friction-cone clamp,
    restitution bounce, and sleep threshold/wake behavior.
  - P4 now has the existing replay snapshot restore test plus a nonzero solver
    hash equality check in the unit runner.
  - `tools\validate_tests.bat` passed in 00:00:05.3128576 with 72/72 cases and
    1643/1643 assertions.
  - Completed 2026-07-09: `AGENTS.md` now states the deleted regex checker is
    not enforcement, routes migration/hot-path/throw policy to review,
    behavioral tests, and targeted validation, and keeps allocation policy
    under Plan 07 rather than frozen regex budgets.
- [x] **3.1** Executed 2026-07-09 by the owner-directed plan consolidation,
  with deletion instead of archiving: Done/Failed/Rejected trees deleted;
  `fable_plans/`, `To_Eval/`, and `In_Progress/` consolidated into
  `Agentic/Plans/TODO/`; master inventory at `Agentic/Plans/MASTER-PLAN.md`.
  Recorded in the plan consolidation follow-up history.
- [x] **4.1** Relax the Comment Quality Gate in `AGENTS.md` so trivial wrappers,
  link stubs, and batch files are exempt from full learning-header requirements.
  Commit.

  Completed 2026-07-09:
  - `AGENTS.md` exempts trivial wrappers, link stubs, one-line forwarding files,
    and tiny batch/PowerShell helpers from full learning-header requirements
    when the diff is self-explanatory. Non-obvious validation purpose, shell
    hazards, ownership, and runtime behavior still need local comments.

## Risks

- Deleting the checker loses whatever real invariants were buried in it. The 0.2
  inventory exists to catch any beyond DX12-only *before* deletion — do not skip
  it. If 0.2 surfaces a genuine invariant, re-express it as a real check in 1.1;
  do **not** keep it as a frozen count.
- Contract-changing. Land as a reviewed proposal, not a drive-by deletion.

## Validation

`tools\validate_fast.bat` after the deletion (proves nothing still calls the
checker). Documentation moves need no validation.

## Acceptance (measurable)

- [x] `tools/check_runtime_boundaries.py` no longer exists.
- [x] No `MAX_*` frozen-count budget exists anywhere in `tools/`.
- [x] No validation script, git hook, or CI step invokes the deleted checker.
- [x] `AGENTS.md` describes no regex-enforced gate; enforcement is review + the
  `TODO/behavioral-test-depth.md` tests + the two hard runtime gates.
- [x] At most one survivor — the DX12-only check — and it is a boolean pass/fail
  with no frozen number.
- [x] Single live plan tree; archived plans separated from active ones.
