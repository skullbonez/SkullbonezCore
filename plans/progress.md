# SkullbonezCore — Progress Log

## Phase 1: Code Quality Fixes (COMPLETE)

| # | Task | Status |
|---|------|--------|
| 1 | `catch(char*)` → `std::runtime_error` everywhere | ✅ Done — commit `9d7672f` |
| 2 | Singleton double-delete (null before delete) | ✅ Done — commit `75102b0` |
| 3 | Resource leaks on exception paths (RAII, unique_ptr) | ✅ Done — commit `acb9263` |
| 4 | `typeid` → `dynamic_cast` for safe type checks | ✅ Done — commit `1577ec6` |
| 5 | Per-frame `new bool[]` leak → `std::vector<bool>` | ✅ Done — commit `114fe3e` |

## Phase 2: Warning Cleanup (COMPLETE)

| Task | Status |
|------|--------|
| `/Gm` deprecated flag removed from vcxproj | ✅ Done |
| `DynamicsObject` missing virtual destructor (C5205) | ✅ Done |
| `fopen` → `fopen_s` (C4996) | ✅ Done |
| `strcpy` → `strcpy_s` (C4996) | ✅ Done |
| `vsprintf` → `vsprintf_s` (C4996) | ✅ Done |
| **Result: 0 warnings, 0 errors** | ✅ Done — commit `684599c` |

## Phase 3: String Lookups → Compile-Time Hashes (COMPLETE)

| Task | Status |
|------|--------|
| `constexpr` FNV-1a `HashStr()` added to SkullbonezCommon.h | ✅ Done |
| 9 named `TEXTURE_*` hash constants | ✅ Done |
| 3 named `CAMERA_*` hash constants | ✅ Done |
| `TextureCollection`: `char**` → `uint32_t[]`, all APIs updated | ✅ Done |
| `CameraCollection`: `char**` → `uint32_t[]`, all APIs updated | ✅ Done |
| All call sites in SkullbonezRun.cpp + SkullbonezSkyBox.cpp updated | ✅ Done |
| **Result: 0 warnings, 0 errors** | ✅ Done — commit `1d2ea94` |

---

## Phase 4: Eliminate Dynamic Allocation (COMPLETE)

See plan.md for full task descriptions.

| ID | Task | Status |
|----|------|--------|
| `fix-jpeg` | TextureCollection: fix malloc/new mismatch + rowPtr vector | ✅ Done |
| `fix-texture-arrs` | TextureCollection: fixed member arrays + static singleton | ✅ Done |
| `fix-camera-arrs` | CameraCollection: fixed member arrays + static singleton | ✅ Done |
| `fix-terrain` | Terrain: vector for posts + heightmap | ✅ Done |
| `fix-gamemodel` | GameModel: `unique_ptr<DynamicsObject>` + explicit move ctor | ✅ Done |
| `fix-collection` | GameModelCollection: `vector<GameModel>` + move | ✅ Done |
| `fix-singletons` | Window + SkyBox: static locals in Instance() | ✅ Done |
| `fix-run` | SkullbonezRun: embed WorldEnv + Collection; `unique_ptr` Terrain | ✅ Done |
| `fix-init` | SkullbonezInit: stack-allocate SkullbonezRun | ✅ Done |

**Result: 0 warnings, 0 errors**

**Deferred:** `gluNewQuadric()` per-frame allocation in SkullbonezHelper
