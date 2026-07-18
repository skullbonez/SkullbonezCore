# Naming N3 Operator Command Seam Rulings

Date: 2026-07-18
Task: `naming-and-identity-debt` N3
Module: `SkullbonezSource/Runtime/OperatorCommandApplier.*`

## Ruling

The ratified `OperatorCommandApplier` name is accurate and the module remains a
single stateless command-application boundary. It owns no state, stores no
borrow, and receives only one-call contexts plus value command packets. N3 does
not introduce a bag, callback bridge, forwarding owner, or compatibility alias.

No function moves to a subsystem source file in this task. The only clean
domain seam identified by N0—the contact-audio value limits—is already applied
through `ContactAudioService` setters. Moving any remaining UI packet decoder
into a subsystem would make that subsystem depend on UI command vocabulary, or
would require a new multi-owner context abstraction outside this naming task.

## Command-Group Decisions

| Command group | Explicit owners used | Decision and reason |
|---|---|---|
| Contact audio | `ContactAudioService` | KEEP decoder here; owner setters already validate/clamp sound values and own initialization/sample operations. The helper only maps one-frame UI enums to those APIs. |
| Render device / VSync | `RuntimeRenderer`, optional `IRenderDeviceLifecycle` | KEEP; one accepted command must update both presentation policy and the live device when present. Neither owner should depend on UI packets. |
| Scene fixed step | `RunSceneState`, `SimulationSystem` | KEEP; toggling policy and resetting the accumulator is an atomic cross-owner application rule. |
| Run simulation / workers | `RunSceneState`, `RunSceneUIOverrideState`, `EngineConfig`, `WorkerPool` | KEEP; time scale, deterministic seed, and worker requests have distinct owners and result flags. Splitting would create a relay surface without reducing authority. |
| World water | `WorldEnvironment` | KEEP the UI decoder here; it translates partial UI fields and clamps them before using the existing domain setters. Moving it would add a UI dependency to the environment owner. The replay-neutral before/after value remains explicit. |
| Runtime presentation / cinematic | `RunDebugState`, scene state, render config, launch options, `RenderDefaultsStore` | KEEP; these helpers coordinate override bits, defaults intent, and presentation state across explicit borrows. Render/config owners remain UI-agnostic. |
| Physics sleep / friction | `SceneWorld`, `EngineConfig` | KEEP decoder here; the physics owner receives domain policy through existing APIs, while the command helper keeps live config and existing-body policy synchronized. |
| Tornado | `SceneWorld` physics plus `RuntimeRenderer` visual settings | KEEP; an accepted operator command may update simulation and presentation together. Moving it to either owner would grant authority over the other domain. |
| Camera mode | Value-only decode | KEEP as a pure protocol decoder; camera ownership, pointer transitions, scene normalization, and action logging remain at their existing owners. |

## Verification

- `RuntimeTuning` has zero occurrences in active source, production project
  metadata, or project-filter validation tooling.
- The focused Profile build passed with zero warnings and zero errors in
  13.146 seconds after resolving MSBuild through `vswhere`.
- The initial bare `msbuild` command did not start because it was absent from
  this shell's `PATH`; it made no repository or build-state change.
- Formal repository validation is deferred to the N3 pre-commit gate.
