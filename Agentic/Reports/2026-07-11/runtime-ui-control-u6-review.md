# Runtime UI/Control U6 Closure Review

Date: 2026-07-11
Scope: 96 source files inventoried in
`Agentic/Plans/TODO/runtime-ui-control-architecture-cleanup.md`

## Expected outcome

Every inventoried file must either participate in the shared fixed-capacity
surface pattern or have a concrete reason why its existing owner/control model
is the correct equivalent. No row may hide duplicated screen geometry, stored
hover bridges, callback dispatch, UI-owned domain state, or unbounded per-frame
storage.

## Reconciliation

All 96 files have one of these mutually exclusive dispositions:

1. **Converted shared surfaces - 9 files.**
   `RuntimeUiSurface.h`, `ReplayOverlayLayout.cpp/.h`,
   `ReplayOverlayRenderer.cpp/.h`, `ReplayRuntime.cpp/.h`,
   `RunReplayScrubberTools.cpp`, and `RunReplayCauseTreeTools.cpp` build or
   consume the scrubber/cause-window rows. Input and render derive geometry and
   hot/active state from those rows. Stored scrubber and cause-row hover bridges
   are deleted.
2. **Existing in-game widget system - 60 files.** Every tracked file under
   `SkullbonezSource/UI/` remains with `UI::InGameUI`. This is not a waiver for
   old boolean geometry: `UIButton`, `UICheckBox`, `UIComboBox`, `UIIconButton`,
   `UIScrollBar`, `UISlider`, and `UITabBar` retain their bounds and use those
   same bounds for `HitTest` and `Draw`. Tab states own their control objects;
   named `HandleContentClick`/slider update/commit handlers emit typed
   `InGameUIInputResult::commands`. Window chrome and the editor mini-palette
   similarly share named layout values between input and drawing. Wrapping this
   established object-control system in a second `RuntimeUiSurface` would
   duplicate state and geometry rather than improve ownership.
3. **Domain handlers and value-only boundaries - 27 files.** Replay velocity,
   path, and editor gizmo affordances are world-space handles whose identity and
   capture live in `RuntimeInteractionController`; their hit geometry and tracer
   geometry belong to the editor/replay domain rather than a pixel `UIRect`
   surface. Editor placement/action files mutate scene/physics owners only after
   typed commands. Input frame, automation, render-input, scene-option, and
   frame-composition files transport or sequence values and do not own controls.

The dispositions total 9 + 60 + 27 = 96 with zero deferred.

## Adversarial checks

- Scoped search found zero `overX` control booleans and zero `std::function`
  usage in the 96-file surface inventory.
- Replay renderer search found zero direct `ReplayScrubber*Rect` calls.
- Repository search found zero deleted scrubber hover fields or
  `RunReplayPredictionUiState`. Cause-tree input/state have no stored
  `hoveredRow`; the renderer's local row index is derived from the shared
  content control each draw.
- `tools/check_allocation_policy.py --repo .` passed in 7.4s:
  310 files scanned and zero allowlist errors. Existing reported API sites are
  governed by the repository allowlist/policy; the new surfaces contain inline
  arrays and do not grow.
- Existing widget `HitTest` branches occur inside named control/tab handlers
  after the frame pointer edge is established. They reuse stored widget bounds
  and do not recreate `leftPressed`, capture, or visibility policy per control.

## Rubber-duck review

The first read-only pass found one blocking issue: cause-window rendering could
resolve row hover while another UI surface blocked the mouse because it lacked
the input turn's block fact. The fix adds a disposable `pointerBlocked` snapshot
to `RunReplayCauseTreeState`; input publishes it and render skips pointer
resolution when blocked. This is policy state for the frame, not a hover mirror.

The pass also required the explicit 60-file widget-system equivalence above.

The single permitted repeat found no ownership, sideways-authority,
replacement-god-object, allocation, compatibility, callback, or project
coverage defect. It did find one actionable regression-evidence gap: the new
blocked-pointer hover rule lacked a CPU test. The shared surface now accepts the
block fact while resolving a pointer and clears any previous hot/pointer/hover
result before returning. `RuntimeUiSurfaceBlockedPointerClearsHover` proves
that contract in Debug and Release. The repeat is exhausted and no actionable
finding remains.

## Validation evidence before the review fix

- U5 CPU umbrella: 25/25 interaction cases in Debug and Release.
- U5 replay scrub: all probes passed.
- U5 DX12 renderer: InfoQueue errors = 0 and all captures matched.
- Cause-window slice: Profile, replay scrub, and DX12 renderer passed before the
  pointer-block review correction. Final gates must be rerun after that fix.

## Final validation

- `tools\validate_runtime_interaction_policy.bat` passed in 9.2s with 26/26
  cases in both Debug and Release, including the blocked-pointer regression.
- `tools\validate_full.bat` passed in 87.3s from final source: all CPU lanes,
  zero-warning Profile and Debug builds, DX12 InfoQueue errors = 0 with all
  three captures matching, standalone handle smoke, and the 44,401-line varied
  physics baseline byte-exact.
- `python tools\check_allocation_policy.py --repo .` passed in 7.4s with 310
  files scanned and zero allowlist errors.
- Project-filter validation covers all 605 production items; the shared UI
  header is registered under the explicit `Runtime\UI` production filter.
- Comment-style audit inspected all 6 touched source-bearing files, including
  the CPU test and substantial project-filter tool; zero were deferred.
