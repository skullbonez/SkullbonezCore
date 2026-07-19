# ImGui + Tracy E16 Hardening Checkpoint

Date: 2026-07-19

Branch: `nightrunner-18th-july`

Campaign task: E16 — Harden persistence, styling, scaling, automation, and
long-session use

Result: complete

## Outcome

E16 adds a fixed version-1 preference record for panel visibility and the three
bounded editor filters while Dear ImGui's versioned ini continues to own dock
sizes and placement. Both files are development-only, ignored at the repository
root, and never enter authored scenes, replay artifacts, baselines, or goldens.
Unknown keys are forward-compatible. Malformed records recover defaults; stale
layout/topology identity keeps bounded filters but restores the current panel
mask and requests the deterministic dock reset.

The editor now applies one coherent dark engine-editor palette with explicit
selected, disabled, navigation, docking-preview, warning, and error contrast.
Style values are rebuilt from 1.0 before each DPI scale, preventing cumulative
rounding drift. The existing non-modal shell, tooltips, keyboard navigation,
minimum-width priority rules, and stable panel identities remain intact.

Automation gained fixed-capacity, validation-only commands for panel visibility,
layout reset, stable-ID focus, DPI scale, client resize, exact scene load, and
exclusive development-surface selection. Assertions cover the selected surface,
mutual visibility, panel mask, layout/focus counters, DPI, descriptor high-water,
viewport recreation count, and preference recovery. A typed editor replay scrub
uses the same operator-command arbitration as a real ImGui widget.

Legacy remains the default. `--dev-ui imgui` is still explicit; no path enables
both surfaces. The Legacy stress harness is inert while ImGui is selected, scene
loads cannot force Legacy visible, and the inactive Legacy replay pointer surface
cannot reset replay state after an ImGui transport command. Atomic hot swaps hide
the source before showing the target.

No authored scene, engine config, physics baseline, replay golden, or visual
golden changed. The new JSON file is a deterministic interaction-validation
script only.

## Deterministic editor matrix and visual review

`SkullbonezData/interaction/imgui_editor_stress.json` runs 180 frames containing
36 scheduled actions and 23 assertions. It covers:

- reset and stable-ID panel focus;
- 1024x720 at 1.5 DPI, default size, and 2560x1080 ultrawide;
- sequential Legacy and ImGui states plus eight additional hot swaps;
- exact transition to the 200-body prediction scene;
- typed replay scrub to historical tick 0;
- panel hide/show churn, final reset, and bounded resource assertions.

The final report is `ok=true`: 36 actions, 23/23 assertions, ImGui descriptor
high-water 2/16, and three viewport recreations. The following native captures
were inspected with the screenshot-driven UI QA skill and showed no overlap,
clipping, bleed, unusable hierarchy, or modal/focus trap:

- `TestOutput/interaction/imgui_e16_default.bmp`;
- `TestOutput/interaction/imgui_e16_minimum.bmp`;
- `TestOutput/interaction/imgui_e16_ultrawide.bmp`;
- `TestOutput/interaction/imgui_e16_legacy.bmp`;
- `TestOutput/interaction/imgui_e16_final.bmp`.

The formal UI-stress gate launches its Legacy stage in a separate default-mode
process, then launches the explicit ImGui matrix. The Legacy process completed
without drawing an ImGui frame; the ImGui process proved both hot-swap directions
without simultaneous visibility.

## Long-session and Tracy evidence

An exploratory Automation launch used `SKORE_TRACY_MODE=standard`, explicit
`--dev-ui imgui`, and graphics stress. Two independent pinned Tracy attachments
completed cleanly:

| Capture | Capture time | Frames | Zones | Artifact bytes |
|---|---:|---:|---:|---:|
| `attach_1.tracy` | 1.216 s | 304 | 14,459 | 98,074 |
| `attach_2.tracy` | 1.212 s | 291 | 13,473 | 93,698 |

The engine ran for 50.43 s and 12,747 frames, completed 263 exact scene loads,
then was closed by exact PID. Descriptor churn passed at baseline/current 20,
131/131 requested/acknowledged textures, and high-water 23. The ImGui renderer
remained bounded. Private working set rose from 282,013,696 bytes during warmup
to a repeated 292,519,936-byte plateau; the final sample was 292,524,032 bytes.
Static and transient descriptor capacities remained fixed, upload flush/drop
counts remained zero, stderr was empty, and DX12 validation reported zero
messages. Artifacts are under
`TestOutput/validation/imgui_tracy_e16/`.

An earlier 131.17-second exploratory ImGui graphics-stress run also passed
descriptor churn (`20 -> 20`, 131/131 acknowledgements, high-water 23), zero
DX12 messages, and empty stderr through 2,023 scene loads before exact-PID stop.
These exploratory launches supplement rather than replace the formal gates.

## Blockers and corrections encountered

- The first matrix showed that the existing scene-authored UI stress harness
  could re-show Legacy while ImGui was selected. E16 now makes Legacy stress
  actions inert under ImGui and keeps scene-load activation from re-enabling it.
- The first typed replay scrub was immediately reset by the hidden Legacy replay
  pointer pass. The workspace now receives an explicit active-surface fact and
  cancels only disposable Legacy gesture capture when Legacy is inactive.
- A 0.25 normalized scrub target correctly resolved to the live-present marker
  for this short capture. The validation target changed to 0.0 so the assertion
  proves a genuinely historical paused sample.
- The first formal `validate_fast` exposed a Profile-only scope error because an
  Automation-only packet was merged outside its build guard. The merge now has
  the matching diagnostics guard; the final rerun and all later configurations
  pass.
- One exploratory Debug whole-suite invocation reproduced two known floating-
  boundary physics cases. Both passed individually in Profile, and the mandatory
  Profile CPU umbrella plus byte-exact Debug physics gate passed from final
  source. No physics code or baseline was changed for E16.

None of these remained a completion blocker.

## Comment audit checklist

The final touched source-bearing inventory was generated from `git diff`, then
reconciled against `git ls-files`: 16 tracked files, 16 inspected, 0 deferred,
0 unchecked. This report is the touched-file checklist. Headers and nearby
comments cover benign preference migration, fixed automation capacity, exclusive
surface ownership, typed replay ordering, native resize units, and sequential
stress behavior.

- [x] `SkullbonezSource/Runtime/DevelopmentTools/ImGuiEditorLayoutPolicy.cpp`
- [x] `SkullbonezSource/Runtime/DevelopmentTools/ImGuiEditorLayoutPolicy.h`
- [x] `SkullbonezSource/Runtime/DevelopmentTools/ImGuiEditorOwner.cpp`
- [x] `SkullbonezSource/Runtime/DevelopmentTools/ImGuiEditorOwner.h`
- [x] `SkullbonezSource/Runtime/InputFrame.cpp`
- [x] `SkullbonezSource/Runtime/InputFrame.h`
- [x] `SkullbonezSource/Runtime/InputFrameExecution.cpp`
- [x] `SkullbonezSource/Runtime/InteractionAutomationController.cpp`
- [x] `SkullbonezSource/Runtime/InteractionAutomationController.h`
- [x] `SkullbonezSource/Runtime/Replay/ReplayCoordination.h`
- [x] `SkullbonezSource/Runtime/Replay/ReplayScrubberTools.cpp`
- [x] `SkullbonezSource/Runtime/RunFrame.cpp`
- [x] `SkullbonezSource/Runtime/RuntimeStressController.cpp`
- [x] `SkullbonezSource/Runtime/RuntimeValidationHarness.h`
- [x] `SkullbonezTests/TestOwnerRequestQueues.cpp`
- [x] `tools/validate_ui_stress.bat`

## Final-source gates

The Codex PTY was the available visible console and output was mirrored to the
listed logs.

| Command | Result | Wall time | Evidence |
|---|---|---:|---|
| focused preference test | PASS; 1 case/24 assertions | targeted | task transcript and final CPU umbrella |
| `tools\validate_fast.bat` first run | Found and localized the Profile-only guard error described above | 29.25 s | `TestOutput/validation/imgui_e16/validate_fast.log` |
| `tools\validate_fast.bat` final run | PASS; 311 cases/21,874 assertions, zero warnings | 61.35 s | `TestOutput/validation/imgui_e16/validate_fast_rerun.log` |
| `tools\validate_ui.bat` | PASS; visual/blur suite and zero DX12 errors | 54.77 s | `TestOutput/validation/imgui_e16/validate_ui.log` |
| `tools\validate_ui_stress.bat` | PASS; separate Legacy stage and 36-action/23-assertion ImGui matrix | 68.92 s | `TestOutput/validation/imgui_e16/validate_ui_stress.log` |
| `tools\validate_perf.bat` | PASS; allocation guard, selected-ball path, scale matrix, and ready builds | 107.28 s | `TestOutput/validation/imgui_e16/validate_perf.log` |
| `python tools\check_allocation_policy.py --repo .` | PASS; 405 files and zero allowlist errors | 9.26 s | `TestOutput/validation/imgui_e16/check_allocation_policy_repo.log` |
| `tools\validate_full.bat` | PASS; all CPU/coverage lanes, Automation/replay, zero-error DX12 comparisons, and byte-exact 44,401-line physics oracle | 143.58 s | `TestOutput/validation/imgui_e16/validate_full.log` |
| `tools\validate_build.bat Release` | PASS; zero warnings/errors | 44.14 s | `TestOutput/validation/imgui_e16/validate_build_release.log` |
| Release exact-token scan | PASS; 7 E16 development-tool tokens, zero hits | 0.14 s | `TestOutput/validation/imgui_e16/release_token_scan.log` |
| `git diff --check` | PASS | <1 s | final source inspection |

The full gate's DX12 manifest is
`TestOutput/validation/dx12_renderer/20260719T004031Z/manifest.json`; all three
committed comparisons passed and InfoQueue reported zero errors. E16 changed no
render-backend or shader source, so the conditional mandatory one-minute DX12
stress gate did not apply. No SkullScope trace was used.

## Continuation

E16 is complete. E17 now owns the final separate-mode evidence, control
disposition, measured overhead, independent ownership review, final gate matrix,
and owner playtest handoff. E14-E15 remain validated checkpoints until the
binding post-E17 Physics P1 artifact transition is reconciled.
