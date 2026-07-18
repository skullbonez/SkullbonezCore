# ImGui + Tracy E8 Shared Editor Exchange Evidence

Date: 2026-07-19

Branch: `nightrunner-18th-july`

Plan task: E8 — introduce the shared command/view-model coexistence seam

## Outcome

E8 is complete. Legacy UI and the development ImGui editor now consume the
same immutable, domain-grouped scene, property, rendering, replay, and surface
view assembled once from concrete owners. Both front ends emit fixed-capacity
typed command queues. A common arbitration pass validates every command,
coalesces exact cross-surface duplicates, rejects conflicting payloads through
Lane R, and projects at most one canonical request into each established owner
path. Neither surface forwards business operations through `Run`.

Independent Legacy and ImGui visibility are process-lifetime development
preferences. `Legacy`, `ImGui`, and `Both` startup selections still honor
scene-authored defaults when `--dev-ui` is omitted, the legacy `0` shortcut is
mirrored after established input routing, and no visibility preference enters
scene or replay state.

## Shared Boundary And Owner Effects

- `OperatorEditorFrameView` groups borrowed/read-only values by scene,
  property, rendering, replay, and surface domain. The legacy draw pass and
  ImGui receive the same frame object; a semantic field-by-field fingerprint
  proves equality without hashing struct padding.
- Scene reset/selection, time scale/gravity, VSync, and replay-memory policy
  are representative typed commands. Legacy flat fields normalize into these
  queues before arbitration; ImGui menu actions submit directly to the same
  queues.
- Legacy has deterministic first priority. Exact duplicates from ImGui count
  as coalesced and execute once. Same-action/different-payload commands fail
  before projection, with no partially modified owner packet.
- Queue counts, enum values, finite floats, positive time scale, scene indices,
  and replay `-1` leave-unchanged sentinels are validated at submission and
  again at the projection trust boundary.
- The editor keeps a bounded next-input queue. `Run` only sequences its
  consumption into `ProcessInputFrame`; the existing scene, property, renderer,
  and replay appliers remain authoritative.

## Focused Acceptance Evidence

Three doctest cases passed with 32 assertions:

- `Operator editor queues coalesce identical frontend intent before projection`
  covers all four representative domains, both surface paths, exact duplicate
  coalescing, and canonical projection.
- `Operator editor queue rejects conflict and malformed surface values` covers
  conflicting payloads, NaN, invalid replay values, and corrupt queue counts.
- `Operator editor frame fingerprint follows semantic values only` proves
  stable equality and per-domain change sensitivity.

The final cumulative doctest run passed 302/302 cases and 21,572/21,572
assertions.

## Required Gates

| Gate | Result |
|---|---|
| `tools\validate_ui.bat` | PASS in 67.202s after correcting the formatter-only first attempt; UI screenshots/blur metrics and Profile/Debug readiness builds passed with zero warnings/errors and zero DX12 validation errors. |
| `tools\validate_tests.bat` | PASS in 7.354s; project filters passed and 302/302 cases with 21,572/21,572 assertions passed. |
| `tools\validate_full.bat` | PASS in 148.245s; CPU umbrella and coverage floors, Automation/replay smoke, zero-error DX12 baselines, physics standalone smoke, and byte-exact 44,401-line physics regression passed. DX12 manifest: `TestOutput/validation/dx12_renderer/20260718T175521Z/manifest.json`. |
| `python tools\check_allocation_policy.py --repo .` | PASS in 9.462s; 402 files scanned and zero allowlist errors. |
| `tools\validate_build.bat Release` | PASS in 44.610s; zero warnings and zero errors. The Release link inventory contains `OperatorEditorExchange.obj` but no ImGui, Tracy, or development-owner object. |
| Release executable exact-token scan | PASS; `SKULLBONEZ_DEVELOPMENT_TOOLS`, `ImGuiEditorOwner`, `ImGuiEditorInputPolicy`, and `TracyClientOwner` are absent from `Release/SKULLBONEZ_CORE.exe`. The incremental PDB retains a `TracyClientOwner` source/debug token, so no broader PDB-string exclusion claim is made. |

No baseline, screenshot golden, replay golden, physics CSV, query golden, or
authored data changed.

## Comment Audit

The 16 touched source-bearing files were inspected against
`Agentic/Skills/comment-style-audit/skill.md` and the repository comment guide:

- [x] `SkullbonezSource/Runtime/DevelopmentTools/ImGuiEditorOwner.cpp`
- [x] `SkullbonezSource/Runtime/DevelopmentTools/ImGuiEditorOwner.h`
- [x] `SkullbonezSource/Runtime/InputFrame.cpp`
- [x] `SkullbonezSource/Runtime/InputFrame.h`
- [x] `SkullbonezSource/Runtime/InputFrameExecution.cpp`
- [x] `SkullbonezSource/Runtime/Render/RuntimeRenderPasses.h`
- [x] `SkullbonezSource/Runtime/Run.cpp`
- [x] `SkullbonezSource/Runtime/Run.h`
- [x] `SkullbonezSource/Runtime/RunFrame.cpp`
- [x] `SkullbonezSource/Runtime/RuntimeFrameViews.h`
- [x] `SkullbonezSource/Runtime/UiTextPass.cpp`
- [x] `SkullbonezSource/UI/OperatorEditorExchange.cpp`
- [x] `SkullbonezSource/UI/OperatorEditorExchange.h`
- [x] `SkullbonezSource/UI/UI.h`
- [x] `SkullbonezSource/UI/UICommands.h`
- [x] `SkullbonezTests/TestOwnerRequestQueues.cpp`

Checked: 16. Deferred: 0. Unchecked: 0. The touched project/filter files are
trivial build inventory entries and do not need source learning headers.

## Unrelated Live Blocker

Physics body-count P1 remains blocked only on exact owner approval for replay
`causal.topologyCount: 199 -> 200` and the mechanically derived
`physics_query_varied.json`. E8 did not touch either artifact.
