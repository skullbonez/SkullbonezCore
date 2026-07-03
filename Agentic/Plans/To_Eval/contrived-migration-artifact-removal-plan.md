# Contrived Migration Artifact Removal Plan

Date: 2026-07-03
Status: Implementation in progress; Phase 0 kill-list CSV complete
Impact area: architecture, global-service remediation, physics, runtime settings, render/resource contexts, scene/world ownership
Validation for this document-only change: none required

Implementation status:

- Kill list:
  `Agentic/Reports/2026-07-03/contrived-migration-artifacts/contrived-migration-artifact-plan.csv`.
- Current implementation tracker:
  `Agentic/Reports/2026-07-03/contrived-migration-artifacts/contrived-migration-artifact-implementation-status.csv`.
- As of `87df4b77` after `78533f94` and
  `d5571316`, rows K001, K002, K004, K005, K006, K007, K008, K009, K010, K011,
  and K012 have source-side deletion/split work recorded in the tracker. K003 is
  still partial: diagnostics, render/collider/sleep/body stores, ragdoll,
  wake/seed island helpers, top-level contact-highlight ticking, explicit
  wake-time underwater refresh, and store-owned force integration are off raw
  `modelAccess.Models()` reads. `PhysicsBodyStore` now owns gravity, buoyancy,
  drag, water damping, righting torque, angular damping, pending impulses, pose
  integration, terrain clamping after integration, and allocator-owned body
  handle identity; `ColliderStore` owns collider handle identity and resolves
  collider bodies from store-owned body handles. Ragdoll joint solving mutates
  `PhysicsBodyStore` records, and point-joint setup, serialization, and smoke
  tests resolve through store-owned handles. `GameModel::UpdatePosition` and
  its terrain-clamp wrapper are deleted; `GameModel` still owns terrain query
  helpers that terrain CCD/manifold generation uses until that later K003 slice.
  Tornado field code no longer opens a raw model range; its remaining model sync
  is named on `PhysicsModelAccess` as owner-side compatibility.
  `PersistentContactSolver::Solve` no longer receives `PhysicsModelAccess`,
  opens `modelAccess.Models()`, or writes through raw model ranges; object
  manifold building now consumes an `ObjectContactBodyView` built from
  `PhysicsBodyRecord` pose plus `ColliderStore` shapes. Terrain/manifold,
  object wake/sweep, final solver writeback, and scene-boundary paths still need
  real physics-owned views before K003 can delete the remaining compatibility
  model ranges.
- Guardrail follow-up added `tools/check_runtime_boundaries.py` checks for
  deleted migration artifacts (`GameModelRuntimePhysicsTuning`,
  `legacyModelIndex`, `RuntimeConfigSnapshot`, and the no-factory
  `AssetSystem::CreateShader(const char*)` overload), and reset the named
  `*PhysicsModelsForCompatibility()` allowlist to zero hits.

## Goal

Identify and remove migration artifacts that were introduced to get rid of
global-service reads but do not yet express a real engine-domain concept.

The motivating example is `GameModelRuntimePhysicsTuning`. It was a useful
bridge because it removed direct `Cfg()` reads from `GameModel`, `RigidBody`,
and `BoundingSphere`. The artifact still feels contrived because it describes
where values came from and when they are copied, not the thing the engine is
modeling. Its fields want to become domain concepts such as:

```text
PhysicsMaterial
  friction, restitution thresholds, drag coefficients, surface/contact policy

BodySimulationLimits
  angular velocity limits, sleep thresholds, CCD limits, per-body clamp policy

ContactPolicy
  contact epsilon, terrain contact threshold, restitution threshold, solver
  policy values that should belong to collision/contact ownership
```

The target outcome is not "no compatibility code." The target outcome is that
temporary bridges are visible, owned, budgeted, and scheduled for deletion, and
that lasting types are named after engine concepts rather than migration
mechanics.

## Current Evidence

- `Agentic/Plans/In_Progress/carmack-global-service-579-hit-remediation-checklist.md`
  is complete, but several rows were closed by re-homing globals into explicit
  compatibility or future-owner buckets. That was the correct audit close, not
  the end of the cleanup.
- Phase 0 hard kill list is recorded in
  `Agentic/Reports/2026-07-03/contrived-migration-artifacts/contrived-migration-artifact-plan.csv`.
  The CSV deliberately excludes broad "could become a dumping ground" rows and
  keeps only 12 current-source migration bridges with proof, exact replacement,
  first deletion move, deletion condition, and validation mapping.
- `SkullbonezSource/GameObjects/GameModel.h` defines
  `GameModelRuntimePhysicsTuning`, which copies a subset of `EngineConfig` into
  game-model and collection state. This is the clearest suspect because the
  name describes runtime config transport instead of physics ownership.
- `SkullbonezSource/World/WorldEnvironment.h` owns a private
  `RuntimeConfigSnapshot` for water style and fluid force values. This may be
  acceptable short-term, but it should be audited because it mixes render style,
  camera frustum, and fluid drag values under a migration-shaped name.
- `SkullbonezSource/Assets/AssetSystem.h` still declares a "Transitional
  bridge" for active asset lookup and fallback shader creation.
- `SkullbonezSource/GameObjects/GameModelCollection.h` exposes
  `MutablePhysicsModelsForCompatibility()` and
  `PhysicsModelsForCompatibility()`. These are honest names for active debt,
  but every caller should have a deletion path toward physics, render, replay,
  or editor-owned stores.
- `SkullbonezSource/Physics/PhysicsModelAccess.h`,
  `SkullbonezSource/Physics/SimulationSystem.h`, and related physics store
  files intentionally describe temporary compatibility boundaries. Keep the
  useful parts, but do not let "temporary" become permanent architecture.

## Definition

A type, function, field, or module is a contrived migration artifact when it
mostly answers "how did we avoid touching the old global/service/storage path?"
rather than "what engine concept owns this behavior?"

Treat an item as suspicious when two or more of these are true:

1. Its name contains migration mechanics such as `Runtime`, `Snapshot`,
   `Compatibility`, `Transitional`, `Bridge`, `Tuning`, or `Context`, but the
   values inside belong to a more specific domain.
2. It copies a subset of `EngineConfig` or another owner into a POD bag without
   defining whether the bag is process-level, scene-level, world-level,
   body-level, material-level, frame-level, or tool-level state.
3. The type exists only so deep code can stop calling `Cfg()`, `Gfx()`,
   `WorkerPool::Instance()`, `Profiler::Instance()`, or active asset globals.
4. It has no deletion condition, owner bucket, checker budget, or follow-up
   phase.
5. It mirrors `GameModel`, `Run`, `EngineConfig`, or renderer globals instead
   of narrowing the API to commands and queries the caller actually needs.
6. Multiple subsystems write the same bridge state, or callers must remember to
   call an `ApplyRuntime*`, `Refresh*`, or `Sync*` method after mutation.

Not every `Context`, `Snapshot`, or `Compatibility` type is bad. A reset
snapshot with one capture/restore purpose, a render pass input that borrows
frame services for one call, or a public scene-file compatibility alias can be
the right shape. The audit must judge intent, owner, lifetime, and deletion
path.

## Non-Goals

- Do not reopen the 579-hit checklist as if it failed. This plan starts from
  that cleanup and asks which accepted bridges should now become domain
  architecture.
- Do not rename things only to make the words nicer. A cleanup must improve
  ownership, lifetime, API shape, or guardrails.
- Do not collapse explicit bridges back into globals.
- Do not remove user-facing compatibility surfaces such as scene/config key
  spellings, replay artifact schema, or command-line aliases without a separate
  compatibility plan.
- Do not combine every artifact removal into one giant PR. Each slice should
  have one owner bucket and one validation story.

## Design Rules

1. Prefer domain names over migration names: `PhysicsMaterial`,
   `BodySimulationLimits`, `ContactPolicy`, `WaterRenderStyleSettings`,
   `FluidForceSettings`, `RenderResourceContext`, `SceneRuntimeServices`.
2. Every retained bridge needs four labels: owner, reason, deletion condition,
   and checker budget.
3. If a bag mixes domains, split it before moving it deeper. For example, water
   visual style and fluid-force coefficients should not share one generic
   runtime snapshot if their owners and validation risks differ.
4. New code may depend on explicit contexts only when the context is narrower
   than the old global surface and borrows for a clear lifetime.
5. Store ownership beats forwarding. A compatibility facade can stay while
   callers migrate, but the authoritative data should move toward the domain
   store named by the plan.
6. Do not replace one vague object with another. A `RuntimeConfigSnapshot`
   renamed to `RuntimeSettingsSnapshot` is still debt unless its owner,
   lifetime, and consumers become clearer.
7. Update boundary checkers in the same slice that removes or accepts an
   artifact so regression cannot creep in quietly.

## Phase 0: Hard Kill List

Purpose: identify the small set of fake migration bridges worth deleting, not
produce an architecture anxiety inventory.

Tasks:

1. Create a report folder:
   `Agentic/Reports/YYYY-MM-DD/contrived-migration-artifacts/`.
2. Generate a source inventory from tracked files, not ad hoc directory globs:

   ```bat
   git ls-files SkullbonezSource tools Agentic | findstr /R "\.cpp$ \.h$ \.hpp$ \.inl$ \.hlsl$ \.py$ \.bat$ \.ps1$"
   ```

3. Use CodeGraph first for symbol-level source and caller/callee context:

   ```bat
   codegraph explore "RuntimePhysicsTuning RuntimeConfigSnapshot ForCompatibility Transitional bridge compatibility bridge active assets runtime config global service remediation"
   ```

4. Run focused text searches to seed the candidate table:

   ```bat
   rg -n "Runtime.*Tuning|RuntimeConfigSnapshot|ForCompatibility|Transitional bridge|Temporary compatibility|compatibility bridge|ActiveAssetSystem|CreateShaderFromActiveAssets" SkullbonezSource
   rg -n "\bCfg\(\)|\bGfx\(\)|GfxRayTracing\(\)|WorkerPool::Instance\(\)|Profiler::Instance\(\)" SkullbonezSource tools
   ```

5. Produce `contrived-migration-artifact-plan.csv` with one row only when the
   artifact passes all three tests:

   - current source preserves a bad dependency through a migration bridge,
   - the replacement can be named as a real domain API/type/owner,
   - the first deletion move is concrete enough for an implementation slice.

   Do not include rows just because a context is broad, a snapshot is large, or
   a facade could become messy later.

   | Field | Meaning |
   | --- | --- |
   | kill_id | Stable row id for follow-up slices |
   | rank | Deletion order, not abstract severity |
   | verdict | `kill` or `split`; avoid soft `monitor` rows |
   | artifact/file/line | Exact source anchor |
   | why_it_is_fake | One blunt reason the current shape is contrived |
   | current_proof | Current source evidence, not historical plan text |
   | exact_replacement | Concrete domain API/type/owner to build |
   | first_deletion_move | Small first implementation slice |
   | done_when | Deletion condition |
   | validation | Smallest validation command when source changes happen |

Validation:

- Documentation-only inventory: no repository validation required.

## Phase 1: Triage And Owner Decisions

Purpose: separate honest compatibility from contrived architecture.

Tasks:

1. Classify each candidate into exactly one decision:
   - `Keep`: already a domain concept with clear lifetime.
   - `Rename`: domain ownership is right, but the migration name is misleading.
   - `Split`: the artifact mixes domains and needs multiple settings/contracts.
   - `Replace`: use a real domain primitive or store-owned setting.
   - `Delete`: no longer needed after call sites move.
   - `Accept with sunset`: compatibility remains, but with a deletion condition.
2. Add a short decision note for each retained bridge:
   `Owner`, `Reason`, `Sunset`, `Guardrail`.
3. Compare the decisions against:
   - `Agentic/Plans/To_Eval/global-service-context-plan.md`
   - `Agentic/Plans/To_Eval/game-model-data-boundary-plan.md`
   - `Agentic/Plans/In_Progress/carmack-global-service-579-hit-remediation-checklist.md`
   - `Agentic/Reports/2026-06-29/carmack-phase-2-global-service-lifetime/global-service-hit-decision-table-final.md`
4. Reject any "rename only" proposal when the old data flow remains unclear.

Validation:

- Documentation-only triage: no repository validation required.

## Phase 2: Replace Physics Runtime Bags

Purpose: turn `GameModelRuntimePhysicsTuning` and similar config copies into
physics-owned concepts.

Tasks:

1. Split the existing fields by domain:
   - friction and drag into `PhysicsMaterial` or material/body descriptors,
   - angular velocity cap into `BodySimulationLimits`,
   - contact epsilon, terrain threshold, and restitution threshold into
     `ContactPolicy` or solver/contact settings.
2. Decide whether values are process defaults, scene settings, body settings,
   material settings, or solver settings before choosing storage.
3. Route default values from `EngineConfig` only at composition or scene setup
   boundaries. Deep contact/body code should receive settings from physics
   stores, body descriptors, or solver inputs.
4. Keep `GameModelCollection` as a facade only where render, replay, editor, or
   diagnostics still need the legacy model order. Do not add new model-order
   dependencies.
5. Remove `GameModelRuntimePhysicsTuning` once all consumers use the new domain
   primitives, then ratchet `tools/check_runtime_boundaries.py` if any budget
   changes.

Validation:

- Physics/body/contact behavior changes: `tools\validate_physics.bat`.
- Broad body-store or `GameModelCollection` migration: `tools\validate_physics.bat`;
  add `tools\validate_perf.bat` if hot-loop storage or iteration changes.
- If broad runtime setup changes are unavoidable: `tools\validate_full.bat`.

## Phase 3: Split World And Render Runtime Snapshots

Purpose: audit `RuntimeConfigSnapshot`-style bags that mix render style,
resource rebuild inputs, and physical world force settings.

Tasks:

1. For `WorldEnvironment::RuntimeConfigSnapshot`, split values into likely
   owners:
   - `WaterRenderStyleSettings`: ordinary/cinematic water style, wave height,
     perturb strength.
   - `FluidForceSettings`: density, angular drag multiplier, gravity-coupled
     force parameters.
   - `WaterMeshBuildSettings`: far plane and terrain footprint values used to
     size generated water meshes.
2. Keep render-resource borrows explicit:
   `AssetSystem&` plus `IRenderResourceFactory&` are rebuild-time services, not
   owned environment state.
3. Verify whether per-frame cinematic overrides should remain inputs rather
   than copied environment state.
4. Replace generic `Config()` accessors with domain accessors only if that
   clarifies call sites. Otherwise keep private helpers and improve naming.

Validation:

- Water render/resource changes: `tools\validate_dx12_renderer.bat`.
- Fluid force or gravity behavior changes: `tools\validate_physics.bat`.
- Mixed world/render behavior changes: `tools\validate_full.bat`.

## Phase 4: Retire Asset And Render Compatibility Bridges

Purpose: delete global-looking asset/render bridges only after callers have
real resource contexts.

Tasks:

1. Inventory every `ActiveAssetSystem()` and `CreateShaderFromActiveAssets()`
   caller.
2. Group callers by real owner:
   - render helper/text/shadow resources,
   - physics debug diagnostics,
   - world terrain/skybox/water resources,
   - UI render resources.
3. Introduce or reuse the narrowest explicit resource contract:
   `AssetSystem&`, `IRenderResourceFactory&`, `IRenderCommandContext&`, or a
   subsystem-specific render-resource owner.
4. Delete active asset fallback paths once the last caller has an explicit
   asset/resource context.
5. Ratchet boundary checker allowlists and source classifications in the same
   PR.

Validation:

- Renderer resource creation or draw-path changes:
  `tools\validate_dx12_renderer.bat`.
- Tool/checker updates: run the changed checker directly, and use
  `tools\validate_fast.bat` when PR-bound.
- UI render path changes that touch diagnostics/profiler text may need
  `tools\validate_full.bat` if the runtime lifecycle is involved.

## Phase 5: Shrink Model Compatibility Access

Purpose: make every `*ForCompatibility()` caller move toward its real store or
service.

Tasks:

1. Inventory `MutablePhysicsModelsForCompatibility()` and
   `PhysicsModelsForCompatibility()` call sites by owner:
   runtime frame, editor mouse pickup, replay recorder/runtime, replay
   prediction helpers, diagnostics, and direct `Run` setup.
2. For each owner, decide whether it needs:
   - a physics command API,
   - a physics query API,
   - a render-instance query,
   - a replay snapshot/query API,
   - an editor selection/manipulation API.
3. Move one owner at a time. Do not make a broad replacement that still returns
   `std::vector<GameModel>&` under a different name.
4. Keep compatibility handles only while model-index order is still required
   for deterministic replay or renderer projections.
5. Delete the compatibility accessor only after all owner buckets are off the
   model vector.

Validation:

- Physics-facing movement: `tools\validate_physics.bat`.
- Replay behavior or runtime frame lifecycle: `tools\validate_full.bat`.
- Hot-loop iteration changes: add `tools\validate_perf.bat`.

## Phase 6: Guardrail Against New Contrived Artifacts

Purpose: stop new migration-shaped bags from appearing without review.

Tasks:

1. Extend `tools/check_runtime_boundaries.py` or add a companion architecture
   checker that flags suspicious names in normal source paths:
   `Runtime.*Tuning`, `RuntimeConfigSnapshot`, `ForCompatibility`,
   `Transitional bridge`, `Temporary compatibility`, `Active*System`.
2. The checker should not fail on every match immediately. Start with a
   reviewed allowlist containing owner, reason, and deletion condition.
3. For new matches, require one of:
   - a linked plan/checklist row,
   - a domain-specific owner name,
   - an explicit public compatibility reason,
   - a short sunset note.
4. Ratchet the allowlist down as phases remove artifacts.

Validation:

- Checker-only changes: run the checker directly and then
  `tools\validate_fast.bat` at PR gate.
- Source behavior changes still use the validation map for the touched area.

## Acceptance Criteria

- `contrived-migration-artifact-plan.csv` exists under the report folder and
  stays short enough to act on. It should be closer to a kill list than a
  catalog.
- Every row has current-source proof, exact replacement, first deletion move,
  deletion condition, and validation mapping.
- `GameModelRuntimePhysicsTuning` is either removed in favor of physics-domain
  primitives or explicitly retained with a short sunset condition tied to
  physics store migration.
- `RuntimeConfigSnapshot`-style bags are either split into domain settings or
  documented as private one-owner snapshots with no cross-subsystem leakage.
- `ActiveAssetSystem()` and `CreateShaderFromActiveAssets()` have a deletion
  path through explicit asset/render-resource contexts.
- `*ForCompatibility()` model accessors have owner-by-owner migration rows and
  no new call sites are added without a linked reason.
- A checker or allowlist prevents new migration-shaped artifacts from being
  introduced silently.
- Each source-removal slice reports the smallest validation command from this
  plan and the matching `AGENTS.md` validation map.

## Suggested First Slice

Start with `GameModelRuntimePhysicsTuning` because it is small enough to reason
about and central enough to prove the cleanup style.

1. Use the Phase 0 kill list row for `GameModelRuntimePhysicsTuning`.
2. Draft the specific replacement design for:
   `PhysicsMaterial`, `BodySimulationLimits`, and `ContactPolicy`.
3. Use CodeGraph to inspect all callers and downstream consumers before
   editing.
4. Move only the friction/drag/limit/contact fields in one physics slice.
5. Run `tools\validate_physics.bat` before PR-bound handoff.

This gives the cleanup a concrete pilot without turning the whole architecture
backlog into one overnight surgery.
