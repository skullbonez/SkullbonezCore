# Source Modernization Sweep

Date: 2026-08-02
Status: NOT STARTED — 0/5 phases complete
Impact area: Maths, Physics, World, Rendering, Runtime Camera legacy naming and idiom
Owner: Engine source conventions
Priority: Fifth

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
| C-style casts in `.cpp` | 44, across 10 files |
| Hungarian-prefixed parameters | 58, across 23 files |
| Raw `new`/`delete`/`malloc` outside allocator internals | 0 |
| `throw` statements | 0 |
| Inheritance relationships | 1 |

What remains is a legacy stratum, not a pervasive style problem, and it is
concentrated in the oldest code. The 23 files carrying Hungarian parameter
prefixes are `Maths/Quaternion.*`, `Maths/RotationMatrix.h`, `Maths/Vector3.h`,
`Physics/BoundingSphere.*`, `Physics/PersistentContactSolver.cpp`,
`Physics/PhysicsEngine.cpp`, `Physics/PhysicsWorld.*`, `Physics/Ragdoll.cpp`,
`Physics/SpatialGrid.*`, `Rendering/Text.*`, `Runtime/Camera/Camera.*`,
`Runtime/Camera/CameraCollection.*`, `Runtime/Render/UiTextPass.cpp`,
`World/Terrain.cpp`, and `World/WorldEnvironment.*`. Representative spellings are
`fCellSize`, `fChangeInTime`, `fX`/`fY`/`fZ`. Adjacent code in the same files
already uses modern names, so a reader meets both conventions inside one function
body — `SpatialGrid::SetCellSize( float fCellSize )` sits beside
`ReserveSceneCapacity( std::size_t bodyCapacity )`.

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

- [ ] **MZ0 — Census and classify.** Produce the exact per-file, per-occurrence
  list for each signal in the table above: the 2 `NULL` uses, the 2 object-like
  `#define` constants, the 44 C-style casts with their target types, and the 58
  Hungarian parameters. Classify each occurrence as retire, retain-with-reason, or
  needs-ruling. A C-style cast between arithmetic types is retire; one that is
  load-bearing for a platform or COM boundary is retain-with-reason. Record which
  occurrences sit in physics hot paths so MZ4 knows exactly which gates the change
  triggers. The census is the deliverable and bounds the diff before any edit.

- [ ] **MZ1 — Retire `NULL`, object-like `#define` constants, and C-style casts.**
  Replace `NULL` with `nullptr`, object-like `#define` constants with
  `inline constexpr` in the owning header, and each retire-classified C-style cast
  with the narrowest correct named cast. Keep every retain-with-reason occurrence
  and add a brief comment naming the reason where it is not obvious from context.
  This phase touches no physics numerics and should produce an empty behavioral
  diff; confirm that by inspection before moving on rather than at MZ4.

- [ ] **MZ2 — Retire Hungarian parameter prefixes.** Rename the 58 parameters
  across the 23 census files. Work one file at a time and build between files;
  a rename that compiles is not automatically correct when a member and a
  parameter differ only by prefix, which is the specific hazard in
  `Camera.cpp`, `WorldEnvironment.cpp`, and `SpatialGrid.cpp`. Update any comment
  that names an old parameter in the same edit, per the Comment Quality Gate rule
  that comments must name post-change reality. Do not rename members, locals, or
  anything the census did not classify — scope creep here is how a behavior-neutral
  plan stops being behavior-neutral.

- [ ] **MZ3 — Rule on the spelling and namespace inconsistencies.** Decide and
  record: whether `Normalise`/`TryNormalise`/`TryNormalised` become American
  spelling, and whether the `Maths/` directory and `Math::` namespace are
  reconciled. Both are legitimately retain decisions — `Vector3.h` is included by
  61 files and the directory/namespace split has been readable for years. If
  either is retained, record the reason so a future reader does not reopen it; if
  either is changed, it is a mechanical rename with the same MZ2 discipline and it
  escalates the closing gate to `validate_full`. State the decision either way;
  leaving it unruled is not an outcome.

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
- Depends on `comment-vocabulary-audit.md` closing CV1 and CV2 first, because MZ2
  edits comments in the same files CV2 reconciles and two plans editing the same
  headers concurrently would produce avoidable conflicts.
- MZ2 benefits from `narrowphase-manifold-sleep-coverage.md` and
  `broadphase-pair-dedup-cost.md` having landed, since both add coverage to files
  MZ2 renames in.
- No phase carries baseline-refresh authority. A rename that moves a physics
  baseline is a defect in the rename, not a behavior change to accept. Revert and
  find the slip.
- If MZ0 finds the residue is even smaller than the counts above suggest — for
  example if most of the 44 C-style casts turn out to be retain-with-reason — the
  correct outcome is a smaller plan with that recorded, not a search for
  additional work to justify the phase count.

## Acceptance

The plan closes when every MZ0 census occurrence is retired or retained with a
recorded reason, the 58 Hungarian parameters are gone from the 23 named files, the
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
- `comment-vocabulary-audit.md`
