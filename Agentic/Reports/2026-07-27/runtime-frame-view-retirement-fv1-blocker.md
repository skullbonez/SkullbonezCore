# Runtime Frame View Retirement FV1 Blocker

Date: 2026-07-27
Branch: `nightrunner-26th-JUL-26`
Plan phase: `runtime-frame-view-retirement` FV1
Measured source: `a8e972f3`

## Blocker

FV1 cannot satisfy all four binding constraints simultaneously:

1. every frame helper receives concrete operands and reaches no `Run` members;
2. every operation remains at or below 12 parameters;
3. `Run::Execute` remains a short phase schedule; and
4. no aggregate, callback pack, `Run&`, frame transaction, or new authority
   carrier may coordinate the work.

`TickPhysics` is the irreducible proof. It owns the fixed-step loop and must keep
these events in their current order for every committed tick:

1. begin presentation capture;
2. apply manipulator physics;
3. step the scene physics owner;
4. publish fixed-contact presentation;
5. complete presentation capture; and
6. conditionally run the replay/post-step hook before the next tick.

It then performs ray/laser updates and conditionally runs camera/director/fluid
logic. The current helper directly reaches 15 owners and has three value
operands, so an honest concrete signature has arity 18.

Splitting leaf operations does not remove the coordinator's need to receive and
forward those owners:

- keeping the coordinator as a `Run` member preserves direct `m_` reach and
  violates the endpoint;
- passing all concrete operands violates the ceiling;
- moving the loop and its conditional post-step hook into `Run::Execute`
  violates the short-schedule non-goal;
- a callback or dependency pack is explicitly prohibited; and
- moving this authority into `SimulationSystem` would give that owner camera,
  replay, UI, diagnostics, and presentation authority it does not own.

`TickScreenshots` and `TickSceneAdvance` show the same coordination pressure
around the ordered `SceneLoadTransaction::Load`, runtime-reaction, and
presentation-output stages, but the fixed-step loop alone is sufficient to
block FV1.

## Owner Decision Required

One constraint must change before plan 5 can continue. The bounded choices are:

- permit the `Run` phase coordinator to retain direct member reach while every
  delegated operation takes concrete operands (smallest source/authority
  change);
- permit a genuine phase-checked frame coordinator/transaction owner, reversing
  the 2026-07-27 rejection;
- permit the coordinator signature to exceed 12; or
- authorize a broader ownership move into a new or existing runtime subsystem.

FV1 made no source edit. Plan 5 is blocked at 1/4 and plan 12 remains blocked on
its frame-signature dependency. The orchestrator advances to dependency-clear
plan 8, `scene-runtime-verb-partition-consolidation`.

## Resolution — 2026-07-27

The owner selected recommendation 1: `Run` phase coordinators may retain direct
member reach, while every delegated operation still receives only concrete
operands and remains at or below 12 parameters. This removes the conflict
described above without introducing a carrier or widening `Run::Execute`.
FV1 subsequently closed with unchanged source and a byte-exact physics pass;
see `runtime-frame-view-retirement-fv1-closure.md`.
