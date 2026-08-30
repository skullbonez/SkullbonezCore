# Move Semantics And Resource Ownership Hardening

Date: 2026-08-30
Status: WNF — owner-requested hardening plan; 0/6 phases complete.
Impact area: first-party C++ ownership types, Rendering/DX12 resources, Runtime
startup and rendering owners, World resources, Core move-only results and
scopes, focused tests, and compiler-backed source design inspection.
Owner: Engine architecture owner
Priority: Parked ownership correctness follow-up
Commit name: `MOVE_OWNERSHIP`

## Owner Direction

Keep this plan under `WNF/`. It grants no authority to change production code,
run heavy validation, or alter a baseline until the owner moves it to `TODO/`
and registers it as active in `Agentic/Plans/MASTER-PLAN.md`.

The goal is not to maximize the number of `std::move` calls. Small detached
values, fixed-capacity packets, handles, and math types should remain ordinary
copyable values when copying is the truthful and inexpensive operation. Work
must instead make every resource-owning or identity-bearing type state whether
it can be copied, moved, or neither, and prove that every permitted move leaves
one valid owner and one harmless moved-from object.

## Problem And Current Evidence

The engine generally transfers values correctly at call sites:

- ownership-taking `std::string`, `std::vector`, and `std::unique_ptr`
  parameters are normally passed by value and moved into their destination;
- `SbResult`, `RuntimeReserveGrowthResult`, and primitive batch scopes have
  explicit `noexcept` moves that transfer their retained lease or lifecycle
  state;
- `PhysicsFixedList` requires nothrow-move-constructible elements before using
  move construction during backing replacement; and
- the current `return std::move(...)` sites return object members, where named
  local return optimization would not perform the transfer automatically.

The weakness is at type boundaries. Several classes retain raw native resources,
descriptor registrations, borrowed owner references, or address-based registry
identity, declare a destructor, and do not explicitly declare copy or move
operations. A user-declared destructor suppresses implicit move generation but
does not necessarily suppress the copy constructor. Reference and raw-pointer
members can therefore leave a resource owner copy-constructible even though two
instances must not release or unregister the same logical resource.

The initial source review identified these high-priority candidates:

| Type | Retained identity | Initial decision to prove |
|---|---|---|
| `Rendering::BLAS` | scratch/result `ID3D12Resource*` values released by `Reset` | Delete copy; keep immovable unless a real transfer caller exists |
| `Rendering::TLAS` | scratch/result resources and per-frame upload resources | Delete copy; keep immovable unless all resource rows can be transferred atomically |
| `Rendering::SBT` | owned shader-table buffer and derived ranges | Delete copy; keep immovable unless a consumer requires direct value transfer |
| `Rendering::MeshDX12` | owned vertex buffer plus stable device/draw/diagnostic borrows | Delete copy and move; transfer its owning `unique_ptr` instead |
| `Rendering::FramebufferDX12` | color/depth resources, descriptors, and texture-registry handles | Delete copy and move because registry and owner identity are address/lifetime sensitive |
| `Rendering::ShaderDX12` | COM bytecode, reflected state, and a development registry containing `this` | Delete copy and move; move the owning `unique_ptr` instead |
| `Runtime::Window` | HWND/HDC values and Win32 dispatch state associated with one object address | Delete copy and move |
| `Environment::WorldEnvironment` | movable `unique_ptr` resources plus stable backend borrows | Retain the explicit `noexcept` move only if moved-from and destination lifecycle tests prove it |

These are seeds, not the final inventory. Each decision must be confirmed from
all constructors, destruction paths, containers, factories, registries, and
callers. A type that is already implicitly noncopyable because of a member still
needs an explicit declaration when ownership would otherwise be unclear to a
reader.

## Goals

1. Every first-party type that owns a native handle, COM reference, heap block,
   registry row, callback lease, or teardown obligation explicitly declares its
   copy and move policy.
2. No owning type can be accidentally copied into a second destructor path.
3. A movable owner transfers every resource, token, accounting value, and
   registry relationship exactly once and leaves the source destructible and
   reusable only where its documented contract permits reuse.
4. Address-stable and thread-affine owners remain immovable; their
   `std::unique_ptr` or containing owner provides transfer when required.
5. Move constructors and assignments used by standard containers are
   `noexcept` whenever their operations truly cannot fail.
6. Value packets remain copyable when that is simpler and cheaper than imposing
   artificial ownership semantics.
7. Compile-time and focused behavioral evidence prevents ownership policy from
   regressing.

## Non-Goals

- Do not add `std::move` to every return, parameter, local, or assignment.
- Do not replace small value copies with rvalue-reference overload families.
- Do not move from `const`, cast away constness, or use `const T&&` as an
  ownership interface.
- Do not add `return std::move(local)` where normal return elision applies.
- Do not make an address-registered, thread-affine, reference-bound, or
  in-flight owner movable merely to satisfy a generic type trait.
- Do not replace stable `unique_ptr<T>` transfer with direct movement of `T`.
- Do not change DX12 command order, descriptor lifetime, fence retirement,
  Physics ordering, serialized formats, allocation timing, or replay output.
- Do not add a regex count budget, source-coordinate permission file, or
  permanent inventory of accepted findings.
- Do not refresh Physics, Replay, rendering, performance, or visual baselines.

## Ownership Decision Rules

Classify each candidate before editing it:

| Classification | Required special-member policy |
|---|---|
| Plain value | Rule of zero; copying and moving follow members |
| Borrowed view | Copyable when two views may observe the same lifetime; no destructor-owned work |
| Unique resource owner | Copy deleted; custom `noexcept` move only when every owned field can be transferred safely |
| Address-stable owner | Copy and move deleted; transfer an outer stable pointer instead |
| Scope or lease | Copy deleted; move either deleted or explicitly transfers the active obligation and disables the source |
| Aggregate composition root | Usually copy and move deleted when children borrow its address or each other |

For a custom move assignment, answer all of these in source and tests:

1. What happens to resources already owned by the destination?
2. Which exact fields are transferred?
3. Which source fields are cleared, invalidated, or left in a documented valid
   unspecified state?
4. Can either destructor release the transferred resource twice?
5. Does a registry, callback, worker task, HWND user-data slot, or borrowed
   pointer still contain the old object address?
6. Does self-move preserve a valid object?
7. Is `noexcept` true for the complete operation rather than added only for
   container performance?

## Inventory Method

Use CodeGraph before source reads to find constructors, factories, containers,
callers, teardown paths, registration calls, and dynamic dispatch. Confirm every
decision against current source.

Review first-party classes that meet at least one of these structural signals:

- a user-declared destructor;
- a raw native handle, COM pointer, mapped range, allocation pointer, descriptor
  index, or explicit `Reset`/`Release`/`Shutdown` operation;
- a `unique_ptr`, owning vector, move-only result, scope, token, or lease;
- registration of `this`, a callback address, a worker task address, or another
  identity that would be invalidated by movement;
- deleted copy with an implicit, defaulted, or custom move;
- by-value ownership parameters or rvalue-reference overloads; or
- movement during dense-store compaction or backing replacement.

Record the temporary working inventory as Markdown or TSV under the owning plan
artifact directory only while the plan is active. The final source and tests,
not a frozen finding count, are the lasting proof.

Use compiler traits to verify suspected implicit operations. Source appearance
alone is insufficient because one noncopyable member may already delete a
special member. At minimum record:

- `std::is_copy_constructible_v<T>`;
- `std::is_copy_assignable_v<T>`;
- `std::is_move_constructible_v<T>`;
- `std::is_move_assignable_v<T>`; and
- `std::is_nothrow_move_constructible_v<T>` when movement is supported.

## Phases

- [ ] **MH0 — Classify ownership and prove the current type traits.** Build the
  complete candidate inventory across Core, Assets, Physics, Rendering, Scene,
  World, UI, and Runtime. For each type, record owned resources, borrowed
  identities, teardown operations, address/thread stability, actual transfer
  callers, current compiler traits, and the intended policy. Start with the
  seeded DX12 and Window types. Do not infer that a type needs movement merely
  because it owns resources.

- [ ] **MH1 — Close raw DX12 ownership copies.** Apply the confirmed copy/move
  policy to `BLAS`, `TLAS`, `SBT`, `MeshDX12`, `FramebufferDX12`, `ShaderDX12`,
  and every adjacent DX12 owner found by MH0. Prefer explicit deleted copy and
  move operations for reference-bound, registry-bound, or address-stable
  objects. Implement custom movement only for a demonstrated production
  transfer and test destination replacement, moved-from destruction, and
  registry/descriptor/resource uniqueness. Add compile-time trait assertions
  beside the focused Rendering contract tests.

- [ ] **MH2 — Harden Runtime, World, Core, and remaining package owners.** Apply
  the ownership matrix to `Window`, composition roots, worker-address owners,
  terrain/water resource holders, scopes, leases, and remaining first-party
  packages. Keep `Terrain` and similar objects immovable when their
  `unique_ptr` owner already supplies the correct transfer boundary. Make
  implicit noncopyability explicit where ownership would otherwise be
  ambiguous. Do not change package direction or introduce forwarding headers.

- [ ] **MH3 — Prove existing movable owners and scopes.** Exercise `SbResult`,
  `RuntimeReserveGrowthResult`, `PrimitiveBatchScope`, `WorldEnvironment`, and
  every other supported custom/default move from MH0. Pin self-move where it is
  supported, destination cleanup before move assignment, source neutrality,
  exactly-once teardown, nested scopes, and `noexcept` traits. A non-owning
  callback/view type must remain honestly copyable unless a real linear
  obligation justifies move-only behavior.

- [ ] **MH4 — Repair demonstrated transfer inefficiencies only.** Use the
  compiler and focused profiling to inspect ownership sinks, return paths,
  container insertion, and dense-store compaction. Fix only confirmed expensive
  copies or missing transfers. Preserve direct returns eligible for copy
  elision, preserve cheap packet copies, and prefer one by-value sink over
  parallel `const&`/`&&` overloads when the destination always owns a copy.
  Check `performance-move-const-arg`, `performance-no-automatic-move`,
  `modernize-pass-by-value`, and special-member diagnostics as advisory
  compiler findings; do not enable a repository-wide check until its scope and
  negative controls are clean.

- [ ] **MH5 — Complete focused and repository validation.** Run compile-time
  trait tests, moved-from lifecycle tests, affected Debug/Profile builds,
  focused Rendering/Core/Runtime tests, dependency and allocation gates,
  compiler-backed source design, full unit tests, Automation, DX12 validation,
  and the terminal plan-completion command exactly once. Obtain an independent
  read-only review centered on double release, stale registry addresses,
  destination cleanup, exception specifications, and unnecessary moves. No
  baseline update is an acceptable fix.

## Validation Map

| Change | Required evidence |
|---|---|
| Deleted copy/move policy | compile-time trait assertions in the owning focused test target |
| Custom unique-owner move | destination replacement, moved-from destruction, self-move decision, and exactly-once release test |
| Address-stable owner | negative compile-time copy/move traits plus source-confirmed registry/callback/borrow path |
| `SbResult` or reserve grant movement | retained diagnostic/grant accounting before and after move construction and assignment |
| DX12 resource owner | affected Debug/Profile build, focused lifecycle test, DX12 validation, and deferred-release/descriptor review |
| Runtime or Window owner | startup/shutdown focused tests and Automation lifecycle validation |
| World resource holder | render-resource reset/rebuild tests without allocation-timing or scene-output changes |
| Ownership sink optimization | compiler evidence or focused measurement showing the removed copy; no pessimizing return move |
| Dependency-bearing source edit | `tools\validate_dependency_graph.bat` |
| Documentation-only plan work | `git diff --check`; no repository validation required |

## Seed Files

This list starts MH0; it is not permission to edit every file.

- `SkullbonezSource/Rendering/DX12/BLASDX12.h`
- `SkullbonezSource/Rendering/DX12/TLASDX12.h`
- `SkullbonezSource/Rendering/DX12/SBTDX12.h`
- `SkullbonezSource/Rendering/DX12/MeshDX12.h`
- `SkullbonezSource/Rendering/DX12/FramebufferDX12.h`
- `SkullbonezSource/Rendering/DX12/ShaderDX12.h`
- `SkullbonezSource/Rendering/PrimitiveBatchRenderer.h`
- `SkullbonezSource/Core/SbResult.h`
- `SkullbonezSource/Core/Allocation/RuntimeReserveAllocator.h`
- `SkullbonezSource/Runtime/Startup/Window.h`
- `SkullbonezSource/Runtime/App/Run.h`
- `SkullbonezSource/Runtime/Render/RuntimeRenderer.h`
- `SkullbonezSource/World/Terrain.h`
- `SkullbonezSource/World/WorldEnvironment.h`
- `SkullbonezTests/TestReserveAllocator.cpp`
- the focused Core, Rendering, DX12, Runtime lifecycle, Terrain, and World tests
  selected by MH0

## Review Questions

1. Can any type with teardown responsibility still be copy-constructed into a
   second release path?
2. Does any permitted move leave a registry, callback, worker task, native
   user-data slot, or borrowed child pointing at the source object's address?
3. Does move assignment retire the destination's prior ownership before taking
   the source state?
4. Is every moved-from object harmless to destroy, and is further use either
   supported explicitly or rejected clearly?
5. Are `noexcept` declarations true for every member operation?
6. Did any change add an rvalue overload, `std::move`, or wrapper without a
   demonstrated ownership or performance benefit?
7. Did any change alter allocation timing, renderer command order, Physics
   ordering, serialized bytes, or a golden baseline?

## Completion Definition

The plan is complete only when every MH0 candidate has a recorded and
implemented ownership policy, all unique and address-stable owners have
compiler-enforced copy/move traits, every supported move has focused lifecycle
evidence, the terminal validation and independent review are clean, and no
baseline or policy exception was introduced to obtain that result.
