# 14 - Public Physics API Boundary

Date: 2026-07-09
Status: Proposed
Priority: P1
Owner: Physics / Runtime API
Source issue: FAC-005 from `13-facade-retirement.md`

## Owner Decision - 2026-07-09

Create and execute a dedicated public physics API boundary cleanup. Public
physics API headers such as `PhysicsApi.h` and `PhysicsEngine.h` must expose no
`GameModel`, no raw dense `modelIndex` authority, and no solver container types.
Treat this as a deliberate physics identity/authority cleanup, not a rename or
vocabulary pass. Validate with `tools\validate_physics.bat` when implementation
reaches the PR gate.

## Problem

FAC-005 remains the only open structural facade item after Plans 01 and 10
closed the main runtime/rendering facade surfaces. The public physics API still
needs a type-level boundary pass so callers cannot depend on gameplay model
objects, dense solver indices, or internal solver containers as public
authority.

The target is structural:

- Public physics API signatures do not name `GameModel`.
- Public physics API signatures do not expose raw dense `modelIndex` authority.
- Public physics API signatures do not expose solver container types.
- Any surviving identity is a stable physics/domain identity owned by the
  physics boundary, not a raw storage slot.

## Scope

Initial headers in scope:

- `SkullbonezSource/Physics/PhysicsApi.h`
- `SkullbonezSource/Physics/PhysicsEngine.h`

Follow references from those headers only as needed to remove public boundary
leaks. Do not restart the wider GameModel authority campaign from scratch.

## Step-by-step implementation

- [ ] **0.1** Inventory public signatures in `PhysicsApi.h` and
  `PhysicsEngine.h` that mention `GameModel`, dense `modelIndex`, or solver
  container types. Record the exact signatures and proposed replacement
  identity/authority shape here. No code change; documentation-only.
- [ ] **1.1** Introduce or reuse the narrow public physics identity types needed
  to replace raw dense model-index authority. Keep the change scoped to the
  public boundary. Gate: `validate_physics`. Commit.
- [ ] **1.2** Remove `GameModel` from public physics API signatures and update
  callers to pass the new domain identity/context. Gate: `validate_physics`.
  Commit.
- [ ] **1.3** Remove solver container types from public physics API signatures
  and keep solver storage behind physics-owned APIs. Gate: `validate_physics`.
  Commit.
- [ ] **2.1** Reconcile FAC-005 acceptance in
  `13-facade-retirement.md` and this plan after the public header grep and
  physics gate pass. Commit.

## Validation

- Inventory/documentation-only steps: no repository validation required.
- Source/API implementation steps: `tools\validate_physics.bat`.
- If implementation also touches runtime frame ownership or broad runtime
  wiring, use `tools\validate_full.bat` instead of physics-only.

## Acceptance

- [ ] `PhysicsApi.h` exposes no `GameModel` in public signatures.
- [ ] `PhysicsEngine.h` exposes no `GameModel` in public signatures.
- [ ] Public physics API signatures expose no raw dense `modelIndex` authority.
- [ ] Public physics API signatures expose no solver container types.
- [ ] `tools\validate_physics.bat` passes after the final source slice.
- [ ] FAC-005 in `13-facade-retirement.md` is checked only after the structural
  header checks and physics validation pass.
