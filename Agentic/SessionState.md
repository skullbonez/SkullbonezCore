# SkullbonezCore Session State

Date: 2026-07-20

Keep this file operational and short. Detailed evidence belongs in plans,
reports, and git history. `Agentic/Plans/MASTER-PLAN.md` is the authoritative
plan inventory.

## Current State

| Field | Value |
|---|---|
| Branch | `nightrunner-20th-july` |
| Current baseline | Synced `origin/main` after PRs #127/#128; dependency direction, allocation namespace, physics facade, and physics settings plans are closed with exact proofs and independent review clear. |
| Current objective | Complete the five remaining architecture-review plans in binding order, recording external blockers without stopping automatable work. |
| Active/future progress | 18 / 37 live tasks; 49%. |
| UI ruling | Legacy remains the default. ImGui is explicit `--dev-ui imgui`; atomic hot swap is allowed, simultaneous Legacy/ImGui activation is forbidden. |
| Last broad local gate | X2 `validate_full` passes in 166.92s: all CPU/coverage/runtime lanes, zero DX12 validation errors, accepted images, and byte-exact physics. |
| Validation for current edits | G0 is documentation-only; no repository validation required. X2 remains the last validated source tip. |

## Live Queue

NOW. Five live plans, 37 tasks; 18 complete (49%). The remaining architecture-review campaign
(registered 2026-07-20 from
`Reports/2026-07-20/engine-architecture-review.md`) is the active queue in
binding order: render-graph-completion → render-hal-modernization →
gameplay-module-extraction →
replay-boundary-containment. Owner decisions at registration: finish the
render-graph migration; PhysicsEngine absorbs PhysicsScene; gameplay
extracts to a new top-level `SkullbonezSource/Gameplay/` module.

`dependency-direction-restoration` is closed 6/6 and archived in
`Agentic/Reports/2026-07-20/dependency-direction-restoration-closure.md`.
`allocation-namespace-restoration` is closed 1/1 and archived in
`Agentic/Reports/2026-07-20/allocation-namespace-restoration-closure.md`.
`physics-facade-unification` is closed 3/3 and archived in
`Agentic/Reports/2026-07-20/physics-facade-unification-closure.md`.
`physics-settings-snapshot` is closed 4/4 and archived in
`Agentic/Reports/2026-07-20/physics-settings-snapshot-closure.md`.
`run-execute-deaccretion` is closed 3/3 and archived in
`Agentic/Reports/2026-07-20/run-execute-deaccretion-closure.md`.

`imgui-tracy-editor-campaign` is 17/18; E17 remains unchecked only for
extended hands-on owner acceptance, now parked and non-blocking. Do not
change the default during that evaluation: Legacy is default, ImGui is
explicit opt-in, and only one UI surface owns focus/input at a time.

## Audio Removal

The `codex/remove-audio-pr` branch removes runtime audio ownership and contact
processing, both UI surfaces, operator commands and diagnostics, startup flags
and probes, XAudio linkage, `stb_vorbis`, the contact-audio scene, and all shipped
audio data. Config format v5 deterministically strips legacy
`contact_audio_*` settings. The only remaining audio spellings in live code and
tools are that v4-to-v5 migration and its fixtures.

## Physics Closure

The body-count campaign closed 8/8 on 2026-07-20. Final Profile
`Frame/Physics` P50 changed from P0 as follows: scale-200 0.1106 -> 0.1075 ms,
scale-520 0.8486 -> 0.8021 ms, scale-1,000 1.0688 -> 1.0850 ms,
scale-2,000 1.7852 -> 1.8878 ms, and sleepy-5,000 2.1479 -> 1.3026 ms.
The sleeping-heavy witness is 39.36% faster, while the all-awake 1,000/2,000
witnesses are 1.52%/5.75% slower; the owner explicitly accepted that trade.

The new 520-body unit fixture and six-scene runtime matrix certify byte-exact
0/1/4-worker determinism (18/18 process matches). Algorithmic and tested
multithreaded determinism are present; cross-platform and rollback determinism
are not certified. Full evidence:
`Agentic/Reports/2026-07-20/physics-body-count-scale-closure.md`.

## Final Validation

| Command | Time | Result |
|---|---:|---|
| `python tools\check_allocation_policy.py --self-test` | 0.09 s | PASS |
| `python tools\check_allocation_policy.py --repo .` | 9.14 s | PASS; zero allowlist errors |
| `tools\validate_fast.bat` | 56.95 s | PASS |
| `tools\validate_physics_deep.bat` | 128.05 s | PASS |
| `tools\validate_build.bat Automation` | 14.40 s | PASS; zero warnings/errors |
| `tools\validate_full.bat` | 142.49 s | PASS |
| `Profile\SKULLBONEZ_CORE.exe --platform-profiler-markers --frames 2` | 1.205 s | PASS; marker emission enabled |
| `tools\validate_full.bat` (L5 closure tip) | 143.84 s | PASS |
| `tools\validate_physics.bat` (C1) | 79.83 s | PASS; 44,401-line CSV byte-exact |
| `tools\validate_perf.bat` (C1) | 109.20 s | PASS; zero allocation violations/regressions |
| replay visual fidelity (C1, one engine generation) | 391.01 s | PASS; all positive/negative controls |
| `tools\validate_full.bat` (C1) | 143.12 s | PASS |
| `tools\validate_tests.bat` (C2) | 12.84 s | PASS; 327 cases, 61,131 assertions |
| `tools\validate_tests.bat` (C3 review fixes) | 12.59 s | PASS; 328 cases, 61,341 assertions |
| `tools\validate_physics.bat` (C3) | 79.13 s | PASS; 44,401-line CSV byte-exact |
| `tools\validate_perf.bat` (C3) | 108.54 s | PASS; allocation and comparison gates report no regression |
| `tools\validate_full.bat` (C3) | 140.74 s | PASS; all CPU/coverage and five runtime lanes |
| direct Automation build (X0) | 13.38 s | PASS; moved controller and Run call site compile |
| direct Release build (X0) | 38.67 s | PASS; production macro path compiles |
| `tools\validate_ui_stress.bat` (X0) | 72.77 s | PASS; moved command matrix, clean logs, zero DX12 errors |
| `tools\validate_full.bat` (X0) | 139.54 s | PASS; all CPU/coverage and five runtime lanes |
| direct Automation build (X1 tip) | 10.46 s | PASS; zero warnings/errors |
| direct Release build (X1 tip) | 25.93 s | PASS; zero warnings/errors |
| focused runtime interaction policy test (X1) | 2.44 s | PASS; 1 case, 25 assertions |
| `tools\validate_ui_stress.bat` (X1) | 87.85 s | PASS; direct surface-command matrix and zero DX12 errors |
| `tools\validate_full.bat` (X1) | 148.65 s | PASS; all CPU/coverage and five runtime lanes |
| direct Automation build (X2) | 19.72 s | PASS; zero warnings/errors |
| direct Release build (X2) | 49.56 s | PASS; production macro path, zero warnings/errors |
| focused scene proceed-policy test (X2) | 2.05 s | PASS; 1 case, 9 assertions |
| `tools\validate_automation.bat` (X2) | 40.46 s | PASS; combined replay/prediction/development-UI/Ctrl+0 lane |
| `tools\validate_full.bat` (X2) | 166.92 s | PASS; all CPU/coverage and five runtime lanes |

The first full gate found one Automation-only orphaned `GameObjects`
using-directive after the SkullScope namespace move. It was removed before the
targeted Automation and final full passes.

## Next Handoff

Continue `render-graph-completion` G1 on `nightrunner-20th-july`.
The G0 map records every callback-owned pass, the four remaining live transition
authorities, the class migration order, and the frame-edge exception candidates.
Transfer graph-transient barriers to compiled graph execution first. The Profiler semantic
exception remains deletion-bound to Render HAL M0/M5. E17
extended owner playtest remains parked; keep Legacy default until the owner
explicitly authorizes a switch.
