# Modern C++ Modernization Plan — SkullbonezCore

## Problem Statement

The codebase originated in 2005 and, while phases 1–4 cleaned up the worst legacy patterns (raw `new`/`delete`, `catch(char*)`, non-RAII resources), many subtler old-style idioms remain throughout the source. This plan upgrades the remaining code to idiomatic C++17.

> **Constraint:** Work must not interfere with the active FFP→shader migration (Phases 7–9 in SessionState.md). Each modernization phase should be a clean, buildable commit so the two workstreams can continue in parallel or be merged easily.

---

## Approach

Four self-contained phases, ordered from highest-value / lowest-risk to lowest-value / highest-risk. Every phase ends with a full build + render test so regressions are caught immediately.

---

## Phase M1 — Correctness Keywords (Quick Wins)

**Goal:** Add missing correctness keywords that have zero behavioural risk.

### Tasks

| ID | Task | Files |
|----|------|-------|
| `m1-nullptr` | Replace all `= 0` / `= NULL` on pointer members and static instance pointers with `= nullptr` | `SkullbonezWindow.cpp`, `SkullbonezCameraCollection.cpp`, `SkullbonezTextureCollection.cpp`, `SkullbonezSkyBox.cpp`, `SkullbonezGameModel.cpp`, `SkullbonezFramebuffer.cpp` |
| `m1-override` | Add `override` to every virtual method in `BoundingSphere` that overrides `DynamicsObject` | `SkullbonezBoundingSphere.h` |
| `m1-const-getters` | Mark all non-mutating getter methods `const` | `SkullbonezBoundingSphere.h/.cpp`, `SkullbonezGameModel.h/.cpp`, `SkullbonezRigidBody.h/.cpp`, `SkullbonezCamera.h/.cpp`, `SkullbonezDynamicsObject.h` |
| `m1-nodiscard` | Add `[[nodiscard]]` to all value-returning query functions (getters, math factories, collision tests) | `SkullbonezRigidBody.h`, `SkullbonezGameModel.h`, `SkullbonezBoundingSphere.h`, `SkullbonezGeometricMath.h`, `SkullbonezMatrix4.h`, `SkullbonezVector3.h` |

### Notes
- These are purely additive keyword changes — zero logic is altered.
- `const` on getters may reveal a small number of call sites that incorrectly hold non-const references; fix those too.

---

## Phase M2 — Initialization & Class Layout

**Goal:** Move all member initialization into initializer lists; eliminate custom assignment operators that are just memberwise copies; rationalize `struct` vs `class`.

### Tasks

| ID | Task | Files |
|----|------|-------|
| `m2-init-lists` | Move in-body member initialization (`this->x = 0;` style) to member-initializer lists, or use in-class member initializers for simple defaults | `SkullbonezRigidBody.cpp` (~15 members), `SkullbonezGameModel.cpp`, `SkullbonezTimer.cpp`, `SkullbonezCamera.cpp`, `SkullbonezWindow.cpp` |
| `m2-default-ops` | Replace hand-written memberwise `operator=` implementations with `= default` in `Plane` and `XZBounds` | `SkullbonezGeometricStructures.h` |
| `m2-ray-struct` | Convert `class Ray` (public data, trivial constructors) to `struct Ray`; mark trivial constructors/destructors `= default` | `SkullbonezGeometricStructures.h` |
| `m2-enum-class` | Convert plain `enum TravelDirection` and `enum PointPlaneClassification` to `enum class`; update all use sites | `SkullbonezCamera.h/.cpp`, `SkullbonezGeometricMath.h/.cpp`, `SkullbonezCollisionResponse.cpp` |
| `m2-std-array` | Replace raw `float[16]` / `float[4]` C arrays with `std::array<float,N>` in `Matrix4`, `Helper`, and local stack arrays in `Quaternion` | `SkullbonezMatrix4.h/.cpp`, `SkullbonezHelper.h/.cpp`, `SkullbonezQuaternion.cpp` |

### Notes
- `std::array::data()` is a drop-in for passing to OpenGL functions that expect `float*`.
- Switching `enum` → `enum class` will require updating every use site (the compiler will flag them all).

---

## Phase M3 — Loop & Algorithm Modernization

**Goal:** Replace index-based loops over STL containers with range-based for; replace verbose iterator/index patterns with STL algorithms where they improve clarity.

### Tasks

| ID | Task | Files |
|----|------|-------|
| `m3-range-for-collection` | Convert all `for(int i=0; i<gameModels.size(); ++i)` loops to `for(auto& model : gameModels)` | `SkullbonezGameModelCollection.cpp` (~8 loops) |
| `m3-range-for-terrain` | Convert terrain post/triangle iteration to range-based for where applicable | `SkullbonezTerrain.cpp` |
| `m3-range-for-spatial` | Convert index loops over `std::vector<int> indices` in the spatial grid to range-based for | `SkullbonezSpatialGrid.cpp` |
| `m3-range-for-misc` | Any remaining index loops over STL containers in `SkullbonezRun.cpp`, `SkullbonezHelper.cpp`, `SkullbonezMatrix4.cpp` | As encountered |
| `m3-auto-locals` | Replace verbose local variable declarations where `auto` clearly improves readability (iterator types, cast results, factory return values) | `SkullbonezGameModelCollection.cpp`, `SkullbonezHelper.cpp` |

### Notes
- Do **not** convert loops with non-trivial index arithmetic (e.g., terrain row/col 2D indexing) — those must stay as index loops for clarity.
- Prefer `const auto&` for read-only iteration, `auto&` for mutation.
- `std::for_each` + lambda is an option but prefer range-for for readability unless a genuine STL algorithm (`std::transform`, `std::find_if`, etc.) replaces real logic.

---

## Phase M4 — API Surface & Compile-Time Constants

**Goal:** Modernize the public API with `std::string_view`, replace `#define` numeric constants with `constexpr`, and introduce `std::optional` for functions that may fail.

### Tasks

| ID | Task | Files |
|----|------|-------|
| `m4-string-view` | Replace `const char*` parameters with `std::string_view` on non-variadic public API functions | `SkullbonezShader.h/.cpp` (ctor + `Set*` methods), `SkullbonezTerrain.h/.cpp` (ctor), `SkullbonezWindow.h/.cpp` (`SetTitleText`, `MsgBox`) |
| `m4-constexpr-defines` | Replace `#define _PI`, `#define _2PI`, `#define _HALF_PI`, and all other pure numeric `#define`s in `SkullbonezCommon.h` with `constexpr` values | `SkullbonezCommon.h` |
| `m4-constexpr-fns` | Mark eligible `inline` free functions in `SkullbonezVector3.h` as `constexpr` (those whose bodies are already trivially constexpr-able) | `SkullbonezVector3.h` |
| `m4-optional` | Change `Terrain::LocatePolygon` and `GeometricMath::ComputeIntersectionPoint` to return `std::optional<T>` and update call sites | `SkullbonezTerrain.h/.cpp`, `SkullbonezGeometricMath.h/.cpp`, call sites in `SkullbonezCollisionResponse.cpp` |

### Notes
- `std::string_view` is non-owning — ensure no call site stores the result past the caller's lifetime.
- Variadic functions like `Render2dText(... const char*, ...)` **cannot** take `string_view` (va_args incompatibility) — leave those as `const char*`.
- `std::optional` for `LocatePolygon` is the highest-value change here; the current code has implicit "did this succeed?" checks scattered around call sites.
- `constexpr` on Vector3 operators requires that all member types are also `constexpr`-friendly — verify before committing.

---

## What Is Already Modern (Do Not Touch)

| Area | Status |
|------|--------|
| Smart pointers (`unique_ptr`) | ✅ Complete — all heap objects |
| Move semantics on `GameModel` | ✅ Complete — `= default` move ctor/assign |
| Singletons with `= default` destructors | ✅ Complete |
| FNV-1a `constexpr` hash constants | ✅ Complete |
| `std::vector` for game object collection | ✅ Complete |
| `std::runtime_error` for exceptions | ✅ Complete |
| Zero `catch(char*)` / raw `new`/`delete` | ✅ Cleaned in Phase 1 |

---

## Build Requirement

Every phase must build with **zero warnings** at `/W4`. Run the full build pipeline after each phase before committing:

```
msbuild SKULLBONEZ_CORE.sln /p:Configuration=Debug /p:Platform=Win32
```

Each commit must follow the standard pipeline (reference images + perf artifact) per `Copilot/Skills/skore-build-pipeline/skill.md`.
