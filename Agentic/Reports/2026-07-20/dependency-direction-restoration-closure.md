# Dependency Direction Restoration Closure

Date: 2026-07-20
Branch: `nightrunner-20th-july`
Plan: `dependency-direction-restoration` (L0-L5, 6/6)

## Outcome

The plan is complete. Shared scene capacity, allocation policy, window
constants, and Tracy client lifecycle are physically owned by Core; solver
snapshots and SkullScope diagnostics are owned by Physics; simulation
scheduling is owned by Runtime; and the renderer-integrated Profiler
implementation is owned by Rendering. WorkerPool consumes a generic Core
string hash rather than Assets.

`AGENTS.md` now states the review-time direction rule and exact physical include
proofs. No forwarding header, namespace alias, solver-snapshot compatibility
type, callback pack, service bag, host pointer, or hidden source duplicate was
introduced. No runtime behavior, baseline, golden, scene, shader, screenshot,
or authored-data artifact changed.

Retained implementation commits:

- `3dc0c5a4cacb93a86f86b421deb32a59b08b40b1` — scene capacity into Core.
- `5c76eaf83e4aa85aefcbbe037a6961ada5e1e145` — allocation policy into Core.
- `7fa5dd21508b682f4c16f629557734c90ff168f4` — solver snapshot into Physics.
- `1eb7f13a0b4f561d91152763e0837edf0788fcd9` — simulation, window, and Tracy ownership.
- `0b93eb3d7f2e0aefb7405d6a352c1e3f81644605` — remaining Core inversion.

## Final Proofs

Every final-source proof returned zero rows:

| Proof | Rows |
|---|---:|
| old `Runtime/Scene/SceneCapacity` path | 0 |
| old `Runtime/Allocation` include/project paths | 0 |
| Physics `Runtime/Replay` references | 0 |
| old replay solver snapshot type names | 0 |
| Physics/Rendering Runtime includes | 0 |
| old SimulationSystem, WindowConstants, and Tracy owner paths | 0 |
| Core physical includes of Assets/Physics/Rendering/Scene/World/Runtime/UI | 0 |
| Physics/Rendering physical includes of Runtime/UI | 0 |
| WorkerPool include of `Assets/AssetKeys.h` | 0 |
| old Core SkullScope/Profiler implementation paths | 0 |

The exact standing commands are recorded in `AGENTS.md`. A fatal diagnostic
label containing `Runtime/Allocation` is not an include or namespace edge and
is explicitly separated from the follow-up namespace proof.

## Surviving Semantic Exceptions

| Surface | Owner and reason | Deletion condition |
|---|---|---|
| `Core/Profiler.h` references Rendering/Text types | Core owns the cohesive CPU marker registry/history; Rendering owns GPU diagnostics and presentation. L4 relocated the implementation without inventing callbacks or inheritance, while a safe state split remains design work. | `render-hal-modernization` M0 defines the Rendering-owned presenter/GPU timing owner; M5 proves Core has zero Rendering/Text types and no hidden callback/global lookup. |
| `Core/Allocation/*` retains the `Runtime::Allocation` namespace | Core owns the policy; L1 preserved a widely consumed public namespace during the behavior-free physical move. The spelling is now an explicit ownership lie, not an invisible waiver. | Immediate `allocation-namespace-restoration` A0 renames all source/tests to `Core::Allocation`, prohibits aliases/shims, and runs allocation, replay, physics, perf, DX12, stress, and full gates. |

Both rows name an owner, reason, deletion condition, and review evidence. The
follow-ups are live MASTER tasks; neither exception authorizes a new edge.

Resolution update, 2026-07-20: `allocation-namespace-restoration` A0 is closed.
`Core/Allocation/*` and every consumer now use `Core::Allocation`, with zero old
namespace, compatibility alias, or forwarding rows. Evidence is in
`allocation-namespace-restoration-closure.md`. The Profiler row remains open
and deletion-bound to Render HAL M0/M5.

## Independent Review

One independent read-only rubber-duck review covered the whole L0-L5 commit
range, current source, project/filter metadata, policies, proofs, plan ledger,
and validation evidence. It initially reopened closure for the hidden Profiler
type seam, over-broad Physics/Rendering wording, retained allocation namespace,
six stale checklist paths, and missing platform-marker smoke.

The corrections narrowed the rule to the intended Runtime/UI ban, fixed the
checklist, registered both bounded deletion conditions, and ran the marker
smoke. The same reviewer then cleared every technical finding. Its final
documentation finding—stale SessionState counts—was corrected to the
post-closure 17/48 ledger before validation.

## Validation

L0-L4 mapped gates and timings are retained in the implementation commits and
former plan history. The final L4 source passed allocation checker self-test
and repo scan, `validate_fast` (56.95s), `validate_physics_deep` (128.05s),
targeted Automation (14.40s), and `validate_full` (142.49s). Comment audit
checked all 14 touched L4 source-bearing files with zero deferred.

L5 platform marker smoke:
`Profile\SKULLBONEZ_CORE.exe --platform-profiler-markers --frames 2` passed in
1.205s with marker emission requested/enabled and clean shutdown. Log:
`TestOutput/logs/l5_platform_profiler_markers.log`.

Final closure-tip `tools\validate_full.bat` passed in 143.84s: project filters
were 719/719, Profile/Automation/Debug builds had zero warnings/errors, every
CPU/coverage/runtime lane passed, DX12 validation stayed at zero with all three
images accepted, and the 44,401-line physics baseline matched byte-for-byte.
The desktop tool surface cannot open a separate visible console, so output is
mirrored to `TestOutput/logs/l5_validate_full.log`.

## Handoff

The completed plan left the active/future ledger under inventory rule 4.
`allocation-namespace-restoration` A0 and `physics-facade-unification` F0-F2
subsequently closed; continue `physics-settings-snapshot` S0. The Profiler
exception remains bound to Render HAL M0/M5.
