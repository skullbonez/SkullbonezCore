# Vector Frame Contract Closure

Date: 2026-08-01
Branch: `nightrunner-1st-AUG-26`
Impact area: Physics force integration and public descriptors, Scene authoring,
Maths vector semantics, tests, and closure governance
Status: CLOSED - VF0-VF4 complete

## Outcome

VF0-VF3 make every investigated frame contract honest without moving any
committed Physics, Replay, visual, signature, or query baseline:

- public Physics vectors, orientations, shapes, inertia, joints, queries, and
  hits now state their local, body-principal, body-to-world, or world frame;
- angular drag clamps rotated anisotropic bodies in body-principal axes and
  returns only a changed result to world space;
- authored impulse lever arms are schema-v4
  `impulseWorldOffsetFromCenter` values, with deterministic v1-v3 migration and
  version-gated legacy parsing; and
- `VectorReflect` now reflects across the plane named by a normalized surface
  normal.

The existing complete `at_rest` regression remains authoritative. No redundant
frame assertion was added. Its accepted artifact remains 7,649,427 bytes,
54,001 lines, and SHA-256
`0a46651405e181428aabb5cc5081bd0d90ac6ca73e3a0c2786353f00cf55a984`.

## Corrected Math

The shared angular response is:

```text
world torque/impulse
  -> R^T into body-principal axes
  -> multiply or divide by the body diagonal inertia
  -> R back to world space
```

VF1 applies the no-reversal angular-drag bound in the same body-principal frame:

```text
limit_axis = abs(omega_body_axis) * inertia_body_axis / dt
clamped_body_axis = clamp(torque_body_axis, -limit_axis, +limit_axis)
```

The no-clamp path returns the original world torque exactly. Isotropic bodies
retain the historical component order and arithmetic, and `ApplyWorldImpulse`
still owns the single response conversion, so the correction does not
double-transform torque.

`VectorReflect` changed from normal-axis mirroring to conventional
surface-plane reflection:

```text
incident - normal * (2 * dot(normal, incident))
```

That preserves tangent components and reverses the normal projection. The
caller continues to own normal normalization.

## Authored Offset Contract

All 56 authored impulse offsets across 23 schema-v4 scenes use the explicit
center-relative world-space name. `ragdoll_playground::wake_ball` changed from
its mistaken absolute position `(515, 28, 492)` to the correct center-applied
offset `(0, 0, 0)`. A production `PhysicsEngine` handoff test proves that the
parsed `(0, 0, 120)` world impulse and zero world-center offset reach the
pending body store.

The current parser rejects the retired spelling in v4 while v1-v3 readers and
the cold migration retain the historical input contract. A non-blocking review
note remains: calling `migrate_scene_text()` directly on an already-v4 document
with the retired key returns it unchanged. Runtime/parser tests reject that
document and the committed scene-parser gate catches it, so no invalid tracked
scene is admitted; a future cold-tool hardening change may make the direct
helper fail the same malformed current stamp.

## Artifact And Baseline Result

VF0 predicted zero committed artifact movement: mapped physics scenes do not
activate the corrected anisotropic clamp, the sole absolute-offset outlier is
outside the mapped artifact set, and `VectorReflect` has no production caller.
VF1-VF3 confirm that prediction completely. Core Physics reproduces both
44,401-line varied runs byte-for-byte, and deep Physics reports every CSV,
known signature, target-reaction, and SkullScope query artifact exact.

Physics does **not** need a new baseline. No baseline, golden, signature,
manifest, scene screenshot, or performance comparison was approved or
refreshed.

## Focused And Repository Validation

| Command or proof | Result |
|---|---|
| Final Profile frame/drag, parser/handoff, snapshot-writer, and reflection selections | PASS; 9 cases / 653 assertions |
| `tools\validate_tests.bat` | PASS; 460 cases / 2,423,070 assertions |
| `tools\validate_physics.bat` | PASS in 27.4 s; two 44,401-line varied runs byte-exact |
| `tools\validate_physics_deep.bat` | PASS in 110.5 s; every mapped artifact exact |
| Replay visual single-generation proof | The authoritative 4,200-frame run completed once; the committed checker stopped only at the accepted historical CRLF/LF scene-hash provenance mismatch |
| Accepted Replay offline proof | PASS in 32.8 s against that existing generation; positive fidelity plus every visual, causal, semantic, artifact, prediction, and ten determinism mutation controls pass after changing only the two ignored provenance-copy fields |
| `tools\validate_perf.bat` | PASS in 119.6 s on the final unchanged source; absolute budgets, allocation, selected-ball structure, scale lanes, DX12, and Physics comparisons pass without refresh |
| `tools\validate_fast.bat` | PASS in 359.8 s on the post-retirement tree; all nine stages and 460 cases / 2,423,070 assertions pass |
| `tools\validate_full.bat` | PASS in 560.3 s; 460 cases / 2,423,070 assertions, Automation smoke, matching DX12 screenshots with zero validation errors, and byte-exact Physics |

The committed Replay manifest still records the scene's historical CRLF hash
`5e066982ea3830c06631e03d48c0a57600f93779f4e741da9ad37c283e899086`,
while the tracked LF scene hashes to
`3b970ccebc040e6350ec92185b9d5b6ea830de7fbcd06aced793a6b59c66ea10`.
Ignored diagnostic copies changed only that visual provenance field and the
causal manifest's dependent visual-manifest hash. No second engine process or
prediction generation ran, and no committed manifest changed.

The initial performance comparisons did not justify a code or baseline change.
Their blocking rows were near-threshold `Frame.avg` values, while unrelated
Input, Physics, GPU skybox, GPU terrain, and Vsync markers slowed together.
Absolute budgets, allocation, selected-ball structural proof, scale
measurements, and memory remained healthy. An uncommitted exact-zero drag
fast-path probe left the Physics marker unchanged and was removed, returning
the source exactly to the independently reviewed VF3 tip. The final clean run
passed both mapped comparisons; `physics_bench` `Frame.avg` was 0.4804 ms,
14.7% above its 0.4189 ms baseline and inside the 15% threshold. No performance
baseline changed.

## Strict Ownership Inventories

| Inventory | Current result |
|---|---|
| Dependency and allocation policy | Generated proof and repository scan pass with zero findings; allocation allowlist has zero errors |
| Authority-free aggregates | 1,183 candidates; 85/85 gated rows ruled; zero unruled |
| Extraction scars | One unrelated ruled `WorkerPool` row; zero unruled |
| Wide signatures | Every operation at or above 12 parameters has an exact current ruling; touched `MakePhysicsBodyCreateDesc` remains at 11 |
| Function complexity | 6,240 functions; 40/40 triggered bodies ruled; both VF2 edited-body digests are current |
| Build configuration | 1,657 compile rows; 316 source files; 62 shared; 124 divergent pairs ruled; zero dropped inheritance or blockers |
| Symbol reachability | 79 rows: 48 no-reference, 10 own-TU-only, 21 test-only; zero blockers |
| Glossary | 576 files; 965 definitions; 965 unique; zero multi-file terms, drift, rulings, or issues |

No `PhysicsBodyRecord` or hot-store field, Replay growth registration,
allocation allowlist, Runtime Replay/Prediction/Planning source, or TestOutput
baseline changed.

## Comment Audit

Checklist paths: the VF0-VF3 phase reports linked below.

The aggregate source-bearing inventory from `81a8a07a..be37ce09` contains 18
unique files. All 18 appear in a completed phase checklist and were reconciled
against the final touched-source inventory. Checked: 18. Deferred: 0.
Unchecked: none.

- `vector-frame-contract-closure-vf0-frame-matrix.md`
- `vector-frame-contract-closure-vf1-angular-drag.md`
- `vector-frame-contract-closure-vf2-authored-impulse-offset.md`
- `vector-frame-contract-closure-vf3-vector-reflect.md`

## Independent Review

The independent read-only closure review finds the frame math, schema boundary,
artifact argument, ownership inventories, and 18/18 comment audit coherent.
Verdict: **ACCEPT - VF4 is complete at 5/5 with no blocking findings or missing
evidence.** The report, live ledger, completed-plan retirement, and final
documentation fast gate are settled. Gate Blind Spot is closed at 21/21 and
the active/future ledger is empty.

One non-blocking direct migration-helper hardening note is recorded above. It
does not weaken the current parser or committed-data gate and does not authorize
scope expansion during documentation-only VF4 closure.

## Commit Record

VF0-VF3 landed as `2676f34a`, `c47f62fb`, `98d3c08a`, and `be37ce09`.
Those commits used the required progress subjects but omitted substantive
bodies. The final VF4 commit body is the non-destructive cumulative handoff: it
must name every phase change and reason, exact validation and baseline result,
Replay provenance handling, report/session/MASTER updates, and completed-plan
retirement. History will not be rewritten.

## Plan Retirement

The Gate Blind Spot Campaign completed 21/21 phases. Inventory rule 4 removes
all five completed TODO plans and the completed glossary companion checklist
from the live tree, then replaces their MASTER and SessionState links with
permanent closure reports. The deleted planning files are recoverable from git
history; no implementation evidence was discarded.
