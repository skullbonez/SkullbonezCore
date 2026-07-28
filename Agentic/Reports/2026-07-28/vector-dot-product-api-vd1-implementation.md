# Vector Dot-Product API VD1 Implementation

Date: 2026-07-28
Branch: `nightrunner-28th-JUL-26`
Plan: `Agentic/Plans/TODO/vector-dot-product-api.md`
Required commit subject first line:
`VECTOR-DOT-PRODUCT-API, TASK 2 / 3, 22% OVERALL COMPLETE — replace ambiguous vector multiplies with explicit Dot`

## Outcome

`Math::Vector::Dot` is now the single first-party vector dot-product contract:

```cpp
inline float Dot( const Vector3& lhs, const Vector3& rhs )
{
    return lhs.x * rhs.x + lhs.y * rhs.y + lhs.z * rhs.z;
}
```

The body preserves the former overload's multiply/add token order exactly.
`Vector3::operator*( const Vector3& )` is deleted. The anonymous
OrbitalMechanics adapter is deleted and that file imports the shared operation.
No macro, forwarding wrapper, alias, or compatibility overload remains.

## Exact Census Reconciliation

The VD0 type-aware census remains the authoritative input:

| Proof | Result |
|---|---:|
| Census rows | 171 |
| Tracked files | 34 |
| Final exact `Dot( lhs, rhs )` rewrites | 170 |
| Adapter overload row removed with adapter deletion | 1 |
| Reconciled total | 171 |
| Missing or duplicate replacement groups | 0 |
| Token-boundary residual old expressions | 0 |

Each rewrite retained the current source's complete left and right operand
subexpressions in the same order. No operand swap, reassociation, temporary,
common-subexpression extraction, precision change, loop, SIMD substitution, or
fused operation was introduced. Six VD0 AST display spellings normalized
`this->` or float-literal presentation; the codemod deliberately used the
current source spellings instead, so those subexpressions also remained
byte-for-byte unchanged inside the new call.

Deletion of the vector-vector member makes the compiler proof type-aware: any
missed first-party `Vector3 * Vector3` expression has no viable overload.
The complete Profile solution build succeeded with zero warnings and errors,
and project/filter validation reconciled all 787 production project and filter
items.

## Validation

| Command / proof | Result |
|---|---|
| `tools\validate_build.bat Profile` | PASS; solution, Maths, Physics, UI, Core, and tests build with 0 warnings / 0 errors |
| Complete `Profile\SKULLBONEZ_TESTS.exe` | PASS; process exited 0 |
| Focused Maths/Physics/Runtime doctest filters | PASS; 62/62 cases, 9,294/9,294 assertions |
| `tools\validate_project_filters.bat` | PASS; 787 project items and 787 filter items, zero errors |
| `tools\validate_dependency_graph.bat` | PASS; 27 include rules, 46 negative fixtures, zero findings |
| `inventory_authority_free_aggregates.py` | PASS; 85/85 gated rows ruled |
| `inventory_extraction_scars.py` | PASS; 1/1 finding ruled |
| `inventory_wide_signatures.py` | PASS; current qualitative ruling scan exited 0 |
| Exact census reconciliation script | PASS; 170 exact final calls + 1 adapter-row deletion = 171 |
| Source deletion proof | PASS; no vector-vector member or OrbitalMechanics-local adapter; exactly one shared inline definition |

`tools\validate_format.bat` correctly remains red in this shared working tree
because it reports only the protected warm files:
`PersistentContactSolver.cpp` and `PhysicsNarrowphaseStage.cpp`. A targeted
pipeline check found all 30 non-Persistent production files in VD1 clean; the
three touched tests are outside that gate's `SkullbonezSource` scope and their
task lines follow the surrounding test style. Reconstructing
`PersistentContactSolver.cpp` from `HEAD` plus only its 21 VD1 rewrites produces
an exact formatter fixed point. Every formatter-proposed current-worktree
change in that file is confined to the protected warm hunk.

VD2 owns the final `validate_tests`, byte-exact `validate_physics`,
`validate_perf`, and `validate_full` gates. VD1 did not refresh any baseline,
golden, config, schema, or performance artifact.

## Touched-Source Comment Audit

Checklist/evidence path: this report.

- Checked: 34/34 census files.
- Deferred: 0.
- Unchecked: none.
- Every file retains its file-specific `File`, `Purpose`, `Summary`, and
  `Glossary` learning-header sections.
- The new public `Dot` contract states the byte-exact arithmetic invariant next
  to its body.
- The migration moves no owner, lifetime, phase, sequencing, or subsystem
  responsibility, so no existing behavioral claim became stale.
- Existing nearby collision, solver, replay, editor, and test comments remain
  accurate after the spelling-only substitution.

The audited inventory is the exact 34-file VD0 migration surface:
1 Gameplay file, 5 Maths files, 15 Physics files, 10 Runtime files, and 3 test
files.

## Protected Warm-Start Boundary

The pre-task and post-task protected bytes are identical:

| Protected content | SHA-256 |
|---|---|
| `PersistentContactSolver.cpp` current lines 251-282 | `d0f4ff950d0c4fb9c353643ee6e3c47a766dce55cd9201129a5568d3b8f1bf86` |
| `PersistentContactSolver.cpp` current lines 257-280 | `9b381d005197c30956381389634dc3d90ec0c894cd2ce41eb18b48dd80d7b5d1` |
| `PersistentContactSolver.h` whole file | `3bc67bfa277bf6be8a0e2086563d686d4e1a4f8d75a82ced39f039d02422ac59` |
| `PhysicsNarrowphaseStage.cpp` whole file | `b23e20c2e54e6868d101b67239d5c9d1a6ad86ff5d45f6404acd625d462cbb7f` |

The combined `PersistentContactSolver.cpp` worktree diff separates exactly:

- Warm/user-owned: four zero-context hunks at current/HEAD areas 257, 263, 268,
  and 276, all before line 280.
- VD1/task-owned: 15 zero-context hunks containing exactly 21 dot replacements,
  beginning at current line 582 and ending at current line 1804.

The complete VD1 task scope is 38 files: 34 source/test files plus this report,
the owning plan, `MASTER-PLAN.md`, and `SessionState.md`. Git status also shows
the two untouched warm-only files, for 40 modified/untracked paths in the shared
working tree; those two paths are not part of VD1.

Partial-stage procedure:

1. Stage every VD1 file except `PersistentContactSolver.cpp`; never stage either
   of the other warm files.
2. Run
   `git add -p -- SkullbonezSource/Physics/PersistentContactSolver.cpp`.
3. Answer `n` for the first four hunks at 257, 263, 268, and 276.
4. Answer `y` for all 15 remaining hunks from 582 through 1804.
5. Verify the cached diff contains 21 added `Dot` rows and no
   `PERSISTENT_CONTACT_BODY_MASK`, removed `BODY_MASK`, or warm-key comment.
6. Verify the unstaged diff still contains only the original warm hunks and the
   protected hashes above remain unchanged.

## Questions

No owner input is required for VD1 or VD2.
