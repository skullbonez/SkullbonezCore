# Source Modernization Sweep

Date: 2026-08-02
Status: IN PROGRESS — 4/5 phases complete
Impact area: Assets, Maths, Physics, World, Rendering/DX12, Runtime App/Camera legacy naming and idiom
Owner: Engine source conventions
Priority: Fourth

## Problem And Evidence

This plan is deliberately small, and the honest framing matters more than the
work: **the tree is already modern.** A modernization plan that manufactures a
large sweep here would be inventing work. The measured baseline:

| Signal | Count |
|---|---|
| C++ language standard | `stdcpp20`, all five configurations |
| Warning level / errors | `Level4` with `TreatWarningAsError=true`, all five configurations |
| `NULL` uses | 2 |
| `typedef` declarations | 0 |
| Object-like `#define` constants | 2 |
| C-style casts in `.cpp` | 136, across 15 files (MZ0 current-tree correction) |
| Hungarian-prefixed parameters | 82 rename-owned semantic parameters across 27 files, plus 7 retained Win32 slots (MZ0 current-tree correction) |
| Raw `new`/`delete`/`malloc` outside allocator internals | 0 |
| `throw` statements | 0 |
| Inheritance relationships | 1 |

What remains is a legacy stratum, not a pervasive style problem, and it is
concentrated in the oldest code. MZ0 corrected the earlier lexical estimate with
a complete syntax-aware census: 82 semantic prefixed parameters are rename-owned
across 27 files, seven Win32 slots are retained, and 136 casts occupy 15 files. The
exact files, lines, target types, rename map, false-positive exclusions, and
validation impact are permanent in
`../../Reports/2026-08-03/source-modernization-mz0-census.md`.

Two smaller inconsistencies are worth a ruling rather than an edit:

- `Vector3::Normalise`, `TryNormalise`, and `TryNormalised` use British spelling
  while the rest of the engine uses American. This is not a defect and renaming it
  touches a type included by 61 files.
- The directory is `Maths/` while the namespace is `Math::`. Same axis, same
  question, and again not a defect.

The constraint that shapes this plan: most of the affected files are physics hot
paths under a byte-exact baseline contract. A rename is behavior-neutral by
construction, but "by construction" is exactly the reasoning that has to be
proven rather than asserted, because `Maths/Vector3.h` is included by 61 files and
a slip during a mechanical rename is not visible in review.

## Goal

The measured legacy residue is removed or explicitly retained with a reason, no
behavior changes, and no baseline moves.

## Non-Goals

- No reformatting. `.clang-format` owns mechanical layout and this plan does not
  touch it or run a broad formatter.
- No API redesign, no header restructuring, no include-graph work, no signature
  changes. Renaming a parameter is in scope; changing what a function takes is not.
- No `auto` crusade, no range-for conversion campaign, no algorithm-header
  substitution, no `[[nodiscard]]` sweep. Those are style preferences, not residue.
- No renaming for aesthetic preference where MZ3 records a retain ruling.
- No behavior change of any kind. Any baseline movement is a defect.
- No new naming rule added to `AGENTS.md` or the code style guide unless MZ3
  produces a ruling that needs to be written down for future readers.

## Phases

- [x] **MZ0 — Census and classify.** The complete current-tree census supersedes
  the stale 44-cast estimate: 136 syntax-confirmed casts across 15 files are all
  retire-classified; the two exact `NULL` tokens and two internal
  `SKULLBONEZ_INTRINSICS` definitions are retire-classified; and 82 semantic
  prefixed parameters across 27 files are bounded for MZ2. Ten A/B-role names
  are not type prefixes, two external declaration rows are excluded, and seven
  first-party `wParam`/`lParam` slots are explicit Win32-boundary retains.
  Physics hot-path, DX12, broad-header, and
  closing-gate impact is mapped in
  `../../Reports/2026-08-03/source-modernization-mz0-census.md`.

- [x] **MZ1 — Retire `NULL`, object-like `#define` constants, and C-style casts.**
  Replace `NULL` with `nullptr`, object-like `#define` constants with
  `inline constexpr` in the owning header, and all 136 retire-classified casts
  with the narrowest correct named cast. Keep every retain-with-reason occurrence
  and add a brief comment naming the reason where it is not obvious from context.
  This phase touches no physics numerics and should produce an empty behavioral
  diff; inspection confirmed that the 136 named-cast substitutions, two
  `nullptr` substitutions, and direct intrinsics build predicate change no
  arithmetic, target type, ordering, signature, control flow, or data flow.
  Profile builds with zero warnings/errors, formatting passes, and the required
  touched-source comment audit is 17/17. Permanent evidence is in
  `../../Reports/2026-08-03/source-modernization-mz1-idioms.md`.

- [x] **MZ2 — Retire Hungarian parameter prefixes.** Rename the 82 semantic
  parameters across all corresponding declaration/definition sites in the 27
  census files. Work one file at a time and build between files;
  a rename that compiles is not automatically correct when a member and a
  parameter differ only by prefix, which is the specific hazard in
  `Camera.cpp`, `WorldEnvironment.cpp`, and `SpatialGrid.cpp`. Update any comment
  that names an old parameter in the same edit, per the Comment Quality Gate rule
  that comments must name post-change reality. Do not rename members, locals, or
  anything the census did not classify — scope creep here is how a behavior-neutral
  plan stops being behavior-neutral. All 82 are retired across the 27 files;
  the collision-free semantic spellings `requestedCellSize`, `messageId`, and
  `commandLineText` replace three MZ0 suggestions that would have hidden a
  member or local. Every file compiled before the next edit, the final Profile
  and format gates pass, and the touched-source comment audit is 27/27.
  Permanent evidence is in
  `../../Reports/2026-08-03/source-modernization-mz2-parameters.md`.

- [x] **MZ3 — Rule on the spelling and namespace inconsistencies.** Decide and
  record: whether `Normalise`/`TryNormalise`/`TryNormalised` become American
  spelling, and whether the `Maths/` directory and `Math::` namespace are
  reconciled. Both are legitimately retain decisions — `Vector3.h` is included by
  61 files and the directory/namespace split has been readable for years. If
  either is retained, record the reason so a future reader does not reopen it; if
  either is changed, it is a mechanical rename with the same MZ2 discipline and it
  escalates the closing gate to `validate_full`. State the decision either way;
  leaving it unruled is not an outcome. Both conventions are retained:
  `Normalise`/`TryNormalise`/`TryNormalised` form a coherent, called public API,
  while `Maths/` names the physical module and `Math::` the singular conceptual
  domain. Neither rename repairs behavior or dependency ownership, and no alias
  or new naming rule is introduced. Permanent evidence is in
  `../../Reports/2026-08-03/source-modernization-mz3-rulings.md`.

- [ ] **MZ4 — Prove byte-exactness and close.** Run `tools\validate_physics.bat`
  and `tools\validate_physics_deep.bat` and confirm no baseline moved. Because
  MZ2 touches `SpatialGrid*`, `PhysicsWorld*`, `Ragdoll*`, and `BoundingSphere*`,
  add `tools\validate_perf.bat`. Because `Maths/Vector3.h`, `Maths/Quaternion.h`,
  and `Rendering/Text.h` are widely included, run `tools\validate_full.bat` at the
  closing gate. If MZ3 changed `Vector3` or the namespace, `validate_full` is
  mandatory rather than precautionary. Audit every touched source-bearing file.
  Obtain an independent read-only review confirming the diff is naming and idiom
  only, with no signature, control-flow, or numeric change.

## Dependencies And Decisions

- Runs last. It is the lowest-value plan in the portfolio and it touches the most
  files, which is the correct reason to do it after the coverage and correctness
  work rather than before.
- Depends on the completed Comment Vocabulary Audit closing CV1 and CV2 first,
  because MZ2 edits comments in the same files CV2 reconciled. Its permanent
  closure evidence is in
  `../../Reports/2026-08-03/comment-vocabulary-audit-closure.md`.
- MZ2 benefits from the completed Narrowphase Manifold And Sleep Coverage plan
  (`../../Reports/2026-08-02/narrowphase-manifold-sleep-coverage-closure.md`)
  and Broadphase Pair Dedup Cost plan
  (`../../Reports/2026-08-02/broadphase-pair-dedup-cost-closure.md`) having
  landed, since both add coverage to files MZ2 renames in.
- No phase carries baseline-refresh authority. A rename that moves a physics
  baseline is a defect in the rename, not a behavior change to accept. Revert and
  find the slip.
- If a later current-tree census finds the residue smaller than MZ0's counts —
  for example if many of the 136 casts have already been retired — the
  correct outcome is a smaller plan with that recorded, not a search for
  additional work to justify the phase count.

## Acceptance

The plan closes when every MZ0 census occurrence is retired or retained with a
recorded reason, the 82 retire-classified parameters are gone from their sites, the
spelling and namespace questions each carry an explicit ruling, comments naming
renamed parameters are updated, no physics or visual baseline moved, the mapped
gates pass, and independent review confirms the diff contains no signature,
control-flow, or numeric change.

## Validation

- `tools\validate_physics.bat` — byte-exact, no baseline movement
- `tools\validate_physics_deep.bat` — no baseline movement
- `tools\validate_perf.bat`
- `tools\validate_full.bat` at the closing gate
- Touched-source comment audit
- Independent read-only review confirming naming-and-idiom-only diff

## Related

- `../../Reference/code-style-guide.md`
- `../../../SkullbonezSource/Maths/Vector3.h`
- `../../../SkullbonezSource/Physics/SpatialGrid.h`
- `../../../SkullbonezSource/Runtime/Camera/Camera.h`
- `../../../SkullbonezSource/World/WorldEnvironment.h`
- `../../Reports/2026-08-03/comment-vocabulary-audit-closure.md`
