# SkullbonezCore — Dynamic Allocation Elimination Plan

## Problem

The codebase was written with raw `new`/`delete` everywhere. The goal is to eliminate all
manual dynamic allocation in favour of fixed member arrays, `std::vector`, `std::unique_ptr`,
and static singleton storage.

## Approach

- **Fixed member arrays** for collections whose max size is a compile-time constant
  (TextureCollection: 9 slots, CameraCollection: 3 slots)
- **`std::vector`** for variable-size collections (Terrain posts/heightmap, GameModelCollection)
- **`std::unique_ptr`** for owned single objects where embedding isn't practical (Terrain in SkullbonezRun)
- **Static locals in `Instance()`** for all singletons — no heap object, `Destroy()` just nulls the pointer
- **Embedded value members** for trivially-constructed objects (WorldEnvironment, GameModelCollection in SkullbonezRun)
- **Move semantics** to get GameModel objects into the collection without copying

Each change is built and verified at 0 warnings / 0 errors before moving on.
No commit until the user approves.

---

## Tasks

### 1. `fix-jpeg` — TextureCollection: malloc/new mismatch + rowPtr
- `malloc(sizeof(tImageJPG))` → `new tImageJPG()`
- `free(pImageData)` / `free(pImage)` → `delete`
- `new unsigned char*[sizeY]` rowPtr → `std::vector<unsigned char*>`

### 2. `fix-texture-arrs` — TextureCollection: fixed arrays + static singleton
- `UINT* textureArray` → `UINT textureArray[TOTAL_TEXTURE_COUNT]` member
- `uint32_t* textureHashes` → `uint32_t textureHashes[TOTAL_TEXTURE_COUNT]` member
- Remove constructor parameter and dynamic alloc/dealloc
- `Instance()` uses a static local; `Destroy()` calls `DeleteAllTextures()` then nulls pInstance

### 3. `fix-camera-arrs` — CameraCollection: fixed arrays + static singleton
- `Camera* cameraArray` → `Camera cameraArray[TOTAL_CAMERA_COUNT]` member
- `uint32_t* cameraHashes` → `uint32_t cameraHashes[TOTAL_CAMERA_COUNT]` member
- Remove dynamic alloc/dealloc
- `Instance()` uses a static local; `Destroy()` nulls pInstance

### 4. `fix-terrain` — Terrain: vectors for posts and heightmap
- `TerrainPost* postData` → `std::vector<TerrainPost> postData`
- `BYTE* pTerrainData` → `std::vector<BYTE> terrainData` (member, cleared after build)
- `LoadTerrainData` fills `terrainData` directly, returns void
- Remove all `new`/`delete` in ctor, dtor, and LoadTerrainData

### 5. `fix-gamemodel` — GameModel: unique_ptr for boundingVolume
- `DynamicsObject* boundingVolume` → `std::unique_ptr<DynamicsObject> boundingVolume`
- `AddBoundingSphere` → `make_unique<BoundingSphere>(...)`
- Destructor drops manual `delete boundingVolume`
- GameModel becomes move-constructible (compiler default via unique_ptr)

### 6. `fix-collection` — GameModelCollection: vector of GameModel by value
- `GameModel** gameModelArray` → `std::vector<GameModel> gameModels`
- `AddGameModel(GameModel*)` → `AddGameModel(GameModel)` taking by value (moved in)
- Remove `maxCount` constructor parameter; remove all `new`/`delete` for the array and elements
- All `gameModelArray[x]->` → `gameModels[x].`

### 7. `fix-singletons` — Window + SkyBox: static locals
- `SkullbonezWindow::Instance()` → `static SkullbonezWindow instance; pInstance = &instance`
- `SkyBox::Instance(...)` → `static SkyBox instance(...); pInstance = &instance`
- Both `Destroy()` methods just null pInstance (no delete)

### 8. `fix-run` — SkullbonezRun: eliminate member heap allocations
- `Terrain* cTerrain` → `std::unique_ptr<Terrain> cTerrain` + `make_unique` in Initialise()
- `WorldEnvironment* cWorldEnvironment` → `WorldEnvironment cWorldEnvironment` (embedded value)
- `GameModelCollection* cGameModelCollection` → `GameModelCollection cGameModelCollection` (embedded value)
- Add `WorldEnvironment()` default constructor
- Remove destructor `delete` calls for the above
- Update all `->` to `.` for embedded members; pass `&cWorldEnvironment` where raw pointer needed
- `SetUpGameModels`: stack-construct `GameModel`, move into collection

### 9. `fix-init` — SkullbonezInit: stack-allocate SkullbonezRun
- `SkullbonezRun* cRun = new SkullbonezRun()` → `SkullbonezRun cRun`
- Remove `delete cRun`; update all `cRun->` to `cRun.`

---

## Out of Scope (deferred)
- `gluNewQuadric()` in SkullbonezHelper (called per-frame) — user explicitly deferred
- JPEG pixel data buffer (`new unsigned char[rowSpan * sizeY]`) — runtime image dimensions, must stay heap
