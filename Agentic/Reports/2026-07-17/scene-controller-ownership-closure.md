# Scene Controller Ownership Closure

Date: 2026-07-17/18
Branch: `nightrunner-17th-july`
Plan: `scene-controller-ownership-decomposition` T0-T6
Result: complete

## Closure Result

`SceneController` is now a scene-lifecycle and cross-store topology owner. The
round-6 work removed the relocated UI-navigation and automation-validation
authority, replaced the 22-borrow load surface with four narrow synchronous
participants, and deleted the class-body `.inl` splice.

The first independent T6 review found two credible closure failures and
reopened the affected work:

1. `SceneAutomationGateTracker` and `SceneController` held reciprocal concrete
   dependencies.
2. `SceneRuntimeGeneratedControlContext` was a 13-field multi-domain bag with
   duplicate aliases to the complete scene controller.

The remediation gives validation an immutable `SceneAutomationGatePhysicsView`
and returns value-only `SceneAutomationGateStatus` / report requests. Generated
controls now receive three cohesive synchronous values plus one explicit scene
topology owner reference. The context bag, duplicate aliases, reciprocal owner
dependency, and reach-back paths are gone.

A fresh independent review over the full logical module found zero credible
god-object, retained-participant, callback/backpointer, forwarding-facade, or
unratified-owner findings. It also confirmed that T1-T5 acceptance remained
intact.

## Task Evidence

- T0: complete member/method/22-parameter census and owner map.
- T1: public member-style parameters renamed.
- T2: `Load` and `ExecutePending` decomposed into narrow phase participants.
- T3: browser and UI override state moved to UI-owned
  `SceneNavigationModel`, with value requests returned to scene lifecycle.
- T4: contact and broadphase requirements moved to the validation-harness-owned
  tracker with immutable physics views.
- T5: `SceneController.Objects.inl` deleted; cohesive declarations re-homed in
  the class header and project/filter references removed.
- T6: first-review findings remediated; fresh independent review and both final
  gates passed.

## Validation

- Initial `tools\validate_full.bat` preflight stopped after 4.2 seconds on one
  header-comment alignment defect. The comment was moved to the required line,
  formatting was reapplied, and the complete gate was rerun.
- `tools\validate_full.bat` passed in 3m25s. Evidence:
  `TestOutput/validation/agent_logs/scene_t6_validate_full_stdout.log`.
  It reported matched 713-item project/filter inventories, 282/282 doctests,
  all ratified coverage floors, zero DX12 InfoQueue errors, committed screenshot
  matches, and a byte-exact 44,401-line physics baseline.
- The plan-required single invocation of
  `tools\validate_replay_visual_fidelity.bat` passed in 7m16s. Evidence:
  `TestOutput/validation/agent_logs/scene_t6_validate_replay_visual_fidelity_stdout.log`.
  The sole engine process produced 2,401 ticks, moved all 200 wall bricks,
  presented one cascade, passed durable-artifact and determinism checks, and
  detected every negative control.
- No baseline, golden, screenshot, scene, or authored-data file changed.

## Comment Quality

The final remediation's touched-source audit inspected 10/10 source-bearing
files against `Agentic/Skills/comment-style-audit/skill.md`; zero files were
deferred. The prior T1-T5 commits recorded their own touched-source audits.
