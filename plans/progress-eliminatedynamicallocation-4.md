# Progress 4 — Eliminate Dynamic Allocation

Commit: `1e2e02f`  
Bug fixes: `2b3bc8d` (CameraCollection reset), `73de94a` (SkyBox texture reload)

## Tasks

| ID | Task | Status |
|----|------|--------|
| `fix-jpeg` | TextureCollection: `malloc/free` → `new/delete`; rowPtr → `std::vector` | ✅ Done |
| `fix-texture-arrs` | TextureCollection: fixed member arrays + static singleton | ✅ Done |
| `fix-camera-arrs` | CameraCollection: fixed member arrays + static singleton | ✅ Done |
| `fix-terrain` | Terrain: `std::vector<TerrainPost>` + `std::vector<BYTE>` | ✅ Done |
| `fix-gamemodel` | GameModel: `unique_ptr<DynamicsObject>` + explicit `noexcept` move ctor | ✅ Done |
| `fix-collection` | GameModelCollection: `std::vector<GameModel>` + move-insert | ✅ Done |
| `fix-singletons` | SkullbonezWindow + SkyBox: static local singletons | ✅ Done |
| `fix-run` | SkullbonezRun: embed WorldEnv + Collection; `unique_ptr<Terrain>` | ✅ Done |
| `fix-init` | SkullbonezInit: stack-allocate SkullbonezRun | ✅ Done |

## Bug Fixes

| Issue | Fix | Commit |
|-------|-----|--------|
| CameraCollection `arrayPosition` not reset → "Camera array full!" on second run | `Destroy()` copy-assigns a default-constructed instance before nulling `pInstance` | `2b3bc8d` |
| SkyBox sky textures not reloaded after GL context recreate → "Texture does not exist" | Extracted `LoadTextures()`; called from `Instance()` on every re-init | `73de94a` |

**Final result: 0 warnings, 0 errors**
