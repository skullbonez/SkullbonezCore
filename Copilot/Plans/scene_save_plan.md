# Plan: Scene State Save Feature (Button → File)

## Goal
Press a key at runtime → dump the full live scene state to a `.scene` file
that can be loaded back immediately to replay from that exact moment.
Balls, positions, velocities, angular velocities, orientations, world config — everything.

---

## C++ Equivalent of ISerializable

Java's `ISerializable` maps cleanly to a **pair of free functions per class**:

```cpp
void   SerializeTo  (FILE* f, const T& obj);
T      DeserializeFrom (FILE* f);   // or a ctor overload
```

No base class or vtable needed. The engine already uses `fprintf`/`fopen_s` for scene
files and logs, so we follow the same pattern.

---

## What Gets Saved

### Per Ball (GameModel)
| Field | Source | How |
|-------|--------|-----|
| Name | `GetName()` | `char[64]` |
| Position | `GetPosition()` | `Vector3` |
| Linear velocity | `GetVelocity()` | `Vector3` |
| Angular velocity | `GetAngularVelocity()` | `Vector3` |
| Orientation | `m_physicsInfo.GetOrientation()` | `Quaternion` (x y z w) |
| Bounding radius | `GetBoundingRadius()` | `float` |
| Mass | `GetMass()` | `float` |
| Restitution | `GetCoefficientRestitution()` | `float` (via RigidBody getter) |
| Rotational inertia | `GetRotationalInertia()` | `Vector3` |

### Scene-Level
| Field | Source |
|-------|--------|
| Gravity | `WorldEnvironment::m_gravity` |
| Fluid surface height | `WorldEnvironment::GetFluidSurfaceHeight()` |
| Fluid density | `WorldEnvironment::m_fluidDensity` |
| Physics enabled | `m_isScenePhysics` |
| Text enabled | `m_isSceneText` |
| Camera eye/at/up | `CameraCollection` active camera |

---

## File Format

Extend the existing `.scene` text format with a new `ball_state` directive
that carries the full dynamic state on top of the static `ball` setup params.

```scene
# Snapshot saved at frame 347 (2026-05-06T21:14:22)
physics on
text on
camera main 500 60 650  600 33 600  0 1 0
world gravity -30.0 fluid_height 25.0 fluid_density 1000.0

# ball_state name  pos(x y z)  vel(vx vy vz)  avel(ax ay az)  orient(qx qy qz qw)  radius mass restitution  rotinertia(ix iy iz)
ball_state marble1  312.4 34.2 410.1  -2.3 0.1 5.6  0.0 -4.2 1.1  0.12 0.45 -0.22 0.86  12.0 50.0 0.8  100 100 100
ball_state marble2  ...
```

**Why text not binary:** Matches existing scene format, human-readable, diffable,
and trivially loadable by the existing `TestScene` parser.

---

## Implementation Tasks

### Task 1 — Expose missing getters (small, non-breaking)

`RigidBody` needs two new public getters (currently private access only):
```cpp
// SkullbonezRigidBody.h
float          GetCoefficientRestitution();   // already exists ✅
float          GetFrictionCoefficient();      // already exists ✅
const Vector3& GetRotationalInertia();        // already exists ✅
// Need to expose orientation publicly:
const Quaternion& GetOrientation() const;     // already exists ✅
```
`GameModel` needs `GetBoundingRadius()` — **already exists** ✅.
Check: `GetRestitution()` path from `GameModel` → may need a passthrough.

---

### Task 2 — `SaveSceneSnapshot()` on `GameModelCollection`

Add to `SkullbonezGameModelCollection.h`:
```cpp
void SaveSceneSnapshot(const char* path, bool physicsOn, bool textOn) const;
```

Add to `SkullbonezGameModelCollection.cpp`:
```cpp
void GameModelCollection::SaveSceneSnapshot(const char* path, bool physicsOn, bool textOn) const
{
    FILE* f = nullptr;
    if (fopen_s(&f, path, "w") != 0 || !f) return;

    fprintf(f, "# Snapshot — %d balls\n", (int)m_gameModels.size());
    fprintf(f, "physics %s\n", physicsOn ? "on" : "off");
    fprintf(f, "text %s\n",    textOn    ? "on" : "off");
    fprintf(f, "frames -1\n");

    for (const auto& model : m_gameModels)
    {
        const Vector3& pos  = model.GetPosition();
        const Vector3& vel  = model.GetVelocity();
        const Vector3& avel = model.GetAngularVelocity();
        const Vector3& ri   = model.m_physicsInfo.GetRotationalInertia();
        const Quaternion& q = model.m_physicsInfo.GetOrientation();
        float r    = model.GetBoundingRadius();
        float mass = model.GetMass();
        float rest = model.m_physicsInfo.GetCoefficientRestitution();

        fprintf(f,
            "ball_state %s  %.6f %.6f %.6f  %.6f %.6f %.6f  %.6f %.6f %.6f"
            "  %.8f %.8f %.8f %.8f  %.4f %.4f %.4f  %.4f %.4f %.4f\n",
            model.GetName(),
            pos.x,  pos.y,  pos.z,
            vel.x,  vel.y,  vel.z,
            avel.x, avel.y, avel.z,
            q.m_x, q.m_y, q.m_z, q.m_w,   // need to expose these
            r, mass, rest,
            ri.x, ri.y, ri.z);
    }

    fclose(f);
}
```

Note: `Quaternion::m_x/y/z/w` are private. Two options:
- **Option A (preferred):** Add `GetComponents(float&,float&,float&,float&) const` to `Quaternion`.
- **Option B:** Add `friend class GameModelCollection` to `Quaternion` (avoid — tight coupling).

---

### Task 3 — `ball_state` parser in `SkullbonezTestScene.cpp`

The existing `TestScene` parser already handles `ball` and `camera` directives.
Add a `ball_state` branch that:
1. Reads all fields from the directive line
2. Creates a `GameModel` (same as `ball`) with static params
3. Sets dynamic state: `SetLinearVelocity`, `SetAngularVelocity`, `SetOrientation`

```cpp
else if (token == "ball_state")
{
    // parse: name  px py pz  vx vy vz  ax ay az  qx qy qz qw  radius mass restitution  ix iy iz
    char name[64];
    float px,py,pz, vx,vy,vz, ax,ay,az, qx,qy,qz,qw, r,mass,rest, ix,iy,iz;
    sscanf_s(line, "%63s  %f %f %f  %f %f %f  %f %f %f  %f %f %f %f  %f %f %f  %f %f %f",
             name, (unsigned)sizeof(name),
             &px,&py,&pz, &vx,&vy,&vz, &ax,&ay,&az, &qx,&qy,&qz,&qw,
             &r, &mass, &rest, &ix,&iy,&iz);

    GameModel gm(&worldEnv, Vector3(px,py,pz), Vector3(ix,iy,iz), mass);
    gm.AddBoundingSphere(r);
    gm.SetCoefficientRestitution(rest);
    gm.SetName(name);
    gm.m_physicsInfo.SetLinearVelocity(Vector3(vx,vy,vz));
    gm.m_physicsInfo.SetAngularVelocity(Vector3(ax,ay,az));
    gm.m_physicsInfo.SetOrientation(Quaternion(qx,qy,qz,qw));
    collection.AddGameModel(std::move(gm));
}
```

---

### Task 4 — Key binding in `SkullbonezRun.cpp`

Choose key: **F2** (unused, easy to reach, won't conflict with F=fly-mode).

In `InputState`:
```cpp
bool fF2WasDown;   // add to struct
```

In `SkullbonezRun::Run()` input handling block:
```cpp
bool f2Now = Input::IsKeyDown(VK_F2);
if (f2Now && !m_input.fF2WasDown)
{
    char path[256];
    sprintf_s(path, "snapshot_frame%04d.scene", m_currentFrame);
    m_gameModelCollection.SaveSceneSnapshot(path, m_isScenePhysics, m_isSceneText);
    // Optionally: log to console/text overlay "Saved snapshot_frame0347.scene"
}
m_input.fF2WasDown = f2Now;
```

File lands in the working directory (same as `Debug/` or `Profile/` depending on build).

---

### Task 5 — `world` directive in `TestScene` parser (optional but recommended)

To restore gravity/fluid settings from a snapshot:
```scene
world gravity -30.0 fluid_height 25.0 fluid_density 1000.0
```

This lets a saved snapshot be fully self-contained and round-trippable.

---

## File Changes Summary

| File | Change |
|------|--------|
| `SkullbonezQuaternion.h/.cpp` | Add `GetComponents(float&,float&,float&,float&) const` |
| `SkullbonezGameModelCollection.h/.cpp` | Add `SaveSceneSnapshot(path, physicsOn, textOn)` |
| `SkullbonezTestScene.cpp` | Add `ball_state` parser branch (+ optional `world` directive) |
| `SkullbonezInput.h` | Add `fF2WasDown` to `InputState` |
| `SkullbonezRun.cpp` | Add F2 key handler that calls `SaveSceneSnapshot` |

**Total: 5 files, ~120 lines of new code.**

---

## Rubber-Duck Findings (Must Fix)

### 1. Private `m_physicsInfo` — won't compile

`GameModelCollection::SaveSceneSnapshot` and the `TestScene` loader both access
`model.m_physicsInfo.GetOrientation()`, `.GetRotationalInertia()`, `.SetLinearVelocity()`, etc.
But `m_physicsInfo` is **private** on `GameModel` — only `CollisionResponse` is a friend.

**Fix:** Add passthrough methods on `GameModel`:
```cpp
// Getters (for serialization)
const Quaternion& GetOrientation() const;
const Vector3&    GetRotationalInertia() const;
float             GetCoefficientRestitution() const;

// Setters (for deserialization — these don't exist today)
void SetLinearVelocity(const Vector3& v);
void SetAngularVelocity(const Vector3& v);
void SetOrientation(const Quaternion& q);
```

### 2. `const` correctness cascade — won't compile

`SaveSceneSnapshot` is declared `const`, but `GetPosition()`, `GetVelocity()`,
`GetAngularVelocity()` on `GameModel` are **not** const-qualified. Can't call non-const
methods on `const auto& model`.

**Fix:** Add `const` qualifier to all read-only getters on `GameModel` and down through
`RigidBody`. Alternatively, drop `const` from `SaveSceneSnapshot` (expedient but wrong).

### 3. `sscanf_s` eats the directive token — runtime bug

Task 3's `sscanf_s(line, "%63s ...")` will capture `"ball_state"` as the name, not the
actual ball name. The existing parser dispatches on the first token, so `line` still starts
with `ball_state`.

**Fix:** Skip past the directive token before parsing. Either advance `line` pointer past
`ball_state` + whitespace, or use `%*s` to discard the first token:
```cpp
sscanf_s(line, "%*s %63s %f %f %f ...", name, (unsigned)sizeof(name), &px, ...);
```

### 4. Empty names produce unparseable output — runtime bug

`GetName()` returns `""` for unnamed balls. Writing `ball_state  312.4...` with no name
between the directive and first float confuses the parser.

**Fix:** Write a sentinel name when empty:
```cpp
const char* name = model.GetName();
if (!name[0]) name = "_unnamed";
```
Or generate indexed names: `_ball_0`, `_ball_1`, etc.

### 5. `WorldEnvironment` gravity/density are private — won't compile

Only `GetFluidSurfaceHeight()` is public. `m_gravity` and `m_fluidDensity` have no getters.

**Fix:** Add to `WorldEnvironment`:
```cpp
float GetGravity() const;
float GetFluidDensity() const;
```

### 6. Missing `SetTerrain` in loader — balls fall through world

Task 3 constructs a `GameModel` and adds it to the collection but never calls
`SetTerrain(pTerrain)`. Without it, terrain collision is disabled and balls drop to infinity.

**Fix:** Add `gm.SetTerrain(pTerrain);` after construction, same as the existing `ball` parser does.

### 7. `m_currentFrame` in legacy mode — file overwrite

In legacy mode, `m_currentFrame` may stay at 0 or not increment (it's a scene-mode concept).
Every F2 press would write to `snapshot_frame0000.scene`, silently overwriting.

**Fix:** Use a static counter instead:
```cpp
static int s_snapshotSeq = 0;
sprintf_s(path, "snapshot_%04d.scene", s_snapshotSeq++);
```

### 8. `frames -1` — verify parser handles it

The plan writes `frames -1`. If the exit condition is `m_currentFrame >= m_targetFrameCount`
with int comparison, -1 is never reached (good). But verify no unsigned cast or
`targetFrameCount == 0` early-exit exists in the parser.

### 9. Timestep caveat — not a bug but set expectations

The engine uses variable timestep from `SkullbonezTimer`. A loaded snapshot produces the
correct starting state, but physics evolution will diverge from the original run if frame
timing differs. This is fine for debugging/inspection — just don't promise bit-exact replay
unless in scene mode with fixed dt.

---

## Revised File Changes Summary

| File | Change |
|------|--------|
| `SkullbonezQuaternion.h/.cpp` | Add `GetComponents(float&,float&,float&,float&) const` |
| `SkullbonezGameModel.h/.cpp` | Add const getters + velocity/orientation setters (6 methods) |
| `SkullbonezRigidBody.h/.cpp` | Add `const` to existing read-only getters |
| `SkullbonezWorldEnvironment.h/.cpp` | Add `GetGravity()`, `GetFluidDensity()` |
| `SkullbonezGameModelCollection.h/.cpp` | Add `SaveSceneSnapshot(...)` (non-const or fix const chain) |
| `SkullbonezTestScene.cpp` | Add `ball_state` parser branch + `world` directive + `SetTerrain` call |
| `SkullbonezInput.h` | Add `fF2WasDown` to `InputState` |
| `SkullbonezRun.cpp` | Add F2 key handler with static sequence counter |

**Total: 8 files, ~120 lines of new code.**

---

## What This Gives You

- Press **F2** at any frame → `snapshot_frame0347.scene` written to disk
- Load it: `SKULLBONEZ_CORE.exe --scene snapshot_frame0347.scene`
- Scene resumes from that exact physical state: same positions, velocities,
  angular velocities, orientations — deterministic replay from any moment
- Works in scene mode AND legacy mode
- Snapshots are human-readable text — easy to inspect and hand-edit
