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
05 — **prerequisite:** its P1 and P4 tests should exist before step 2.1
lands; its P5 injected-bug drill is the honest sign-off for the deletion).

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
- [ ] **1.1** *(after 0.1)* Re-express the DX12-only check as a standalone
  boolean check (or a `Dx12ArchUnitTests` case). Gate: `validate_tests`. Commit.
- [ ] **1.2** Delete `tools/check_runtime_boundaries.py` and every invocation of
  it (`validate_fast.bat`, `.githooks/`, `.pre-commit-config.yaml`, CI). Build +
  run `validate_fast` to prove nothing depends on it. Commit.
- [ ] **2.1** Strip the regex-enforced gates from `AGENTS.md` (Migration Artifact
  Gate, throw ratchet, hot-path inheritance count, unenforceable allocation
  prose). Replace with: "enforced by code review + the tests in
  `Agentic/Plans/TODO/behavioral-test-depth.md`." Do not land this step until
  that plan's P1 (solver-stage tests) and P4 (replay round-trip test) exist.
  Commit.
- [x] **3.1** Executed 2026-07-09 by the owner-directed plan consolidation,
  with deletion instead of archiving: Done/Failed/Rejected trees deleted;
  `fable_plans/`, `To_Eval/`, and `In_Progress/` consolidated into
  `Agentic/Plans/TODO/`; master inventory at `Agentic/Plans/MASTER-PLAN.md`.
  Not yet committed (another agent active); commit rides with the next
  documentation commit.
- [ ] **4.1** Relax the Comment Quality Gate in `AGENTS.md` so trivial wrappers,
  link stubs, and batch files are exempt from full learning-header requirements.
  Commit.

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

- [ ] `tools/check_runtime_boundaries.py` no longer exists.
- [ ] No `MAX_*` frozen-count budget exists anywhere in `tools/`.
- [ ] No validation script, git hook, or CI step invokes the deleted checker.
- [ ] `AGENTS.md` describes no regex-enforced gate; enforcement is review + the
  `TODO/behavioral-test-depth.md` tests + the two hard runtime gates.
- [ ] At most one survivor — the DX12-only check — and it is a boolean pass/fail
  with no frozen number.
- [ ] Single live plan tree; archived plans separated from active ones.
