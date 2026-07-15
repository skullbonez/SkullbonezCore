# SkullbonezCore Session State

Date: 2026-07-15

Keep this file operational and short. Detailed evidence belongs in plans,
reports, and git history. `Agentic/Plans/MASTER-PLAN.md` is the authoritative
plan inventory.

## Current State

| Field | Value |
|---|---|
| Branch | `nightrunner-15th-july` (runtime mass-reduction campaign) |
| Current baseline | Replay visual-fidelity V0-V6 are complete: one prediction generation, one 2,401-tick presented cascade, CPU-only durable reconstruction, and 187/200 bricks grounded and sleeping through the final second |
| Current objective | Execute the runtime mass-reduction campaign: `init-startup-decomposition` → `run-member-and-include-shrink` → `wide-call-desc-struct-pass` (wide-call may run as a parallel lane; its T2 rebases on the Run shrink's RunRender edits if both are in flight) |
| Active/future progress | 3 / 16 tasks = 19% overall (mass-reduction plans only; completed campaigns and externally blocked work are excluded) |
| Last broad local gate | `tools\\validate_full.bat` passed in 311.71 s on 2026-07-15: mandatory CPU lanes, zero-warning Profile/Automation/Debug builds, replay/prediction smoke, DX12 screenshots with zero InfoQueue errors, both physics smoke lanes, and the 44,401-line byte-exact varied baseline all passed |
| Validation for current edits | Init T3 moved all 4 probe and 12 launch-resolution functions; a mechanical comparison covered 16/16 definitions and 146 string literals with zero mismatches. Project/filter validation reconciled 707/707 items; the full gate passed; comment audit inspected 5/5 touched source-bearing files with 0 deferred. No baseline or golden refresh. |

## Live Queue

0000. Runtime mass-reduction campaign is the active lane (activated
      2026-07-15; registered in MASTER at 3/16). Binding order:
      `init-startup-decomposition` (3/5; T3 probe/launch extraction complete,
      continue with T4 residue/include/line-bound closure)
      → `run-member-and-include-shrink` (6 tasks) →
      `wide-call-desc-struct-pass` (5 tasks, may run parallel to the first
      two; T2 rebases on Run-shrink RunRender edits if concurrent). Owner
      rulings: Init's round-3 parking is lifted and a free-function file
      split by responsibility is the approved shape; the Run shrink allows
      at most two cohesive owners, bans a services bag, and requires one
      end-of-plan independent ownership review; wide-call conversion applies
      only to ≥12-argument rows using designated-initializer desc structs,
      and replay-touching work runs the one-invocation 200-box mega gate per
      MASTER rule 11. Zero baseline, golden, or screenshot refresh anywhere
      in the campaign; move-only semantics with identical strings, exit
      codes, and call positions.
000. `physicsworld-stage-owner-decomposition` completed at 11/11.
     Broadphase owns its grid/candidate/diagnostic storage;
     force owns exact mutual-gravity preparation, bounded gravity scratch, and
     force dispatch without retaining borrowed frame state. Narrowphase owns
     bounded pair/island scratch and terrain owns detection/manifold/rest rows.
     The contact-solver stage owns persistent state, bounded solve scratch,
     replay transfer, and typed consequence queues;
     sleep ownership now includes all wake/seed/support/island/underwater rows
     and scratch. Diagnostics owns collision visuals, debug contacts, pipeline
     trace, and cold sink output. P10's first independent review found three
     blockers; remediation removed the forbidden TU split, concrete sibling
     references, and facade-owned sleep policy. Repeat review found zero
     credible blockers and all full/perf/allocation/comment gates passed.
     Binding rules:
     stage owners with value contexts and no
     reach-back (never a `PhysicsWorld` TU split), one owner per task per
     commit, byte-exact physics gate after every task with revert-on-diff,
     zero baseline refresh campaign-wide, allowlist rows move with their
     vectors, P7 added one `validate_physics_deep`, P10 is the mandatory
     independent ownership review.
0. Round-5 adversarial-review remediation completed 10/10 (2026-07-15,
    from the owner-commissioned review of round-4 claims). Binding order:
    `mutual-gravity-large-scene-fallback` → `fp-envelope-hardening` →
    `math-fatal-survey-restoration` (the last two are mutually independent;
    the survey waits on FP hardening only if both touch the same Maths
    files). Owner rulings: FP work is layers 1-3 only — diagnose the
    ff6e780e inline knife-edge flip, pin `/fp:contract-`/`fp_contract(off)`
    across all four projects, and re-scope the determinism docs to the
    certified binary+envelope+content contract; manifold hysteresis is
    deferred to the future SIMD lane. Gravity restores the pre-round-4
    8,192-body envelope with an exact serial fallback above the 512-body
    pair-scratch cap. The survey plan retro-commits the missing round-4
    math-fatal T2 classification evidence. Zero baseline refresh is
    authorized anywhere in this round.
    `mutual-gravity-large-scene-fallback` is complete at 3/3: body counts
    above 512 bypass triangular pair scratch and run the pre-parallel exact
    serial accumulation; a 520-body fixture is byte-identical across 0/1/4
    workers, and the unchanged physics baseline passed.
    `fp-envelope-hardening` is complete at 4/4: the historical fixture's
    Profile-only flip is diagnosed as an inline/packed-SSE optimizer-boundary
    change; all four projects force contraction off, the certified determinism
    envelope is documented honestly, and full/perf gates passed with the
    44,401-line physics baseline unchanged.
    `math-fatal-survey-restoration` is complete at 3/3: the fresh report
    reconciles every Vector3 named/division call and every Quaternion spelling
    match, finds no migration gap, and closes documentation-only after an
    independent completeness review.
1. Round-4 adversarial-review remediation completed 22/22 (2026-07-15).
   Six plans registered in MASTER with a binding order: `win32-message-pump-
   drain` → `data-driven-shadow-caster-streams` → `vector3-inline-hot-math` →
   `math-fatal-removal` → `deterministic-parallel-mutual-gravity` →
   `runtime-signature-decomposition` (plans 1-2 and 3-5 may run as parallel
   lanes; 3→4→5 and pump→signatures ordering is binding). Owner rulings per
   plan: inline-only Vector3 (padded-SIMD/SoA deferred), Try-APIs + debug
   assert, exact-sum parallel gravity (approximation rejected), signatures
   only from the god-object finding (PhysicsWorld/Init splits and Run member
   shrink deferred). Every plan requires unchanged committed baselines — no
   refresh is authorized by this round.
   `win32-message-pump-drain` is complete at 3/3: the outer loop drains up to
   256 FIFO messages before each frame, preserves `WM_QUIT` ownership/exit-code
   semantics, and passed the interactive input/quit smoke plus full gate.
   `data-driven-shadow-caster-streams` is complete at 3/3: scene-owner instance
   build resolves one opaque stream id, render submission no longer inspects
   pine material content, and unchanged renderer baselines plus stress passed.
   `vector3-inline-hot-math` is complete at 3/3: all member operations are
   header-inline, copy/assignment are trivial, the ABI remains 12 bytes, the
   44,401-line physics baseline is unchanged, and the perf gate found no
   regression.
   `math-fatal-removal` is complete at 4/4: Try normalization/division APIs
   cover reachable degeneracy, plain operators retain Debug-only assertions,
   deterministic caller fallbacks are classified, Maths has zero `SB_FATAL`,
   and the physics baseline remains byte-exact.
   `deterministic-parallel-mutual-gravity` is complete at 4/4: fixed row
   chunks compute unique triangular pair slots on workers, the owner replays
   those slots in the original serial order, 0/1/4-worker CSVs are byte-exact,
   pair construction is about 48% faster with four workers, and all mapped
   gates pass without a baseline refresh.
   `runtime-signature-decomposition` is complete at 5/5: eleven repeated-owner
   calls now use the existing stack-only frame views at 4–6 arguments; a
   208-row persistent inventory gives every remaining wide call an individual
   keep reason; independent review is clear; and the unchanged full gate passed.
1. `replay-visual-fidelity-mega-probe` is complete at 7/7 on
   `nightrunner-13th-july`. It uses one generation and one presented reveal,
   then only non-presenting CPU/artifact verification.
   `Toppled` now means more than half the wall is directly grounded and sleeping;
   the approved base has 187/200 through the final second. Independent V6 review
   found no blocking issue.
2. `replay-monolith-decomposition` is complete at 9/9 after the mandatory M8
   ownership review reopened M3-M7 and remediation reclosed M3-M6. M0 certified the exact
   starting tree; M1 bound all 55 current header type definitions/aliases and
   212 free functions to named owners; M2 mechanically split the six owner/value
   headers; M3 extracted the concrete presentation state, query, packet, and
   diagnostic owner; M4 extracted timeline retention and scrubber cursor/
   restore authority; M5 extracted authoring and its queued prediction-refresh
   value boundary; M6 extracted private prediction ownership and its
   never-stored published view while leaving mixed drawing statics for M8; M7
   published the HUD value seam, removed replay ownership from the late UI
   pass, and moved Run/query/probe behavior behind replay operations. The
   historical two-consumer grep premise was stale. The M8 review reopened
   M3-M7. Remediation reclosed presentation ownership and removed mutable
   owner/root escape hatches, frame-view reach-back, the broad workspace bag,
   mixed prediction/presentation scheduling and drawing, incomplete prediction/
   presentation/prediction owner-TU placement, event encoding, root-wide
   diagnostics traversal, root-owned cause/velocity authoring input, and the
   stale root allocation exception. M7 is now reclosed: probes own their
   workflow decisions, loaded activation is shared with production, tool/editor
   events cross a bounded value seam, and the root class is 299 lines. M8
   removed the final `RunReplay*` names, placed helpers in owner namespaces,
   renamed the narrow restore transaction header, audited 42/42 touched source
   files, and reviewed every root method across all three implementation TUs.
   No credible authority, reach-back, forwarding, or next-god-object finding remains.
   Every M0-M8 task,
   including inventory documentation, must run the
   unchanged 200-box gate before it can be checked or committed.
3. Validation-gate V3 is externally blocked at 5/6. The hosted CPU
   PR lane has a successful real run. Remaining work is a `merge_group` proof,
   required `main` branch protection, and trusted DX12-runner administration.
   Persistent self-hosted DX12 stays trusted-main/manual only; public-PR GPU
   evidence needs an ephemeral isolated runner.
4. Round-1 adversarial-review remediation is locally complete. The
   comment-rot sweep is owner-parked in `WNF/` (no comment changes yet,
   2026-07-12 ruling) and is not live portfolio work.
5. Round-2 runtime-contract remediation is locally complete and recorded in
   `Reports/2026-07-12/runtime-contract-enforcement-closure.md`.
6. Round-3 adversarial-review remediation is complete at 10/10. R10 added
   three-frame headroom and a backend-owned b1 bindless texture-index payload;
   closure evidence and the 29/29 touched-file comment audit live in
   `Reports/2026-07-13/adversarial-review-round-3-closure.md`. Out-of-scope
   rulings remain recorded in MASTER to avoid re-litigation.
7. Future-path presentation lane is complete (2026-07-14 owner request):
   `Plans/TODO/future-path-vector-splines.md` (7 tasks — near-black prediction
   sky, thin anti-aliased vector-spline trajectory ribbons, comma-cycled color
   modes with UI reflection, selected-object-only glow, and deletion of the
   temporary authoring-look cycler). Independent presentation-only lane; its
   restyle changes presented prediction frames. The owner approved the 200-box
   golden refresh after decomposition closure; T1-T7 are complete with focused
   build/test/screenshot evidence, final gates, and the one-process oracle.

## Current Plan Decisions

- `Plans/TODO/` contains live implementation work.
- `Plans/WNF/` contains only owner-parked “will not do now” work and is ignored
  unless the owner explicitly restores a plan to `TODO/`.
- The MASTER critical path is binding; preparation may run early only where it
  is explicitly named, and no work crosses a recorded dependency barrier.
- Active decomposition and future spline work execute on
  `nightrunner-14th-july`. The visual mega probe is completed historical
  evidence; every decomposition task reruns its unchanged golden 200-box
  manifest. Refactors do not authorize baseline refresh.
- Every plan-runner commit and plan-implementation prompt starts with the
  resolved MASTER progress header: plan name, completed plan tasks, and rounded
  active/future portfolio completion. That percentage covers only the active
  decomposition and future spline plans, not completed historical plans or the
  externally blocked validation lane. Ordinary commits do not claim plan
  progress.
- A completed plan may remain in the tip tree only when MASTER explicitly marks
  it as evidence for an unmet aggregate closure gate; it is deleted when that
  gate passes.
- Scene v1→v2 defines versioning semantics: integer per-format history,
  deterministic named upgrades, current/previous compatibility, recoverable
  future-version rejection, and writers stamping current. Encodings and
  version histories remain format-owned.
- `Run` remains process/frame composition only. The god-object closure rule in
  `AGENTS.md` applies across the full logical runtime surface.
- No `SimulationController` and no unified `EntityId`; `PhysicsSceneObjectId`
  remains the cross-system identity while subsystem handles remain hot-path
  currency.

## Current External Evidence

- Mandatory CPU validation PR runs 29148955729 and 29179364775 passed on
  2026-07-11 and 2026-07-12 respectively; no real `merge_group` run exists yet.
- DX12 runtime runs 29149260881 and 29149344794 were skipped while trusted
  runner activation remains disabled; they are not runtime evidence.
- `main` is currently unprotected.
- V3 activation details:
  `Agentic/Reports/validation_ci_v3_20260710.md`.
- Runtime-shell final ownership evidence:
  `Agentic/Reports/2026-07-11/runtime-shell-final-ownership-review.md`.
- Physics authority closure evidence:
  `Agentic/Reports/2026-07-11/physics-authority-and-identity-closure-review.md`.
- Entity-model closure evidence:
  `Agentic/Reports/2026-07-11/entity-model-endgame-closure.md`.
- Render-backend closure evidence:
  `Agentic/Reports/2026-07-12/render-backend-decomposition-closure.md`.
- DX12 post-cleanup closure evidence:
  `Agentic/Reports/2026-07-12/dx12-post-final-cleanup-closure.md`.
- Engine-config closure evidence:
  `Agentic/Reports/2026-07-12/engine-config-decomposition-closure.md`.
- Shader-pipeline closure evidence:
  `Agentic/Reports/2026-07-12/shader-pipeline-modernization-closure.md`.
- Render-visibility closure evidence:
  `Agentic/Reports/2026-07-12/render-visibility-architecture-closure.md`.
- Shadow-quality closure evidence:
  `Agentic/Reports/2026-07-12/shadow-edge-quality-closure.md`.
- Simulation/render interpolation closure evidence:
  `Agentic/Reports/2026-07-12/sim-render-interpolation-closure.md`.

## Next Handoff

The PhysicsWorld campaign closed at 11/11 and passed the 2026-07-15
owner-commissioned validation review (reach-back greps, capability-token
lifetime check, sequencer inspection, ledger arithmetic, per-task byte-exact
claims, and the locally runnable allocation/filter/migration checks; the P10
review's three caught-and-remediated blockers and two recorded residuals are
in the closure report). The active work is now the runtime mass-reduction
campaign at 3/16 — continue with `Plans/TODO/init-startup-decomposition.md`
T4 per the Live Queue entry above. Documentation-honesty findings from the
round-4 review remain recorded in MASTER without plans. The externally
administered validation-gate V3 lane remains blocked and excluded from the
ledger.
