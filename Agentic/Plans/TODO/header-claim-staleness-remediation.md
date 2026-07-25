# Header Claim Staleness Remediation

Date: 2026-07-25

Owner: Runtime (Input/Interaction/Editor/Scene/Render) + Core + governance

State: IN PROGRESS

Ledger tasks: 3 (HC0-HC2)

Branch at registration: `nightrunner-25th-JUL-26`

Impact area: source header comments, `Related:` pointer hygiene, comment-audit
skill, `AGENTS.md` comment governance, `validate_fast` mechanical check

Priority: High-urgency, low-cost. Every finding is comment-only, but the
Class A findings are **actively misleading the agents executing round 4 right
now**. This plan is deliberately sequenced early — ahead of the replay
partition and domain-bleed plans — because their censuses read the exact
files carrying false ownership claims.

Implementation mode: use `Agentic/Skills/orchestrator/SKILL.md`. Given the
size and comment-only nature, one independent review at whole-plan closure is
sufficient; per-task reviews are not required.

## Registration

Registered in `Agentic/Plans/MASTER-PLAN.md` inside the 2026-07-25 round-4
campaign, growing the active/future denominator from 24 to 27.

Required plan-runner commit first line:

```text
Header Claim Staleness Remediation, TASK <DONE> / 3, <OVERALL_PERCENT>% OVERALL COMPLETE — <ACTION SUMMARY>
```

Resolve `OVERALL_PERCENT` from the live ledger at commit time; this plan
interleaves with the in-flight UI plan, so nominal values are not reusable.

## Problem And Measured Evidence

Source evidence: `Agentic/Reports/2026-07-25/header-comment-staleness-audit.md`
(full audit, 306 headers plus 552 files scanned for pointer resolution, at
tip `d0e2c14f`).

The audit was triggered by a near-miss: the 2026-07-25 architecture review
reported a false "half-finished render graph" finding because `RenderGraph.h`
still described the pre-`render-graph-completion` design. A six-task campaign
was nearly registered to build what shipped on 2026-07-20. The header was
corrected in `d0e2c14f`; this plan handles everything else the audit found.

**The precise gap:** the comment regime is strong at *presence* and strong at
*deletion* — the audit found zero references to `PhysicsScene`,
`GameModelCollection`, retired render interfaces, `UIRenderContext`, deleted
SIMD kernels, or audio. It has no coverage for **responsibility movement**:
when a campaign moves ownership without deleting a symbol, every comment
naming the old owner survives and nothing re-reads it.

### Class A — falsified ownership claims (4 findings, 17 comment sites)

| ID | Site | False claim | Verified reality |
|---|---|---|---|
| A1 | 14 comments across 9 files | `RunInput` owns bindings, action logging, input-mode bookkeeping, simulation-step reset | `RunInput` does not exist: zero files, zero `class`/`struct`, zero `RunInput::`. Phantom owner, almost certainly `source-blemish-remediation` B3 rename fallout |
| A2 | `Runtime/Render/RuntimeRenderResources.h:9` | "while Run still owns the broader backend teardown order" | `RuntimeRenderer::ReleaseBackendOwnedRuntimeResources` owns the order (`RuntimeRenderer.cpp:2158`, invariant at `RuntimeRenderer.h:34`); `Run.cpp:374` only calls it |
| A3 | `Runtime/Scene/SceneController.h` (line 326 at time of registration) | "while Run temporarily executes the returned batch during lifecycle extraction C1" | C1 is closed; `TakePendingRequests` is consumed at `Runtime/Scene/SceneRequestExecution.cpp`, not in Run |
| A4 | `Runtime/Editor/EditorOverlayTools.h:8` | "Run still owns runtime side effects" | Superseded by `run-execute-deaccretion` X0-X2 and the binding decision that Run is only process/frame composition |

A1's full 14-site table is in the audit report and is the authoritative
inventory for HC0. **Line numbers throughout are as of tip `d0e2c14f` and a
concurrent repository-wide comment-alignment format pass has already shifted
some;** the search patterns are authoritative, so re-derive positions from
the tip before editing. All four Class A findings were re-verified after that
format pass and still hold.

### Class B — broken `Related:` pointers (12, mechanically verified)

Full table in the audit. Systemic sub-pattern: four rows (B3, B5, B10, B12)
cite `Agentic/Plans/TODO/*.md` paths that **inventory rule 4 deletes at
closure** — headers citing live plans are broken pointers by construction.
Two rows (B1, B2) are days old from UR4 deleting
`Rendering/RenderProfilerPresentation.cpp`.

### Class C — cleared, do not touch

~55 `currently`/`pending`/`temporarily` hits correctly describe runtime state
("rows currently owned by live resources", "pending query ring depth"). A
bulk vocabulary edit would damage good comments. The audit lists every
cleared item.

## Goal

1. Every Class A comment names the owner that live source actually shows.
2. Every Class B pointer resolves, and headers stop citing deletion-bound
   plan paths.
3. The comment audit gains a claim-verification step so responsibility
   movement is caught at the campaign that causes it.
4. `Related:` resolution becomes a mechanical gate — the one fully
   automatable part of this failure class.

## Non-Goals

- No code change of any kind. Every HC0/HC1 diff is comments and `Related:`
  lines only; if behavior would change, stop and re-scope.
- No bulk find-and-replace of `RunInput` to a single name. Each of the 14
  sites needs the correct owner for *that* site.
- No banned-word gate on `currently`/`still`/`pending`. Class C proves those
  words are frequently correct; the rot markers are review prompts, not
  defects.
- No rewriting of headers beyond the falsified claims and pointers. This is
  not a comment-quality pass.
- No baseline, golden, artifact, scene, config, or physics CSV interaction.

## Permanent Invariants

1. A header comment asserting an owner names an owner that exists in source.
2. `Related:` paths resolve. Headers cite permanent closure **reports** under
   `Agentic/Reports/<date>/`, never `Agentic/Plans/TODO/` paths that
   inventory rule 4 deletes.
3. When a change moves a responsibility, the same change corrects every
   comment describing the previous owner in the files it touches.
4. Rot markers (`still owns`, `remains the owner`, `currently`, `for now`,
   `temporarily`, `on this branch`, `not yet`, or an embedded task code such
   as `C1`/`UR3`) require verification during the comment audit; their
   presence is a prompt, not automatically a defect.

## Ledger

- [x] **HC0 — Correct the falsified ownership claims.**

  Fix all 17 Class A comment sites. Comment-only.

  Implementation notes:

  1. **A1 requires per-site judgment.** For each of the 14 `RunInput`
     mentions, determine the real owner from source before editing. The
     likely owners differ by site: `InputRouter` (retained input routing,
     context, pointer state), `InputController.Bindings` (the immutable
     binding table), `InputFrame`/`InputFrameExecution` (frame-local
     assembly and turn sequencing), or `OperatorCommandApplier` itself. The
     standing `AGENTS.md` Runtime package rule names `InputRouter` as the
     only retained input routing/context/pointer owner — use it to
     disambiguate, and confirm against the actual call path rather than
     assuming. Do **not** substitute one name globally.
  2. For "action logging" sites (`EditorTools.h:172`,
     `OperatorCommandApplier.h:142,158`, `SceneRuntimeCoordinator.h:99`),
     trace where accepted-command flags are actually consumed and name that
     consumer. If the logging path no longer exists, delete the claim rather
     than renaming it.
  3. **A2**: replace the `Run` teardown claim with the real contract —
     `RuntimeRenderer` owns ordered backend release after a successful GPU
     drain, keeping consumer passes ahead of producer passes; `Run`
     sequences the call. Reuse the wording already proven in
     `RuntimeRenderer.h:34` so the two headers agree.
  4. **A3**: delete the completed-task reference entirely. State where batch
     execution lives now (`SceneRequestExecution.cpp`). Never cite a task
     code as live context — that is guaranteed rot. Note this file is inside
     `invariant-ownership-governance-and-transaction-repair` GV2's census
     scope; correcting it now prevents GV2 from inheriting the false context.
  5. **A4**: state the actual contract (the calling owner applies runtime
     side effects; these helpers refresh preview state and append tool
     geometry from borrowed state) without asserting a `Run` ownership the
     de-accretion campaigns removed.
  6. Re-run the audit's Class A searches from the final tip and confirm zero
     surviving phantom-owner or superseded-owner claims.

  Acceptance:

  - `rg -n '\bRunInput\b' SkullbonezSource` returns no rows. The word
    boundary intentionally ignores the real `RunInputPhase` symbol.
  - A2/A3/A4 claims match live source, verified by naming the source
    file:line that proves each new statement in the commit body.
  - `git diff` shows comment lines only; prove it explicitly.
  - No repository validation required (comment-only per `AGENTS.md`).

  Evidence (2026-07-25):

  - Corrected all 17 registered sites plus one additional `UICommands.h`
    phantom-owner claim exposed by the final word-boundary proof.
  - `rg -n '\bRunInput\b' SkullbonezSource` returns no rows.
  - `rg -n 'lifecycle extraction C1|Run still owns' SkullbonezSource`
    returns no rows.
  - The old/new/proof table is recorded in
    `Agentic/Reports/2026-07-25/header-claim-staleness-hc0.md`.
  - HC0 changed comments/documentation only; no repository validation was
    required.

- [ ] **HC1 — Fix the pointers and make resolution mechanical.**

  Repair all 12 Class B rows and add the automatable guard.

  Implementation notes:

  1. Repoint the four plan-path rows (B3, B5, B10, B12) at the permanent
     closure reports for those campaigns, not at recreated plan files.
  2. B1/B2 (`Core/Profiler.{h,cpp}` → deleted
     `Rendering/RenderProfilerPresentation.cpp`): repoint at whatever UR4
     made the presentation owner. Coordinate with the in-flight UI plan —
     if UR6 has not landed, confirm the destination from the UR4 commit
     rather than guessing.
  3. B6/B7/B8 (`Runtime/RunPasses.cpp`) and B11
     (`SkullbonezSource/GameObjects/SceneController.cpp`): resolve to the
     post-decomposition paths.
  4. B9 points into `Agentic/Plans/In_Progress/`, a folder
     `Agentic/README.md` explicitly bans. Delete the row; do not recreate
     the folder.
  5. B10 (`Runtime/Replay/ReplayPredictionArchive.h`) is a file
     `replay-subsystem-partition` RS1 will move into `Runtime/Prediction`.
     Fix the pointer now anyway — the correction is comment-only and moves
     with the file; note the overlap so RS1's agent expects it.
  6. **Add the mechanical check.** Extend the existing repository comment
     metadata/format preflight (the one `validate_fast` already runs) with
     `Related:` path resolution: parse each `Related:` block, test every
     repository-relative path, and fail with the file, line, and dead path.
     Self-test with a positive fixture (resolving path) and a negative
     fixture (dead path). Non-repository-relative entries (bare topic names)
     are ignored, not failures.

  Acceptance:

  - All 12 rows resolve; the audit's resolution sweep returns zero.
  - The new check runs inside `validate_fast` through the established call
    chain, with both fixtures passing.
  - `tools\validate_fast.bat` passes (tooling change requires it per the
    file-to-validation map), and the changed script/checker is run directly.

- [ ] **HC2 — Install the claim-verification governance.**

  Close the process gap so the next campaign catches its own comment rot.

  Implementation notes:

  1. Extend `Agentic/Skills/comment-style-audit/skill.md` with a
     **claim-verification step**: for each touched file, identify sentences
     asserting ownership, sequencing, or subsystem behavior, and confirm
     each against post-change source. Any claim the change falsified is
     corrected in the same commit. Include the rot-marker list from
     permanent invariant 4 as a verification prompt, and state explicitly
     that marker presence is not itself a defect (cite Class C: ~55 correct
     uses describing runtime state).
  2. Add to `AGENTS.md` Comment Quality Gate: permanent invariants 1-3 —
     comments name owners that exist, `Related:` cites permanent reports
     rather than deletion-bound `TODO/` plans, and a responsibility move
     corrects the prose describing the old owner in the same change.
  3. **Coordinate with GV0.** The registered
     `invariant-ownership-governance-and-transaction-repair` GV0 also amends
     this skill file (aggregate-invariant check). These are separate
     sections; whichever lands second rebases rather than overwrites. Record
     in both plans that the file has two pending amendments.
     GV0 landed first on 2026-07-25; HC2 must preserve its
     aggregate-invariant check and add claim verification as a separate step.
  4. Add one worked example drawn from real history: the RenderGraph
     near-miss (stale header → false review finding → nearly-registered
     six-task campaign). It is the most persuasive argument the guide can
     carry for why this step exists.
  5. Do not add a counting gate, banned-word list, or frozen inventory.

  Acceptance:

  - Skill and `AGENTS.md` carry the step, the invariants, the rot-marker
    prompt with its not-a-defect caveat, and the worked example.
  - The GV0 coordination note exists in both plan files.
  - Documentation-only; no repository validation required.

## Dependencies And Decisions

- **Sequenced early by design.** Runs immediately after
  `ui-renderer-hard-boundary` closes and **before**
  `replay-subsystem-partition`, because RS0's census reads Replay headers
  (B10) and GV2's census reads `SceneController.h` (A3). Fixing after those
  censuses would mean both inherit false context.
- May also run fully in parallel with the tail of the UI plan if the owner
  prefers: every HC0 diff is comment-only in Input/Interaction/Editor/Scene
  files that the UI plan does not touch. HC1's B1/B2 rows are the only UI
  overlap and can be deferred within the task until UR6 lands.
- The whole plan is documentation/comment-only except HC1's checker, which
  is the sole item requiring `validate_fast`.
- No owner decision is required to start. If HC0 finds a site where the real
  owner is genuinely ambiguous, record the ambiguity in the commit body and
  leave the comment factual-but-vague rather than asserting a wrong owner.

## Static Closure Proofs

```powershell
rg -n '\bRunInput\b' SkullbonezSource
rg -n 'lifecycle extraction C1|Run still owns' SkullbonezSource
rg -n 'Related:' -A 6 SkullbonezSource | rg -n 'Agentic/Plans/TODO/'
```

All three must return no rows at closure. The HC1 mechanical `Related:`
resolution check is the authoritative pointer proof.

## Validation Map

| Phase | Iteration evidence | Pre-commit/closure gates |
|---|---|---|
| HC0 | Comment-only diff proof; per-site owner evidence | None required (comment-only) |
| HC1 | Checker self-test, both fixtures, resolution sweep | `tools\validate_fast.bat`, then run the changed checker directly |
| HC2 | Governance diff review | None required (documentation-only) |

## Closure Evidence Requirements

- The 17 Class A sites with old text, new text, and the source file:line
  proving each new claim.
- The 12 Class B rows with resolved destinations, and the deleted B9 row.
- Checker fixture output and `validate_fast` result.
- Proof that HC0/HC1 source diffs are comments and `Related:` lines only.
- Final output of all three static closure proofs.
- The independent review verdict, whose mandate is: did any comment acquire
  a *new* false claim, and does any surviving rot marker hide one?
