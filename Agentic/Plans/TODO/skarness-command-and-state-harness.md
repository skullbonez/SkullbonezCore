# Skarness Command And State Harness

Date: 2026-09-02
Status: TODO — 0/7 phases implemented
Owner: Runtime Automation
Impact area: Automation, Debug diagnostics, input/command routing, Replay, Prediction, Planning, query tools, and validation

## Goal

Build **Skarness**, a first-class command and observation plane for a running
SkullbonezCore process. An agent must be able to control player-facing behavior
without desktop automation and inspect the resulting state without loading huge
raw logs into model context.

The first delivery completes replay-family control and observation. It must let
an agent select a scene object by stable identity, operate every replay control,
advance the game deterministically, and prove that future paths reached the
renderer. The same registry and protocol will later grow to cover the remaining
player commands. Native menus and raw mouse emulation are explicitly deferred.

Skarness is compiled into **Automation and Debug only**. It is absent from
Profile and Release. The existing `Automation|x64` configuration remains the
primary agent-testing executable; Debug adds deeper developer diagnostics.

## Success Criteria

A clean external client can perform this sequence without touching the desktop:

1. Launch `Automation\SKULLBONEZ_CORE.exe --skarness <session-directory>` with a
   deterministic scene.
2. Connect to the session's local named pipe.
3. Subscribe to full replay and physics diagnostics.
4. Issue `scene.object.select` for `ball_x` and receive its durable scene object
   id in the applied acknowledgement.
5. Enable replay recording and prediction with idempotent setter commands.
6. Advance a requested number of fixed ticks, or advance until a typed condition
   such as `replay.prediction.complete` becomes true.
7. Query the trace while it is still growing and observe replay samples,
   prediction frames, causal topology, trajectory records, retained collision
   wireframes, and renderer-bound line/ribbon submissions.
8. Retry any request id without applying the command twice.
9. Disconnect and reconnect without losing durable diagnostics or allowing the
   paused game to run ahead.

The future-path acceptance test passes only when all of the following agree on
one target/source identity:

- prediction published a non-empty future timeline;
- causal topology contains the selected root and its collision-derived child
  nodes;
- trajectory publication contains root, child incoming, and child outgoing
  lanes with drawable point counts;
- retained entry/rest/horizon marker state contains the collided child
  wireframe evidence;
- the production `ReplayVisualPacket` contains non-empty renderer-bound buffers;
- DX12 replay submission reports non-zero segments, vertices, and a stable
  content hash; and
- a deterministic before/after viewport raster check detects a connected
  path-sized change outside UI chrome.

No assertion may pass from a parallel reconstruction of the path. The test must
observe the same packets and buffers used by production rendering.

## Architecture

### 1. Session And Transport

`--skarness <session-directory>` creates one session manifest and one private
local named pipe. The manifest is published atomically after the pipe is ready
and contains:

- protocol and trace schema versions;
- run id, process id, build configuration, executable hash, scene, seed, and
  fixed-step policy;
- pipe name plus a random session token;
- runtime trace, SkullScope trace, and SQLite cache paths; and
- current session state (`starting`, `ready`, `failed`, or `complete`).

The pipe carries only control requests, acknowledgements, capability discovery,
and small state notifications. It never carries full replay or physics buffers.
Use newline-delimited UTF-8 JSON because commands are bounded and the supplied
Python client must be able to diagnose malformed messages easily.

Each request has this envelope:

```json
{
  "schemaVersion": 1,
  "sessionToken": "...",
  "requestId": "client-stable-id",
  "command": "replay.prediction.set",
  "arguments": { "enabled": true }
}
```

Every request receives `accepted`, followed by exactly one `applied` or
`rejected` result. Results include the request id, accepted turn, applied turn,
scene generation, relevant previous/current state, and an explicit reason on
rejection. The server retains a bounded request-id result cache; replaying a
completed request returns the prior result and cannot repeat the mutation.

Only one controlling client may write commands. Read/query processes do not need
the pipe. Pipe loss pauses further stepping, leaves the traces intact, and lets
the same session token reconnect. Queue exhaustion rejects the command instead
of dropping or delaying it silently. A trace write or flush failure fails the
session and prevents further advancement.

The pipe is restricted to the launching Windows user. Profile and Release
parsers reject `--skarness` as unsupported rather than accepting a dormant flag.

### 2. Deterministic Execution

Skarness starts paused and forces render-frame lockstep. Commands are drained at
one named pre-input boundary; their resulting state is sampled after render.
An acknowledgement is `applied` only after that frame's trace batch has been
flushed, so the client can immediately query durable evidence.

Initial execution commands are:

- `run.step` with an exact fixed-tick count;
- `run.step_frames` with an exact render-frame count;
- `run.until` with a registered condition and maximum fixed-tick budget;
- `run.pause`; and
- `session.stop` for an orderly trace close and process exit.

`run.until` accepts only named, typed conditions published by the state registry;
it does not evaluate arbitrary expressions inside the game. Initial conditions
cover scene-object availability, replay sample counts, replay track position,
prediction building/complete state, topology readiness, visual submission, and
command failure. Timeouts reject with the last observed condition values.

### 3. Command Registry

Skarness commands express player intent but enter through existing typed command
and owner boundaries. Automation must not call mutable Replay, Prediction,
Planning, editor, or renderer internals from the transport thread. The App
composition phase validates a detached request, resolves scene identity when
needed, and submits the same value command used by in-engine operator controls.

Setters are canonical because they are retry-safe. Toggle spellings may be
exposed as explicit parity aliases, but tests and generated examples use setters.
All numeric commands publish accepted/clamped values in their applied result.

The initial registry contains:

**Scene and selection**

- `scene.object.list` and `scene.object.resolve` by display name or durable scene
  object id;
- `scene.object.select` with `inspect` or `editor` scope; duplicate display names
  are rejected unless the id is supplied; and
- `scene.object.clear_selection`.

`inspect` selection must use the existing selection command and replay path-pick
semantics so selecting `ball_x` establishes the same replay path target a player
would establish. It must not fabricate a dense model row as durable identity.

**Replay capture and timeline**

- recording enabled, retention seconds, and memory budget setters;
- jump to start/end, step backward/forward, scrub by normalized position or
  exact replay frame, playback paused/running setter, and return to live;
- restore branch; and
- save/load with an explicit validated path, never a native file dialog.

**Prediction and path presentation**

- path and intercept target setters by durable scene object id;
- prediction enabled, detail mode, horizon, and reveal-rate setters;
- past-path, ragdoll-visual, guide-arc, and path-color-mode setters;
- velocity-edit enabled plus preview/commit/cancel velocity commands carrying
  linear/angular values; and
- deterministic reveal reset/advance commands used by visual validation.

**Cause inspection and planning**

- cause row selection using the row's full generation/frame/topology identity;
- filter text/chip, inspector tab, detail close/return, and bounded record-copy
  retrieval commands;
- porkchop visibility and cell selection;
- trip-planner time-of-flight, plan, commit, and cancel commands; and
- continuous forecast start, reset, and stop commands.

Extend the Replay transport value vocabulary where a replay overlay action still
has only a pointer-specific handler. GameUI, development UI, keyboard bindings,
legacy automation adapters, and Skarness must converge on that vocabulary rather
than add a second Replay mutation path.

One immutable capability catalog exposes every command name, argument schema,
enum value, current availability, and owning player control. A mechanical test
must fail when a replay player control is added without Skarness coverage. Future
domains use the same registry to reach the long-term requirement that every
player command is agent-addressable.

### 4. State Subscriptions And Trace Storage

Skarness copies detached values after render; it does not reflect arbitrary
memory, retain owner references, or add callbacks to lower layers. `subscribe`
selects registered topics and one of `summary`, `normal`, or `full` detail. A
subscription first emits a complete snapshot, then append/change/evict/reset
rows. Every row has:

```json
{
  "schemaVersion": 1,
  "sequence": 42,
  "runId": "...",
  "runtimeTurn": 18,
  "sceneGeneration": 1,
  "simulationTick": 216,
  "renderFrame": 18,
  "replayFrame": 216,
  "topic": "replay.prediction.topology",
  "kind": "change",
  "payload": {}
}
```

Rows are appended to `runtime.skarness.ndjson` in sequence order and flushed as
one complete frame batch. Incomplete final lines are ignored by the importer.
Ring-buffer wrap emits explicit evictions, scene load emits topic resets, and
every large publication carries its owner version/generation so clients cannot
join stale buffers.

Initial replay topics are:

- session, command results, scene object catalog, selection, input mode, camera,
  and frame clocks;
- replay timeline, presentation samples, solver samples, event rows, branches,
  checkpoints, scrubber state, memory policy, and retained-ring statistics;
- prediction controls, scheduling/build state, every published future frame and
  body sample, contacts/evidence, topology nodes, trajectory records and points,
  baseline poses, retained markers, and velocity preview state;
- cause hierarchy, selected evidence identity, inspector transport/drawer state,
  filters, raw records, solver rows, and iterations;
- path, intercept, porkchop, trip-planner, and continuous-forecast state; and
- the complete production replay visual-packet header, buffer hashes/counts,
  optional full float buffers, ghost requests, marker records, and DX12 replay
  submission evidence.

`full` deliberately favors evidence over speed. It emits the complete bounded
rows and renderer buffers even when the game advances slowly. `normal` emits
append deltas plus hashes/counts; `summary` emits scalar health and identity.

SkullScope remains Physics-owned. Broaden its current compile guard so the
existing emitter is available under `_DEBUG` **or**
`SKULLBONEZ_AUTOMATION_DIAGNOSTICS`, while remaining absent from Profile/Release.
It continues writing a separate `physics.physicsdiag.ndjson`; do not make Physics
include Runtime replay types. Add `runtimeTurn`, `sceneGeneration`, and
`simulationTick` correlation values to the Physics diagnostics input so the two
traces join exactly without guessing from wall time.

### 5. Query Client

Add `tools\skarness.bat` backed by Python. It owns launch/connect/send/wait/tail
and query workflows, and prints bounded JSON suitable for an agent.

Required command shapes include:

```text
tools\skarness.bat launch --scene <scene> --session <directory> --detail full
tools\skarness.bat send <session> scene.object.select --name ball_x --scope inspect
tools\skarness.bat send <session> replay.prediction.set --enabled true
tools\skarness.bat step <session> --ticks 1200
tools\skarness.bat wait <session> replay.prediction.complete --max-ticks 2400
tools\skarness.bat query <session> summary
tools\skarness.bat query <session> replay --frames 0:1200
tools\skarness.bat query <session> prediction --target ball_x --full
tools\skarness.bat query <session> render-submission --target ball_x
```

The first query creates `session.skarness.sqlite` and incrementally imports only
complete new NDJSON rows. It imports the Skarness and SkullScope sidecars into
one indexed database, records the source sizes/schema versions, and invalidates
the cache on incompatible schema or replaced source identity. Named queries are
bounded by default; raw read-only SQL is optional and row-limited.

The client reports raw trace bytes, SQLite bytes, each query's output size, and
total model-read bytes using the existing SkullScope accounting convention.

### 6. Compatibility And Ownership

- Keep F8 recording and recorded device-frame playback working unchanged.
- Adapt replay-specific `--interaction-script` actions to the same Skarness
  command dispatcher during migration; do not maintain parallel special-case
  Replay mutation logic.
- Existing manifests remain readable. Skarness session artifacts use a distinct
  schema and extension and are never silently treated as interaction recordings.
- Automation owns transport, request ids, subscriptions, serialization, and
  session files. App owns cross-package composition. Replay/Prediction/Planning
  own their commands and detached publications. Physics owns SkullScope.
- Do not add a callback/subscriber registry to hot engine layers. The Skarness
  subscription registry is an Automation-owned list of App-provided value
  snapshots.
- Detailed capture may allocate only in the Automation/Diagnostics phase and
  must use explicit bounded queues or file-backed growth. It may not add fields
  to `PhysicsBodyRecord` or grant a new post-gameplay growth privilege.

## Implementation Phases

- [ ] **SK0 — Lock schemas and coverage inventory.** Add versioned command,
  capability, acknowledgement, trace-envelope, topic, and condition definitions.
  Inventory every current replay control from keyboard, GameUI overlay,
  development UI, Prediction, and Planning. Add a failing coverage test before
  wiring behavior.
- [ ] **SK1 — Build the session host and client.** Add Automation/Debug startup
  flags, atomic manifest publication, current-user named pipe, request-id
  deduplication, bounded command/result queues, reconnect behavior, and the
  `tools\skarness.bat` client. Profile/Release negative tests must reject the
  feature.
- [ ] **SK2 — Add deterministic pause/step control.** Drain commands at the
  pre-input boundary, sample after render, flush before applied acknowledgements,
  and implement named `run.until` conditions with exact timeout evidence.
- [ ] **SK3 — Complete replay command coverage.** Route selection and every
  replay/prediction/planning control through shared typed commands. Replace
  overlay-only handlers with reusable command values where necessary. Add
  idempotent setters and explicit save/load paths.
- [ ] **SK4 — Publish replay and SkullScope state.** Implement snapshots and
  append/change/evict/reset rows for all listed topics, enable existing
  SkullScope emission in Automation, and correlate both traces without crossing
  the Physics/Runtime dependency boundary.
- [ ] **SK5 — Build incremental SQLite queries.** Import both live sidecars,
  expose bounded summary/replay/prediction/cause/render queries, support tailing
  by sequence cursor, and report artifact/model-read sizes.
- [ ] **SK6 — Replace the fragile prediction smoke.** Express the current
  select-target/predict/wait workflow entirely through Skarness. Require causal
  child trajectories, retained collided-object wireframes, production visual
  buffers, DX12 submission, and the viewport raster oracle. Keep negative
  controls for root-only, semantic-only, UI-only, disconnected-pixel, stale
  identity, and zero-submission failures.

## Test And Validation Plan

### Focused tests

- Protocol framing, malformed JSON, schema mismatch, unauthorized token,
  duplicate request id, queue full, disconnect/reconnect, and trace I/O failure.
- Setter idempotency, toggle alias parity, clamping, rejected ambiguous object
  names, stale scene ids, scene-generation reset, and command ordering within a
  frame.
- Exact paused stepping for fixed ticks/render frames and bounded `run.until`
  timeout behavior.
- Snapshot-before-delta ordering, monotonically increasing sequence ids,
  replay-ring append/evict behavior, prediction publication replacement, and
  incomplete-final-line recovery.
- Capability coverage proving every replay player control has one Skarness
  command and every command has a serializer, availability rule, and result.
- SQLite incremental import, schema invalidation, bounded query output, and exact
  Skarness/SkullScope correlation.

### End-to-end Automation and Debug runs

1. Launch the deterministic replay prediction harness scene in both builds.
2. Subscribe at `full`, select `path_striker`/`ball_x`, enable recording and
   prediction, and step until complete.
3. Prove the complete future-render success criteria above and save the compact
   query result plus before/after screenshots.
4. Exercise every replay command, including unavailable/rejected states, and
   verify applied acknowledgements against the next state publication.
5. Save a replay, load it into a clean process, and compare timeline, camera,
   prediction identity, visual packet fingerprint, and renderer submission.
6. Run the same command transcript twice and require byte-identical normalized
   command/state rows and matching visual hashes.
7. Disconnect during a paused full-detail session, reconnect, and prove no ticks
   or sequence rows were skipped.

### Repository closure

- Focused `SKULLBONEZ_TESTS` and Python client/query self-tests while iterating.
- Automation and Debug affected-target builds.
- Existing interaction recording/playback tests and automation validation.
- Dependency graph, build-config consistency, source-design, plain-language,
  project-filter, and `validate_fast` gates.
- Terminal closure: full unit tests, `tools\validate_automation.bat`, replay
  visual-fidelity validation, Debug SkullScope query regression, and independent
  rubber-duck review.
- Do not refresh a replay, visual, causal, Physics, or SkullScope golden merely
  to make Skarness pass.

## Planned Files And Placement

- New host/protocol/trace values under `SkullbonezSource/Runtime/Automation/`.
- App composition hooks in the existing startup, input-boundary, and
  after-render automation phases.
- Shared replay command extensions in `Runtime/Replay/` and product planning
  commands in `Runtime/Planning/`; no replay types move into Physics or Rendering.
- SkullScope compile-guard and correlation additions remain under
  `SkullbonezSource/Physics/Diagnostics/` and its Physics diagnostics input.
- Client, importer, queries, self-tests, and the end-to-end gate under `tools/`.
- Focused C++ coverage under `SkullbonezTests/` and the existing automation
  interaction scene retained as the deterministic renderer fixture.

## Explicit Assumptions

- “All game state” means all owner-published, behaviorally relevant typed state,
  not arbitrary memory reflection. The first delivery is exhaustive for existing
  SkullScope Physics diagnostics and the Replay/Prediction/Planning/render path;
  later domains register additional topics.
- “Every player input” is implemented as stable semantic commands. Replay is
  complete first. Raw keyboard/mouse injection and native menus are not part of
  this delivery, though recorded device-frame playback remains supported.
- Full diagnostics are allowed to make the simulation run slowly. Correctness,
  complete identity, and durable evidence take priority over real-time speed.
- One controller owns a session. Any number of local readers may tail/query the
  trace artifacts.
- Automation + Debug are supported; Profile + Release contain no Skarness server
  or SkullScope/Skarness diagnostic writer.

## Review Questions

1. Can any replay player control still mutate state without a corresponding
   command/capability entry?
2. Does selecting a target use durable scene identity and the normal replay path
   ownership rather than a model-row shortcut?
3. Can a retry, reconnect, or duplicate request apply a mutation twice?
4. Can the game advance without its requested full-detail evidence becoming
   durable first?
5. Can any trace row join state from different scene generations, prediction
   publications, or replay frames?
6. Does the future-path gate observe the production renderer submission and the
   rasterized viewport rather than a parallel diagnostic reconstruction?
7. Did any new dependency point from Physics or Rendering upward into Runtime
   Automation, Replay, Prediction, or Planning?
8. Is Skarness entirely absent from Profile and Release artifacts?
