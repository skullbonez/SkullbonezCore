# Owner Fan-Out Reduction OF5 — Closure Census

Date: 2026-07-22

## Final Transaction Boundary

The OF0 graph exposed 18 borrowed scene-load owners/values plus four excluded
output consumers. The final `Load`/`ExecutePending` surface has ten
transaction inputs and no host, interaction-owner, Replay-owner, UI-owner,
validation-owner, timer, overlay, camera-controller, tool, or simulation-owner
bundle.

| Final input | Why it is present during the transaction |
|---|---|
| `EngineConfig&` | Authored capacity, physics, world, and render policy is resolved before population. |
| `RunLaunchOptions&` | CLI overrides change population and activation policy. |
| `const CinematicRenderConfig&` | Generated/demo style starts from the startup baseline value. |
| `const RunStartupState&` | Generated and authored capacity/worker fallbacks are pre-population values. |
| `AssetSystem&` | Terrain, scene assets, and registered source paths resolve during population. |
| `WorkerPool&` | Authored/startup worker policy commits before population jobs. |
| `DiagnosticsRuntime&` | The live parsed `AuthoredScene` configures automation, perf, and Debug physics-diagnostic state that affects pause/exit and population behavior. |
| `Dx12FrameOwner*` | Mandatory pre-mutation GPU drain; failure preserves the old scene. |
| `Dx12ResourceBuilder*` | Cold terrain/scene GPU resources are constructed during population. |
| `RuntimeRenderer&` | Scene render policy reset, resource lifecycle, and raytracing warm-up transact with activation. |

Detached `RunCameraState`, navigation, and `RunDebugState` values enter by
copy. Scene time and renderer name enter as scalar/vocabulary values. The
complete `RuntimeRenderBackendView` no longer crosses the load boundary;
render-device VSync is applied by the consumer boundary only after
`AfterSceneActivated`.

### Diagnostics survivor decision

- Owner: `DiagnosticsRuntime`.
- Reason: unlike its reset operations, authored automation/perf/physics
  configuration consumes the parsed scene while it is live and changes load
  decisions. Moving only the reset calls would not remove this owner edge.
- Deletion condition: publish a complete, bounded diagnostics configuration
  value from authored parsing, prove it covers pause/exit/perf/physics paths,
  and apply it at a diagnostics-owned frame entry without changing scene-load
  behavior.
- Review evidence: final independent ownership review plus focused and broad
  gates recorded below. This is the sole justified deviation from the OF0
  nine-input target; the plan acceptance ceiling is ten.

## Reactive Owner Graph

Lifecycle identity is one fixed `SceneLifecyclePacket` owned by
`SceneController`. Owners retain only their last applied generation. Existing
reactions cover timers, overlays, tools/editor state, attached camera,
interaction workspace, detached camera state, input context/cursor, Replay
clear and activation, validation gates/stress resume, and simulation pacing.

`SimulationSystem` is the ≤3-file closure witness: its header owns the
generation observer, its implementation applies the reset, and `RunFrame.cpp`
samples the packet at the existing physics-frame entry. It does not extend
`Load`, `ExecutePending`, `ApplySceneLoadConsumerOutputs`, any participant
record, or any frame view. Focused tests prove before-clear, once-per-generation,
same-generation, and next-generation behavior.

The centralized `ApplySceneLoadConsumerOutputs` remains the application point
for existing detached load payloads and already-migrated reactions. It is not
the integration path for a new reactive owner; the checklist in
`Agentic/Reference/runtime-reference.md` forbids extending that signature for a
reaction-only owner.

## Consumer Output Delta

`SceneLoadConsumerOutputs` contains 11 physical payload fields, down from 20:
UI activation, automation gates, navigation, presentation state, camera state,
two bounded world-change slots plus count, completed requests, window title,
navigation commit, and durable scene-browser refresh. Lifecycle identity,
capacity/override policy, validation apply flags, stress resume flags,
Inspect/cursor booleans, and editor-history policy are absent.

## Frame-View Delta

OF4 records the exhaustive matrix in
`owner-fanout-reduction-of4-frame-view-diet.md`. View-bearing helpers fell
25→21 and view-parameter slots fell 68→36. All retained root-view fields have a
real consumer; `RuntimeUiTextFrameFacts` is a copyable value aggregate and its
mutable editor output is a separate parameter.

## Acceptance Delta

| Measure | OF0 | Final |
|---|---:|---:|
| Scene-load owner/value inputs | 18 | 10 |
| Excluded output consumers | 4 | Value application only; no load participant edge |
| `SceneLoadConsumerOutputs` fields | 20 | 11 |
| View-bearing helpers | 25 | 21 |
| View-parameter slots | 68 | 36 |
| Files for a reaction on an already integrated owner | 5+ | ≤3 |
| Callback/subscriber/context-bag/`void*` seams | 0 | 0 |

## Review And Validation

- Independent rubber-duck review: clear after remediation, with no blocking
  findings. It independently confirmed the 10-input transaction, exact DX12
  authority, ≤3-file Simulation witness, and absence of callback/context-bag,
  downward Replay, allocation, or Replay-growth regressions. Residual risk is
  the broad existing composition surface, which the new checklist forbids
  extending for reaction-only owners. Review time: 7.3 minutes.
- Focused Profile build: pass, zero warnings.
- Focused Simulation lifecycle tests: 6 cases / 177 assertions pass.
- Dependency-direction and Replay downward-include proofs: pass with no rows.
- Added-source static scan: no callback, `void*`, exception, heap, reserve, or
  STL growth path.
- Comment audit: 14 touched source-bearing files checked, zero deferred or
  unchecked files.
- `tools\validate_full.bat`: pass in 99.2 s at the final tip; mandatory CPU
  umbrella, five runtime lanes, zero DX12 validation errors, accepted images,
  and byte-exact physics all pass. Output is retained in
  `TestOutput/validation/agent_logs/of5_validate_full_final.log`.
- `tools\run_graphics_stress.bat 1`: pass in 61.0 s; PID 61368 completed the
  bounded one-minute DX12 run and was stopped by the PID-scoped timeout without
  a crash. Output is retained in
  `TestOutput/validation/agent_logs/of5_graphics_stress_final.log`.
- Non-stopping external blocker retained from OF2: replay visual-fidelity
  provenance expects config SHA `83401df03cb6e212a6a74a38e815fc550d57aa983fc9b792c2c8f4e5c784a3f4`
  while the repository produces
  `bd0bb719aad7231cf500ca9a61af7d2f017e557b1b18b7de82df7eb93a3b5d93`.
  No config or golden metadata is changed by this plan.
