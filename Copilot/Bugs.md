# SkullbonezCore — Known Bugs

Crash bug in broadphase when you set model count to 512.

## DX12: Resource deleted before command list close (TDR at end of suite)

During a full test suite run (render_tests.suite), the DX12 backend consistently produces 5 InfoQueue errors near the end of the run: two ID3D12Resource objects are deleted before the command list is closed, which triggers a GPU TDR (device hung). This happens during teardown/cleanup, not during rendering. All screenshots and perf artifacts are produced correctly before the crash. **Pre-existing as of commit 8b4967c — not introduced by any recent changes.**