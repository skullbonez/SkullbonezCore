# Source Blemish Remediation Closure

Date: 2026-07-23
Branch: `nightrunner-23rd-JUL-26`
Plan: `source-blemish-remediation`, completed and deleted from
`Agentic/Plans/TODO/` under inventory rule 4
Status: COMPLETE — 6/6 phases

## Outcome

The campaign closes all five measured source blemishes without changing
runtime behavior:

- B1 removes the 32-byte authored material name from the hot
  `ColliderRecord` row and keeps it in the parallel cold
  `ColliderAuthoringRecord`.
- B2 registers diagnostic names at topology time and removes them from both
  per-tick `PhysicsEngine::Step` signatures.
- B3 gives eleven extracted Runtime implementations and four residue types
  honest owner names, with no aliases or forwarding headers.
- B4 puts all 48 Core profiler definitions beside `Core/Profiler.h` and keeps
  the six renderer GPU/presentation definitions in Rendering.
- B5 records the owner decision to retain the cohesive ImGui editor owner:
  `BuildEditorShell` alone exceeds the optional physical split target, and a
  smaller unit would require an unauthorized ownership/API redesign.
- B6 completes independent review, static proofs, and final validation.

Accepted implementation commits before this closing commit:

| Phase | Commit |
|---|---|
| B1 | `8db28540` |
| B2 | `242427b4` |
| B3 | `a7432545` |
| B4 | `9bb4c7bf` |
| B5 | `2a8db442` |

## Independent Review

The repository-required rubber-duck review ran once at whole-plan closure.

| Field | Evidence |
|---|---|
| Run | `source-blemish-remediation-duck-01` |
| Prompt | 1,430 characters |
| Response | 1,391 characters |
| Tokens | unavailable |
| Elapsed | 3m42s |
| Verdict | PASS after active-document corrections; no material code finding |

The reviewer found two blocking documentation defects: the validation mapping
still named `RunEditorTracer*`, and two session-state passages said this
campaign had not started. Both are fixed. Renamed-file entries in the
historical comment-audit and wide-signature checklists are also corrected.

One non-blocking historical related-file comment in
`trajectory_ribbon.hlsl` still names the former `RunEditorTracer` path. It is
not an operational mapping or source dependency and is deliberately retained:
editing HLSL would create shader-source and generated-artifact scope unrelated
to this non-behavioral naming campaign.

## Static Closure Proofs

All four required dependency and Replay-boundary commands returned zero rows:

```powershell
rg -n '^#include[[:space:]]+.*(Assets|Gameplay|Physics|Rendering|Scene|World|Runtime|UI)/' SkullbonezSource/Core
rg -n '^#include[[:space:]]+.*(Gameplay|Runtime|UI)/' SkullbonezSource/Physics SkullbonezSource/Rendering
rg -n '^#include[[:space:]]+.*(Assets|Scene|World|Runtime|UI)/' SkullbonezSource/Gameplay
rg -n '^#include[[:space:]]+.*Runtime/Replay/' SkullbonezSource/Physics SkullbonezSource/Rendering SkullbonezSource/Scene SkullbonezSource/World SkullbonezSource/Core
```

Focused census results:

- `ColliderRecord` contains no material-name bytes;
  `ColliderAuthoringRecord` owns `contactMaterialName[32]`.
- Both `PhysicsEngine::Step` overloads contain no diagnostic-name parameter;
  the cold `SetDiagnosticNames(std::span<...>)` command remains.
- The case-sensitive `Run` physical-file census contains eight honest files:
  `Run.cpp`, `Run.h`, `RunFrame.cpp`, `RunRender.cpp`,
  `RunLaunchOptions.Renderer.h`, `RunLaunchOptions.h`, `RunStartupState.h`,
  and `RunTimerState.h`.
- Profiler ownership is 48 Core definitions and six Rendering definitions.
- The diff from plan base `bddd30b7d22d1c026e9c47cbac1b8730a73bbaa9`
  changes no authored data, baselines, goldens, config, schema, or shader path.

## Final Validation

| Command | Time | Result |
|---|---:|---|
| `tools\validate_physics.bat` | 43.28 s | PASS; lifecycle/runtime-handle smoke and byte-exact 44,401-line physics CSV |
| `tools\validate_perf.bat` | 69.90 s | PASS; allocation scan clean, zero gameplay violations, DX12/physics budgets pass |
| `tools\validate_full.bat` | 102.27 s | PASS; CPU/coverage and five runtime lanes, zero DX12 validation errors, accepted captures, byte-exact physics |
| `Profile\SKULLBONEZ_CORE.exe --platform-profiler-markers --frames 2` | 1.24 s | PASS; exit 0, markers requested/enabled, clean shutdown |

Logs:

- `TestOutput/source_blemish_b6_validate_physics.log`
- `TestOutput/source_blemish_b6_validate_perf.log`
- `TestOutput/source_blemish_b6_validate_full.log`
- `TestOutput/source_blemish_b6_platform_profiler_markers.log`

No baseline, golden, authored-data, schema, config, or shader artifact was
refreshed.

## SkullScope Accounting

B2 used one deterministic before/after trace pair:

```text
Debug\SKULLBONEZ_CORE.exe --renderer dx12 --scene SkullbonezData\scenes\physics_bench_varied.scene.json --fixed-step --frames 120 --physics-diag TestOutput\orchestrator_b2_{before|after}.physicsdiag.ndjson --vsync off --shadows off
```

Both NDJSON traces are 9,481,773 bytes; both SQLite caches are 4,644,864 bytes.
Their SHA-256 is identical:
`641BDD98CB7229A82433D0AE74FA20D90C4448A9147D89C5451569A5427B7C83`.
All 20 registered diagnostic names match.

Queries and bounded model-read cost:

| Query | Output exposed to model |
|---|---:|
| before `summary` | 8,775 characters/bytes |
| before `events --limit 12` | 324 characters/bytes |
| before `frame 0` | 11,093 characters/bytes |
| after `summary` | redirected; 0 exposed |
| after `frame 0` | redirected; 0 exposed |
| bounded derived comparison | 587 characters/bytes |
| **Total GPT-read** | **20,779 characters/bytes** |

No query output was truncated and no raw trace or SQLite artifact was ingested
by the model.
