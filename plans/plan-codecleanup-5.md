# Plan 5 — Code Cleanup

## Problem

After Phase 4 removed cleanup logic from destructors, many `.cpp` files were left with
empty destructor definitions (`{}`  or `{ // comment }`). These are noise — the compiler
generates an identical default. Similarly, several comments in the modified files explained
behaviour that is now self-evident from the types (e.g. "unique_ptr cleans up automatically").

## Tasks

| # | Change |
|---|--------|
| 1 | Remove all empty destructor definitions from `.cpp` files |
| 2 | Mark corresponding header declarations as `= default` |
| 3 | Remove superfluous "cleaned up automatically" and "nullptr by default" comments |

### Affected files

**Destructors removed from `.cpp`:**
- SkullbonezCamera.cpp
- SkullbonezQuaternion.cpp
- SkullbonezRotationMatrix.cpp
- SkullbonezTimer.cpp
- SkullbonezCameraCollection.cpp
- SkullbonezTextureCollection.cpp
- SkullbonezGameModel.cpp
- SkullbonezGameModelCollection.cpp

**Headers updated to `= default`:**
- SkullbonezCamera.h, SkullbonezQuaternion.h, SkullbonezRotationMatrix.h, SkullbonezTimer.h
- SkullbonezCameraCollection.h, SkullbonezTextureCollection.h
- SkullbonezGameModel.h, SkullbonezGameModelCollection.h

**Superfluous comments removed from:**
- SkullbonezGameModel.cpp (`// boundingVolume is nullptr by default (unique_ptr)`)
- SkullbonezGameModelCollection.cpp (`// vector automatically frees memory on any exit path`)
- SkullbonezRun.cpp (`// cTerrain ... cleaned up automatically`)

**Non-empty destructors retained:**
- SkyBox (sets `textures = 0`)
- Terrain (calls `glDeleteLists`)
- SkullbonezRun (calls `Destroy()` on singletons + `DeleteFont()`)
