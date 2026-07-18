# Scene Controller Decomposition Round 2 — Split The Ratified Aggregate

Date: 2026-07-18
Status: Active — 5/7 tasks
Branch: `nightrunner-17th-july`
Impact area: `SkullbonezSource/Runtime/Scene/*`, `Runtime/RunFrame.cpp`,
`Runtime/RunInput.cpp`, `Runtime/InputFrame.cpp`, UI navigation consumers,
project filters
Owner: runtime scene subsystem

## Problem And Evidence (measured 2026-07-18 at `nightrunner-17th-july` tip, 06a17ff31)

The round-6 `scene-controller-ownership-decomposition` campaign closed on
2026-07-18 after real coupling reduction (navigation state moved to the
UI-owned `SceneNavigationModel`, automation gates moved to the validation
harness, the `.inl` splice and reciprocal gate-tracker dependency deleted).
The 2026-07-18 hostile review accepts that work but finds the ratified
endpoint is still a multi-domain aggregate:

- One class still directly owns `PhysicsEngine`, `CameraCollection`,
  `WorldEnvironment`, `SceneTerrain`, `SceneEntityStore`,
  `RenderInstanceStore`, the scene queue, and the request ring
  (`SkullbonezSource/Runtime/Scene/SceneController.h:404-414`), behind a
  public surface of roughly 120 member functions in a 417-line header.
- Navigation *policy* methods remain on the scene owner even though the
  state moved: `AdjacentCinematicModeBrowserIndex` (five parameters of UI
  policy), `LoadAdjacentSceneFromBrowser`, `LoadSceneFromBrowserIndex`
  (`SceneController.h:336-344`). The state relocation left the
  decision-making behind.
- Out-of-domain edges the round-6 plan never targeted:
  `ApplyWaterHeightControl( bool pageDown, bool pageUp, float dt )` puts raw
  key semantics on a scene owner (`SceneController.h:304`);
  `NotifyAudioContact` (`SceneController.h:273`); the ragdoll topology query
  family `IsSimpleRagdollPart` / `IsSimpleRagdollTorso` /
  `RagdollRootModelIndexForPart` / `TryFindSimpleRagdollPart` /
  `GatherGroupMemberIndices` (`SceneController.h:231-235`); and
  `CollectMemoryStats` (`SceneController.h:240`).
- The four load participant structs
  (`SceneLoadPolicyInputs` / `SceneLoadHostParticipants` /
  `SceneLoadInteractionParticipants` / `SceneLoadPresentationParticipants`,
  `SceneController.h:149-185`) still deliver ~20 owner references through
  every load. The shape is disciplined (synchronous, never retained), but
  the authority footprint is unchanged: scene load still requires the whole
  object graph to attend.

## Goal

`SceneController` becomes a scene lifecycle coordinator (queue, request
ring, load transaction sequencing, lifecycle events) composing a distinct
concrete world-state owner (entity store, physics lifetime, cameras,
terrain, world settings, render snapshot). Browser policy finishes its move
to the UI/navigation owner; input-edge and audio-notification edges move to
their consumer boundaries; ragdoll/grouping queries land on the store owner
that holds the data. The load participant count shrinks because fewer
owners genuinely participate, not because bags merge.

## Non-Goals

- No behavioral change: scene load order, physics stepping, replay
  recording, and presentation output stay byte/pixel-identical throughout.
- No baseline, golden, screenshot, or coverage-floor refresh.
- No `SimulationController`, unified entity registry, or change to
  `PhysicsSceneObjectId` identity policy (2026-07-11 binding ruling).
- No universal context bag, `*Services` type, callback pack, or forwarding
  facade — round-6 closure vocabulary applies verbatim.
- The four-phase participant convention itself is not the target; only the
  breadth of what participates.

## Tasks

- [x] S0 — Census refresh and split map. Re-inventory every public method
  and member at the current tip, classify each into lifecycle vs world-state
  vs relocate-out (navigation policy, water input, audio notify, ragdoll
  queries, memory stats), and produce the two-owner target map with the
  post-split load participant list. Owner ratifies map and branch before any
  edit. Evidence: dated census under `Agentic/Reports/`. Gate: none
  (documentation).
- [x] S1 — Finish the navigation-policy move. Relocate
  `AdjacentCinematicModeBrowserIndex`, `LoadAdjacentSceneFromBrowser`,
  `LoadSceneFromBrowserIndex`, and `LoadDemoSceneFromUI` decision logic to
  the UI-owned `SceneNavigationModel`, leaving the scene owner a value-only
  `SceneLoadRequest` submission boundary. Gate: `validate_full`.
- [x] S2 — Move the input and audio edges. `ApplyWaterHeightControl`
  becomes a typed world-settings command issued by the interaction owner
  (no key-name parameters below the input boundary); `NotifyAudioContact` /
  `NotifyFixedContact` become bounded post-step event outputs consumed by
  the audio/presentation owners. Gate: `validate_full`.
- [x] S3 — Re-home grouping/ragdoll queries and memory stats onto
  `SceneEntityStore` (which owns the behavior-group data), exposing them to
  former callers through the store accessor; delete the controller relays
  rather than forwarding. Gate: `validate_full`.
- [x] S4 — Extract the concrete world-state owner per the S0 map (working
  name decided at S0; a domain noun, not `*State`/`*Context`). It owns the
  entity store, physics engine lifetime, cameras, terrain, world settings,
  and render instance store as one scene-lifetime unit with typed
  boundaries. `SceneController` composes it; no reach-back, no duplicate
  accessors kept "for compatibility". Gate: `validate_full` plus
  `validate_physics` (physics ownership plumbing moves).
- [ ] S5 — Shrink the load surface against the split. Each load phase now
  names only the owners that phase genuinely uses; participants that became
  internal to the world-state owner leave the public structs. Record the
  before/after participant counts. Gate: `validate_full`; replay-adjacent
  load-path edits also run the one-invocation
  `tools\validate_replay_visual_fidelity.bat` per MASTER rule 11.
- [ ] S6 — Independent ownership review and closure. One review over the
  logical module (both owners, all TUs, participants, relocated consumers)
  under the `AGENTS.md` god-object closure rule; any credible finding
  reopens its task. Final gate: `validate_full`. Update MASTER-PLAN,
  SessionState, and delete this plan on closure.

## Dependencies And Decisions

- Runs before `naming-and-identity-debt` (both touch `Scene/*`; renames
  rebase on this split).
- S0 owner decisions: world-state owner name; whether `StepPhysics` stays a
  controller sequencing call or moves to the world-state owner; whether
  `CollectMemoryStats` aggregates on the store or a diagnostics boundary.
- S0 ruling (2026-07-18): the concrete owner is `SceneWorld`; `StepPhysics`
  moves to it; grouping/name queries move to `SceneEntityStore`; aggregate
  memory accounting uses a diagnostics boundary over const owner views. Reuse
  branch `nightrunner-17th-july`. Evidence:
  `../../Reports/2026-07-18/scene-controller-round-2-census.md`.
- S1 evidence (2026-07-18): browser, demo, and cinematic navigation policy now
  lives on `SceneNavigationModel`; the model borrows `SceneRuntime` only while
  producing value `SceneLoadRequest` records, and `SceneController` retains no
  navigation relay. Two focused cases passed 16/16 assertions. The touched-file
  comment audit inspected 9/9 source-bearing files with zero deferred. Allocation
  policy self-test and repository scan passed (`scanned=377`,
  `allowlist_errors=0`), and the final `tools\validate_full.bat` gate passed in
  161.165s: every CPU lane, Automation/replay smoke, DX12 validation and captures,
  and byte-exact physics passed. No baseline or golden was refreshed. Log:
  `TestOutput/validation/agent_logs/scene_round2_s1_validate_full_final_stdout.log`.
- S2 evidence (2026-07-18): `InputRouter` now emits a typed
  `FluidSurfaceAdjustment` in world meters per second; `WorldEnvironment`
  applies it without seeing Page Up/Page Down vocabulary. `StepPhysics` returns
  the physics owner's fixed-capacity fixed-contact span, while the bounded
  `ContactAudioService` decisions and solver output are consumed directly by
  `RenderInstanceStore` in live and replay target order. All three
  `SceneController` contact relays are deleted. The focused command case passed
  4/4 assertions, the allocation scan passed with `allowlist_errors=0`, and the
  touched-file comment audit inspected 12/12 source-bearing files with zero
  deferred. Final `tools\validate_full.bat` passed in 165.228s (287/287 tests,
  all coverage floors, Automation/replay smoke, zero DX12 validation errors and
  accepted captures, byte-exact physics). The single
  `tools\validate_replay_visual_fidelity.bat` invocation passed in 433.672s with
  2,401 ticks, 200 moved wall bricks, 187 toppled bricks, one presented cascade,
  durable saved/loaded proof, and all false-pass controls. No baseline or golden
  was refreshed. Logs:
  `TestOutput/validation/agent_logs/scene_round2_s2_validate_full.log` and
  `TestOutput/validation/agent_logs/scene_round2_s2_replay_visual_fidelity.log`.
- S3 evidence (2026-07-18): behavior-group, ragdoll, group-member, and Debug
  name queries now live on `SceneEntityStore`; all matching `SceneController`
  relays are deleted. Aggregate memory accounting now lives at
  `SceneMemoryDiagnostics` and receives only synchronous const references to
  the concrete entity, physics, and render owners. The focused behavior-group
  case passed 12/12 assertions. The touched-file comment audit inspected 18/18
  source-bearing files with zero deferred, project filters passed 726/726, and
  allocation-policy self-test/repository scans passed (`scanned=379`,
  `allowlist_errors=0`) after moving the existing Debug-only name-table
  assignment exception to its concrete owner. `tools\validate_fast.bat` passed
  in approximately 74s with zero build warnings. Final
  `tools\validate_full.bat` passed in approximately 148s: 288/288 doctest cases
  and 21,438/21,438 assertions, all coverage floors, Automation/replay smoke,
  zero DX12 validation errors with accepted captures, and the 44,401-line
  byte-exact physics comparison passed. No baseline or golden was refreshed.
  Logs: `TestOutput/validation/agent_logs/scene_round2_s3_validate_fast.log` and
  `TestOutput/validation/agent_logs/scene_round2_s3_validate_full.log`.
- S4 evidence (2026-07-18): concrete `SceneWorld` now owns the entity store,
  physics engine lifetime, cameras, terrain, world environment, and render
  instance store. `StepPhysics`, replay topology repair/trim, presentation
  capture, and cross-store creation/deletion moved with that authority.
  `SceneController` composes exactly one world and exposes only `Scene()`;
  attached-camera, render-frame, and overlay helpers borrow `SceneWorld`
  directly instead of using lifecycle forwarding APIs. The touched-file
  comment audit inspected 45/45 source-bearing files with zero deferred.
  Project filters passed 727/727, allocation-policy self-test/repository scans
  passed (`scanned=380`, `allowlist_errors=0`), and
  `tools\validate_fast.bat` passed in 81.564s. Final
  `tools\validate_full.bat` passed in 135.419s: 288/288 doctest cases and
  21,438/21,438 assertions, all coverage floors, Automation replay/prediction
  smoke, zero DX12 validation errors with accepted captures, and byte-exact
  physics passed. The explicit `tools\validate_physics.bat` gate passed in
  52.074s. The single `tools\validate_replay_visual_fidelity.bat` invocation
  passed in 428.126s with 2,401 ticks, 200 moved wall bricks, 187 toppled
  bricks, one presented cascade, durable save/load proof, and every false-pass
  control. No baseline or golden was refreshed. Logs:
  `TestOutput/agent_logs/s4_validate_fast_20260718_final.log`,
  `TestOutput/agent_logs/s4_validate_full_20260718_rerun.log`,
  `TestOutput/agent_logs/s4_validate_physics_20260718.log`, and
  `TestOutput/agent_logs/s4_validate_replay_visual_fidelity_20260718.log`.
- Round-6 closure evidence
  (`Agentic/Reports/2026-07-17/scene-controller-ownership-closure.md`) is
  the baseline census; contradicting it requires the fresh S0 measurements.

## Acceptance

- The lifecycle owner's public surface drops materially (target: under 60
  public member functions, measured and recorded at S6, not enforced as a
  mechanical ratchet).
- Zero navigation-policy, raw-input, audio, or grouping-query methods
  remain on the lifecycle owner.
- Independent review records zero credible god-object, reach-back,
  forwarding-facade, or context-bag findings.
- All mapped gates pass from final source with zero baseline refresh.
