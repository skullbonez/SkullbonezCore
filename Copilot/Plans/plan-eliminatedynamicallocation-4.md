# Plan 4 — Eliminate Dynamic Allocation

## Problem

The codebase used raw `new`/`delete` everywhere: heap-allocated singletons, heap arrays
for fixed-size collections, per-frame heap allocation in physics, and heap-owned objects
that could simply be embedded by value. This creates fragile ownership, exception-safety
risks, and unnecessary runtime cost.

## Approach

- **Fixed member arrays** for compile-time-bounded collections (TextureCollection, CameraCollection)
- **`std::vector`** for variable-size collections (Terrain geometry, GameModelCollection)
- **`std::unique_ptr`** for single owned objects where embedding isn't practical (Terrain in SkullbonezRun)
- **Static locals in `Instance()`** for all singletons — object lives in static storage, `Destroy()` nulls the pointer and resets state
- **Embedded value members** for trivially-constructed objects (WorldEnvironment, GameModelCollection in SkullbonezRun)
- **Move semantics** — explicit `noexcept` move constructor and move assignment on GameModel required because a user-declared destructor suppresses implicit move generation; needed for `std::vector` reallocation

## Tasks

| ID | File(s) | Change |
|----|---------|--------|
| `fix-jpeg` | TextureCollection | `malloc/free` → `new/delete`; rowPtr → `std::vector<unsigned char*>` |
| `fix-texture-arrs` | TextureCollection | Pointer arrays → `UINT[TOTAL_TEXTURE_COUNT]` + `uint32_t[TOTAL_TEXTURE_COUNT]` fixed members; static local singleton |
| `fix-camera-arrs` | CameraCollection | Pointer arrays → `Camera[TOTAL_CAMERA_COUNT]` + `uint32_t[TOTAL_CAMERA_COUNT]` fixed members; static local singleton |
| `fix-terrain` | SkullbonezTerrain | `TerrainPost*` → `std::vector<TerrainPost>`; `BYTE*` → `std::vector<BYTE>` (cleared after build) |
| `fix-gamemodel` | SkullbonezGameModel | `DynamicsObject*` → `std::unique_ptr<DynamicsObject>`; explicit `noexcept` move ctor/assignment |
| `fix-collection` | SkullbonezGameModelCollection | `GameModel**` array → `std::vector<GameModel>`; `AddGameModel` takes by value and moves in |
| `fix-singletons` | SkullbonezWindow, SkullbonezSkyBox | Static local singletons in `Instance()` |
| `fix-run` | SkullbonezRun | `Terrain*` → `std::unique_ptr<Terrain>`; embed `WorldEnvironment` + `GameModelCollection` by value; `SetUpGameModels` stack-constructs and moves each GameModel |
| `fix-init` | SkullbonezInit | `new SkullbonezRun()` → stack variable; remove `delete cRun` |

## Known Issues Fixed During Implementation

### Singleton state not reset on second run
Converting singletons from heap objects to static locals introduced a bug: `Destroy()` only
nulled `pInstance`, leaving the static object's state dirty. On the second simulation loop:

- **CameraCollection**: `arrayPosition` remained 3 → `AddCamera` threw "Camera array full!"
  Fix: `Destroy()` resets the instance via copy-assignment from a default-constructed object before nulling `pInstance`

- **SkyBox**: Sky textures loaded in the constructor (runs once). After `DeleteAllTextures()` cleared
  them, the constructor never re-ran on second use.
  Fix: Extract texture loading into `LoadTextures()`; call it from `Instance()` every time `pInstance` is re-established

## Out of Scope

- `gluNewQuadric()` per-frame in SkullbonezHelper — addressed in Plan 6
- JPEG pixel row buffer (`new unsigned char[rowSpan * sizeY]`) — runtime image dimensions, must stay heap
