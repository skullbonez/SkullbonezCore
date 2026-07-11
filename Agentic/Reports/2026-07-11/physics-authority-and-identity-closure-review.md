# Physics Authority And Identity Closure Review

Date: 2026-07-11
Plan: `Agentic/Plans/TODO/physics-authority-and-identity.md`
Outcome: clean after one blocking finding and the required repeat review

## Expected outcome

Physics owns all live body/collider mutation and identity. Scene creation and
deletion keep descriptor, body, collider, constraint, metadata, presentation,
and render rows paired. Persistent runtime identity uses handles or explicitly
typed row hints, with behavioral and broad-gate evidence.

## First adversarial review

- **Blocking:** `GameModelCollection::DestroySceneEntity` rebuilt surviving
  bodies from cold authored descriptors after physics had already compacted the
  live stores. A body moved only by simulation or replay restore could therefore
  teleport during unrelated entity deletion. The existing smoke did not expose
  this because its surviving body still matched its authored pose.
- **Fix:** removed the descriptor reload, retained the already-compacted live
  body/collider rows, and refreshed render projection only. The runtime handle
  smoke now seeds a live-only replay pose before deleting the other body and
  proves that pose survives compaction.

## Required repeat review

No blocking, non-blocking, or missing-evidence findings remain.

Deletion-proof searches return no production/test references to:

- `PhysicsBodyStateEdit`
- split `RegisterAuthoredCollider` / `UpdateAuthoredCollider`
- raw `TryGetAuthoredBodyDescriptor`, `UpdateAuthoredBodyDescriptor`, or
  `RefreshBodyFromDescriptor`
- `RuntimeInteractionCommand::modelIndex`
- `RuntimeInteractionGesture::modelIndex`
- bare attached-camera or picker model-index identity

The final call path is `GameModelCollection::DestroySceneEntity` →
`PhysicsEngine::DestroyAuthoredBody` → `PhysicsScene::DestroyAuthoredBody`.
Point joints retire before body-handle reuse; body and collider stores update
their moved-row maps; the scene/presentation/render owners perform the matching
swap-last operation; no surviving live body is re-imported from cold data.

## Validation evidence

- `tools\validate_runtime_interaction_policy.bat`: Debug and Release passed in
  9.5s; gesture tests assert body-handle slot and generation.
- `tools\validate_interaction_clicks.bat`: all five scenarios passed in 14.0s.
- `tools\validate_full.bat`: passed in 70.0s; 131/131 doctest cases and 2,814
  assertions, all standalone CPU lanes, zero-warning Profile/Debug builds, zero
  DX12 InfoQueue errors, matching DX12 captures, expanded standalone/runtime
  handle smoke, and the 44,401-line physics baseline byte-exact.
- Focused final handle smoke: creation atomic, deletion atomic, stale mutation
  rejected, surviving mutation accepted, moved handle preserved, joint removed,
  and live-only pose preserved.

## Comment audit

The repository comment-style audit inspected every touched source-bearing file:
45 checked, 0 deferred, 0 unchecked. All learning headers remain complete.
Local comments cover coordinated registration/deletion, swap-last row alignment,
joint lifetime, render-handle rewriting, authored/live mutation authority,
failure lanes, queued handle capture, and typed row-hint semantics.
