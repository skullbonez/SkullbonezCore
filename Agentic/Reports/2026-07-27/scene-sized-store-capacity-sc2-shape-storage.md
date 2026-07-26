# Scene-Sized Store Capacity SC2 — Shape-Sized Collider Rows

Date: 2026-07-27
Branch: `nightrunner-26th-JUL-26`
Plan: `scene-sized-store-capacity` SC2

## Outcome

SC2 is complete. The dense `ColliderRecord` fell from 7,228 bytes to 80 bytes.
It retains hot collision/material scalars, handles, the concrete shape kind,
and a typed non-owning reference. Sphere, box, and convex-hull payloads now live
in separate `ColliderStore` backing arrays, so a scene with no hull colliders
has zero hull capacity.

## Ownership And Dispatch

`ColliderStore` is the single owner of per-kind payload storage and the only
owner that creates store-backed shape references. Each reference carries its
per-kind storage index, allowing copy, move, compaction, replacement, and
backing relocation to rebind every dense row deterministically.

Runtime physics, rendering, picking, editor overlays, replay visualization, and
diagnostics borrow `CollisionShapeReference`; they do not materialize the
7,176-byte hull alternative. Cold authoring snapshots that genuinely require
ownership call `CopyCollisionShape` explicitly. Object-object and
terrain-object dispatch use nested exhaustive visits with compile-time
`static_assert` fallbacks. No interface, virtual dispatch, callback, service
bag, or type erasure was introduced.

## Focused Proof

`Collider shape stores: hot rows stay compact and zero-hull scenes commit no
hull payload` passes 40/40 assertions. It proves:

- `sizeof(ColliderRecord) == 80`;
- three live sphere rows retain their handles and values;
- reserving sphere capacity from three to six relocates the live backing and
  rebinds both inspected references;
- store copy and move operations rebind into their own backing;
- compaction after deletion preserves the moved payload;
- replacing a sphere with a box updates per-kind counts and the typed view;
- hull count and capacity remain zero throughout the zero-hull case.

## Independent Review

The first review found four blockers: a move test outside `SceneLoad`, an
implicit reference-to-owning conversion that copied hull payload in read-only
scans, no forced live-backing relocation test, and stale allocation/comment
wording. All four were corrected. The final read-only re-review returned
`ZERO BLOCKERS`.

## Comment Audit

All 44 touched C++ source/header files were checked against their final
implementation. Updated comments describe borrowed per-kind payload ownership,
explicit cold copying, exhaustive dispatch, and cold topology growth. Stale
claims that collider rows own full shape snapshots were removed. Checked: 44.
Deferred: 0. Unchecked: none.

## Validation

- Focused shape-store doctest: PASS, 1 case / 40 assertions.
- Profile, Automation, and Debug builds: PASS, zero warnings and errors.
- `tools\validate_format.bat`: PASS.
- `tools\validate_physics.bat`: PASS; 44,401-line regression CSV byte-exact.
- `tools\validate_perf.bat`: PASS; allocation guard, DX12 absolute/regression
  budgets, and physics-benchmark absolute/regression budgets all pass.
- `tools\validate_full.bat`: PASS; 409 doctests / 2,404,024 assertions, all CPU
  lanes, Automation replay/prediction smoke, zero-error DX12 and accepted
  screenshots, and byte-exact physics regression.

No physics, replay, visual, DX12, schema, scene, or configuration baseline was
changed.
