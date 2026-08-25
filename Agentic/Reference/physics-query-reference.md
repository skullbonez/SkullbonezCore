# SkullScope Physics Query Reference

SkullScope is the model-facing query system for physics diagnostics.

Main rule: do not upload whole physics logs. Generate a diagnostic trace, then query it locally and return only the smallest useful result.

## Mandatory Agent Reporting

When an agent uses SkullScope, the handoff or final answer must include:

- the exact runtime command used to generate the trace,
- the trace NDJSON path and byte size,
- the SQLite cache path and byte size when a cache was created,
- every `tools\physics_query.bat` command that was run,
- the output size for each query that the GPT model read,
- the total GPT-read size across all query outputs,
- a note when any query output was truncated by the shell, app, or model context.

Keep the accounting honest:

- Raw trace bytes are not GPT-read bytes unless the agent actually opens or pastes the raw trace.
- SQLite cache bytes are not GPT-read bytes unless the agent opens or pastes the cache, which should not happen.
- GPT-read size means the bounded query result text exposed to the model through terminal output, a file read, or a quoted handoff.
- Prefer characters for context accounting and bytes for file/artifact accounting. If both are easy to measure, report both.

If a query result is too large or gets truncated, do not reason from the truncated result as if it were complete. Rerun a narrower query with `--limit`, `--frame`, `--frames`, `--body`, `--type`, or `--severity`.

## Generate A Trace

Target command:

```bat
Debug\SKULLBONEZ_CORE.exe --scene SkullbonezData\scenes\at_rest.scene.json --physics-diag Debug\at_rest.physicsdiag.ndjson
```

`--physics-diag` automatically forces deterministic render-frame lockstep. The runtime should print a message when it does that.
Each imported run keeps the legacy `fixed_step` columns for query compatibility
and also exposes the scene-session request, explicit request, effective
render-frame-lockstep policy, and diagnostics-forced request as separate fields.

The deterministic CSV logger remains separate:

```bat
Debug\SKULLBONEZ_CORE.exe --scene SkullbonezData\scenes\at_rest.scene.json --physics-regression-log Debug\at_rest.csv
```

Use the regression CSV for byte-exact validation and baseline diffs. Use the diagnostic trace for model-facing investigation.

## Query A Trace

Target command shape:

```bat
tools\physics_query.bat <trace.ndjson> <command> [options]
```

Use the `.bat` launcher from PowerShell or `cmd.exe`. It invokes `physics_query.py` through `tools\find_python.bat`, so the workflow does not depend on Windows `.py` file associations.

On first query, the tool imports the trace into a sibling SQLite file:

```text
Debug\at_rest.physicsdiag.sqlite
```

Subsequent queries use SQLite and should be fast.

Regression coverage lives in `tools\check_physics_query_regression.py`. It generates a deterministic trace from `SkullbonezData\scenes\physics_bench_varied.scene.json`, runs representative SkullScope queries, normalizes cache paths out of the JSON, and compares against `TestOutput\baselines\physics_query_varied.json`.

## Measuring Query Output

The simplest reliable pattern is to capture each bounded query result to a small text file, measure it, and only then read or summarize it:

```powershell
tools\physics_query.bat Debug\scene.physicsdiag.ndjson summary | Tee-Object Debug\scope_summary.json
(Get-Item Debug\scope_summary.json).Length
(Get-Content -Raw Debug\scope_summary.json).Length
```

For a final handoff, report something like:

```text
SkullScope artifacts:
- Trace: Debug\scene.physicsdiag.ndjson, 22,191,227 bytes
- SQLite cache: Debug\scene.physicsdiag.sqlite, 4,923,392 bytes

Queries read by GPT:
- tools\physics_query.bat Debug\scene.physicsdiag.ndjson summary
  Output read: 5,842 chars, 5,842 bytes
- tools\physics_query.bat Debug\scene.physicsdiag.ndjson contacts --body marble --frames 320:340 --limit 50
  Output read: 12,104 chars, 12,104 bytes

Total GPT-read SkullScope output: 17,946 chars. Raw trace was not read by GPT.
```

## Common Commands

```bat
tools\physics_query.bat Debug\at_rest.physicsdiag.ndjson summary
tools\physics_query.bat Debug\at_rest.physicsdiag.ndjson events
tools\physics_query.bat Debug\at_rest.physicsdiag.ndjson events --severity high
tools\physics_query.bat Debug\at_rest.physicsdiag.ndjson event E12 --window 30
tools\physics_query.bat Debug\at_rest.physicsdiag.ndjson frame 570
tools\physics_query.bat Debug\at_rest.physicsdiag.ndjson body 17 --frames 540:590
tools\physics_query.bat Debug\at_rest.physicsdiag.ndjson body box_03 --frames 540:590
tools\physics_query.bat Debug\at_rest.physicsdiag.ndjson contacts --frame 570 --body 17
tools\physics_query.bat Debug\at_rest.physicsdiag.ndjson island 4 --frame 570
tools\physics_query.bat Debug\at_rest.physicsdiag.ndjson stacks --frames 500:650
tools\physics_query.bat Debug\at_rest.physicsdiag.ndjson energy --frames 0:1000
tools\physics_query.bat Debug\at_rest.physicsdiag.ndjson rolling --frames 300:700
tools\physics_query.bat Debug\at_rest.physicsdiag.ndjson broadphase --frames 0:1000
tools\physics_query.bat Debug\at_rest.physicsdiag.ndjson solver --frames 0:1000
tools\physics_query.bat Debug\at_rest.physicsdiag.ndjson pipeline --frames 0:1000
tools\physics_query.bat Debug\at_rest.physicsdiag.ndjson motion --frames 0:1000
tools\physics_query.bat Debug\at_rest.physicsdiag.ndjson motion --body <body> --frames 0:1000
tools\physics_query.bat Debug\at_rest.physicsdiag.ndjson questions
tools\physics_query.bat Debug\at_rest.physicsdiag.ndjson questions why_not_resting
```

## Output Controls

```bat
--pretty
--limit 25
--frame 570
--frames 540:590
--window 30
--body 17
--body box_03
--severity high
--type penetration_sustained,penetration_growing
--reason propagated_impulse,body_budget
--submitted yes|no|any
--window-ms 100
```

Default output should be compact JSON. Use `--pretty` only when a human needs to read the result.

## Pre-Baked Questions

Use these when the user asks a natural-language physics question.

The helper data lives in:

```text
Agentic/Reference/physics-query-questions.json
```

List or expand a query pack with:

```bat
tools\physics_query.bat Debug\scene.physicsdiag.ndjson questions
tools\physics_query.bat Debug\scene.physicsdiag.ndjson questions energy_spikes
```

### What happened in this run?

```bat
tools\physics_query.bat Debug\scene.physicsdiag.ndjson summary
tools\physics_query.bat Debug\scene.physicsdiag.ndjson events --limit 20
```

Expected answer:

- run metadata
- top high-severity events
- max energy/speed/penetration
- sleeping/support counts
- suggested follow-up queries

### Why is this scene not resting?

```bat
tools\physics_query.bat Debug\scene.physicsdiag.ndjson events --type failed_to_sleep,sleep_inhibited_quiet,unsupported_sleep
tools\physics_query.bat Debug\scene.physicsdiag.ndjson stacks --frames 0:1000
```

Follow up with:

```bat
tools\physics_query.bat Debug\scene.physicsdiag.ndjson island <id> --frame <frame>
tools\physics_query.bat Debug\scene.physicsdiag.ndjson body <body> --frames <start>:<end>
```

### Did anything sleep while unsupported?

```bat
tools\physics_query.bat Debug\scene.physicsdiag.ndjson events --type unsupported_sleep
```

Follow up:

```bat
tools\physics_query.bat Debug\scene.physicsdiag.ndjson event <id> --window 30
tools\physics_query.bat Debug\scene.physicsdiag.ndjson contacts --frame <frame> --body <body>
```

### Where is the penetration/intersection problem?

```bat
tools\physics_query.bat Debug\scene.physicsdiag.ndjson events --type penetration_sustained,penetration_growing
tools\physics_query.bat Debug\scene.physicsdiag.ndjson contacts --top penetration --limit 20
```

Follow up:

```bat
tools\physics_query.bat Debug\scene.physicsdiag.ndjson event <id> --window 20
tools\physics_query.bat Debug\scene.physicsdiag.ndjson body <body> --frames <start>:<end>
```

### Did the solver inject energy?

```bat
tools\physics_query.bat Debug\scene.physicsdiag.ndjson energy --frames 0:1000
tools\physics_query.bat Debug\scene.physicsdiag.ndjson events --type energy_spike
```

Follow up:

```bat
tools\physics_query.bat Debug\scene.physicsdiag.ndjson contacts --frame <frame> --top impulse
tools\physics_query.bat Debug\scene.physicsdiag.ndjson body <body> --frames <start>:<end>
```

### Is the Catto solver cache/projection healthy?

```bat
tools\physics_query.bat Debug\scene.physicsdiag.ndjson solver --frames 0:1000
tools\physics_query.bat Debug\scene.physicsdiag.ndjson contacts --top impulse --limit 20
tools\physics_query.bat Debug\scene.physicsdiag.ndjson contacts --top penetration --limit 20
```

The importer stores the engine's bounded convergence summaries in its local
SQLite cache, including the maximum per-row squared impulse delta that owns
stopping, the historical diagnostic sum, and row attribution. Request them with:

```bat
tools\physics_query.bat Debug\scene.physicsdiag.ndjson solver --include-convergence --limit 20
```

Those optional diagnostics are deliberately excluded from the default
validated `solver` packet: changing an engineering projection is not authority
to redefine the byte-exact physics query oracle. A governed Physics-plan update
still needs the exact candidate and append-only old/new runtime bundle.

### Which bodies are moving fastest?

```bat
tools\physics_query.bat Debug\scene.physicsdiag.ndjson summary --top bodies
tools\physics_query.bat Debug\scene.physicsdiag.ndjson body <body> --frames <start>:<end>
```

### Is the stack stable?

```bat
tools\physics_query.bat Debug\scene.physicsdiag.ndjson stacks --frames 0:1000
tools\physics_query.bat Debug\scene.physicsdiag.ndjson events --type stack_drift,island_churn,unsupported_sleep
```

Follow up:

```bat
tools\physics_query.bat Debug\scene.physicsdiag.ndjson island <id> --frame <frame>
tools\physics_query.bat Debug\scene.physicsdiag.ndjson contacts --frame <frame> --body <body>
```

### Is rolling behaving correctly?

```bat
tools\physics_query.bat Debug\scene.physicsdiag.ndjson rolling --frames 0:1000
tools\physics_query.bat Debug\scene.physicsdiag.ndjson events --type rolling_slip,friction_saturation
```

Follow up:

```bat
tools\physics_query.bat Debug\scene.physicsdiag.ndjson body <ball> --frames <start>:<end>
tools\physics_query.bat Debug\scene.physicsdiag.ndjson contacts --frame <frame> --body <ball>
```

### Are islands splitting or churning?

```bat
tools\physics_query.bat Debug\scene.physicsdiag.ndjson events --type island_churn,island_split,island_merge
tools\physics_query.bat Debug\scene.physicsdiag.ndjson island <id> --frame <frame>
```

### Is broadphase causing excess work?

```bat
tools\physics_query.bat Debug\scene.physicsdiag.ndjson broadphase --frames 0:1000
tools\physics_query.bat Debug\scene.physicsdiag.ndjson events --type broadphase_spike
```

### Which bodies used Discrete or swept collision policy, and when?

```bat
tools\physics_query.bat Debug\scene.physicsdiag.ndjson motion --frames <start>:<end>
tools\physics_query.bat Debug\scene.physicsdiag.ndjson motion --body <body> --frames <start>:<end>
tools\physics_query.bat Debug\scene.physicsdiag.ndjson motion --frame <frame> --policy swept
```

The frame timeline reports Discrete and swept body counts plus promotion and
demotion events. A body timeline reports the active selector, squared-check
thresholds, measured travel, and each transition frame.

### What changed between two runs?

```bat
tools\physics_query.bat Debug\before.physicsdiag.ndjson compare Debug\after.physicsdiag.ndjson
```

Follow up:

```bat
tools\physics_query.bat Debug\after.physicsdiag.ndjson frame <first_bad_frame>
tools\physics_query.bat Debug\after.physicsdiag.ndjson events --frames <start>:<end>
```

## Advanced SQL

The query tool may expose read-only SQL for local analysis:

```bat
tools\physics_query.bat Debug\scene.physicsdiag.ndjson sql "select frame,max_penetration from frames order by max_penetration desc limit 10"
```

Rules:

- SQL output must be row-limited.
- SQL is read-only.
- Prefer named commands before raw SQL.

## Agent Usage Pattern

When debugging physics:

1. Generate or locate `*.physicsdiag.ndjson`.
2. Run `summary`.
3. Run `events`.
4. Pick one suspicious event/frame/body/island.
5. Run a focused query with `--window`, `--frame`, or `--frames`.
6. Answer from the compact query result.

Do not paste full `*.csv`, `*.ndjson`, or `*.sqlite` contents into conversation unless explicitly requested.
