# ImGui/Tracy E5 Context And Lifecycle

Date: 2026-07-19
Branch: `nightrunner-18th-july`
Plan task: E5 — add an engine-owned ImGui context and lifecycle

## Outcome

E5 is complete. One development-only `ImGuiEditorOwner` now owns the Dear
ImGui context, fonts and style, per-frame begin/end balance, visibility,
versioned layout persistence, and orderly shutdown. `Run` constructs the
concrete owner directly as the composition root; the owner receives immutable
display/DPI/delta-time values and returns a typed command value. It has no
scene, replay, render, physics, audio, input, or legacy-UI authority.

The development selector is `--dev-ui legacy|imgui|both`. Omitting it preserves
the existing legacy launch state exactly. Release and Profile-WPO exclude the
owner, vendor ImGui sources, selector fields, and development-tool allocation
wrapper; final Release executable and PDB scans found no ImGui or development
capability token.

## Lifecycle And Configuration

- Docking and keyboard navigation are enabled. Platform multi-viewports remain
  explicitly disabled under the single-window campaign contract.
- The owner pins layout persistence to `imgui_editor_layout_v1.ini`; generated
  versioned layouts are ignored by git.
- A future readable asset may live at
  `SkullbonezData/fonts/SkoreEditor-Regular.ttf`. Because that asset is absent,
  E5 deterministically builds Dear ImGui's embedded vector font at 16 px and
  publishes `embedded_vector_fallback` as the chosen source.
- DPI changes restore the fresh dark style before applying the new scale, so
  repeated notifications cannot accumulate rounding drift.
- Context allocation uses Dear ImGui's allocator callback seam and the existing
  hard-capped `DearImGui` development-tool row. The small allocator wrapper is
  the only direct heap authority; steady gameplay allocation guarding remains
  active on the calling thread.
- Empty or minimized frames do not enter an unbalanced context frame. Shutdown
  terminates any active frame before destroying the context.

Dear ImGui 1.92 initially faulted during the first `NewFrame()` because E6 has
not yet installed the renderer backend that normally builds the font atlas.
A CDB stack located the fault in `ImFontCalcTextSizeEx`. E5 now explicitly
selects `io.FontDefault` and builds the CPU atlas at context initialization;
the DX12 texture upload remains correctly deferred to E6.

## Acceptance Evidence

- Default, ImGui, and Both finite launches exited normally. ImGui and Both
  completed frames; the omitted selector preserved the authored legacy state.
- Three consecutive ImGui open/close processes created and destroyed their
  contexts cleanly. The 242-byte version-1 layout file remained ignored.
- A resize/device-path probe performed 131 native resizes and texture churn
  cycles. The descriptor baseline remained 20, the final count remained 20,
  the high-water was 23, and 131 resize requests were acknowledged. The stress
  harness intentionally held interactive mode and stopped only its exact PID
  after 172.956 seconds; subsequent finite ImGui launches exited normally and
  proved clean context shutdown.
- An allocation-guarded finite launch reported 1,059 ImGui allocations,
  1,059 frees, 5,395,180 cumulative bytes, zero active bytes, 352,127-byte
  high-water, and the unchanged 64 MiB hard cap.
- Selector tests cover Legacy, ImGui, Both, omitted default, missing value, and
  exact invalid-value diagnostics.

## Validation

Final-source evidence:

- Targeted `Profile|x64` build: 14.092 seconds, zero warnings/errors.
- `tools\validate_ui.bat`: passed in 58.273 seconds, including zero DX12
  validation errors and matching screenshots.
- `tools\validate_fast.bat`: passed in 55.873 seconds with 297 tests and
  21,497 assertions, project/filter integrity, formatting, and zero-warning
  Profile/Debug builds.
- `tools\validate_perf.bat`: passed in 113.604 seconds with no allocation-policy
  or performance regression.
- `tools\validate_full.bat`: passed in 140 seconds with every coverage floor,
  Automation/replay smoke, zero-error DX12 screenshot comparison, and the
  44,401-line core physics baseline byte-exact.
- `tools\validate_build.bat Release`: passed in 49.404 seconds with zero
  warnings/errors. Binary/PDB scans found no `imgui` or
  `SKULLBONEZ_DEVELOPMENT_TOOLS` token.
- Standalone allocation-policy scan passed: 399 files scanned and zero
  allowlist errors.

Two iteration failures were resolved before these gates. The allocation scan
rejected a first-pass `make_unique` owner, so `Run` now holds the cohesive owner
directly. The first fast preflight required the new source prefix in the
project-filter inventory; the inventory now covers both project entries.
No baseline, screenshot golden, replay golden, authored data, or physics oracle
was changed.

## Comment Audit

The touched tracked source inventory was inspected against
`Agentic/Skills/comment-style-audit/skill.md` and the repository comment guide:

- [x] `SkullbonezSource/Runtime/Allocation/DevelopmentToolAllocation.cpp`
- [x] `SkullbonezSource/Runtime/Allocation/DevelopmentToolAllocation.h`
- [x] `SkullbonezSource/Runtime/DevelopmentTools/ImGuiEditorOwner.cpp`
- [x] `SkullbonezSource/Runtime/DevelopmentTools/ImGuiEditorOwner.h`
- [x] `SkullbonezSource/Runtime/Run.cpp`
- [x] `SkullbonezSource/Runtime/Run.h`
- [x] `SkullbonezSource/Runtime/RunFrame.cpp`
- [x] `SkullbonezSource/Runtime/RunLaunchOptions.h`
- [x] `SkullbonezSource/Runtime/Startup/StartupCommandLine.h`
- [x] `SkullbonezSource/Runtime/Startup/StartupLaunchResolution.cpp`
- [x] `SkullbonezTests/TestStartup.cpp`

The one-line project-filter inventory extension was also inspected as a trivial
tool change. Checked: 11. Deferred: 0. Unchecked: 0.

## Remaining External Blocker

Physics P1 remains unchecked pending exact owner authority for the replay
topology golden transition `199 -> 200` and the mechanically derived
`physics_query_varied.json` update. Neither artifact is part of E5 and neither
was changed here.
