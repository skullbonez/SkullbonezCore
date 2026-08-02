# Narrowphase Manifold And Sleep Coverage - NM2 Identity

Date: 2026-08-02
Branch: `nightrunner-2nd-AUG-26`
Baseline: `d26163eddc2c42ce1dcdd6d37f6a63ee4d926416`
Plan progress: 3/6
Portfolio progress: 3/26 (12%)

## Outcome

Object-manifold feature identity is now pinned across its complete lifetime:
the narrowphase produces stable rows for sub-slop resting contacts, crosses the
45-degree incident-face boundary once, reduces clipped candidates independently
of insertion order, and exposes a changed feature as a real persistent-solver
cache miss. The production reducer itself is exercised, including its deepest,
feature-id tie, tangent-spread, and invalid-input branches.

No contact geometry, solver policy, baseline, golden, capacity, shape asset, or
allocation privilege changed. The only production surface change gives the
existing allocation-free reducer an honest Physics-owned value seam so tests
and production call the same algorithm.

## Identity Matrix

| Contract | Configuration-derived proof |
|---|---|
| Resting box / box | Unit face contacts at Y separations 1.50000 and 1.50001 retain the same ordered point count and feature ids. The 0.00001 movement is one hundredth of the 0.001 contact skin. |
| Resting hull / hull | Authored brick-hull face contacts at Y separations 1.40000 and 1.40001 retain the same ordered point count and feature ids under the same sub-slop movement. |
| 45-degree face boundary | Forty-one authored poses sweep yaw from 40 through 50 degrees in quarter-degree steps. Body A's +X face remains the reference; body B changes from -X to -Z exactly once, with either tied face permitted only at exactly 45 degrees. |
| Deepest-first reduction | A shallower feature 1 precedes a deeper feature 90 in input, while the result selects feature 90 first. |
| Feature-id penetration tie | Equal-penetration candidates feature 41 and feature 7 select feature 7 first, proving the stable identity tie-break rather than insertion order. |
| Tangent spread | Six explicit candidates force the reducer to keep the deepest center and maximize minimum tangent-plane distance. A deliberately nearby higher-penetration point is omitted from the four-row patch. |
| Insertion-order independence | All 720 permutations of the six-candidate fixture return ordered feature ids `{100, 20, 30, 40}`. |
| Warm-start consequence | A radius-0.5 sphere contacting a unit box's +X then +Y face keeps body ids fixed while changing only narrowphase feature identity. A stored nonzero +X normal impulse hits; the +Y lookup misses. |

## Production Reducer Seam

`ObjectContactCandidate`, `ObjectContactCandidateSelection`, and
`SelectObjectContactCandidateIndices` live at the Physics object-manifold
boundary. Box and polytope face clipping now call that exact public function;
there is no test-only wrapper, alternate implementation, retained pointer, or
owner reach-back. The selection carries at most four indices into a synchronous
caller-owned array, and a fixed 32-bit selection map covers the existing 8-row
box and 32-row polytope buffers without allocation.

Null, non-positive, and over-capacity borrows return an empty selection before
the candidate pointer is read. Focused coverage pins the null/non-positive and
over-capacity cases. Production candidate counts remain bounded by their fixed
buffers, so the guard documents and enforces the existing hot-path invariant.

## Touched-Source Comment Audit

Audit skill: `Agentic/Skills/comment-style-audit/skill.md`

| File | Result | Evidence |
|---|---|---|
| `SkullbonezSource/Physics/ObjectContactManifold.h` | Pass | The learning header names the narrowphase value flow and 32-to-4 reduction invariant; candidate and selection values document geometry/identity coupling and synchronous borrowed lifetime. |
| `SkullbonezSource/Physics/ObjectContactManifold.cpp` | Pass | Existing ownership and deterministic-output header remains accurate; the reducer carries local `Invariant:` and `Hazard:` comments for deepest/spread policy, fixed capacity, and invalid borrows. |
| `SkullbonezTests/TestObjectContactManifold.cpp` | Pass | The learning header adds identity lifetime, insertion-order, and cache coupling; sub-slop, spread, and cache fixtures explain why their authored values distinguish the required behavior. |

Checked: 3/3. Deferred: 0.

## Validation

| Command | Result |
|---|---|
| Focused Profile build and `Profile\\SKULLBONEZ_TESTS.exe --test-case="Object contact manifold identity:*,Object contact manifold reduction:*"` | Pass: 4/4 cases, 1,716/1,716 assertions. |
| Complete Profile doctest executable during iteration | Pass, exit 0 in 42.7 seconds. |
| `tools\\validate_tests.bat` | Final-source pass in 53.6 seconds; 129/129 project/filter items and the complete Profile harness passed. |
| `tools\\validate_coverage.bat` | Final-source pass in 72.1 seconds; Physics stages/solver is 4,966/5,760 lines (86.22%) against the 70% floor and every subsystem floor passes. |
| `tools\\validate_physics.bat` | Final-source pass in 41.0 seconds; Debug build, deterministic engine runs, and the owner-approved Physics baseline comparison passed without refresh. |
| `tools\\validate_format.bat` | Pass in 44.9 seconds; 587 source files, 327 headers, and every repository-relative `Related:` path are clean. |
| `git diff --check` | Pass. |

NM2 is an ordinary incremental slice, so no rubber-duck review is appropriate.
The mandatory independent plan-level review remains owned by NM5.
