# Vector Dot-Product API Closure

Date: 2026-07-28
Branch: `nightrunner-28th-JUL-26`
Plan: `Agentic/Plans/DONE/vector-dot-product-api.md`
Required commit subject first line:
`VECTOR-DOT-PRODUCT-API, TASK 3 / 3, 0% OVERALL COMPLETE — close named dot-product contract`

## Outcome

`Math::Vector::Dot` is the only first-party vector dot-product contract. Its
body retains the exact established arithmetic spelling:

```cpp
return lhs.x * rhs.x + lhs.y * rhs.y + lhs.z * rhs.z;
```

The vector-vector multiplication overload, OrbitalMechanics-local adapter, and
all compatibility spellings are deleted. Scalar vector multiplication,
component-wise `VectorMultiply`, and the unrelated
`RotationMatrix::operator*( Vector3 )` transform remain distinct contracts.

VD2 adds a permanent mixed-sign cancellation witness. With
`lhs = (1e10, 1e10, 3.25)` and `rhs = (1e10, -1e10, 1)`, the established
`(x + y) + z` evaluation produces exactly `3.25f`; reassociation to
`x + (y + z)` loses the z term at the y-product magnitude and produces zero.
The test checks both operand orders.

## Configuration-Complete Census

The VD0 Clang census was complete for Profile preprocessing but omitted two
`_DEBUG`-only expressions. Deleting the overload and compiling the complete
Debug solution exposed both:

| Debug-only row | Final spelling |
|---|---|
| `SkullbonezSource/Physics/Diagnostics/SkullScope.cpp:690` | `Dot( relVel, c.normal )` |
| `SkullbonezSource/Runtime/Editor/LauncherTools.cpp:150` | `Dot( toModel, rayDir )` |

The explicit dated correction in the VD0 and VD1 reports preserves the
historical 171-row Profile evidence while superseding its completeness claim.
The final reconciliation is:

| Evidence | Count |
|---|---:|
| VD0 Profile-preprocessed ambiguous uses | 171 |
| Additional Debug-only ambiguous uses | 2 |
| **Configuration-complete ambiguous uses** | **173** |
| Final named call-site replacements | 172 |
| Adapter-body use deleted with the adapter | 1 |
| **Reconciled original uses** | **173** |

The final tracked-source scan reports 180 `Dot(` occurrences: one shared inline
definition and 179 calls. Those calls reconcile as 172 migrations, five
pre-existing OrbitalMechanics named calls that now bind the shared owner, and
two VD2 test witnesses.

| Area | Final shared calls |
|---|---:|
| Gameplay | 1 |
| Maths | 14 |
| Physics | 97 |
| Runtime | 57 |
| Tests | 10 |
| **Total** | **179** |

The deletion proof finds no vector-vector `Vector3` overload, local `Dot`
helper, macro, type/function alias, explicit member invocation, forwarding
wrapper, or compatibility surface. Full Profile and Debug builds supply the
type-aware proof: a residual `Vector3 * Vector3` cannot compile after overload
deletion.

## Validation

All gates ran from detached worktree
`C:\SkullbonezCore-vd2-validation` containing only the final four task
source/test files. The two pinned submodules were initialized at their committed
revisions. Replay visual validation used the already approved local
`SkullbonezData/engine.cfg` bytes with SHA-256
`541816eec32f361ccfeb1ad9b6719f8db0d70cd75d1f10b10db253f577bac83d`;
the clean checkout's committed config was not edited in the branch. No
baseline, golden, config, schema, or performance artifact was refreshed.

| Command / proof | Time | Result |
|---|---:|---|
| `tools\validate_format.bat` | 44.35 s | PASS; 571 implementations and 317 headers clean |
| `tools\validate_dependency_graph.bat` | 3.19 s | PASS; 27 include rules, 46 negative fixtures, zero findings |
| `tools\validate_project_filters.bat` | 2.40 s | PASS; 787/787 production project/filter items |
| Aggregate ownership inventory | 23.80 s | PASS; 1,174 candidates, 85/85 gated rows ruled |
| Extraction-scar inventory | 27.90 s | PASS; 1/1 finding ruled |
| Wide-signature inventory | 27.91 s | PASS; every 12-or-more trigger row has a current ruling |
| `tools\validate_tests.bat` | 35.0 s | PASS; complete Profile build and test gate |
| Complete Profile doctests | 14.47 s | PASS; 437/437 cases, 2,419,129/2,419,129 assertions |
| Focused cancellation witness | 0.02 s | PASS; 1/1 case, 2/2 exact assertions |
| `tools\validate_physics.bat` | 24.0 s final rerun | PASS; 44,401-line CSV byte-exact |
| `tools\validate_physics_deep.bat` | 106.32 s | PASS; core/bullet/shooting/chaos CSVs, known signatures, and `physics_query_varied.json` exact |
| `tools\validate_perf.bat` | 89.07 s | PASS; allocation, DX12, and Physics comparisons report no regression |
| `tools\validate_replay_visual_fidelity.bat` | 394.67 s | PASS; one process/generation/presentation, 2,401 ticks, all positive/negative controls |
| `tools\validate_full.bat` | 336.33 s | PASS; 437 tests, coverage/CPU lanes, zero DX12 errors, accepted images, byte-exact Physics |

The validation logs were captured under
`TestOutput/validation/vector_dot_vd2` in the isolated worktree. Their sizes
and SHA-256 digests make the evidence quoted above auditable without committing
generated output:

| Log | Bytes | SHA-256 |
|---|---:|---|
| `aggregate.log` | 6,580 | `5046da83588d28d637f9e9d4c1020f00c58006ac8a28aef675eb2b36801f62e8` |
| `scar.log` | 400 | `10f0487e4b0dea219793d98b52e5bd01fcc2af58b2dcc3f5413fad2e9cb53d46` |
| `wide.log` | 414,138 | `019e70158638b8a37552b14b9fee96f8f7a28d8461d3c191f5cc1695bb26e49b` |
| `unit_tests.log` | 2,973,064 | `445d61cf420785accead22c0170a69c555167a2ad163b7a8a362cde54aebbd9d` |
| `validate_physics.log` | 575,418 | `758c4a93426e6d7cf6e79d60832c890435546eb5ee3c3e9a1cca77238a9c9267` |
| `validate_physics_deep.log` | 1,166,710 | `d045024fa79f5800e88e4da38ba0fccdc54023c10814d5e57d61354a2e339861` |
| `validate_perf.log` | 3,345,620 | `ef3272798db17c682bba5d50c516bdfaabbd99a504b4a31c41f06cfecdc174fb` |
| `validate_replay_visual_fidelity.log` | 26,003 | `551ab7c5daa0f5c695a02a670b43cf80051998d885f50ee8df9c0ac7ef3b494c` |
| `validate_full.log` | 1,966,548 | `bce89a05522323af07b4880301bfc4d5fcc433f77241f063750cb75fc477d0a7` |

## Comment Audit

Checklist/evidence path: this closure report.

- Checked: 36/36 source-bearing census files.
- Deferred: 0.
- Unchecked: none.
- The 34 VD1 files retain the file learning headers and local invariant/hazard
  comments audited in the VD1 report.
- `SkullScope.cpp` and `LauncherTools.cpp` each retain complete File, Purpose,
  Summary, Glossary, Invariants, and Related sections. Their two spelling-only
  replacements move no owner, lifetime, phase, or sequencing responsibility.
- `Vector3.h` keeps its adjacent byte-exact arithmetic invariant, and
  `TestVector3.cpp` explains why the cancellation witness detects reassociation.
- A final `git ls-files` reconciliation accounts for all 36 files; zero are
  silently skipped.

## Independent Review

The initial end-of-plan review found one missing validation mapping:
Replay-source edits in VD1 require the full visual-fidelity oracle. That gate was
added and passed. The follow-up review found the historical 171-row evidence
stale after Debug compilation exposed two conditional rows, required explicit
dated corrections, and mapped `SkullScope.cpp` to deep Physics validation.
Both findings were completed before closure.

All five ownership questions are clear: no aggregate, capability slice, or
extraction scar was added; no deleted shape reappeared under a new name; and
the final source comments match the implemented owner and arithmetic behavior.
No signature changed, so every current wide-signature ruling remains valid.

| Duck run | Reviewer | Reason | Prompt chars | Response chars | Tokens | Verdict | Follow-up |
|---|---|---|---:|---:|---|---|---|
| `vector-dot-product-api-duck-01` | `/root/vector_vd2_worker/vector_vd2_duck` | Initial VD2 closure review | n/a | n/a | n/a | One missing Replay visual gate | Gate added and passed |
| `vector-dot-product-api-duck-02` | same | Review after Debug-only repairs | n/a | n/a | n/a | Stale historical evidence and missing deep Physics gate | Corrections and gate completed |

The collaboration tool did not expose prompt/response character or token
accounting, so those fields are recorded as unavailable rather than estimated.

## Protected Warm-Start State

The user-owned warm-start experiment remains unstaged and uncommitted. VD2 did
not edit, format, stage, or validate those bytes:

| File | Working-tree SHA-256 |
|---|---|
| `SkullbonezSource/Physics/PersistentContactSolver.cpp` | `c5fb32e50ba05702a9dc24108938a87c81374d1d3e6f77a6491317032711adcd` |
| `SkullbonezSource/Physics/PersistentContactSolver.h` | `3bc67bfa277bf6be8a0e2086563d686d4e1a4f8d75a82ced39f039d02422ac59` |
| `SkullbonezSource/Physics/Stages/PhysicsNarrowphaseStage.cpp` | `b23e20c2e54e6868d101b67239d5c9d1a6ad86ff5d45f6404acd625d462cbb7f` |

## Residual Risk

The arithmetic API and configuration-complete compiler proof leave no known
functional residual. The only operational residual is that future
configuration-specific source censuses must include both Profile and Debug
preprocessing; a single Profile compilation database is insufficient for code
inside `_DEBUG`.
