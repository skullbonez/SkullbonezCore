---
name: skore-skullscope
description: Use SkullScope physics diagnostics with focused queries, mandatory query logging, and GPT-read data-size accounting.
---

# skore-skullscope

Use this skill whenever a physics investigation needs runtime diagnostics, rest-state analysis, energy accounting, contact/island inspection, or a comparison between a raw physics log and queryable SkullScope output.

## Contract

Do not ingest whole physics CSV, NDJSON, or SQLite files unless the user explicitly asks for raw logs. Generate or reuse a deterministic `--physics-diag` trace, query it with `tools\physics_query.bat`, and expose only bounded query results to the model.

Every handoff or final answer after using SkullScope must print:

- the exact trace-generation command,
- trace NDJSON path and byte size,
- SQLite cache path and byte size when present,
- every `tools\physics_query.bat` query run,
- per-query output size read by GPT,
- total GPT-read query output size,
- whether any query output was truncated.

Raw artifact size and GPT-read size are different numbers. The GPT-read size is only the query output text shown to the model, not the NDJSON trace or SQLite cache sitting on disk.

## Runbook

1. Build Debug when the trace is needed:

```bat
tools\validate_build.bat Debug
```

2. Generate a deterministic trace:

```bat
Debug\SKULLBONEZ_CORE.exe --renderer dx12 --scene SkullbonezData\scenes\scene.scene.json --physics-diag Debug\scene.physicsdiag.ndjson --vsync off
```

3. Measure raw artifacts:

```powershell
Get-Item Debug\scene.physicsdiag.ndjson
Get-Item Debug\scene.physicsdiag.sqlite
```

4. Start broad, then narrow:

```bat
tools\physics_query.bat Debug\scene.physicsdiag.ndjson summary
tools\physics_query.bat Debug\scene.physicsdiag.ndjson events --limit 20
tools\physics_query.bat Debug\scene.physicsdiag.ndjson contacts --frame <frame> --body <body>
tools\physics_query.bat Debug\scene.physicsdiag.ndjson body <body> --frames <start>:<end> --limit 50
tools\physics_query.bat Debug\scene.physicsdiag.ndjson island <id> --frame <frame>
```

5. Capture and measure bounded query output when size accounting matters:

```powershell
tools\physics_query.bat Debug\scene.physicsdiag.ndjson summary | Tee-Object Debug\scope_summary.json
(Get-Item Debug\scope_summary.json).Length
(Get-Content -Raw Debug\scope_summary.json).Length
```

6. If a result is too large or truncated, rerun a narrower query using `--limit`, `--frame`, `--frames`, `--body`, `--type`, or `--severity`.

## Good Final Evidence

Summarize the diagnosis in plain language, then include a compact audit block:

```text
SkullScope artifacts:
- Trace: Debug\scene.physicsdiag.ndjson, 22,191,227 bytes
- SQLite cache: Debug\scene.physicsdiag.sqlite, 4,923,392 bytes

Queries read by GPT:
- tools\physics_query.bat Debug\scene.physicsdiag.ndjson summary
  Output read: 5,842 chars, 5,842 bytes
- tools\physics_query.bat Debug\scene.physicsdiag.ndjson body marble --frames 320:340 --limit 50
  Output read: 12,104 chars, 12,104 bytes

Total GPT-read SkullScope output: 17,946 chars. Raw trace was not read by GPT.
```

## Validation

Documentation-only changes require no validation.

For SkullScope tool or physics diagnostic changes, run this as the targeted
pre-commit/PR validation gate:

```bat
tools\validate_physics.bat
```
