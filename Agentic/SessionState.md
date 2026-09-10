# Session State

Date: 2026-09-09
Branch: `codex/unified-ui`
Status: Unified UI complete at 8/8; portfolio 146/154. Closure is committed on this branch.

## Vertical dock navigation and title - 2026-09-10

The shared header now offers the camera selector, Solver Lab and a Full Screen /
Docked Interface switch. Its title is `Skullbonez Core - <scene basename>`;
names longer than 20 characters gain `...`. Collapsed Scene rails show rotated
EDITOR, REPLAY and CAUSES labels. Solver Lab uses the same treatment for
CONTROLS and DIFFERENCES. Tabs open independently, and panel choices survive
switching to the full-screen game and back. Solver Lab retains its pinned
header and top-right exit.

The bottom-right Tools tab replaces Details. Dragging upward opens the drawer
from zero height; its top edge supports further resizing. Clicking toggles the
remembered size and selected tool. Native capture prevents the drag from
becoming world input. Drawer geometry leaves positive scene space at tiny
client sizes. Rotated labels use baked font advances and bounded draw storage.

Validation: `validate_fast` passed for this task's diff from d659cdc96, including
994 unit cases / 2,745,756 assertions and compiler-backed source checks. The
earlier full-branch source scan also passed; its filter finding for the icon
resource was repaired. Profile and Automation build cleanly. Native navigation,
header, diagnostics and scrubber checks pass in `TestOutput/skarness/docked-title-final`,
`docked-header-complete`, `docked-diagnostics-complete` and `docked-scrubber-complete`.
Screenshots confirm full and compact Scene/Lab labels and the branded title.

The isolated renderer gate reports zero DX12 validation errors, but its three
committed references still show the old always-visible header and Details tab.
Inspection and pixel comparison found no scene differences above 1/255 outside
the top 100 and bottom 28 pixel UI bands. The isolated UI gate reaches the same
two old floating-window containment failures documented below; all other image
and timeline checks pass. No golden baselines were replaced. Evidence:
`TestOutput/docked-final-gates.log`, `docked-renderer-region-review.log`,
`docked-final-renderer-isolated.log` and `docked-final-ui-isolated.log`.

## Persistent Solver Lab header and exit - 2026-09-10

Solver Lab now keeps its header visible in both Canvas and Editor, during
loading and playback. A top-right cross exits to the full-screen Scene using
the same return operation as Escape. The comparison viewport starts below the
header so A/B badges remain visible. Compact labels and title clipping keep
the exit usable at 320x240. Leaving during loading cancels the job; completing
that cancellation no longer leaves a stale error that blocks the next visit.

The revised native header assertion failed against the old executable and
passes in `TestOutput/skarness/lab-header-complete`, including loading exit,
re-entry, playback, both layouts and compact mouse exit. Screenshots were
inspected. The broader Lab regression passes in `lab-header-lab-final`.
Profile and Automation builds pass warning-clean; the focused UI/comparison
units pass 49 cases / 875 assertions. Compiler-backed checks for all touched
sources, dependency checks and formatting pass. No golden baseline changed.
The full UI gate again reaches clean builds and DX12 diagnostics but stops at
the same two old floating-menu containment assertions documented below; see
`TestOutput/lab-header-ui-gate.log`.

## Executable skull icon - 2026-09-10

The executable resource and native window class now use the same skull shape
and colours as the shared UI header. `tools/generate_app_icon.py` reproduces
that 24-unit artwork as a transparent ICO with nine sizes from 16 to 256 pixels.
The project embeds `SkullbonezData/branding/Skullbonez.ico` in all configurations.
Profile and Automation builds pass warning-clean. Resource inspection confirms
all nine embedded images exactly match the ICO in both executables; a native
Skarness launch confirms the window class loads the icon and stops normally.
The preview was inspected. Evidence: `TestOutput/skull-icon-verification-final.log`
and `TestOutput/skullbonez-icon-preview.png`. The focused Window.cpp compiler
check and dependency gate pass. No gameplay behavior or baseline changed.

## Escape, floating diagnostics and Solver Lab follow-up - 2026-09-10

This follow-up supersedes the diagnostic docking behavior described below.
One Escape press closes docked Editor, Tools and Details surfaces and returns
Solver Lab to the full-screen Scene. The input edge is consumed before popup
and quit routing. F5/F6 stay floating in Canvas and Editor, including when
Tools opens, and no longer reserve dock or viewport space. Floating F5 input
takes priority over controls underneath it.

Entering an empty Solver Lab automatically loads the first library comparison,
Ragdoll Wall. Its Controls pane can replace it from the comparison picker and
offers Exit Solver Lab; loading also has an Exit Lab button. Exit retains a
loaded comparison for re-entry. The Lab header draws above the loading view.
Profiler worker controls now follow the core chart in the scrollable content,
leaving the marker table at the top.

Profile and Automation builds pass with zero warnings. The complete unit gate
passes 992 cases and 2,748,002 assertions. Compiler-backed checks pass for the
12 touched translation units and the final two-file header adjustment;
dependency, formatting and plain-language checks pass. The Profiler draw-stream
fingerprint changed intentionally after screenshot inspection; physics and
replay golden data did not change.

Native acceptance passes in `TestOutput/skarness/escape-float-acceptance`,
covering one-press Escape (including popup priority), first comparison identity
and coverage, replacement, mouse exit, retained re-entry, floating diagnostics,
menu/viewport stability, dragging and the worker controls. Header checks pass
in `escape-float-header-final`; shell, compact-window, Solver Lab and scrubber
regressions pass in `escape-float-shell-2`, `escape-float-compact`,
`escape-float-lab` and `escape-float-scrubber`. Screenshots were inspected.

The full UI gate builds both configurations and reports clean DX12 diagnostics,
then fails the same two legacy screenshot containment checks documented below:
`check_ui_blur.py` still assumes a floating menu at `(64,70,520,...)`, while
the current shell uses the shared bottom drawer and a reduced scene viewport.
See `TestOutput/escape-float-ui-gate-3.log`. The gate used the existing
`SKULLBONEZ_UI_LAYOUT_FILE` override pointing into TestOutput to isolate test
preferences. The native acceptance and header checks were run separately after
that gate stopped. Owned Skarness sessions were stopped normally.

## Tools space and F5 refresh follow-up - 2026-09-09

Opening Tools now dismisses both diagnostic panels once. Their space extends
the Tools drawer; F5 and F6 independently toggle them back while Tools remains
open. A single restored panel spans the bottom row, and both share it when
visible. Closing Tools leaves the chosen diagnostic visibility intact; reopening
Tools dismisses both again. Compact tab controls remain available after the
drawer gains height, and popup bounds respect either restored diagnostic.

F5 used to draw completed 50 ms peak buckets, giving its graph visibly stepped
updates. It now draws the live bucket and interpolates horizontal scrolling on
every presentation frame. The existing bounded ring and peak capture remain.
The focused test checks the first live sample, successive 240 Hz presentation
times within one bucket, an immediate new spike, and retention of that peak.

Automation and Profile builds succeed. UI units pass 40 cases / 836 assertions,
and six-source compiler checks plus dependency and plain-language gates pass.
The native Tools assertion fails against the old executable and passes after
the fix; screenshots and state are in `TestOutput/skarness/tools-diagnostics-after`.
The shared shell, compact-window and Solver Lab regressions also pass, under
`tools-diagnostics-shell-final`, `tools-diagnostics-compact` and `tools-diagnostics-lab`.
No golden baseline changed in this follow-up. The old Profile app is now closed,
and the normal Profile executable has been rebuilt with all current UI fixes.
The full UI gate now gets through both builds and clean DX12 diagnostics. Its
legacy screenshot checker fails two containment comparisons because it still
expects a floating window at `(64,70,520,...)`; inspected captures show the
shared bottom drawer and its reduced scene viewport. That checker was left
unchanged. See `TestOutput/tools-diagnostics-ui-isolated.log`. The run used
`SKULLBONEZ_UI_LAYOUT_FILE=TestOutput/tools-diagnostics-ui.preferences` to keep
test preferences writable and separate from the user's AppData preferences.
The independent legacy causal probe also reports missing counterpart-body rows
(`TestOutput/tools-diagnostics-cause-check.log`); its checker self-test passes.
The current native unified Causes regression passes, as do the header and
scrubber regressions. These legacy failures prevent claiming the complete UI
suite as passing; no cause-production code or legacy assertion was changed.

## Visual polish follow-up - 2026-09-09

Reviewed fresh native Scene, Canvas and Solver Lab captures against the approved
mockups from the unified UI design task. Header and dock labels are clearer,
selected dock tabs have cyan underlines, and diagnostic Details buttons use the
shared palette. Both transports have round handles. Lab controls have centered
labels, consistent padding and hover feedback; split/stacked views identify A/B.
Plot curves and tick cursors now stay below their titles. Narrow dropdown values
are clipped before the arrow instead of bleeding across it or beyond the field.

Native shell, Causes, Lab and scrubber gates pass. Inspected artifacts are under
`TestOutput/skarness/visual-polish-shell-final`, `visual-polish-causes-after`,
`visual-polish-lab-final` and `visual-polish-scrubber-final`. The eleven Tools
surfaces also have a contact sheet at `TestOutput/visual-polish-tools-contact.png`.
UI draw-stream fingerprints were intentionally updated for the new combo text
clips after inspecting those surfaces. The UI unit selection passes 39 cases /
813 assertions. Six-source compiler checks and dependency checks pass.
Compact Tools and header gates pass down to 320x240, including popup clipping.
The complete replay visual-fidelity gate and all negative controls pass
(`TestOutput/visual-polish-replay-fidelity.log`), including 18 cases / 82 assertions.
Formatting and the plain-language gate also pass.

The complete UI gate passes formatting but again stops at the Profile link:
the user's preserved process 29508 still holds `Profile/SKULLBONEZ_CORE.exe`.
Automation builds and native validation use the new code. See
`TestOutput/visual-polish-ui-gate.log`; the open Profile app still uses the old
build. Physics and replay goldens have not been changed.

## Scrubber handle motion correction - 2026-09-09

Ordinary pointer rewind selected presentation history, while the drawing code
always selected the solver track unless a recording was loaded. The handle
therefore stayed at the end even though the replay cursor changed. Drawing now
uses the same surface track choice as input, and the time label receives the
selected and latest presentation samples for ordinary history too.

The native scrubber gate now checks the actual white handle pixels at the
dragged position and repeats left/right drags in Canvas and Editor. It fails
against the previous build (`scrubber-motion-before`) and passes after the fix
(`TestOutput/skarness/scrubber-motion-final-2`). Inspected images show the handle,
time label and historical object poses changing together. Automation builds,
two-source compiler checks, formatting and dependency checks pass. The running
Profile app still holds its old executable open; it is preserved.
The complete replay visual-fidelity gate and negative controls also pass,
including 18 unit cases / 82 assertions; see
`TestOutput/scrubber-motion-replay-fidelity.log`. No baseline changed.

## Bottom scrubber auto-hide follow-up - 2026-09-09

Full UI correction (2026-09-09): Editor layout now keeps the top header and
bottom transport strip visible away from the pointer. Canvas retains edge
reveal. Native header and scrubber checks cover layout transitions, Scene/Lab
headers, camera popup lifetime, paused replay and captured scrubbing. The new
Editor header assertion fails against the previous Automation build and passes
after the fix. Inspected screenshots and owner state are under
`TestOutput/skarness/full-ui-header-after/` and `full-ui-transport-after/`.
Automation builds warning-clean; the five-source compiler check and dependency
gate pass. Replay visual fidelity and its negative controls pass, including
18 focused unit cases / 82 assertions (`TestOutput/full-ui-replay-fidelity.log`).
`validate_ui` passes formatting but stops at Profile linking because
the user's running Profile executable remains locked; that app is preserved.
The following earlier note describes the superseded Editor auto-hide policy.

The shared Scene scrubber now fades in at the bottom transport edge and fades
out when the pointer leaves, including while replay is paused. Existing router
capture holds it visible during a scrub outside the strip. The full edge can
reveal it, but clicking outside its controls does not start a scrub. The Details
button shares the fade; an already-open Details pane remains drawn and usable.
Canvas and Editor use this policy without changing their scene viewport geometry.

`tools/validate_scrubber_autohide.py` is included in `validate_ui.bat`. Its native
checks pass for both layouts, paused hide/reveal, cursor movement during capture,
edge clicks, strip pixel changes and unchanged Details pixels. Screenshots and
recorded owner state are under `TestOutput/skarness/scrubber-verified/`; inspected
earlier matching captures are in `scrubber-after-2/`, with the original always-on
state in `scrubber-before/`. The existing unified-shell regression passes.
Automation builds warning-clean. Units pass 991 cases / 2,747,493 assertions;
the required Physics commit gate passes. No golden baseline changed.

All fast-preflight lanes pass, including 99 sources / 860 compiler contexts with
zero findings. Both `validate_fast` and `validate_ui` stop at Profile linking
because the user's paused Profile app still locks the executable (LNK1104).
Neither complete invocation is claimed as passing. The unit gate, new native
check and existing shell check were run independently; the paused user process
remains preserved.

## Window move/resize crash follow-up - 2026-09-09

The native window callback queue now keeps only the final client resize while
Windows runs its title-bar move/resize loop. Mouse motion and wheel input owned
by that loop never accumulate for a later gameplay frame. Ordinary events keep
their bounded FIFO behavior, and cached client dimensions still update immediately.

A native callback probe against the previous source reproduces the exact
`Native event queue exhausted capacity=256` fatal (exit 91). The fixed source
handles 10,000 nested resize/raw-message pairs, publishes one final resize, and
accepts the next ordinary resize (exit 0). This exercises real WndProc callbacks;
it is not a manual title-bar drag. Regression units separately cover copied raw
input, wheel input, repeated move-only loops, reset and FIFO wrap.
Evidence: `TestOutput/window-move-native/comparison.json` and
`TestOutput/window-move-*.log`. No golden baseline changed.

The fast gate's complete preflight passes, including 845 compiler contexts with
zero findings. The separate unit gate passes 991 cases / 2,746,638 assertions.
Automation builds warning-clean. The complete Automation/Skarness gate and the
required Physics commit gate pass, including the clean-process worker matrix.
Profile linking is currently blocked by the
user's paused Profile process holding its executable open (LNK1104); the process
is preserved while its release is requested. The original `validate_fast` run
therefore remains failed at its build step; its preflight and the separate unit
gate results above are the completed checks.

## Unified UI current handoff - 2026-09-09

UNIFIED_UI is complete at 8/8. MASTER and this handoff both report 146/154 after
removing the completed plan and checklist from the live inventory. The approved
contract and full control inventory remain in Git history at `7d0b347fe`.
Canvas/Editor and Scene/Solver Lab now share the shell and authoritative viewport.
All Tools, editor, replay/evidence, planning and comparison controls have native
coverage, including short panes, dialogs, persistence and retained Lab lifetime.

Independent implementation review and separately reviewed UI references are
clean. All mapped terminal checks pass after repairing the initial preflight
format/filter failures and completing the remaining gates individually. The
original failed `agent_validate --plan-completion` log remains preserved; this
handoff does not claim that invocation passed. Final units: 988 cases and
2,731,966 assertions. Skarness: all fifteen prediction scenes pass. Physics,
Replay visual fidelity, DX12, coverage, UI/runtime/scene/architecture suites,
source design and bounded graphics stress pass. See MASTER's Unified UI Closure
and `TestOutput/unified-terminal-*` for exact evidence. Only approved UI regions
and fingerprints changed; all world-reference, Physics, Replay and evidence
pixels/data are preserved. The non-blocking frame-spike diagnostic missed its
fixed-frame completion assertions; tracing proves the click starts prediction
and work continues at frame 900. No frame-time conclusion is claimed.

The live ledger cannot start with unconfigured verified model pricing. Its
unrelated active task remains untouched; token splits, costs and completion-ledger
HTML/PNG are unavailable. Logs and native streams preserve available evidence.
Earlier dated checkpoints below are historical and do not supersede this handoff.

## Final causal layout and commit-all closure (2026-09-08)

The user requested committing all outstanding project work and preparing PR 168.
The causal inspector now clips and wraps raw/iteration text at larger sizes.
Outline controls occupy a reserved hierarchy footer and remain usable with the
drawer closed. The separate CausalAutomation build passes 42 focused causal
tests / 725 assertions. Captured summary, raw, iteration, and closed-drawer views
are under `TestOutput/skarness/final-causal-ui/`; a pixel comparison confirms
that changing detail tabs does not overwrite the adjacent hierarchy.

The user's running Profile app is preserved. Broader validation and integration
of the complete Solver Lab, shader, and causal work continue on PR 168 after
this checkpoint commit. Temporary build output and machine-local tool settings
are excluded from source control.

## Prediction selection outline (2026-09-07)

Yellow object outlines now resolve the current path target directly instead of
iterating retained targets. This also handles selection changes before the new
target enters the retained list. Future contact entry markers use cyan, reserving
yellow for selection; the main texture shader is unchanged.

Automation and Profile builds pass warning-clean. Recorded Skarness selection
changes across ball_a, ball_b, ball_c and back to ball_a were inspected visually.
Pixel assertions bind the selected/published IDs to each object's screen region:
667, 497 and 613 yellow pixels respectively, with zero yellow pixels outside the
selected object. Evidence: `TestOutput/skarness/prediction-selection-verified/`.
Focused source-design (two files, six contexts), dependency and whitespace checks
pass. No shader algorithm or baseline changed in this follow-up.

## Solver Lab binary diagnostic loading (2026-09-07)

Implemented SKOBS1 indexed binary contact/iteration loading and converted both
existing library comparisons (four sides, 2,400 frames each). About 3.95 GB of
source JSON becomes 110,724,024 bytes of derived numeric evidence. Original
diagnostic archives and motion captures are retained. Manifests now prefer the
binary files; older manifests retain the JSON fallback. Packaging emits binary
records too. Runtime verifies binary/source identity and uses directory counts
instead of the JSON preflight scan and two JSON parse passes.

Conversion completed after hashing the original source bytes. Per the user's
explicit instruction, no builds or tests were run, and no runtime speedup or
round-trip equivalence result is claimed. Format and conversion details are in
`Agentic/Reference/solver-observation-binary.md`. No commit or push.

## Solver Lab cyan brick edges (2026-09-07)

After dotted edges and missing-ball reports, all outline edits to
`lit_textured_instanced.hlsl` and `RenderMaterial.h` were removed; those source
files match HEAD. User explicitly requires a dedicated outline shader and no
further changes to the main texture shader. `paired_outline.hlsl` now owns the
edge mask in a fourth paired-view graph pass. Boxes use their 12 actual edges,
hulls use their polygon edges and spheres use a tangent silhouette ring. The
composite no longer infers edges from depth or object alpha. Depth is consulted
only for visibility, including X-ray against the other comparison.

User also requested antialiasing for manifold lines and gizmos, specifically in
the pixel shader. `grid_line.hlsl` now consumes existing endpoint pairs as
instances and computes smooth capsule coverage in the pixel shader. Its matching
PSO uses triangle expansion and alpha blending; transient and retained diagnostic
line callers declare that recipe. No CPU line-storage expansion was introduced.
Manifold ribbons have a one-pixel feather, pixel-space directions and joins, and
use the active viewport dimensions (including split views). The dedicated edge
and diagnostic shaders share `line_coverage.hlsli`; main object shading is separate.

The user subsequently reported startup failure and explicitly required builds
and runtime verification. Reproduced the fatal startup message: UnifiedRaster
expected 44 raster stages but found 46. The new outline program had left both
CPU inventory counts at 22. Both counts now derive from their actual arrays,
and reflection tests cover the complete inventory without duplicated counts.
Automation and Profile builds pass with zero warnings/errors; shader reflection
tests pass 13 cases / 175 assertions. Skarness startup and inspected screenshots
prove generated Demo balls, cyan brick edges with X-ray on/off, and the selected
two-point manifold at tick 2281 render. Evidence is under
`TestOutput/skarness/outline-startup-fixed/` and
`TestOutput/skarness/outline-render-verified/`. The original earlier missing-ball
cause remains unproven, but balls render with the restored main shader.

Shader freshness (48 stages), formatting, and dependency checks pass. The Profile
renderer suite starts, produces all three captures, and reports zero DX12 errors.
Its baseline comparison retains the previously documented terrain mismatch
(water 4.9171, solver 4.0155, space exact); no baselines changed. One-minute graphics
stress exits through its timed PID cleanup with result 0 and empty stderr.
A diagnostic script tried loading Demo while a comparison was active, then
loading the same bundle again without closing it; those requests failed. A clean
session loads the comparison successfully. This does not validate that unsupported
script sequence or replace the earlier header-switch evidence.

## Recovered manifold controls and Solver Lab header (2026-09-07)

The failed causal-arrow task was resumed in this checkout. Causal taps now
step one retained Physics sample; a 250 ms hold delay precedes 0.1x playback.
Solver Lab manifold arrow repeat is 750 ms instead of 75 ms. Contact normals
pulse to 1.2x and contract over 200 ms. Selected manifold centres own orbit and
zoom; Solver Lab keeps the patch while scrubbing and WASD still enters free
camera. The top-right button reads `exit`. The header dropdown reflects the
loaded library comparison, ignores reselecting it, and releases the old bundle
before switching so the existing memory cap is respected.

Automation and Profile build warning-clean. Focused tests pass 26 cases / 627 assertions;
compiler source-design, dependency and whitespace checks pass. Native Skarness
assertions and inspected screenshots prove the manifold behavior under
`TestOutput/skarness/manifold-acceptance/` and header no-op/switch/exit behavior
under `TestOutput/skarness/solver-header-verified/`. Owned sessions were stopped.

Broader validation is not clean: the full unit run has two Scene-tab forecast
visibility failures from the earlier dirty layout; the UI screenshot checks
pass with zero DX12 errors but its causal retarget fixture reports an
unavailable row at frame 1200. The long replay visual generation was stopped
when the header request superseded that build; no final replay report is
claimed. Details are in `TestOutput/manifold-handoff.md`. Existing dirty work
is preserved, with no commit, push, solver change, cap increase or baseline
refresh. This does not close PHYSICS_AB or RAGDOLL_PHYSICS plan work.

## Solver Lab (2026-09-07)

The Scene tab now has a third dropdown, Solver Lab, offering both full
Ragdoll & Wall (212 bodies) and Wall Only - Post-Ragdoll Velocity (202 bodies)
FP6/FP7 comparisons. Exit Solver Lab returns to the normal Scene interface.
The four original replay files and required inputs are under
`SkullbonezData/solver-lab/`. Full diagnostic bytes are losslessly archived;
verbose Skarness transport logs remain local. Recorded evidence and all four
replay SHA-256 identities were checked against the originals.

The original loader appeared hung at 0% while performing unreported scans.
An isolated warm-cache ragdoll measurement took 83.313 seconds: about 65 seconds
in two diagnostic JSON passes and 12 seconds in capacity scans. Bounded block
reads and SAX field projection replace per-character reads and temporary JSON
object graphs, preserving full syntax validation and numerical evidence.
The initial optimized measurement is 33.031 seconds (60.4% less); wall-only
loads in 28.672 seconds. A final cleanup also avoids rehashing already verified
archive bytes and is awaiting its timing/check results. Named phases and byte
progress now cover initial verification, capacity and reservation work.

Evidence: `TestOutput/skarness/solver-lab-qa/` holds physical dropdown, ragdoll,
wall-only and exit screenshots plus exact input/replay/diagnostic hash assertions.
`solver-lab-timing-before/` holds original phase timings. `solver-lab-optimized-qa/`
holds before/after-compatible body/event counts, bundle identities, exact tick
coverage, monotonic phase progress and physical exit assertions. Both native
sessions were stopped through Skarness. Final tests and UI checks are pending.
Independent read-only review found no functional blocker in archive, SAX or
buffered-reader ownership; new malformed-input tests await the final test gate.

This does not mark unfinished AB1-AB6 plan work complete. PHYSICS_AB remains
0/6; RAGDOLL_PHYSICS remains 8/10. No solver or baseline changes. The earlier
PR #167 is merged into main; its preparation notes below are historical.

## Combined PR preparation (2026-09-07)

PR #167 (`https://github.com/skullbonez/SkullbonezCore/pull/167`) now combines
committed causal inspection, dependency cleanup, ragdoll FP0-FP7 and owner-accepted
baselines with the formerly local A/B viewer. The README contains only the
original ASCII skull and COMIN' REAL SOON. The original dirty checkout and its
user-owned viewer are preserved; PR integration lives in `TestOutput/pr-physics/`.
This is a partial A/B implementation checkpoint, not AB1-AB6 closure.

The native viewer includes five render modes, shared camera and tick transport,
selected/all causal rows, height/vertical-velocity plots and saved findings.
Feedback fixes cover persistent timeline dragging, both split orientations,
correct mouse direction/aspect ratio, object orbit and WASD free camera.
Causal event clicks preserve the current camera even when clicking the same row
repeatedly. Summary contacts supplement available recorded checkpoint manifolds.
A/A repeated contacts match as multisets; ambiguous residuals remain explicit.

Capture verifies both producer asset roots and archives inputs. Loading verifies
recording/input hashes and actual format versions, reserves diagnostic rows by
exact count, and checks combined loading capacity before publication. Replay
readers reject oversized serialized counts before allocating; indexed frames
cannot repeatedly decode the same payload. Finding identities must match both
loaded producers. No solver, golden baseline or tolerance changes in this slice.

Local unit validation passes 953 cases / 2,728,772 assertions. Native evidence
under the isolated checkout includes `TestOutput/skarness/ab-pr-qa-final/` (all five
modes, linked camera, slider capture, pixel-exact A/A and repeat-click camera
preservation), `ab-pr-capture-aa-final/` (identical 24-tick A/A bytes), and
`ab-pr-integrity-final-02/` (hashes, versions, capacity, malformed replay count and
findings rejection). The complete 2400-tick no-ragdoll wall loads successfully in
`ab-pr-wall-final-02/`, publishing 1,286,014 events; its screenshot was inspected.
`TestOutput/ab-preflight-closure.log` passes all fast checks, build and 953 tests;
final label edits pass the focused compiler gate and Automation build. One-minute
graphics stress passes. DX12 has zero InfoQueue errors but retains terrain image
mismatches (water 4.9171; solver 4.0155; space exact). Replay fidelity finishes its
native run but fails the shader-inventory provenance hash because image_pair was
added; read-only diagnostic comparison finds identical visual and causal payloads.
The official gate is still failed, and its baseline remains unchanged. The static
allocation scan reports 117 findings, including the new cold comparison containers;
registration/review remains part of AB6 alongside inherited allocation findings.
Final gate results and
current hosted checks are recorded in the PR description; earlier green checks
on `17a6bb12d` or README commit `f39c6639a` do not validate the new A/B head.
The inherited allocation-policy inventory and two terrain screenshot mismatches
remain explicit follow-up work. No automatic baseline refresh is authorized.

The owner approved `Plans/TODO/physics-ab-comparison.md`: independent build
captures, five synchronized visual modes, freely controlled linked cameras,
selected/all causal differences and restorable findings. AB1 is active; no
solver or baseline changes. Prior uncommitted Skarness sleep queries remain.
Work-ledger start failed because another session owns GOV1; preserve its state.

## Comparison feedback and isolated wall run (2026-09-07)

Implemented persistent timeline drag ownership in InputRouter, side-by-side /
stacked layouts, WASD free look with selected-object orbit, and aspect-aware
projection shared by picking and rendering. Comparison file loading now runs
on a Planning-owned worker with progress and cancellation; only complete data
is published on the UI thread. Native mouse-right/down direction, circular
sphere proportions in both layouts, camera mode changes, drag continuation,
load cancellation, and finding restoration pass in
`TestOutput/skarness/ab-feedback-qa-06/`. Automation builds, 26 focused cases /
421 assertions, shader bake, and dependency ownership checks pass. Full feature
closure remains pending; PHYSICS_AB is not complete and no baseline changed.

The requested isolated trial is complete at
`TestOutput/skarness/ab-post-ragdoll-wall-01/comparison.json`: FP6 A and current
B, 2400 ticks each, 202 bodies (ball, 200 bricks, catcher), no ragdoll. Both
start with the exact new-physics ball state at original physics tick 86, after
its last ragdoll contact at tick 85. Scene fixtures are
`SkullbonezData/scenes/wall_200_post_ragdoll_fp6.scene.json` and
`wall_200_post_ragdoll_current.scene.json`; only their display names differ.
First recorded body states are identical, with the first small solver-result
difference at isolated tick 89. `isolation-check.json` preserves the proof.
These are cold scene starts with unchanged wall inputs, not checkpoint
continuations with inherited contact caches. The user owns baseline approval.

The corrected viewer launch is
`TestOutput/skarness/ab-post-ragdoll-user-01`, executable
`Automation/SKULLBONEZ_CORE-PhysicsComparisonPreview3.exe`, PID 22376.
Leave this user session open. Earlier comparison windows have been closed;
completed producer sessions were stopped through Skarness.

## Ragdoll FP7 acceptance (2026-09-07)

Contacts and point joints now share deterministic island sweeps and solver-body
velocities. Release continuation wakes connected articulation and incorporates
new support before cache publication. Separate joint-wake scratch preserves
contact sleep topology; sleeping joint caches and unrelated islands remain
exact. No body field or runtime allocation privilege was added.

All three native builds, 183 focused tests / 2,552,691 assertions, 126-file
compiler checks, Physics worker determinism, and the complete independent
200-box replay repeat with negative controls pass. Exact baseline transitions
retain old/new first-party producers under the FP7 artifact directory. Dynamic
allocation smoke, selected path, physics benchmark, repeated DX12 timing and
five scale runs pass. The 95 exact inherited static findings and 13 existing
causal render allocations remain explicit full-plan closure work; archived FP6
reproduces the latter at the same callsite. FP8 speculative contacts is next.

## Native UI and profiling dependency cleanup (2026-09-07)

The obsolete docked editor and remote-profiler integrations, vendor submodules,
build properties, renderer resources, startup switches, allocation privileges,
fixtures, settings, and documentation references are removed. Native GameUI,
its profiler, causal inspection, and Skarness remain. Recorded version-1 files
retain a reserved zero-valued UI slot so existing evidence stays readable.
Window events sent synchronously now drain even without a queued message;
the native UI smoke checks actual 1024-by-720 capture dimensions after resizing.

Validation passes the 926-case unit suite, portable CPU tests, compiler-backed
source checks, dependency/build metadata checks, native UI and graphics stress,
200-box replay fidelity, and gameplay allocation-guard smoke. The two inherited
DX12 terrain screenshot mismatches remain (water 4.9171, solver 4.0154; space
exact and zero InfoQueue errors). Allocation-policy scanning reports the same
91 findings as starting revision `3475567b1`, with no new findings. No physics
or replay baseline was refreshed. The native profiler stream now matches its
existing portable fingerprint after removal of the remote status badge.
Archived regression executables remain historical evidence.
The Debug 0/repeat/1/4-worker physics proof is byte-exact (44,401 lines,
SHA-256 `03088b30b8826f88a6193e511b7f4205aff9324d06ad08456610aac0e13a3f6b`).
The full Skarness lane passes transport, control, commands, state, queries,
rendered futures and causal playback; the scene matrix passes 14 of 15 cases.
The demo's live assertion still expects cause rows during prediction, contrary
to the loading policy introduced by `f23993067`. An isolated rerun produces 15
completed causal nodes and fails only that live-row assertion. Both the
assertion and loading implementation are unchanged by this cleanup. The first
matrix run also found no contacts in the time-seeded demo's selected target.
The final affected UI/input rerun passes 126 cases and 4,441 assertions.

## Ragdoll FP5 acceptance (2026-09-07)

The coupled 3-DOF point joint and world-space vector warm start are implemented.
Current and historical snapshot codecs, import migration, and pre-mutation
legacy-continuation rejection have focused regressions. The Python query reader
supports nested versions 1-7 and rejects version 8. Native saved replay
save/query/restore/rollback, the complete 200-box native replay repeat and its
negative controls, the final Debug worker matrix, source design, dependency,
formatting, and focused unit checks pass. The core Physics CSV is unchanged;
the visual and causal transitions retain exact old/new first-party producers
under the FP5 artifact directory. A pre-existing SkullScope frame-key typo
found by the native artifact gate is repaired with a generated-trace assertion.

Historical loaded-chain comparisons measure final sag of 35.084/24.379/7.113
mm for pre-(a)/stage-(a)/FP5. Against stage (a), FP5 increases jitter from
0.281 to 1.730 mm/step, settled kinetic energy from 0.02275 to 0.18876 J, and
solve cost from 0.153 to 0.286 microseconds per joint iteration. These are
explicit FP6 inputs, not a universal performance claim. The performance gate
still reports the same 91 inherited allocation-policy findings, zero new.
Independent review has no remaining implementation blocker.

## Ragdoll FP6 acceptance (2026-09-07)

Explicit 40 Hz/damping-ratio-1 softness, eight sweeps, physical scene schema v5
and solver snapshot v8 are complete. Legacy controls migrate with matching
binary32 arithmetic and incompatible warm starts clear on explicit import.
The archive now retains a bounded high-detail prefix with an explicit RVPD v7
coverage boundary, preserving later causal topology and exact sparse re-save.
No body-store field, reserve owner or growth cap was added.

Debug/Profile/Automation builds, 63 focused cases/32,678 assertions, source
design, dependency, formatting, plain-language and migration checks pass. The
Debug worker matrix is unchanged. Native saved replay checks and the corrected
200-box capture plus independent full fidelity repeat and negative controls
pass. The guarded transition `FP6/golden-transitions/joint-softness-9499cda6/`
retains exact old/new producers and matching scene inputs. The Physics CSV is
unchanged. The 91 allocation findings and intentional non-unit hull-normal
migration fixture remain inherited limitations. Independent review is clean.
The measured tradeoff is 15.531 mm physical sag and 0.00321 mm/step settled
jitter with about 63% more total solve cost than FP5. Full plan closure remains
FP9; no FP7-FP9 implementation is included in this checkpoint.

Branch CI runs 34045496086 (mandatory CPU) and 34045497777 (Linux diagnostics)
passed on FP5. Fresh runs will be dispatched after the FP6 push.

## Current State

Current objective: Unified UI goal complete on codex/unified-ui.
Plan progress: UNIFIED_UI 8/8 closed; portfolio progress: 146/154.
Next binding task: PHYSICS_AB AB1 (independent capture), then AB2-AB6 and
RAGDOLL_PHYSICS FP8-FP9 in MASTER order. Those plans are outside this UI goal.
The live ledger limitation and final evidence are recorded at the top of this file.

The owner activated `SKARNESS` SK0-SK6 and directed the Night Runner to finish
the plan without stopping. The bootstrap checkpoint repairs prediction
publication, causal inspection timing/camera behavior, deterministic manual
control, subscription durability, and renderer-bound identity evidence. Its
15-case persistent Automation matrix, focused run-control checks, prediction
unit cases, and two independent 6,800-frame replay captures pass. The replay
visual baseline now records the corrected publication and the causal baseline
transition from 200 to 201 topology nodes is archived with its exact old and
new Automation executables in checkpoint `7b6d74367`. SK0's versioned protocol
and initial capability inventory landed in `f0c60b2a`. SK1 completes the
session-host/client contract: Debug and Automation own the feature, Profile and
Release reject its flags, manifests publish atomically with a current-user pipe,
and reconnect/deduplication remain correct through 260 completed requests and
an exact 128-command queue saturation. `tools/validate_skarness_transport.py`
owns that focused regression. SK2 now publishes state
after the committed scene-frame boundary and proves exact three-tick, four-
render-frame, immediate-condition, three-frame timeout, three-tick timeout, and
25-tick disconnect/reconnect behavior. Accepted work freezes while its controller
is absent, then resumes without lost or duplicate ticks. Automation and Debug
builds, the transport regression, and `validate_fast` pass; no baseline changed.
SK3 now exposes the immutable replay/player capability catalog and routes scene
identity, selection, replay capture/timeline, prediction presentation, causal
inspection, velocity editing, porkchop/trip planning, and continuous forecast
commands through App and their existing owners. Numeric setters return applied
values, path commands reject truncation, and full cause selection rejects stale
identity. `tools/validate_skarness_command_coverage.py` proves catalog uniqueness,
player-control inventory coverage, retry-safe setters, explicit replay paths,
and live command routes in Automation. Automation and Debug builds, focused
source design, dependency enforcement, and the live command proof pass with no
baseline change. SK4 now publishes snapshot-first registered replay-family
topics with summary, normal, and full detail, explicit append/change/evict/reset
rows, and complete production visual-packet buffers. Physics-owned SkullScope
emits in Automation as well as Debug and carries exact runtime-turn, scene-
generation, and committed simulation-tick correlation without a Runtime include.
`tools/validate_skarness_state_stream.py` proves ordering, sequence monotonicity,
ring wrap, scene resets, full buffers, and cross-trace joins in both builds. SK5
now adds a replace-safe incremental SQLite importer for both sidecars, preserves
Physics correlation across imported row batches, ignores incomplete final lines,
and exposes bounded summary, replay, prediction, cause, render-submission,
Physics, and sequence-tail queries with exact byte accounting. Diagnostic log
handles permit shared reads, and App flushes Physics once at the durable after-
render boundary, so queries work before session shutdown. The client now accepts
the planned `send`, `--detail`, `--ticks`, frame-range, target, and `--full`
shapes. `tools/validate_skarness_queries.py` proves incremental recovery, source
replacement invalidation, name-to-id filtering, cursor tails, and live cross-
trace joins. SK6 now drives the future-path gate entirely through Skarness,
binds every published and rendered fact to the selected target and source frame,
validates complete child ancestry and trajectories, and proves renderer-bound
bytes and hashes remain stable for 120 render frames. Automation and Debug live
witnesses pass with six causal nodes, one child, 241 root points, 9,792 geometry
bytes, and 3,396 connected raster-change pixels. The full 15-scene matrix and
the complete 443.149-second Automation/Skarness lane pass. Independent review
reports zero blockers and `READY`.

The exactly-once plan-completion command passed Debug, Automation, fast checks,
source design, Profile, all CPU lanes, and the Automation/Skarness lane. It
exited 1 only at the inherited DX12 terrain-UV screenshot mismatch documented
before this plan: water averaged 4.9171, solver averaged 4.0155, space remained
pixel-exact, and DX12 InfoQueue reported zero errors. Skarness is compiled out
of Profile. No visual, replay, causal, Physics, or SkullScope baseline changed.

The owner completed `SIGNATURE_COHESION` SC0-SC7 on the current Night Runner
branch and requested substantial related batches instead of a build after each
small signature change. SC0 through SC2 are complete. The compiler census covers
736 files/6,435 contexts with 698 candidates; the motivating broadphase bag is
replaced by retained stage configuration, a checked activity view, a validated
sweep/contact value, a synchronous pair filter, explicit joints, and direct
trace capability. The Profile build and 39 focused cases/9,273 assertions pass;
the Debug 0/repeat/1/4-worker matrix remains byte-exact at the accepted 44,401-
line digest. SC2 replaces nine-scalar rotation construction, tornado vertex
serialization and oversized render orchestration, Physics authored/hot/force
row bags, terrain output references, ragdoll impulse plumbing, per-step runtime
settings, and unchecked height-map derived counts with named values, aligned
views, typed results, and concrete owner phases. The 33 focused cases pass 925
assertions, source-design passes 24 files/192 contexts, and the Physics digest
is unchanged. SC3 is complete. Rendering now uses validated mesh vertex/upload
values, typed camera/light views, explicit model-selection modes, and typed
shadow batch views. The UI library records named bounds/point/triangle/color
values and uses a span-backed combo presentation view. Compiler-required
repairs also split the touched GameUI and replay scrubber composers into
concrete draw phases. Profile builds warning-clean; 912 cases/2,693,378
assertions, the renderer-free UI boundary, dependency, formatting,
build-configuration, and source-design checks pass. DX12 produces all captures
with zero InfoQueue errors; a detached pre-SC3 build proves the two committed
screenshot-oracle mismatches are inherited, and no baseline was refreshed. SC4
now reviews Runtime packages below App in dependency order. The first SC4 slice
gives Capture typed frame/schedule inputs and an explicit state update, gives
replay scrubber availability one behavioral source-selection value, and gives
UI interaction caching a behavioral signature value with the previous hash
order preserved. The combined Profile build passes warning-clean, the complete
suite passes 914 cases and 2,692,750 assertions, source-design passes 15 files
and 96 contexts, and format, dependency, build-configuration, and whitespace
checks pass. The next grouped slice repairs the UI/App coordinator structure
exposed by the compiler gate. That below-App portion now splits the UI window
input owner into derived-layout, minimized, chrome, popup, tab, footer, slider,
drag/resize, and release phases; splits editor gizmo live drag from scale and
pose release recording; and splits operator draw-trace/profiler projection.
Profile builds warning-clean, the normal suite passes 913 cases and 2,691,927
assertions with one expected skip, source-design passes five files and 39
contexts, and the renderer-free UI boundary plus lightweight gates pass. The
following grouped slice moves into App orchestration and the typed UI input
seam. It decomposes `ApplyInputCommandsPhase` into ordered replay,
forecast, device/mode, editor, presentation, tuning, generated-scene, and
world/cinematic phases. GameUI receives detached frame, editor-mode, and camera
availability values instead of ten adjacent primitive arguments, and the
unused selected-camera scalar is gone. The Profile solution builds
warning-clean; 913 cases and 2,694,677 assertions pass with one expected skip;
source-design passes seven files under 53 consumer contexts. The next grouped
App slice decomposes `RunInputPhase` into capture/default draining, pre-UI
action families, operator input, replay restore, recording diagnostics, camera
control, and deferred owner requests. `RenderOperatorUiPhase` now shares one
detached projection for GameUI and separately projects hierarchy,
inspector, diagnostics, GameUI data, and text/GPU submission.
The Profile solution builds warning-clean; 913 cases and 2,692,437 assertions
pass with one expected skip; source-design passes three files under 16 consumer
contexts; format, dependency/project ownership, build-configuration, and
whitespace checks pass. The fifth grouped slice replaces GameUI's 115 flat root
frame fields with eight cohesive detached sections while retaining the existing
narrow tab projections. The Profile solution builds warning-clean; 913 cases
and 2,692,662 assertions pass with one expected skip; source-design passes seven
files under 41 consumer contexts; format, dependency/project ownership,
build-configuration, and whitespace checks pass. The sixth grouped slice
replaces Prediction topology publication's three-pointer context and one-use
resolver-callback traversal with a scoped topology builder, typed node
candidates, a concrete affected-marker input, and a target/budget overlay
request. Profile builds warning-clean; 913 cases and 2,692,417 assertions pass
with one expected skip; source-design passes three files under 20 consumer
contexts; format, dependency/project ownership, build-configuration, and
whitespace checks pass. The seventh grouped slice removes the render-startup
service bag and binds result diagnostics, backend, assets, window, render
configuration, environment, profiler, and scene generation facts directly to
their consuming owners. `RuntimeRenderer` retains only window, environment,
and profiler, while `RenderResourceLifecycle` receives assets, configuration,
and profiler directly. Profile builds warning-clean; 913 cases and 2,692,057
assertions pass with one expected skip; source-design passes six files under 47
consumer contexts. The eighth grouped slice removes the seven-field
`RenderResourceContext` from ten pass-resource operations. Each pass now
receives only its actual asset, resource-builder, geometry, cinematic, or extent
inputs; the no-op debug ensure and forwarding helpers are gone.
`RuntimeRenderer` remains the resource-epoch sequencer. Profile builds
warning-clean; 913 cases and 2,692,412 assertions pass with one expected skip;
source-design passes four files under 30 consumer contexts; format, dependency/
project ownership, build-configuration, and whitespace checks pass. The next
batch reconciles the remaining SC4/SC5 inventory against the compiler census.
Eight SC4 commits (`8bfdc279ec` through `8c6172103f`) were published with
subject-only messages. History remains immutable; the owning plan now records
the recovered per-hash implementation and validation evidence. A repository
`commit-msg` hook plus self-tested checker rejects empty, short, reordered, or
placeholder bodies and requires the six evidence sections documented in
`AGENTS.md` and `Agentic/README.md`. No baseline refresh
is authorized. The live work ledger is temporarily owned by
unfinished task `GOV1` in another Codex session, so this run cannot open its own
ledger row until that external metadata owner finishes.

The owner visually accepted the persistent deterministic simulation-island
sleep policy as-is. The scoped result makes a sphere on terrain steeper than
five degrees sleep-ineligible, prevents a box from sleeping on one logical
edge, replaces the three-body wall exception with pose/contact residual
stability, and commits whole-island wake through the serial Physics path. The
authored 0/1/4-worker matrix is byte-identical across two clean runs: the corner
box topples before both bodies sleep, `ball_b` rolls into the basin near the
other balls, and all 200 wall bricks finish asleep with a stable four-brick-high
stack. Ragdoll quality remains explicitly out of scope.

The Profile unit suite passes 907/907 cases and 2,694,564 assertions. Physics,
Physics deep, replay-v2 artifact, authored sleep, allocation-policy,
dependency, build-configuration, compiler-backed source-design, format, and
plain-language validation pass. Governed Physics behavior, deep diagnostic,
known-issue, and query baselines were refreshed with retained old/new Debug
executables and manifests. The historical Physics benchmark reports
`Frame/Physics/Step` at +4.9 percent, inside the sleep plan's five-percent
bound. The full performance comparator still reports unrelated aggregate
Frame/Input/Render/Vsync host-timing regressions against `3a4b52e94`; no
performance baseline was refreshed.

`RESERVE_TRANSACTION` RAT0-RAT3 is complete at 4/4 and
`SOURCE_DESIGN_THROUGHPUT` SDT0-SDT4 is complete at 5/5.

RAT0 adds the non-copyable, non-movable Core
`RuntimeReserveAllocationScope`. Its member order establishes phase, owner, and
grant together and closes them in reverse order. Five focused cases cover
context restoration, unused grant release, exact real-allocation consumption
with the guard off, nested restoration, mismatched owner/phase rejection, and
thread-local isolation without consuming more process-lifetime test-owner
slots. The Profile test target compiled, the focused family passed 55/55
assertions, the full suite passed 892/892 cases and 2,691,405 assertions, and
format, allocation-policy, and compiler-backed source-design checks passed.

RAT1 replaces the five direct approved-allocation scope assemblies in Physics
and Replay with the composite transaction. SceneLoad retains the same concrete
Physics owners; solver snapshots retain one aggregate Replay grant; and both
recorder vector helpers retain their existing owner, cap, byte request, and
failure behavior. The nested Physics Replay reserve remains owner-only beneath
the approved outer grant, while the recorder's Capture-owned cold path remains
outside retained Replay authority. The Profile test target compiled, focused
PhysicsFixedList, ReplayRecorder, and replay-solver families passed, and the
allocation-policy and compiler-backed source-design checks passed.

RAT2 replaces the nine direct approved-allocation scope assemblies in
Prediction reserve helpers, archive candidates, solver-evidence segment stores,
and trajectory storage. Existing batched grants retain their allocation order
and aggregate byte boundaries. Production callers no longer construct
`RuntimeReserveGrowthScope` directly; its remaining production references are
the Core implementation and composite member. The Profile test target compiled,
40 focused Prediction/evidence/trajectory/visual cases passed 18,669 assertions,
the production prediction-engine seed case passed 645 assertions, and the
allocation-policy and compiler-backed source-design checks passed.

RAT3 closes the reserve transaction after a blocker-free independent re-review.
The review first required a mechanically order-sensitive test and corrected an
overbroad grant invariant; standard-layout `offsetof` assertions now fail if
the phase, owner, and grant member order changes, and the invariant accurately
distinguishes SceneLoad from Replay-approved growth. The focused composite
family passes 5/5 cases and 55/55 assertions; compiler-backed source-design,
allocation-policy, dependency proof/repository, and byte-exact Physics gates
pass, with the accepted 44,401-line SHA unchanged. Debug, Profile, and
Automation targets compile. Performance reproduces the pre-plan unavailable
cause-row/track mismatch, and replay visual fidelity reproduces the pre-plan
missing `full_reveal_probe_profile.json` after its 18/18 typed controls and
scheduled engine exit. Fast validation and the single terminal
`--plan-completion` run stop on the same three pre-plan plain-language findings;
the terminal run first completes Debug, Physics, and Automation builds. No
Physics, Replay, performance, or visual baseline was refreshed.

SDT0 pins immutable source/context work-item identity and adds structured
context result, timing, and bounded summary values without changing source
selection, compiler arguments, findings, or exit classification. The exact PR
162 reference regenerates 79 files and 623 contexts at identity SHA-256
`d9e4b117d81e3ae8ca5f29376335f7c219bdc47cd94c4438d4cb4a7a0da5db77`.
Two clean serial runs both pass all 3,115 LLVM launches in 3,274.505 and
3,275.521 seconds, with 669,978,624 and 669,888,512 peak committed bytes.
The refreshed runner is `windows-2022` image `20260824.284.2`; local LLVM is
22.1.3. The self-test and a five-context live source scan pass with the new
summary and zero findings or infrastructure errors. No test, coverage,
Physics, or golden baseline changed.

SDT1 batches the four unchanged Clang Query matchers into one parsed session
per source/context. Explicit section and bound-location accounting rejects a
missing rule, partial command stream, truncated match output, or malformed
matcher as infrastructure failure. Independent fixtures cover each rule and
multi-rule files. A planted all-four-rules comparison is byte-identical to the
pre-change diagnostics at stderr SHA-256 `43e42af4a712ca0a920cef9785c2cec28a661dbd2afb5bb8cf7934709acc4404`.
The exact 79-file/623-context reference passes with 623 Tidy plus 623 Query
launches, zero findings/errors, and 1,480.681 seconds elapsed, down from the
3,274.505-second first serial reference. No selection, compiler argument,
matcher, threshold, failure class, test, coverage, or baseline changed.

SDT2 runs immutable source/context work items through a pool capped at four,
with automatic selection `min(logical_processors, 4)`. Each worker runs Tidy
then Query and returns a value; the coordinator alone admits work, aggregates
measurements, and sorts diagnostics by source/project/configuration/rule/location.
Planted delay, admission, policy, Tidy, Query, child-crash, and worker-exception
controls pass. Clean and 70-finding planted scans retained byte-identical output
and exact exit values at one, two, and automatic workers. The planted automatic
scan fell from 8.981 to 3.669 seconds. A current 19-source/138-context scan used
four workers and completed in 94.331 seconds with no infrastructure error; its
22 findings belong to preserved user-owned Physics work and pre-existing Replay
source, not SDT2. No selection, context, matcher, threshold, test, coverage, or
baseline changed.

SDT3 replaces `validate_fast`'s two opaque process one-liners with one direct
retained-policy group runner used once for self-tests and once for live scans.
It captures every child invocation once, always emits the bounded source-design
summary into the existing mandatory CPU log stream, preserves failed output
without replay, and writes the live source/context/process/worker/timing table
to `GITHUB_STEP_SUMMARY`. The current 19-source/138-context failure path wrote
the complete table at 94.557 seconds and retained the existing 22 findings.
The plain-language prerequisite now passes after two wording-only repairs in an
inactive WNF plan. No workflow split, scan duplication, context pruning, test,
coverage, or baseline change occurred.

SDT4 closes at exact measured implementation `4894b9f0`. The 79-file/623-context
reference completes 623 Tidy and 623 batched Query analyses in 440.016 seconds
with four workers, a 3.38-times improvement over the unchanged 1,488.086-second
batched serial path. Stable output remains byte-identical. The final 21-file/154-
context scan passes in 128.044 seconds, and hosted runs `33274650785` and
`33274651985` complete source design in 177.089 and 218.936 seconds and their
mandatory jobs in 18m15s and 20m14s. Independent review first found
completion-order-dependent failure admission; fixed deterministic prefixes and
the new fast/slow failure control closed it, and follow-up review is blocker-free.
The exactly-once terminal lane passes builds, preflight, 893 discovered unit
cases with 2,692,862 assertions, coverage, all CPU suites, Automation, and DX12
InfoQueue before reproducing the exact pre-plan water/solver screenshot mismatch.
No baseline was refreshed. Closure evidence is under
`Agentic/Plans/Artifacts/source-design-validation-throughput/SDT4/`.

The ignored work ledger retains an unfinished 2026-08-28 goal from another
Codex task (`GOV1`, `finding-fix-01`). The orchestrator refused to start a new
goal row, so the current run preserves that ledger unchanged and records its
goal/task usage through the active Codex goal instead.

## Rendering Bug Ledger Closure (2026-08-26)

RENDER-004 through RENDER-011 are fixed as one Rendering subsystem batch. The
render graph now rejects impossible overlapping same-pass states and carries
same-state UAV ordering through API-neutral compile output into both transient
and external DX12 execution. Primitive readiness requires its material table,
pose presentation uses one bounded interpolation fraction, and material/font
inputs reject unsafe numeric values before publication. SDF generation and
frame-graph diagnostics now report incomplete output honestly and retry without
publishing failed state.

One consolidated read-only rubber-duck review used task
`01a03961-d56f-70d1-a7f2-56d4651b858b`. The first pass found five focused
evidence/wiring gaps across UAV dispatch, primitive readiness, Refresh pose
publication, font-state atomicity, and GDI result aggregation; the follow-up
closed all five. Final counts were 0 Blocking, 0 Non-blocking, and 1 Missing
evidence. Per owner direction, focused Rendering tests, builds, scanners,
inventories, and repository validation were not executed.

## Runtime Prediction Bug Ledger Closure (2026-08-26)

PRED-001 and PRED-002 are fixed as one Runtime Prediction subsystem batch.
Prediction archives now classify child trajectory records against the bank that
is actually presentation-visible, discard the opposite bank before inspecting
its variable payload, and serialize the ragdoll-visual fact captured beside the
visible nodes and retained markers. A real save/load fixture makes the captured
and live generations disagree, retains the visible build-bank child, and proves
the malformed inactive bank cannot reject or leak into the artifact.

One consolidated read-only rubber-duck review used task
`01a03961-d56f-70d1-a7f2-56d4651b858b`. The first pass found three blocking
ordering and evidence gaps; the follow-up closed all three. Final counts were
0 Blocking, 0 Non-blocking, and 1 Missing evidence. Per owner direction,
focused Prediction tests, builds, scanners, inventories, and repository
validation were not executed.

## Runtime Replay Bug Ledger Closure (2026-08-26)

REPLAY-001 and REPLAY-002 are fixed as one Runtime Replay subsystem batch.
Solver restore now publishes the recorded camera eye, target, and up basis as
one pose after cancelling any live tween. Interactive save preparation and both
artifact-writer entry families classify their allocation-heavy work as cold
Capture activity, including fallback prediction-state encoding before the
binary writer starts.

One consolidated read-only rubber-duck review used task
`01a03961-d56f-70d1-a7f2-56d4651b858b`. Review closed active-tween and
independent plain, solver-track, and runtime-fallback evidence gaps. Final
counts were 0 Blocking, 0 Non-blocking, and 1 Missing evidence. Per owner
direction, focused Replay tests, builds, scanners, inventories, and repository
validation were not executed.

## Core Allocation Bug Ledger Closure (2026-08-26)

CORE-009 through CORE-011 are fixed as one Core allocation subsystem batch.
Duplicate reserve-owner names now reuse a process-lifetime handle only when
their normalized effective policies match. Replay growth-count admission stays
serialized with its owner budget transaction, with concurrent regression
evidence covering the final permitted grant. Allocation callsite keys now retain
owner identity, publish only complete rows, and snapshot ranked rows under the
accounting lock so reset cannot mix fields within a reported row.

One consolidated read-only rubber-duck review used task
`01a03961-d56f-70d1-a7f2-56d4651b858b`. The first pass found one blocking
reset-versus-summary race and missing normalized-policy controls; the follow-up
closed both. Final counts were 0 Blocking, 0 Non-blocking, and 1 Missing
evidence. Per owner direction, focused allocation tests, builds, scanners,
inventories, and repository validation were not executed.

## UI Library Bug Ledger Closure (2026-08-26)

UI-001 through UI-005 are fixed as one UI Library subsystem batch. Losing
pointer capture now discards deferred Replay memory previews. Window, minimized
editor, and runtime camera-mode geometry shrink to the actual client without an
invalid clamp or unreachable controls. Programmatic scroll feedback is anchored
to the next visible draw. Captured window drags replay retained commands from a
stable source using window-local pointer identity while live content rebuilds
after release. The Scene header now shares its supported width across every
control, keeping Save Defaults reachable.

One consolidated read-only rubber-duck review used task
`01a03961-d56f-70d1-a7f2-56d4651b858b`. Three passes closed the non-editor
camera-mode overflow and compact-editor title overlap; the root pass also closed
cumulative drag-offset and live-content cache invalidation hazards before final
review. Final counts were 0 Blocking, 0 Non-blocking, and 1 Missing evidence.
Per owner direction, focused UI tests, builds, scanners, inventories, and
repository validation were not executed.

## Scene Data Bug Ledger Closure (2026-08-26)

SCENE-003 through SCENE-006 are fixed as one Scene Data subsystem batch. Direct
and saved-state bodies now reject unusable dimensions and mass properties before
publication while preserving zero restitution as a valid no-bounce material.
Style documents are closed to presentation-owned fields and are checked before
include traversal. Signed integer parsing rejects int32 overflow, numeric booleans
preserve full 64-bit nonzero meaning, and bounded output/filter strings reject
capacity overflow and embedded NUL instead of truncating.

One consolidated read-only rubber-duck review used task
`01a03961-d56f-70d1-a7f2-56d4651b858b`. The first pass reported two non-blocking
findings: style policy ordering and incomplete whole-scene atomicity evidence.
Both were closed before the final pass. Final counts were 0 Blocking,
0 Non-blocking, and 1 Missing evidence. Per owner direction, the focused parser
tests, builds, scanners, inventories, and repository validation were deferred
and not executed.

## Runtime Camera Bug Ledger Closure (2026-08-26)

CAM-001 through CAM-003 are fixed as one Runtime Camera subsystem batch.
Locked dolly and rotation inputs now compose against the effective staged eye,
clamp requested orbit endpoints, share the supported world-Y basis fallback,
and never repair floating rotation drift by moving the retained target. Tween
completion publishes the terrain-corrected pose and its corrected view
magnitude back to the selected camera slot.

One consolidated read-only rubber-duck review used task
`01a03961-d56f-70d1-a7f2-56d4651b858b`. Four passes closed staged-input
composition, partial-assignment magnitude publication, zero-up orbit basis, and
locked target-recovery findings. Final counts were 0 Blocking, 0 Non-blocking,
and 1 Missing evidence. Per owner direction, the focused camera tests, builds,
scanners, inventories, and repository validation were not executed.

## Validation Tooling Bug Ledger Closure (2026-08-26)

TOOL-001 is fixed as the complete pending Validation Tooling subsystem batch.
The Physics commit gate now classifies `SkullbonezData/engine.cfg` and owned
`SkullbonezData/hulls/*.hull` files as deterministic inputs for staged
triggering, index fingerprinting, and dirty-worktree rejection. Deletions are
included in both staged and unstaged checks, and the self-test plants real Git
deletions while isolating its temporary repository from global signing and hook
policy.

One consolidated read-only rubber-duck review used task
`01a03961-d56f-70d1-a7f2-56d4651b858b`. Three passes closed the omitted-deletion
bypass and temporary-repository Git-policy isolation. Final counts were 0
Blocking, 0 Non-blocking, and 1 Missing evidence. Per owner direction, the
updated self-test, commit gate, builds, scanners, inventories, and repository
validation were not executed.

At-Rest Ball Stability RS0-RS7, Invariant Enforcement And Assertion
Hardening IH0-IH7, Cause Hierarchy Scientific Inspector CHUI0-CHUI6, Full Source Comment
Truth Replacement CT0-CT5, Full Validation Time And Value Audit VTA0-VTA5, and Repository
Hygiene Cleanup RC0-RC5 are 100% complete, reviewed, and closed.
Causal C0-C8, Determinism T0-T8, Catto CD0-CD5, Predicted Solver Cause Hierarchy PSD0-PSD7,
and Continuous Orbital Forecast OF0-OF6 are complete.
Governance De-Bureaucratization and Jargon Removal DB0-DB7 is complete at 8/8.
DB0 inventories all 13 tracked JSON files and 56 tracked Python
tools, records sixteen replacement negative tests, and corrects the current
wide-signature seed to the omitted 12-parameter
`PhysicsBroadphaseStage::Run`. DB1 removes physical-coordinate policy identity
from deterministic math, Runtime repair debt, aggregate review, and function-
complexity review. Their location-movement mutations and live focused scans are
green. DB2 deletes the two custom formatters, glossary permission machinery,
and governance meta-runner; clang-format is the sole forward layout rule,
Related-path reporting is advisory, and the retained direct checks stay
parallel with exact failure propagation. DB3 replaces five lexical design
inventories with `tools/check_source_design.py`, deletes their four permission
ledgers and shared scanner, and narrows the missed broadphase signature from 12
parameters to three. Effective Debug/Profile/Automation compile contexts,
project dead-code settings, MSVC link behavior, and exact negative fixtures are
covered by focused checks. Three read-only reviews are clean after closing the
reported missed-failure gaps. An automatic commit hook was stopped during its Debug
build before tests or runtime comparison; broad validation and Physics
behavior/determinism evidence remain deferred by explicit owner direction. DB4
now scans all ten engine roots with enclosing-symbol and local-code identity,
rejects ambiguous permissions, and reconciles the FP0, TOOL-002, TOOL-003, and
ASSET-003 bug dispositions. Asset registries have constructor-reserved hard
ceilings, while terrain and water reuse cold-built CPU geometry during backend
retries. Four serial reviews are clean after three correction cycles. Only the
focused allocation checker and its synthetic controls ran; broad validation was
omitted by explicit owner direction. DB5 now reports bounded first-divergence
frame/body/metric diagnostics and makes the content-bound core Physics golden
transition one transactional command. It discovers and copies the accepted
predecessor from the clean Git index, archives and force-stages old/new
first-party producers, invokes the existing guard, and rechecks the staged
transition. Four serial reviews are clean after closing ignored-binary staging,
mutable-predecessor, malformed-CSV, and output-bound gaps. Focused script
self-tests and the live predecessor lookup pass; no golden, build, runtime test,
or repository validation was run by explicit owner direction. DB6 removes the
retired terms and their morphological variants from tracked first-party source
and documentation except the active plan's temporary lexicon table. Exact
external report keys and Runtime-contract child-case arguments remain stable
through split literals. Fifty-six boilerplate headers were reduced, and the
comment policy now requires concise local engineering explanations without
mandatory template sections. Two serial reviews closed missed-variant and
parked-patch integrity findings. No build, test, Physics comparison, or
validation command ran by explicit owner direction. DB7 adds the Git-derived
`tools/check_plain_language.py` scan with zero source/document path or site
exceptions, source and documentation negative fixtures, and a clean fixture. `validate_fast.bat`
and the mandatory hosted CPU workflow both invoke it. The completed TODO plan
is deleted under repository convention. Five serial read-only review passes
closed nine matcher, scope, fixture, and metadata findings; the final pass is
clean. No scanner, fixture, build, test, or validation command ran by explicit
owner direction. CORE-001 is now closed: worker profiler samples enter a
frame-tokened fixed staging store, and the main thread closes admission and
merges them before finalizing marker history. Token-aware worker nesting prevents
a stale cross-frame scope from suppressing valid next-frame core occupancy, and
enabled-only state remains behind the stable common `Profiler` layout. Two
serial read-only reviews closed the nesting and mixed-definition layout findings;
the final pass is clean. No build, test, scanner, or validation command ran by
explicit owner direction. CORE-003 is now closed: replay growth results carry a
private one-use owner/phase identity and an exact allocation-byte reservation.
Pending grant bytes are serialized with live owner bytes, converted atomically
by the real allocation hook, and released when unused; diagnostic-off mode still
accounts valid owners without charging unowned allocations. Aggregate snapshot,
prediction-engine, and archive staging callers now grant only the backing they
actually allocate. Two serial read-only reviews closed live-cap, caller-budget,
guard-mode, pending-grant, and archive-constructor findings; the final pass is
clean. No build, test, scanner, or validation command ran by explicit owner
direction. CORE-004 is now closed: allocation headers carry tracker and owner
accounting generations, resets preserve real live and pending bytes while
starting fresh event totals, and stale frees remove only their authentic
backing. Replay grant, allocation, free, and reset transactions share one
session lock; development-tool mappings retain address-bound generation
tickets; foreign-header probes mirror the stable production layout. Three
serial read-only reviews closed live/pending reset, development-tool ticket,
foreign-header, transaction-boundary, and preserved-over-cap findings; the
final pass is clean. No build, test, scanner, or validation command ran by
explicit owner direction. CORE-005 is now closed: optional config absence is
limited to `ENOENT`; every other open, seek, or read failure reports through
`Core/EngineConfig`. One handle supplies both version and settings passes, and
parsed rows remain in a candidate copy until the complete stream succeeds, so
a failed read cannot publish a valid prefix. Two serial read-only reviews
closed the missing mid-stream failure proof and a CRT-specific directory
assertion; the final pass is clean. No build, test, scanner, or validation
command ran by explicit owner direction. CORE-006 is now closed: config reads
use binary mode, reject embedded NUL bytes, and require each fixed-buffer row to
reach a real LF, CRLF, or physical EOF boundary before parsing. Exact boundary,
continuation, Ctrl-Z, settings-pass rollback, and raw-NUL fixtures cover the
truncation paths. Three serial read-only reviews closed off-by-one, text-mode
EOF, settings-path, and embedded-NUL findings; the final pass is clean. No
build, test, scanner, or validation command ran by explicit owner direction.
CORE-007 is now closed: clipboard publication requires the live application
window, checks open, empty, allocation, lock, publish, and close failure, and
releases the movable allocation exactly until Windows accepts ownership.
Detached Windows fault operations cover every failure without touching the
machine clipboard, including exact terminating-NUL and unlock-before-publish
ordering. Two serial read-only reviews closed the null owner, portable CMake
inventory, and missing terminator-test findings; the final pass is clean.
Validation was deferred by explicit owner direction; no build, test, scanner,
or validation command ran.
RENDER-001 is reconciled as already fixed by the current render boundary:
`BeginConvexHullBatch` short-circuits before dereferencing a missing shader,
overwrites readiness with false, and `DrawConvexHullModel` skips the batch;
failed shader or dynamic-buffer creation retries at the next Begin. One serial
read-only review confirmed the visible and shadow paths are clean. No build,
test, scanner, or validation command ran by explicit owner direction.
RENDER-002 is now closed: the public zero-capacity stack fallback is removed,
and `ShadowPass::RenderShadowMap` requires the frame-owned caster batches by
reference. Those vectors reserve for maximum scene capacity during render
resource construction, retain capacity across frame clears, and serve both
shadow-map submissions. One serial read-only review confirmed allocation,
empty-scene, worker, lifecycle, and caller behavior is clean. No build, test,
scanner, or validation command ran by explicit owner direction. RENDER-003 is
now closed: text and quad flushes capture the bounded vertex count and reset
their CPU queue before every missing-resource exit, so a capacity-triggered
retry always appends from zero. Uploads use the captured count, and comments
describe per-segment rather than false per-frame draw totals. Two serial
read-only reviews closed the comment-truth finding and confirmed text, quad,
triangle, capacity, and synchronous-copy behavior is clean. No build, test,
scanner, or validation command ran by explicit owner direction. SCENE-002 is
now closed: scene snapshots publish through a unique sibling only after the
complete JSON has been written, flushed, and successfully closed, then replace
the destination atomically. Focused write, flush, close, and replace regression
cases cover prior-scene byte preservation and sibling cleanup. Two serial
read-only reviews closed the initial test-gap and cleanup-evidence findings;
the final pass is clean. Validation was deferred by explicit owner direction;
no build, test, scanner, or validation command ran. SCENE-007 is now closed:
snapshot material rows retain every exact nonzero emissive-color component even
when the material is Textured and strength is zero. An otherwise-default
sub-epsilon color case covers both the outer override-save decision and the
inner emissive-field decision through save, parse, and fresh-owner recreation.
Two serial read-only reviews closed the initial epsilon-boundary finding; the
final pass is clean. Validation was deferred by explicit owner direction; no
build, test, scanner, or validation command ran. CORE-008 is now closed:
`HashStr` converts each source character through `uint8_t` before the FNV-1a
mix, so high-bit UTF-8 bytes have one identity on signed- and unsigned-char
targets while ASCII hashes remain unchanged. Portable compile-time and runtime
fixtures bind ASCII, byte `0x80`, and UTF-8 `C3 A9` identities. One serial
read-only review confirmed constants, escapes, modulo arithmetic, recursion,
ODR behavior, and build-source wiring are clean. Validation was deferred by
explicit owner direction; no build, test, scanner, or validation command ran.
WORLD-004 is now closed: World Terrain is the render-vertex projection owner,
and height-map terrain UVs normalize post coordinates by the number of quads
per side, so the last post reaches the authored texture-wrap endpoint. The
exact-minimum grid regression pins every emitted UV pair and
would fail the old posts-per-side denominator. One serial read-only review
confirmed arbitrary-grid scaling, axis orientation, triangle order, and the
minimum-grid denominator are clean. Validation was deferred by explicit owner
direction; no build, test, scanner, or validation command ran. WORLD-005 is now
closed: World Terrain is the RAW asset-load owner and accepts only the exact
map-size-squared payload. A one-byte-oversized fixture uses a trailing Ctrl-Z,
proves the binary probe rejects the extra byte, checks the specific diagnostic,
and preserves the caller's existing terrain. Two serial read-only reviews are
clean after strengthening the initial zero-byte fixture for Windows text-mode
regression coverage. Validation was deferred by explicit owner direction; no
build, test, scanner, or validation command ran. ASSET-005 is now closed: AssetSystem is
the path-resolution owner and treats a drive prefix as absolute only when a
root separator follows the colon. The focused regression distinguishes
drive-relative `C:asset.jpg` from both drive-root separator forms, so resolution
cannot consult the process's ambient per-drive directory. Two serial read-only
reviews are clean after narrowing one test-header comment to drive-prefixed
paths. Validation was deferred by explicit owner direction; no build, test,
scanner, or validation command ran. GAME-003 is now closed: Gameplay retains
ordinary float arithmetic until its fixed-step clock first stalls, then keeps
elapsed time, lifecycle math, replay checkpoints, prediction archives, and
visual phase progression precise beyond that boundary. Solver snapshot v5 and
prediction raw-v5/sectioned-v6 preserve the double clock while their historical
read, write, and hash branches retain the legacy float layouts. Four serial
read-only reviews closed replay/prediction propagation, visual compatibility,
exact-float restore, archive-probe isolation, schema-negative, and test-gap
test findings; the final pass is clean. Dedicated historical sectioned-v4
prediction and solver-v2-v4 byte fixtures remain deferred coverage; static
review found their layout discrimination coherent. Validation was deferred by
explicit owner direction; no build, test, scanner, or validation command ran.
MATH-005 is now closed: `Matrix4::Inverse` measures singularity against the
four determinant expansion terms instead of an absolute world-unit cutoff.
Finite small and translated scales therefore retain their reciprocal transform,
while cancellation inside sixteen float epsilons and exact dependent rows keep
the documented identity fallback. Two serial read-only reviews closed a
missing negative case with paired inside/outside cancellation fixtures; the final pass
is clean. Validation was deferred by explicit owner direction; no build, test,
scanner, or validation command ran. MATH-003 is now closed: `Vector3` retains
the established arithmetic and exact output bytes when its squared magnitude
is finite, while large finite components whose square overflows are scaled by
their largest absolute component before normalization. NaN and infinity are
rejected before publication, and both normalization APIs preserve their source
and output atomically on failure. Three serial read-only reviews closed the NaN
class and byte-atomicity test gaps; the final pass is clean. Validation
was deferred by explicit owner direction; no build, test, scanner, inventory,
or validation command ran. MATH-007 is now closed: `Quaternion::Normalise`
keeps its exact ordinary finite arithmetic, near-zero identity cutoff, and
component order, while large finite values are scaled by their largest absolute
component before normalization and NaN/infinity reset to identity. Three serial
read-only reviews closed ordinary-byte, threshold-boundary, nonfinite-component,
and hot-path test-gap findings; the final pass is clean. Validation was
deferred by explicit owner direction; no build, test, scanner, inventory, or
validation command ran. MATH-006 is now closed: ordinary finite half-space
classification retains its exact float operation order, while overflowed
normal length, signed distance, or expanded radius falls through to positively
scaled double arithmetic. Positive scaling preserves the half-space, negative
scaling reverses it, and invalid coefficients remain conservatively visible.
Four serial read-only reviews closed robust-routing, threshold, epsilon-unit,
nonfinite-policy, cancellation, and test-oracle findings; the final pass is
clean. Validation was deferred by explicit owner direction; no build, test,
scanner, inventory, or validation command ran. MATH-001 is now closed:
`RotatePointAboutArbitrary` uses the same right-handed active convention as
`Quaternion::RotateAboutAxis`, so editor group offsets and orientations follow
one positive angle around the captured pivot. Cardinal and non-cardinal parity
fixtures cover all six corrected Rodrigues sine terms. One serial read-only
review found no blocking or non-blocking issue. Validation was deferred by
explicit owner direction; no build, test, scanner, inventory, or validation
command ran. MATH-002 is now closed: equatorial element recovery selects its
perifocal Y convention from the angular-momentum direction, preserving both
prograde and retrograde circular and eccentric epoch states. Two serial
read-only reviews closed a prograde-compatibility test gap; the final pass is
clean. The unchanged node-magnitude threshold remains optional boundary
coverage. Validation was deferred by explicit owner direction; no build, test,
scanner, inventory, or validation command ran. MATH-004 is now closed: ordinary
orbital propagation retains its established float arithmetic, overflowed or
underflowed mean motion falls back to the equivalent double-range formula, and
unrepresentable propagation or sampling fails without publishing nonfinite or
partial output. Two serial read-only reviews closed the recovery-oracle and
comment-truth findings; the final pass is clean. Validation was deferred by
explicit owner direction; no build, test, scanner, inventory, or validation
command ran. APP-001 is now closed: automated scene advancement retains the
actual queued-load result, gives that failure precedence over a normal quit,
latches its immutable diagnostic into `ApplicationExitState`, and posts a
nonzero platform fallback. Two serial read-only reviews closed an end-to-end
exit-wiring test gap; the final pass is clean. Validation was deferred by
explicit owner direction; no build, test, scanner, inventory, or validation
command ran. DIAG-001 is now closed: perf-scene automation becomes active only
while RuntimeDiagnostics owns the required CSV handle. A failed open leaves
scene rerun/advance disabled, a successful open activates it, and close clears
the signal after the current transition has consumed it. One read-only review
found no blocking or non-blocking issue. Validation was deferred by explicit
owner direction; no build, test, scanner, inventory, or validation command ran.
AUTO-002 is now closed: trace and report targets are resolved against immutable
interaction input before any truncating open, including relative components,
existing links, and Windows case aliases. Fixed path copies fail closed, and an
unsafe report target suppresses Run's normal failure-write callback. Two serial
read-only reviews closed long-path and destructive-seam evidence gaps; the final
pass is clean. Validation was deferred by explicit owner direction; no build,
test, scanner, inventory, or validation command ran. AUTO-001 is now closed:
each manifest owns filename-derived scene and replay sidecars, and arming fails
closed when any final or partial publication destination is already occupied.
The focused fixture preserves the first manifest and both sidecars through a
second recording in the same directory and covers inverse artifact-role
collisions. One serial read-only review closed the inverse-collision and replay
metadata coverage gaps; the final pass is clean. Validation was deferred by
explicit owner direction; no build, test, scanner, inventory, or repository
validation command ran. DX12-003 is the next selected candidate: it is a high-
impact memory-safety defect with bounded fixed-array admission sites.
Deterministic Collision Modes And Ragdoll Unification is parked after FP4;
its completed work established the binding
deterministic Discrete collision with automatic Swept TOI
promotion and isolated A/B evidence. FP2's retired sphere-box test surface and
two reachability repair rows are gone, both live orders retain the exact sweep
owner, and focused/all-config evidence is green. FP2 is closed through the
standing archive-bound automated Physics override: core, deep, and replay-
visual gates pass, and the artifact manifest retains the prior and new
first-party game executables, hashes, and the 6,800-frame 200-box comparison.
Third-party runtime DLLs are intentionally omitted from tracked artifacts and
must be restored from the pinned setup/build inputs before an isolated launch. The
replay witness runs one 2,401-tick generation, moves all 200 wall bricks,
publishes 200 causal nodes, and rejects every registered negative control.
FP3 and FP4 are complete. FP4's exact-final direction-valid radius comparison
keeps 95.66%-100% of measured body ticks Discrete and reduces mean Physics time
by 6.98%-51.30% across the mixed, 520-body, and 2,000-body retained runs. The owner
signed off on the result and exact current-tree performance-baseline transition.
The owner made the radius rule the sole shipping policy, retired the absolute
threshold and runtime selector, and authorized the governed baseline refresh.
Sphere radius-squared, oriented-box local-axis half-extent checks, and cached
centered-difference hull face and edge-cross-edge SAT axes keep elongated shapes Discrete unless travel
exceeds a valid shape width. SkullScope records per-frame and per-object policy state and
`physics_query motion` reconstructs promotions and demotions. FP5-FP9 remain
parked in `WNF/`. Runtime
Boundary Separation And Project Topology RBS0-RBS7 is complete at 8/8. RBS7
repaired the allocation rulings, replaced the Replay startup context with a
bounded startup state handler, moved Operator UI projection into Runtime/UI, and
closed submission/command protocol ownership in the composition root.
Strict Runtime enforcement now passes with zero forbidden sites, zero repair
debt, zero reverse-App edges, and no multi-package SCC. Runtime/App alone
composes cross-owner effects; native host, frame metrics, operator UI projection,
GPU submission, and domain owners exchange bounded values and commands. All
ten tracked Visual Studio projects remain inside the closed topology policy.
The approved Rendering library owns the exact Rendering source closure; all
five configurations and the portable CPU build pass. Pushed integration commit
`7a3e952d2` passed cumulative `validate_fast` and the ASSET/RBS focused overlap
tests. Completed plan files remain deleted under the repository convention;
Git history retains their phase evidence.

Game UI Component Library Separation UI0-UI6 is complete at 7/7: all tracked
UI files have documented ownership decisions; explicit stateless geometry and
state contracts cover the
proved shared component families; and the seven retained wrappers now route
through those contracts without changing product fingerprints, command values,
or interaction geometry. Repeated Replay/Planning controls and fitting Runtime
badges now use shared components without moving semantic owners. Product
composition sits above the reusable foundation, project/test boundaries retain
all 31 portable determinism cases, and UI6 closed Replay popup bounds through
their Prediction and Replay/Planning owners. Its exactly-once plan-completion
gate passed 760/760 cases and 2,685,294 assertions with unchanged baselines;
the inherited causal-depth oracle and historical Physics performance baseline
remain external recorded failures without refresh authority.
Recorded Interaction Playback Cursor RIC0-RIC3 is complete at 4/4. The
Automation-owned frame value crosses one bounded Runtime/UI compositor and one
App submission edge after GameUI, replay overlays, UI finalization
but before screenshots and Present. The exact unchanged 413-turn recording
contains 256 visible turns and 157 cursorless right-look turns; command high
water and capacity are both `2`; recorded coordinates and every legacy trace
field remain unchanged. Focused tests pass 10/10 cases and 222/222 assertions,
and independent review is blocker-free after requiring successful submission
for observed visibility and correcting the sample/delta allocation inventory.
The exactly-once plan-completion command rebuilt Automation cleanly and then
stopped at the existing plain-language wording queued for the next governance
task; it was not rerun. No golden, manifest, sidecar, or schema changed.

Governance Simplification And Scar Removal is complete at 5/5 with
blocker-free independent terminal review. Earlier DB work
already removed most registry and meta-tool administration; this closure
deleted the remaining deterministic-math source-fingerprint registry and empty
native-diagnostics suppression file, repaired stale reviewer and validation
metadata, and documented the guarded one-command Physics baseline updater as
the core workflow. The four-configuration `/W4 /WX` build, byte-exact Physics
matrix, all six CPU lanes, and the focused checker self-tests and live scans
pass. The accepted Physics golden remains
`1b98431012f632d66cb18c50e3f253cea4898b57bcb8e78cdadd0de3f065e387`.
The branch-wide compiler-backed source-design scan passes all 30 changed source
files after bounded DX12 direct draw operations, ordered
interaction-automation dispatch, and scene-writer refactors; no exception or
weaker selection was added. The
historical ledger infographic remains intact apart from two stale review-term
attributes, and no golden baseline changed. The exactly-once
`tools\agent_validate.bat --plan-completion` run passed Debug, Physics, fast
preflight, all six CPU lanes, and Automation, then stopped at the inherited
DX12 terrain-baseline mismatch already recorded by run `20260827T174544Z`
before the governance DX12 edit. The water output is byte-identical across the
two failing runs, `space_three_body` remains byte-exact, the command was not
rerun, and no visual baseline was refreshed.

Real-Time Physics Pacing SP0-SP2 is complete. Live and unlimited scenes now
schedule fixed-frequency Physics from elapsed wall time, while explicit startup
intent and finite unattended capture can select deterministic render-frame
lockstep. The scheduler retains fractional time across capped catch-up, reports
dropped whole ticks and hitch events, and safely saturates enormous finite tick
requests before integer conversion. VSync remains presentation-only.

The focused SimulationSystem family passes 13/13 cases and 2,802 assertions;
the UI fingerprint control passes 1/1 case and 51 assertions, and the standalone
UI boundary probe renders all 11 detached surfaces. `validate_fast`, all 716
unit cases and their assertions, `validate_physics`, Automation smoke, coverage,
real DX12 validation, and `tools\agent_validate.bat --plan-completion` pass. The
44,401-line Physics golden remains byte-identical at
`debf57f744774d4e7c1eb5cc61f05ba6e41dc6dc997ad20db6c91b02b0958c32`.
The Replay visual gate passes 18/18 typed-packet controls and 82 assertions,
then reproduces the inherited reveal-0 `header.futureNodeCount` mismatch: the
current corrected packet has 201 future nodes and 806 trajectory records while
the preserved oracle expects the retired 200/802 topology. No Physics, Replay,
or visual golden was refreshed. Independent review is blocker-free, and the
touched-file comment audit checked 39/39 files with zero deferrals.

CORE_REDUCTION CR0-CR5 are complete. Owner direction activated the plan without a
new branch. The clean `cc194f9aa` baseline contains 615 tracked production
source files and 207,080 physical lines. CodeGraph is current; Debug, Profile,
and Automation builds pass; final decorated-symbol reachability reports 87 ruled rows
and zero blockers. No dead-production candidate was approved merely to create
savings. The accepted work is the GameUI naming correction plus exact DX12
completed-fence, Replay capacity, cold `FILE` deleter, and render-lifecycle log
duplication. CR1 renamed the complete built-in development surface to GameUI,
including automation values/assertions, runtime fields, labels,
fixtures, and manuals. The focused 7-case/170-assertion set, 832-item project
filters, explicit GameUI launch, and full GameUI stress matrix pass across
Profile, Debug, and Automation with zero DX12 validation errors. CR2 reran the
current reachability inventory: all 93 rows remain explicitly ruled, zero rows
block, and the CR0 ledger still approves no production deletion. CR2 therefore
closed honestly at 0 additions, 0 deletions, and 0 net reduction. CR3 replaces
four identical DX12 retirement fence-observation blocks with one private
`Dx12FrameOwner` predicate while leaving caller-specific release policy intact.
The phase removes 5 net production lines; CPU architecture tests and the real
DX12 screenshot/InfoQueue gate pass with zero validation errors. CR4 consolidates
Replay capacity calculation, four cold `FILE*` deleters, and two render lifecycle
log forwarders. It removes another 71 net production lines; 18 focused cases and
822 assertions pass, and Profile builds cleanly. CR5 repaired process-local
Replay topology restoration and archives the renderer-visible committed
presentation bank, then closed at `+303/-373` production lines: a 70-line net
reduction. All seven ownership inventories, dependency and project gates,
`validate_fast`, 692 tests / 2,537,437 assertions, Automation, physics, DX12,
the terminal `validate_full --plan-completion` gate, and one-minute graphics
stress pass. Independent review found no blocker. The mapped Replay visual gate
still rejects the inherited corrected 201-node/806-record topology and changed
physics-derived wall outcome against its retired 200-node/802-record baseline.
The topology mismatch predates CR0; the wall outcome follows the owner-approved
2026-08-21 physics golden. No physics, Replay, or visual golden was refreshed.

REPOSITORY_CLEANUP (RC0-RC5) is 100% complete and closed.
- RC0 rebased inventory and established strict absolute-path deletion manifests.
- RC1 deleted 69 historical TestOutput children and obsolete Agentic/Reports/Temp scratch (51.14 GiB reclaimed).
- RC2 removed 7 clean history-reachable detached worktrees and pruned Git worktree metadata (22.07 GiB reclaimed).
- RC3 removed derived build, IDE, and test build artifacts (6.59 GiB reclaimed). Total disk reclaimed: 79.80 GiB.
- RC4 deleted 10 approved tracked candidates (old water baseline, 7 shadow reference images, Concepts mood board,
  and lone DONE/ completed plan), reconciled MASTER-PLAN.md, and repaired WNF contact-stack report link.
- RC5 verified all governance scans, passed fast validation, received Rubber Duck PASS verdict, and closed the plan.

ORBIT_FORECAST OF0 is complete. It ratifies the fixed sun as the primary,
Earth and Mars as core bodies, and the ship as an auxiliary whose orbital-
configuration failure does not by itself end the system-wide horizon;
numerical health remains globally blocking. The owning plan records exact radial
envelopes, a 600-tick sustained escape rule, core collision policy,
reset/retirement semantics, informational conservation drift, and the existing
5.0 ms worker slice plus separate frame-admission deadline. Two 120-second
fixed-step live captures
are byte-identical at SHA-256
`F3D71F660228561D155E11511FDF58DBAD8F5EF966765A21B92B711420C2AE62`.
Two isolated bounded forecasts both pass and share their rendered submission
hash, but their private simulation hashes and trajectory fingerprints differ;
OF2 and OF6 own that pre-existing determinism closure.

ORBIT_FORECAST OF1 is complete. `ContinuousPredictionSampleRing` preallocates
all-body rows through the existing Replay prediction reserve owner, publishes
each absolute-tick row only after every body is complete, and exposes a logical
oldest-to-newest snapshot with one or two wrap-safe physical segments. Slot
versions make concurrent copying retry instead of accepting torn data;
cancellation and checked counters fail closed. The focused group passes 6/6
cases and 116/116 assertions, its concurrency case passed ten stress runs, and
`validate_tests` passes 610/610 cases with 2,483,870 assertions.

ORBIT_FORECAST OF2 is complete. `ContinuousPredictionProducer` snapshots the
live body and solver values into a private Physics engine, advances unlimited-
target whole ticks under separate five-millisecond frame-admission and worker
slice clocks, and publishes only complete all-body positions through the OF1
ring. The focused case passes 53/53 assertions through three 14,401-row wraps;
retained bytes and Replay growths stay flat after warm-up, live and bounded
prediction state remain unchanged, tick 1,024 is exact across zero-thread and
one-worker execution, and stop joins in-flight work. `validate_tests` passes
611/611 cases with 2,483,563 assertions.

ORBIT_FORECAST OF3 is complete. The solar scene authors a fixed-capacity
Primary/CoreOrbiter/Auxiliary stability contract that setup resolves to stable
scene-object IDs and snapshot saving round trips through current entity names.
The Planning-owned analyzer consumes detached complete tick publications,
globally blocks numerical failure, distinguishes system-wide primary/core
orbital failure from auxiliary-only failure, applies the exact softened
fixed-primary 600-tick escape law, and publishes normalized all-member
conservation drift plus first-failure evidence. Eight focused cases cover the
positive and negative semantics; `validate_tests` passes 620/620 cases with
2,487,883 assertions. All seven ownership inventories are current, the 17/17
touched-source comment audit has no deferrals, and the change adds no Replay
include, reserve privilege, Physics row field, or post-start growth path.

ORBIT_FORECAST OF4 is complete. Planning now composes the continuous producer
and stability analyzer; App owns lifetime, frame admission, mutual exclusion
with bounded `PREDICT`, scene-transition joins, and shutdown. GameUI
routes typed continuous/reset/exit commands and publish aligned detached status,
timing, first-cause, and conservation readouts while retaining the accepted
bounded-horizon control discrepancy. `validate_fast`, all 620 unit cases,
dependency, allocation-policy, and performance gates pass; the 21/21 touched-
source comment audit has no deferrals. Physics and replay visual gates reproduce
only their inherited owner-controlled CSV and topology-version mismatches, and
no oracle was refreshed.

ORBIT_FORECAST OF5 is complete. Planning converts the detached logical sample
ring into fixed-capacity, double-buffered generic ribbon and head-marker
packets. Authored colors and one coherent newest absolute tick reach every
configured body; logical oldest-to-newest sampling never draws across the
physical ring seam. The generic DX12 retained-range path now accepts compact
record prefixes while proving every populated range remains inside both its
reserved lane and supplied span. Focused wrap/downsampling tests pass, and the
solar automation probe captures pre-wrap and post-wrap views with an advancing
absolute forecast tick and overwritten old geometry. The 15/15 touched-source
comment audit has no deferrals, and all seven ownership inventories are current.

ORBIT_FORECAST OF6 is complete. Automation separates process-local source
clock metadata from deterministic private-simulation hashes and covers every
private frame, body pose/velocity/sleep value, tornado elapsed value,
contact-completeness flag, and `PhysicsDebugContact` field. A paused solar
120-second witness produces 14,401 identical private frames with workers 0 and
1, identical submitted geometry hash `0x0E0FF9DB0F2EF6E0`, and flat Replay
reserve growth from 475 to 475 after warm-up. Focused continuous-prediction
tests pass 7/7 cases and 173/173 assertions; the complete Profile suite passes
622/622 cases and 2,520,795/2,520,795 assertions. Dependency, strict Replay
allocation, DX12, one-minute graphics stress, and performance pass. All seven
ownership inventories are current, the 4/4 touched-source comment audit has no
deferrals, and the independent post-fix review reports zero findings. Physics
and replay visual fidelity reproduce only the inherited owner-controlled varied
CSV and reveal-0 `header.topologyVersion` stops; no oracle was refreshed.

The owner parked Deterministic Trigonometry under `Agentic/Plans/WNF/` on
2026-08-18 and replaced its active slot with At-Rest Ball Stability. The new
plan removes the authored witness's arbitrary 1,800-frame timeout: `at_rest`
now runs unlimited and must not auto-exit until a generic authored requirement
proves `ball_a`, `ball_b`, and `ball_c` are all Physics-asleep. SkullScope
diagnosis and semantic negative controls precede tuning, and vertical
vibration, excessive sliding, rolling reversals, and delayed supported sleep
remain separate quality metrics. Friction and sleep policy remain valid
diagnostic hypotheses, but proper contact/solver/support fixes come first and
any production policy change is blocked on isolated evidence plus explicit
owner approval.

Catto CD5 retained the authored partial-TOI sequence and derives one local
contact interval from the remaining time of each awake dynamic participant.
Terrain uses its dynamic body's interval, two awake dynamics use the shorter
interval, and authored-fixed or sleeping solver-static bodies contribute no
clock. Baumgarte, terrain support/friction, and object constant friction use
that interval; near-zero intervals disable bias and friction while retaining
impact restitution. Six focused interval cases are included. The full tests
pass 590 cases / 2,480,638 assertions, performance and the independent review
are green, and the terminal gate passes preflight, CPU/coverage, Automation,
and DX12 before the approved immutable physics golden reports 20,394 changed
rows. Physics and replay candidates plus their generating executables remain
under `TestOutput/validation/candidates/CATTO_REPAIRS_CD5*`; no golden was
refreshed.

Catto CD4 added scalar accumulated-impulse warm starting to point joints and
proved a loaded ten-link chain reduces final sag by about 30.5 percent and peak
sag by about 11.8 percent. Because the cache is next-step state, solver
checkpoints now persist it through durable scene identity in version 3, with
preflight-before-mutation restore, stable topology trimming, legacy cold-cache
compatibility, sparse deltas, hashing, and cold Clear/recreate coverage. The
full test gate passes 584 cases / 2,480,611 assertions; replay artifact and
strict allocation gates pass with snapshot high water 3,401,552 bytes and
recorder high water 16,223,044 bytes. The varied physics candidate remains
byte-identical to CD3; replay timing moves for 155 nodes. Fresh executables,
CSV, report, logs, and replay are preserved under
`TestOutput/validation/candidates/CATTO_REPAIRS_CD4*`; no golden was refreshed.
All governance inventories, the 19/19 touched-source comment audit, and the
independent re-review are green.

Predicted Solver Cause Hierarchy was registered last on 2026-08-18 after an
adversarial plan review. Its High mode retains exact predicted Body -> Manifold
-> SolverRow evidence; Low keeps the selected-root trajectory topology while
hiding causal inspection and releasing all detail capacity through an
observable F6 checkpoint. The bottom-timeline `HIGH DETAIL` checkbox replaces
the mouse Pause button, remains on by default, and preserves keyboard `P`.

PSD0 is complete. Pure policy values and tests pin High-default transition
effects, preference persistence across generation and archive boundaries, and
explicit selected-tree versus authored-space presentation. The at-rest witness
retains flat synthetic rows and `SolverDetailNotAvailable`; the seeded ordinary
generated demo has one selected root (body 162) with private mutual gravity and
draw-list all-body presentation both false, while `solar_system.scene.json`
retains four intentional roots with both facts true. Automation reports now
serialize complete bounded trajectory, future-node, and cause-row topology.
Focused tests pass and the touched-source comment audit is 5/5 with none
deferred. `validate_tests`, `validate_fast`, and `validate_automation` are
green. The canonical replay visual gate retains the inherited one-frame causal
golden mismatch (`topology[0].firstFrame` 137 -> 136), so no golden was
refreshed; the exact executable/report/artifact evidence is preserved under
`TestOutput/validation/candidates/PREDICT_SOLVER_DETAIL_PSD0/`. Its candidate
comparison and all nine mutation-control families pass.

PSD1 is complete. `ReplayPrediction` owns the sole retained High/Low mode and
the typed command reaches it through both established replay input seams. The
former mouse Pause slot is a shared checked-by-default High Detail checkbox;
drawing, hit testing, and automation use one rectangle, while keyboard `P`
retains ReplayRuntime play/pause and cross-scene pause remains a separate typed
UI command. Prediction-mode transitions clear prediction inspection and exit
its camera without disturbing recorded inspection. The full 595-case suite,
all nine fast-gate stages, Automation smoke, one-minute graphics stress, and all
eight isolated DX12 stages pass. The touched-source audit is 19/19 with none
deferred. The canonical replay visual oracle retains only the inherited 137 ->
136 causal first-frame mismatch; no golden changed, and the exact PSD1 evidence
plus candidate baselines live under
`TestOutput/validation/candidates/PREDICT_SOLVER_DETAIL_PSD1/`, where the
candidate comparison and all nine negative control families pass.

PSD2 is complete. Paired immutable segmented evidence banks now provide stable
generation/mode/epoch/frame/topology/publication identity, release/acquire
prefix publication, independent build/committed promotion, overflow-safe
reserve, cancellation, and explicit capacity release at the post-join High ->
Low boundary. The measured 20/120-second matrix selected 128-frame,
256-contact, and 1,024-pipeline segments with a 320 MiB per-bank cap. The dense
120-second witness measured 317,157,376 bytes; two banks plus the prior base
working-set high water total 653,016,512 bytes, so the shared
`replay_prediction_working_set` cap is now 960 MiB with 1.542x headroom. F6 and
diagnostics expose current/peak, bank split, and release checkpoints. Five
focused cases, the full 600-case / 2,485,514-assertion suite, strict allocation
policy, project filters, UI fingerprint, and all nine fast-gate stages pass.
The touched-source audit is 14/14 with none deferred.

PSD3 is complete. `ReplayPrediction` now owns the typed selected-causal versus
all-bodies-space presentation policy, carried through scene/demo seeding,
promotion, archives, views, and draw lists without Physics-force inference.
Ordinary scenes publish the selected root plus only its debug-contact cascade;
six authored mutual-gravity showcase scenes explicitly preserve all-body space
paths. Parser/snapshot/archive, promotion, cycle, disconnected-body, partial-
budget, and draw-helper tests pin the contract. Runtime witnesses record one
generated-demo root with 240 future nodes and four explicit solar roots. The
full suite passes 601 cases / 2,484,572 assertions, Automation smoke passes,
all nine fast-gate stages pass, and the touched-source audit is 24/24 with none
deferred. The immutable replay visual oracle was not refreshed: it correctly
rejects PSD3's 201 contact-derived depth-1-to-11 nodes and 806 trajectory
records against its retired 200-node flat topology and 802 records. Exact
reports, logs, and executables remain under
`TestOutput/validation/candidates/PREDICT_SOLVER_DETAIL_PSD3/`.

PSD4 is complete. High prediction builds now acquire the full-pipeline consumer
only on their private Physics engine, publish exact persistent-contact and
ordered pipeline evidence behind the matching sealed frame prefix, and balance
the consumer across promotion, cancellation, restart, and destruction. Low
builds allocate and copy no solver evidence. High and Low generated-demo
witnesses publish the same 90-frame private simulation hash
`0x18C9CE2B02FF5399`; High retains 14 contacts / 24,364 pipeline rows with two
balanced acquire/releases, while Low retains zero evidence and performs no
acquire/release. Terrain and dense witnesses retain non-empty exact evidence,
with the 200-body case remaining inside the 320 MiB bank cap. The full
602-case / 2,484,212-assertion suite, Automation, strict allocation,
frame-spike, and all nine fast-gate stages pass; compiled reachability has 90
ruled rows and zero blockers. The touched-source audit is 9/9 with none
deferred. The stale Physics CSV and PSD3 visual-topology oracles remain
unchanged and are recorded in the phase evidence; neither mismatch is caused
by PSD4.

PSD5 is complete. Planning now consumes source-neutral recorded or predicted
solver-detail views with exact generation, bank-epoch, topology, publication,
frame, range, sequence, feature, and completeness checks. High detail emits the
Body -> Manifold -> SolverRow hierarchy from immutable Prediction evidence and
matching predicted poses; Low retains synthetic PredictionContact/Motion rows.
Scrubber selection, manifold presentation, and camera focus preserve the exact
evidence identity, including same-frame replacement rejection. Final High and
Low witnesses share live solver hash `0xFC1E96D513B66A0B`: High has 4 Body / 3
Manifold / 3 SolverRow / 0 synthetic rows with balanced 2/2 consumer activity,
while Low has 4 Body / 0 Manifold / 0 SolverRow / 3 synthetic rows and zero
evidence capacity. The 603-case / 2,483,802-assertion suite, Automation, strict
allocation, and four-generation frame-spike gates pass. The touched-source
comment audit is 14/14 with none deferred. The unchanged Physics oracle still
differs in 20,394 lines beginning at frame 102, and the replay visual oracle
still rejects the corrected topology at reveal 0 (`header.topologyVersion`);
neither baseline was refreshed.

PSD6 is complete. RVPD schema 4 carries captured capability, explicit path
policy, ordered section descriptors, cumulative byte closure, lightweight
state, and optional bounded High event-frame/contact/pipeline evidence. Load
preflights the complete layout and builds reserve-accounted candidates before
atomically replacing prediction and evidence-bank state. High restores exact
inspection without Physics; active Low validates but does not retain High
evidence; Low and v2/v3 never upgrade. Repeated load rebases bank epochs, and
all corruption/future-schema failures preserve the prior state. Allocation,
Automation, replay-v2 artifact, and all nine fast stages pass, including 603
tests / 2,485,922 assertions and zero-warning Automation/Debug/Profile builds.
The touched-source comment audit is 11/11 with none deferred. The visual gate
reaches only the inherited reveal-0 `header.topologyVersion` mismatch after the
archive controls complete. Performance passes DX12 and absolute Physics
budgets; the unchanged relative Physics sample oscillates around its threshold
across three runs while memory improves, so no baseline was refreshed.

PSD7 is complete. App now samples complete replay totals and independently
summed categories immediately before and after the synchronous High -> Low
evidence release. The release oracle rejects stale totals, mismatched category
snapshots, capacity disagreement, underflow, and fabricated extreme values; the
final witness proves matching positive evidence/replay/category deltas of
1,925,120 bytes. One shared availability predicate governs predicted cause-row
publication, rendering, hit testing, automation, and transition clearing, so
Low visibly removes the cause window while leaving the compact High Detail
control available. The at-rest workflow passes 19 assertions through frame
2,094 and rebuilds fresh High detail; the multi-body witness passes six
assertions through frame 1,002 with three manifolds and three solver rows.

The full 604-case / 2,484,279-assertion terminal suite, all seven ownership
inventories, strict replay allocation, four-generation frame-spike, replay
artifact, dependency, fast, Automation, and DX12 gates pass. Touched-source
comment audit: 13/13 checked, zero deferred. The independent review first found
the retired tautological memory proof and solver-panel-only Low assertion; both
were corrected, and the fresh post-fix review found no remaining blocker. The
immutable visual oracle still stops at inherited reveal-0
`header.topologyVersion`, Physics retains its inherited 20,394-line mismatch
from frame 102, and relative Physics performance remains noisy while absolute
budgets pass. The terminal `tools\agent_validate.bat --plan-completion` rerun
passes preflight, every mandatory CPU lane, Automation, and DX12 before
stopping at that exact inherited Physics mismatch; its verbatim log is
`TestOutput/validation/PREDICT_SOLVER_DETAIL_PSD7_agent_validate.log`. No
baseline was refreshed.

Catto CD3 replaced the global summed squared impulse-delta early-out with the
maximum per-contact-row squared delta while retaining the historical sum for
diagnostics only. The focused eight-contact oracle fails the retired gate and
the full test gate passes 582 cases / 2,479,968 assertions. The deterministic
physics mismatch changes 13,369 rows across 23 bodies from frame 102, while the
replay causal candidate moves `topology[1].firstFrame` from 154 to 174. Exact
Debug and Automation/Profile executables, CSV, report, log, and replay remain
under `TestOutput/validation/candidates/CATTO_REPAIRS_CD3*`; no golden was
refreshed. Performance, dependency proof, all seven inventories, a 3/3 touched-
source comment audit, and independent review are green.

Catto CD2 landed R5 and R6 as separate commits. R5 removed terrain manifold-
count scaling from restitution; R6 removed the hardcoded solver-local quiet
velocity snap so `PhysicsSleepController` remains the configured transition
owner. Focused tests pin the 4.5 metres-per-second target, one-row/four-row
agreement, and preservation of `0.04` linear / `0.01` angular residual motion.
The full test gate passes 581 cases / 2,479,964 assertions. R5 and R6 divergence
executables, reports, logs, CSVs, and replay artifacts remain under
`TestOutput/validation/candidates/CATTO_REPAIRS_CD2_*` for later owner
assessment. By explicit owner direction, the canonical physics, replay visual,
and replay causal goldens were refreshed; `validate_physics` and the complete
replay visual-fidelity gate now pass. All seven governance inventories are
green, the touched-source comment audit is 2/2 checked with none deferred, and
independent review found no implementation or ownership blocker.

Catto CD1 replaced per-contact-row position projection with one deepest-row
linear correction per contiguous manifold, inverse-mass shared and accumulated
to one body-store publication. The focused family passed 13 cases / 172
assertions and the full test gate passed 579 cases / 2,479,944 assertions. The
physics gate reached its expected immutable-golden mismatch (40,905 lines); a
direct T4 comparison is identical through frame 289 and first differs at frame
290. The replay gate's build and 17 cases / 75 assertions passed before its
older causal golden reported firstFrame 185 -> 184.

The independent review required and cleared a same-scene Automation A/B. Its
non-physics envelope was identical, while the Physics-owned trajectory and RVPD
hashes diverged; a second CD1 run reproduced canonical projection hash
`E16AD39C7CA4CAF8E3DE4F809F3E7FACB313B5AF13CA72216FE0CA59D0D465D3`
and byte-exact replay hash
`932DC9FADFBEED901D27743C4D90C2F1849BBCAC7406C0B62B138C1657F53765`.
Direct, replay, and pre-CD1 attribution candidates are retained under
`TestOutput/validation/candidates/CATTO_REPAIRS_CD1*`. The owner accepted the
improved CD1 simulation on 2026-08-17 and authorized the varied, shooting,
space, known-issue, query, replay-visual, and causal golden refresh. All seven
ownership inventories are green and the touched-source comment audit is 4/4
checked, 0 deferred.

Determinism T4 replaced ragdoll `acosf` with the shared deterministic vector-
angle owner and closed the Physics-reachable CRT transcendental set. Focused
ragdoll tests passed 4 cases / 31 assertions, the 0/1/4-worker oracle passed
30,709 assertions, and independent ownership review returned clear. The full
gate passed 578 cases / 2,479,932 assertions, coverage, standalone CPU suites,
Automation, and DX12 before the expected inherited 40,909-row Physics mismatch.
The direct candidate is retained under
`TestOutput/validation/candidates/TIER2_DETERMINISM_T4/` with CORE hash
`8DAAB85DAF180C7292FB4203AA64FC671FA52EB26892C439816795929F46819D`,
TESTS hash `44F60FD4EEDDBCB1B75A00BC7A32D83FE39FD95F7BC11B25DF29B1568F0B96C8`,
and CSV hash `0F25F3B6813401B7D9EA4B52CBB088D30E03EEBAFFEFCAF15FEE63B3DFF72FFD`.
The final full-gate candidate in the same directory has CORE hash
`C09EEF7B8B616349BD979BDFC1AD10D58FB36214322F8338EA0A417C8AB8B774`,
TESTS hash `182D75A1CB79294AB9A51A5622B0331B9F4A95C629F0CD5C744F174AFF24D5C3`,
and the identical CSV hash. That CSV is also byte-identical to T3, proving T4
added no further drift in the varied regression scene. No golden was refreshed.

Replay Prediction Adversarial Repair completed on
`codex/replay-prediction-adversarial-fixes` in two `REPLAY_ADVERSARIAL` commits.
It corrected all five committed-frame readers, repaired the Automation evidence
harnesses, added deterministic best-fit reuse and whole-node append resume,
bound markers to complete coherent publication, and recorded real-run retained
high water. Its completed plan was deleted from the live queue; Git history is
the audit archive. No baseline was refreshed.

Source Modernization Sweep, Dense Pile Sleep Resolution, Broadphase Dense
Dedup Restoration, and Look Lab Random Style Authoring remain closed by owner
direction.

The mandatory CPU CI lane was repaired this session; see the CI note below
before assuming a red lane is your change.

## Merge Note

`nightrunner-14th-AUG-26` was merged into `main` on 2026-08-16 by owner
direction, and repository validation was skipped on an explicit owner decision.
The merged tree has therefore never been built. Both sides passed independently
first — the branch passed `tools/validate_full.bat` end to end at its tip, and
`main`'s fifteen intervening commits changed only documentation, CI workflow,
and `tools/validate_native_diagnostics.py`, touching no engine source — so the
merge had no source-level conflict and only the two ledger files needed
reconciliation. Treat the first validation run on `main` after this merge as
the gate that was deferred, and do not read the branch's green full-gate result
as covering the merged tree.

That deferred merged-tree gate is now discharged by the adversarial-repair
branch. Its exact final source state passed `tools\validate_fast.bat` and
`tools\validate_full.bat` without a baseline refresh, along with the focused
Replay CPU family, visual-fidelity, allocation-policy, dependency, and spike
diagnostic gates. The repair's two commit bodies retain the measured output.

`claude/codebase-overview-jatmcg` needed no merge. It landed earlier via PR #152
(`5dd31893d`) and was already an ancestor of `main`; the remote branch is stale.

## Next Work

No owner-selected implementation plan is active. Await owner direction.

## Blockers

- FP0 pre-change `tools\validate_replay_visual_fidelity.bat` builds and passes
  its 18 typed/negative controls, then the authoritative run stops at reveal 0
  on `header.futureNodeCount`. Preserve the oracle until the Physics phase
  explains the transition, retains both launch payloads, and applies the exact
  candidate through the archived automated lane.
- The stale Physics CSV and corrected-topology visual oracle remain validation
  findings. Active Physics
  plans now have standing authority for every golden they govern, including
  Physics, replay, visual, causal, SkullScope, and performance transitions,
  without another pause or interactive phrase. Every write remains exact-digest
  and requires a new append-only old/new executable-and-DLL bundle plus atomic
  source/test/evidence commits. Non-Physics golden policy is unchanged.
- Every changed FP golden uses the manifest schema under
  `Agentic/Plans/Artifacts/README.md`; the final new producer is retained for
  the next old-versus-new comparison. Existing FP0/FP1 phase executables remain
  historical evidence, while future transition bundles fail closed unless both
  launch payloads are complete.

## FP0 Closure - 2026-08-22

FP0 closes `PHYS-001` through `PHYS-006` and `PHYS-008` through `PHYS-010`.
World-shape transforms, default descriptors, transactional solver snapshots,
complete swept fallback, and exact spatial-cell identity now have focused
negative controls. The strict two-generation replay allocation interaction
passes after sorted-unique cache publication/capture was made explicit; its
allocation guard reports no gameplay or reserve-policy violation.

`tools\validate_physics.bat` and the final ten-stage fast preflight pass. The
44,401-line Physics golden remains byte-identical at
`debf57f744774d4e7c1eb5cc61f05ba6e41dc6dc997ad20db6c91b02b0958c32`, so no
baseline update was necessary. The inherited replay visual reveal-0
`header.futureNodeCount` mismatch remains separately controlled. The final FP0
Debug executable and manifest live under
`Agentic/Plans/Artifacts/ragdoll-physics-unification/FP0/`; executable SHA-256
is `cdefc1b53c3de37c0d75fdd9a423b61aac8df368b45919f8a312cf6dc73cc053`.

## FP1 Closure - 2026-08-22

FP1 originally added one deterministic post-force motion-eligibility pass,
version-1 thickness-scaled promotion/demotion thresholds, collider-topology-
owned thickness/farthest-point geometry, stage-owned hysteresis, conservative
angular broadphase reach, and a version-4 Physics snapshot/replay tail. On
2026-08-23 the owner superseded only the threshold semantics with version 2:
absolute `0.1`-metre promotion and `0.075`-metre demotion travel per Physics tick,
independent of thickness. Angular expansion still uses cached farthest-point
reach and the resettable SpatialGrid overlay or complete-coverage fallback, so
long blades do not inflate persistent membership or add fixed-step allocation.

The focused eligibility family passes 4/4 cases and 53/53 assertions in Debug
and Profile; the real replay hysteresis-band continuation passes 1/1 case and
14/14 assertions. Exact 0/1/4-worker comparison passes 36,981 assertions, two clean
Profile processes emit byte-identical evidence at SHA-256
`5CFCB874C9AA34488AABD437BB0FCD9B00301189DA50C04C57ACCF3B6599B6E3`, and the
Profile pass probe reports 3,800 ns for four evaluated rows with counts captured
separately. Production prediction snapshot, replay artifact v1-v4 compatibility,
strict two-generation allocation, dependency, ownership, and Physics gates pass.
The Physics golden remains unchanged at
`debf57f744774d4e7c1eb5cc61f05ba6e41dc6dc997ad20db6c91b02b0958c32`.
The final Profile suite passes 708/708 cases and 2,544,123 assertions, and the
exact-current `validate_fast --preflight-only` gate passes after complete
Debug/Profile rebuilds and strict reachability. The 29-file touched source/tool
comment audit has zero deferrals. The final FP1 Debug executable and manifest
live under `Agentic/Plans/Artifacts/ragdoll-physics-unification/FP1/` with
SHA-256 `612461c8dbd48eb8823468a8b06d1fb3f576b5610b6e48610b6b5a870ae7888a`.

## CI Note

`.github/workflows/mandatory-cpu-validation.yml` now installs the pinned
clang-format 21.1.8 from the PyPI wheel instead of the Chocolatey community feed.
The feed dropped that version on 2026-08-15, `choco` still exited 0 after
upgrading 0/1 packages, and only the workflow's explicit version assertion caught
that the runner's pre-installed 20.1.8 had been left in place. Do not repin
downward: `.clang-format:43` sets `BinPackLongBracedList`, which clang-format 20
rejects outright, and all 651 tracked C++ sources format differently under 20.

The same workflow now builds one Debug physics runtime artifact after restoring
the pinned PIX package with patched NuGet 6.14.3, then runs that exact
manifest-hashed payload on two fresh hosted runners without rebuilding. Run
`31990868600` passed end to end on `eddb25e88`, with both replicas reporting
`byte_comparison=PASS` for executable SHA-256
`2C5CFEF85DAD5595A104277A2A2185D3ACBE18ECF2C2B8FCCA288CD5CA27EDE8`.

## Finding That Motivated The Plan

A `SkullbonezSource/Physics` scan reports one `acosf` and no other
implementation-defined transcendental, which understates the exposure. The trig
is one call level down: `Quaternion::RotateAboutAxis`
(`SkullbonezSource/Maths/Quaternion.cpp:94-95`) calls `sinf` and `cosf`, and
`IntegrateBodyRecordPose` at `SkullbonezSource/Physics/PhysicsBodyStore.cpp:1007`
calls it for every rotating body every tick. Any future scan of this class must
cover `Maths` as well as `Physics`, which is why the T2 gate scopes both.

Separately, `SkullbonezSource/Physics/Ragdoll.cpp:253` passes an unclamped dot
product to `acosf`; a normalized dot product routinely exceeds 1.0 by an ULP and
`acos` returns NaN. That is a live correctness defect independent of the
determinism work, and T4 lands the clamp as its own reviewable change.

## Repository Presentation Conventions

Still current; read before your first commit.

- `Agentic/Reports/` is deleted and must not be recreated. Closure and
  investigation evidence belongs in the commit body and the owning plan. Git
  history is the archive. Source `Related:` blocks cite only durable targets —
  source, `tools/`, `Agentic/Reference/`, or a root document.
- The commit progress header carries no percentage. Use
  `<PLAN_NAME>, TASK <DONE>/<TASK_COUNT> — <ACTION SUMMARY>` and keep the whole
  subject under 72 characters. Commits made outside a plan runner, including
  plan registration, use normal subject rules and claim no plan progress.
- `tools/validate_build_all.bat` builds Automation, Debug, and Profile only when
  all three development configurations are explicitly requested. `validate_fast`
  builds Profile and does not rebuild unrelated object roots.

Retained review decisions no longer use physical source coordinates.
Deterministic-math calls use normalized statement identity and Runtime repair
debt uses exact edge content plus stable occurrence. Source design now comes
from Clang syntax trees without a per-site ledger.

## DX12 Bug Ledger Closure (2026-08-26)

DX12-001 through DX12-008 are fixed as one subsystem batch. The batch recycles
graph-transient resources and their descriptor rows, gives every in-flight DXR
frame private constant/TLAS upload storage, rejects oversized geometry layouts
and incomplete raytracing material tables before command work, reconciles
physical transient state and same-state UAV ordering across logical aliases,
uses non-repeating reusable dynamic-geometry handles, bounds instanced uploads
and draws to retained byte ranges, and checks every reflected uniform write
against both its declared field and backing constant buffer.

One consolidated read-only rubber-duck review covered all eight DX12 rows. It
closed four subsystem interactions found during review: generation rollover and
restart revival, unsupported combined color/depth transient views, illegal
COMMON promotion into write-only states, and missing UAV ordering between
same-state aliases. Final review counts were 0 blocking, 0 non-blocking, and 1
missing-evidence item. Per owner direction, no build, test, scanner, inventory,
or validation command was run; native fence, COM, and command-stream behavior
therefore remains unexecuted evidence rather than a claimed result.

## Assets Bug Ledger Closure (2026-08-26)

ASSET-002 and ASSET-004 are fixed as one subsystem batch. Shader creation now
carries an AssetSystem-owned request containing the configured or absolute
resolved base path and the authored contract name. Ordinary asset shaders and
all three UI text shaders use that request, while direct Rendering builder
calls keep their compile-time `DATA_ROOT` behavior. Texture replacement now
loads a candidate through one shared transaction, leaves the resident row
untouched on decode or backend failure, publishes the complete candidate, and
only then retires the superseded backend handle.

One consolidated read-only rubber-duck review covered both Assets rows in task
`01a03961-d56f-70d1-a7f2-56d4651b858b`. The final counts were 0 blocking, 0
non-blocking, and 1 missing-evidence item. Per owner direction, no build, test,
scanner, inventory, or validation command was run; compile/link and focused
runtime behavior remain unexecuted evidence rather than claimed results.

## World Bug Ledger Closure (2026-08-26)

WORLD-001 through WORLD-003 are fixed as one subsystem batch. Passive generated
camera bounds now consume the live WorldEnvironment fluid plane, preserve the
terrain floor and out-of-bounds no-support rule, and independently apply the
configured upper cap. Height-map terrain publication remains local until both
its mesh and shader exist, so either failure preserves a caller's existing
terrain owner. Skybox reset now returns the exact failed result when any face
mesh or the shader is unavailable instead of reporting successful resources.

One consolidated read-only rubber-duck review covered all three World rows in
task `01a03961-d56f-70d1-a7f2-56d4651b858b`. It found and closed one conflicting
lower/upper camera-bound precedence defect and two focused-evidence gaps. Final
counts were 0 blocking, 0 non-blocking, and 1 missing-evidence item. Per owner
direction, no build, test, scanner, inventory, or validation command was run;
compile/link and focused runtime behavior remain unexecuted evidence rather
than claimed results.

## Gameplay Bug Ledger Closure (2026-08-26)

GAME-001 and GAME-002 are fixed as one subsystem batch. Editing the inactive
legacy field now preserves per-body capture and cooldown timers while an
authored TornadoSystem remains active, while disabling every tornado source
still clears those timers. Runtime body appends preserve surviving dense rows
and add only zeroed rows. SceneWorld deletion mirrors PhysicsBodyStore's
swap-last compaction so the moved stable body retains its exact timer history.

One consolidated read-only rubber-duck review covered both Gameplay rows in
task `01a03961-d56f-70d1-a7f2-56d4651b858b`. It found and closed three focused
test gaps plus one portable-test ownership issue. Final counts were 0
blocking, 0 non-blocking, and 1 missing-evidence item. Per owner direction, no
build, test, scanner, inventory, or validation command was run; compile/link
and focused runtime behavior remain unexecuted evidence rather than claimed
results.

## Runtime App Bug Ledger Closure (2026-08-26)

APP-002 and APP-003 are fixed as one subsystem batch. Every `Run::Execute`
exit now finalizes an armed interaction recording before process status is
resolved, retains the exact save diagnostic, and gives that owned failure
precedence over an otherwise successful exit. Raw mouse registration now
returns an owned Win32 diagnostic; startup stops before renderer creation and
releases workers, development tools, the window, and COM in order when that
registration fails. Window cleanup also treats an HWND already retired by
`WM_DESTROY` as an idempotently completed release.

One consolidated read-only rubber-duck review covered both Runtime App rows in
task `01a03961-d56f-70d1-a7f2-56d4651b858b`. It found and closed the initial
three lifecycle/evidence blockers, then one stale-HWND blocker and one cleanup
ordering evidence gap. Final counts were 0 blocking, 0 non-blocking, and 1
missing-evidence item. Per owner direction, no build, test, scanner, inventory,
or validation command was run; compile/link and focused runtime behavior remain
unexecuted evidence rather than claimed results.

## Physics Bug Ledger Closure (2026-08-26)

PHYS-007 is fixed. The existing point-joint body assignment boundary clears the
retained scalar warm-start impulse before a stable constraint handle can refer
to a different body pair. Focused regression evidence now plants an exact
nonzero retained impulse through replay restore, rebinds through the public
PhysicsEngine API, and pins the stable handle, replacement endpoints, and zeroed
solver cache.

One terminal read-only rubber-duck review covered the sole remaining Physics row
in task `01a03961-d56f-70d1-a7f2-56d4651b858b`. Final counts were 0 blocking, 0
non-blocking, and 1 missing-evidence item. Per owner direction, no build, test,
scanner, inventory, or validation command was run; compile/link and focused
runtime behavior remain unexecuted evidence rather than claimed results.

## Runtime Capture Bug Ledger Closure (2026-08-26)

CAP-001 through CAP-004 are fixed as one subsystem batch. BMP and PNG captures
now publish complete binary buffers through the shared temporary-sibling file
transaction, so write, flush, close, or replacement failure preserves an
existing final artifact and returns the exact owned failure. Regular one-shot
and interval triggers remain independent when both are due on one frame, and
the interval ordinal advances only after successful publication. Screenshot-
and-exit naming selects the last slash of either path-separator kind without
truncating its bounded output.

One consolidated read-only rubber-duck review covered all four Runtime Capture
rows in task `01a03961-d56f-70d1-a7f2-56d4651b858b`. Final counts were 0
blocking, 0 non-blocking, and 1 missing-evidence item. Per owner direction, no
build, test, scanner, inventory, or validation command was run; compile/link
and focused runtime behavior remain unexecuted evidence rather than claimed
results.
## Runtime Diagnostics Bug Ledger Closure (2026-08-26)

DIAG-002 through DIAG-005 are fixed as one subsystem batch. Scene memory
reporting now partitions the canonical Physics scene-sized store total between
collider and other Physics storage without double counting. Performance CSV and
main-memory dump ownership now propagate write, flush, and close failures; the
application exit owner retains those failures, including when a scene load also
fails. The C-key overlay cycle now preserves the independent terrain and
pipeline layers.

One consolidated read-only rubber-duck review covered all four Runtime
Diagnostics rows in task `01a03961-d56f-70d1-a7f2-56d4651b858b`. It found and
closed two process-exit propagation blockers: a discarded scene-load diagnostic
result and the combined scene-load/performance-artifact failure case. Final
counts were 0 Blocking, 0 Non-blocking, and 1 Missing evidence. Per owner
direction, no build, test, scanner, inventory, or repository validation was
run; compile, link, and focused runtime evidence remain unexecuted.

## Shader Suite Bug Ledger Closure (2026-08-26)

SHADER-001 through SHADER-004 are fixed as one subsystem batch. Mip dispatches
now stop before an odd shared-memory reduction so the next filtered dispatch
retains NPOT edge texels. Separate sky faces use the shared s1 clamp plan in
both raster and DXR paths. Procedural ridge, cloud-domain, and streak inputs are
periodic across the longitude cut. Trajectory ribbons clip homogeneous w and
the D3D near plane before any screen-space expansion.

The bake now owns raster, compute, and DXR-library bytecode in one 44-stage
content-addressed manifest, including recursive local-include hashes checked by
the runtime loader. The shared dual-language shader behavior header supplies
the exact periodic and clipping function bodies to both shipping HLSL and the
focused CPU fixtures. Required artifact generation completed successfully with
pinned DXC 1.8.2502.11; this was not a validation run.

One consolidated read-only rubber-duck review covered all four Shader Suite
rows in task `01a03961-d56f-70d1-a7f2-56d4651b858b`. It found and closed the
initial three subsystem blockers: unaudited DXR bytecode, symbol-only seam
evidence, and symbol-only clipping evidence. Final counts were 0 Blocking, 0
Non-blocking, and 1 Missing evidence. Per owner direction, no `--check`, build,
test, scanner, inventory, or repository validation was run; compile/link and
focused runtime behavior therefore remain unexecuted evidence.

## Runtime Debug Bug Ledger Closure (2026-08-26)

DBG-001 through DBG-006 are fixed as one subsystem batch. Scene activation now
clears collision fades, contact linger rows, and broadphase cells through one
renderer-owned epoch reset. Disabling the broadphase overlay also clears its
visual cache immediately. Collision mesh, shader, and dynamic-buffer creation
now occurs only in the BackendInit resource phase; guarded Render can draw or
skip but cannot create those resources.

Physics debug line staging is pre-sized to the complete scene-body axes budget
and every later layer is bounded by that retained capacity. Contact linger rows
have a fixed scene-sized ceiling, while broadphase line staging covers all 2,048
tracked visual cells. Focused evidence exercises the production reset and
resource-phase seams, ready-epoch reuse, disabled-overlay clearing, exact
capacities, bounded contacts, and same-sized-scene collision reset.

One consolidated read-only rubber-duck review covered all six Runtime Debug
rows in task `01a03961-d56f-70d1-a7f2-56d4651b858b`. It found and closed two
initial evidence blockers: bypassing the renderer-owned scene reset and failing
to pin BackendInit preparation against Render admission. Final counts were 0
Blocking, 0 Non-blocking, and 1 Missing evidence. Per owner direction, no build,
test, scanner, inventory, formatter, or repository validation was run;
compile/link and focused runtime behavior therefore remain unexecuted evidence.

## Runtime Automation Bug Ledger Closure (2026-08-26)

AUTO-003 through AUTO-007 are fixed as one subsystem batch. Startup now
authenticates the exact recorded scene and replay sidecar paths selected from a
single parsed manifest before publishing either consumer path. Recorded
manifest content failures remain Automation-owned so initial scene-load and
ordinary execution exits both publish the required report while retaining an
earlier process diagnostic.

Malformed script roots, nested baseline containers, and optional press-key
fields are admitted before typed JSON reads in the repository's no-exception
configuration. Reports publish through temporary-sibling replacement and
cannot claim success after write, flush, close, or replacement failure. Replay
artifacts append a disjoint role suffix to the complete report path, and legacy
same-frame actions retain authored order through the production stable sorter.

One consolidated read-only rubber-duck review covered all five Runtime
Automation rows in task `01a03961-d56f-70d1-a7f2-56d4651b858b`. It found and
closed manifest-epoch binding, startup report-finalization, no-exception JSON,
test-link ownership, optional-field admission, and unstable-sort evidence
blockers. Final counts were 0 Blocking, 0 Non-blocking, and 1 Missing evidence.
Per owner direction, no build, test, scanner, inventory, formatter, or
repository validation was run; compile/link and focused runtime behavior remain
unexecuted evidence.

## Runtime Direction Bug Ledger Closure (2026-08-26)

DIR-001 through DIR-009 are fixed as one subsystem batch. Shot-list loading now
rejects non-finite or degenerate camera input, preserves exact long automation
paths end to end, carries timer overshoot into the next phase, retries failed
phase styles, and keeps opposite up-vector blends away from zero.

Live-style polling now retains failed-read stamps for retry, admits bounded paths
before publication, and rejects physical overlong or NUL-bearing capture lines
without publishing a valid-looking prefix. Look Lab validates receipt facts,
Gregorian timestamps, UTC offsets, and every derived bundle path before either
the controller publication port or the filesystem can create a directory.

One consolidated read-only rubber-duck review covered all nine Runtime Direction
rows in task `01a03961-d56f-70d1-a7f2-56d4651b858b`. It found and closed the
capture-line prefix, upstream automation-path, report-target, and Look Lab
filesystem-preflight blockers. Final counts were 0 Blocking, 0 Non-blocking,
and 1 Missing evidence. Per owner direction, no build, test, scanner, inventory,
formatter, or repository validation was run; compile/link and focused runtime
behavior therefore remain unexecuted evidence.

## Engine Signature Cohesion SC4 Render Slices (2026-09-01)

Profile builds warning-clean. The current render-frame slice deletes the
19-field model frame bag and ten-part frame-entry bag in favor of consumer-owned
model, broadphase, collision, Physics, world-extension, diagnostics, world, and
overlay views. The complete test executable passes 913 cases and 2,693,947
assertions with one expected skip. Focused compiler-backed source-design passes
11 source/header targets under 80 compile contexts with zero findings. Project
filters, formatting, dependency direction, plain-language policy, commit-note
policy, and whitespace checks pass.
No baseline or golden changed. SC4 remains active; SC5, SC6, and the final SC7
closure still follow, corresponding to tasks 6/8, 7/8, and 8/8.

The eleventh SC4 slice separates Replay render timing from renderer overlay
submission. Profile builds warning-clean; the focused seam passes five
assertions, and compiler-backed source-design passes five targets under 36
contexts with zero findings. The renderer's pre-existing large coordinator was
left untouched after the first scan correctly rejected reopening it.

The twelfth SC4 slice repairs Runtime render-debug presentation as one owner
group. Physics body geometry, contact, sleep, and pipeline overlays now receive
four exact child views, and retained-contact update cannot reach unrelated
diagnostics. Collision visualization drops its unused live body-store borrow;
App publication and the two collision phases clamp the model rows they index.
Profile builds warning-clean and the focused debug-visualizer contract passes
one case and 17 assertions. Compiler-backed source-design passes nine targets
under 63 compile contexts with zero findings; format, dependency/project
ownership, project-filter, and plain-language gates pass. SC4 remains task 5/8;
SC7 remains the required closure and the stale active-goal label ending at SC6
is not a completion gate.

The thirteenth SC4 slice replaces the 17-field launcher repro context with
separate scene-capture, launch-policy, and presentation views. RuntimeTools now
captures one detached snapshot before file serialization; the writer cannot
reach live scene, physics, terrain, camera, entity, or renderer owners. Debug
builds warning-clean, and compiler-backed source-design passes three targets
under 20 compile contexts with zero findings. SC4 remains task 5/8; the required
finish is still SC7 task 8/8.

The fourteenth SC4 slice replaces the flat 53-reference UI widget reconstruction
with seven owner-cohesive draw groups and routes minimized, tab, target, footer,
overlay, hitbox, and test consumers through their exact groups. The active-tab
and footer composers were split when the compiler gate exposed their size and
signature debt. Profile builds warning-clean; the focused UI selection passes
84 cases and 2,608 assertions; compiler-backed source-design passes five targets
under 40 compile contexts with zero findings. SC4 remains task 5/8, and SC7
remains the required terminal closure.

The fifteenth SC4 slice replaces the flat Prediction presentation record with
timeline, topology, trajectory, retained-marker, baseline, drag-preview,
controls, and diagnostics child views. App/UI retain the composition envelope;
Planning receives only controls before prediction and timeline/topology/controls
after publication. The stale `ReplayPredictionFutureContext` inventory row is
reconciled as already retired by the explicit topology builder. Cause-focus,
retained-evidence publication, and two regression oracles were split when the
compiler gate exposed their touched function-shape debt. Profile builds with
zero warnings/errors; the final sequential Prediction/Replay/Planning selection
passes 57 cases and 4,907 assertions. Source-design closes the 16 changed targets
across 90 contexts with zero remaining findings; formatting, dependency/project
ownership, build-configuration, project-filter, plain-language, and whitespace
checks pass. No baseline or golden changed. SC4 remains task 5/8; SC7 remains
the required task 8/8 closure.

The sixteenth SC4 slice separates Planning overlay reads into timeline/scrubber,
planning-surface, and causality children. Cause inspection now exposes typed
transport, selection, solver-evidence, and drawer-display projections while its
flat inherited value layout preserves automation serialization. Individual
renderers and editor panels no longer receive the complete overlay or cause
inspection aggregate. Profile builds warning-clean; 40 focused cases and 703
assertions pass; compiler-backed source-design passes 14 targets across 82
contexts with zero findings. Formatting, dependency/project ownership,
build-configuration, project-filter, plain-language, and whitespace checks
pass. No baseline or golden changed. SC4 remains task 5/8; SC7 remains the
required task 8/8 terminal closure.

The seventeenth SC4 slice deletes the duplicated 36-field operator inspector
projection record and its field-by-field copy. The canonical detached inspector
value now exposes selection, transform, identity, render-material, and Physics
children; App builds it directly from the scene and editor owners. The established flat fingerprint values remain intact.
Profile builds warning-clean; 10 focused cases and 225 assertions pass;
source-design passes six targets across 35 contexts with zero findings.
Formatting, dependency/project ownership, build-configuration, project-filter,
plain-language, and whitespace checks pass. No baseline or golden changed. SC4
remains task 5/8; SC7 remains the required task 8/8 terminal closure.

The eighteenth SC4 slice closes the remaining below-App aggregate worksheet.
Replay transport commands are a closed action-specific variant, Replay startup
load/probe requests are grouped by operation, and the distinct workspace,
immutable input-publication, and scene-reset values remain value-only owner
boundaries. Scene defaults saving now consumes an owned snapshot projected
before file I/O; focused tests prove the snapshot is detached. The Look Lab
save request is retained after confirming its path/UTC validation and
copy-before-return coverage. Profile builds warning-clean; the focused selection
passes 4 cases and 43 assertions. Source-design passes 11 targets across 52
compile contexts, and grouped ownership gates pass. No baseline or golden
changed. SC4 remains task 5/8; SC7 remains the mandatory task 8/8 terminal
closure.

SC5 task 6/8 is complete in the nineteenth grouped signature-cohesion slice.
Camera movement is projected once into named `RuntimeCameraMovementInput`
fields; graphics stress classifies every authored action into one concrete
owner family and calls narrow owner operations; scene-load presentation is an
explicit validation/render/window-UI/stress sequence at all seven callers. The
Profile solution builds warning-clean. Six focused cases pass 124 assertions,
source-design passes 11 targets/72 contexts, and the App advisory rescan covers
47 targets/340 contexts with 119 fully reconciled candidates (seven fewer than
the pre-edit scan). Format, dependency/project ownership, build configuration,
project filters, plain language, and whitespace pass. No baseline or golden
changed. SC6 task 7/8 and mandatory SC7 task 8/8 remain; scrubber behavior stays
deferred by user direction.

## Engine Signature Cohesion SC6 Whole-Engine Reconciliation (2026-09-01)

SC6 task 7/8 is complete; SC7 task 8/8 is the active terminal phase. The final
whole-engine advisory rescan covers 737 files and 6,449 compile contexts with
zero infrastructure errors. It accounts for 667 candidates against the frozen
SC0 count of 698, a net reduction of 31 across 71 changed file/kind rows. The
qualitative pass found no old participant surface reconstructed through a
rename, a nominal slice, forwarding member reach, or a new generic owner bag.

The grouped cleanup deletes the behavior-free `InputBindingContext` alias,
refreshes 23 exact allocation-policy fingerprints relocated by accepted source
slices, and updates the technical-manual source plus generated DOCX to describe
the current named pass inputs, `RuntimeRenderPassResources`, and
`RenderResourceLifecycle`. Current `ReplayStartupRequest` and grouped UI
`WidgetView` values remain legitimate operation/composition boundaries.

Profile x64 builds with zero warnings/errors. The four exact runtime input-
binding cases pass 1,204 assertions; source-design passes one changed source
across five compile contexts. Allocation, format, dependency/project ownership,
build-configuration consistency, project filters, plain language, and
whitespace checks pass. The manual regenerates to 60 pages and structural DOCX
verification finds all current terms and no retired names. LibreOffice is not
installed, so the document skill's page-image render was unavailable; this is
recorded rather than represented as visual evidence. No baseline or golden
changed. SC7 supplies the terminal review, validation, computer-control,
closure, and PR evidence below. Scrubber behavior remains deferred by owner
direction.

## Engine Signature Cohesion SC7 Terminal Closure (2026-09-01)

SC7 task 8/8 is complete. The final all-first-party inventory covers 737 files
and 6,449 compile contexts with 665 advisory candidates, a net reduction of 33
from SC0, and zero infrastructure errors. Independent reviewer thread
`01a05b08-6649-7be2-adcc-c8b4166cb795` returned CLEAN with zero blocking,
non-blocking, or missing-evidence findings. Renderer transaction ordering,
prediction policy ownership, direct UI member operations, stable camera
configuration, and typed Replay transport are closed without a downward Replay
edge or a new/expanded growth privilege.

Implementation commit `964e7759c` and portability correction `50c623b14` are
pushed. Portable Linux run 33474002031 passes Clang/GCC warning-clean, Clang
ASan/UBSan, and GCC TSan. Profile and all focused camera, UI, renderer,
transport, prediction, dependency, formatting, policy, and unchanged-manifest
checks pass. Windows computer control proved the native top-right close button
highlights red and closes the DX12 application.

The exactly-once `tools\agent_validate.bat --plan-completion` run passed Debug,
the byte-exact Physics 0/repeat/1/4 matrix, mandatory fast preflight,
source-design/retained policy, Profile, all six CPU lanes, and Automation/replay
smoke. It then failed closed on the inherited terrain-UV DX12 screenshot
mismatch first recorded before this plan: water average delta 4.9171, solver
4.0154, space pixel-exact, and zero InfoQueue errors. A focused rerun reproduced
the same metrics. Comparison with the 2026-08-27 inherited captures shows only
0.005790 average channel movement for water, 0.003872 for solver, and exact
space output across SC7. The plan forbids baseline updates; no golden was
refreshed, no gate was weakened, and the intentional World UV fix was not
reverted. Replay visual fidelity still lacks its expected
`full_reveal_probe_profile.json` report and is not claimed as passing. Scrubber
click-hold behavior remains deferred and unclaimed.

Permanent evidence is in
`Agentic/Plans/Artifacts/engine-signature-and-context-cohesion/sc7-closure-evidence.md`.
Every closure commit note must pass both detailed-message validators with
substantive Why, Ownership, What, Validation, Baselines/Artifacts, and Review
sections; empty or placeholder commit bodies fail closed. No implementation
plan is active; await owner direction.

## Combined Solver Lab and causal UI PR (2026-09-08)

Committed all reviewed Solver Lab project work on codex/solver-lab as 55c7cebd6,
then integrated it with the causal PR using a normal Git merge. The inspector
uses larger wrapped raw/iteration rows with bounded text submission. Blue and
grey outline controls now belong to the hierarchy footer and remain usable
with the detail drawer closed. The footer is excluded from hierarchy scrolling.

The combined Automation and Profile builds passed without warnings. Skarness
causal playback passed with object identity/mass/dimension assertions, camera
wheel isolation, independent outline rendering, red contact flash, and controls
with the drawer closed. Final fast/UI and hosted validation are being completed.

Build review for PhysicsComparison.Binary.cpp: its compiler variants are
identical to the existing Archive.cpp variants in every configuration. The
registered fingerprints cover only the existing engine/doctest executable-role
macros and EHsc distinction; both retain _HAS_EXCEPTIONS=0 and JSON_NOEXCEPTION.
This follows the shared-test composition used by physics-ab-comparison.md and
changes no compiler setting or validation rule. Source-design repairs retain
triangle vertex values, preview submission ordering, and atlas sample order.

The Scene draw-stream fingerprint changes only for the added Solver Lab row.
The forecast toggle now requires scrolling at the 520-pixel test height; its
spacing and physical click command remain explicitly tested. Other tab hashes
remain unchanged. Physics, replay and screenshot golden files are unchanged.

## P prediction shortcut (2026-09-08)

P now pauses live Physics and enables Predict/recording with the operator UI
visible and minimized for world selection. The next press returns to live,
clears prediction/cause/path state through InputRouter workspace cleanup, and
releases a cross-scene pause lock. Existing UI play/pause commands retain their
transport behavior. Holding P does not repeat the transition.

The narrow Skarness input.set_prediction_key control drives the physical key
bit through the normal binding table. The regression proves pause/resume using
captured live solver frames while the harness pause is released, checks empty
and selected prediction exits, confirms selected and submitted target 6 agree,
and checks that late worker output cannot restore cleared overlays. Screenshots
and trace are under TestOutput/skarness/p-shortcut-ready. Preflight, the affected
source-design checks and all 960 unit tests passed (2,731,442 assertions).
The prior full UI and complete Skarness suites passed for the combined layout.

Unified UI checkpoint: Causes is now docked and folded within the shared shell.
Native evidence: TestOutput/skarness/unified-causes-details; source design:
TestOutput/unified-ui-causes-design.log. InputRouter shell-click regression
fixed and verified; native P behaviour also passes. Editor pane, camera/header,
Solver Lab, preference persistence and complete mapped acceptance remain work
in progress. Branch remains codex/unified-ui; plan remains 0/8 complete.

Unified UI checkpoint: Editor and camera controls now use the shared shell.
Native evidence: TestOutput/skarness/unified-editor-grid (37 catalog entries,
24 quick buttons, 11 held variants, dock resizing/folding) and
TestOutput/skarness/unified-camera-header (all seven modes in both layouts).
Screenshots inspected; old floating palette overlap removed. Focused build,
eight-file source design and 19 tests pass. Solver Lab integration is next;
preferences and complete mapped acceptance remain outstanding. Plan 0/8,
active branch codex/unified-ui; no commit/push performed.


Unified UI continuation: Solver Lab is integrated into the shared shell and
retains an independently owned comparison across background Scene loads. Native
`unified-lab-controls` and `unified-lab-input-fix` pass the implemented control
routes and retained camera/tick checks. Library replacement remains release-first;
generic file/finding replacement remains transactional, as existing integrity
checks require. The first archived-bundle integrity invocation failed a fixture
sidecar expectation and remains recorded; do not count it as a gate pass.
Per-user layout persistence is implemented; `unified-preferences-1` passes
cross-launch layout/dock/fold/drawer/tab retention and invalid-file fallback.
Tools always starts closed. All progress remains on `codex/unified-ui`, 0/8
accepted groups; complete inventory, remaining native checks, terminal gates,
independent review and commits are still required.

Latest Unified UI checkpoint: `unified-lab-narrow-3` passes paired picking,
F5/F6, popup/focus priority, 900x640 resizing and local scrolling, exact retained
camera/tick restoration and actual Scene advancement with background Lab.
`unified-lab-integrity-raw` passes the unchanged comparison integrity gate.
`unified-scenes-native-routes` passes native creation, filtering, Demo, distinct
reset operations and save in both layouts. Popup typing over header/diagnostics
was repaired; temporary tracing is removed. No test-created authored scenes
remain. Remaining work includes complete control/tooltips catalog and checks,
detailed profiler/memory operations, more small-window/capture/error checks,
remaining Replay/Causes/planning routes and terminal closure. Still 0/8 accepted.

Unified UI Tools checkpoint: shared delayed help now covers Physics, Options,
Memory, Profiler workers, all footer controls, Render parameters, Sky and
Cinematic parameters/features, Targets and Keys. Static render help follows
existing command enums; Sky reuses cinematic parameter descriptions. Native
`TestOutput/skarness/unified-tools-clipped` passes worker toggle/restore,
Profiler root folding and timeline retention across drawer/layout changes,
Memory presets and budget adjustment against the replay owner, plus visible
help in each Tools tab. A screenshot exposed Memory content painting into the
footer; the shared Tools content clip fixes it while foreground popups remain
independent. `unified-tools-clip-shell` passes all tabs and Scene popup routing.
Focused checks: 23 tests / 487 assertions, three-file and five-file source-design
checks pass. Logs: `unified-ui-tools-focused-tests.log`,
`unified-ui-tools-tooltip-design.log`, `unified-ui-tools-observations-design.log`.
No commit/push or baseline refresh. Complete mapped control acceptance, narrow
windows, remaining replay/planning routes and terminal closure remain pending.

Latest checkpoint: `unified-tools-profiler-help` passes native hierarchy/value
hover targets along with the Tools operations above. The corresponding build
and two-file source-design check pass. Status badges now use the computed
`statusContent` rectangle below the header and clear of perimeter/Details panes;
`unified-tools-status-bounds`, three-file source design and 20 focused tests /
499 assertions pass. The initial status build had two non-constant local
expressions declared constexpr; corrected to const, rebuilt successfully.

Small-window baseline captured under `TestOutput/skarness/unified-small-before`
at 640x480, 480x360 and 320x240. This is evidence of remaining defects, not a
pass: the existing fixed 44/44/78-pixel Tools title/tab/footer geometry overlaps
at 320x240 and leaves almost no Scene control area at 640x480. No minimum-window
restriction was introduced and no compact layout fix has been applied yet.
The next implementation should use one shared compact Tools geometry for input,
drawing and tooltip clipping, retaining all tabs/footer actions through bounded
menus or scrolling. Avoid changing layout/workspace automatically. The full
control inventory and terminal gates remain outstanding; plan still 0/8.


Compact Tools checkpoint (2026-09-09): title, tabs, content, footer and close
bounds now come from `ComputeToolsChromeRects` for drawing, input and help.
Small drawers replace the eleven-column tab strip with a scrollable selector
and expose all footer actions through Display settings; wide and compact
controls share `ApplyFooterAction`. Local scrolling uses twelve-pixel steps in
compact content. Popup state participates in keyboard/world blocking and is
cleared on focus loss or hiding the drawer. Reflection choices honor the
existing disabled mask.

Pinned F5/F6 retain their original data and charts with compact labels and
bounded plot areas. The F5 marker popup can use adjacent dock width; its row
count, scroll range and picking agree with available height. A native test
exposed the observation hash treating Main (hash zero) like no selection; the
observation now includes the existing frame-total identity. No runtime marker
identity or sampling baseline changed.

Evidence: `TestOutput/skarness/unified-compact-tools-final` passes native access
to all eleven tabs at 640x480, 480x360 and 320x240 in Canvas and Editor, footer
timeline/reflection routes, popup scrolling, focus loss and F5 selections
without activating underlying Tools. Screenshots were inspected. Wide Tools
worker/Profiler/Memory/help routes pass in `unified-wide-tools-after-compact`;
Replay transport, identity, controls, layout/drawer retention and folding pass
in `unified-replay-after-compact`. Automation builds and focused source-design
checks pass; `unified-compact-final-tests.log` records 19 tests / 528 assertions.
No baseline refresh, commit or push. Full mapped parity and terminal closure
remain outstanding; this is progress within UU4/UU6, not plan acceptance.


Scene timing checkpoint (2026-09-09): the existing Scene owner already accepted
`toggleCrossScenePause` and `requestSingleStep`, but the Scene presenter emitted
neither. The agreed Pause lock and Single step controls now use those commands,
with disabled-state help and the owner-projected pause value. They occupy a
locally scrollable final Scene row without moving the existing forecast/reveal
controls. InputRouter still owns capture and one-turn stepping.

`TestOutput/skarness/unified-scene-time-2` passes native pause/one-shot stepping
(including a held click), reveal rate minimum/interior/maximum without advancing
the paused scene, and private forecast start/progress/reset/stop in both layouts.
The first run incorrectly kept Skarness' independent pause active while testing
a Scene step; the corrected test releases the harness pause for that click.
Screenshots were inspected. Automation build and eight-source design checks
pass; `unified-scene-time-tests.log` records 21 tests / 539 assertions.

`unified-scenes-saved-values-3` strengthens Save defaults by changing fluid
height through Keys and verifying the saved authored value (200 in Canvas,
-100 in Editor). Earlier checks clicked outside the window after Reset defaults
closed Tools and only checked an existing file; that was insufficient evidence.
The corrected test reopens Scenes and validates the existing version-5 writer
format, active scene path and changed value. The writer intentionally does not
retain the starter-only root name property. Creation, filtering, Demo, Reset and
Reset defaults also pass. Only test-owned scene files were created and removed.
`unified-preferences-after-compact` passes native restart and invalid-file checks.

No baseline refresh, commit or push. Complete catalog/control parity, remaining
planning and comparison routes, preference section folds and the terminal gates
remain open. Plan acceptance remains 0/8.


Filter and catalog checkpoint (2026-09-09): native cause text focus now blocks
pre-UI world shortcuts while leaving device keys available to the native filter.
Focused keyboard editing no longer depends on a pointer sample, and local Escape
does not also open Tools. `unified-causes-typing-5` passes typed PFJI, Backspace,
Escape/Return, selection/camera/prediction identity, evidence tabs and folding.
Earlier failing runs exposed key stripping, a pointer-only early return and an
unconsumed after-UI Escape; these are repaired, not accepted failures.

Summary accordion preferences now use the existing versioned folded-section
field. Selection/reset retain the presentation choice without retaining evidence.
`unified-preferences-evidence-2` passes native section expansion, shutdown/restart,
layout/dock/drawer/tab retention and invalid-file fallback. Four focused cause
tests / 48 assertions pass. Filter automation anchors now reveal Causes and use
the current shared bounds; the corrected Automation build passes.

Read-only Skarness observations reuse the existing rendering parameter projection
and sample Physics values from their actual owners. `unified-render-catalog-2`
passes all 38 Render sliders in both layouts. `unified-cinematic-catalog-3` passes
all 26 Sky and 64 Cinematic sliders plus all four/eight feature toggles in both
layouts. Total: 768 endpoint/interior slider checks and 24 held-toggle checks,
plus hidden-control isolation. The first midpoint test hit float rounding at an
exact half step; interior checks now use a non-tie position. Feature rows are
scrolled into view before clicking. No parameter range or runtime action changed.

`unified-physics-ui-3` passes 13 toggles and 13 sliders in both layouts plus
pipeline next/previous (106 recorded checks). The test preserves the existing
tornado auto-visual coupling: disabling the field can disable its shell. This
was an incorrect independent-toggle assumption in the first test, not a product
regression. Native Editor Physics/Render screenshots were inspected and show
bounded content above the retained F5/F6 docks.

Automation builds and focused compiler-backed source design pass; latest checks
are `unified-filter-summary-design.log`, `unified-catalog-anchor-design.log` and
`unified-physics-design.log`. `unified-filter-catalog-tests.log` records 24 tests /
584 assertions. The control inventory now includes explicit 128 render-parameter
rows. A failed text-encoding write temporarily truncated that task-owned document;
it was reconstructed from the inspected control sources and inventory, then
written with explicit UTF-8. No source, user scene, baseline, commit or push was
lost or changed by that documentation failure.

The full plan remains incomplete. Remaining work includes Options/Keys and
per-entry Editor routes, recording and Save/dialog/error outcomes, planning
overlay placement and controls, comparison findings/persistence, final visual
coverage and all terminal gates/review. No aggregate phase is accepted yet.


File and catalog acceptance reconciliation (2026-09-09): the preceding session
left passing native logs newer than its prose handoff. Inspected logs confirm
`unified-options-keys-2` (54 checks), `unified-editor-both-catalog-1` (37 catalog
choices, 24 quick buttons and 11 hold variants in each layout), `unified-files-2`
(Open/Save finding/Load finding, picker cancellation, missing/invalid file errors
and restored identity/settings), `unified-replay-files-3` (Save/Load recording,
cancellation, missing file and recovery), and `unified-config-files-1` (Save CFG,
Save Paths and Save Sky against isolated config copies, preserving comments and
checking changed owner values). Original authored config hashes are preserved.
The corresponding Automation builds and five-source `unified-files-design-2.log`
passed. These are concrete route checks, not acceptance of untested controls.

Planning placement is being completed against the shared scene viewport. The
new layout orders intercept/trip/transfer panels in one bounded, locally
scrollable column, wraps trip actions at narrow widths and shares the same
geometry between input, drawing and delayed help. Focused initial build and
13 tests / 434 assertions passed; eight-source design checks passed. Native
planning acceptance and the terminal gates remain pending. The first native
probe requested Plan before Prediction completed its required baseline; its
abort is preserved in `unified-planning-1`. The probe now waits for completion
and the UI is being updated to expose that precondition as disabled help.


UI goal continuation, 2026-09-09: Planning now uses bounded scrollable shell
content with explicit paused-versus-advancing semantics. Native planning and
near-target Commit routes pass in both layouts; the ordinary solar fixture
reports no solution, so its pre-existing convergence probe remains a terminal
check. Fixed UI capacity now fits the complete 3,072-cell grid; native maximum
3,177 commands, no overflow. Short Causes panes scroll their whole content.
Focused tests: 14 / 425 pass; Automation build 10 passes. Targets entries pass;
recording-catalog playback is being rerun after correcting its read-only popup
row count. Details/evidence are in unified-ui.md. UNIFIED_UI remains 0/8 pending
full diagnostics/visual coverage, final gates and independent closure review.

### UI integrated-review checkpoint  -  2026-09-09

UNIFIED_UI remains 0/8; MASTER remains 146/162. Target/recording native catalog
checks and full Profiler/Memory controls now pass in both layouts. Retention UI
matches Replay's 20-600 seconds; existing policy limits are unchanged. Whole-diff
source design passed 91 sources / 783 contexts / zero findings. Final review is
active. Remaining work includes the unchanged solar trip regression, narrow Lab
event scroll and seek geometry findings, final visual inspection and mapped gates.
See the owning plan's integrated-review checkpoint for exact artifacts and the
unchanged live-ledger limitation. No UI completion commit or push exists yet.
