# Behavioral Test Depth Closure — 2026-07-11

All six phases are complete. P3 uses the mixed-shape schema-v2 owner round-trip
and production save/reload evidence in `119b359c` and `7fdd91d3`.

Mutation evidence:

- Solver/doctest: omitted friction clamp failed `4.04061 <= 0.1001`; full exited
  4 in 30.0s before later lanes.
- Replay restore: dropped angular velocity changed solver/presentation hash from
  `4558989638039294353` to `8448418270499344807` and body state.
- Interaction: named mutation made full exit 2 in 25.2s; later lanes were not run.
- Scene parser: named mutation made full exit 2 in 29.0s; later lanes were not run.
- DX12 architecture: mutation revealed signed fatal NTSTATUS exits escaped
  `if errorlevel 1` and fatal tests aborted the parent. The wrapper now requires
  exact zero and seven fatal contracts run in isolated child processes. The
  repeated mutation exited 2.

All injected lines were removed. Final `validate_dx12_arch_tests` passed 50/50
in 26.1s. Final `validate_full` passed in 96.5s with all CPU lanes, zero-warning
Profile/Debug builds, DX12 InfoQueue errors = 0 and matching captures, plus the
44,401-line physics baseline byte-exact. Comment audit: 4/4, zero deferred.
