# Source Code Quality Remediation

Date: 2026-09-18
Status: WNF — owner-parked 2026-09-18; 0/6 phases complete. Implementation has not started.
Owner: Scene input validation, Replay artifact loading, and Core diagnostics
Impact: Scene, Replay, tests, and focused maintainability work
Commit name: `SOURCE_QUALITY`

## Owner direction and activation

The owner parked this plan in WNF on 2026-09-18. It is excluded from active
portfolio totals and is not selectable for implementation. Resume only after
explicit owner reactivation, moving the plan back to TODO and updating the
master inventory. The phase order and dependencies below apply after activation.

## Goal and evidence

Turn the five findings in the [source-code quality evaluation](../../Audits/source-code-quality-evaluation-2026-09-18.md)
into bounded repairs: reject invalid scene numbers before publication, reject
oversized replay inputs before allocation, enforce chunk uniqueness for the
right reason, and reduce the knowledge needed to change one application workflow.
Preserve stable identity, transactional publication, worker/GPU lifetimes and
behavioral tests identified as strengths in the evaluation.

The review observed a dirty tree based on c686f653b. This plan was drafted on
main ff65964ba after the combined PR landed. Findings are dated leads, not proof
that every defect remains. SQ0 must establish current behavior before editing.
This request authorizes planning and documentation publication on main, not
implementation, baseline changes or acceptance of any phase.

## Finding ownership and dependencies

| Evaluation finding | Owning phase | Completion evidence |
|---|---|---|
| 1. Scene numeric conversion | SQ1 | Overflow rejected before narrowing; existing scene unchanged |
| 2. Replay input memory | SQ2 | Raw and decoded limits reject before excessive allocation |
| 3. Duplicate chunk IDs | SQ3 | Structurally valid duplicates fail because of uniqueness |
| 4. Application coordination | Governance GV2; acceptance recorded in SQ4 | Comparison and Physics controls have domain-owned transitions |
| 5. Infrastructure and explanatory overhead | SQ4 | Focused API/lifetime review and local explanations at risky operations |

[Governance remediation](governance-review-remediation.md) owns the actual
comparison/Physics-controls extraction. Do not implement a second owner here.
SQ1-SQ3 can proceed after SQ0 without waiting for that extraction. SQ4's
application-ownership acceptance waits for GV2. Replay codec edits must be
coordinated with GV1's Physics-owned compatibility policy; codec budgets and
chunk-table uniqueness stay owned by this plan. Neither plan changes the
[Physics A/B acceptance scope](../TODO/physics-ab-comparison.md).

## Boundaries and decisions

- Scene owns generic finite conversion and field-specific ranges; Physics owns
  physical validity. Check the source numeric value before conversion to float.
  Do not silently clamp invalid authored values or broaden accepted ranges.
- Replay owns file/header/decoded-memory admission. Define supported byte caps
  from legitimate captures and existing budgets during SQ2; document exact
  values and peak overlap before implementation. A guessed universal cap is
  not acceptance. Account for encoded bytes, decoded arrays, scratch, retained
  old state and concurrent comparison loads with checked arithmetic.
- Preserve every currently supported artifact version and unknown-chunk rule.
  Decide singleton versus repeatable chunk semantics explicitly from the format.
  Unknown chunks must not become a way to bypass byte or table budgets.
- No new runtime allocation privilege, exceptions to dependency direction,
  capacity waiver, baseline replacement or broad framework is authorized.
  If a dependency exception proves necessary, record owner, reason and deletion
  condition here before the affected change; currently none are proposed.
- Preserve useful lifetime/capacity comments. Apply the comment standard while
  touching code; do not launch a cosmetic whole-repository comment rewrite.

## Phases

- [ ] SQ0: Reconcile findings against the implementation selected for execution.
- [ ] SQ1: Make scene numeric conversion finite, bounded and transactional.
- [ ] SQ2: Bound replay input and decoded memory before allocation.
- [ ] SQ3: Enforce singleton chunk uniqueness with independent negative tests.
- [ ] SQ4: Verify reduced workflow authority and simplify touched API explanations.
- [ ] SQ5: Complete cumulative validation, independent review and closure.

### SQ0 — Current evidence

Inspect the parser helpers in `SkullbonezSource/Scene/AuthoredSceneParserSchema.h`,
body/runtime parsers, `Runtime/Replay/ReplayV2Artifact.cpp` and their tests.
Resolve these paths beneath `SkullbonezSource` where abbreviated. Use CodeGraph
first when available, confirm current source, and map each finding to the owning
function and focused test. Reproduce failures without publishing invalid state.
Record already-fixed cases with the repair commit and a current regression;
a historical report alone cannot justify a fix or a completed phase.

Acceptance: all five findings have current disposition, owner and evidence;
existing accepted inputs and the caller's state are captured for comparison.

### SQ1 — Scene numeric admission

Implement a checked numeric conversion before float narrowing; handle conversion
failure without escaping the parser's error/result contract. Retain separate
positive-value and interval checks. Cover vector positions, velocities, forces,
Euler/camera values, gravity, fluid and other consumers of the same helpers.
Include positive/negative 1e100, float boundary neighbors, wrong types, array
cardinality and nonfinite values at any programmatic entry that can supply them.
JSON NaN/Infinity text is invalid JSON and cannot alone test numeric overflow.

Acceptance: a finite out-of-float-range JSON number gives a useful field/path
diagnostic; no caller scene replacement or partial object publication occurs.
Valid boundary values follow the documented field policy. A negative control
that removes the finite/range guard makes the new regression fail.

### SQ2 — Replay memory admission

Preflight a fixed-size header and file length before whole-file allocation.
Validate all length/count arithmetic and the combined decoded peak before
resizing containers. Avoid loading the same bytes twice merely to preflight.
For supported large captures, choose bounded reads or streaming explicitly;
reject unsupported sizes with a recoverable diagnostic. Preserve cancellation,
old loaded state and comparison's combined memory accounting.

Acceptance: truncated headers, oversized sparse/mock files, count products near
overflow, and legal-sized files with excessive decoded footprints reject before
the forbidden allocation. Use an allocator/read observer or a narrow injectable
reader seam in tests; do not actually exhaust machine memory. Valid legacy and
current files load. Failed load leaves no partially published recording, and
old scene/recording state remains available under the caller's existing contract.

### SQ3 — Chunk-table uniqueness

Reject duplicate singleton IDs in the table reader before first-match lookup can
hide them. Build fixtures with valid duplicate payloads both before and after
the original, including required and optional singleton chunks. Preserve all
other required entries, offsets and checksums so those checks cannot explain
the rejection. Keep format-supported repeatable/unknown chunks compatible.

Acceptance: structured error or table-reader tests establish uniqueness as the
rejection cause. Removing the uniqueness check makes the valid-duplicate
negative control load or otherwise fail its specific expected diagnostic;
existing malformed-payload tests continue to test their original failures.

### SQ4 — Maintainability acceptance

Consume GV2's comparison-session and Physics-controls owner tests and inspect
the complete affected Run surface. Confirm a workflow change no longer needs
arbitrary access to unrelated owners; forwarding methods or file splitting do
not count. Review touched `SbResult`/diagnostic-store usage for lease lifetime,
copy behavior and fixed capacity. Add a narrow enforcement/test only for a
confirmed misuse or missing contract; retain justified infrastructure.
Keep non-obvious lease, phase, units and capacity explanations near their code;
remove misleading touched preambles and use the shared glossary when needed.

Acceptance: record before/after decision ownership for the chosen workflows,
show the preserved behavior, and explain any unchanged infrastructure with its
concrete lifetime requirement. No line-count target or test-count target applies.

### SQ5 — Terminal closure

Run the cumulative gates below against the final implementation, obtain the
repository's independent terminal review, repair material findings, and record
commands, exit results and artifacts in this plan and commit notes. Reconcile
MASTER-PLAN and SessionState only from completed phase evidence. Delete the
completed checklist under repository policy; Git history retains the plan.

## Validation for later implementation

During iteration use affected-target compilation and focused tests in
`SkullbonezTests/TestSceneParserUnit.cpp`, `TestReplayArtifact.cpp` and the owning
workflow test files. Preserve failure outputs. Heavy gates belong at terminal
closure, with additional mapped gates selected from actual changed paths:

- `tools/validate_scene_parser_tests.bat` and `tools/validate_scene_loads.bat`.
- `tools/validate_replay_v2_artifact.bat` and `tools/validate_replay_visual_fidelity.bat`.
- `tools/validate_dependency_graph.bat` and `tools/validate_replay_allocation_policy.bat`.
- `tools/validate_tests.bat`; compile changed C++ and run the focused
  `python tools/check_source_design.py --repo . --files <changed source paths>`.
- `tools/validate_fast.bat`, plus `tools/validate_physics.bat` if physical defaults,
  state or continuation changes; `tools/validate_perf.bat` if allocation/hot paths change.
- `tools/agent_validate.bat --plan-completion` once at final plan closure.

Use Skarness for native load/restore/settings checks, discover capabilities,
query resulting stable identities/state, retain the stream beneath
`TestOutput/skarness/source-quality/`, and stop the owned session. Add missing
narrow controls under standing authorization. No golden is refreshed to accept
invalid inputs or changed valid-scene behavior. Leave Profile built after source
work and verify an unchanged rebuild before claiming F5 readiness.

Planning validation: documentation only; no build or runtime test is required
or claimed by creation of this plan. All six phases remain unchecked.
