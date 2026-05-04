# DirectX 11 Renderer Audit

**Date:** 2026-05-03  
**Files Reviewed:** `SkullbonezRenderBackendDX11.*`, `SkullbonezShaderDX11.*`, `SkullbonezMeshDX11.*`, `SkullbonezFramebufferDX11.*`, HLSL shaders  
**Target API:** DirectX 11 (Feature Level 11_0)

---

## Executive Summary

The DX11 backend is functional and correctly structured, but uses a **legacy swap chain model** (DXGI_SWAP_EFFECT_DISCARD), has **inconsistent HRESULT checking**, performs **runtime shader compilation without optimization flags**, and **recreates state objects on the fly** instead of caching them. The biggest wins come from migrating to flip-model swap chain, adding HRESULT validation, and reducing per-draw overhead.

---

## 1. Architecture & Design

| Aspect | Assessment |
|--------|-----------|
| Interface adherence | ✅ Fully implements `IRenderBackend` |
| Class structure | ✅ Thin wrappers for Shader/Mesh/FBO over D3D11 objects |
| Resource registry | ⚠️ SRV/texture entries stored with 1-based handles, tombstone on delete |
| Backend coupling | ⚠️ `CreateShader` ignores fragment path; maps vert path to combined `.hlsl` file |
| State approach | Pre-created common states + on-the-fly blend state recreation |

---

## 2. Device & Swap Chain

### Current Configuration

```
Driver: D3D_DRIVER_TYPE_HARDWARE
Feature Level: Whatever is returned (no explicit min requirement)
Swap Effect: DXGI_SWAP_EFFECT_DISCARD
Buffer Count: 1
Format: DXGI_FORMAT_R8G8B8A8_UNORM
Debug Layer: Enabled in _DEBUG builds
```

### Issues Found

| Issue | Severity | Location |
|-------|----------|----------|
| **Legacy blit-model swap chain** (`SWAP_EFFECT_DISCARD`) — not recommended since Windows 10 | 🔴 High | `RenderBackendDX11.cpp:136-148` |
| Single back buffer (`BufferCount=1`) — forces synchronous present | Medium | `RenderBackendDX11.cpp:136-148` |
| No explicit feature level validation — silently accepts anything | Low | `RenderBackendDX11.cpp:149-166` |
| No DXGI adapter enumeration — relies on default adapter | Low | `RenderBackendDX11.cpp:130-170` |

### Recommendation

Migrate to **DXGI_SWAP_EFFECT_FLIP_DISCARD** with `BufferCount=2` or `3`. This is the Microsoft-recommended model for all modern Windows applications and enables better compositor integration, lower latency, and HDR support.

---

## 3. State Management

### Pre-Created States (Good)

Rasterizer, depth-stencil, and sampler states are created once in `CreateStateObjects()` — this is correct DX11 practice.

### Issues Found

| Issue | Severity | Location |
|-------|----------|----------|
| **`SetBlendFunc()` recreates blend state every call** instead of caching per-configuration | 🔴 High | `RenderBackendDX11.cpp:490-514` |
| `SetCullFace`/`SetPolygonOffset` switch between pre-created states immediately — no dirty-flag batching | Medium | `RenderBackendDX11.cpp:517-528` |
| No tracking of currently-bound pipeline state — redundant `OMSetBlendState`/`RSSetState` calls | Medium | Entire state management section |

### Recommendation

Pre-create a small set of blend states (the engine only uses ~3 blend configurations) and cache them. Add a state-dirty tracking system to avoid redundant `*Set*` calls.

---

## 4. Resource Management

### Issues Found

| Issue | Severity | Location |
|-------|----------|----------|
| FBO creation lacks rollback on partial failure — leaked resources on error | Medium | `FramebufferDX11.cpp:23-83` |
| `DeleteTexture()` / `UnregisterSRV()` null entries rather than compacting | Low | `RenderBackendDX11.cpp:679-730` |
| `Resize()` releases and recreates without checking success before continuing | Medium | `RenderBackendDX11.cpp:371-418` |

### Recommendation

Add transactional resource creation (create into temps, assign on full success). Use ComPtr for automatic release safety.

---

## 5. Shader Compilation

### Current Approach

- **Runtime compilation** via `D3DCompile` from HLSL source text
- **No precompiled CSO path** — shaders compiled every application launch
- **Optimization level: 0** (no optimization flags set)
- Reflection via `D3DReflect` for constant buffer layout

### Issues Found

| Issue | Severity | Location |
|-------|----------|----------|
| **No shader optimization flags** — `D3DCOMPILE_OPTIMIZATION_LEVEL3` not used | Medium | `ShaderDX11.cpp:58-68, 130-140` |
| No precompiled shader cache (CSO) — startup time penalty | Medium | `ShaderDX11.cpp:42-196` |
| Combined VS+PS in single `.hlsl` file — non-standard tooling interaction | Low | `RenderBackendDX11.cpp:539-551` |

### Recommendation

1. Add `D3DCOMPILE_OPTIMIZATION_LEVEL3` for release builds
2. Implement CSO caching: compile once → serialize blob to disk → load on subsequent runs
3. This eliminates the runtime compilation cost entirely after first run

---

## 6. Performance Issues

### Critical

| Issue | Impact | Location |
|-------|--------|----------|
| **Per-draw constant buffer flush** (`Map(WRITE_DISCARD)` on every mesh draw) | 🔴 High | `MeshDX11.cpp:154-171`, `ShaderDX11.cpp:227-245` |
| **Input layout rebuilt when VS bytecode changes** for dynamic VBs | Medium | `RenderBackendDX11.cpp:857-900, 1006-1064` |
| **`Clear()` calls `OMGetRenderTargets` every invocation** | Medium | `RenderBackendDX11.cpp:433-457` |
| **`CaptureBackbuffer()` allocates staging texture every capture** | Medium | `RenderBackendDX11.cpp:733-778` |

### Recommendation

- Use a ring of 2–3 constant buffers with `NO_OVERWRITE` semantics to avoid implicit GPU stalls from `WRITE_DISCARD` every draw.
- Cache input layouts per (VS bytecode + vertex format) combination.
- Cache the current render target reference in the backend rather than querying OM state.
- Reuse a persistent staging texture for captures.

---

## 7. Best Practice Violations

| Practice | Current State | Recommendation |
|----------|--------------|----------------|
| Flip-model swap chain | ❌ Using legacy DISCARD | Migrate to FLIP_DISCARD with BufferCount≥2 |
| Constant buffer management | ❌ Single CB, `WRITE_DISCARD` per draw | Ring buffer or multiple CBs with `NO_OVERWRITE` |
| Deferred contexts | ❌ Everything immediate-mode | Consider for multi-threaded command recording |
| State caching | ❌ Minimal boolean tracking only | Full pipeline state cache with dirty flags |
| Shader compilation | ❌ Runtime only, no optimization | Precompile CSOs, use optimization level 3 |
| Info queue | ❌ Debug layer on but no message filtering/logging | Add `ID3D11InfoQueue` with severity filtering |
| Input layout cache | ❌ Rebuilt on VS change | Cache per (bytecode hash + format) |
| Staging resources | ❌ Allocated per use | Persistent pool/reuse |

---

## 8. Error Handling

| Aspect | Status |
|--------|--------|
| HRESULT checking on device/swap chain creation | ⚠️ Partial — some paths checked, some not |
| HRESULT checking on state object creation | ❌ Not checked (`CreateStateObjects`) |
| HRESULT checking on Map/Unmap | ❌ Inconsistent |
| HRESULT checking on texture/buffer creation | ❌ Many paths unchecked |
| Debug layer enabled | ✅ In `_DEBUG` builds |
| `ID3D11InfoQueue` filtering/logging | ❌ Not configured |
| Shader compile error reporting | ✅ Error blob thrown as exception |

**This is the most impactful reliability gap.** Failed resource creation could lead to null pointer dereferences or corrupted rendering with no diagnostic.

### Recommendation

Wrap all HRESULT-returning calls in a `ThrowIfFailed()` macro or equivalent. Add `ID3D11InfoQueue` setup in debug builds with `SetBreakOnSeverity(CORRUPTION/ERROR)`.

---

## 9. Memory Management (COM)

| Issue | Severity |
|-------|----------|
| Manual `Release()` calls throughout — no `ComPtr<T>` usage | Medium |
| `Shutdown()` has long manual release chains — fragile if order changes | Medium |
| No null checks before some Release() calls | Low |
| Resize path releases then recreates without transactional safety | Medium |

### Recommendation

Migrate to `Microsoft::WRL::ComPtr<T>` for all COM objects. This eliminates an entire class of leak/double-release bugs and simplifies destruction order.

---

## 10. Present & V-Sync

| Setting | Value | Assessment |
|---------|-------|-----------|
| `Present(1, 0)` | V-Sync ON, 60Hz locked | ⚠️ No option for unlocked/adaptive |
| Swap effect | DISCARD | ❌ Legacy |
| Buffer count | 1 | ❌ Forces synchronous present |

### Recommendation

- Flip-model with `BufferCount=2`, `DXGI_SWAP_EFFECT_FLIP_DISCARD`
- Support `Present(0, DXGI_PRESENT_ALLOW_TEARING)` for unlocked frame rates
- Check `IDXGIOutput::WaitForVBlank()` for proper frame pacing

---

## Prioritized Recommendations

| # | Improvement | Impact | Effort |
|---|-------------|--------|--------|
| 1 | **Add HRESULT checking + `ThrowIfFailed()`** everywhere | Reliability | Low |
| 2 | **Flip-model swap chain** (FLIP_DISCARD, BufferCount=2) | Latency, compatibility | Medium |
| 3 | **Cache blend states** (pre-create the 3 needed configurations) | Performance (CPU) | Low |
| 4 | **Add shader optimization flags** (`D3DCOMPILE_OPTIMIZATION_LEVEL3`) | Performance (GPU) | Low |
| 5 | **CB ring buffer** (eliminate per-draw WRITE_DISCARD) | Performance (CPU+GPU) | Medium |
| 6 | **Input layout cache** | Performance (CPU) | Medium |
| 7 | **Info queue logging** in debug builds | Debuggability | Low |
| 8 | **ComPtr migration** for all COM objects | Reliability, maintainability | Medium |
| 9 | **Precompiled shader cache (CSO)** | Startup time | Medium |
| 10 | **Reuse staging texture** for backbuffer capture | Performance (capture) | Low |

---

## Verdict

The DX11 renderer is **stable and produces correct output** but operates with legacy DXGI patterns and has significant per-draw CPU overhead. The most impactful changes are fixing HRESULT handling (reliability), moving to flip-model (latency + modern compositor support), and reducing constant buffer churn. The shader compilation path would benefit from optimization flags immediately and CSO caching longer-term.
