# OpenGL 3.3 Renderer Audit

**Date:** 2026-05-03  
**Files Reviewed:** `SkullbonezRenderBackendGL.*`, `SkullbonezShaderGL.*`, `SkullbonezMeshGL.*`, `SkullbonezFramebufferGL.*`, `SkullbonezWindow.cpp`, GLSL shaders, GLAD loader  
**Target API:** OpenGL 3.3 Core Profile

---

## Executive Summary

The GL backend is a clean, functional OpenGL 3.3 Core Profile implementation. Context creation is correct, the GLAD loader is properly integrated, and the abstraction layer is thin. However, it has **no runtime error reporting**, **no state deduplication**, **per-call uniform lookups**, and uses **synchronous GPU stalls** (`glFinish`) in the hot path. These are the primary performance and reliability gaps.

---

## 1. Architecture & Design

| Aspect | Assessment |
|--------|-----------|
| Interface adherence | ✅ Fully implements `IRenderBackend` |
| Class structure | ✅ Clean RAII wrappers (`MeshGL`, `ShaderGL`, `FramebufferGL`) |
| Resource registry | ⚠️ Dynamic VBs/instanced meshes stored as 1-based index vectors with tombstones — never compacted |
| Separation of concerns | ✅ Backend owns state; resources are self-contained |

---

## 2. OpenGL Version & Context

- **Target:** OpenGL 3.3 Core Profile — explicitly validated via `GL_CONTEXT_PROFILE_MASK`
- **Context creation:** Legacy temp context → `wglCreateContextAttribsARB` → 3.3 core attribs (`SkullbonezWindow.cpp:266-305`)
- **GLAD:** Loaded after context creation; failure is handled with `throw`
- **Assessment:** ✅ Correctly done. No compatibility profile fallback risk.

---

## 3. State Management

### Issues Found

| Issue | Severity | Location |
|-------|----------|----------|
| No state-change deduplication — every `SetDepthTest`/`SetBlend`/`SetCullFace` calls GL even if unchanged | Medium | `RenderBackendGL.cpp:110-206` |
| Only `m_depthTestEnabled` and `m_blendEnabled` are tracked; cull/polygon-offset/clip-plane state is untracked | Low | `RenderBackendGL.h:44-52` |
| `SetCullFace(true)` enables culling but never sets winding or face mode | Low | `RenderBackendGL.cpp:166-176` |

### Recommendation

Add a full state shadow struct and skip GL calls when the requested state matches current state. This eliminates redundant driver overhead across draw calls.

---

## 4. Resource Management

### Issues Found

| Issue | Severity | Location |
|-------|----------|----------|
| `Shutdown()` does not destroy dynamic VB / instanced mesh GL objects | High | `RenderBackendGL.cpp:39-43` |
| Deleted resources leave tombstones in vectors — never compacted | Low | `RenderBackendGL.cpp:370-507` |
| `MeshGL` destructor assumes live GL context (safe due to lifecycle enforcement, but brittle) | Low | `MeshGL.cpp:67-77` |

### Recommendation

Implement explicit resource cleanup in `Shutdown()`. Consider a free-list for handle reuse.

---

## 5. Shader System

### Issues Found

| Issue | Severity | Location |
|-------|----------|----------|
| **`glGetUniformLocation` called per-frame for every uniform set** — no caching | 🔴 High | `ShaderGL.cpp:112-145` |
| No validation of `glGetUniformLocation == -1` (silent no-op on typos) | Medium | `ShaderGL.cpp:112-145` |
| Shader source loaded via `new[]` — not using `std::vector<char>` or RAII | Low | `ShaderGL.cpp:9-30` |

### Recommendation

Cache all uniform locations in an `unordered_map<const char*, GLint>` at link time (or use layout qualifiers with explicit locations in GLSL 3.3+). This alone could save measurable CPU time per frame.

---

## 6. Performance Issues

### Critical

| Issue | Impact | Location |
|-------|--------|----------|
| **`glFinish()` in `Finish()`, `FlushGPU()`, and `CaptureBackbuffer()`** — hard GPU pipeline stalls | 🔴 High | `RenderBackendGL.cpp:51-60, 291-294` |
| **`UploadAndDrawDynamicVB()` calls `glBufferData` every frame** — full buffer reallocation | 🔴 High | `RenderBackendGL.cpp:375-389` |
| Repeated `glBindVertexArray(0)` / `glBindBuffer(0)` after every operation | Medium | `MeshGL.cpp:61-64, 80-93` |

### Recommendation

- Replace `glFinish` with fence sync objects (`glFenceSync` / `glClientWaitSync`) where needed.
- Use buffer orphaning (`glBufferData(NULL)` + `glBufferSubData`) or persistent mapped buffers (`GL_MAP_PERSISTENT_BIT | GL_MAP_COHERENT_BIT`) for streaming data.
- Remove unnecessary unbind-to-zero calls — Core Profile doesn't require them.

---

## 7. Best Practice Violations

| Practice | Current State | Recommendation |
|----------|--------------|----------------|
| Direct State Access (DSA) | ❌ Not used | Use `glCreate*` / `glNamed*` variants (available via GL 4.5 or `ARB_direct_state_access`) |
| Uniform Buffer Objects | ❌ All uniforms set individually | Batch view/projection into a UBO; reduces API call overhead |
| Sampler Objects | ❌ Sampler state set on textures directly | Use `glGenSamplers` / `glBindSampler` for decoupled filtering |
| Debug Callbacks | ❌ No `KHR_debug` / `glDebugMessageCallback` | Register debug callback in debug builds for real-time GL error reporting |
| Buffer Streaming | ❌ Full reallocation per frame | Use orphan + subdata, or persistent mapping |
| Bindless Textures | ❌ Not used | Low priority — current texture count is small |

---

## 8. Error Handling

| Aspect | Status |
|--------|--------|
| `glGetError()` checks | ❌ None found anywhere |
| `KHR_debug` callback | ❌ Not registered |
| Shader compile errors | ✅ Info log retrieved and thrown |
| FBO completeness check | ✅ `glCheckFramebufferStatus` present |
| Context creation failure | ✅ Handled with exceptions |

**This is the single biggest reliability gap.** GL errors are entirely silent. A misconfigured state or failed texture upload produces no diagnostic output.

### Recommendation

Register `glDebugMessageCallback` immediately after GLAD loads. Filter by severity in release builds. This one change would have prevented multiple debugging sessions during the shader migration.

---

## 9. Memory Management

| Issue | Severity |
|-------|----------|
| Dynamic VB/instanced mesh vectors grow forever (tombstones) | Low |
| `Shutdown()` doesn't free all GL objects | High |
| `ShaderGL::LoadShaderSource` uses raw `new[]` instead of RAII container | Low |

---

## 10. Screenshot/Backbuffer Capture

Current: `glReadPixels` with `glFinish()` before — fully synchronous, stalls GPU.

Better approach: Use a PBO (Pixel Buffer Object) for async readback:
1. `glReadPixels` into PBO (non-blocking)
2. Map PBO on the *next* frame after GPU completes
3. Eliminates the `glFinish` stall

---

## Prioritized Recommendations

| # | Improvement | Impact | Effort |
|---|-------------|--------|--------|
| 1 | **Register `KHR_debug` callback** | Reliability | Low |
| 2 | **Cache uniform locations** at shader link time | Performance (CPU) | Low |
| 3 | **Remove `glFinish()` from non-capture paths** | Performance (GPU) | Low |
| 4 | **Buffer orphaning / persistent mapping for dynamic VBs** | Performance (CPU+GPU) | Medium |
| 5 | **State deduplication** (shadow all GL state, skip redundant calls) | Performance (CPU) | Medium |
| 6 | **Implement `Shutdown()` resource cleanup** | Correctness | Low |
| 7 | **PBO-based async screenshot** | Performance (frame time) | Medium |
| 8 | **Migrate to DSA APIs** (requires GL 4.5 or extension check) | Modernization | High |
| 9 | **UBO for camera/lighting uniforms** | Performance (CPU) | Medium |
| 10 | **Remove bind-to-zero patterns** | Performance (minor CPU) | Low |

---

## Verdict

The OpenGL renderer is **functionally correct and stable** but leaves significant CPU performance on the table due to per-call uniform lookups, redundant state changes, and synchronous GPU stalls. The complete absence of runtime error reporting makes debugging harder than necessary. Implementing recommendations 1–5 would bring it to modern best-practice standards with moderate effort.
