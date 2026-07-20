# SkullbonezCore Session State

Date: 2026-07-20

Keep this file operational and short. Detailed evidence belongs in plans,
reports, and git history. `Agentic/Plans/MASTER-PLAN.md` is the authoritative
plan inventory.

## Current State

| Field | Value |
|---|---|
| Branch | `main` |
| Current baseline | Physics body-count scale P0-P7 is complete (P6 evidence-deferred; P7 sleeper/canonical behavior and baselines accepted). The audio subsystem, its UI, diagnostics, startup controls, decoder, linkage, authored lab scene, and shipped samples are removed (PR #127 merged). |
| Current objective | Architecture-review campaign (8 plans, registered 2026-07-20) is the active queue, starting with `dependency-direction-restoration` L0. ImGui/Tracy E17 hands-on owner acceptance is parked and non-blocking. |
| Active/future progress | 17 / 53 live tasks; 32%. |
| UI ruling | Legacy remains the default. ImGui is explicit `--dev-ui imgui`; atomic hot swap is allowed, simultaneous Legacy/ImGui activation is forbidden. |
| Last broad local gate | Audio-removal `validate_full` passes: 323/323 cases, 61,057 assertions, every coverage floor, Automation replay/prediction smoke, zero DX12 validation errors, three image baselines, and byte-exact physics. |
| Validation for current edits | Fast, performance, deep physics, full, allocation-policy, config-migration, project-filter, and physics-query regression gates all pass. |

## Live Queue

NOW. Nine live plans, 53 tasks. The eight-plan architecture-review campaign
(registered 2026-07-20 from
`Reports/2026-07-20/engine-architecture-review.md`) is the active queue in
binding order: dependency-direction-restoration →
physics-facade-unification → physics-settings-snapshot →
run-execute-deaccretion → render-graph-completion →
render-hal-modernization → gameplay-module-extraction →
replay-boundary-containment. Owner decisions at registration: finish the
render-graph migration; PhysicsEngine absorbs PhysicsScene; gameplay
extracts to a new top-level `SkullbonezSource/Gameplay/` module.

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
| `tools\validate_fast.bat` | ~55 s | PASS |
| `tools\validate_perf.bat` | ~118 s | PASS |
| `tools\validate_physics_deep.bat` | ~132 s | PASS |
| `tools\validate_full.bat` | ~183 s | PASS |
| config migration, allocation policy, project filters, and physics-query regression | ~45 s combined | PASS |

The initial fast-gate attempt found only mechanical formatting differences;
the touched files were formatted before every final-source pass above.

## Next Handoff

Begin the architecture-review campaign at
`Plans/TODO/dependency-direction-restoration.md` task L0 through the
orchestrator skill. PR #127 (audio removal) is merged; when campaign tasks
touch surfaces the removal deleted (contact-audio service, Sound tab, audio
frame views), reconcile the task's census against post-removal source first.
The E17 extended owner playtest (separate Legacy and ImGui modes, atomic hot
swap only when desired) remains parked with the owner; keep Legacy default
until the owner explicitly authorizes a switch.
