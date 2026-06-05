# Queryable Physics Diagnostics Plan

## Directive

Make physics debugging cheap for the model.

The model should not ingest entire physics logs to understand a simulation. It should request a deterministic diagnostic artifact, then query that artifact locally for the smallest useful packet: summary, events, frame windows, body timelines, contact rows, islands, stack state, energy, broadphase, water, or regression deltas.

The existing byte-exact physics CSV remains the validation artifact. The new diagnostics system is a model-facing analysis artifact.

## Problem

The current Debug physics log writes one row per body per frame:

```text
frame,idx,name,posX,posY,posZ,velX,velY,velZ,speed,omegaX,omegaY,omegaZ,omegaMag,qX,qY,qZ,qW,grounded,sleeping,sleepInhibited
```

For `physics_regression_solver.scene`, that is about 20,000 lines for one 1000-frame run. It is deterministic and useful for baseline diffing, but it is expensive and inefficient for model reasoning. The model has to infer high-level questions from raw samples:

- Is the scene losing or gaining energy?
- Which body or contact caused the spike?
- Did an island become quiet but fail support?
- Did a body sleep while unsupported?
- Which stack link is drifting?
- Are contacts warm-starting?
- Are manifolds stable?
- Is rolling actually no-slip, or are bodies sliding?
- Did broadphase pair count explode?
- Which frame should be inspected next?

Those answers should be computed locally and returned as compact query results.

## Goals

1. Add a command-line diagnostics mode that writes a structured physics diagnostic trace.
2. Automatically force deterministic fixed-step playback when diagnostics mode is enabled.
3. Print a clear runtime message when diagnostics mode forces fixed step.
4. Keep the old full CSV path unchanged for byte-exact validation.
5. Build a Python query tool that imports the trace into SQLite on first query.
6. Make repeated model queries cheap, local, stable, and bounded.
7. Provide a syntax/reference file with pre-baked questions for common debugging tasks.
8. Update `AGENTS.md` so future agents use diagnostic queries instead of pasting entire logs.

## Non-Goals

- Do not replace `tools\validate_physics.bat` or the committed CSV baseline.
- Do not link SQLite into the C++ engine in the first implementation.
- Do not make diagnostics the default for all runs.
- Do not dump full SQLite tables into model context.
- Do not optimize the physics solver while adding diagnostics, except for obvious no-allocation logging hygiene.

## Proposed User Workflow

Run a deterministic diagnostic trace:

```bat
Debug\SKULLBONEZ_CORE.exe --scene SkullbonezData\scenes\at_rest.scene --physics-diag Debug\at_rest.physicsdiag.ndjson
```

Because `--physics-diag` is present, the runtime should force fixed-step mode and print:

```text
Physics diagnostics enabled: forcing fixed_step for deterministic queryable trace.
```

Then query locally:

```bat
tools\physics_query.py Debug\at_rest.physicsdiag.ndjson summary
tools\physics_query.py Debug\at_rest.physicsdiag.ndjson events --severity high
tools\physics_query.py Debug\at_rest.physicsdiag.ndjson event E12 --window 30
tools\physics_query.py Debug\at_rest.physicsdiag.ndjson body box_03 --frames 540:590
tools\physics_query.py Debug\at_rest.physicsdiag.ndjson island 4 --frame 570
tools\physics_query.py Debug\at_rest.physicsdiag.ndjson contacts --frame 570 --body box_03
```

On first query, the tool creates or refreshes:

```text
Debug\at_rest.physicsdiag.sqlite
```

Subsequent queries use SQLite directly.

## Command-Line Design

### Engine Arguments

Add:

```text
--physics-diag <path>
--physics-diagnostics <path>   alias
```

Behavior:

- Enables structured physics diagnostics.
- Requires a path ending in `.ndjson` by convention, but does not need a hard failure if the extension differs.
- Forces fixed-step mode before scene playback begins.
- Prints the fixed-step message once per process when fixed-step was not already enabled.
- Writes run metadata stating that fixed step was forced by diagnostics mode.
- Does not enable or modify `--physics-log`.
- Should work with `--scene`, `--suite`, and generated solver scenes.

Suggested parser state:

```cpp
char physicsDiagPath[256] = {};
bool physicsDiagRequested = false;
bool fixedStepForcedByPhysicsDiag = false;
```

If diagnostics mode is enabled while `--fixed-step` is already present, use a quieter message:

```text
Physics diagnostics enabled: fixed_step already active.
```

### Scene Directive

Defer scene-file support initially. A command-line-only first pass keeps the behavior explicit and avoids accidental diagnostic output from committed scenes.

Later optional directive:

```text
physics_diag Debug/scene.physicsdiag.ndjson
```

If added, it should follow the same fixed-step forcing behavior.

## Artifact Format

The engine writes append-only newline-delimited JSON:

```text
*.physicsdiag.ndjson
```

NDJSON is preferred over one large JSON object because:

- The engine can append without retaining the whole run in memory.
- A partial trace is still inspectable after a crash.
- Python can stream import into SQLite.
- It is easy to keep C++ output simple.

Every row has:

```json
{
  "kind": "frame",
  "run": "R1",
  "frame": 570,
  "data": {}
}
```

Use stable `kind` values:

| Kind | Purpose |
|------|---------|
| `run` | One row at start: scene, seed, config, fixed-step state, solver mode, commit when available. |
| `frame` | Per-frame aggregate stats. |
| `body` | Per-body state for each frame or sampled frame. |
| `contact` | Contact/manifold row data for terrain and object contacts. |
| `island` | Per-island summary for sleep/support state. |
| `island_member` | Body-to-island membership when needed. |
| `support_edge` | Stack/support graph edges. |
| `broadphase` | Candidate-pair and spatial-grid stats. |
| `event` | Detected anomaly or important transition. |
| `end` | Final counts, hashes, and close status. |

The model should not read this file directly. It should call `tools\physics_query.py`.

## SQLite Import

`tools\physics_query.py` owns SQLite. The C++ runtime does not.

On first query:

1. Compute a source identity from path, size, and modification time.
2. If `*.sqlite` is missing or stale, rebuild it.
3. Stream the NDJSON rows into SQLite in one transaction.
4. Create indexes after bulk insert.
5. Run the requested query and print bounded JSON.

Suggested cache path:

```text
Debug\at_rest.physicsdiag.sqlite
```

Suggested metadata table:

```sql
create table source_files(
  path text primary key,
  size_bytes integer not null,
  modified_utc text not null,
  imported_utc text not null
);
```

## Initial SQLite Schema

Keep the first schema broad enough for recent physics work, but simple enough to implement quickly.

```sql
create table runs(
  run_id text primary key,
  scene text,
  suite text,
  seed integer,
  fixed_step integer,
  fixed_step_forced_by_diag integer,
  renderer text,
  solver text,
  frame_count integer,
  config_json text
);

create table frames(
  run_id text,
  frame integer,
  time_seconds real,
  body_count integer,
  awake_count integer,
  sleeping_count integer,
  supported_count integer,
  inhibited_count integer,
  contact_count integer,
  island_count integer,
  total_energy real,
  linear_energy real,
  angular_energy real,
  max_speed real,
  max_speed_body integer,
  max_omega real,
  max_omega_body integer,
  max_penetration real,
  max_penetration_contact text,
  primary key(run_id, frame)
);

create table bodies(
  run_id text,
  frame integer,
  body_id integer,
  name text,
  shape text,
  pos_x real,
  pos_y real,
  pos_z real,
  vel_x real,
  vel_y real,
  vel_z real,
  omega_x real,
  omega_y real,
  omega_z real,
  q_x real,
  q_y real,
  q_z real,
  q_w real,
  speed real,
  omega_mag real,
  linear_energy real,
  angular_energy real,
  sleeping integer,
  sleep_supported integer,
  sleep_inhibited integer,
  sleep_counter integer,
  island_id integer,
  primary key(run_id, frame, body_id)
);

create table contacts(
  run_id text,
  frame integer,
  contact_id text,
  body_a integer,
  body_b integer,
  contact_type text,
  feature_id integer,
  point_count integer,
  normal_x real,
  normal_y real,
  normal_z real,
  penetration real,
  normal_impulse real,
  tangent_impulse real,
  slip_speed real,
  rolling_residual real,
  warm_started integer,
  supports_sleep integer,
  primary key(run_id, frame, contact_id)
);

create table islands(
  run_id text,
  frame integer,
  island_id integer,
  body_count integer,
  awake_count integer,
  sleeping_count integer,
  supported_count integer,
  inhibited_count integer,
  eligible integer,
  can_sleep integer,
  max_speed real,
  max_omega real,
  total_energy real,
  primary key(run_id, frame, island_id)
);

create table island_members(
  run_id text,
  frame integer,
  island_id integer,
  body_id integer,
  primary key(run_id, frame, island_id, body_id)
);

create table support_edges(
  run_id text,
  frame integer,
  supporter integer,
  supported integer,
  source text
);

create table broadphase(
  run_id text,
  frame integer,
  candidate_pairs integer,
  contact_pairs integer,
  rejected_pairs integer,
  active_cells integer,
  max_cell_occupancy integer,
  collision_cell_count integer,
  primary key(run_id, frame)
);

create table events(
  run_id text,
  event_id text,
  frame integer,
  type text,
  severity text,
  body_a integer,
  body_b integer,
  island_id integer,
  summary text,
  data_json text,
  primary key(run_id, event_id)
);
```

Indexes:

```sql
create index idx_events_type on events(type, severity, frame);
create index idx_bodies_body_frame on bodies(body_id, frame);
create index idx_contacts_frame_body_a on contacts(frame, body_a);
create index idx_contacts_frame_body_b on contacts(frame, body_b);
create index idx_islands_frame on islands(frame);
create index idx_members_body_frame on island_members(body_id, frame);
```

## Query Tool Design

Path:

```text
tools\physics_query.py
```

Usage:

```bat
tools\physics_query.py <trace.ndjson> <command> [args]
```

Default output is compact JSON. Add `--pretty` for human-readable indentation. Add `--limit N` to expand bounded lists.

Required first-pass commands:

| Command | Purpose |
|---------|---------|
| `summary` | One compact run overview and top anomalies. |
| `events` | Ranked anomaly/transition list. |
| `event <id>` | Focused event context with suggested follow-up queries. |
| `body <id-or-name>` | Body timeline and state over a frame window. |
| `frame <n>` | Aggregate frame state plus top bodies/contacts. |
| `contacts` | Contact rows by frame/body/type. |
| `island <id>` | Island membership and sleep/support state. |
| `stacks` | Support chains and stack stability. |
| `energy` | Energy trend, spikes, and contributors. |
| `rolling` | Rolling residual/slip/spin diagnostics. |
| `broadphase` | Pair counts, grid stats, hot frames. |
| `water` | Buoyancy/submersion summaries when water data exists. |
| `compare` | Compare two diagnostic traces by summary/events/frame hashes. |
| `sql` | Read-only SQL escape hatch for advanced local analysis. |

Use frame windows consistently:

```text
--frames 540:590
--frame 570
--window 30
```

Use body filters consistently:

```text
--body 17
--body box_03
--body-name BoxA
```

## Pre-Baked Questions

The reference file `Agentic/Reference/physics-query-reference.md` should document pre-baked model questions and their commands.

Initial named questions:

| Question | Command |
|----------|---------|
| `what_changed` | `summary` plus top events. |
| `why_not_resting` | `events --type rest,sleep,support` and `island` follow-up. |
| `unsupported_sleepers` | `events --type unsupported_sleep`. |
| `penetration_spikes` | `events --type penetration_spike`. |
| `energy_spikes` | `events --type energy_spike`. |
| `fastest_bodies` | `body --top-speed` or `summary --top bodies`. |
| `stack_health` | `stacks --frames A:B`. |
| `rolling_health` | `rolling --frames A:B`. |
| `contact_health` | `contacts --top impulse,penetration,slip`. |
| `island_churn` | `events --type island_split,island_merge,island_churn`. |
| `broadphase_health` | `broadphase --frames A:B`. |
| `water_health` | `water --frames A:B`. |
| `first_bad_frame` | `events --first` or `compare --first-divergence`. |

## Queryable Physics Surface

The first implementation should be broad enough to cover the physics work that has been active recently.

### Run and Configuration

Queryable:

- scene path
- suite path
- renderer
- solver mode
- seed
- frame count
- fixed-step state
- whether diagnostics forced fixed step
- relevant physics config values: gravity, contact epsilon, restitution threshold, friction, rolling friction, spin friction, broadphase cell size

### Body State

Queryable:

- position
- linear velocity
- angular velocity
- orientation quaternion
- speed
- angular speed
- shape type
- mass
- inverse mass
- inertia diagonal
- sleeping
- sleep support
- sleep inhibition
- sleep counter
- island membership

### Energy

Queryable:

- total kinetic energy
- linear kinetic energy
- angular kinetic energy
- per-body energy
- frame-to-frame energy delta
- top energy contributors
- energy spikes
- unexpected energy growth in friction/rest scenarios

Potential energy can be added later once the terrain/world reference height policy is explicit.

### Rest, Sleep, and Support

Queryable:

- quiet bodies
- sleeping bodies
- sleep candidates
- bodies below threshold but unsupported
- bodies supported but not quiet
- sleep-inhibited bodies
- unsupported sleepers
- wake transitions
- terrain support
- stack-propagated support
- directed support edges
- sleep island eligibility

### Islands

Queryable:

- island count
- largest island
- island membership
- island awake/sleeping counts
- island eligibility
- island churn
- island split/merge events
- island energy
- island max velocity
- island sleep transition frame

### Contacts and Manifolds

Queryable:

- active contacts per frame/body
- terrain contacts
- object-object contacts
- sphere/sphere, sphere/box, box/box, box/terrain
- contact point count
- feature IDs
- penetration
- normal
- normal impulse
- tangent impulse
- slip speed
- rolling residual
- warm-start use
- support eligibility
- impulse spikes
- correction-heavy frames

### Stacks

Queryable:

- support chains
- stack depth
- bottom support body
- unsupported links
- drifting bodies in a stack
- bodies quiet but not support-eligible
- bodies support-eligible but still moving
- stack island stability

### Rolling and Friction

Queryable:

- slip speed at contact
- no-slip residual
- rolling residual by body
- tangent impulse saturation
- drill spin
- rolling friction activity
- spin decay
- bodies sliding while support-eligible
- roll-align corrections once those are instrumented

### Terrain Support Policy

Queryable:

- stable face support
- edge/point support rejection
- terrain-supported vertex count
- best face-normal dot
- support gap range
- whether rest-only policy was enabled
- whether unstable contact rows were skipped

### Broadphase

Queryable:

- candidate pair count
- rejected pair count
- real contact pair count
- active grid cell count
- max cell occupancy
- collision cell keys or count
- broadphase hot frames
- candidate/contact ratio

### Water and Buoyancy

Queryable when water fields are instrumented:

- submerged body count
- submersion percent
- buoyancy force
- drag force
- bodies crossing fluid height
- floating sleepers
- high drag/energy loss events

### Regression and Comparison

Queryable:

- first divergent frame between two traces
- changed events
- changed body summary
- changed contact counts
- changed island sleep frame
- changed max penetration
- changed energy trend
- deterministic hashes per frame or per section

## Event Detectors

The trace should include computed events so the model starts from a small ranked list.

Initial event types:

| Event Type | Trigger |
|------------|---------|
| `energy_spike` | Frame energy delta exceeds threshold. |
| `penetration_spike` | Max penetration exceeds threshold or local percentile. |
| `impulse_spike` | Normal/tangent impulse exceeds threshold. |
| `unsupported_sleep` | Body is sleeping without terrain or stack support. |
| `sleep_inhibited_quiet` | Body is quiet but sleep-inhibited for many frames. |
| `failed_to_sleep` | Body remains quiet and supported but does not sleep after expected counter. |
| `wake_event` | Sleeping body wakes. |
| `missed_wake_candidate` | Awake moving body overlaps or hits sleeping body without wake. |
| `island_churn` | Island membership changes repeatedly over a short window. |
| `stack_drift` | Supported stack body moves above small threshold over a quiet window. |
| `rolling_slip` | Rolling body has persistent contact slip above threshold. |
| `friction_saturation` | Tangent impulse sits at friction limit for many frames. |
| `terrain_edge_support_rejected` | Terrain contact resolves but is not allowed to seed rest support. |
| `broadphase_spike` | Candidate pairs or max cell occupancy spikes. |
| `water_float_sleep` | Body sleeps while not credibly supported and interacting with fluid. |

Each event row should include:

```json
{
  "kind": "event",
  "event_id": "E12",
  "frame": 570,
  "type": "penetration_spike",
  "severity": "high",
  "body_a": 4,
  "body_b": 17,
  "island_id": 3,
  "summary": "Box/ball penetration reached 0.42 near frame 570.",
  "data": {
    "max_penetration": 0.42,
    "normal_impulse": 18.7,
    "followups": [
      "event E12 --window 30",
      "contacts --frame 570 --body 17",
      "body 17 --frames 540:590",
      "island 3 --frame 570"
    ]
  }
}
```

## Output Budget Rules

The query tool should cap output by default:

- `summary`: at most 20 events and 20 top bodies.
- `events`: at most 50 events unless `--limit` is provided.
- `body`: at most 120 frame samples unless `--detail full` is provided.
- `contacts`: at most 50 rows unless `--limit` is provided.
- `sql`: at most 100 rows unless `--limit` is provided.

Every command should include `relatedQueries` when more detail is available. The model can request the next small packet instead of expanding the whole trace.

## Implementation Phases

### Phase 1 - Documentation and Contract

Status: this plan.

Tasks:

1. Add this plan.
2. Add `Agentic/Reference/physics-query-reference.md`.
3. Update `AGENTS.md` with the model-facing diagnostic directive.

Validation:

```text
No validation required for documentation-only changes.
```

### Phase 2 - CLI and Runtime Plumbing

Status: implemented in `codex/physics-queryable-diagnostics`.

Impact area: scene system / physics diagnostics.

Tasks:

1. Add parser support for `--physics-diag` and alias `--physics-diagnostics`.
2. Store the path in runtime init args.
3. Force fixed-step when diagnostics are requested.
4. Print the fixed-step message.
5. Pass the diagnostics path into `SkullbonezRun` and `GameModelCollection`.
6. Write only the `run` and `end` NDJSON rows at first.

Files likely touched:

- `SkullbonezSource/SkullbonezInit.cpp`
- `SkullbonezSource/SkullbonezRun.cpp`
- `SkullbonezSource/SkullbonezRun.h`
- `SkullbonezSource/SkullbonezGameModelCollection.cpp`
- `SkullbonezSource/SkullbonezGameModelCollection.h`
- `Agentic/Reference/runtime-reference.md`

Validation:

```bat
tools\validate_full.bat
```

Reason: `SkullbonezInit*`, `SkullbonezRun*`, and `SkullbonezGameModelCollection*` are mapped to full validation or stricter validation in `AGENTS.md`.

### Phase 3 - Frame and Body Rows

Status: implemented in `codex/physics-queryable-diagnostics`.

Impact area: physics diagnostics.

Tasks:

1. Add a small `PhysicsDiagnostics` helper or equivalent private methods.
2. Emit frame aggregate rows.
3. Emit body rows.
4. Compute linear/angular kinetic energy from mass, velocity, angular velocity, and inertia.
5. Add basic event detection: energy spike, fastest body, unsupported sleep.
6. Keep output deterministic: fixed ordering by body index and frame.

Validation:

```bat
tools\validate_full.bat
```

Also run one manual command to inspect a small trace:

```bat
Debug\SKULLBONEZ_CORE.exe --vsync off --scene SkullbonezData\scenes\at_rest.scene --physics-diag Debug\at_rest.physicsdiag.ndjson
```

### Phase 4 - Query Tool and SQLite Import

Status: implemented in `tools\physics_query.py`.

Impact area: tools / diagnostics.

Tasks:

1. Add `tools\physics_query.py`.
2. Stream-import NDJSON to SQLite.
3. Add stale-cache detection.
4. Implement `summary`, `events`, `event`, `body`, `frame`, and `sql`.
5. Return compact JSON by default.
6. Add `--pretty`, `--limit`, `--frames`, `--frame`, and `--window`.

Validation:

```bat
tools\validate_fast.bat
tools\physics_query.py Debug\at_rest.physicsdiag.ndjson summary
```

Reason: `tools/*` changes require `validate_fast`, then running the changed script.

### Phase 5 - Contacts, Islands, Stacks, and Broadphase

Status: implemented for persistent object contacts, sleep islands, support edges, and broadphase frame stats. Terrain-contact-specific rows and richer rolling event classifiers remain later extensions.

Impact area: physics / performance-sensitive diagnostics.

Tasks:

1. Emit persistent object contact rows from `m_persistentContacts`.
2. Emit terrain contact rows from terrain response, enough to classify support and slip.
3. Emit island rows and membership from the existing union-find pass.
4. Emit support edges from `m_sleepSupportEdges`.
5. Emit broadphase frame stats from `m_candidatePairs`, contacts, and spatial grid summaries.
6. Implement query commands: `contacts`, `island`, `stacks`, `rolling`, `broadphase`.
7. Add event detection for penetration, impulse, rolling slip, stack drift, island churn, and broadphase spikes.

Validation:

```bat
tools\validate_full.bat
tools\validate_perf.bat
```

Reason: this touches physics internals and may add overhead in hot paths.

### Phase 6 - Regression Comparison

Status: first-pass `compare` is implemented in `tools\physics_query.py` using run/frame aggregate differences. Per-frame diagnostic hashes remain a later extension; the legacy CSV is still authoritative for byte-exact physics validation.

Impact area: tools / tests.

Tasks:

1. Add per-frame diagnostic hashes.
2. Implement `compare`.
3. Report first changed frame, changed events, changed body summaries, and changed contact/island states.
4. Keep byte-exact CSV validation as the authoritative pass/fail signal.

Validation:

```bat
tools\validate_fast.bat
tools\physics_query.py Debug\before.physicsdiag.ndjson compare Debug\after.physicsdiag.ndjson
```

### Phase 7 - Optional Runtime Extensions

Later options:

1. Scene directive `physics_diag`.
2. Profile-build diagnostics mode for perf runs.
3. Event-triggered stop-on-hit diagnostics.
4. Repro snapshot export from a diagnostic event.
5. Interactive timeline viewer.

## Engine-Side Design Notes

### Keep Logging Out Of The Solver Core Where Possible

Prefer collecting diagnostics at the boundaries that already have state:

- after `RunSolverPhysics`
- after persistent contact solve
- after terrain response classification
- after sleep/island pass
- after broadphase candidate generation

Do not add heap allocation to inner contact loops. Reuse vectors already retained by `GameModelCollection`, or collect counters and emit after the pass.

### Deterministic Ordering

Diagnostics mode is for analysis, so stable ordering matters:

- body rows ordered by body index
- contact rows ordered by pair key and feature ID
- island rows ordered by island ID/root
- events ordered by frame, then severity, then event type

### Debug Build First

Implement diagnostics for Debug first. Existing physics logging is Debug-oriented, and the first goal is model analysis, not production telemetry.

If Profile diagnostics are later needed for broadphase/perf work, gate the extra output explicitly behind the command-line argument and measure overhead with `tools\validate_perf.bat`.

## Agent Directive

Once `tools\physics_query.py` exists, agents should follow this order for physics debugging:

1. Run or request a diagnostic trace with `--physics-diag`.
2. Query `summary`.
3. Query `events`.
4. Query one or two focused frame/body/island/contact windows.
5. Only inspect the full legacy CSV when validating deterministic baseline diffs or when the query system is missing the required field.

Agents should not paste entire physics CSVs or NDJSON traces into the conversation unless the user explicitly requests raw logs.

## Acceptance Criteria

The system is useful when:

1. A full physics run can be summarized in under 200 lines of query output.
2. The model can identify likely bad frames without reading the raw trace.
3. Common tasks like unsupported sleep, penetration spikes, stack drift, rolling slip, and island churn are one-command queries.
4. Repeated queries do not reparse the NDJSON when the SQLite cache is fresh.
5. The old physics CSV baseline remains byte-exact and unchanged unless intentionally updated.
6. Diagnostics mode clearly states when it forces fixed-step playback.
7. `AGENTS.md` directs agents to the query workflow to control token cost.
