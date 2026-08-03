# Source Modernization MZ0 — Current-Tree Census

Date: 2026-08-03
Branch: `nightrunner-3rd-AUG-26`
Plan phase: MZ0 of `Agentic/Plans/TODO/source-modernization-sweep.md`
Impact: documentation only

## Result

MZ0 remeasured the complete current first-party C++ tree after the preceding
coverage and convention plans landed. The plan's earlier cast total is stale:
there are 136 syntax-confirmed C-style or functional-style casts across 15
implementation files, not 44 across 10. This is current measurement, not a
budget. Every cast is an explicit numeric, enum, size, address, or `void*`
conversion and is classified `retire` to the equivalent narrow named cast.

The two exact `NULL` tokens and two internally defined object-like constants are
also `retire`. No cast, `NULL`, or constant occurrence is load-bearing on C-style
semantics and none needs a retain or owner ruling.

## C-Style Cast Census

The complete syntax-aware `google-readability-casting` pass parsed all 260
tracked `.cpp` files and produced these exact rows. Repeated line numbers carry
multiple casts at the recorded columns.

| File | Count | Exact line:columns and target types | Class |
|---|---:|---|---|
| `Physics/SpatialGrid.cpp` | 18 | 138:14/45/76 `int64_t`; 591:28/52, 592:28/52, 593:28/52 `int64_t`; 993:32/50, 994:32/50, 995:32/50 `int64_t`; 1169:56, 1170:56, 1171:56 `int64_t` | retire |
| `Physics/Stages/PhysicsNarrowphaseStage.cpp` | 3 | 332:32/63/94 `int64_t` | retire |
| `Rendering/DX12/BLASDX12.cpp` | 2 | 72:53 `UINT64`; 74:38 `UINT` | retire |
| `Rendering/DX12/FramebufferDX12.cpp` | 4 | 86:23 `UINT64`; 87:24 `UINT`; 132:23 `UINT64`; 133:24 `UINT` | retire |
| `Rendering/DX12/MeshDX12.cpp` | 5 | 67:32 `int`; 70:23 `UINT64`; 110:30 `size_t`; 151:28 `UINT`; 152:30 `UINT` | retire |
| `Rendering/DX12/RenderBackendDX12.DXR.cpp` | 11 | 250:36, 251:32 `size_t`; 356:21 `UINT64`; 357:22 `UINT`; 529:40, 540:66 `D3D12_GPU_VIRTUAL_ADDRESS`; 740:60 `size_t`; 764:27, 873:91, 892:26, 893:27 `UINT` | retire |
| `Rendering/DX12/RenderBackendDX12.DynamicGeometry.cpp` | 24 | 290:32 `int`; 292:12, 302:34 `uint32_t`; 326:51 `size_t`; 343:23, 344:25, 355:33, 475:26, 483:33 `UINT`; 641:47, 643:42 `int`; 662:23 `UINT64`; 713:44 `size_t`; 730:32, 731:34 `UINT`; 734:12, 742:34 `uint32_t`; 761:52 `size_t`; 764:27 `UINT`; 772:44 `uint32_t`; 794:29, 808:33/63 `UINT`; 815:34 `uint32_t` | retire |
| `Rendering/DX12/RenderBackendDX12.Pipeline.cpp` | 17 | 103:21–112:21 and 114:21 `size_t`; 184:53, 191:29, 222:36, 229:23, 283:19 `UINT`; 325:26 `size_t` | retire |
| `Rendering/DX12/RenderBackendDX12.Resources.cpp` | 1 | 123:23 `UINT64` | retire |
| `Rendering/DX12/RenderBackendDX12.Textures.cpp` | 1 | 592:22 `size_t` | retire |
| `Rendering/DX12/RenderBackendDX12.cpp` | 22 | 450:79, 460:32 `UINT`; 574:48/62 `float`; 575:42/55 `LONG`; 699:20 `const char*`; 949:29 `D3D12_MESSAGE*`; 961:67 `int`; 1407:93 `UINT`; 1465:43/57 `float`, 1465:94/107 `LONG`; 1484:31/41/51/61 `float`; 1485:31/40/49/66 `LONG` | retire |
| `Rendering/DX12/SBTDX12.cpp` | 5 | 177:24, 180:48, 183:44, 186:54, 188:72 `size_t` | retire |
| `Rendering/DX12/TLASDX12.cpp` | 4 | 73:27 `UINT64`; 105:23 `UINT`; 192:44 `size_t`; 201:23 `UINT` | retire |
| `Rendering/Text.cpp` | 9 | 156:40/80, 157:19, 163:38/78/101, 178:36, 184:16 `float`; 759:27 `unsigned char` | retire |
| `Runtime/Debug/BroadphaseVisualizer.cpp` | 10 | 79:14/45/76 `int64_t`; 182:36/64, 199:36/64, 220:16, 221:16, 222:16 `float` | retire |
| **Total** | **136** | 15 files | **136 retire** |

The 21 Physics/Runtime Debug rows are hot-path adjacent and require the mapped
Physics and performance gates. The 96 DX12 rows require full/render coverage.
The 19 remaining rows are in Text and general Rendering support. Conversions to
Win32/D3D typedefs are platform-boundary values, but named `static_cast` retains
the same conversion and makes the narrowing visible; none requires C syntax.

## `NULL` And Object-Like Constant Census

| File:line | Occurrence | Class and MZ1 action |
|---|---|---|
| `Rendering/Text.cpp:306` | `NULL` in the `CreateCompatibleDC` explanation | retire; spell `nullptr` so the explanation matches the call |
| `Rendering/Text.cpp:309` | `CreateCompatibleDC( NULL )` | retire to `nullptr` |
| `Maths/MathsCommon.h:42` | release/profile `#define SKULLBONEZ_INTRINSICS 1` | retire to a header-owned `inline constexpr bool` selected by the existing build condition |
| `Maths/MathsCommon.h:44` | Debug `#define SKULLBONEZ_INTRINSICS 0` | retire to the same constexpr owner |

`RotationMatrix.h` is the only consumer of the intrinsics switch. MZ1 must keep
the existing Debug scalar and non-Debug SSE selection while replacing its two
`#if SKULLBONEZ_INTRINSICS` sites with the direct build predicate. No project,
platform, COM, or external define currently overrides the switch.

## Hungarian Parameter Census

The parameter inventory is recorded by semantic operation and all physical
declaration/definition sites. Type prefixes are distinguished from legitimate
axis/domain names such as `xMove`, `rA`, and `bIndex`; those names are not part
of this modernization signal.

The current tree contains 89 first-party semantic parameters in the reconciled
prefix-shaped inventory: 82 are rename-owned across 27 files and seven
`wParam`/`lParam` slots are explicit Win32-boundary retains. The syntax-aware
physical inventory contains 160 declaration/definition sites; three counterparts
already use different names (`deltaSeconds` twice and `m_cTerrain` once). The
earlier 58 count omitted Assets, Ray, complete Text/Camera, Runtime App, and
`Terrain.h` identities. The complete operation map is:

| Owner / physical sites | Semantic count | Exact old → new map | Class |
|---|---:|---|---|
| `Assets/TextureCollection.{h,cpp}:102-103,335,365` | 2 | `CreateJpegTexture/EnsureJpegTexture cFileName` → `fileName` | retire |
| `Maths/GeometricStructures.h:89` | 2 | `Ray vOrigin/vVector3` → `origin/vector` | retire |
| `Maths/Quaternion.{h,cpp}:52,42` | 4 | `fX/fY/fZ/fW` → `x/y/z/w` | retire |
| `Maths/RotationMatrix.h:79-80` | 3 | `fRadians/vAxis/vPoint` → `radians/axis/point` | retire |
| `Maths/Vector3.h:70` | 3 | `fX/fY/fZ` → `x/y/z` | retire |
| `Physics/BoundingSphere.{h,cpp}:69-71,38,224` | 4 | constructor `fRadius/vPosition/fDragCoefficient` → `radius/localPosition/dragCoefficient`; setter `fDragCoefficient` → `dragCoefficient` | retire |
| `Physics/PhysicsEngine.cpp:876,883` | 2 | both `Step fChangeInTime` definitions → `deltaSeconds`, matching the existing header | retire |
| `Physics/PhysicsWorld.{h,cpp}:191,201,602,1311` | 2 | `RunPhysics/EmitStepDiagnostics fChangeInTime` → `deltaSeconds` | retire |
| `Physics/SpatialGrid.{h,cpp}:299,315,143,183` | 2 | constructor/setter `fCellSize` → `cellSize` | retire |
| `Rendering/Text.{h,cpp}:105-106,124,128-129,141,144,287,539,653,736,872-873` | 7 | `fSize` → `size`; `cRawText` → `format`; `cFontName` → `fontName`; `cOutPath` → `outputPath` | retire |
| `Runtime/Camera/Camera.{h,cpp}:100-102,51,85` | 5 | `SetAll vPosition/vView/vUpVector` → `position/view/up`; `MoveCamera enumDir/fQuantity` → `direction/amount` | retire |
| `Runtime/Camera/CameraCollection.{h,cpp}:64,86-87,95,97,109,114-115,118-119,85,92,115,174,264,270,318,451,510` | 12 | `cCameraData` → `camera`; `vView` → `view`; `vPos/vPosition` → `position`; `vUp` → `up`; `fTweenSpeed` → `tweenSpeed`; `fIsLocked` → `isLocked`; `cTerrain/m_cTerrain` → `terrain`; `enumDir` → `direction`; `fQuantity` → `amount`; `fTween` → `tween` | retire |
| `Runtime/App/Window.{h,cpp}:109,127,130-131,245,423,541,547`; `Runtime/App/Init.cpp:305,359` | 12 | `cText` → `text`; `hInstance` → `instance`; `cMsgBoxText/cMsgBoxTitle/iMsgBoxType` → `text/title/type`; `CleanupWindow hInstance` → `instance`; `WndProc hWnd/iMsg` → `windowHandle/message`; `WinMain hInstance/hPrevInstance/szCmdLine/iCmdShow` → `instance/previousInstance/commandLine/showCommand` | retire |
| `World/Terrain.{h,cpp}:83-84,88-89,91,96,194,203,80,88,95,121-122,147-148,521` | 18 | every operation-local `sFileName/iMapSize/iStepSize/iTextureWrap` → `fileName/mapSize/stepSize/textureWrap` | retire |
| `World/WorldEnvironment.{h,cpp}:141-142,81` | 4 | `fFluidSurfaceHeight/fFluidDensity/fGasDensity/fGravity` → `fluidSurfaceHeight/fluidDensity/gasDensity/gravity` | retire |
| **Total** | **82** | **all corresponding sites in 27 files** | **82 retire** |

Ten `bBody`/`bBox`/`bHull` semantic parameters in
`Physics/ObjectContactManifold.cpp` are legitimate A/B pair notation, not Boolean
Hungarian notation. Two `wParam`/`lParam` rows on the external
`ImGui_ImplWin32_WndProcHandler` declaration are not first-party. Seven
first-party `wParam`/`lParam` semantic parameters remain with a recorded reason
at `ImGuiEditorInputPolicy.h:72`, `ImGuiEditorOwner.{h,cpp}:267,647`, and
`Window.{h,cpp}:117,112,245`: they are canonical opaque Win32 message slots.
Axis/domain names such as `xMove`, `rA`, and `bIndex` likewise remain.

## Validation Impact Map

- Physics hot paths: `SpatialGrid.cpp` and
  `Physics/Stages/PhysicsNarrowphaseStage.cpp`, plus MZ2's Physics parameter
  files. Required at closure: byte-exact Physics, deep Physics, and performance.
- Rendering/DX12 and broadly included Maths/Text headers: required at closure:
  full validation, with no baseline movement.
- MZ0 itself is documentation-only. No repository validation is required for
  this phase; the census commands are its proof.

## Census Commands

- `clang-tidy` with `google-readability-casting` over all 260 tracked `.cpp`
  files: 136 findings in 15 files.
- Exact `rg` token review for `NULL`, `SKULLBONEZ_INTRINSICS`, and every reported
  target-type spelling.
- Syntax-aware `clang-query` parameter declarations, reconciled with matching
  headers and implementation definitions.
