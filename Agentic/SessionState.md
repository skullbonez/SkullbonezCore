# SkullbonezCore Session State

Date: 2026-07-20

Keep this file operational and short. Detailed evidence belongs in plans,
reports, and git history. `Agentic/Plans/MASTER-PLAN.md` is the authoritative
plan inventory.

## Current State

| Field | Value |
|---|---|
| Branch | `nightrunner-20th-july` |
| Current baseline | Synced `origin/main` after PRs #127/#128; dependency-direction restoration, allocation-namespace restoration, and physics-facade unification are closed with exact proofs and independent review clear. |
| Current objective | Complete the six remaining architecture-review plans in binding order, recording external blockers without stopping automatable work. |
| Active/future progress | 20 / 44 live tasks; 45%. |
| UI ruling | Legacy remains the default. ImGui is explicit `--dev-ui imgui`; atomic hot swap is allowed, simultaneous Legacy/ImGui activation is forbidden. |
| Last broad local gate | C1 `validate_full` passes in 143.12s: all CPU/coverage/runtime lanes, zero DX12 validation errors, accepted images, and byte-exact physics. |
| Validation for current edits | C2 settings faithfulness: 2/2 touched-test comment audit and `validate_tests` pass (327 cases, 61,131 assertions). |

## Live Queue

NOW. Seven live plans, 44 tasks; 20 complete (45%). The remaining architecture-review campaign
(registered 2026-07-20 from
`Reports/2026-07-20/engine-architecture-review.md`) is the active queue in
binding order: physics-settings-snapshot → run-execute-deaccretion → render-graph-completion →
render-hal-modernization → gameplay-module-extraction →
replay-boundary-containment. Owner decisions at registration: finish the
render-graph migration; PhysicsEngine absorbs PhysicsScene; gameplay
extracts to a new top-level `SkullbonezSource/Gameplay/` module.

`dependency-direction-restoration` is closed 6/6 and archived in
`Agentic/Reports/2026-07-20/dependency-direction-restoration-closure.md`.
`allocation-namespace-restoration` is closed 1/1 and archived in
`Agentic/Reports/2026-07-20/allocation-namespace-restoration-closure.md`.
`physics-facade-unification` is closed 3/3 and archived in
`Agentic/Reports/2026-07-20/physics-facade-unification-closure.md`.
`physics-settings-snapshot` C0-C2 are complete. The 27-field value snapshot is
stamped once by `PhysicsEngine`; fixed-step Physics no longer names Core config
types. Exhaustive default/custom mapping and use-site clamp tests pass; C3
independent review and closure gates are next with no blocker.

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

The first full gate found one Automation-only orphaned `GameObjects`
using-directive after the SkullScope namespace move. It was removed before the
targeted Automation and final full passes.

## Next Handoff

Continue `physics-settings-snapshot` C3 on `nightrunner-20th-july`.
Run the independent bag-shape/clamp review, reconcile provenance against final
source, and pass physics, perf, and full closure gates. The Profiler semantic
exception remains deletion-bound to Render HAL M0/M5. E17
extended owner playtest remains parked; keep Legacy default until the owner
explicitly authorizes a switch.
