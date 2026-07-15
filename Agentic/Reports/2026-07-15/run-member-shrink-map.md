# Run Member And Include Shrink Map

Date: 2026-07-16
Branch: `nightrunner-15th-july`
Owner: runtime shell
Task: `run-member-and-include-shrink` T1-T3

## Current Measurements

At T1, `SkullbonezSource/Runtime/Run.h` is 198 lines and has 46 include
directives. The direct-member inventory has grown since the plan's dated
27-member observation: the current class declares 31 member rows in the normal
build, plus the conditional Automation interaction controller. The scoped
presentation/diagnostics residents are eight concrete values: contact audio,
live style, UI, debug state, graphics stress, and three visualizers.

The named heavy headers are currently direct includes: `UI.h`, `Text.h`,
`SkyBox.h`, and `TestScene.h`. T4 must prove they are no longer reachable
through `Run.h`, not merely remove their direct spelling.

## Final Owner Ruling

### Owner A: `RuntimeOverlayDiagnostics`

Members: `m_debug`, `m_broadphaseVisualizer`, `m_collisionVisualizer`, and
`m_physicsDebugVisualizer`.

These values form one debug-presentation domain. `RunDebugState` is the single
presentation policy record for HUD selection, visibility flags, and physics-
debug drawing; the three visualizers render the selected spatial-grid,
collision/sleep, and physics pipeline views. Their shared invariants are
presentation-only behavior, scene-reset policy, input-toggle routing, and
renderer-resource lifetime. The owner provides domain operations for startup/
scene policy, input toggles, frame-policy construction, scene reset, and post-
physics visualizer updates. Renderer construction receives one typed opaque
resource capability; the owner exposes neither individual visualizers nor its
mutable policy record and retains no back-reference to `Run`.

Current call positions that T2 must preserve:

| Member(s) | Current call sites | Required ordering |
|---|---|---|
| three visualizers | `Run.cpp:356-358` | Borrowed while `RuntimeRenderer` is constructed; owner must outlive renderer. |
| operator UI | `Run.cpp:424`, `492`, `512` | The UI remains an opaque cohesive `Run` owner; backend release, startup presentation policy, and Automation binding remain in their existing startup/shutdown positions. |
| UI, presentation policy, physics visualizer | `Run.cpp:632-637`, `788-793`; `RunFrame.cpp:1189-1194`, `1319-1324` | Scene load/advance/reset receives the same UI and overlay policy at the same call positions through separate cohesive owners. |
| UI/presentation policy/physics visualizer | `RunFrame.cpp:600`, `607`, `613` | Existing interaction, scene, and presentation frame-view slots carry only the narrow owner or value needed at each checkpoint; no new universal frame bag. |
| debug + three visualizers | `RunFrame.cpp:714-719` | Post-physics visualizer refresh remains after physics and before rendering. G/V/physics-debug toggle routing remains inside the existing input turn. |
| presentation policy + UI | `RunRender.cpp:47-157` | Text-only early exit, render-frame policy, replay collision policy, and final UI render submission keep exact order across separate owners. |
| debug | `Run.cpp:674`, `756-757`; `RunFrame.cpp:1062` | Replay restore/timeline and snapshot facts remain read-only presentation borrows. |

### Owner B: `RuntimeValidationHarness`

Members: `m_liveStyle` and `m_graphicsStress`.

Both values are explicitly CLI-enabled validation harnesses rather than game
presentation state. Live style watches a control directory and coordinates a
deterministic post-render capture; graphics stress drives deterministic
UI/scene/render churn and reports bounded progress. They share launch
configuration, scene-load restart behavior, per-frame validation sequencing,
capture/exit policy, and the invariant that ordinary launches pay no active
harness behavior. The owner must own those workflows with named operations
such as launch configuration, pre-simulation tick, capture pin/query, post-draw
capture, scene-load resume, and quit diagnostics. It must not become a generic
container with raw member getters or a callback route back into `Run`.

Current call positions that T3 must preserve:

| Member(s) | Current call sites | Required ordering |
|---|---|---|
| live style + graphics stress | `Run.cpp:448-458` | CLI launch policy is applied before initialise/execute exactly as today. |
| graphics stress | `RunFrame.cpp:543-548` | `WM_QUIT` diagnostics remain inside the message-drain exit branch. |
| graphics stress | `RunFrame.cpp:612`; downstream `RuntimeStressController` input turn | The existing presentation-view slot becomes owner B one-for-one; deterministic stress still runs in the input phase. |
| live style | `RunFrame.cpp:663-686` | File-watch tick remains after input and before physics; pending capture still pins presentation before render. |
| live style | `RunFrame.cpp:793-794` | Pending capture save remains in the post-draw capture phase. |
| graphics stress | `Run.cpp:635`, `791`; `RunFrame.cpp:1192`, `1322` | Scene load, advance, and reload resume stress with identical seed/action state and position. |

### Recorded Stay: `m_contactAudio`

`ContactAudioService` stays directly on `Run` for this plan. It is a cohesive
audio subsystem that consumes committed physics contacts after each step; it
does not share validation-harness policy or overlay-render state. Forcing it
into either owner would create exactly the cross-domain bag prohibited by the
plan, while extracting a third owner would exceed the owner ruling.

Its retained positions are constructor/config (`Run.cpp:369`, `598-613`),
launch policy (`Run.cpp:447`), scene load/reset (`Run.cpp:632`, `788` and
matching `RunFrame.cpp` load paths), the scene frame view (`RunFrame.cpp:609`),
physics-step enable policy (`RunFrame.cpp:934`), and committed post-step audio
submission (`RunFrame.cpp:1025-1038`). A future audio-owner plan may extract it
without reopening this two-owner campaign.

### Recorded Stay: `m_operatorUi`

`InGameUI` stays directly on `Run` as an opaque cohesive UI owner. The first
T5 review found that storing it inside `RuntimeOverlayDiagnostics` forced the
overlay owner to publish raw UI access to interaction, scene, automation, and
render clients, turning that owner into a component bag. Keeping UI separate
preserves its existing domain API and lets the overlay owner enforce its own
transaction and resource-capability boundaries. `Run.h` still sees only an
opaque `unique_ptr`, so the include-graph result is unchanged.

Its retained positions are startup construction/policy/automation binding,
interaction input, scene-load synchronization, final render submission, and
backend release. The UI's one process-lifetime allocation is explicitly
Startup-scoped and allowlisted. This is not a third extracted mid-level owner:
it is the pre-existing cohesive UI domain object retained by the composition
root because placing it in either new owner violated the no-bag rule.

## Boundary And Allocation Rules

- Owner A and the operator UI are each opaque to `Run.h` through one
  `unique_ptr`; owner A's debug policy and three visualizers remain by-value.
  Construction runs under an explicit Startup allocation scope, and each
  process-lifetime allocation has a complete owner/phase/reason/cap/wrapper-
  plan allowlist row. Owner B follows the same rule.
- Neither owner stores `Run*`, a host reference, callbacks, `void*`, a services
  bag, or a whole-frame view.
- Frame views replace moved member references with one owner reference
  one-for-one; they remain stack-only and non-retained.
- Renderer wiring uses one typed opaque overlay-resource capability assembled
  during construction. Runtime work crosses domain APIs and stack-only value
  transactions, not `Run::*` forwarding methods or raw member getters.
- Contact audio remains an explicit, documented remainder rather than being
  forced into an unrelated owner to satisfy a member-count metric.

## T1 Validation

Documentation-only grouping proposal. No repository validation required.

## T2 Extraction Evidence

`RuntimeOverlayDiagnostics` initially owned `InGameUI`, `RunDebugState`, and
the broadphase, collision, and physics debug visualizers. The T5 review later
corrected that provisional shape: UI is an opaque direct `Run` owner, while
`RuntimeOverlayDiagnostics` exclusively owns debug presentation policy and
the three matching visualizers. It owns real domain operations rather than
only storing fields:

- startup overlay/physics-debug launch policy;
- post-physics refresh of all three visualizers plus their matching scene
  validation gates;
- immutable render-frame policy sampling;
- stack-only copied presentation edits and one opaque renderer-resource
  capability at the narrow scene/replay/render boundaries.

The renderer is declared after UI, overlays, and validation owners and
therefore releases all borrowed resources before owner destruction. Frame
views carry separate narrow UI, overlay-owner, or sampled-value boundaries as
needed. Scene loading receives UI separately and mutates overlay policy through
a copied transaction. No `Run*`, callback pack, services bag, raw component
getter, or retained frame view was introduced.

Formal evidence on 2026-07-16:

- targeted Profile build: passed, zero warnings;
- `python tools/check_allocation_policy.py --self-test`: passed in 0.61 s;
- `python tools/check_allocation_policy.py --repo .`: passed in approximately
  9.8 s (`365` files, `0` allowlist errors); the same edit corrected startup
  allowlist paths left stale by the preceding Init split;
- `python tools/validate_project_filters.py`: passed in approximately 2.0 s,
  `709` project/filter items and `0` errors;
- final `tools\\validate_full.bat`: passed in 207.64 s, with all CPU lanes,
  zero-warning
  Profile/Automation/Debug builds, Automation replay/prediction smoke, DX12
  InfoQueue errors `0`, all three screenshot comparisons passing against the
  committed baselines, standalone/runtime-handle physics smoke, and the
  44,401-line varied physics CSV byte-exact.

Two earlier broad-gate attempts stopped before runtime validation: formatting
preflight named four changed files, then project-filter policy named the two
new owner files. A later build attempt found two stale stress-path aliases.
All three findings were corrected before the successful final gate; no
baseline, screenshot, golden, or authored-data file was refreshed.

Comment-quality audit: touched-file scope, 16/16 source-bearing files inspected
(15 C++ headers/implementations plus `tools/validate_project_filters.py`), zero
deferred or unchecked files, and no separate subsystem checklist required.
The two legacy `Mental model` headings in touched files were normalized to the
required `Summary` section; the new owner files include the full learning
header and nearby allocation/lifetime/invariant comments.

## T3 Extraction Evidence

`RuntimeValidationHarness` now owns `LiveStyleController` and
`GraphicsStressController`. Its domain operations preserve the former shell
checkpoints:

- startup control-directory configuration plus UI/graphics-stress launch
  normalization;
- live-style polling in the input phase and pending-capture sampling before
  physics fixes presentation to current solver poses;
- live-style screenshot consumption after render and UI submission;
- graphics-stress resume inside cold scene load without resetting its random
  stream or persistent counters;
- deterministic stress execution through the presentation capability slice and
  the final WM_QUIT counter summary.

The presentation frame view and cold scene-load APIs carry the cohesive owner,
while the stress executor takes the controller through one named narrow borrow.
Run has no direct live-style or graphics-stress member. Contact audio remains a
direct Run owner by the T1 ruling, and its scene-reset and post-physics step
positions are unchanged. The owner retains no Run pointer, callback pack,
services bag, or frame view.

Migration-name record: the runtime-shell plan owns `RuntimeValidationHarness`;
the name is required because both members are process-level CLI validation
controls rather than gameplay services. Delete or split this type only when
live-style and graphics-stress no longer share process startup/frame/exit
lifetime. Review evidence is the T3 call-position audit and broad gate below;
the plan-wide independent ownership review remains T5.

Formal evidence on 2026-07-16:

- targeted Profile build: passed in 12.85 s, zero warnings;
- `python tools/check_allocation_policy.py --self-test`: passed in 0.13 s;
- `python tools/check_allocation_policy.py --repo .`: passed in 8.56 s
  (`367` files, `0` allowlist errors);
- `python tools/validate_project_filters.py`: passed in 1.56 s with `711`
  project/filter items and `0` errors;
- `tools\\validate_full.bat`: passed in 200.61 s with all CPU lanes,
  zero-warning Profile/Automation/Debug builds, Automation replay/prediction
  smoke, DX12 InfoQueue errors `0`, all three screenshot comparisons passing,
  standalone/runtime-handle physics smoke, and the 44,401-line varied physics
  CSV byte-exact.

One targeted-build attempt found the final stress-driven scene-load call still
passing the nested controller instead of its owner. The compiler finding was
corrected before the successful build and formal gate. No baseline, screenshot,
golden, or authored-data file was refreshed.

Comment-quality audit: touched-file scope, 12/12 source-bearing files inspected
(11 C++ headers/implementations plus `tools/validate_project_filters.py`), zero
deferred or unchecked files. The two new owner files have the full learning
header and local allocation/order invariants; existing touched files retain
their complete headers and relevant lifetime/checkpoint comments.

## T4 Include-Graph Shrink Evidence

`Run.h` now includes only headers required to lay out its remaining value
members and opaque owners. Implementation files directly include the complete
types they use instead of relying on the composition-root header to publish
them transitively. No owner, heap allocation, call position, or runtime
behavior changed in this task.

Measured results on 2026-07-16:

- original plan baseline: 46 direct includes; immediate pre-T4 commit:
  40 direct includes; final: 23 direct includes (exactly half the original,
  and 17 fewer than the task-start state);
- a temporary MSVC `/showIncludes /Zs` translation-unit probe of `Run.h`
  reported 451 dependency rows at commit `4931f6473` and 424 after the edit,
  a reduction of 27 rows (6.0%);
- exact probe matches for `UI.h`, `Text.h`, `SkyBox.h`, and `TestScene.h`
  fell from four to zero;
- matched clean `Profile|x64` builds measured 31.19 s at the detached
  pre-T4 commit and 31.37 s after the final edit, a +0.18 s (+0.6%) delta
  that is within single-run timing noise. Both builds had zero warnings and
  zero errors.

The probe source existed only for the measurement and was removed before the
diff. The detached baseline worktree was verified clean and removed. Early
targeted builds correctly exposed implementation files that had depended on
the old transitive graph; those files now name their `Window`, frame-view,
view-model, UI, and text dependencies directly. A later targeted build exposed
the `EngineConfig` forward declaration previously supplied by `UI.h`; the
render-host header now declares that borrow explicitly.

Formal evidence: `tools\\validate_full.bat` passed in 209.75 s. All mandatory
CPU lanes passed; Profile, Automation, and Debug builds completed with zero
warnings and zero errors; the Automation replay/prediction smoke passed; DX12
reported zero InfoQueue validation errors and all three screenshots matched
the committed baselines; standalone and runtime-handle physics smokes passed;
and `physics_regression_varied.csv` matched the committed 44,401-line baseline
byte-exactly. No baseline, screenshot, golden, authored-data, or allocation
allowlist file changed.

Comment-quality audit: touched-file scope, 6/6 C++ source-bearing files
inspected, zero deferred or unchecked. All retain the required learning-header
sections and relevant local ownership/lifetime comments; the touched renderer
implementation's legacy `Mental model` heading was normalized to `Summary`.

## T5 Independent Ownership Review

The mandatory fresh review first found three credible closure blockers in
185.52 s:

1. `RuntimeOverlayDiagnostics` exposed raw UI and individual visualizer
   getters, so callers treated it as a component bag instead of a domain owner.
2. `RuntimeValidationHarness` exposed its graphics-stress controller and left
   stress-frame sequencing outside the owner.
3. `Run` still owned mutable presentation alpha and capture-pin policy across
   frames.

The reopened T2/T3 boundaries were corrected before closure. `InGameUI` now
remains an opaque cohesive `Run` owner with the recorded stay reason above.
Overlay mutation crosses a stack-only copied presentation transaction whose
commit publishes policy atomically; rendering receives one private typed
resource capability that only `RuntimeRenderer` can unpack. The validation
harness now executes its private graphics-stress controller through a named
domain operation. Presentation alpha and capture pinning are frame-local values
resolved explicitly and threaded through physics, audio, camera, and render
checkpoints rather than persistent `Run` policy.

The same independent reviewer repeated the whole logical-surface review after
remediation in 219.50 s and reported zero credible ownership blockers. It
verified that the presentation transaction's explicit commit/refresh points
protect every current nested edit site, the renderer capability cannot be used
as a general services bag, stress sequencing is harness-owned, and `Run` has no
new pointer/callback/`void*` reach-back or forwarding relay. The residual
non-blocking maintenance risk is that any future nested presentation edit must
follow the same commit/refresh protocol; the transaction intentionally carries
no version token because all current edits are synchronous on one thread.

Formal T5 remediation evidence on 2026-07-16:

- targeted Profile builds passed in 18.16 s and, after the transaction-order
  correction, 13.51 s, both with zero warnings and zero errors;
- allocation-policy self-test passed in 0.12 s and the repository scan passed
  in 8.64 s over 367 files with zero allowlist errors;
- `tools\\validate_fast.bat` passed in 53.33 s: formatting and project-filter
  checks, 202/202 doctests with 12,595/12,595 assertions, and zero-warning
  Profile/Debug builds;
- `tools\\validate_full.bat` passed in 116.00 s: all CPU lanes, zero-warning
  Profile/Automation/Debug builds, Automation replay/prediction smoke, zero
  DX12 InfoQueue errors, all three committed screenshot comparisons, both
  physics smoke lanes, and the 44,401-line varied physics CSV byte-exact.

Comment-quality audit: touched-file scope, 22/22 C++ source-bearing files
inspected, zero deferred or unchecked. Every file retains the required learning
sections and the edited owner/transaction/lifetime comments match the reviewed
boundaries. No baseline, screenshot, golden, or authored-data file changed.
