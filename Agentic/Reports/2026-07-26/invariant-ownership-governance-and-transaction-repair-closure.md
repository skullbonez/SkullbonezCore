# Invariant Ownership Governance And Transaction Repair Closure

Date: 2026-07-26

Branch: `nightrunner-25th-JUL-26`

Plan result: GV0-GV4 complete (5/5)

## Outcome

The repository now distinguishes authority-free parameter bags from concrete
invariant owners. `AGENTS.md` and the comment-audit skill require a named,
enforced invariant plus focused coverage; they do not relax the context-bag,
callback, reach-back, forwarding, inheritance, or 12-parameter rules.

Two previously homeless runtime invariants now have stack-scoped owners:

- `SceneLoadTransaction` enforces
  `Idle -> Load -> RuntimeReactions -> Presentation -> Complete` and owns
  mid-batch navigation/presentation arbitration.
- `SceneGeneratedControlTransaction` enforces
  `Idle -> DrainAndReset -> Repopulate -> PublishFollowUps -> Complete`,
  partial-count arbitration, failed-drain mutation denial, and detached
  replay/profile follow-ups.

Both owners retain values and phase cursors only. Runtime owners are borrowed
synchronously by the phase that uses them; neither transaction stores an owner
pointer/reference or survives across frames.

## Governance And Census Evidence

- GV0 installed the permanent Invariant Ownership Rule in `AGENTS.md`, the
  aggregate-invariant check in
  `Agentic/Skills/comment-style-audit/skill.md`, and the binding decision in
  `Agentic/Plans/MASTER-PLAN.md`.
- GV1's complete 201-operation implementation-tip census and disposition table
  remains in
  `invariant-ownership-governance-gv1-census.md`.
- The final hand-filtered wide-operation census contains 202 definitions:
  the original 196 ruled `wide-only` operations, the same three PB0-assigned
  13-parameter UI/render operations, and three concrete scene-load transaction
  surfaces replacing the two homeless free apply operations. The new surfaces
  have arities 9, 8, and 10, all below the 12-parameter ceiling.
- The final suffix-family sweep contains 18 rows: `SceneLoadPolicyInputs`
  (assigned to PB1), ten previously ruled `*GraphInputs`, and seven previously
  ruled `*PassInputs`. No unruled extrusion candidate remains.
- Arbitration sweep: zero rows.
- Ordering sweep: only the retained cohesive
  `CameraCollection::ResetRelativity` checkpoint and two prose-only
  “must observe” comments.

## Scene-Load Repair

Deleted symbols, with zero final source/test rows:

- `ApplySceneLoadRuntimeReactions`
- `ApplySceneLoadPresentationOutputs`
- `SceneNavigationForFollowingRequest`
- `ScenePresentationForFollowingRequest`
- `SceneLoadInteractionParticipants`
- `SceneLoadPresentationParticipants`

The two participant structs were pure pass-through bags. Their values now cross
as explicit camera, navigation, debug, frame-drain, cold-resource, and renderer
arguments. `SceneLoadPolicyInputs` remains deliberately assigned to PB1 in the
registered concrete parameter-bag plan.

Focused tests exhaust the 25 legal/illegal scene-load cursor pairs and construct
the real transaction to prove request/phase/output gating for loaded versus
submitted navigation and presentation.

Detailed implementation evidence:
`invariant-ownership-governance-gv2-scene-load-transaction.md`.

## Generated-Scene Repair

Deleted symbols, with zero final source/test rows:

- `SceneGeneratedControlPolicy`
- `SceneGeneratedControlPresentation`
- `SceneGeneratedControlResetParticipants`
- the five former free generated-control apply operations

Focused tests exhaust all 36 cursor transitions and instantiate the real
transaction. Bounded friend access invokes the exact private production
kernels; it contains no copied arbitration rule or owner borrow. Coverage proves:

- newest partial solver-count arbitration and capacity trimming;
- an injected drain failure records failure, denies mutation permission, leaves
  external UI/session values unchanged, and publishes no follow-up flags;
- a successful active rebuild permits mutation only in `DrainAndReset`; and
- follow-up flags publish only after repopulation for an active rebuild.

Production `DrainAndReset` feeds the real GPU result through the same
`RecordDrainResult` kernel, returns immediately on failure, and checks
`MutationAllowedAfterDrain` immediately before the first owner mutation.

Detailed implementation evidence:
`invariant-ownership-governance-gv3-generated-scene-transaction.md`.

## Comment Audit

The complete plan delta contains 14 unique source-bearing files. All 14 were
inspected against `Agentic/Reference/comment-style-guide.md` and the
GV0-extended aggregate-invariant rule; 14 checked, 0 deferred, 0 unchecked:

1. `SkullbonezSource/Runtime/App/InputFrame.cpp`
2. `SkullbonezSource/Runtime/App/InputFrameExecution.cpp`
3. `SkullbonezSource/Runtime/App/Run.cpp`
4. `SkullbonezSource/Runtime/App/RunFrame.cpp`
5. `SkullbonezSource/Runtime/Capture/RuntimeStressController.cpp`
6. `SkullbonezSource/Runtime/Scene/SceneController.h`
7. `SkullbonezSource/Runtime/Scene/SceneController.Load.cpp`
8. `SkullbonezSource/Runtime/Scene/SceneLoadTransaction.h`
9. `SkullbonezSource/Runtime/Scene/SceneRequestExecution.cpp`
10. `SkullbonezSource/Runtime/Scene/SceneRuntimeGeneratedControls.cpp`
11. `SkullbonezSource/Runtime/Scene/SceneRuntimeGeneratedControls.h`
12. `SkullbonezSource/Runtime/Scene/SceneWorld.h`
13. `SkullbonezTests/TestOwnerRequestQueues.cpp`
14. `tools/validate_project_filters.py`

Remediation corrected one stale same-frame solver override claim, added
type-adjacent `Invariant:` blocks and exact focused-test references, restored
the DX12 drain/resource lifetime hazard beside the explicit scene-load
signature, and updated transaction headers to describe the final test depth.

## Independent Review

Review run: `invariant-ownership-governance-duck-01`

Reviewer: `/root/gv4_hostile_review`

The first read-only pass returned `BLOCK`:

1. two scene-load participant bags were still pure pass-throughs;
2. scene-load tests exercised static selectors rather than transaction
   request/phase/output gating; and
3. generated-scene tests covered only the cursor, not drain denial,
   arbitration, or follow-up publication.

All three findings reopened their owning work and were remediated. The same
reviewer then repeated the two-sided hostile review and returned `PASS`:

- no surviving homeless invariant or unruled extrusion shape;
- no camouflage owner, stored owner borrow, service bag, or cross-frame state;
- all explicit scene-load APIs remain below 12 parameters; and
- bounded friend access exercises production kernels rather than duplicate test
  logic.

No review finding remains unresolved.

## Final Validation

No baseline, golden, replay artifact, scene, config, or physics CSV was
refreshed.

| Command | Result |
|---|---|
| `tools\validate_tests.bat` | PASS; 397/397 doctests, 2,403,431 assertions |
| `tools\validate_full.bat` | PASS in 165.3 s; formatting/Related paths, 783/783 project filters, dependency graph, production builds, CPU/coverage, Automation/replay, DX12, and byte-exact 44,401-line physics |
| `tools\run_graphics_stress.bat 1` | PASS in 61 s; bounded DX12 run ended by its expected PID timeout |

The first focused test attempt exposed two test-only pointer-identity
expectations: transaction outputs are owned copies, not aliases of submitted
fixtures. Those expectations were corrected to assert transaction-owned versus
submitted selection; the final focused and full gates pass.

## Handoff

The completed five-task plan leaves the live inventory under rule 4. The only
active/future plan is `concrete-parameter-bag-elimination` at 0/8, so the live
ledger is 0/8 (0%). PB0 is next.
