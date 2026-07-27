# Scene Runtime Verb Partition Consolidation

Date: 2026-07-26
Status: IN PROGRESS — SR0-SR2 closed 2026-07-27. State-owning operations and
residual names now sit behind concrete owners, transactions, or pure domain
concepts. SR3 is binding.
Drafted from the 2026-07-26 from-source architecture review of
`nightrunner-26th-JUL-26` at tip `35f6de4e`. Registered in
`MASTER-PLAN.md` on 2026-07-26 as plan 8 of the Architecture Follow-Up Campaign
Round 5. Starts after `ceremonial-aggregate-elimination` closes. 3/4 phases
complete.
Impact area: `Runtime/Scene/SceneRuntime*.{h,cpp}`,
`Runtime/Scene/SceneController.*`, `Runtime/Scene/SceneRequestExecution.cpp`,
`SKULLBONEZ_CORE.vcxproj` and filters
Owner: runtime scene
Priority: Medium — the naming actively misdirects, and the file family is the
template an agent copies when adding the next scene operation.

## Problem And Evidence (measured 2026-07-26)

`Runtime/Scene/` contains a family of eight `SceneRuntime*` units that partition
scene work by **verb**, not by owner:

| Unit | Shape |
|---|---|
| `SceneRuntimeCreate.{h,cpp}` | free `CreateSceneFromUI` over a one-member context |
| `SceneRuntimeLoad.{h,cpp}` | free load operations |
| `SceneRuntimeReset.{h,cpp}` | free capture/restore/clear over a snapshot value |
| `SceneRuntimeStyle.{h,cpp}` | three free operations over a seven-reference context |
| `SceneRuntimeUiOptions.{h,cpp}` | free operations over a context |
| `SceneRuntimeDefaults.{h,cpp}` | free defaults operations |
| `SceneRuntimeGeneratedControls.{h,cpp}` | free generated-control operations |
| `SceneRuntimeCoordinator.{h,cpp}` | coordination entry points |

`SceneRuntime.h` itself is 152 lines and declares a small type
(`SceneRuntime() = default;` at `:117`, a queue constructor at `:118`, private
section at `:145`). So the eight `SceneRuntime*`-named units are not the
implementation of `class SceneRuntime` — they are free functions over context
structs, named after a type they do not implement.

Two `AGENTS.md` rules bear directly on this:

- **God-Object Closure Rule**: "Making `Run.cpp` short while the same authority
  remains reachable through sibling translation units is not decomposition." The
  same test applies to the scene owner: authority reachable through eight
  verb-named siblings over shared context structs is a partition, not an
  ownership boundary.
- **Migration Cleanup Review Rule**: it bans new types and modules named around
  migration mechanics including `Runtime`. `SceneRuntimeCreate`,
  `SceneRuntimeStyle`, and their siblings are exactly that — `Runtime` as a
  file-name filler, carrying no owner meaning.

This is the same class of finding as `source-blemish-remediation` B3, which
renamed ten `Run*`-prefixed files that contained zero `Run::` member definitions
because "the `Run` prefix is extraction residue from the old god-object
decomposition and now actively misleads." The `SceneRuntime*` family is that
finding's unaddressed twin.

The two GV-ratified invariant owners in this directory —
`SceneLoadTransaction` (GV2) and `SceneGeneratedControlTransaction` (GV3) — are
the correct shape and show what the rest of the family should look like.

## Goal

Every unit under `Runtime/Scene/` is named for the owner it implements or the
domain concept it defines. Scene operations belong to a concrete owner or to a
phase-checked transaction; none is a free function over a shared context struct
named after a verb.

## Non-Goals

- No disturbance of `SceneLoadTransaction` (GV2) or
  `SceneGeneratedControlTransaction` (GV3). Both are ruled invariant owners and
  are the model, not the target.
- No new god object. Collapsing eight units into one 4,000-line
  `SceneController.cpp` is the opposite failure and is explicitly rejected. The
  test is ownership, not file count.
- No behavior change. Scene load, reset, live style, cinematic selection, UI
  options, defaults, and generated-control rebuild all behave identically.
- No new context/service bag while moving operations. That is
  `ceremonial-aggregate-elimination`'s job and its deletions must not be undone.
- No change to the scene file schema, its version, or its migrations.
- No `Run*`-style rename that only swaps one meaningless prefix for another.

## Phases

- [x] **SR0 — Census ownership and rule each unit's destination.**
  For every operation in the eight units, record: the state it mutates, the owner
  that owns that state, whether the operation is ordered with respect to others,
  and whether any group shares a sequencing or arbitration invariant. Rule each
  operation to exactly one destination: (a) a method on the concrete owner of the
  state it mutates, (b) a phase of an existing GV transaction, (c) a new
  phase-checked transaction where SR0 finds a real multi-step ordering invariant,
  or (d) a domain-named value/operation header where it is genuinely a free pure
  function. Record which units disappear entirely and which are renamed.
  Acceptance: every operation has one ruled destination; any proposed new
  transaction has its exact phase order and arbitration rule written before
  implementation; no operation is ruled "keep as free function over a context".
  Closed 2026-07-27. The exhaustive operation/state/owner/order ruling is in
  `../../Reports/2026-07-27/scene-runtime-verb-partition-sr0-census.md`.
  Seven units dissolve; style splits between `SceneController` and pure
  `SceneCinematicPolicy`; GV3 receives an owner-name-only rename. No new
  transaction is justified because load/reset/UI work is already ordered by
  GV2 and generated-control work by GV3.

- [x] **SR1 — Move the state-owning operations to their owners.**
  Implement destinations (a) and (b): style, UI options, defaults, and reset
  operations become methods on the concrete owner of the state they touch, or
  phases of the existing transactions. `SceneRuntimeReset`'s
  `CaptureSceneRuntimeResetSnapshot` / `RestoreSceneRuntimeResetSnapshot` pair
  (`SceneRuntimeReset.h:95,100`) is a capture/restore ordering invariant and is a
  strong candidate for (c) — SR0's ruling decides. Delete each emptied unit and
  update `SKULLBONEZ_CORE.vcxproj` and its filters in the same commit.
  Acceptance: no `SceneRuntime*`-named unit contains free operations over a
  context struct; scene reset, live style, cinematic mode, UI options, and
  defaults behave identically; scene snapshot and lifecycle tests pass; physics
  CSV byte-exact.
  Closed 2026-07-27. Seven implementation units disappeared; owner methods and
  the GV2 private load/reset/presentation phases preserve their exact ordering.
  Fast, Physics, and full gates pass without baseline motion. Evidence:
  `../../Reports/2026-07-27/scene-runtime-verb-partition-sr1-owner-moves.md`.

- [x] **SR2 — Resolve the residual naming.**
  Rename any surviving unit to the owner or domain concept it actually carries,
  eliminating `Runtime` as a filler token per the Migration Cleanup Review Rule.
  `SceneRuntimeCoordinator` must either be a real named owner with stated
  responsibility or be dissolved. Confirm `class SceneRuntime` itself is either a
  meaningful owner or folded into `SceneController`. Correct every comment that
  names a moved responsibility — the Comment Quality Gate requires this in the
  same commit, and `header-claim-staleness-remediation` established that this
  regime is blind to moved responsibilities unless done deliberately.
  Acceptance:
  `rg -n 'SceneRuntime(Create|Style|UiOptions|Defaults|GeneratedControls|Reset|Coordinator)' SkullbonezSource SkullbonezTests`
  returns no rows, or only rows for a unit whose header states the owner it
  implements; project and filter files match the source tree exactly.
  Closed 2026-07-27. The queue, session state, and lifecycle ledger now belong
  to `SceneSession`, folded directly into `SceneController` without a
  forwarding accessor. Residual headers and types carry their load-request,
  cinematic-policy, generated-control-transaction, or UI-submission names.
  Tracked-source search returns zero forbidden rows; production and test project
  filters match exactly. Evidence:
  `../../Reports/2026-07-27/scene-runtime-verb-partition-sr2-residual-naming.md`.

- [ ] **SR3 — Reconcile, review, and hand off.**
  Re-run the SR0 census. Complete the comment audit for every touched file.
  Obtain one independent ownership review asking: is scene authority now reachable
  through fewer, named owners; did any operation become a method on an owner that
  does not own its state; did a new god object appear; and did any context struct
  return. Acceptance: review clear; `validate_full.bat`, `validate_physics.bat`,
  and `validate_dx12_renderer.bat` pass with no scene-schema, physics, or DX12
  baseline change.

## Dependencies And Decisions

- Depends on `ceremonial-aggregate-elimination` CA2, which deletes
  `SceneRuntimeCreateContext`, `SceneRuntimeStyleContext`, and
  `SceneRuntimeUiOptionsContext`. Running this plan first would move operations and
  then have to move them again when their contexts are deleted.
- Depends on `governance-shape-to-judgment-conversion` G1 for SR3's review test.
- Owner-overridable default, agent does not stop: whether `class SceneRuntime`
  survives at all. SR0 reports its members and consumers; SR2 folds it into
  `SceneController` unless the owner rules it a distinct owner.

## Acceptance

- No unit under `Runtime/Scene/` is a verb-named file of free functions over a
  context struct.
- `Runtime` does not appear as a filler token in any surviving unit name.
- Scene authority sits with named owners or phase-checked transactions.
- Zero behavior change; project files match the source tree.

## Validation

- `tools\validate_physics.bat` — `SceneController.Objects*`-class physics
  coordination is touched.
- `tools\validate_dx12_renderer.bat` — style/cinematic operations feed render
  config.
- `tools\validate_tests.bat` — scene parser, snapshot, and lifecycle coverage.
- `tools\validate_full.bat` — `Runtime/*` and scene setup changed; required at the
  closure gate.
