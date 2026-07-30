# Inverse Trig Domain Guards Closure

Date: 2026-07-30
Plan: `inverse-trig-domain-guards`
Status: COMPLETE - TD0-TD3, 4/4
Impact areas: Maths, Physics, Runtime Camera, Runtime Editor, tests, project
inventory

## Outcome

Every reachable first-party inverse-trig call now receives either a
construction-proven argument or the shared
`SkullbonezCore::Math::ClampUnit` policy. The former file-local helper and two
inline `std::clamp` spellings are gone.

The Camera pitch cap clamps both normalized dot products before `acosf` and uses
the same world-Y fallback as the nearby right-vector basis repair when the
stored up vector is zero. A crafted rounded-pole regression proves that a
normalized self-dot of `1.000000119f` no longer makes the cap fail open.

`Matrix4::ShadowFromNormal` clamps the Debug reference path and the shipping
path, and handles the antiparallel normal with an explicit world-X rotation
axis. Editor terrain alignment now delegates its numerical policy to
`Runtime/Editor/EditorTerrainOrientation`, which applies the same clamp and
antiparallel treatment through a focused test seam. No baseline, golden,
configuration, schema, or committed runtime artifact changed.

## TD0 Census

The post-Maths-remediation census is permanent in
`inverse-trig-domain-guards-td0-census.md`:

- seven inverse-trig code sites and one comment reference;
- 95 `sqrtf` sites: 80 proven non-negative, 14 explicitly guarded, and one
  reachable open domain;
- three `atan2f` sites, all valid for their construction;
- the remaining open square-root site is the negative-restitution product in
  `PersistentContactSolver.cpp`; it is documented as a separately owned,
  out-of-scope defect rather than hidden by this plan;
- `Matrix4::ShadowFromNormal` had no production caller. Its new regression seam
  changes its inventory classification from no-reference to test-only.

The final strict reachability inventory reports 407 exact rows with zero
blocking diagnostics: 298 no-reference, 61 test-only, 41 own-translation-unit
only, and seven with both production and test reachability.

## Regression Evidence

Focused Debug and Profile tests passed:

- Camera rounded-pole and zero-up pitch-cap coverage: two cases, 11 assertions;
- inverted-normal shadow matrix coverage: 33 assertions;
- antiparallel editor terrain orientation: 10 assertions.

The complete unit harness passed 469/469 cases and
2,423,935/2,423,935 assertions. Direct Debug and Profile builds of
`SKULLBONEZ_CORE` and `SKULLBONEZ_TESTS` also passed.

## Ownership And Comment Review

The current inventories report:

- wide signatures: all 32 trigger rows ruled, with no new touched-area trigger;
- function complexity: 6,385 recognized functions and all 40 trigger rows
  ruled; the existing depth-six Attached Camera operation remains current and
  its body was not changed;
- authority-free aggregates: no new touched-area review candidate;
- extraction scars: zero in the touched scope; the repository-wide gate's one
  unrelated `WorkerPool` row remains explicitly ruled;
- build configuration: zero diagnostics and zero dropped inheritance;
- dependency direction and project ownership: zero findings.

All 12 touched C++ source-bearing files were inspected against
`Agentic/Reference/comment-style-guide.md`: 12 checked, zero deferred. The
shared clamp, pole fallbacks, numerical-equivalence qualification, and
antiparallel branches carry nearby invariant or hazard explanations. The
one-line Runtime/Editor project-filter inventory addition was also inspected;
it does not need a learning header or a separate subsystem checklist.

Independent Duck-01 review initially blocked closure on two evidence defects:
the stale no-reference reachability ruling for `ShadowFromNormal`, and a
comment that claimed the Debug and shipping matrix forms were numerically
identical. The ruling now records test-only reachability, and the comment now
states numerical equivalence while acknowledging the `sin(pi)` residual. The
follow-up review reports no blocker and answers all five ownership questions
without finding an aggregate, capability slice, extraction scar, rename
evasion, or unsupported completion claim.

Residual test sensitivity is explicit: the exact-negative-one Matrix and Editor
tests pin their antiparallel branches but would not independently detect removal
of `ClampUnit`; the rounded-pole Camera test directly requires the clamp. The
reviewer accepted that division of coverage because the shared policy itself is
exercised by the rounded-domain case.

## Final Validation

- `tools\validate_tests.bat`: PASS - 469 cases and 2,423,935 assertions.
- `tools\validate_physics.bat`: PASS -
  `physics_regression_varied.csv`, 44,401 lines, byte-exact across two generated
  output runs and the committed baseline.
- `tools\validate_full.bat`: PASS - mandatory CPU preflight, coverage,
  Automation policy, DX12 renderer checks, full CPU matrix, and final Physics
  comparison.

The first formal test invocation exposed the missing
`EditorTerrainOrientation` project-filter classification; adding the source to
the existing Runtime/Editor prefix inventory closed it. The first full-gate
preflight then found one paragraph-spacing correction in `MathsCommon.h`; the
repository formatter applied that mechanical fix. Neither event changed
behavior. A final `tools\validate_fast.bat` over this report, the campaign
ledger, and the TODO deletion is a commit precondition.
