# ImGui + Tracy Development Editor Campaign

Status: Active — 8/18 tasks (E0-E7 complete; E8 next) under the owner's
continue-on-blocker direction while physics P1 awaits two explicit
transition-artifact approvals
Owner direction: 2026-07-18
Ledger: E0-E17 (7/18 complete)

## Objective

Build a development-only Dear ImGui editor shell and Tracy instrumentation
lane that can run beside the existing in-game debug UI. The first default
dock layout must feel like an editor rather than a debug overlay:

- editor mode, scene, hierarchy, creation, and selection workflows run down
  the left side;
- the live game viewport remains the large central surface;
- inspection, world/render settings, diagnostics, and a deliberately compact
  causality view occupy the right side;
- replay transport and scrub controls are always anchored across the bottom;
- Tracy owns deep CPU/timeline/allocation profiling through its external
  viewer rather than another crowded in-engine profiler tab.

This campaign is an additive evaluation lane. **It does not delete, rename,
disable, or silently replace the old UI.** The owner must be able to display
the legacy UI, the ImGui editor, or both in the same development build and
compare them interactively. Legacy retirement is explicitly outside this
plan and requires a later owner-approved plan after hands-on evaluation.

## Binding Decisions

1. Dear ImGui and Tracy are development tools, not shipped game UI. They may
   allocate inside a narrowly defined development-tools exception; gameplay,
   simulation, replay, rendering, and shipping Release allocation rules do
   not change.
2. Tracy remains a separate native viewer. It is launched/connected from the
   editor and can be placed beside the game window by the OS, but it is not
   embedded as an ImGui dock tab. ImGui provides connection state and useful
   engine counters; Tracy provides flame graphs, timelines, zones, plots,
   call stacks, and allocation investigation.
3. ImGui uses docking in the engine-owned Win32/DX12 window. The engine keeps
   ownership of the device, swap chain, command lists, descriptor allocation,
   resource transitions, frame pacing, and Present. `FRAME_COUNT` remains 2.
4. Single-window docking is the first supported shape. Native ImGui platform
   multi-viewport support remains disabled until the central viewport,
   descriptor lifetime, input capture, and graphics-stress evidence are
   stable; enabling it later is an explicit evidence-backed sub-decision.
5. Existing typed frame data and command boundaries remain authoritative.
   ImGui panels may emit the same domain commands as the legacy UI; they may
   not gain `Run`, `GameModel`, renderer, replay-owner, or physics-owner
   reach-back to make migration convenient.
6. The old profiler tab remains intact while the UIs coexist. The ImGui
   editor does not rebuild its timing tree, timeline, percentile table, or
   histogram because Tracy supersedes those views. Engine-specific counters
   that help authoring or capacity diagnosis remain available in focused
   Diagnostics panels.
7. No behavioral baseline, replay golden, existing UI screenshot baseline,
   physics CSV, or coverage-floor refresh is authorized by this plan. New
   side-by-side screenshots and interaction reports are evidence, not a
   license to rewrite existing oracles.

## Default Dock Contract

```text
+--------------------------------------------------------------------------+
| Menu / mode toolbar / play state / scene / Tracy connection              |
+----------------------+--------------------------------+------------------+
| Scene & Modes        |                                | Inspector        |
| Hierarchy            |       GAME VIEWPORT            | World/Simulation |
| Assets / Create      |       (largest region)         | Render/Audio     |
|                      |                                | Diagnostics      |
|                      |                                | Causality        |
|                      |                                | (compact/tabbed) |
+----------------------+--------------------------------+------------------+
| REPLAY: record | jump | play/pause | step | scrubber | prediction/cause |
+--------------------------------------------------------------------------+
| Status: selection | dirty/undo | FPS/frame | warnings | Tracy            |
+--------------------------------------------------------------------------+
```

Layout invariants:

- Left is editor-first from the top down: mode/scene, hierarchy, assets/create.
- Center viewport is never default-tabbed behind a tool and receives the
  majority of width and height.
- Bottom replay transport is always visible in the default layout; resizing
  may reduce labels before hiding controls.
- Right begins with Inspector, then World/Simulation and focused rendering or
  diagnostics tabs. Causality defaults to a compact contextual summary and
  must not consume the whole right rail.
- Any panel may be redocked or closed, and `View > Reset Editor Layout`
  restores this exact topology deterministically.

## Legacy Tab Disposition During Coexistence

This table describes where useful content belongs in the new ImGui editor.
It does **not** authorize removal from the legacy UI.

| Legacy surface | New ImGui destination |
|---|---|
| Prof | Tracy for zones/timeline/percentiles/histograms; renderer and worker counters under Diagnostics |
| Scene | Left `Scene` browser; active scene and frame facts in the status bar |
| Edit | Left `Hierarchy`, `Assets/Create`, contextual `Inspector`, and top-left mode toolbar |
| Phys | `World > Physics`, `World > Tornado`, `Diagnostics > Physics`, and an advanced pipeline panel |
| Sound | `Audio Authoring`; reducer/counter/debug state under `Diagnostics > Audio` |
| Opt | Split among Simulation, Viewport visibility, Rendering/Water, replay diagnostics, and Preferences |
| Render | Right `Rendering` sections for lighting, shadows, water, and materials; stats under Diagnostics |
| Targets | Normally closed `Diagnostics > Render Targets` |
| Ctrl | `World` sections for seed, population, simulation, and fluid controls |
| Sky | Fold into `Rendering > Environment`; no second competing sky editor |
| Cine | Split into `Rendering > Lighting`, `Environment`, `Post`, `Water`, and `Terrain/Materials` |
| Mem | Tracy for generic allocation investigation; fixed-capacity/replay reserve facts under `Diagnostics > Engine Memory` |
| Footer | Small status bar; DX12 selector omitted, VSync under Viewport, hitboxes under Diagnostics, replay controls at bottom |
| Causality window | Compact right-side contextual summary with an explicit expanded/detail view |

## Work Rules

- Execute E0-E17 in order. A task is complete only when every acceptance item
  and its cumulative mapped gates pass from the task's final source.
- One task owns each implementation commit. Third-party drops, generated
  project changes, and engine integration stay reviewable; do not mix broad
  panel migration into backend bring-up.
- Do not delete or gut `SkullbonezSource/UI/*`, legacy tab code, legacy replay
  overlay code, or its current toggle. Coexistence tests must prove both paths
  can be opened independently and simultaneously.
- Source-bearing changes receive the required learning header and nearby
  ownership/lifetime/hazard comments, followed by the repository comment
  audit before task closure.
- DX12 tasks run `tools\validate_dx12_renderer.bat` and
  `tools\run_graphics_stress.bat 1`. Replay-facing tasks additionally run the
  one-invocation replay visual-fidelity gate. Profiling-marker tasks run
  `Profile\SKULLBONEZ_CORE.exe --platform-profiler-markers`.
- New dependencies must be pinned, licensed, reproducible without an
  unreviewed network fetch, and removable from Release artifacts.

## Tasks

- [x] E0 — Freeze the coexistence inventory and interaction contract.
  - Inventory every legacy tab/window, footer control, replay transport,
    causality interaction, hotkey, command, and frame-data field; record each
    as keep, regroup, Tracy-superseded-in-ImGui, or legacy-only-for-evaluation.
  - Capture screenshots of the legacy UI's important states and document the
    current open/close/input behavior without changing baselines.
  - Identify the concrete owners for UI frame composition, typed UI commands,
    replay presentation/scrubbing, window input, DX12 submission, profiler
    markers, and allocation policy.
  - Ratify the default dock sketch above, minimum supported window size,
    development configurations, and the exact toggles for Legacy / ImGui /
    Both. Record whether multi-viewport remains deferred (default: yes).
  - Acceptance: a checked inventory maps 100% of current controls and names
    any intentionally deferred control; no source behavior changes.
  - Gate: documentation-only; no repository validation.
  - Evidence:
    `../../Reports/2026-07-18/imgui-tracy-e0-coexistence-inventory.md` maps all
    12 tabs, chrome/overlays/footer, replay and causality interactions, static
    hotkeys, typed commands, frame fields, and concrete owners. Seven current
    screenshots freeze the important states. It ratifies 1280 x 720,
    Debug/Profile/Automation development builds, `--dev-ui
    legacy|imgui|both`, and single-window docking. No behavior or baseline
    changed; the existing editor interaction harness's post-capture
    fixed-coordinate failure is recorded as a follow-up risk.

- [x] E1 — Pin Dear ImGui and Tracy as reproducible development dependencies.
  - Choose a pinned Dear ImGui docking revision and pinned Tracy client/server
    revision; preserve their licenses and upstream notices.
  - Prefer repository-controlled dependency acquisition or a documented
    submodule/vendor mechanism that works on a fresh machine without fetching
    an unbounded latest revision.
  - Add project filters/property sheets so third-party warnings do not weaken
    `/W4` for engine code. Compile only the required ImGui Win32/DX12 backend
    sources and the Tracy client source.
  - Acceptance: clean bootstrap/build documentation identifies revisions,
    hashes, licenses, update procedure, and Release exclusion.
  - Gate: `tools\validate_fast.bat` after project/tooling changes.
  - Evidence:
    `../../Reports/2026-07-18/imgui-tracy-e1-dependencies.md` records Dear
    ImGui `v1.92.8-docking` at `b61e56346a92cfcaf1f43a545ca37b0b32239654`
    and Tracy `v0.13.1` at `05cceee0df3b8d7c6fa87e9638af311dbabc63cb`,
    license hashes, exact vendor source inputs, development-only property-sheet
    wiring, and reproducible submodule bootstrap. `validate_fast` passed all
    five stages, and a final Release build passed with no ImGui/Tracy source or
    object in its compile/link commands.

- [x] E2 — Establish the development-tools and allocation-policy boundary.
  - Introduce one explicit compile-time development-tools capability used by
    both integrations; Release must compile and link with neither ImGui nor
    Tracy and must contain no hidden initialization path.
  - Route ImGui/Tracy allocations through the approved development allocation
    policy boundary where practical. Add only narrow allowlist rows with
    owner, phase, reason, cap/diagnostics, and permanent-development or removal
    condition; never exempt the whole UI/runtime tree.
  - Keep the gameplay allocation guard active while the tools run so a tool
    exception cannot mask allocations from physics, replay, render submission,
    or other runtime owners.
  - Report ImGui/Tracy allocation totals separately from engine allocation
    violations when the supporting APIs permit it.
  - Acceptance: tool allocations are allowed in development, a deliberate
    gameplay allocation still fails the guard, and Release contains no tool
    symbols/resources.
  - Gates: `tools\validate_fast.bat`, allocation-policy self-test and repo
    scan, plus a targeted Release build inspection.
  - Evidence:
    `../../Reports/2026-07-18/imgui-tracy-e2-allocation-boundary.md` records the
    shared compile-time capability, exact thread-local owner boundary, separate
    64 MiB ImGui and 256 MiB Tracy caps/counters, and why no static allowlist
    row was required. The focused probe passed 11/11 assertions: scoped tool
    allocations produced no violation and an unscoped Render allocation still
    failed the active gameplay guard. Allocation self/repo scans,
    `validate_tests`, `validate_fast`, the targeted Release build and artifact
    scan, and the cumulative `validate_full` gate all passed. Release contains
    no tool source, object, capability, owner, or symbol token; no baseline or
    golden changed.

- [x] E3 — Bring up the minimal Tracy client lifecycle.
  - Initialize/shutdown the client at the platform/application boundary with
    an explicit lifetime that outlives instrumented worker threads and ends
    before logging/platform teardown.
  - Add one frame mark per submitted game frame and name the main, render,
    worker, replay/prediction, and relevant IO threads.
  - Preserve zero-cost-disabled semantics when Tracy is compiled out; marker
    macros must not evaluate expensive arguments or allocate.
  - Add editor-visible connection/build state without polling or allocating in
    a hot loop.
  - Acceptance: the external Tracy viewer connects, receives stable frame and
    thread names, disconnects safely, and the game exits cleanly with or
    without a viewer.
  - Gates: `tools\validate_fast.bat`,
    `Profile\SKULLBONEZ_CORE.exe --platform-profiler-markers`.
  - Evidence:
    `../../Reports/2026-07-18/imgui-tracy-e3-lifecycle.md` records the explicit
    manual/on-demand application lifetime, post-Present frame boundary, real
    main/worker topology, fixed editor connection snapshot, and disabled macro
    contract. A pinned external capture connected and received 101 stable
    frame marks; a live probe observed one composite main label and 63 unique
    worker labels. No-viewer, connected/disconnected, exact platform-marker,
    Release-exclusion, focused-test, fast, and cumulative full-gate evidence
    all passed. Full closed with 295 tests, 21,471 assertions, zero DX12
    validation errors, matching screenshots, and byte-exact physics output;
    no oracle changed.

- [x] E4 — Instrument Tracy at owner boundaries, not every function.
  - Map existing platform-profiler owner intervals to Tracy zones first so
    prior measurements remain intelligible. Add zones for frame sequencing,
    replay record/restore/prediction, physics stage owners, DX12 command
    recording/submission/present, UI build/render, and bounded cold IO.
  - Add plots/counters for worker utilization, body/contact/draw counts,
    replay retained/reserve high-water state, descriptor use, and other
    capacity facts that explain performance without duplicating full tools UI.
  - Enable call stacks and allocation tracing only in an explicitly selected
    heavier profiling mode; document overhead and keep perf comparisons on a
    named configuration.
  - Acceptance: a reference capture answers which owner consumed a frame and
    correlates replay, physics, render, and UI work without changing output.
  - Gates: mapped subsystem gates, `tools\validate_perf.bat`, and platform
    profiler-marker smoke.
  - Evidence:
    `../../Reports/2026-07-19/imgui-tracy-e4-owner-boundary-instrumentation.md`
    records the fixed owner-path registry, connection-generation-safe zones,
    32 capacity plots, explicit standard/heavy capture modes, and final-source
    capture inventory. Standard captured 303 frames and 22,519 zones across
    Frame/Replay/Physics/Render/UI/DX12; heavy proved depth-16 call stacks and
    named global heap events. `validate_perf`, `validate_full`, bounded graphics
    stress, and the exact platform-marker launch passed without a baseline or
    golden refresh. The mapped replay-fidelity gate reached only Physics P1's
    already-owner-gated topology `199 -> 200` transition.

- [x] E5 — Add an engine-owned ImGui context and lifecycle.
  - Create a cohesive development-editor owner responsible for ImGui context,
    fonts/style, per-frame begin/end, visibility, persisted layout version,
    and orderly shutdown. It must not become a cross-domain services bag.
  - Use engine-provided immutable frame data and typed command outputs. Keep
    domain state in scene, replay, rendering, physics, audio, and editor owners.
  - Configure docking and keyboard navigation; keep platform multi-viewports
    disabled initially. Select readable DPI-aware fonts with deterministic
    fallback when assets are missing.
  - Acceptance: an empty dockspace opens/closes repeatedly, survives device
    and window resize paths, and leaves the legacy UI untouched.
  - Gate: `tools\validate_ui.bat` and `tools\validate_fast.bat`.
  - Evidence:
    `../../Reports/2026-07-19/imgui-tracy-e5-context-lifecycle.md` records the
    cohesive development-only owner, direct composition-root storage, typed
    frame/command values, docking/keyboard configuration, deterministic
    DPI-aware embedded-font fallback, versioned layout, and exact
    Legacy/ImGui/Both selector. Repeated lifecycle, resize/device-path, and
    allocation-guard probes passed. `validate_ui`, `validate_fast`,
    `validate_perf`, `validate_full`, and Release exclusion passed without a
    baseline or golden refresh.

- [x] E6 — Integrate ImGui with the engine-owned DX12 frame.
  - Add a bounded ImGui descriptor/font-resource allocation owned through the
    concrete DX12 resource/descriptor authority; do not hand ImGui the global
    heap or swap-chain authority.
  - Record ImGui draw data in the correct pass after the game viewport and
    before Present, with explicit render-target state transitions and
    `FRAME_COUNT == 2` lifetime rules.
  - Handle resize, device loss/recreate, font upload retirement, command-list
    reuse, scissor conversion, and empty/minimized frames without stale GPU
    references.
  - Keep ImGui render stats separable from world-render stats.
  - Acceptance: zero DX12 validation messages, no descriptor growth across
    repeated open/close/resize, and no crash during the bounded stress run.
  - Gates: `tools\validate_dx12_renderer.bat`,
    `tools\run_graphics_stress.bat 1`, allocation repo scan.
  - Evidence:
    `../../Reports/2026-07-19/imgui-tracy-e6-dx12-frame.md` records the concrete
    DX12 renderer owner, separate fixed 16-row development descriptor heap,
    synchronous font-upload retirement, two-frame lifetime, draw placement
    before Present, explicit backbuffer state/binding restoration, separable
    statistics, graceful resize/fault teardown, and Release exclusion.
    Renderer, one-minute graphics stress, fast, allocation, full, and Release
    gates passed with zero warnings, DX12 validation messages, descriptor
    growth, baseline changes, or golden refreshes.

- [x] E7 — Route Win32 input and focus without stealing gameplay controls.
  - Feed Win32 messages through the ImGui backend at the window/input boundary
    while preserving the existing input owner and hotkey ordering.
  - Apply `WantCaptureMouse`, `WantCaptureKeyboard`, and text-input intent only
    to the relevant event class. Viewport hover/focus must preserve camera,
    selection, gizmo, and game input; editor fields must not leak keystrokes.
  - Define mouse capture, relative mode, alt-tab, DPI, resize, IME, clipboard,
    and escape behavior. Both UIs open must not double-consume commands.
  - Acceptance: a scripted matrix covers Legacy / ImGui / Both, viewport/tool
    focus, typing, dragging, camera input, and replay shortcuts.
  - Gates: `tools\validate_ui.bat`, `tools\validate_ui_stress.bat`,
    `tools\validate_full.bat` because `Window*` is touched.
  - Evidence:
    `../../Reports/2026-07-19/imgui-tracy-e7-win32-input.md` records the pinned
    Win32 backend boundary, exact mouse/keyboard/text/platform classification,
    viewport authority seam, no-ghost resynchronization, native capture/cursor
    arbitration, Legacy/ImGui/Both matrix, visible 15-message probe, and Release
    exclusion. UI, UI-stress, fast, allocation, full, and Release gates passed
    with zero warnings, DX12 errors, baseline changes, or golden refreshes.

- [ ] E8 — Introduce the shared command/view-model coexistence seam.
  - Extend or split existing frame-data records into domain-cohesive read-only
    editor views and fixed-capacity typed command queues. Both UI front ends
    consume/emit these boundaries; neither forwards through `Run`.
  - Give commands stable ownership, validation, recoverable error reporting,
    and deterministic arbitration when the same action is emitted twice while
    both UIs are visible.
  - Add independent visibility flags and shortcuts for Legacy, ImGui, and Both;
    persist them only as development preferences, not scene/replay state.
  - Acceptance: representative scene, property, rendering, and replay commands
    produce the same owner-side effect from either UI, and both paths coexist
    without duplicated execution.
  - Gates: `tools\validate_ui.bat`, mapped command-owner tests,
    `tools\validate_full.bat` for Runtime/Run-facing boundary changes.

- [ ] E9 — Build the deterministic dockspace, menus, toolbar, and reset path.
  - Implement the default topology exactly: editor-left, viewport-center,
    utility-right, replay-bottom, status-bottommost. Build it from stable dock
    IDs only on first run or layout-version mismatch.
  - Add File/Edit/View/Debug menus, top-left mode/placement controls, undo/redo,
    play/pause/step state, scene identity, Tracy launch/connect affordance, and
    `Reset Editor Layout`.
  - Version layout persistence so renamed panels migrate or reset cleanly;
    corrupt/missing settings recover without a fatal path.
  - Acceptance: 16:9, ultrawide, and minimum-window screenshots preserve the
    central viewport and bottom replay strip; reset is byte-stable in topology.
  - Gates: `tools\validate_ui.bat`, `tools\validate_ui_stress.bat`.

- [ ] E10 — Deliver the left editor workflow from top-left downward.
  - `Scene & Modes`: edit/play state, selection mode, placement mode, active
    scene, create/load/reset actions, and concise undo/redo/dirty feedback.
  - `Hierarchy`: filterable scene-object tree/list, stable scene identity,
    selection synchronization, multi-select rules, visibility/lock state, and
    context actions that emit typed editor commands.
  - `Assets/Create`: registered asset library browser, categories/search,
    primitive/registered-asset placement, drag/drop payloads, and clear
    unsupported/error states. Reusable objects continue through registered
    asset instances rather than hardcoded editor recipes.
  - Keep the most frequent edit-mode controls at the top; advanced creation
    and scene administration may collapse lower in the rail.
  - Acceptance: select, create/place, duplicate, delete, undo, redo, save/load,
    and reset workflows are usable without opening a generic debug tab.
  - Gates: `tools\validate_ui.bat`, `tools\validate_full.bat` for scene/runtime
    paths, and relevant editor interaction probes.

- [ ] E11 — Make the central game viewport an editor-grade surface.
  - Reserve the central dock node for the game render target and derive its
    content rectangle, aspect handling, DPI mapping, hover/focus, and input
    coordinates explicitly.
  - Provide compact viewport overlays for camera/view mode, gizmo mode,
    snapping, visibility, VSync/presentation facts, and selection—not a new
    strip of all legacy debug controls.
  - Define selection outline, drag/drop placement, gizmo manipulation,
    letterboxing, resize throttling, and pop-out behavior without reallocating
    world targets every cursor movement.
  - Acceptance: picking and placement map correctly at multiple DPI/aspect
    ratios, viewport focus governs game input, and resize has bounded resource
    behavior and zero DX12 errors.
  - Gates: `tools\validate_ui.bat`, `tools\validate_dx12_renderer.bat`,
    `tools\run_graphics_stress.bat 1`.

- [ ] E12 — Build the right-side Inspector and World/Simulation panels.
  - Inspector is selection-contextual and grouped by Transform, Identity,
    Render, Physics, Audio, and object-specific components; mixed multi-select
    values and invalid/stale selection are explicit.
  - World/Simulation contains canonical fixed-step, time scale, gravity,
    friction, sleep, population/seed, fluid, and tornado/environment-force
    authoring. Avoid duplicate controls across Inspector and World.
  - Edits use typed commands with preview/commit semantics where continuous
    sliders would otherwise flood undo/replay streams.
  - Acceptance: every retained Scene/Edit/Phys/Ctrl authoring control is mapped
    and the inventory has no unexplained duplicates.
  - Gates: `tools\validate_ui.bat`, plus `tools\validate_physics.bat` when
    physics coordination or defaults change.

- [ ] E13 — Consolidate Rendering, Audio, and Diagnostics on the right.
  - Rendering sections: Lighting, Environment, Shadows, Post, Water, and
    Terrain/Materials. Merge Sky/Cine concepts into these canonical sections.
  - Audio Authoring: contact recipes/material bands/sample library/global
    tuning. Put reducer activity, counters, and flashes in Diagnostics > Audio.
  - Diagnostics: Physics overlays/pipeline, Renderer counters and render
    targets, Engine Memory capacities/replay reserve events, Workers, and UI.
    Generic profiler timeline/histogram/percentile content is intentionally
    absent because Tracy owns it.
  - Render Targets and advanced pipelines start closed. Diagnostic overlays
    never become serialized authored scene state accidentally.
  - Acceptance: the legacy disposition table is reconciled item by item and
    common authoring views fit in the right rail without forcing Causality to
    dominate it.
  - Gates: `tools\validate_ui.bat`, renderer/physics gates when their source
    boundaries change, and DX12+stress for render backend work.

- [ ] E14 — Reduce Causality to a useful contextual right-side tool.
  - Default compact view shows selected object, current replay tick, immediate
    cause/effect summary, prediction state, and a bounded list of the most
    relevant causal links.
  - Preserve the full causal tree/details behind an Expand/Open Detail action
    that can dock as a separate panel. Do not delete existing causal rendering
    or its data path during coexistence.
  - Selection and replay-tick changes update context without rescanning or
    allocating unbounded trees each frame. Empty, stale, truncated, and
    capacity-limited states are visible.
  - Acceptance: the compact panel shares the right rail with Inspector and
    diagnostics at the default resolution, while every current causal query
    remains reachable in the expanded view or legacy UI.
  - Gates: `tools\validate_ui.bat`,
    `tools\validate_replay_visual_fidelity.bat` once in the task invocation,
    and replay interaction probes; zero golden refresh.

- [ ] E15 — Anchor the complete replay workflow across the bottom.
  - Provide record/stop, jump start/end, play/pause, step backward/forward,
    speed, tick/frame display, scrubber, range/marker feedback, prediction
    request/state, selected cause, and recoverable replay errors.
  - Keep transport controls visible at minimum supported width using priority
    collapse: shorten labels and move secondary detail to popovers before
    hiding core transport or the scrubber.
  - Reuse Replay presentation/scrubber/command owners; the panel must not own
    restore transactions, artifact IO, prediction archives, or timeline state.
  - Define behavior while recording, prediction is pending, no artifact is
    loaded, scrub is clamped, or the world/selection changes.
  - Acceptance: record, stop, scrub, step, play, predict, select cause, and
    return-to-live workflows pass with Legacy / ImGui / Both and the replay
    bar remains docked below the viewport after layout reset.
  - Gates: `tools\validate_ui.bat`, replay interaction tests,
    `tools\validate_replay_visual_fidelity.bat` once, `tools\validate_full.bat`;
    zero replay-golden refresh.

- [ ] E16 — Harden persistence, styling, scaling, automation, and long-session use.
  - Persist panel visibility, dock layout version, sizes, filters, and benign
    preferences separately from authored scenes and replays. Reset/migration
    must recover from stale panel IDs.
  - Establish a coherent engine-editor visual system: spacing, hierarchy,
    disabled/error/warning states, selection colors, readable plots, tooltips,
    keyboard navigation, DPI scaling, and no accidental modal dead ends.
  - Add deterministic automation hooks for panel visibility, layout reset,
    focus, essential actions, and screenshots. Stress panel churn, scene
    switching, replay scrubbing, resize/DPI, Tracy connect/disconnect, and
    legacy coexistence for allocation/resource growth.
  - Acceptance: no unbounded CPU/GPU descriptor/memory growth, stable layout
    recovery, and a screenshot set for default/minimum/ultrawide/Both modes
    reviewed against the dock contract.
  - Gates: `tools\validate_ui.bat`, `tools\validate_ui_stress.bat`,
    `tools\validate_perf.bat`, allocation scan, DX12+stress when applicable.

- [ ] E17 — Side-by-side owner evaluation, independent review, and campaign closure.
  - Produce a control-disposition report proving every legacy control is kept,
    regrouped, Tracy-superseded only in ImGui, or explicitly deferred while
    still reachable in the legacy UI.
  - Capture the final default dock layout and representative workflows with
    Legacy, ImGui, and Both. Give the owner a development build and concise
    playtest script; record feedback and resolve campaign-blocking usability
    findings without deleting the old path.
  - Audit every touched source file with the comment-style skill. Run an
    independent ownership review for command authority, DX12 lifetime,
    replay ownership, allocation exception scope, and absence of a replacement
    editor god object; credible findings reopen the owning task.
  - Measure ImGui-disabled, ImGui-visible, and Tracy-enabled overhead and state
    the configuration/capture method. Do not claim negligible cost without
    numbers.
  - Final gates from final source: `tools\validate_full.bat`,
    `tools\validate_ui.bat`, `tools\validate_ui_stress.bat`,
    `tools\validate_dx12_renderer.bat`, `tools\run_graphics_stress.bat 1`,
    `tools\validate_perf.bat`, allocation self-test/repo scan, platform marker
    smoke, and the one-invocation replay visual-fidelity gate.
  - Closure condition: owner accepts the new editor as ready for extended
    hands-on use. **The legacy UI still exists and remains selectable at
    closure.** Any retirement/deletion proposal is a separate future plan.

## Final Acceptance

- The default editor opens with editor controls down the left, the game
  viewport dominant in the center, useful inspection/settings on the right,
  compact causality, and replay controls permanently anchored at the bottom.
- The user can select Legacy, ImGui, or Both and perform side-by-side evaluation
  without loss of legacy functionality or duplicate command execution.
- Tracy captures frame, thread, physics, render, replay, UI, allocation, and
  capacity evidence in its external viewer; ImGui contains no redundant
  general-purpose profiler replacement.
- Development-only allocations are isolated and diagnosed; gameplay and
  shipping allocation rules remain strict, and Release ships neither tool.
- DX12 validation is clean, bounded graphics stress is crash-free, replay and
  physics oracles remain unchanged, UI stress shows no unbounded growth, and
  independent ownership review is clear.
- No old UI source or feature is deleted by this campaign.

## Explicitly Out of Scope

- Deleting, deprecating, or making the legacy UI unreachable.
- Embedding the Tracy viewer inside the ImGui dockspace.
- Shipping ImGui or Tracy in the player/Release product.
- Changing physics, replay, rendering, or authored-data behavior merely to
  simplify a panel.
- Replacing existing baselines/goldens because the editor looks different.
- Enabling ImGui native multi-viewport before a separate evidence-backed
  decision after the single-window editor is stable.
