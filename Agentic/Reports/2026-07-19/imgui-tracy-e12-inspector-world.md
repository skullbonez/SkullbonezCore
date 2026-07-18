# ImGui + Tracy E12 Inspector and World Evidence

Date: 2026-07-19

Plan: `imgui-tracy-editor-campaign`, E12

Branch: `nightrunner-18th-july`

## Outcome

E12 is complete. The development editor now presents a contextual, read-only
Inspector beside a canonical World/Simulation authoring panel. Both surfaces
consume one bounded operator view and emit only typed owner commands. No ImGui
panel reaches back into `Run`, `GameModel`, physics storage, or renderer state.

The implementation changes six source-bearing files:

- `SkullbonezSource/UI/OperatorEditorExchange.h`
- `SkullbonezSource/UI/OperatorEditorExchange.cpp`
- `SkullbonezSource/Runtime/DevelopmentTools/ImGuiEditorOwner.h`
- `SkullbonezSource/Runtime/DevelopmentTools/ImGuiEditorOwner.cpp`
- `SkullbonezSource/Runtime/RunFrame.cpp`
- `SkullbonezTests/TestOwnerRequestQueues.cpp`

No authored data, committed baseline, replay golden, or physics query golden
changed.

## Contextual Inspector

`OperatorEditorInspectorView` is a borrowed-label/value snapshot sampled only
when the secondary editor surface is visible. Its state is explicit:

- `None`: no selection, with guidance to select in the Hierarchy or viewport.
- `Single`: stable scene identity plus contextual facts.
- `Mixed`: an explicit multi-selection state rather than a fabricated value.
- `Stale`: the selected scene identity no longer resolves.

The single-selection view groups Transform, Identity, Render, Physics, Audio,
and object-specific source/shape facts. It reports position, orientation,
linear/angular velocity, material and visibility state, mass/static/sleep facts,
contact-audio material, asset affiliation, and collider/group information. The
Inspector does not duplicate viewport transform mutation: E11 gizmos remain the
sole transform-authoring authority.

## Canonical World and Simulation Authoring

The World panel exposes the retained global authoring controls under five
sections:

- Simulation: fixed-step, time scale, and sleep policy.
- Population / seed: model count, seed, solver balls, and solver boxes.
- World forces / fluid: gravity, fluid height, and fluid density.
- Contact friction: terrain, object, and rolling friction.
- Tornado / environment force: enable plus radius, height, inward, swirl, and
  lift.

Ranges come from the existing canonical UI layout constants. E12 adds 19 typed
property command kinds and a fixed 24-row queue; invalid values are rejected as
recoverable operator-command errors.

## Preview and Commit Contract

Continuous widgets hold one `ImGuiEditorPropertyEditState`. Active drags emit
typed `Preview` values for local presentation only. `Preview` commands still
pass validation and arbitration, but owner projection deliberately performs no
mutation. Deactivation after a real edit emits exactly one `Commit`, which maps
to the existing `InGameUICommands` owner packet. Hiding the editor cancels the
active preview.

Legacy controls normalize to `Commit`. In `Both` mode, exact legacy/ImGui
duplicates are coalesced deterministically, so a shared control executes once.
The focused suite proves preview non-mutation, all 19 commit projections, invalid
seed rejection, complete legacy coexistence, and Inspector/World fingerprint
changes: 6 cases, 156 assertions.

## Legacy Authoring Disposition

| Legacy surface | E12 disposition |
|---|---|
| Scene | Demo, new, reset, default/load, and save remain in `Scene & Modes`; time scale moves to World. |
| Edit | Mode, place/static, 37 registered asset recipes, undo, and redo remain in the E10 left workflow; picking/gizmos remain in E11; contextual readout is the E12 Inspector. |
| Phys | Sleep, tornado enable and five tornado scalars, gravity, and all three friction controls are canonical World controls. Debug visual toggles, ray/projectile probes, and pipeline diagnostics are explicitly assigned to E13 Diagnostics. |
| Opt | Fixed step and time scale are canonical World controls; model population is under Population/seed. Visibility/render options remain with E11/E13 owners. |
| Ctrl | Seed, solver ball/box counts, fluid height, and fluid density are canonical World controls. |

The retained authoring controls have no unexplained duplicate in the ImGui
surface.

## Native Interaction Evidence

Command:

```text
Debug\SKULLBONEZ_CORE.exe --scene stacking --interactive on --dev-ui imgui --vsync off --replay off
```

Observed in the native editor:

- no-selection guidance rendered before entering Edit mode;
- selecting `mid #2` populated Transform, Identity, Render/lock, and source
  groups with the stable scene id;
- dragging World time scale from `10.00x` to `5.00x` updated the toolbar only on
  release, demonstrating the preview/commit boundary;
- the lower Tornado/environment-force section remained reachable by scrolling;
- the process closed normally with empty stderr;
- shutdown reported 35,509 completed frames/draws, 35,509 viewport captures,
  one viewport resource recreation, zero live descriptors, and descriptor
  high-water `2/16`.

## Validation

| Evidence | Result | Wall time |
|---|---|---:|
| Focused Debug core build | PASS | 16.10s |
| Focused Debug test build | PASS | 6.69s |
| Final focused operator exchange suite | PASS: 6 cases, 156 assertions | 9.06s including rebuild |
| `tools\validate_ui.bat` | PASS: formatting, Profile/Debug builds, zero DX12 errors, screenshot validation | 55.80s |
| `tools\validate_physics.bat` | PASS: standalone/runtime-handle smoke and byte-exact 44,401-line varied baseline | 57.28s |
| `tools\validate_full.bat` | PASS: CPU umbrella, coverage floors, Automation/replay, DX12 baselines, deterministic physics | 148.71s |
| `tools\validate_perf.bat` | PASS: zero gameplay allocation violations, absolute DX12 budgets, scale matrix | 109.26s |
| `python tools\check_allocation_policy.py --repo .` | PASS: 404 files scanned, zero allowlist errors | 8.99s |
| `tools\validate_build.bat Release` | PASS: 0 warnings, 0 errors | 46.31s |
| Release executable/object token audit | PASS: 8 tokens and 5 ImGui/Tracy objects absent | 0.31s |

The performance comparison printed non-gating noise/regression deltas for some
small markers, but the allocation guard, absolute budgets, and script exit gate
all passed. No performance baseline was refreshed.

## Comment Quality Audit

The comment-style audit inspected every touched source-bearing file: 6 checked,
0 deferred, 0 unchecked. Each file retains the required learning-header
sections. Local comments also document borrowed-label lifetime, preview
non-mutation, stale-selection hazards, and the visible-only sampling invariant.

## Remaining Blocker

Physics P1 remains isolated and unchanged. It still requires exact owner
approval for the one-process replay topology transition `199 -> 200` and the
mechanically derived `physics_query_varied.json` update. Neither artifact is part
of E12.
