# Narrowphase Manifold And Sleep Coverage - NM4 Sleep State

Date: 2026-08-02
Branch: `nightrunner-2nd-AUG-26`
Baseline: `d26163eddc2c42ce1dcdd6d37f6a63ee4d926416`
Plan progress: 5/6
Portfolio progress: 5/26 (19%)

## Outcome

`TestSleepController.cpp` now directly drives every wake family named by the
NM0 census through the public Physics owner: visual-island fan-out, explicit
point-joint component fan-out, transitive resting-contact traversal, ordinary
single-row explicit wake, automatic same-step point-joint wake, underwater lock
refusal and release, awake-list remove/add/rebuild, and bidirectional point-
joint support propagation.

Support-edge exhaustion is now proved at both boundaries. The existing child
probe crosses the semantic 32,768-row ceiling; a new child reserves only two
rows and proves the third request fails through
`ValidateSleepSupportEdgeCount` before `PhysicsFixedList` can grow or silently
drop the edge.

No production file, sleep policy, Physics behavior, baseline, golden, capacity,
asset, or configuration changed.

## Focused Wake Matrix

| Contract | Direct proof |
|---|---|
| Visual-island wake | Four far-separated sleepers receive visual ids `{7, 7, 9, 0}` through the public replay-state value seam. Waking row 0 wakes exactly rows 0/1; visual-9 row 2 and zero-id row 3 remain asleep. |
| Ordinary explicit wake | Waking zero-id row 3 then changes only row 3. Far separation is a negative control for accidental resting-neighbor fan-out, and the awake list becomes sorted `{0, 1, 3}`. |
| Point-joint component wake | Joints 0-1 and 1-2 wake the complete selected component while disconnected row 3 remains asleep. |
| Resting-contact traversal | Retained edges 0-1 and reversed 2-1 wake row 2 two hops from row 0. Far disconnected row 3 remains asleep, proving traversal is transitive but bounded. |
| Automatic same-step wake | An awake/sleeping joint pair wakes the sleeper, resets its remaining clock to 0.25 seconds, applies exactly one `-12 * 0.25 = -3` gravity velocity step, and publishes sorted awake membership. A repeated pass changes no velocity. |
| Underwater refusal | A fully submerged sleeping sphere locks, zeros its remaining time, stays asleep, and rejects the world-aware explicit wake path. |
| Underwater release | Disabling sleep clears state/lock and the cold mirror republishes hot awake state plus list membership. After re-enable above the fluid, a reseeded nonlocked sleeper follows the ordinary explicit wake path. |
| Awake-list ownership | Out-of-order sleeps remove rows 3 then 1; explicit wakes add 3 then 1 while preserving sorted order. A same-count edit excludes newly fixed row 2 and sleeping row 4 during one cold rebuild, yielding exactly `{0, 1, 3}`. |
| Bidirectional support | Point joints 0-1 and 1-2 publish all four directed edges. Fixed row 0 then propagates support through rows 1 and 2 to a fixed point; the fixed row never enters awake membership. |

NM0 found no one-hop wake policy in current source. Visual and point-joint
wakes cover complete membership; resting-contact wake and support propagation
iterate transitively. NM4 therefore pins those actual bounded fan-out contracts
and deliberately does not manufacture a one-hop expectation.

## Focused Owner Fixture

One process-lifetime test owner reserves eight body/collider/controller/cache
rows once under `RuntimeAllocationPhase::SceneLoad`. Every case clears retained
rows and reconstructs only Physics-owned body, collider, sleep, contact, and
joint values. There is no `PhysicsWorld`, scene, renderer, window, worker pool,
or test-only production backdoor. The controller's public snapshot seam is used
only to give two sleeping rows the same positive diagnostic visual id; the
following cold mirror restores the normal derived awake-list contract before
wake execution.

## Capacity Failure Matrix

| Boundary | Child request | Required result |
|---|---|---|
| Scene-committed reservation | Reserve 2, append rows 1 and 2, request row 3. | Fatal includes `requested=3`, `reserved_capacity=2`, `high_water=2`, and `phase=steady_gameplay`. |
| Semantic ceiling | Reserve 32,768 and request row 32,769. | Fatal includes semantic `capacity=32768`, `high_water=32768`, and steady-gameplay phase. |

Both probes enter through `AppendSleepSupportEdge`, whose first operation is the
shared `ValidateSleepSupportEdgeCount` check. Neither probe relies on a later
container overrun.

## Touched-Source Comment Audit

Audit skill: `Agentic/Skills/comment-style-audit/skill.md`

| File | Result | Evidence |
|---|---|---|
| `SkullbonezTests/TestSleepController.cpp` | Pass | The learning header owns the complete wake/support/list matrix; process-lifetime reservation, visual negative control, transitive resting boundary, one-application force order, cold rebuild, and support symmetry have nearby ownership/invariant comments. |
| `SkullbonezTests/TestRuntimeContracts.cpp` | Pass | The existing fatal-probe learning header now names both sleep support limits, and the reserved-capacity child has a local `Hazard:` comment explaining why semantic-ceiling coverage alone is insufficient. |

Checked: 2/2. Deferred: 0.

Project metadata files `SKULLBONEZ_TESTS.vcxproj` and
`SKULLBONEZ_TESTS.vcxproj.filters` register the new test translation unit and do
not require a source comment audit.

## Validation

| Command | Result |
|---|---|
| Focused Profile build and `Profile\\SKULLBONEZ_TESTS.exe --test-case="Physics sleep controller:*"` | Pass: 7/7 cases, 183/183 assertions. |
| Profile runtime fatal-contract case | Pass: 1/1 case, 246/246 assertions, including both sleep support-edge fatal children. |
| `tools\\validate_tests.bat` | Final-source pass in 52.6 seconds; the new translation unit is registered in 130/130 project/filter items and the complete Profile harness passed. |
| `tools\\validate_coverage.bat` | Final-source pass in 68.6 seconds; Physics stages/solver rises to 5,050/5,760 lines (87.67%) and every subsystem floor passes. |
| `tools\\validate_format.bat` | Pass in 45.1 seconds; 587 source files, 327 headers, and every repository-relative `Related:` path are clean. |
| `git diff --check` | Pass. |

NM4 is an ordinary incremental slice, so no rubber-duck review is appropriate.
The mandatory independent plan-level review remains owned by NM5.
