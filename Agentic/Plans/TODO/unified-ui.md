# Unified UI

Code: `UNIFIED_UI`
Status: Active, 0/8 complete
Branch: `codex/unified-ui`
Owner direction: 2026-09-08, implement the approved unified UI plan.

## Accepted contract

Layout (Canvas or Editor) and workspace (Scene or Solver Lab) are independent.
Presentation changes preserve camera, selection, simulation, playback and editor
state. The cute rounded skull in the approved demo concept is the canonical
native scalable logo for both layouts and both workspaces. Reference:
`C:/Users/sesch/.codex/generated_images/01a0800e-9682-7142-a8cd-2d5321229205/exec-804ffe56-7993-4e0e-9276-2171c5874857.png`.

Use neutral chrome while preserving causal, comparison, selection and diagnostic
colours. Rename the displayed Alternate velocity action to Modify velocity,
without changing its command. The transport has a thin visible scrubber and a
generous hit target. Docks have fixed edges, bounded resizing and folding.

The shared header contains the skull, activated scene name, Scenes, workspace,
layout and Tools. Scenes opens the existing Scene tab in Tools; at narrow sizes
the name is its trigger. Selection is published as loaded only after activation.
Tools starts closed, remembers its tab, and opens as a resizable bottom drawer
which reduces the viewport. Editor keeps F5 and F6 below that drawer. All current
tabs and footer controls remain available. Full Profiler retains its existing
hierarchy, expansion, timeline, CPU/self/work, percentiles, worker/core-work and
draw-call information; F5 retains its distinct marker-history chart. F6 retains
the memory waterline. Both have direct links to the corresponding detailed tab.

Canvas exposes Replay and Causes through Details. Editor places Editor/Replay
on the left, Causes/evidence on the right and pins F5/F6 below the viewport.
Entering Editor layout does not enable functional editing. F5/F6 focus pinned
panels in Editor and preserve independent Canvas visibility preferences.

Solver Lab replaces the shared shell contents: comparison loading, findings,
view and camera/playback settings on the left; paired rendering in the centre;
differences, selection and height/vertical-velocity plots on the right. Canvas
has equivalent Details controls. Preserve five modes, split orientations, A/B,
X-ray, Focus, Follow A, filters, thresholds, event navigation, reverse, speed,
looping and exact ticks. Recorded divergence must not imply proven causality;
missing and ambiguous evidence remain explicit.

Leaving Lab pauses but retains comparison data and inspection state. Returning
does not resume playback. Close releases data; replacement releases before
loading. Loading/errors/cancellation stay inside the shell. A background
comparison cannot block Scene input or advancement. Scene loading selects Scene
and follows the current lifecycle. Restoring a pre-Lab simulation snapshot is
outside this migration.

One presentation layout computes window-coordinate viewport, dock, drawer,
transport and clipping rectangles. Rendering resources use viewport dimensions;
projection, picking, gizmos, outlines, prediction and paired-pane picking use the
same bounds. Pointer conversion happens at its owner boundary; reject new world
gestures outside, retain valid captured drags. InputRouter remains the sole
retained input/capture owner. Runtime/UI owns presentation preferences; App
composes owners and dispatches typed commands. UI has no upward includes.

Shared delayed tooltips explain actions, units, shortcuts and disabled reasons,
support existing keyboard focus, clamp to the window and dismiss during gestures
or workspace transitions. A versioned per-user file remembers layout, sizes,
last tool and folds. First launch is Canvas with Tools closed. Invalid values
fall back and all dimensions clamp to the current window; small windows scroll
locally without silently switching layouts.

## Tasks and evidence

| Task | Work | Completion condition | Status |
|---|---|---|---|
| UU0 | Inventory and existing-state evidence | Every control maps to handler, displayed state, availability, shortcut, destinations, tooltip and regression check; representative captures retained | In progress |
| UU1 | Shared presentation primitives | Header, skull, Scenes, layout/workspace choices, neutral chrome and tooltip metadata use shared geometry | In progress |
| UU2 | Authoritative viewport | Render/projection/input/overlays/picking share offset viewport; captured drags and paired picking verified | In progress |
| UU3 | Transport, Replay and Causes | Existing commands and identities preserved through both layouts; semantic colours retained | In progress |
| UU4 | Editor, Tools and diagnostics | All tabs/footers retained, editor mode independent, F5/F6 pinned/focusable, full Profiler state retained | In progress |
| UU5 | Solver Lab shell and lifetime | Shared shell, all controls, retained paused comparison and explicit close/load semantics verified | In progress |
| UU6 | Preferences and visual coverage | Versioned preferences, bounded resizing/folds/scrolling/tooltips and both workspaces/layouts inspected | In progress |
| UU7 | Terminal validation and review | All mapped gates and independent review pass; remaining regressions repaired | Pending |

Do not remove an old surface before its mapping checks pass. Source behaviour is
authoritative; illustrations do not add unsupported camera or profiler features.

## Inventory ownership map

The detailed migration checklist is [unified-ui-controls.md](unified-ui-controls.md).
It records tab and footer actions, state sources, popup/slider policies,
catalog expansion requirements, tooltips and regression routes. Expanded
catalogs and planning controls now have native command/state evidence.

This table establishes intended destinations before moving controls. The
individual command/state/availability/shortcut inventory is not complete yet;
the ownership map below is not a control-parity acceptance result.

| Existing owner | Current actions/state | Canvas destination | Editor destination | Regression oracle |
|---|---|---|---|---|
| SceneTab, SceneNavigationModel, SceneController | Filter/select, Demo, create with validation/starter/refresh/load, Reset, Reset defaults, Save defaults, recording catalog, Lab library, time scale, pause lock, step, reveal, forecast | Tools/Scene through Scenes | Same | Activated identity, pause state and authored/live distinction |
| UIWindowInteractionOwner, UIFrameComposition | Supported camera choices, editor mini controls and object count | Header and Tools/Editor | Toolbar and Editor pane | Real supported mask and authoritative editor state |
| ReplayOverlayLayout, ReplayScrubber, ReplayOverlayRenderer, App replay input | Recording/load, timeline, branch, publication, velocity modification, ragdoll, past/live and outlines | Transport and Details/Replay | Transport and left Replay | Command once; selected/published/rendered identity |
| ReplayCauseInspection and ReplayOverlayRenderer | Hierarchy, evidence filter/type, selected evidence, folding, raw/iteration details, outline toggles | Details/Causes | Right Causes | Retained evidence identity and truth status |
| EditorTab and EditorTools | Existing functional edit/placement/object/undo/redo controls | Tools/Editor | Left Editor | Layout choice never enables editing |
| ProfilerTab | Full tree/timeline/metrics/worker/core-work/draw detail and expansion | Tools/Profiler | Same | Existing metric source, units and retained selection |
| ProfilerTab histogram | F5 marker history, marker/axis/window controls | Independent overlay | Pinned bottom | Existing history and selected marker; F5 focuses |
| MemoryTab | Detailed memory operations and F6 waterline/events | Tools/Memory and independent F6 | Tools/Memory and pinned F6 | Source values, units, allocation bounds; F6 focuses |
| UIWindowInteractionOwner active tab/footer presenters | Physics, Options, Render, Targets, Keys, Sky, Cinematic; renderer/water/blur/vsync/timeline/perf/hitbox footer | Tools, existing tabs | Same | Actual owner receives each existing command once |
| PhysicsComparisonPanel and RunComparison | Library/load/cancel/error, findings, five displays, split orientation, A/B, X-ray, Focus, Follow A, thresholds/filter/events, plots, reverse/play/step/speed/loop/seek/close | Shared shell Details and transport | Left/right docks and transport | Comparison integrity plus selected id, exact tick and camera |

## Validation

During implementation use focused unit/UI checks and builds. At terminal closure
run UI and Skarness routes for every mapped control, comparison and integrity,
replay visual fidelity, dependency/source-design, DX12, graphics stress (at least
10 seconds), all mapped CPU suites and `tools/agent_validate.bat --plan-completion`.
Run independent rubber-duck review after implementation. Preserve physics,
replay and evidence baselines. Review intentional UI reference changes separately.

Exercise scene success/failure/empty lists in both layouts; UI capture, text
focus, wheel/local scroll, resize/DPI, viewport edges and offset picking, scrubber
hold/drag, capture leaving regions and focus loss; all comparison modes, events,
missing evidence, cancellation and repeated layout/workspace transitions. Inspect
screenshots for overlap, skull fidelity, readability, colours and tooltips.

## Startup observations

The branch was created from a clean main checkout. No user-owned dirty files were
present. Two pre-edit Automation launch attempts did not create a Skarness
manifest; stdout/stderr were empty. Their artifacts are under
`TestOutput/skarness/unified-ui-before` and `unified-ui-baseline`. This is an
initial baseline capture failure, not a passed check. Rebuilding the unchanged
Automation configuration resolved startup. Baseline Canvas, Scene browser,
detailed Profiler, library popup and Solver Lab captures are retained in
`TestOutput/skarness/unified-ui-baseline-built`. The library's actual pointer
route loaded wall-only with both motion coverages available, tick 1, last tick
2400 and playback paused. That owned session was stopped through Skarness.

## Implementation and native evidence - 2026-09-09

The shared shell, offset viewport, Replay/Causes, Editor, all eleven Tools tabs,
F5/F6, Solver Lab lifetime and versioned preferences are implemented. The native
checks and exact per-control catalogs are recorded in unified-ui-controls.md.
The evidence includes all 128 rendering sliders, Physics/Options/Keys, every
Editor and Target catalog entry, recording playback, file dialogs and errors,
actual isolated configuration persistence, comparison controls and preferences.

Final focused native evidence under TestOutput/skarness:

- unified-short-causes-2: Summary/Raw/Iterations inner scrolling and later
  hierarchy selection in short panes of both layouts, bound to selected ids.
- unified-small-lab-3: 320x240 transport, exact seek, later events, both library
  choices and visible foreground popups in both layouts.
- unified-diagnostics-9: worker endpoints/interior/restore, retained marker and
  draw hierarchy, Memory presets/sliders, and popup-only dismissal on Details.
- unified-planning-8: TOF, Plan/Cancel, transfer hover/selection identity,
  bounded local scrolling and 900/640/480/320 widths in both layouts.
- unified-planning-commit-2: test-owned near-Mars convergence and held Commit
  in both layouts.
- unified-planning-step-2: AwaitingPrediction and Converged stepping matches
  cancel-then-step exactly for selected ship identity, position and velocities.

The unchanged solar_system_trip_planner_probe.json passes all eight assertions:
three candidate misses reduce from 34.26 to 11.0938 to 3.16097, below 3.205.
Report/trace: TestOutput/interaction/unified-trip-probe-5-*.json*.
App publishes actual committed physics advancement to Planning; Inspect and
Replay pause do not masquerade as advancement. Candidate acceptance checks the
published source velocity. The intercept scan key also identifies the borrowed
frame bank so a same-generation replacement cannot reuse a retained minimum.
Negative unit cases cover both stale candidate data and frame-bank replacement.
No Physics or Prediction algorithm, golden, or recorded assertion changed.

Small-pane fixes preserve existing owners: the Causes edge scrolls the shell
while content scrolls evidence/hierarchy; Lab event rows receive their own wheel
input; Lab transport scales below 220 pixels; popup commands are extracted into
the foreground and clipped to their bounds. Short copy actions suppress an
overlapping secondary hint. Memory retention uses Replay's existing 20-600
seconds; budget remains 32-512 MiB. The transfer grid needs 3,072 cells, so the
fixed UI draw-command capacity is 4,096; measured peak is 3,177 with no overflow.

## Review and validation checkpoint

Integrated rubber-duck-03 found six issues: short Causes inner scrolling,
short Lab event scrolling, compact seek geometry, Lab popup clipping, Details
popup click-through, and four unruled build settings. All are repaired and
native-tested. Follow-up found two further defects: cancellation after the first
Physics tick and narrow Lab label overlap. Rollback now precedes the first
admitted tick and skips idle plans; compact labels fit and clip per button.
The final read-only review passes with no further actionable findings and no
new downward Replay include, growth privilege or ownership defect.
The four build rulings apply the existing engine/test exception and preprocessor
roles to UIPresentationPreferences.cpp and UIToolsTooltips.cpp. The replacement
inventory has zero blocking diagnostics (unified-terminal-build-config-2.log).

Compiler-backed whole-diff source design passed 91 sources / 783 contexts with
zero findings in 491.128 seconds. Later fixes passed 14 sources / 55 contexts
in 52.023 seconds, and the final copy-hint change passed one source / three
contexts in 3.595 seconds. Logs: unified-terminal-source-design.log,
unified-review-fixes-source-design.log, unified-copy-source-design.log.
Focused tests passed 13 cases / 174 assertions; popup tests passed five cases /
38 assertions. Latest Automation build passed without warnings in 6.56 seconds.
Later step/label fixes pass four-source design (14 contexts, 23.670 seconds),
and the idle guard passes one source (three contexts, 7.074 seconds).
All logs are under TestOutput. Replay visual fidelity passes the unchanged
2,401-tick oracle, 201 causal nodes, durable artifact and all negative/determinism
controls in unified-terminal-replay-visual.log. Graphics stress remains open and
agent_validate --plan-completion is running. No phase is declared complete.

The initial live ledger could not start because verified pricing is missing for
the active model. The later transition reports no active UNIFIED_UI task. The
unrelated existing ledger remains untouched; no usage counters or costs are
invented. Command logs and review messages preserve available evidence.

## Exceptions

None approved. No dependency direction, runtime growth privilege or golden
transition is authorized by this plan.
