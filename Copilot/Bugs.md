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

## TODO: InputState should be a bit field

`InputState` in `SkullbonezInput.h` stores each key-down and edge-detected flag as a separate `bool` member. As more keys are added the struct grows linearly. Replace the individual bools with a packed bit field (or a pair of `uint32_t` bitmasks — one for current state, one for edge-detected rises) so adding new bindings costs no extra per-frame memory and the whole state fits in a cache line.

## DX12: Resource deleted before command list close (TDR at end of suite)

During a full test suite run (render_tests.suite), the DX12 backend consistently produces 5 InfoQueue errors near the end of the run: two ID3D12Resource objects are deleted before the command list is closed, which triggers a GPU TDR (device hung). This happens during teardown/cleanup, not during rendering. All screenshots and perf artifacts are produced correctly before the crash. **Pre-existing as of commit 8b4967c — not introduced by any recent changes.**

Additional observed output at the end of a DX12 run:

```
D3D12 INFO: Create ID3D12GraphicsCommandList: Addr=0x00000135101AF7E0, ExtRef=1, IntRef=0 [ STATE_CREATION INFO #560: CREATE_COMMANDLIST12]
D3D12 INFO: Create ID3D12GraphicsCommandList: Addr=0x00000135101ACFD0, ExtRef=1, IntRef=0 [ STATE_CREATION INFO #560: CREATE_COMMANDLIST12]
D3D12 INFO: Destroy ID3D12GraphicsCommandList: Name="unnamed", Addr=0x00000135101A57A0 [ STATE_CREATION INFO #586: DESTROY_COMMANDLIST12]
D3D12 INFO: Destroy ID3D12GraphicsCommandList: Name="unnamed", Addr=0x00000135101AA7C0 [ STATE_CREATION INFO #586: DESTROY_COMMANDLIST12]
D3D12 INFO: Create ID3D12GraphicsCommandList: Addr=0x00000135101A57A0, ExtRef=1, IntRef=0 [ STATE_CREATION INFO #560: CREATE_COMMANDLIST12]
D3D12 INFO: Create ID3D12GraphicsCommandList: Addr=0x00000135101B1FF0, ExtRef=1, IntRef=0 [ STATE_CREATION INFO #560: CREATE_COMMANDLIST12]
D3D12 INFO: Destroy ID3D12GraphicsCommandList: Name="unnamed", Addr=0x00000135101AF7E0 [ STATE_CREATION INFO #586: DESTROY_COMMANDLIST12]
D3D12 INFO: Destroy ID3D12GraphicsCommandList: Name="unnamed", Addr=0x00000135101BC030 [ STATE_CREATION INFO #586: DESTROY_COMMANDLIST12]
D3D12 INFO: Destroy ID3D12GraphicsCommandList: Name="unnamed", Addr=0x00000135101ACFD0 [ STATE_CREATION INFO #586: DESTROY_COMMANDLIST12]
```

This interleaved create/destroy sequence suggests command list lifetimes/refcounts are not stable during teardown. Consider verifying that all command lists are closed and GPU work is completed (fence) before releasing resources; also ensure destructors don't recreate or reallocate command lists during cleanup.


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


## Startup: WinRT / COM initialization failure on startup

Occurs on startup. Symptoms observed (stack/trace excerpt):

```
onecore\internal\sdk\inc\wil\opensource/wil\winrt.h(1686)\MSCTF.dll!00007FFC26807CBA: (caller: 00007FFC268128CD) Exception(1) tid(aac) 800401F0 CoInitialize has not been called.
Exception thrown at 0x00007FFC2489044C in SKULLBONEZ_CORE.exe: Microsoft C++ exception: wil::ResultException at memory location 0x00000094119C4BF0.
Exception thrown at 0x00007FFC2489044C in SKULLBONEZ_CORE.exe: Microsoft C++ exception: [rethrow] at memory location 0x0000000000000000.
clientcore\windows\advcore\ctf\shellhandwriting\client\handwritingclient.cpp(328)\MSCTF.dll!00007FFC26812F40: (caller: 00007FFC26801733) LogHr(1) tid(aac) 800401F0 CoInitialize has not been called.
    Msg:[onecore\internal\sdk\inc\wil\opensource/wil\winrt.h(1686)\MSCTF.dll!00007FFC26807CBA: (caller: 00007FFC268128CD) Exception(1) tid(aac) 800401F0 CoInitialize has not been called.
]
```

Repro: Happens immediately on startup (startup path / initialisation). Run under CDB or check logs to reproduce.

Likely cause: A WinRT / Text Services Framework call is made before the thread has called CoInitializeEx / RoInitialize. The MSCTF module indicates a text/input COM activation on startup where the active apartment has not been initialised.

Suggested mitigations:
- Initialise COM/WinRT early (WinMain or SkullbonezRun::Initialise) with an appropriate apartment model (CoInitializeEx / RoInitialize or use wil::init_apartment()).
- Audit any worker threads that may touch OS text/input APIs and ensure they call CoInitializeEx / RoInitialize before such calls.
- Wrap third-party API calls with try/catch and log full hr codes when wil::ResultException is thrown.
- Add defensive logging around any code paths that call WinRT/TSF APIs during startup to capture the offending call site.

Severity: High — occurs on startup and throws wil::ResultException. Status: Untriaged.


## OpenGL: Vertex shader being recompiled on startup [GL MEDIUM] [API] [PERF]

Message observed on startup:

```
Program/shader state performance warning: Vertex shader in program 9 is being recompiled based on GL state.
```

Repro: Appears during GL initialisation / program creation on startup (GL MEDIUM build). Causes a shader recompile at runtime with a measurable perf impact during load.

Probable cause: Program linking or shader usage depends on mutable GL state (attribute bindings, enabled vertex arrays, generic attrib state, or client state) that differs between link-time and use-time, forcing the driver to recompile the vertex shader for the current state.

Suggested mitigations:
- Assign explicit attribute locations in GLSL (layout(location = X)) or call glBindAttribLocation *before* linking so the driver's link-time state is deterministic.
- Ensure GL state used during program link (enabled arrays, vertex attribute formats) matches runtime usage, or pre-link/cache programs for the expected state combinations.
- Enable KHR_debug / capture GL warnings to identify the exact state change triggering the recompile.
- Consider pre-warming or caching compiled programs on a known state to avoid runtime recompiles.

Impact: Medium (performance / startup latency). Status: Untriaged.


## Freeze on scene end: `at_rest.scene` (freeze / shutdown hang)

Occurs when a test scene ends. Repro case:

```
--scene SkullbonezData/scenes/at_rest.scene
```

Symptom: When the scene completes and the program attempts to tear down the scene or exit, the process becomes unresponsive (hung/frozen). The window stops responding and the exe must be killed. Repro: Launch the Debug/Profile exe with the above `--scene` argument or run the scene via the render test suite; hang occurs at scene end/teardown.

Observed behaviour:
- Main thread appears to block during shutdown.
- Background threads (physics/renderer/worker) may be waiting on synchronization primitives or on GL context-related calls.
- No crash or exception — the process simply stops making progress.

Likely causes:
- Deadlock in shutdown ordering (main thread waiting for worker threads that themselves wait on main-thread-only resources such as the GL context).
- Worker thread trying to delete GL resources after the GL context has been destroyed.
- Blocking waits (WaitForSingleObject/Join) with no timeout while a worker is stuck.

Suggested mitigations:
- Ensure deterministic shutdown order: signal workers to stop, let them exit cleanly, then destroy GL context and other single-threaded resources.
- Move GL resource cleanup to the GL/main thread; never call GL functions from non-GL threads during teardown.
- Add timeouts and watchdogs to thread joins and log thread states during shutdown for diagnostics.
- Add verbose shutdown logging and capture thread stacks (via CDB `~*k` or !threads) when hang reproduces.

Severity: High — freeze on scene end blocks automated test runs. Status: Untriaged.

