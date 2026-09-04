---
name: skarness
description: Drive and observe SkullbonezCore Automation-build gameplay through the Skarness named-pipe harness. Use when Codex must reproduce, debug, or validate replay controls, prediction selection and rendering, scene interaction, screenshots, or streamed runtime state without desktop keyboard and mouse input, or when adding a player control or observable state that Skarness must expose.
---

# Skarness

Use Skarness as the input and observability plane for a running Automation
build. Treat player intent, resulting runtime state, and rendered pixels as
three separate things that each need evidence.

## Start a clean session

Choose a unique artifact directory under `TestOutput/skarness/` and launch the
Automation executable:

```powershell
python tools\skarness.py launch --session TestOutput\skarness\<case> --exe Automation\SKULLBONEZ_CORE.exe --detail full
python tools\skarness.py capabilities TestOutput\skarness\<case>
```

Inspect capabilities before constructing a workflow. If a required player
control or state field is absent, add it to Skarness as part of the task. Do not
use desktop input to hide the gap.

For a player-driven reproduction, add `--manual`. The Automation build keeps
publishing its state and accepting later client connections, but native mouse,
keyboard, cursor, frame pacing, and initial run state remain player-owned:

```powershell
python tools\skarness.py launch --manual --session TestOutput\skarness\<case> --exe Automation\SKULLBONEZ_CORE.exe
```

Do not send synthetic pointer commands during a manual reproduction unless the
user explicitly hands input ownership back to the harness.

## Drive intent and wait for outcomes

Send typed commands through the client. Prefer stable scene object IDs when the
test fixture supplies them; names may depend on generated setup.

```powershell
python tools\skarness.py send TestOutput\skarness\<case> prediction.select_target sceneObjectId=133
python tools\skarness.py send TestOutput\skarness\<case> replay.set_prediction_enabled enabled=true
python tools\skarness.py wait TestOutput\skarness\<case> prediction.causal_rendered --max-frames 3000
python tools\skarness.py step TestOutput\skarness\<case> --ticks 120
```

Use `command` for individual player controls, `step` or `step-frames` for
deterministic advancement, and `run.until` through `wait` for a named outcome.
Do not interrupt a client after its command is accepted; wait for its applied or
rejected result.

Use `input.pointer_drag` when the behavior depends on the physical pointer
route rather than a semantic UI command. It emits a press frame, a raw-movement
frame, and a release frame through `Input::CaptureDeviceInputFrame`, so edge
creation, capture, camera-look ownership, and dismissal policy are all exercised:

```powershell
python tools\skarness.py command TestOutput\skarness\<case> input.pointer_drag button=right x=800 y=450 deltaX=-65 deltaY=20
```

Exercise scene lifecycle defects in one persistent session. Scene commands do
not report `applied` until the requested generation is activated and ready:

```powershell
python tools\skarness.py load-scene TestOutput\skarness\<case> at_rest.scene.json
python tools\skarness.py reset-scene TestOutput\skarness\<case>
python tools\skarness.py load-demo TestOutput\skarness\<case>
```

## Observe state

Query the latest detached snapshot for a bounded check:

```powershell
python tools\skarness.py query TestOutput\skarness\<case> prediction
python tools\skarness.py query TestOutput\skarness\<case> scene
python tools\skarness.py query TestOutput\skarness\<case> cause
python tools\skarness.py query TestOutput\skarness\<case> render-submission
python tools\skarness.py query TestOutput\skarness\<case> prediction --target ball_x --frames 0:1200
python tools\skarness.py query TestOutput\skarness\<case> physics --limit 50
python tools\skarness.py tail TestOutput\skarness\<case> --after 400 --topic replay.prediction.frames
```

Queries incrementally import only complete JSONL rows from both live sidecars
into `session.skarness.sqlite`. Use the returned `nextSequence` as the next tail
cursor. Named queries omit large float/sample arrays by default; add `--full`
only when those exact values are needed. The result reports raw trace, SQLite,
and model-read bytes so the evidence cost stays visible.

Subscribe when the sequence or state growth matters:

```powershell
python tools\skarness.py watch TestOutput\skarness\<case> --topic replay
```

Only subscribed topics are written to the live pipe; every topic is still
preserved in the session JSONL. Send an empty `topics` array to stop streaming
without closing the command connection. This keeps persistent command clients
from overflowing the finite pipe buffer while they inspect files or screenshots.

Preserve the session JSONL as evidence. An `applied` command result confirms
input routing only; it does not prove the gameplay outcome.

## Assert the result

Bind every assertion to the requested object and generation. For prediction,
require the selected target, published target, path target, and rendered target
to agree before accepting geometry counts. Use `prediction.causal_rendered`
when the expected result includes the cause tree; it must include incoming and
outgoing child paths, reject outgoing samples before the collision entry, and
prove every published collision and ending pose produced a complete wireframe.

Use `verify-future` for a convenient before/after raster check:

```powershell
python tools\skarness.py verify-future TestOutput\skarness\<case> <object-name> --frames 3000
```

Treat raster change as supplemental evidence. Also assert the owning runtime
state and render-submission counts so unrelated pixels cannot be mistaken for
the requested result. Add a focused native test when the expected invariant can
be expressed without a running renderer.

For repeated-selection defects, select several distinct IDs in the same
session and require each request to publish and render its own target. For
causal paths, require zero pre-entry outgoing points. For replay controls,
subscribe to `replay` and verify the requested timeline/control value after
each command.

For prediction or scene-lifecycle changes, run the persistent multi-scene gate.
It covers rest, both tornado showcases, dense authored scenes, generated demo,
and a second 200-body wall prediction after all intervening transitions:

```powershell
python tools\validate_skarness_prediction_matrix.py --self-test
python tools\validate_skarness_prediction_matrix.py --session TestOutput\skarness\prediction-matrix
```

## Capture and stop

Capture the exact accepted state and inspect the image:

```powershell
python tools\skarness.py command TestOutput\skarness\<case> capture.screenshot path=C:\SkullbonezCore\TestOutput\skarness\<case>\result.png
python tools\skarness.py command TestOutput\skarness\<case> session.stop
```

Keep artifacts in `TestOutput/skarness/`; they are local unless the user asks
for a tracked fixture. If orderly shutdown fails, read `session.json`, verify
that the recorded process ID still names the exact Automation executable, and
only then terminate that process.
