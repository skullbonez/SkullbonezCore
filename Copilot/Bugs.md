# SkullbonezCore — Known Bugs

Crash bug in broadphase when you set model count to 512.

## TODO: Fix stacking

Boxes stacked on top of each other (see `stacking.scene`) tend to drift or
topple slowly over several hundred frames rather than reaching a rock-solid
rest. The solver converges for individual resting contacts but multi-body
stacks expose gaps in the constraint ordering and lack of warm-starting across
frames. Likely needs persistent contact caching (warm-start accumulated
impulses from the previous frame) and/or position-stabilisation correction
applied to the full stack chain, not just individual contact pairs.

## TODO: Fix balls contact resting state

Balls resting on terrain (see `at_rest.scene`) exhibit a visible micro-bounce
or jitter before the sleep threshold kicks in. The ImpulseSolver's restitution
path applies a small bounce impulse even at near-zero normal velocity, keeping
the ball alive longer than it should be. The fix is to gate restitution behind
a minimum separation velocity (typically 1–2 × the Baumgarte bias speed) so
that low-energy contacts go straight to the resting / positional-correction
path rather than bouncing. The same issue affects box-on-terrain resting.

## DX12: Resource deleted before command list close (TDR at end of suite)

During a full test suite run (render_tests.suite), the DX12 backend consistently produces 5 InfoQueue errors near the end of the run: two ID3D12Resource objects are deleted before the command list is closed, which triggers a GPU TDR (device hung). This happens during teardown/cleanup, not during rendering. All screenshots and perf artifacts are produced correctly before the crash. **Pre-existing as of commit 8b4967c — not introduced by any recent changes.**

## Quaternion::Normalise div-by-zero at moment of airborne box-box collision

**Witnessed once** in `collision_demo_box_box.scene` at the exact frame of first contact between two airborne boxes. The exception fired inside `Quaternion::Normalise` (zero-magnitude quaternion). Could not be reproduced reliably under CDB across multiple suite runs.

**Note:** The default `Quaternion()`, `RotationMatrix()`, and `Camera()` constructors were uninitialised and have been fixed to proper identity/zero defaults as a hygiene fix — but this is NOT the cause of this crash. A box with non-zero euler angles (`box_b` has `0 15 0`) would have died at scene load, not at the collision frame.

**Root cause: UNKNOWN.** Something in the box-box collision impulse path produces an exact zero-magnitude quaternion at the moment of first contact. Candidates:
- The roll-align visual correction (`ImpulseSolver.cpp` ~line 848): `RotateAboutAxis` called with an axis that passes the `axisMag > TOLERANCE` check but cancels to zero in the quaternion multiply due to floating-point precision
- A specific flush collision geometry producing an angular impulse that drives the orientation quaternion to zero through a pathological sequence of floating-point cancellations

To investigate:
- Stress test: loop `collision_demo_box_box.scene` 1000+ times looking for recurrence after the ctor fix
- Add a low-magnitude guard in `Quaternion::Normalise`: if `magSq > 0 && magSq < epsilon`, reset to identity and log rather than throw
- Add `#ifdef _DEBUG` logging of quaternion magnitude before each `Normalise` call in the collision path
