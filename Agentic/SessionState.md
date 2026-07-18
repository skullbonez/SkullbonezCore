# SkullbonezCore Session State

Date: 2026-07-18

Keep this file operational and short. Detailed evidence belongs in plans,
reports, and git history. `Agentic/Plans/MASTER-PLAN.md` is the authoritative
plan inventory.

## Current State

| Field | Value |
|---|---|
| Branch | `nightrunner-17th-july` |
| Current baseline | Round-6 closed without baseline, golden, screenshot, or coverage-floor refresh. The 2026-07-18 hostile review at tip 06a17ff31 opened round 7. |
| Current objective | Execute the four round-7 plans in the MASTER-PLAN binding order: scene-controller round 2, DX12 backend ownership decomposition, naming/identity debt, small findings hardening. |
| Active/future progress | 0 / 18 live tasks; 0%. |
| Last broad local gate | Scene round-2 S6 `validate_full` passed in 143.261s: 291/291 tests, 21,455/21,455 assertions, and every coverage floor passed; Automation replay/prediction smoke passed; DX12 reported zero validation errors with committed captures accepted; and physics matched byte-exact. |
| Validation for current edits | S6 project filters passed 728/728; allocation-policy self-test/repository scans passed; `validate_fast` passed after scoped formatting; and the task's sole replay visual-fidelity invocation passed in 435.698s. No baseline or golden refresh. |

## Live Queue

NOW. Adversarial-review round 7 (2026-07-18, tip 06a17ff31): three live plans,
     18 tasks. Scene-controller round 2 is complete at 7/7 and left the live
     ledger under rule 4. `SceneController` has 43 public declarations, its load
     surface has 18 concrete participants, and one `SceneWorld` owns the six
     cohesive scene-lifetime domains. The sixth independent review found zero
     material ownership issues; full and the sole replay mega gate passed with
     no baseline or golden refresh. Closure evidence:
     `Agentic/Reports/2026-07-18/scene-controller-round-2-closure.md`. Remaining
     binding order: DX12 backend ownership D0-D7, naming/identity N0-N4, then
     small-findings H0-H4. Next action: DX12 D0 state census and owner map.

00. Round-6 C0-C6 closed after a repeat independent review, direct
    `Profile-WPO` oracle/performance proof, the broad gate, and bounded DX12
    stress. Closure evidence is in
    `Agentic/Reports/2026-07-18/code-level-red-flags-closure.md`. The
    GPU-hosted CI activation lane remains externally blocked and excluded from
    the active/future denominator.

00000000. `physics-broadphase-scale-attribution` is COMPLETE at 5/5; its plan
          is deleted under inventory rule 4 and closure evidence lives in
          `Agentic/Reports/2026-07-17/broadphase-scale-closure.md`. B0 records
          that Broadphase is the inclusive total and the old nested GridBuild/
          ScalarBounds rows must not be added to it. B1 replaced them with
          mutually exclusive direct child markers and bounded grid counters;
          tests, fast/query, byte-exact physics, performance, and bounded marker
          gates all pass. B2 proved scalar grid insertion grows 30.94x because
          the 2,000-body scene fills all 4,096 hash slots and then averages
          3,660 full-table misses per queried frame; duplicate rejections are
          zero. B3 retains the 4,096-row primary table, adds a fixed 16,384-slot
          lookup and 4,096-row cold overflow tier, and fatals at 8,193 cells.
          Exact-tip paired medians are -1.61% Step / -7.34% Broadphase / -9.78%
          GridInsert at 1,000 bodies and -75.26% / -87.39% / -91.38% at 2,000.
          B4 closed the final matrix, exact provenance, comment audit, and
          independent review. Its evidence selected the one-table S7 cleanup;
          the attribution counters and two-route storage are removed.

0000000. `unit-test-coverage-campaign` is COMPLETE at 10/10 (2026-07-17; plan
         deleted under inventory rule 4; closure evidence lives in
         `Agentic/Reports/2026-07-17/unit-test-coverage-closure.md`). The 2026-07-16 owner
         reorder: unit tests came first, before the S7 decision. The
         tolerance-based behavioral/property suites now protect the retained
         scalar SoA path while rejected SIMD code is deleted.
         U0 shipped OpenCppCoverage 0.9.9.0 and the report-only
         `validate_coverage` lane, measured the per-subsystem baseline,
         ratified the default floors (Tier 1 85%,
         Tier 2 70%, Tier 3 50%, Tier 4 excluded), and the governance ruling
         that quality-gate floors are not the banned migration-debt ratchet.
         U5 (adversarial artifact decode) consumed the campaign's single
         mega-gate invocation successfully. Its provenance-hash-only correction
         was retroactively owner-ratified on 2026-07-17 under the permanent
         config-version reconciliation rule in `AGENTS.md`. Behavioral
         assertions only; fixed seeds; ~60 s
         `validate_tests` budget; floors armed at U9 with a final measured
         report and independent anti-gaming review. U1 raised Maths to 86.41%
         and core primitives to 88.34% through behavioral contracts. U2 mapped
         every reachable Try-API degeneracy to a named call-site test. U3 locked
         store, grid, broadphase, force, and terrain-stage boundaries. U4 locked
         sleep/support/underwater transitions, parallel island ordering,
         friction/restitution bounds, and repeated manifold feature stability.
         U5 locked canonical replay writes, malformed-table rejection, optional
         track absence, and all full-artifact false-pass controls. U6 raised
         startup to 91.89% with CLI, scene/suite, launch-packet, and exact-error
         coverage. U7 added deterministic v1 config evidence and fixed-seed
         momentum, quaternion/matrix, mutual-gravity, prepared-AABB, friction,
         and restitution properties. U8 linked the CPU-only interaction and
         replay-overlay value owners, locked their policy/hit-region/event-ring
         contracts, and raised every Tier-3 subsystem above 50%. U9 armed the
         ratified floors, added positive translation-unit scope
         checks, resolved the independent review, and closed the campaign.
000000. `physics-soa-simd-1000-bodies` is COMPLETE at 9/9. Its live plan is
        deleted under inventory rule 4 and closure evidence lives in
        `Agentic/Reports/2026-07-17/soa-simd-closure.md`. S0 registered the
        campaign, authored fixed-seed 200/520/1,000/2,000-body scale scenes,
        measured the scalar-AoS matrix, confirmed the 2,000-body stretch scene
        does not exhaust fixed capacity, and ratified a 0.80 ms average Physics
        Step budget at 1,000 bodies against the 0.9978 ms reference. S0 passed
        performance, byte-exact physics, full, and the single reference replay
        mega gate without refreshing any baseline or golden. S1 added 20
        fixed-capacity aligned hot component arrays with a bit-exact two-way
        compatibility seam. All arrays are 32-byte aligned in focused coverage;
        normal/deep physics, 204 tests, format, and allocation policy passed.
        S2 migrated all stage/replay/presentation/diagnostics/editor/automation
        consumers to direct hot-field views, made records cold-only, and deleted
        the compatibility seam. Final full, 44,401-line byte-exact physics,
        allocation, and the sole one-process replay mega gate passed with zero
        baseline/golden refresh. S3 diagnosed by-value copies of 20-span hot
        views plus full-row traffic in narrow scalar store kernels, removed
        both without changing arithmetic or stored values, and restored the
        1,000-body Physics average to 0.9795 ms versus S0's 0.9978 ms. The full
        performance gate and 44,401-line byte-exact physics oracle passed. S4
        added the default-OFF v3 config/migration, a dedicated per-file AVX2/FMA
        integration kernel, deterministic masked tails, and a streaming A/B
        oracle. The pilot marker improved 4.5%, aggregate 1,000-tick stability
        stayed inside the explicit 1% outcome envelope, and final full/perf plus
        44,401-line byte-exact OFF gates passed. S5 added dark universal-gravity,
        mutual-pair, and broadphase-bounds kernels. Its 2,000,000-row chaotic
        and 72,000-row mutual-gravity oracles passed without non-finite values;
        pre/post-optimization ON captures were byte-identical. Paired Profile
        evidence was negative, so the S7 cutover budget remains binding. S6
        added narrowphase-prune and solver-row preparation kernels, passed all
        284 doctests with the toggle ON, and measured the complete enabled
        matrix. The 1,000-body step is 1.0666 ms versus the 0.80 ms budget; the
        retained scalar solver core is 0.0092 ms (0.86% of step). The owner
        rejected the SIMD cutover on 2026-07-17, then explicitly directed S7
        to retain SoA, delete SIMD/toggle/counter complexity, simplify the grid,
        and close with full validation. S7 removed 3,190 lines, retained one
        complete 8,192-cell grid, armed mandatory coverage, passed independent
        review, and closed the full gate. S8 published the final verdict,
        reconciled MASTER/SessionState, and deleted the completed plan.
00000. `replay-mass-reduction` completed at 9/9 plus R8 on 2026-07-16. R0 closed the 42-file,
       33,783-line census, D1-D8 duplication dispositions, Release/Profile
       map-attributed baselines (498,264/456,044 bytes), owner ratification,
       and reference gates. R1 proved the fingerprint boundary. R2 moved the
       legacy probes into a Debug-only TU and the RVPD verifier into an
       Automation-only TU; the 1,949-line product restore TU and product codec
       remain in every configuration, while the final Release map has zero
       probe/verifier/fingerprint rows. R3 retained the distinct RVPD/V2 codec
       owners and recorder hashing, formally found schedule-sensitive
       topology/trajectory/reserve bookkeeping in complete artifact bytes, and
       proved the honestly gate-covered projection identical across three
       artifacts. R3-F1 is separately ruled and requires its own artifact-
       compatibility plan; no fix occurred in R3. R4 unified D4 quota and D7
       pose mechanics with no callback/allocation/order change, retained D5's
       policy-distinct loops, and passed tests, DX12, stress, and the one-process
       mega. R5 audited 99 header callables, deleted only two true
       zero-caller accessors, and retained all production, test-seam,
       CLI/config, and supported migration paths under individual rulings.
       R4b closed R3-F1 with a complete writer/reader inventory, dense durable
       tokens, zero reserve telemetry, a content-sensitive canonical semantic
       hash, and a no-version-bump ruling. Both original R3 artifacts and the
       real review-fix encoder are byte-identical after canonicalization at SHA
       `F916DED3...B24`; the R3 gate-covered SHA is unchanged. The first zero
       sentinel was correctly rejected and the
       owner approved one additional same-tip invocation, for three total;
       the final gate passed. R6 then recorded KEEP-cohesive rulings for the
       four oversized/near-threshold TUs; no source or project move was made.
       R7 then closed the final census, exact original-mismatch/final-encoder
       proof, and independent review: product compilation is down one TU and
       2,354 lines, map attribution honestly changes +364/+1,988 bytes, and
       the same reviewer confirmed the reopened R4b semantic/governance fixes
       with no remaining material blocker. R8 passed tests, full, performance,
       allocation policy, and the sole final mega gate. Product compilation is
       -1 TU/-2,354 lines; linked map bytes are honestly +364/+1,988; both
       original mismatches and both final gates are byte exact at
       `F916DED3...B24`. The plan is deleted and no active local replay step
       remains. Binding rules throughout were: exactly one
       `tools\validate_replay_visual_fidelity.bat` invocation (one engine
       process, one prediction generation, zero golden refresh,
       revert-on-diff); link-level diagnostics boundary, never `#ifdef`
       scatter; no artifact-format or probe-schema changes; no ownership
       re-decomposition; deletions only via R5 owner rulings; owner-TU
       partitions only in R6; reserve-allocator registrations move
       unchanged. R4 adds DX12+stress gates; R8
       closes with full/perf/allocation gates and the final census against
       R0's measured numbers.
0000. Runtime mass-reduction campaign completed at 16/16 on 2026-07-16. Binding
      order and closure state:
      `init-startup-decomposition` is complete at 5/5 after independent review
      remediation restored generic CLI policy to its parser owner. Continue with
      `run-member-and-include-shrink` is complete at 6/6 after its clear repeat
      ownership review and full/DX12/stress closure gates. Finally,
      `wide-call-desc-struct-pass` is complete at 5/5: ten record-construction
      names are 0–2 arguments, all 31 surviving ≥12-argument names have current
      individual reasons, and its replay/full/comment/review gates are closed. Owner
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
- Round-6 work completed on `nightrunner-17th-july`. Its plan-mapped replay,
  baseline, coverage, and graphics-stress restrictions remain recorded in the
  closure reports.
- Every plan-runner commit and plan-implementation prompt starts with the
  resolved MASTER progress header: plan name, completed plan tasks, and rounded
  active/future portfolio completion. With no live plans the ledger reports
  0/0 and 100% by convention; completed historical plans and the externally
  blocked validation lane remain excluded. Ordinary commits do not claim plan
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

Round-7 scene decomposition is complete at 7/7 and its plan is deleted under
inventory rule 4. Continue immediately with DX12 backend ownership D0: use the
completed owner census to ratify state, barrier, descriptor, submission,
profiling, and readback ownership before implementation. Preserve the seven
excluded render consumer interfaces and `FRAME_COUNT = 2`. Every DX12 source
change requires the mapped renderer gate and at least the standard bounded
graphics-stress proof. Do not regenerate behavioral, physics, replay, visual,
or screenshot baselines.
