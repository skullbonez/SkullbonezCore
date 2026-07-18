# Small Findings H3 — Cast Boundary Closure

Date: 2026-07-18

Branch: `nightrunner-17th-july`

Task elapsed: approximately 48 minutes

## Result

The H0 census is fully reconciled. Of the original 153 cast/pointer sites, H2
removed one pointer-identity site and H3 eliminated 66 replaceable sites. The
86 required original sites remain with local ABI, aligned-storage, lifetime,
or numeric-conversion rulings. Two new private implementation sites centralize
the unavoidable mechanics: trivially-copyable object representation in
`Core/ByteView.h`, and RenderGraph's private typed-callback payload erasure.
The final ruled inventory is therefore **88 sites, zero unruled**.

The implementation establishes typed byte views for physics, replay,
automation, capture, shader, readback, and editor boundaries; typed native
resource tokens and callbacks for RenderGraph; typed shader borrowing; and
semantic camera diagnostics. The only `const_cast` is the documented
`BCryptHashData` Windows ABI boundary.

During validation, the new replay byte helper exposed an array-to-pointer decay
bug in `AppendChunkId`. The helper now accepts a four-byte array reference and
the generic byte append path rejects pointer types at compile time. The final
doctest run passed all 291 cases and 21,455 assertions.

No baseline, golden, screenshot, authored-data, protected render consumer
interface, or `FRAME_COUNT = 2` change occurred. The seven protected interface
SHA-256 hashes remain:

- `CD9B552B...D1CC` — `IRenderDeviceLifecycle.h`
- `5D3DB83D...4531` — `IRenderResourceFactory.h`
- `63B68509...DB3A` — `IRenderCommandContext.h`
- `B224CE78...BAAE` — `IRenderDiagnostics.h`
- `5001FA3A...BEE2` — `IRenderCaptureBackend.h`
- `7EF6C04F...AE6` — `IRenderRayTracing.h`
- `C3D062B9...A1EF` — `IRenderShaderDevelopment.h`

## Validation

Final source passed every cumulative mapped gate:

- `tools\validate_fast.bat`: 76.986s, zero warnings, all checks and doctests
  passed.
- `tools\validate_perf.bat`: 104.296s, allocation policy clean across 392
  files and all performance checks passed.
- `tools\validate_physics.bat`: 50.862s, standalone/runtime-handle smoke passed
  and the 44,401-line varied-scene CSV matched byte-for-byte.
- `tools\validate_replay_visual_fidelity.bat`: the task's sole invocation
  passed in 435.819s; 2,401 ticks, 200 moved bodies, 187 toppled bodies, all
  causal/presentation/save-load checks, and every negative control passed.
- `tools\validate_dx12_renderer.bat`: 53.931s, zero InfoQueue errors and all
  three committed captures matched.
- `tools\run_graphics_stress.bat 1`: 62.092s; PID 23468 reached frame 13,782
  across 379 scene loads, exited through PID-scoped `WM_QUIT`, and wrote zero
  stderr bytes.
- `tools\validate_dx12_arch_tests.bat`: 24.714s, all 60 architecture tests
  passed after typed fixture updates.
- `tools\validate_full.bat`: 133.253s, every CPU and coverage lane, Automation
  boundary, replay/prediction smoke, DX12 renderer, and physics process passed.

Iteration evidence retained for diagnosis: the first fast attempts caught
  formatting, project-filter ownership, and the replay array-decay regression;
  the first full attempt caught stale DX12 architecture fixtures. Each was fixed
  before the final clean gates above.
