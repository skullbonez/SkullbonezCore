# Progress — Improve Collision Detection (Multi-Ball)

## Task: `split-response`

**Goal:** Modify `CollisionResponseGameModel` so it only changes velocities and clears flags.
Position advancement will be handled externally by `RunPhysics`.

### Steps

| # | Step | Detail | Status |
|---|------|--------|--------|
| 1 | Remove `remainingTimeStep` parameter | `CollisionResponseGameModel` signature changes from `(GameModel& responseTarget, float remainingTimeStep)` to `(GameModel& responseTarget)` | ⬜ Pending |
| 2 | Remove `this->UpdatePosition(remainingTimeStep)` call | Line 174 of `SkullbonezGameModel.cpp` — delete | ⬜ Pending |
| 3 | Remove `responseTarget.UpdatePosition(remainingTimeStep)` call | Line 175 of `SkullbonezGameModel.cpp` — delete | ⬜ Pending |
| 4 | Update declaration in `SkullbonezGameModel.h` | `void CollisionResponseGameModel(GameModel& responseTarget, float remainingTimeStep)` → `void CollisionResponseGameModel(GameModel& responseTarget)` (line 118) | ⬜ Pending |
| 5 | Update call site in `RunPhysics` | `SkullbonezGameModelCollection.cpp` line 108: remove `fChangeInTime - colTime` argument from `CollisionResponseGameModel(...)` call | ⬜ Pending |

---

## Task: `restructure-loop`

**Goal:** Rewrite the `RunPhysics` collision loop in `SkullbonezGameModelCollection.cpp` to
allow each ball to participate in multiple collision pairs per frame.

### Steps — Game-model collision loop

| # | Step | Detail | Status |
|---|------|--------|--------|
| 6 | Replace `isTimeStepApplied` with `timeRemaining` | Change `std::vector<bool> isTimeStepApplied((int)this->gameModels.size(), false)` to `std::vector<float> timeRemaining((int)this->gameModels.size(), fChangeInTime)` (line 80) | ⬜ Pending |
| 7 | Remove outer `isTimeStepApplied[x]` guard | Delete the `if(!isTimeStepApplied[x])` block at line 90 while keeping the inner loop body | ⬜ Pending |
| 8 | Remove inner `isTimeStepApplied[y]` guard | Delete the `if(!isTimeStepApplied[y])` block at line 95 while keeping the collision detect/response body | ⬜ Pending |
| 9 | Add `timeRemaining` skip guards | At the top of the inner loop body, add `if(timeRemaining[x] <= 0.0f \|\| timeRemaining[y] <= 0.0f) continue;` — skip pairs where either ball has exhausted its frame time | ⬜ Pending |
| 10 | Compute `availableTime` for each pair | `float availableTime = min(timeRemaining[x], timeRemaining[y])` — use `<algorithm>` include if not already present | ⬜ Pending |
| 11 | Pass `availableTime` to `CollisionDetectGameModel` | Change `CollisionDetectGameModel(this->gameModels[y], fChangeInTime)` to `CollisionDetectGameModel(this->gameModels[y], availableTime)` | ⬜ Pending |
| 12 | After collision: advance both to collision point | Keep existing `UpdatePosition(colTime)` calls (lines 104-105) — they advance each ball from current position to the point of contact | ⬜ Pending |
| 13 | Subtract consumed time from `timeRemaining` | After `UpdatePosition(colTime)`: `timeRemaining[x] -= colTime; timeRemaining[y] -= colTime;` | ⬜ Pending |
| 14 | Call modified response (no time arg) | Change `CollisionResponseGameModel(this->gameModels[y], fChangeInTime - colTime)` to `CollisionResponseGameModel(this->gameModels[y])` — velocity-only, no position update | ⬜ Pending |
| 15 | Remove `isTimeStepApplied[x] = true` and `isTimeStepApplied[y] = true` | Lines 111-112 — delete both, they are replaced by timeRemaining accounting | ⬜ Pending |

### Steps — Terrain collision loop

| # | Step | Detail | Status |
|---|------|--------|--------|
| 16 | Replace terrain guard | Change `if(!isTimeStepApplied[x])` to `if(timeRemaining[x] > 0.0f)` (line 123) | ⬜ Pending |
| 17 | Pass `timeRemaining[x]` to terrain detect | Change `CollisionDetectTerrain(fChangeInTime)` to `CollisionDetectTerrain(timeRemaining[x])` (line 126) | ⬜ Pending |
| 18 | Adjust terrain response remaining time | Change `CollisionResponseTerrain(fChangeInTime - colTime)` to `CollisionResponseTerrain(timeRemaining[x] - colTime)` (line 135) — `CollisionResponseTerrain` still advances position internally | ⬜ Pending |
| 19 | Zero `timeRemaining` after terrain response | Replace `isTimeStepApplied[x] = true` with `timeRemaining[x] = 0.0f` (line 138) — prevents double-advancement in the final pass | ⬜ Pending |

### Steps — Final advancement pass

| # | Step | Detail | Status |
|---|------|--------|--------|
| 20 | Replace final guard | Change `if(!isTimeStepApplied[x])` to `if(timeRemaining[x] > 0.0f)` (line 148) | ⬜ Pending |
| 21 | Pass `timeRemaining[x]` to final `UpdatePosition` | Change `UpdatePosition(fChangeInTime)` to `UpdatePosition(timeRemaining[x])` (line 150) | ⬜ Pending |

---

## Task: `positional-correction`

**Goal:** After velocity impulse in `RespondCollisionGameModels`, push overlapping spheres
apart so floating-point overlap does not accumulate across frames.

### Steps

| # | Step | Detail | Status |
|---|------|--------|--------|
| 22 | Compute sphere centres | In `RespondCollisionGameModels` (`SkullbonezCollisionResponse.cpp` line 86), after the angular/linear response code: get `Vector3 pos1 = gameModel1.physicsInfo.GetPosition()` and `Vector3 pos2 = gameModel2.physicsInfo.GetPosition()` | ⬜ Pending |
| 23 | Get radii | `float r1 = dynamic_cast<BoundingSphere*>(gameModel1.boundingVolume.get())->GetRadius()` and same for `r2` — `dynamic_cast` is already used in the same function so the pattern is established | ⬜ Pending |
| 24 | Compute distance and overlap | `Vector3 delta = pos2 - pos1; float dist = Vector::VectorMag(delta); float overlap = (r1 + r2) - dist;` | ⬜ Pending |
| 25 | Guard: skip if not overlapping | `if(overlap <= 0.0f \|\| dist <= 0.0f) return;` — no correction needed, and avoid division by zero for coincident centres | ⬜ Pending |
| 26 | Compute correction axis | `Vector3 axis = delta / dist;` — unit vector from pos1 to pos2, reusing the collision normal direction | ⬜ Pending |
| 27 | Push spheres apart | `float halfOverlap = overlap * 0.5f; gameModel1.physicsInfo.SetPosition(pos1 - axis * halfOverlap); gameModel2.physicsInfo.SetPosition(pos2 + axis * halfOverlap);` — each sphere moves half the overlap distance | ⬜ Pending |
| 28 | Place correction after velocity code | Insert after `gameModel2.physicsInfo.ApplyChangeInAngularVelocity()` (line 106) and before the `return` (line 108) | ⬜ Pending |

---

## Task: `verify-build`

| # | Step | Detail | Status |
|---|------|--------|--------|
| 29 | Build Debug configuration | `msbuild SKULLBONEZ_CORE.sln /p:Configuration=Debug /p:Platform=Win32` — expect 0 errors, 0 warnings | ⬜ Pending |
| 30 | Build Release configuration | `msbuild SKULLBONEZ_CORE.sln /p:Configuration=Release /p:Platform=Win32` — expect 0 errors, 0 warnings | ⬜ Pending |

---

## Summary

| Task | Steps | Status |
|------|-------|--------|
| `split-response` | 1–5 | ⬜ Pending |
| `restructure-loop` | 6–21 | ⬜ Pending |
| `positional-correction` | 22–28 | ⬜ Pending |
| `verify-build` | 29–30 | ⬜ Pending |
