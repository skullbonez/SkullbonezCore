# SkullbonezCore Session State

Date: 2026-07-20

Keep this file operational and short. Detailed evidence belongs in plans,
reports, and git history. `Agentic/Plans/MASTER-PLAN.md` is the authoritative
plan inventory.

## Current State

| Field | Value |
|---|---|
| Branch | `codex/remove-audio` |
| Current baseline | The audio subsystem, its UI, diagnostics, startup controls, decoder, linkage, authored lab scene, and shipped samples are removed. |
| Current objective | Audio removal is implemented and validated; the branch is ready for review or merge. |
| Active/future progress | 17 / 18 live tasks; 94%. |
| UI ruling | Legacy remains the default. ImGui is explicit `--dev-ui imgui`; atomic hot swap is allowed, simultaneous Legacy/ImGui activation is forbidden. |
| Last broad local gate | Audio-removal `validate_full` passes: 323/323 cases, 61,057 assertions, every coverage floor, Automation replay/prediction smoke, zero DX12 validation errors, three image baselines, and byte-exact physics. |
| Validation for current edits | Fast, performance, deep physics, full, allocation-policy, config-migration, project-filter, and physics-query regression gates all pass. |

## Live Queue

NOW. One live plan, 18 tasks. `imgui-tracy-editor-campaign` is 17/18. E0-E16
and every automatable E17 implementation, review, and validation obligation are
complete. E17 remains unchecked only for extended hands-on owner acceptance.
Do not change the default during that evaluation: Legacy is default, ImGui is
explicit opt-in, and only one UI surface owns focus/input at a time.

## Audio Removal

The `codex/remove-audio` branch removes runtime audio ownership and contact
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

Review or merge `codex/remove-audio`. No pull request has been opened. The
separate E17 owner playtest remains the live portfolio follow-up after this
branch is integrated.
