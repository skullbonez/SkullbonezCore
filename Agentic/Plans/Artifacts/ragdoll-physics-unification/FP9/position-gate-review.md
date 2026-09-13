# Position-gate diagnostic follow-up

The independent reviewer found a retained-observation lifetime defect after the
initial implementation: screenshot capture can restart App's frame before
Skarness publication, so consumption alone does not clear a skipped frame.

The repair clears Planning's fixed two-gate observation in App's unconditional
BeginFrameTurn boundary. Composition records the exact values passed to drawing;
after-render publication consumes and clears them. No Physics state, replay
allocation privilege, capacity, baseline or UI behavior changes.

The follow-up review found no remaining actionable source issue. The focused
Profile test subsequently passed 4 cases / 45 assertions, including missed
publication followed by skipped composition and recovery with a new cue.
Source-design validation passes RunFrame and the test in five compiler contexts.
Native causal-playback13 passes in 17.594 seconds, including identity/frame-bound
marker pixels, retained geometry, camera controls, Evidence scrolling/toggles,
and prediction-off clearing. Its screenshots were inspected. That native run
precedes the frame-boundary repair; validation of the final Automation producer
is still required.

Logs: TestOutput/fp9-position-gates-tests-01.log,
TestOutput/fp9-position-gates-source-design-03.log,
TestOutput/fp9-position-gates-profile-build-05.log,
TestOutput/fp9-causal-playback-13.log.
The initial Profile build invocation failed on duplicate-case environment keys
in MSBuild. The retry used case-normalized environment keys and passed with
zero warnings/errors in 25.031 seconds; no source fix was needed for that failure.

The final bounded follow-up also reviewed the exact two-gate JSON allowance and
causal measurement's private preferences. No actionable issue was found: the
allowance remains inside the existing Diagnostics owner, and the child-only
environment override neither mutates the parent environment nor changes the
analyzer's assertions or thresholds. Full04 remains pending.


## Parallel preferences follow-up

Independent reviewer Boole found no blocking issue in the runner repair. Each
lane receives a fresh preferences path in its existing private workdir; only
child environments change, and explicit lane overrides remain supported.
The concurrent regression preserves the parent file/environment, fails before
the repair and passes afterward. Native DX12 verification remains required;
full04 loaded the old runner, so its contaminated captures justify no baseline
change. Evidence: `TestOutput/fp9-parallel-ui-isolation-negative.log` and
`TestOutput/fp9-parallel-ui-isolation-positive.log`.

Native follow-up now confirms isolation in 12.482 seconds. Automation smoke
passes and writes Editor only in its lane; DX12 writes Canvas in a different
lane, and the parent file remains byte-identical. The screenshot gate still
fails its older UI references with zero DX12 errors. See
`TestOutput/fp9-native-parallel-ui-isolation-01/verified-result.json`.
