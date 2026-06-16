# Source Instructions

Follow the root `../AGENTS.md` contract first. This folder contains production
C++ source for the engine and runtime.

## Local Rules

- DX12 is the only runtime renderer. Do not add OpenGL or DX11 runtime
  dependencies.
- Keep changes scoped to the touched subsystem. Avoid broad cleanup in hot
  renderer, physics, runtime, or initialization paths unless the task asks for
  it.
- Preserve Windows x64, C++17, VS2022 `v143`, `/W4`, and zero-warning
  expectations.
- Use explicit engine-owned names at renderer boundaries. Keep D3D12 details
  inside DX12 backend code or DX12-specific helpers.
- Physics changes must preserve determinism and byte-exact validation behavior.
- Use the root file-to-validation table before committing PR-bound code.
