# DirectX 12 Renderer Audit

**Date:** 2026-05-03  
**Files Reviewed:** `SkullbonezRenderBackendDX12.*`, `SkullbonezShaderDX12.*`, `SkullbonezMeshDX12.*`, `SkullbonezFramebufferDX12.*`, HLSL shaders  
**Target API:** DirectX 12 (Feature Level 11_0, Shader Model 5.0)

---

## Executive Summary

The DX12 backend is a functional implementation that correctly uses flip-model presentation, PSO caching, and descriptor heaps. However, it has a **critical synchronization bug** in its command allocator management, uses a **single command list with no frame-based allocator ring**, and relies on **GPU stalls to recover from upload buffer exhaustion**. The architecture is "DX12 shaped" but misses several fundamentals that DX12 demands for safe and performant operation.

---

## 1. Architecture & Design

| Aspect | Assessment |
|--------|-----------|
| Interface adherence | ✅ Fully implements `IRenderBackend` |
| Complexity management | ⚠️ Single command list / single allocator — insufficient for safe multi-frame operation |
| PSO caching | ✅ Hash-based deduplication of pipeline state objects |
| Resource model | ⚠️ All committed resources — no placed/suballocated heaps |
| Descriptor management | ⚠️ Linear allocation without robust overflow handling |

---

## 2. Device & Swap Chain

### Current Configuration

```
Feature Level: D3D_FEATURE_LEVEL_11_0
DXGI Factory: Created with debug flag when available
Swap Effect: DXGI_SWAP_EFFECT_FLIP_DISCARD ✅
Buffer Count: 2 (double-buffered)
Format: DXGI_FORMAT_R8G8B8A8_UNORM
Frame Count: 2 (FRAME_COUNT constant)
```

### Assessment

✅ **Correct use of flip-model** — this is the recommended swap chain model for DX12.

| Issue | Severity | Location |
|-------|----------|----------|
| Feature level 11_0 is conservative (could target 12_0 for DX12-specific features) | Low | `RenderBackendDX12.cpp:222-226` |
| No explicit adapter enumeration/selection | Low | Init path |

---

## 3. Command Queue & Command Lists

### Current Design

- Single **direct** command queue
- Single command allocator
- Single graphics command list
- `EnsureCommandListOpen()` resets allocator and reuses the list

### 🚨 CRITICAL BUG: Allocator Reuse Without Fence Wait

| Issue | Severity | Location |
|-------|----------|----------|
| **`Present()` signals the fence but does NOT wait for GPU completion** | 🔴 Critical | `RenderBackendDX12.cpp:620-635` |
| **`EnsureCommandListOpen()` may reset the allocator while GPU is still executing** | 🔴 Critical | `RenderBackendDX12.cpp:58-81` |

**Explanation:** In DX12, you **cannot** reset a command allocator until the GPU has finished executing all command lists recorded with that allocator. The current code signals a fence at Present but doesn't wait for it before resetting. Under light workloads this may not manifest, but under load (or with validation layer) this is undefined behavior.

### Recommendation

Implement a **per-frame allocator ring**:
```
Frame 0: Allocator A → record → execute → signal fence 0
Frame 1: Allocator B → record → execute → signal fence 1
Frame 0: Wait for fence 0 → reset Allocator A → record...
```

This is fundamental DX12 architecture. Without it, the renderer is technically unsafe.

---

## 4. Descriptor Heap Management

### Current Design

- Two SRV heaps: shader-visible (GPU) + CPU staging
- Static SRVs: linear allocator (never freed)
- Transient SRVs: linear allocator, frame-local-ish

### Issues Found

| Issue | Severity | Location |
|-------|----------|----------|
| Transient SRV heap can exhaust within a single frame if many textures are bound | Medium | `RenderBackendDX12.h:154-162` |
| No explicit per-frame reset of transient descriptor range | Medium | Transient allocation path |
| Descriptor copy happens per texture bind — extra CPU cost | Low | `RenderBackendDX12.cpp:1103-1115` |

### Recommendation

- Make transient SRV allocation explicitly frame-scoped with reset at frame start
- Consider a ring-buffer descriptor allocator sized to `FRAME_COUNT * max_descriptors_per_frame`
- Batch descriptor copies where possible

---

## 5. Root Signature & PSO

### Current Design

```
Root Parameter 0: CBV at b0 (constant buffer — per-draw)
Root Parameter 1: Descriptor table — 1 SRV at t0
Root Parameter 2: Descriptor table — 1 SRV at t1
Static Samplers: 2 (linear wrap + linear clamp)
```

### PSO Cache

- Keyed on: shader pointers + format + state bits (blend, depth, cull, polygon offset)
- In-memory only — no disk serialization
- `m_psoDirty` flag exists but hash drives actual rebuilds

### Issues Found

| Issue | Severity | Location |
|-------|----------|----------|
| PSO cache is in-memory only — recompiled every application start | Medium | `RenderBackendDX12.cpp:959-1026` |
| Cache key doesn't include render target format variability (assumes constant) | Low | `RenderBackendDX12.cpp:834-852` |
| `m_psoDirty` flag is partially redundant with hash-based rebuild | Low | PSO management section |

### Recommendation

- Serialize PSO cache blobs to disk (`ID3D12PipelineState::GetCachedBlob`)
- Validate format assumptions or include RT format in cache key

---

## 6. Resource Management

### Current Approach

- **All committed resources** (no placed/suballocated)
- Single 8 MB upload heap with mapped pointer
- Barriers are inline transition barriers

### Issues Found

| Issue | Severity | Location |
|-------|----------|----------|
| **No explicit resource state tracking** — code assumes transitions happen in correct order | 🔴 High | Throughout |
| Upload heap is fixed 8 MB — exhaustion triggers full GPU flush | Medium | `RenderBackendDX12.h:125-129`, `cpp:100-112` |
| All resources use committed allocation — no memory pooling | Medium | All creation paths |
| FBO color textures created in PIXEL_SHADER_RESOURCE then transitioned — works but brittle | Low | `FramebufferDX12.cpp:39-98` |

### Recommendation

- Add a resource state tracker (per-resource current state, validate transitions, batch barriers)
- Implement per-frame upload buffer slices instead of single buffer with flush-on-exhaustion
- Consider placed resources with a memory allocator (D3D12MA or custom) for better memory reuse

---

## 7. Synchronization

### Current Design

- Single fence object
- `WaitForGpu()` — full CPU wait on fence
- `Present()` signals fence but doesn't wait
- `FlushUploadBuffer()` — full GPU stall when upload heap exhausts

### Issues Found

| Issue | Severity | Location |
|-------|----------|----------|
| **Present signals without wait → allocator reuse hazard** (see §3) | 🔴 Critical | `RenderBackendDX12.cpp:620-635` |
| `FlushUploadBuffer()` is a full GPU stall — creates CPU/GPU bubble | Medium | `RenderBackendDX12.cpp:100-112` |
| `Resize()` correctly waits before releasing — good | ✅ | `RenderBackendDX12.cpp:664-698` |
| `CaptureBackbuffer()` fully stalls GPU | Medium | `RenderBackendDX12.cpp:1421-1503` |

### Recommendation

- Fix the critical synchronization bug (per-frame fences + allocator ring)
- Replace upload flush with per-frame upload ring (each frame owns a slice, wait only for N-2 frame)
- Use async readback for screenshot capture

---

## 8. Shader Compilation

### Current Approach

- Runtime `D3DCompile` from HLSL source text
- Shader Model 5.0 (`vs_5_0`, `ps_5_0`)
- `D3DReflect` on VS only for cbuffer layout

### Issues Found

| Issue | Severity | Location |
|-------|----------|----------|
| Runtime compilation on every launch — no cached bytecode | Medium | `ShaderDX12.cpp:39-95` |
| Shader Model 5.0 — could use 5.1 or 6.x with DXC for better features | Low | `ShaderDX12.cpp:58, 76` |
| No DXC (DirectX Shader Compiler) usage — legacy `D3DCompile` | Low | `ShaderDX12.cpp:39-95` |

### Recommendation

1. Cache compiled shader bytecode to disk (hash source → blob file)
2. Consider DXC for SM 6.0+ features (wave intrinsics, etc.) — not urgent for current workload
3. Use optimization level 3 for release builds

---

## 9. Performance Issues

### Critical Path Analysis

| Issue | Impact | Location |
|-------|--------|----------|
| **`WaitForGpu()` on upload exhaustion** — full pipeline stall | 🔴 High | `RenderBackendDX12.cpp:100-112` |
| **Descriptor copy per texture bind** — CPU overhead scales with bind count | Medium | `RenderBackendDX12.cpp:1103-1115` |
| **Single command list** — no parallelism or multi-thread recording | Medium | Entire backend |
| **`CaptureBackbuffer()` allocates readback buffer per call** | Medium | `RenderBackendDX12.cpp:1421-1503` |
| No command list bundling for repeated draw patterns | Low | Draw paths |

### Recommendation

- Per-frame upload ring eliminates the stall
- Bindless descriptor table (all SRVs in one heap, index via root constant) eliminates per-bind copies
- Multi-threaded command recording not needed at current scale but would help at higher object counts

---

## 10. Best Practice Violations

| Practice | Current State | Recommendation |
|----------|--------------|----------------|
| Per-frame allocator ring | ❌ Single allocator, unsafe reuse | Implement FRAME_COUNT allocators + fences |
| Resource state tracking | ❌ Assumed/implicit | Add per-resource state tracker |
| Placed resources / suballocation | ❌ All committed | Use D3D12MA or manual placed heaps |
| Upload ring buffer | ❌ Single buffer + flush stall | Per-frame upload slices |
| PSO disk cache | ❌ In-memory only | Serialize via `GetCachedBlob` |
| Split barriers | ❌ Not used | Add where possible for GPU overlap |
| Bundles | ❌ Not used | Low priority at current draw count |
| Enhanced barriers | ❌ Not used | Low priority — requires Win11 |
| Residency management | ❌ Not implemented | Only needed under memory pressure |
| GPU-driven rendering | ❌ Not used | Not needed at current scale |
| DRED (Device Removed Extended Data) | ❌ Not enabled | Enable in debug builds for crash diagnostics |

---

## 11. Error Handling & Validation

| Aspect | Status |
|--------|--------|
| Debug layer enabled | ✅ When available |
| Info queue — error log on shutdown | ✅ Queried and logged |
| Info queue — break on error | ❌ Not configured |
| GPU-based validation | ❌ Not enabled |
| DRED | ❌ Not enabled |
| Message filtering | ⚠️ Only one warning filtered |
| HRESULT validation | ⚠️ Inconsistent — some paths check, some don't |

### Recommendation

In debug builds:
1. Enable `ID3D12InfoQueue::SetBreakOnSeverity(CORRUPTION)` and `(ERROR)`
2. Enable GPU-based validation for thorough barrier/state checking
3. Enable DRED for post-mortem crash analysis
4. Add `ThrowIfFailed()` wrapper for all HRESULT-returning calls

---

## 12. Memory Management

| Aspect | Current | Recommendation |
|--------|---------|----------------|
| Upload heap | Fixed 8 MB | Size based on workload; per-frame ring |
| Static SRV slots | Hard-capped | Fine for current texture count |
| Transient SRV slots | Hard-capped | Add overflow detection / warning |
| Buffer allocation | Committed (per-buffer heap) | Placed resources in shared heaps |
| Texture allocation | Committed | Placed resources for better utilization |
| Memory budget | Not tracked | Query `IDXGIAdapter3::QueryVideoMemoryInfo` |

---

## Prioritized Recommendations

| # | Improvement | Impact | Effort |
|---|-------------|--------|--------|
| 1 | 🚨 **Fix allocator/fence synchronization** (per-frame allocator ring) | Correctness | Medium |
| 2 | **Per-frame upload ring** (eliminate GPU stalls on exhaustion) | Performance | Medium |
| 3 | **Add resource state tracking** | Correctness/Safety | Medium |
| 4 | **Frame-scoped transient descriptor reset** | Correctness | Low |
| 5 | **HRESULT validation + ThrowIfFailed** | Reliability | Low |
| 6 | **Enable DRED + GPU-based validation** in debug | Debuggability | Low |
| 7 | **PSO disk cache** (`GetCachedBlob` serialization) | Startup time | Medium |
| 8 | **Shader bytecode caching** | Startup time | Medium |
| 9 | **Reuse readback buffer** for captures | Performance (capture) | Low |
| 10 | **Placed resources / suballocation** | Memory efficiency | High |

---

## Critical Bug Summary

### 🚨 Command Allocator Reuse Race Condition

**Severity:** Critical — undefined behavior, potential GPU crash  
**Location:** `RenderBackendDX12.cpp:620-635` (Present) and `58-81` (EnsureCommandListOpen)  
**Trigger:** Under heavy GPU load where frame N's GPU work hasn't completed before frame N+1 attempts to reset the allocator  
**Fix:** Implement double/triple-buffered command allocators with per-frame fence tracking:

```cpp
// Pseudocode
void Present() {
    m_fenceValues[m_frameIndex] = ++m_currentFence;
    m_commandQueue->Signal(m_fence, m_fenceValues[m_frameIndex]);
    m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();
    
    // Wait for the frame we're about to reuse
    if (m_fence->GetCompletedValue() < m_fenceValues[m_frameIndex]) {
        m_fence->SetEventOnCompletion(m_fenceValues[m_frameIndex], m_fenceEvent);
        WaitForSingleObject(m_fenceEvent, INFINITE);
    }
    
    m_allocators[m_frameIndex]->Reset();  // Now safe
}
```

---

## Verdict

The DX12 renderer **works in practice** due to the relatively light workload (GPU finishes before the next frame most of the time), but it has a **fundamental synchronization bug** that constitutes undefined behavior under the DX12 spec. Beyond that critical fix, the main improvements are proper frame-based resource management (allocator ring, upload ring, descriptor reset) which would bring it in line with DX12 best practices. The PSO caching and flip-model swap chain are already correctly implemented — those are positives that the DX11 backend lacks.
