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

## Phase 4: Eliminate Dynamic Allocation (COMPLETE)

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

---

## FFP → Shader Migration (branch: `opus-rendering`)

### Test Harness (COMPLETE)

| ID | Task | Status |
|----|------|--------|
| `th-init-cmdline` | Parse `--scene` from command line | ✅ Done |
| `th-scene-parser` | Create SkullbonezTestScene class | ✅ Done |
| `th-run-scene-mode` | Add scene mode to SkullbonezRun | ✅ Done |
| `th-vcxproj` | Update vcxproj for TestScene files | ✅ Done |
| `th-physics-roll-scene` | Create physics_roll.scene | ✅ Done |
| `th-water-ball-scene` | Create water_ball_test.scene | ✅ Done |
| `th-render-test-skill` | Create skore-render-test skill | ✅ Done |
| `th-build-verify` | Build and verify test harness | ✅ Done |

### Phase 1 — Foundation: GLAD + Core Context + Matrix4 (COMPLETE)

| ID | Task | Status |
|----|------|--------|
| `p1-glad-files` | Generate GLAD files for OpenGL 3.3 Core | ✅ Done |
| `p1-update-includes` | Update SkullbonezCommon.h includes | ✅ Done |
| `p1-core-context` | Create 3.3 context with wglCreateContextAttribsARB | ✅ Done |
| `p1-matrix4` | Implement SkullbonezMatrix4 class | ✅ Done |
| `p1-vcxproj` | Add new files to vcxproj | ✅ Done |
| `p1-build` | Build verification | ✅ Done |

### Phase 2 — Shader Infrastructure (COMPLETE)

| ID | Task | Status |
|----|------|--------|
| `p2-shader-class` | Implement SkullbonezShader class | ✅ Done |
| `p2-mesh-class` | Implement SkullbonezMesh class | ✅ Done |
| `p2-write-shaders` | Write all GLSL shader programs | ✅ Done |
| `p2-build` | Build verification | ✅ Done |

### Phase 3 — Terrain Migration (COMPLETE)

| ID | Task | Status |
|----|------|--------|
| `p3-texture-upload` | Replace gluBuild2DMipmaps with glTexImage2D+glGenerateMipmap | ✅ Done |
| `p3-terrain-mesh` | Convert terrain to VBO Mesh | ✅ Done |
| `p3-terrain-render` | Wire terrain to lit_textured shader | ✅ Done |
| `p3-terrain-verify` | Visual match verified | ✅ Done |

### Phase 4 — Skybox Migration (COMPLETE)

| ID | Task | Status |
|----|------|--------|
| `p4-skybox-mesh` | Convert skybox to VBO Mesh + unlit shader | ✅ Done |
| `p4-skybox-render` | Wire skybox model matrix in SkullbonezRun | ✅ Done |
| `p4-skybox-verify` | Visual match verified | ✅ Done |

### Phase 5 — Sphere (Game Model) Migration (COMPLETE)

| ID | Task | Status |
|----|------|--------|
| `p5-sphere-gen` | Generate procedural sphere mesh in SkullbonezHelper | ✅ Done |
| `p5-bounding-sphere` | Update BoundingSphere render to use Matrix4 | ✅ Done |
| `p5-gamemodel-orient` | Update GameModel::OrientMesh to use Matrix4 | ✅ Done |
| `p5-quaternion-cleanup` | Remove glMultMatrixf from Quaternion | ✅ Done |
| `p5-sphere-verify` | Visual match verified (300 spheres) | ✅ Done |

### Phase 6 — Water & Shadow Migration (COMPLETE)

| ID | Task | Status |
|----|------|--------|
| `p6-water-mesh` | Convert water to VBO Mesh + unlit_textured shader — commit `4c51895` | ✅ Done |
| `p6-shadow-mesh` | Convert shadows to VBO Mesh + shadow shader — commit `b30fe70` | ✅ Done |
| `p6-water-shadow-verify` | Visual match verified | ✅ Done |

Also added in this session:
- Performance test infrastructure — commit `4f85b6e`
- GL resource lifecycle fix — commit `bbe5921`
- Perf regression thresholds — commit `bbe5921`
- `skore-build-pass` renamed to `skore-build-pipeline` with mandatory baselines + perf artifacts per commit — commit `e708d6c`

---

### Phase 7 — Text Rendering (COMPLETE)

| ID | Task | Status |
|----|------|--------|
| `p7-font-atlas` | Generate font texture atlas | ✅ Done |
| `p7-text-render` | Replace text display lists with shader quads | ✅ Done |
| `p7-text-verify` | Verify text rendering | ✅ Done |

### Phase 8 — FFP Matrix Elimination (COMPLETE)

| ID | Task | Status |
|----|------|--------|
| `p8-camera-matrix` | Replace gluLookAt with Matrix4::LookAt | ✅ Done |
| `p8-projection-matrix` | Replace gluPerspective with Matrix4::Perspective | ✅ Done |
| `p8-run-cleanup` | Remove all FFP matrix calls from SkullbonezRun | ✅ Done |
| `p8-helper-cleanup` | Remove FFP state from SkullbonezHelper | ✅ Done |
| `p8-rigidbody-cleanup` | Remove RotateBodyToOrientation/glMultMatrixf | ✅ Done |
| `p8-ffp-verify` | Verify zero remaining FFP calls | ✅ Done |

### Phase 9 — Core Profile Switch (PENDING)

| ID | Task | Status |
|----|------|--------|
| `p9-core-profile` | Switch to true Core Profile context | ⏳ Pending |
| `p9-remove-glu` | Remove GLU dependency | ⏳ Pending |
| `p9-dead-code` | Remove dead FFP code | ⏳ Pending |
| `p9-final-verify` | Final build + visual verification | ⏳ Pending |

