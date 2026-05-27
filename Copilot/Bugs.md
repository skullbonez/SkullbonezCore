# SkullbonezCore — Known Bugs

Crash bug in broadphase when you set model count to 512.

## DX12: Resource deleted before command list close (TDR at end of suite)

During a full test suite run (render_tests.suite), the DX12 backend consistently produces 5 InfoQueue errors near the end of the run: two ID3D12Resource objects are deleted before the command list is closed, which triggers a GPU TDR (device hung). This happens during teardown/cleanup, not during rendering. All screenshots and perf artifacts are produced correctly before the crash. **Pre-existing as of commit 8b4967c — not introduced by any recent changes.**

## Quaternion::Normalise div-by-zero at moment of airborne box-box collision

**Witnessed once** in `collision_demo_box_box.scene` at the exact frame of first contact between two airborne boxes. The exception fired inside `Quaternion::Normalise` (zero-magnitude quaternion). Could not be reproduced reliably — CDB caught it only intermittently across multiple suite runs.

**Root cause (partial):** Default `Quaternion()` constructor left all 4 components uninitialised. Fixed — default ctor now initialises to identity `(0,0,0,1)`. Same fix applied to `RotationMatrix` (now defaults to identity) and `Camera` (all members now zero/false).

**Potentially still present:** The crash may also be triggered by a near-zero-magnitude quaternion produced during a flush (perfectly head-on) box-box collision — the angular impulse could drive the orientation quaternion to near-zero before the next `Normalise` call. To investigate:
- Stress test: run `collision_demo_box_box.scene` in a tight loop (1000+ iterations) and check for recurrence
- Add a magnitude guard in `Quaternion::Normalise`: if `magSq < epsilon` but `magSq != 0`, reset to identity rather than throwing
