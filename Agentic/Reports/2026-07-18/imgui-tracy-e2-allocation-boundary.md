# ImGui + Tracy E2 Allocation-Boundary Evidence

Date: 2026-07-18

Branch: `nightrunner-18th-july`

> E17 correction, 2026-07-19: the final independent review proved that Tracy's
> private rpmalloc backing maps bypassed the scoped global-new ledger described
> below. E17 reopened the boundary and routes every real rpmalloc backing map
> through an atomic named-owner reservation before `VirtualAlloc`; Tracy's
> embedded LZ4 stream also uses the named-owner allocation wrappers instead of
> raw CRT allocation. Measured standard captures require a truthful 512 MiB hard
> ceiling rather than the nominal 256 MiB row recorded here. The E17 evaluation
> report and final gates supersede the original Tracy accounting claim; the
> ImGui boundary and Release exclusion remain valid.

Task: E2 — development capability and allocation-policy boundary

## Shared Development Capability

`SkoreDevelopmentThirdParty.props` defines exactly one
`SKULLBONEZ_DEVELOPMENT_TOOLS` capability for Debug, Profile, and Automation.
Every Dear ImGui and Tracy vendor translation unit force-includes
`DevelopmentToolsCapability.h`; that header intentionally fails compilation
when the capability is absent. The integration therefore cannot drift into a
partly enabled configuration.

Release and Profile-WPO do not import the development property sheet, compile
the development allocation owner, or compile any of the seven vendor source
files. The final E17 Release rebuild reconfirmed no ImGui/Tracy development
payload in the production compile/link inputs, root object inventory, or
shipping executable. The PDB retains a generic `tracy` source-symbol spelling
from zero-cost profiler fields, so it is not described as token-free; that debug
name is not linked vendor or development-owner functionality.

## Allocation Boundary

`DevelopmentToolAllocationScope` applies one registered owner to the calling
thread without changing the process-wide allocation phase. This is the narrow
boundary that later ImGui and Tracy lifecycle work must enter around vendor
calls. Other threads retain their gameplay owner and phase, so the exception
cannot mask physics, replay, render-submission, or other engine allocations.

The fixed allocator registry keeps the owners separate:

| Owner | Phase metadata | Active-byte cap | Growth | Reason / deletion condition |
|---|---|---:|---|---|
| `DevelopmentTools/DearImGui` | `BackendInit` | 64 MiB | none | Permanent development-only ImGui process storage; absent from shipping configurations |
| `DevelopmentTools/Tracy` | `BackendInit` | 512 MiB | none | Permanent development-only Tracy client buffers plus engine-accounted rpmalloc/LZ4 backing; absent from shipping configurations |

Both rows expose allocation count, active bytes, high-water bytes, and hard cap
through a fixed-copy API. No static allocation-policy allowlist row was added:
the new engine boundary contains no direct heap/growth finding, while the
runtime owner descriptors carry the required owner, phase, reason, cap, and
permanent-development condition. Neither owner can request replay reserve
growth, and exceeding its active-byte cap is a normal allocation-policy
violation rather than an unbounded fallback.

The first concrete vendor lifecycle calls arrive in E3 (Tracy) and E5 (ImGui).
Those tasks must enter this boundary where their calling-thread allocation API
permits it; background vendor threads do not receive a broad process exemption.

## Guard Acceptance Probe

The focused development test allocates 32 bytes in the ImGui scope and 48 bytes
in the Tracy scope while the gameplay guard is active in the Render phase. It
observes zero tool-scope violations and distinct owner totals/caps. An unscoped
16-byte allocation in the same phase then produces a gameplay violation. The
test passed 11/11 assertions, proving the tool permission is exact-owner and
calling-thread scoped rather than a global development bypass.

## Validation

- `Debug\SKULLBONEZ_TESTS.exe --test-case=*Development*` — pass: 1 test case,
  11 assertions; approximately 0.2 seconds.
- `python tools\check_allocation_policy.py --self-test` — pass.
- `python tools\check_allocation_policy.py --repo .` — pass: 395 files,
  43 direct-heap findings, 146 owning dynamic members, 669 STL-growth findings,
  and 0 allowlist errors; existing governed inventory only.
- `tools\validate_tests.bat` — pass: 294 test cases and 21,470 assertions;
  approximately 17 seconds.
- `tools\validate_fast.bat` — pass from final E2 source: formatting, 750/750
  project-filter items, staged-size policy, and zero-warning, zero-error
  Profile/Debug builds.
- `tools\validate_build.bat Release` — pass with 0 warnings and 0 errors in
  43.76 seconds. The compile and link inputs excluded the development owner and
  all ImGui/Tracy vendor sources.
- Targeted Release inspection — pass: no matching tool/capability/owner payload
  in `Release\SKULLBONEZ_CORE.exe`, the production compile/link inputs, or the
  root Release object inventory. The E17 final inspection supersedes the older
  PDB-token wording as described above.
- `tools\validate_full.bat` — pass from final E2 source: mandatory CPU and
  coverage lanes, Automation boundary and replay/prediction smoke, DX12 with
  zero validation errors and passing screenshots, and byte-exact physics.
  The visible run took approximately 3 minutes 20 seconds.

No runtime, replay, visual, physics, coverage-floor, or behavioral baseline was
changed.

## Comment Audit

Touched-source audit, 8/8 files checked and 0 deferred:
`DevelopmentToolsCapability.h`, `DevelopmentToolAllocation.h`,
`DevelopmentToolAllocation.cpp`, `RuntimeReserveAllocator.h`,
`RuntimeReserveAllocator.cpp`, `RuntimeAllocationTracker.cpp`,
`TestReserveAllocator.cpp`, and `tools/validate_project_filters.py`. The C++
files retain complete learning headers and nearby comments for compile-time
capability, thread-local attribution, process-phase isolation, cap enforcement,
and the deliberate violation probe. The substantial project-filter tool keeps
its existing learning header and adds only the owned path prefixes required by
the new files. No subsystem-wide checklist was required.
