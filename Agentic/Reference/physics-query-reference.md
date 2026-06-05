# Physics Query Reference

This is the model-facing reference for queryable physics diagnostics.

Main rule: do not upload whole physics logs. Generate a diagnostic trace, then query it locally and return only the smallest useful result.

## Generate A Trace

Target command:

```bat
Debug\SKULLBONEZ_CORE.exe --scene SkullbonezData\scenes\at_rest.scene --physics-diag Debug\at_rest.physicsdiag.ndjson
```

`--physics-diag` automatically forces deterministic fixed-step playback. The runtime should print a message when it does that.

The legacy CSV logger remains separate:

```bat
Debug\SKULLBONEZ_CORE.exe --scene SkullbonezData\scenes\at_rest.scene --physics-log Debug\at_rest.csv
```

Use the legacy CSV for byte-exact validation and baseline diffs. Use the diagnostic trace for model-facing investigation.

## Query A Trace

Target command shape:

```bat
tools\physics_query.py <trace.ndjson> <command> [options]
```

On first query, the tool imports the trace into a sibling SQLite file:

```text
Debug\at_rest.physicsdiag.sqlite
```

Subsequent queries use SQLite and should be fast.

## Common Commands

```bat
tools\physics_query.py Debug\at_rest.physicsdiag.ndjson summary
tools\physics_query.py Debug\at_rest.physicsdiag.ndjson events
tools\physics_query.py Debug\at_rest.physicsdiag.ndjson events --severity high
tools\physics_query.py Debug\at_rest.physicsdiag.ndjson event E12 --window 30
tools\physics_query.py Debug\at_rest.physicsdiag.ndjson frame 570
tools\physics_query.py Debug\at_rest.physicsdiag.ndjson body 17 --frames 540:590
tools\physics_query.py Debug\at_rest.physicsdiag.ndjson body box_03 --frames 540:590
tools\physics_query.py Debug\at_rest.physicsdiag.ndjson contacts --frame 570 --body 17
tools\physics_query.py Debug\at_rest.physicsdiag.ndjson island 4 --frame 570
tools\physics_query.py Debug\at_rest.physicsdiag.ndjson stacks --frames 500:650
tools\physics_query.py Debug\at_rest.physicsdiag.ndjson energy --frames 0:1000
tools\physics_query.py Debug\at_rest.physicsdiag.ndjson rolling --frames 300:700
tools\physics_query.py Debug\at_rest.physicsdiag.ndjson broadphase --frames 0:1000
tools\physics_query.py Debug\at_rest.physicsdiag.ndjson questions
tools\physics_query.py Debug\at_rest.physicsdiag.ndjson questions why_not_resting
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
--type penetration_spike
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
tools\physics_query.py Debug\scene.physicsdiag.ndjson questions
tools\physics_query.py Debug\scene.physicsdiag.ndjson questions energy_spikes
```

### What happened in this run?

```bat
tools\physics_query.py Debug\scene.physicsdiag.ndjson summary
tools\physics_query.py Debug\scene.physicsdiag.ndjson events --limit 20
```

Expected answer:

- run metadata
- top high-severity events
- max energy/speed/penetration
- sleeping/support counts
- suggested follow-up queries

### Why is this scene not resting?

```bat
tools\physics_query.py Debug\scene.physicsdiag.ndjson events --type failed_to_sleep,sleep_inhibited_quiet,unsupported_sleep
tools\physics_query.py Debug\scene.physicsdiag.ndjson stacks --frames 0:1000
```

Follow up with:

```bat
tools\physics_query.py Debug\scene.physicsdiag.ndjson island <id> --frame <frame>
tools\physics_query.py Debug\scene.physicsdiag.ndjson body <body> --frames <start>:<end>
```

### Did anything sleep while unsupported?

```bat
tools\physics_query.py Debug\scene.physicsdiag.ndjson events --type unsupported_sleep
```

Follow up:

```bat
tools\physics_query.py Debug\scene.physicsdiag.ndjson event <id> --window 30
tools\physics_query.py Debug\scene.physicsdiag.ndjson contacts --frame <frame> --body <body>
```

### Where is the penetration/intersection problem?

```bat
tools\physics_query.py Debug\scene.physicsdiag.ndjson events --type penetration_spike
tools\physics_query.py Debug\scene.physicsdiag.ndjson contacts --top penetration --limit 20
```

Follow up:

```bat
tools\physics_query.py Debug\scene.physicsdiag.ndjson event <id> --window 20
tools\physics_query.py Debug\scene.physicsdiag.ndjson body <body> --frames <start>:<end>
```

### Did the solver inject energy?

```bat
tools\physics_query.py Debug\scene.physicsdiag.ndjson energy --frames 0:1000
tools\physics_query.py Debug\scene.physicsdiag.ndjson events --type energy_spike
```

Follow up:

```bat
tools\physics_query.py Debug\scene.physicsdiag.ndjson contacts --frame <frame> --top impulse
tools\physics_query.py Debug\scene.physicsdiag.ndjson body <body> --frames <start>:<end>
```

### Which bodies are moving fastest?

```bat
tools\physics_query.py Debug\scene.physicsdiag.ndjson summary --top bodies
tools\physics_query.py Debug\scene.physicsdiag.ndjson body <body> --frames <start>:<end>
```

### Is the stack stable?

```bat
tools\physics_query.py Debug\scene.physicsdiag.ndjson stacks --frames 0:1000
tools\physics_query.py Debug\scene.physicsdiag.ndjson events --type stack_drift,island_churn,unsupported_sleep
```

Follow up:

```bat
tools\physics_query.py Debug\scene.physicsdiag.ndjson island <id> --frame <frame>
tools\physics_query.py Debug\scene.physicsdiag.ndjson contacts --frame <frame> --body <body>
```

### Is rolling behaving correctly?

```bat
tools\physics_query.py Debug\scene.physicsdiag.ndjson rolling --frames 0:1000
tools\physics_query.py Debug\scene.physicsdiag.ndjson events --type rolling_slip,friction_saturation
```

Follow up:

```bat
tools\physics_query.py Debug\scene.physicsdiag.ndjson body <ball> --frames <start>:<end>
tools\physics_query.py Debug\scene.physicsdiag.ndjson contacts --frame <frame> --body <ball>
```

### Are islands splitting or churning?

```bat
tools\physics_query.py Debug\scene.physicsdiag.ndjson events --type island_churn,island_split,island_merge
tools\physics_query.py Debug\scene.physicsdiag.ndjson island <id> --frame <frame>
```

### Is broadphase causing excess work?

```bat
tools\physics_query.py Debug\scene.physicsdiag.ndjson broadphase --frames 0:1000
tools\physics_query.py Debug\scene.physicsdiag.ndjson events --type broadphase_spike
```

### What changed between two runs?

```bat
tools\physics_query.py Debug\before.physicsdiag.ndjson compare Debug\after.physicsdiag.ndjson
```

Follow up:

```bat
tools\physics_query.py Debug\after.physicsdiag.ndjson frame <first_bad_frame>
tools\physics_query.py Debug\after.physicsdiag.ndjson events --frames <start>:<end>
```

## Advanced SQL

The query tool may expose read-only SQL for local analysis:

```bat
tools\physics_query.py Debug\scene.physicsdiag.ndjson sql "select frame,max_penetration from frames order by max_penetration desc limit 10"
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
