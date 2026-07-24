# Solar System Slingshot Usability Closure

Date: 2026-07-24

Branch: `nightrunner-24th-JUL-26`

Result: SSU0-SSU3 complete (4/4)

## Outcome

- Solar-system scenes now use the terrain convention: motion on XY, Z up, and
  a camera looking down the positive-Z axis.
- Prediction defaults to 20 seconds and supports an operator-selected horizon
  up to 120 seconds without changing the 256 MiB capture cap.
- A materially changed velocity sample refreshes prediction while the mouse is
  still held. Instant work supersedes directly; amortized work keeps the newest
  request pending until a coherent prefix has been presented, then promotes
  that prefix before beginning the replacement.
- `solar_system_mars_slingshot.scene.json` contains the Sun, eight planets,
  twenty-two major named moons, and one rocket (32 bodies). Its authored flight
  makes a non-contact Earth flyby before a non-contact Mars encounter.

## Behavioral Evidence

| Proof | Result |
|---|---|
| Existing solar scene orientation | Every body has `z=0`; camera is above +Z with +Y up |
| Live instant drag | Prediction refreshes while held; superseded generation observed |
| Live amortized drag | While pointer capture remained `ToolGesture`, presented generation reached 8 and root-velocity delta reached 0.951203 |
| Earth flyby | Non-contact miss 4.359093 at 1.683333 s |
| Mars encounter | Non-contact miss 2.903577 at 4.825 s |
| Scene inventory | Exact 32-body inventory, including all eight planets and twenty-two major moons |

## Validation

| Gate | Result |
|---|---|
| Focused Automation build and unit tests | PASS |
| `tools\validate_fast.bat` | PASS |
| `tools\validate_tests.bat` | PASS |
| `tools\validate_alt_velocity_visualization.bat` | PASS, instant and amortized held-drag probes |
| Mars slingshot Automation probe | PASS |
| `tools\validate_replay_visual_fidelity.bat` | PASS |
| `tools\validate_full.bat` | PASS in 151.9 s |
| Dependency, Runtime package, UI/Runtime, and Replay-boundary proofs | PASS, no rows |
| `git diff --check` | PASS |
| `tools\validate_perf.bat` / allocation-policy scan | BLOCKED by pre-existing, untouched `Runtime/Editor/EditorTracer.cpp:210` `resize`; this branch neither changes that file nor adds an allocation exception |

No baseline, golden, shader, allocation registration, or allocation allowlist
was changed. The performance-gate blocker is outside this task's diff and was
not hidden with an ad-hoc exception.

## Comment Audit

The final tracked-source inventory contains 21 touched source-bearing files.
All 21 were inspected against
`Agentic/Skills/comment-style-audit/skill.md`; 21 are checked, 0 deferred, and
0 remain unchecked.

## Independent Review

| Reviewer | Started | Initial verdict | Finding and disposition |
|---|---|---|---|
| `/root/solar_slingshot_review` | 2026-07-24T23:44:40+10:00 | REJECTED | Per-sample amortized cancellation could starve visible progress, and the first proof covered only instant mode. Remediated with no-op sample suppression, newest-request pending state, presented-prefix promotion, focused scheduling tests, and a held Amortized mouse probe. |

Prompt/response character and token accounting were not exposed by the agent
interface. Per the orchestrator contract, the reviewer was not looped after
remediation; the focused tests and independent Amortized interaction proof are
the closure evidence.
