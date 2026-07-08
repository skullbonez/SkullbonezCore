# 03 — Governance Apparatus Removal

Date: 2026-07-08
Status: Proposed (changes the project contract — needs owner sign-off to execute)
Priority: P1
Owner: Repo maintainer
Source issue: audit iss-04 + iss-12 (severity 4/3)

## Decision (definitive)

**Remove the entire regex-based enforcement apparatus.** This is not a "trim it
down" plan — the frozen-count linter goes away completely. The only survivor is
one genuine product invariant (DX12-only renderer), re-expressed as a real
pass/fail check with no frozen number. Everything else is deleted and replaced by
code review plus the real behavioral tests from plan 05.

Rationale: a check whose value is a frozen count of today's debt
(`MAX_SOURCE_THROW_TOKENS = 294`, `MAX_RUN_PRIVATE_MEMBER_FIELDS = 41`, …) is not
an invariant — it just says "don't change what exists," breaks on reformatting,
and must be hand-edited every time guarded code moves. For a solo engine that is
pure tax.

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

- [ ] **0.1 (SIGN-OFF GATE — stop for a human).** Get explicit owner approval to
  delete the checker and edit `AGENTS.md`. Until approved, do only the read-only
  inventory in 0.2.
- [ ] **0.2** Inventory: confirm which real product invariants (if any) the
  checker nominally protects that are worth re-expressing. Default outcome: only
  DX12-only exclusivity survives. No code change; record the list here.
- [ ] **1.1** *(after 0.1)* Re-express the DX12-only check as a standalone
  boolean check (or a `Dx12ArchUnitTests` case). Gate: `validate_tests`. Commit.
- [ ] **1.2** Delete `tools/check_runtime_boundaries.py` and every invocation of
  it (`validate_fast.bat`, `.githooks/`, `.pre-commit-config.yaml`, CI). Build +
  run `validate_fast` to prove nothing depends on it. Commit.
- [ ] **2.1** Strip the regex-enforced gates from `AGENTS.md` (Migration Artifact
  Gate, throw ratchet, hot-path inheritance count, unenforceable allocation
  prose). Replace with: "enforced by code review + the tests in plan 05."
  Commit.
- [ ] **3.1** Merge `fable_plans/` into `Agentic/Plans/`; move
  Done/Failed/Rejected out of the live working set into an archive folder. No
  validation (doc moves). Commit.
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
- [ ] `AGENTS.md` describes no regex-enforced gate; enforcement is review + plan
  05 tests + the two hard runtime gates.
- [ ] At most one survivor — the DX12-only check — and it is a boolean pass/fail
  with no frozen number.
- [ ] Single live plan tree; archived plans separated from active ones.
