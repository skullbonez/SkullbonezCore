# Replay Prediction Adversarial Repair

Date: 2026-08-16
Status: Active - 1/2 tasks complete
Impact area: Runtime/Prediction publication and retained storage,
Runtime/Automation readers and fidelity capture, focused CPU tests, validation
tooling, and governance documentation
Owner: Replay Prediction owns coherent publication and retained-capacity reuse;
Automation consumes only committed value views
Priority: Close the post-merge adversarial findings before unrelated plan work

## Owner Direction

The owner ratified all three repair decisions on 2026-08-16:

1. Correct all five Automation readers of retained prediction frames, including
   completion/counting gates, so every external consumer observes only the
   committed prefix.
2. Record the work in this standalone adversarial-repair plan instead of
   reopening RP5 or relying only on commit prose.
3. Land two buildable commits: gate/reader/harness safety first, then the
   Prediction algorithms, retained-memory ruling, and production-path tests.

No visual, replay, physics, or fingerprint baseline may be refreshed. The
pre-merge comparison commit is `306b040`; implementation branches from current
`main` at `98404cbf5` on `codex/replay-prediction-adversarial-fixes`.

## Findings And Required Repairs

The adversarial review found six closure defects in the merged replay-prediction
spike-reduction work:

1. Automation readers used the retained `simulation.frames` storage extent as
   published truth instead of the committed prefix.
2. The RP2 equivalence fixture did not force budget expiry often enough to
   prove exact interrupted resume behavior.
3. The retained-memory policy used an estimate rather than high water measured
   from a real two-generation Automation run.
4. Dormant trajectory reuse selected an arbitrary sufficient slot instead of
   same-key reuse followed by smallest-sufficient best fit.
5. Committed child-frame publication could append an entire node after one
   outer budget check and had no exact resume cursor inside that work.
6. Child-marker state could survive an incoherent flip because the key omitted
   buffer completeness and the flip did not reject incomplete marker state.

Narrow review notes also require semantic allocation-report validation, sorted
marker-order documentation, removal of a dead source-change predicate, and a
correct one-way visual-capture closure latch. These are repair support, not new
product scope.

## Pre-Merge Attribution

Both requested gates were run in an isolated worktree at `306b040` before
editing current `main`:

| Gate | Exit | Authoritative result |
|---|---:|---|
| `tools\validate_replay_visual_fidelity.bat` | 1 | Engine report captured 2,401 ticks, then the existing harness reported `replay visual fidelity probe entered a second live playback pass`. The suspected harness failure predates the merged branch. |
| `tools\validate_replay_allocation_policy.bat` | 1 | The wrapper rejected an obsolete 180/181-frame spelling, while its engine report was semantically successful: `ok=true`, 240 frames, two generations. The policy run itself predates the wrapper failure. |

Correcting the visual harness exposed a separate merged behavioral regression:
the first replacement bank retired already-presented build child records before
the coherent replacement was ready. That immutable-publication defect belongs
to AR1 and is fixed without changing the established packet hashes.

## Task AR0 - Repair Gate Readers And Evidence Harnesses

- [x] Route all five raw Automation readers through `CommittedFrames()` or
  `CommittedFrameCount()`, including report facts, content scans, fidelity
  capture bounds, and completion predicates.
- [x] Make the allocation wrapper validate the semantic contract: at least 180
  frames, the successful frame-180 path assertion, and exactly two completed
  prediction generations.
- [x] Give visual capture a one-way completion latch, use the authoritative
  pause meaning of `liveAdvanceHeld`, and clear prior report/log/artifact files
  before launch so stale evidence cannot pass the gate.
- [x] Update the two exact function-complexity rulings invalidated by the
  Automation source edits after qualitative owner review.

AR0 commit subject:

```text
REPLAY_ADVERSARIAL, TASK 1/2 — repair gates and readers
```

## Task AR1 - Repair Resume, Reuse, And Coherent Publication

- [ ] Select dormant trajectory slots by same key, then smallest sufficient
  capacity, then largest-capacity fallback; preserve the active prefix order.
- [ ] Add Prediction-owned child-append target and node cursors. Check the
  presentation budget between whole-node appends and resume without duplicating
  or skipping records.
- [ ] Bind marker scans to buffer completeness and refuse coherent publication
  until marker state matches the complete promoted source.
- [ ] Preserve already-presented build child records through the first coherent
  replacement flip; retire them only when the next build bank becomes the
  replacement source.
- [ ] Force repeated RP2 budget interruptions, require partial marker and child
  append progress, and compare every final marker/trajectory/publication fact
  with the uninterrupted production path.
- [ ] Re-measure strict two-generation retained high water from a real
  Automation launch and align the capacity ruling and allocation allowlist with
  the measured retained value.
- [ ] Run the complete validation map with no baseline refresh, audit every
  touched source-bearing file, close the plan, and remove this completed file
  from the live queue in the same commit.

AR1 commit subject:

```text
REPLAY_ADVERSARIAL, TASK 2/2 — repair publication resumes
```

## Memory Ruling

The real two-generation Automation measurement reports:

- registered owner: `replay_prediction_working_set`;
- retained `CapacityBytes()` high water: 18,701,760 bytes;
- transient allocator high water: 26,615,942 bytes;
- hard cap: 268,435,456 bytes;
- reserve growths: 339; failed growths: 0.

The source constant and policy allowlist record 18,701,760 bytes because that is
the retained owner capacity the contract names. The allocator diagnostic stays
separate; neither value creates or expands a growth privilege.

## Validation Map And Accepted Evidence

| Requirement | Evidence |
|---|---|
| Focused behavior | Replay CPU family: 87/87 cases and 49,256/49,256 assertions |
| Immutable visual output | `tools\validate_replay_visual_fidelity.bat`: PASS, 2,401 ticks, 200 nodes, one presented cascade, saved/load proof and negative controls |
| Allocation policy | `tools\validate_replay_allocation_policy.bat`: `PASS: strict two-generation replay allocation policy is clean.` |
| Dependency direction | `python tools\check_dependency_graph.py --repo .`: 27 include rules, one content rule, one project rule, zero findings |
| Repository fast gate | `tools\validate_fast.bat`: exit 0 |
| Repository full gate | `tools\validate_full.bat`: `VALIDATE_FULL: DEFAULT GATE PASSED` |
| Spike diagnostic | `tools\validate_replay_prediction_frame_spikes.bat`: exit 0; 3,671 frames, four generations, p99 15.5885 ms, p99.9 16.2162 ms; excluded report serialization owns the 118.7348 ms maximum |
| Comment quality | 15/15 touched source-bearing files inspected, zero deferred; glossary and related-path checks pass |

The spike diagnostic remains informational. This repair neither answers nor
changes the original spike-reduction plan's owner-only RP4 threshold task.

## Closure Invariants

- External readers never infer publication from retained storage capacity.
- Budget expiry can occur at every expensive causal-node boundary and resumes
  from the exact next node.
- A coherent flip exposes topology, trajectory records, and markers from one
  complete key only.
- Slot reuse is deterministic and minimizes unnecessary retained growth without
  reordering active records.
- Real-run retained memory remains within the existing registered cap with no
  new privilege.
- Pre-existing visual packet fingerprints and replay physics remain unchanged.
