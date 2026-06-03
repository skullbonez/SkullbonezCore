# Autonomous Agentic Loop Demo Handoff

## Purpose

This handoff is for a management-facing screen-recorded demo showing an autonomous coding loop on SkullbonezCore:

1. Start from a clean, observable OpenGL demo run.
2. Introduce or recover from a controlled performance regression.
3. Let the agent make measured code changes.
4. Prove correctness with validation output.
5. Prove ROI with profiler/perf marker improvement, elapsed time, and token use.

The intended story is not "the engine was broken." The honest and stronger story is:

> We created a controlled regression, then let the agent diagnose, edit, validate, and measure recovery under the same rules a human engineer must follow.

## Current Branch And State

Current branch:

```bat
codex/demo-gl-profiler-overlay
```

This branch currently contains CLI support for demo-friendly overlay startup:

```bat
--profiler
--show-profiler
--hide-top-text
--no-top-text
--broadphase-visualizer
--broadphase-overlay
```

Use this demo launch once Profile is built:

```bat
Profile\SKULLBONEZ_CORE.exe --renderer gl --vsync off --fixed-step --profiler --hide-top-text --scene SkullbonezData\scenes\perf_test.scene
```

OpenGL is already the default renderer, but include `--renderer gl` in the recording command because it makes the intent obvious to reviewers.

When broadphase is present and active, turn on the broadphase visualizer with `G` after launch. The visualizer makes the spatial grid/collision-cell story visible on screen while the profiler shows the timing story. During a deliberate "broadphase removed/bypassed" regression, either leave the visualizer off or explicitly call out that the grid is no longer participating in candidate pruning.

## Fresh Context Start

At the start of a fresh agent context, read:

1. `AGENTS.md`
2. `README.md`
3. `Agentic/README.md`
4. `Agentic/SessionState.md`
5. This file
6. `Agentic/Plans/agent-loop/broadphase-plan.md`

Then run:

```bat
git status --short --branch
```

Do not revert uncommitted work unless the user explicitly requests it.

## Validation Contract

For documentation-only edits to this handoff, no validation is required.

For the demo overlay CLI changes already made on this branch, validation was:

```bat
tools\validate_renderers.bat
```

That renderer validation has screenshot/parity work and should not be part of the normal physics/perf recovery loop.

For this management demo, this handoff replaces the normal build/validation pipeline during the recorded autonomous loop. The visible recorded loop has exactly one command:

```bat
Agentic\Plans\agent-loop\run_perf_demo_visible.bat
```

`Agentic\Plans\agent-loop\run_perf_demo_visible.bat` opens a real `cmd` window, runs the demo pipeline, and closes the window automatically when the run completes.

For a non-visible/captured run, call:

```bat
Agentic\Plans\agent-loop\validate_perf_demo.bat
```

`Agentic\Plans\agent-loop\validate_perf_demo.bat` runs `Agentic\Plans\agent-loop\validate_physics_visual.bat`, then calls `Agentic\Plans\agent-loop\validate_perf_single.bat`. The physics step builds Debug for regression testing and runs with broadphase visuals enabled. The perf step builds Profile for OpenGL performance capture and does not enable broadphase visuals, so overlay rendering does not contaminate perf timing.

`Agentic\Plans\agent-loop\validate_perf_single.bat` is demo-specific. It builds Profile, runs only OpenGL, captures `Profile\gl_perf_log.csv`, and writes/analyzes `Profile\gl_perf.json`.

Do not run screenshot, renderer, full, or general build-pipeline validation during the recorded loop unless the user explicitly asks for it. If `AGENTS.md` maps a touched file to broader validation, defer that broader gate outside the recorded loop.

Always paste validation output. Never claim success without command output.

## Shadow Broadphase Files

The demo is allowed to start from a no-broadphase engine state. That is not a trick: it replays the original development path from a simpler O(N²) collision candidate loop to the spatial-grid broadphase implementation.

Known-good broadphase implementation snapshots live here:

```text
Agentic\Plans\agent-loop\shadow-broadphase\SkullbonezSource\
```

The shadow set currently includes:

```text
SkullbonezSpatialGrid.h
SkullbonezSpatialGrid.cpp
SkullbonezBroadphaseVisualizer.h
SkullbonezBroadphaseVisualizer.cpp
SkullbonezGameModelCollection.h
SkullbonezGameModelCollection.cpp
```

During the recorded recovery loop, use these as historical/reference implementation files. The agent should inspect and copy/adapt the relevant parts back into `SkullbonezSource\`, then prove recovery with `Agentic\Plans\agent-loop\run_perf_demo_visible.bat`.

The original lightweight broadphase design note is:

```text
Agentic\Plans\agent-loop\broadphase-plan.md
```

It is intentionally short and leaves caching under-designed, so the recorded loop can discover the naive-cache performance gap and then optimize it.

## On-Screen Status Reporting

The demo should make progress legible in this Codex conversation, not only in hidden tool output.

Tool calls return output to the agent after the command completes; they do not stream animated console output live into the chat. Therefore, after each validation/profiling command, the agent must post a compact status card in the conversation.

Use this format:

```text
Perf Loop Status
🟢 physics parity: validate_physics_visual.bat passed, byte-exact CSV match with broadphase visuals
🟢 perf run: Agentic\Plans\agent-loop\run_perf_demo_visible.bat completed
🟢 marker delta: Frame/Physics improved from X.XX ms to Y.YY ms
🔴 blocker: <only if failed, include exact failing command/output excerpt>
```

The important part is that the conversation itself shows the evidence after every loop: command, pass/fail, key marker deltas, and next action.

## Planned Failure Beats

The recording should include a few realistic development failures. This is part of the story: the agent is useful because it can read build/test output, adapt, and keep moving under validation pressure.

Keep these beats honest. Do not fake random failures or claim surprises that were scripted. Present them as staged development milestones that mirror how the feature was originally built.

Recommended sequence:

1. Compile failure during broadphase reintegration.
   - Restore/copy the shadow broadphase implementation in small pieces rather than all at once.
   - It is acceptable for the first build to fail because declarations, includes, call sites, or constructor/member wiring are incomplete.
   - The agent should paste the failing compiler output, explain the missing integration point, fix it, and rerun `Agentic\Plans\agent-loop\run_perf_demo_visible.bat`.

2. Correct but inefficient cache implementation.
   - The first cache pass should preserve physics determinism but still be obviously inefficient.
   - Acceptable naive shapes include rebuilding the cache every frame, not reserving pair/cell vectors, sorting/deduplicating after generating too many candidates, or using broadphase data without dirty-frame reuse.
   - This pass should aim for green physics and red or yellow perf, so the next optimization loop has a visible target.

3. Optimized cached broadphase recovery.
   - Reuse per-frame buffers where safe.
   - Avoid duplicate pair generation as early as possible.
   - Reserve stable storage for hot vectors.
   - Keep deterministic pair ordering if validation depends on byte-exact physics output.
   - Finish with green physics and improved OpenGL perf markers.

Status cards should show these failures clearly:

```text
Perf Loop Status
🔴 build: failed in <file>:<line> with <short compiler error>
⚪ physics parity: not run because build failed
⚪ perf run: not run because build failed
🟡 next action: wire missing declaration/include and rerun the same demo command
```

For an inefficient-but-correct pass:

```text
Perf Loop Status
🟢 physics parity: byte-exact CSV match
🟡 perf run: completed, but Frame/Physics marker is still above target
🔴 marker delta: naive cache did not recover enough; next action is cache reuse/dedup/reserve
```

## Demo Loop Plan

### Phase 0 - Establish The Clean Demo View

1. Build Profile.
2. Launch OpenGL fixed-step, no vsync, profiler visible, top text hidden:

```bat
Profile\SKULLBONEZ_CORE.exe --renderer gl --vsync off --fixed-step --profiler --hide-top-text --scene SkullbonezData\scenes\perf_test.scene
```

3. If broadphase is active, press `G` to enable the broadphase visualizer during manual scene inspection.
4. Confirm the screen shows the engine scene plus bottom-left profiler panel, with no top title/model/physics rows.
5. When broadphase is active, confirm the spatial grid overlay is visible so viewers can connect the profiler improvement to the candidate-pruning structure.

### Phase 1 - Capture Baselines

Run:

```bat
Agentic\Plans\agent-loop\run_perf_demo_visible.bat
```

Record:

- Wall-clock start/end.
- Token usage if the surface exposes it.
- Top relevant profiler markers from `Profile\gl_perf_log.csv` and generated perf JSON.
- Any summary from `Profile\gl_perf.json` and the broadphase-visual physics validation output.

Optional helper:

```bat
py Agentic\Skills\bench_report.py
```

If adding temporary profiler markers, first read:

```bat
Agentic\Skills\skore-cpu-profiler\skill.md
```

### Phase 2 - Controlled Regression

Preferred regression target:

- Broadphase collision detection / spatial grid candidate generation.

Why:

- It is easy to explain to non-engineers.
- It has measurable CPU cost.
- It has correctness risk, so validation matters.
- It ties directly to physics determinism and ROI.

Safer implementation shape:

- Start from a no-broadphase candidate loop or a branch-local bypass of the spatial-grid candidate filter.
- Keep the regression obvious, reversible, and isolated.
- Restore from `Agentic\Plans\agent-loop\shadow-broadphase\SkullbonezSource\` during the recorded agent loop.

Likely impact areas:

- `SkullbonezSpatialGrid*`
- `SkullbonezGameModelCollection*`
- collision pair generation / solver dispatch

Required validation after this phase:

```bat
Agentic\Plans\agent-loop\run_perf_demo_visible.bat
```

Expected result:

- Physics should remain byte-exact if the regression only changes candidate filtering conservatively.
- Perf should worsen enough to make the later recovery visible.
- Broadphase visualizer should be disabled or explained as inactive while the broadphase path is deliberately bypassed.

### Phase 3 - Autonomous Recovery Loop

Loop until both correctness and performance recover:

1. Inspect profiler/perf output.
2. Make one focused code change.
3. Run the required validation.
4. Post the on-screen status card in this conversation.
5. Compare markers and artifact summaries.
6. Keep a short running note of token usage, elapsed time, files changed, and validation output.
7. Once broadphase is restored, run the visible demo with `G` enabled so the grid overlay reinforces the measured profiler improvement.

Do not batch several speculative changes before validation. The demo is strongest when each loop is visible and evidence-driven.

The loop should intentionally pass through the planned failure beats:

- First, a compile failure from incomplete broadphase wiring.
- Second, a correct but inefficient cache pass that proves physics parity while failing to recover enough perf.
- Third, an optimized cached broadphase pass that improves the measured markers.

The point is not that the model is flawless. The point is that the model can use compiler output, deterministic physics checks, and profiler evidence to converge.

### Phase 4 - Final Evidence

Collect:

- Before regression metrics.
- Regressed metrics.
- Recovered metrics.
- Physics validation output.
- Perf validation output.
- Broader validation output only if requested outside the recorded loop.
- Changed-file summary.
- Token budget/usage from the Codex UI if visible.
- Wall-clock elapsed time.

Suggested closing message for the demo:

> The agent changed performance-sensitive engine code, repeatedly validated deterministic physics, measured profiler markers, and recovered the regression in a bounded loop. The cost was visible AI budget and validation time; the return was engineering work compressed into a recorded, auditable session.

## Model Recommendation

For a "lesser capable model" demonstration, use one of these two strategies:

### Best lower-cost autonomous choice: GPT-5.4 mini

Use the Codex/app equivalent of `gpt-5.4-mini` if available.

Reasoning:

- Official model docs describe it as a smaller, faster, lower-cost model aimed at coding, computer use, and subagent-style workloads.
- It keeps enough context and reasoning headroom for C++ engine edits plus validation output.
- It is a better fit than nano-class models for multi-file reasoning, perf interpretation, and deterministic validation loops.

### Best real-time screen-recording choice: GPT-5.3-Codex-Spark

Spark is plausible if it is available in your Codex surface and the task is tightly scaffolded by this handoff.

Positioning:

- Spark is a smaller Codex model optimized for near-real-time coding interaction.
- It is excellent for targeted edits, reshaping logic, and fast iteration while recording.
- It is a research preview with a 128k context window and text-only input at launch.
- Official guidance says its default working style is lightweight and it does not automatically run tests unless asked.

Recommendation:

- Use Spark for the live "look how fast this can iterate" portion.
- Use `gpt-5.4-mini` for the more autonomous diagnose/edit/validate loop if Spark starts skipping validation or losing context.
- Escalate to a stronger model only after repeated validation failures or when the agent needs deeper architectural reasoning.

Avoid using nano-class models for this demo. They may be fine for small docs or rote edits, but this workload needs sustained context, C++ reasoning, performance interpretation, and strict validation discipline.

Official OpenAI references checked on 2026-06-03:

- https://openai.com/index/introducing-gpt-5-3-codex-spark/
- https://developers.openai.com/api/docs/models
- https://developers.openai.com/api/docs/guides/code-generation

## Fresh Agent Prompt

Use this prompt to start the next context:

```text
We are in Y:\SkullbonezCore on branch codex/demo-gl-profiler-overlay.

Read AGENTS.md, README.md, Agentic/README.md, Agentic/SessionState.md, Agentic/Plans/agent-loop/autonomous-agentic-loop-demo-handoff.md, and Agentic/Plans/agent-loop/broadphase-plan.md.

Goal: continue the management demo setup for a fully autonomous agentic loop. The demo should run OpenGL, fixed step, no vsync, profiler visible by default, and top HUD text hidden. Use the CLI flags already added on this branch:

Profile\SKULLBONEZ_CORE.exe --renderer gl --vsync off --fixed-step --profiler --hide-top-text --scene SkullbonezData\scenes\perf_test.scene

When broadphase is active in the visible demo, the regression step starts with the broadphase visualizer enabled. During manual scene inspection, press G after launch to toggle the visualizer. During a deliberate broadphase-bypassed regression, leave it off or explain that the grid is inactive.

Next likely task: create or continue from a controlled no-broadphase/performance-regressed engine state, then run an evidence-driven autonomous recovery loop. Restore the broadphase from Agentic\Plans\agent-loop\shadow-broadphase\SkullbonezSource\. During the recorded loop, run only Agentic\Plans\agent-loop\run_perf_demo_visible.bat unless I explicitly ask for broader validation. If running from Codex and a captured exit is needed, run Agentic\Plans\agent-loop\run_perf_demo_visible.bat --wait, then inspect Profile\gl_perf.json and the physics validation artifacts before posting the status card. After the command returns, post a compact green/red status card in the conversation showing physics parity, perf pass/fail, key marker deltas, and the next action. Do not run screenshot, renderer, full, or general build-pipeline validation during the recorded loop unless I explicitly request it.

The demo should include realistic failure beats. Do not fake random failures; use staged engineering milestones. First restore the broadphase in small pieces so the first build can fail from incomplete wiring, then fix the compiler error from its output. Next implement a correct but inefficient cache pass that preserves physics parity but leaves perf red/yellow. Then optimize cache reuse, duplicate-pair pruning, and stable storage until both physics and perf are green.

Never skip validation. Paste command output. Do not submit, force-push, rebase, or rewrite git history.
```
