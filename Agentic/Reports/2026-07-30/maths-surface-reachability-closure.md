# Maths Surface Reachability Closure

Date: 2026-07-30
Branch: `nightrunner-30th-JUL-26`
Plan: `maths-surface-reachability`
Result: COMPLETE — MR0-MR3, 4/4

## Outcome

`GeometricMath` now exposes only the two operations with production callers:
`ComputePlane(Triangle)` and `CalculateIntersectionTime(Plane, Ray)`.
Eight externally declared definitions were removed:

- `DeterminePointDistFromPlane`;
- `ClassifyPointAgainstPlane`;
- `GetHeightFromPlane`;
- `CalculateIntersectionTime(Triangle, Ray)`;
- both `ComputeIntersectionPoint` overloads;
- `IsPointInsideTriangle`;
- `ComputeBarycentricCoordinates`.

The surviving triangle-normal algorithm is an anonymous-namespace helper.
Tests no longer manufacture reachability for the retired height, triangle-ray,
or intersection-point APIs. No alias, forwarding declaration, or commented-out
body remains.

## MR0 — Configuration-Complete Census

The dated census is
`Agentic/Reports/2026-07-30/maths-surface-reachability-mr0-census.md`.
Debug/Profile decorated-symbol evidence and history established that only
`ComputePlane(Triangle)` and `CalculateIntersectionTime(Plane, Ray)` have live
production callers. Terrain height use was deliberately replaced by cached
terrain interpolation, and the legacy collision-response intersection-point
path was removed. The broader dead component was therefore deleted rather than
hardened.

## MR1 — Surface Deletion

`GeometricMath.h`, `GeometricMath.cpp`, and `TestGeometricMath.cpp` were reduced
to the production-reachable plane and ray-time contracts. The retained behavior
is unchanged:

- degenerate triangles deterministically produce a zero-normal plane;
- a ray query diagnoses a zero plane normal in Debug and follows the miss path
  in Release;
- intersection time is signed, so a negative finite parameter remains valid.

Focused Profile coverage passes 4/4 cases and 17/17 assertions. The final broad
gates below prove no behavior or Physics artifact moved.

## MR2 — Reachability Governance

`tools/inventory_unreachable_symbols.py` now inventories ordinary out-of-line
first-party `.cpp` function definitions with matching header declarations. Its
scope deliberately excludes constructors, destructors, operators,
inline/header definitions, and internal-linkage helpers; those remain
review-owned.

The inventory joins source definitions to current Debug and Profile decorated
COFF identities, uses header declarations for default-argument arity, supplies
same-TU source edges, reads `SKULLBONEZ_TESTS` references from objects, and adds
masked lexical edges from standalone `Agentic/Tests`. It rejects:

- a missing configuration root;
- duplicate roots pretending to be two configurations;
- a root not named `Debug` or `Profile`;
- current-source objects older than their `.cpp`;
- unruled, malformed, or stale rulings;
- a repair ruling whose plan does not exist.

The executable self-test compiles real MSVC COFF fixtures and covers a
Debug-only caller, test-only callers from both test families, a default-argument
same-TU call, overload arity, internal linkage, missing/duplicate roots, stale
objects, an unruled symbol, and a moved-symbol stale ruling.

`validate_fast` owns the self-test and strict repository scan. Direct calls
leave current Profile/Debug builds; parents that suppress ready builds must
prove Debug current. `validate_full` now builds Debug before preflight, and
`validate_select fast` does the same before delegating.

## MR3 — Exact Current Judgements

The corrected final census is:

| Classification | Rows |
|---|---:|
| No reference | 299 |
| Test-only | 60 |
| Own-TU-only | 41 |
| Own-TU-and-test-only | 7 |
| **Total** | **407** |

| Compiler mapping | Rows |
|---|---:|
| Exact | 320 |
| Ambiguous | 72 |
| Missing | 15 |
| **Total** | **407** |

At this closure point all 407 rows had exact `repair-plan` rulings and strict
mode reported zero diagnostics; none was retained by allowance. The completed
four-phase row-by-row adjudication is recorded in
`Agentic/Reports/2026-07-30/unreachable-symbol-remediation-closure.md`.

## Independent Review

The same independent rubber-duck reviewer followed the complete diff across
three passes:

1. Duck 01 found false Maths comments, missing default-argument/COFF self-test
   coverage, over-broad governance prose, unsafe nested-gate object freshness,
   and an unregistered repair plan.
2. Duck 02 verified those corrections and found standalone `Agentic/Tests`
   evidence was dropped, duplicate roots were accepted, and two MASTER
   summaries still reported 0/10.
3. Duck 03 verified the regenerated 407-row census, distinct-root guard,
   Agentic test evidence, 0/14 ledger, batch ordering, comments, all ownership
   questions, and the complete deletion boundary. Verdict: **NO BLOCKER**.

Prompt/response character counts, token counts, and per-pass elapsed times are
unavailable because the collaboration API does not expose exact retained
message accounting. No values were estimated.

## Comment Audit

The comment-style audit is 7/7 with zero deferred files:

1. `SkullbonezSource/Maths/GeometricMath.cpp`;
2. `SkullbonezSource/Maths/GeometricMath.h`;
3. `SkullbonezTests/TestGeometricMath.cpp`;
4. `tools/inventory_unreachable_symbols.py`;
5. `tools/validate_fast.bat`;
6. `tools/validate_full.bat`;
7. `tools/validate_select.bat`.

Learning headers and nearby comments describe the post-change behavior,
inventory scope, evidence ownership, and validation ordering without stale
claims. All repository-relative `Related:` paths resolve.

## Final Validation

| Gate | Result |
|---|---|
| `tools\validate_fast.bat` | PASS in 285.3 s; 465/465 cases, 2,423,881 assertions, zero build warnings/errors, current Debug/Profile objects, strict reachability |
| `python tools\inventory_unreachable_symbols.py --self-test` | PASS |
| direct strict repository reachability | PASS in the combined 79.1 s direct run; 407/407 ruled, zero diagnostics |
| `tools\validate_coverage.bat` | PASS in 43.6 s; Maths 834/912 lines, 91.45% against unchanged 85% floor; all ten floors pass |
| `tools\validate_full.bat` | PASS in 502.6 s; all CPU lanes, Automation, DX12, and Physics |
| Physics regression | PASS; 44,401 lines byte-exact, two output runs against one baseline |

No baseline, coverage floor, golden, schema, scene, configuration, or runtime
artifact was changed.
