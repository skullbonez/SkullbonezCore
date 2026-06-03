# Broadphase Collision Detection Plan - From Zero To Visual Spatial Grid

## Intent

This is the implementation plan for broadphase collision detection.

The plan is intentionally visual-first. The first useful code is not the hash
table, not pair generation, and not optimization. The first useful code is a
debug renderer that draws cubic grid cells in the world so a programmer can see
the coordinate system, tune the cell size, and catch wrong assumptions before
the broadphase is trusted by physics.

Design objective:

- The world is divided into fixed-size cubic cells.
- Every dynamic object is inserted into every cell touched by its conservative
  bounding sphere.
- Candidate object pairs are generated only from objects sharing at least one
  cell.
- Candidate pairs are deduplicated.
- Narrowphase collision remains authoritative.
- A live overlay shows cell occupancy and actual collision cells.
- The optimized hot path performs no per-frame heap allocation.

Implementation renderer scope:

- For this implementation, build the broadphase visual system in OpenGL only.
- Do not add DirectX 11 or DirectX 12 broadphase overlay support during this
  work.
- Do not spend time on cross-renderer parity while building this feature.
- The OpenGL path is the only renderer path for the visual-first development
  loop.

## Target Behavior

As the system is implemented, the programmer should see, at all times during
this effort:

- White wireframe cell boxes for cells that are fading out or being tracked as
  empty.
- Yellow cells when an object first enters them.
- Blue cells while they remain occupied.
- Red cells when a narrowphase collision actually occurs inside them.
- Red darkening toward black when repeated collision activity happens in the
  same cell.
- Red/black fading back to blue after collision activity stops.

The overlay must be observational only. Rendering debug visuals must never
change physics behavior, pair generation, wake-up behavior, or solver
determinism.

## Non-Negotiable Process Notes

1. This implementation is OpenGL only. Do not implement DirectX 11 or DirectX 12
   broadphase visual support as part of this plan.
2. Test and build runs for this plan must keep vsync on at all times. Fixed step
   is still allowed and encouraged for deterministic physics, but do not turn
   vsync off for broadphase visual tests, profiler notes, larger stress scenes,
   or legacy run mode.
3. Debug visuals must always be on while building and validating this system.
   Every implementation stage must be inspected with the grid/contact visuals
   visible. If a runtime toggle exists later, the development and test scenes for
   this work must force visuals on. No milestone may be accepted from logs alone.
4. A new falling-ball test scene must be made before collision scenes are added.
   The required sequence is:
   - first, one falling ball moving through grid cells;
   - then boundary and neighbor-cell probes;
   - then two balls colliding while cells change color;
   - then mixed ball/box collisions;
   - then larger solver stress scenes such as `physics_bench_varied.scene`;
   - last, legacy run mode.
5. Profiler measurements must be taken along the way, not only at the end.
   Record milliseconds and percentage deltas at each stage. Use status-dot emoji
   in the notes: 🟢 for speed-up or pair-count reduction, 🟠 for mixed or
   inconclusive results, and 🔴 for regressions. For example:

   ```text
   🟢 Narrowphase 4.80 ms -> 1.35 ms, speed-up 71.9%
   🟢 Candidate pairs 44,850 -> 1,260, reduction 97.2%
   🟠 Broadphase 0.10 ms -> 0.38 ms, accepted because narrowphase dropped 3.45 ms
   🔴 Total physics 2.45 ms -> 3.10 ms, regression 26.5%
   ```

## Allowed Physics Debug Render Views

The broadphase grid overlay should be developed alongside the available physics
debug render views. These are allowed to be referenced and used in the visual
test scenes:

Scene directives:

```text
physics_debug all
physics_debug contacts
physics_debug axes
physics_debug sleep
physics_debug_contacts on
physics_debug_axes on
physics_debug_sleep on
physics_debug_contact_linger 0.75
debug_vectors on
```

Command-line overrides:

```text
--physics-debug all
--physics-debug-contacts on
--physics-debug-axes on
--physics-debug-sleep on
--physics-debug-contact-linger 0.75
```

Runtime keys:

```text
C  cycle physics debug overlay modes
V  toggle collision visualizer
9  toggle debug vectors
```

## Core Design Choices

Use a uniform grid rather than an octree or dynamic BVH for this first version.

Reasons:

- The objects are small relative to the world and mostly similar in size.
- A fixed grid is easier to visualize and debug.
- Insert cost is straightforward.
- Candidate generation is simple.
- Cell occupancy has a direct visual representation.
- The data structure can later be made fixed-capacity and allocation-free.

Use conservative bounding spheres for broadphase.

Reasons:

- A sphere needs only center and radius.
- It works for balls directly.
- It works for boxes by using distance from center to farthest corner.
- It may produce extra candidates, which is acceptable.
- It should never drop a real collision candidate.

Use narrowphase collision to drive collision colors.

Reasons:

- A shared grid cell means "maybe colliding", not "colliding".
- Coloring candidates red would teach the wrong mental model.
- Red must mean an actual narrowphase collision response or contact happened.

## Glossary

| Term | Meaning |
|------|---------|
| Cell | One fixed-size cubic region of world space. |
| Cell coordinate | Integer coordinate `(ix, iy, iz)` identifying one cell. |
| Cell key | Packed or hashed representation of a cell coordinate. |
| Active cell | A cell occupied by at least one object this frame. |
| Candidate pair | Two object indices that share at least one active cell. |
| Narrowphase | The exact or shape-aware collision test run after broadphase. |
| Collision cell | The grid cell containing the midpoint of an actual collision/contact. |

## Required Equations

The broadphase uses integer cell coordinates for storage and world coordinates
for rendering. These equations are required and should be written into the code
comments or test notes while the system is being implemented.

### Object Cell Range

For an object with center `p = (px, py, pz)`, conservative broadphase radius
`r`, and grid cell size `C`:

```text
minX = floor((px - r) / C)
minY = floor((py - r) / C)
minZ = floor((pz - r) / C)

maxX = floor((px + r) / C)
maxY = floor((py + r) / C)
maxZ = floor((pz + r) / C)
```

Then insert the object into every overlapped cell:

```text
for ix in minX..maxX
for iy in minY..maxY
for iz in minZ..maxZ
    insert object into cell(ix, iy, iz)
```

Use `floor`, not integer truncation. This matters for negative coordinates:

```text
floor(-0.04) = -1
truncate(-0.04) = 0
```

### Cell Rendering Bounds

For cell coordinate `c = (ix, iy, iz)`:

```text
worldMin = (ix * C, iy * C, iz * C)
worldMax = worldMin + (C, C, C)
```

If `C = 24`, then cell `(20, 4, 20)` spans:

```text
x: 480..504
y:  96..120
z: 480..504
```

That one example should be used constantly while building the renderer. If a
ball at `(500, 120, 500)` is not visually aligned with those cell boundaries,
the grid math is wrong.

### Broadphase Radius

For a sphere:

```text
rBroadphase = rSphere
```

For a box with half extents `(hx, hy, hz)`:

```text
rBroadphase = sqrt(hx^2 + hy^2 + hz^2)
```

The box equation intentionally overestimates. Broadphase bounds must be
conservative; they are allowed to create extra candidates, but they must not
miss real contacts.

### Cell Hash Key

A hash table can use a stable large-prime mix:

```text
cellKey = ix * 73856093 xor iy * 19349663 xor iz * 83492791
bucketIndex = cellKey & (tableSize - 1)   // tableSize must be a power of two
```

The visualizer should keep the original `(ix, iy, iz)` as well as the key so it
can draw cells without reverse-engineering the hash.

### Candidate Counts

The baseline all-pairs count for `N` objects is:

```text
naivePairs = N * (N - 1) / 2
```

The local pair count inside one cell with `m` objects is:

```text
cellPairs = m * (m - 1) / 2
```

The broadphase candidate set is the deduplicated union of all cell-local pairs:

```text
candidatePairs = unique(union(cellPairs for every active cell))
```

### Pair Deduplication Bitset

Normalize every pair so `a < b`.

```text
pairIndex = b * (b - 1) / 2 + a
wordIndex = pairIndex >> 6
bitMask   = 1 << (pairIndex & 63)
```

This maps every possible object pair to one bit in a triangular pair matrix.

### Fixed-Capacity Sizing

For `Nmax` maximum objects, with each object overlapping at most eight cells:

```text
maxCellEntries = Nmax * 8 + safetyMargin
pairBitCount   = Nmax * (Nmax - 1) / 2
pairWordCount  = ceil(pairBitCount / 64)
```

The eight-cell assumption comes from an object that can straddle at most two
cells along each axis:

```text
2 * 2 * 2 = 8
```

### Profiler Delta Notes

Speed-up percentage:

```text
speedUpPercent = ((beforeMs - afterMs) / beforeMs) * 100
```

Pair-count reduction percentage:

```text
reductionPercent = ((beforePairs - afterPairs) / beforePairs) * 100
```

Regression percentage:

```text
regressionPercent = ((afterMs - beforeMs) / beforeMs) * 100
```

## Phase 1 - Start The Test Scene And Draw One Debug Cube

Goal: start the first dedicated test scene and prove OpenGL can draw one
wireframe cube in world space before any broadphase data exists.

Build:

1. Create the first broadphase visual test scene immediately. This is the scene
   where the first cube will be drawn.
2. The scene should be deterministic and minimal:
   - physics off,
   - text/HUD clutter off except the profiler if available,
   - vsync on,
   - stable camera aimed at `(500, 120, 500)`,
   - one visible ball or marker at `(500, 120, 500)`,
   - debug visuals forced on.
3. Add an OpenGL-only debug line path for world-space lines.
4. Use interleaved per-vertex data:

   ```text
   x, y, z, r, g, b
   ```

5. Draw independent line segments, two vertices per line.
6. Add a simple OpenGL shader:
   - vertex input: position and color,
   - uniform: view-projection matrix,
   - fragment output: interpolated RGB color at full opacity.
7. Add a temporary call that draws one hardcoded cube inside this first test
   scene.
8. Disable depth testing for this debug pass or otherwise ensure the overlay
   remains readable through scene geometry.
9. Keep the primitive deliberately small: no grid logic yet, only colored
   OpenGL lines.

Visual checkpoint:

1. Run the new broadphase cube-probe scene using OpenGL.
2. Draw a single yellow cube at:

   ```text
   min = (480, 96, 480)
   max = (504, 120, 504)
   ```

3. Rotate or move the camera.
4. Confirm the cube stays fixed in world space.
5. Confirm the cube is not screen-space UI.
6. Confirm the cube is visible in OpenGL. Do not implement or test DirectX
   renderers for this phase.

Expected visible result:

```text
One yellow wireframe cube appears around the chosen world-space region.
Nothing else about physics or collision has changed.
```

Stop conditions:

- If the cube is offset, fix matrix multiplication or coordinate handedness.
- If the cube is invisible behind geometry, fix debug line depth behavior.
- If it does not render in OpenGL, fix the OpenGL shader, vertex layout, or
  draw call before continuing.

## Phase 2 - Draw A Small Manual Grid

Goal: turn the single cube into a small visible 3D grid so cell size and cell
origin can be inspected.

Build:

1. Create a debug overlay component that owns a dynamic line buffer.
2. Give it a configurable `cellSize`.
3. Write a helper that emits one cube for a cell coordinate:

   ```text
   EmitCell(ix, iy, iz, color)
   ```

4. Emit these cells manually:

   ```text
   (20, 4, 20)
   (21, 4, 20)
   (20, 5, 20)
   (20, 4, 21)
   ```

5. Color `(20, 4, 20)` yellow and the others white.
6. Add runtime debug state for the overlay, but force it on for every
   broadphase development scene.
7. Make the overlay update and render every frame, but keep the data hardcoded.

Visual checkpoint:

1. Place one visible ball at `(500, 120, 500)` with radius `4`.
2. Confirm the grid overlay is visible before touching input.
3. Confirm:
   - The highlighted cell spans x `480..504`.
   - The highlighted cell spans y `96..120`.
   - The highlighted cell spans z `480..504`.
   - The ball touches or crosses the top face of y-cell `4`.
4. Change `cellSize` manually:
   - `12` should show smaller cubes.
   - `24` should be the initial tuning candidate.
   - `48` should show larger cubes.
5. Restore `cellSize = 24`.

Expected visible result:

```text
The programmer can point at a ball and say which grid cell it is in.
```

Stop conditions:

- If grid boxes do not tile seamlessly, fix cube emission.
- If changing cell size moves cell origins unexpectedly, fix cell-to-world math.
- If the overlay is not visible by default in the probe scene, fix runtime debug
  state before moving on.

## Phase 3 - Add The Mandatory Falling Ball Probe Scene

Goal: create the first visual broadphase test before broadphase storage exists.
This scene is mandatory. No collision scene, pair generation milestone, stress
test, or legacy run-mode test may be accepted until this falling-ball scene
exists and clearly shows a ball moving through grid cells.

Build:

1. Add a small deterministic test scene with:
   - one ball,
   - gravity enabled,
   - fixed time step,
   - vsync on,
   - no random spawning,
   - a stable camera looking at the grid region.
2. Start the ball above the known cell:

   ```text
   ball center = (500, 155, 500)
   ball radius = 4
   ```

3. Keep the manual grid cells visible while the ball falls.
4. Add optional on-screen or log-only diagnostics for the selected ball:

   ```text
   position
   radius
   predicted min cell
   predicted max cell
   ```

Visual checkpoint:

At `cellSize = 24`:

```text
position.y = 155, radius = 4
minY = floor((155 - 4) / 24) = 6
maxY = floor((155 + 4) / 24) = 6
```

Near y boundary:

```text
position.y = 120, radius = 4
minY = floor((120 - 4) / 24) = 4
maxY = floor((120 + 4) / 24) = 5
```

Expected visible result:

```text
The falling ball passes through the manual grid.
At y = 120, it visibly straddles two y cells.
Debug visuals are visible for the whole run.
```

Why this matters:

This teaches the programmer what the grid should do before it does it
automatically. When occupancy is added later, the correct behavior will be
obvious by eye.

## Phase 4 - Add Simple Occupancy Storage

Goal: make the grid light up based on real object positions, while collision
logic still uses the baseline all-pairs loop.

Build the simplest useful grid:

```text
grid:
    cellSize
    inverseCellSize
    map cellKey -> list of object indices
    list of active cell coordinates
```

Implementation steps:

1. Add `Clear`.
2. Add `InsertObject(index, position, radius)`.
3. In `InsertObject`, calculate min/max cell coordinates with `floor`.
4. Insert the object index into every overlapped cell.
5. Store active cell coordinates separately so the renderer never has to decode
   an opaque hash.
6. Add `GetActiveCells`.
7. Each physics tick:
   - clear the grid,
   - insert every dynamic object,
   - expose active cells to the overlay.
8. Do not use the grid for collision candidates yet.
9. Keep the previous all-pairs collision loop unchanged.

Temporary storage is allowed here:

- A hash map is fine.
- Dynamic arrays are fine.
- The point is to prove behavior visually before optimizing.

Overlay behavior:

1. Unknown active cell becomes yellow.
2. Yellow fades to blue over `0.5` seconds.
3. Blue means still occupied.
4. A cell no longer active fades to white and disappears.

Visual checkpoint:

Run the falling ball probe.

Expected visible result:

```text
As the ball falls, new cells flash yellow.
Cells remain blue while occupied.
When the ball leaves a cell, the previous cell fades out.
At boundaries, neighboring cells light up together.
```

Stop conditions:

- If only one cell lights at boundaries, fix min/max cell range.
- If cells flicker every frame while continuously occupied, fix tracked-cell
  state identity.
- If previous cells never disappear, fix inactive-cell transition handling.

## Phase 5 - Prove Face, Edge, Corner, And Negative Coordinates

Goal: visually test the exact cases that broadphase bugs usually miss.

Build four tiny probe scenes or debug placements.

Probe A: single-axis face crossing

```text
cellSize = 24
ball center = (503, 120, 500)
ball radius = 5.5
```

Expected:

```text
x cells: 20 and 21
y cells: 4 and 5
z cells: 20 only, unless z is also near a boundary
```

Probe B: two-axis edge crossing

```text
cellSize = 24
ball center = (503, 120, 503)
ball radius = 5.5
```

Expected:

```text
x cells: 20 and 21
y cells: 4 and 5
z cells: 20 and 21
up to 8 cells visible
```

Probe C: exact corner crossing

```text
cellSize = 24
ball center = (504, 120, 504)
ball radius = 5.5
```

Expected:

```text
The object overlaps all neighboring cells around that corner.
No off-by-one hole appears.
```

Probe D: negative coordinate crossing

```text
cellSize = 24
ball center = (-1, 25, -1)
ball radius = 5.5
```

Expected:

```text
negative cells light up correctly.
floor(-0.04) behaves as -1.
integer truncation to 0 would be visibly wrong.
```

Visual acceptance:

- Face crossing lights neighboring cells on one axis.
- Edge crossing lights neighbors on two axes.
- Corner crossing lights neighbors on three axes.
- Negative coordinates produce cells on the negative side of the origin.

Stop conditions:

- Any mismatch means collision pair generation is not allowed yet.
- Fix occupancy first, because candidate generation will only hide this bug
  under more code.

## Phase 6 - Generate Candidate Pairs For Inspection Only

Goal: create broadphase pairs, but do not feed them into collision response yet.

Build:

1. Add `GetCandidatePairs`.
2. For every cell with two or more objects:
   - copy the object indices,
   - emit every pair combination.
3. Normalize pair order:

   ```text
   a = min(indexA, indexB)
   b = max(indexA, indexB)
   ```

4. Skip self-pairs.
5. Deduplicate pairs across cells.
6. Store candidate pairs in a retained pair buffer.
7. Add temporary diagnostics:

   ```text
   object count
   naive pair count
   active cell count
   candidate pair count
   max objects in one cell
   ```

8. Keep the all-pairs collision loop authoritative.

Visual checkpoint: same-cell candidate

```text
ball A = (494, 120, 500), radius 4
ball B = (501, 120, 500), radius 4
```

Expected:

```text
Both balls share at least one cell.
Candidate pair count = 1.
Cells are blue/yellow, not red, because candidate does not mean collision.
```

Visual checkpoint: boundary candidate

```text
ball A = (500, 120, 500), radius 5.5
ball B = (508, 120, 500), radius 5.5
```

Expected:

```text
The balls may occupy different center cells, but their AABBs share boundary
cells, so candidate pair count = 1.
```

Stop conditions:

- If candidate pairs appear for far-apart objects, inspect stale cells or bad
  hash/key handling.
- If boundary candidates are missing, return to occupancy range math.
- If duplicate pairs appear, fix pair normalization and deduplication.

## Phase 7 - Replace Collision Pair Source For Simple Sphere Physics

Goal: run actual collision detection only on broadphase candidate pairs for the
simplest object type first.

Build:

1. Keep the baseline all-pairs loop available behind a temporary debug switch.
2. For the simple sphere path:
   - clear the grid,
   - insert spheres,
   - generate candidate pairs,
   - loop candidate pairs instead of all pairs.
3. Keep the baseline narrowphase body exactly the same:
   - skip objects with no time remaining,
   - compute available time,
   - run swept sphere collision,
   - advance both objects to collision time,
   - apply response,
   - handle static overlap fallback.
4. Add a collision-cell key list:
   - only append to it after an actual narrowphase collision response,
   - calculate midpoint of the two colliding objects,
   - convert midpoint to cell coordinate with `floor`,
   - hash or pack that coordinate.
5. Feed collision-cell keys to the overlay.
6. Add red collision state:
   - red overrides yellow/blue,
   - repeated collisions increase heat,
   - heat darkens red toward black,
   - when collision stops, fade back to blue if occupied.

Visual checkpoint: two falling/colliding balls

Create a scene:

```text
fixed step
two balls
same height
moving toward one another
stable camera looking at impact region
debug visuals forced on for the entire run
```

Expected visible sequence:

```text
1. Each ball activates cells as it moves.
2. Shared/nearby candidate cells stay yellow/blue.
3. Nothing turns red merely because a candidate exists.
4. At actual impact, the midpoint cell turns red.
5. Repeated impact/contact darkens red.
6. After separation, red fades back toward blue or white.
```

Behavior checkpoint:

Run the same scene twice:

- once with all-pairs collision,
- once with broadphase candidate pairs.

Expected:

```text
The visible collision response is the same.
The broadphase version performs fewer narrowphase tests.
```

Stop conditions:

- If collision disappears, candidate generation missed the pair.
- If collision timing changes, compare pair order and time-remaining behavior.
- If red appears too early, collision-cell keys are being recorded from
  candidates rather than actual responses.

## Phase 8 - Add Boxes Through Conservative Radius

Goal: extend candidate generation to non-sphere dynamic objects without making
the broadphase shape-specific.

Build:

1. Add a broadphase radius query for every object type.
2. For a sphere:

   ```text
   broadphase radius = sphere radius
   ```

3. For an oriented box:

   ```text
   broadphase radius = sqrt(halfX^2 + halfY^2 + halfZ^2)
   ```

4. Insert every dynamic object using center position plus broadphase radius.
5. Keep narrowphase shape-specific:
   - sphere/sphere,
   - sphere/box,
   - box/box.
6. Keep broadphase ignorant of shape details.

Visual checkpoint: box occupancy

Create a scene:

```text
one box at a known cell boundary
one ball nearby
fixed camera
debug visuals forced on
```

Expected:

```text
The box lights cells according to its conservative bounding radius.
The lit area may be visibly larger than the rendered box.
That is correct: broadphase is allowed to overestimate.
```

Visual checkpoint: box-ball collision

Create a scene:

```text
box moving toward ball
off-center impact
fixed step
debug visuals forced on
```

Expected:

```text
Cells turn yellow/blue for occupancy.
The collision cell turns red only when narrowphase reports box-ball contact.
```

Stop conditions:

- If box cells are too tight and collisions are missed, increase/fix the
  conservative radius.
- If the overlay makes the broadphase look "wrong" because it is larger than
  the mesh, document that broadphase bounds are intentionally conservative.

## Phase 9 - Integrate Sleeping And Wake-Up Behavior

Goal: ensure broadphase culling does not make sleeping bodies invisible to
nearby awake bodies.

Build:

1. Insert sleeping bodies into the grid.
2. Generate candidate pairs that include sleeping bodies.
3. During narrowphase handling:
   - skip sleeping-sleeping pairs,
   - when one object is awake and the other sleeping, test whether the awake
     object has meaningful wake energy,
   - if wake energy is present, run a swept or persistent overlap test,
   - wake the sleeping body only when contact/impact is plausible.
4. Keep sleeping bodies visually represented as occupied cells.
5. Keep collision coloring tied to actual contact, not mere wake candidacy.

Visual checkpoint: sleeper wake scene

Create a scene:

```text
one sleeping object resting on terrain or another stable support
one awake ball or box moving toward it
debug visuals forced on
```

Expected:

```text
1. The sleeping object's cells remain blue.
2. The moving object enters nearby cells.
3. A candidate pair exists before wake-up.
4. Contact wakes the sleeper.
5. Actual contact paints a red cell.
```

Stop conditions:

- If sleepers are not inserted, awake objects can tunnel through the logical
  wake-up path.
- If every nearby awake body wakes sleepers without contact, wake criteria are
  too broad.
- If red appears before contact, collision-cell recording is too early.

## Phase 10 - Reuse Candidate Pairs For Persistent Contacts

Goal: make the candidate pair list the single broadphase source for both swept
impact response and resting/contact manifold generation.

Build:

1. After candidate generation, retain the pair buffer for the whole physics
   tick.
2. First pass:
   - use candidate pairs for swept narrowphase and immediate responses.
3. Second pass:
   - use the same candidate pairs to build contact manifolds for touching or
     nearly touching objects.
4. For each candidate pair in the contact pass:
   - skip invalid/self pairs,
   - skip sleeping-sleeping pairs,
   - run shape-aware contact manifold generation,
   - create one or more contact rows,
   - assign deterministic feature ids,
   - cache contact impulses by stable pair/feature key.
5. Use contact rows for the iterative solver.

Visual checkpoint:

Create a scene:

```text
two balls or boxes initially resting in contact
little or no initial velocity
debug visuals forced on
```

Expected:

```text
Cells are blue because occupied.
No red appears unless the contact pass records active contact visualization.
The resting contact remains stable because the pair is available to the solver.
```

Stop conditions:

- If resting contacts vanish, the candidate list may be too narrow or not
  retained for the second pass.
- If stacks jitter after integration, inspect pair order, contact caching, and
  manifold feature ids.

## Phase 11 - Stabilize Pair Order And Determinism

Goal: make broadphase behavior deterministic enough for fixed-step physics
validation.

Build:

1. Insert objects by stable object index.
2. Normalize every pair to `(lowerIndex, higherIndex)`.
3. Deduplicate pairs deterministically.
4. Decide whether to sort the emitted pair list.

Suggested policy:

- First, do not sort.
- Measure whether pair order is stable frame-to-frame.
- If deterministic physics baselines drift, sort pairs by `(a, b)` after
  generation and measure the cost.

Important:

```text
Debug overlay state must never influence pair order.
```

Visual checkpoint:

1. Run a collision scene.
2. Keep debug visuals forced on during simulation.
3. Repeat from reset.

Expected:

```text
Collision behavior is unchanged by debug visualization being present.
Pair counts are unchanged by debug visualization being present.
```

Stop conditions:

- Any simulation difference caused by debug visual state is a design bug.
- Any dependence on cell tracking timers is a design bug.

## Phase 12 - Optimize Storage Into A No-Allocation Spatial Hash

Goal: replace the simple observable grid with fixed-capacity storage while
preserving every visual and behavioral result.

Only do this after:

- falling ball occupancy is visually correct,
- boundary cases are visually correct,
- candidate pairs work,
- collisions use candidate pairs,
- boxes and sleepers are integrated.

Final storage design:

```text
SpatialGrid
    cellSize
    inverseCellSize
    generation
    fixed bucket array
    active bucket index array
    fixed entry pool
    fixed pair dedup bitset
```

Bucket:

```text
key
generation
head entry index
object count
cell coordinate ix, iy, iz
```

Entry:

```text
object index
next entry index
```

Build:

1. Replace dynamic cell map with a fixed bucket array.
2. Use a power-of-two table size and a bit mask for bucket lookup.
3. Convert cell coordinate to a hash key with stable large-prime mixing.
4. Find buckets with linear probing:
   - if bucket generation is stale, claim it,
   - if key matches, reuse it,
   - otherwise probe the next bucket.
5. Replace per-cell dynamic vectors with a fixed entry pool.
6. Each cell bucket stores a linked list head into the entry pool.
7. `Clear` increments `generation` and resets counters.
8. Keep an `activeBuckets` list so pair generation iterates only touched cells.
9. Keep cell coordinates in the bucket for visualization.
10. Add bounds checks and debug assertions for:
    - object index range,
    - entry pool exhaustion,
    - bucket table exhaustion,
    - active bucket overflow,
    - pair bitset bounds.

Capacity model:

```text
maxObjects = 512
maxCellsPerObject = 8
maxEntries = maxObjects * maxCellsPerObject + safetyMargin
```

Why 8:

```text
With cellSize = 24 and object diameter well below cell size, an object can
straddle at most two cells on each axis.
2 * 2 * 2 = 8
```

Pair dedup bitset:

```text
pairCount = maxObjects * (maxObjects - 1) / 2
pairWords = ceil(pairCount / 64)
```

Triangular pair index:

```text
// a < b
pairIndex = b * (b - 1) / 2 + a
```

Visual checkpoint:

Run all previous visual probes:

- single hardcoded grid region,
- falling ball,
- face boundary,
- edge boundary,
- corner boundary,
- negative coordinate,
- same-cell candidate,
- boundary candidate,
- sphere collision,
- box collision,
- sleeper wake.

Expected:

```text
The optimized grid looks exactly like the simple grid.
Only performance and allocation behavior changed.
```

Stop conditions:

- Any visual difference means the optimized storage changed behavior.
- Any missing red cell means collision feedback path broke.
- Any new physics drift means pair output order/count changed unexpectedly.

## Phase 13 - Add Broadphase Performance Instrumentation

Goal: prove the broadphase is reducing narrowphase work and not moving cost into
grid maintenance.

Build:

1. Add timing markers around:
   - force application,
   - broadphase insert and pair generation,
   - narrowphase,
   - contact generation,
   - solver.
2. Add temporary or debug-only counters:
   - object count,
   - active cell count,
   - candidate pair count,
   - naive pair count,
   - maximum cell occupancy,
   - entry pool high-water mark,
   - bucket probe high-water mark.
3. Create a many-object scene:
   - hundreds of balls,
   - spread over a large area,
   - fixed step,
   - vsync on,
   - no random behavior after initialization,
   - optional mixed clusters for worst-case cells.
4. Compare:

   ```text
   naive pairs = n * (n - 1) / 2
   candidate pairs = generated broadphase pairs
   ```
5. At every milestone, record the profiler numbers before and after the change:
   - broadphase milliseconds,
   - narrowphase milliseconds,
   - total physics milliseconds,
   - candidate-pair count,
   - percentage reduction versus the previous stage.
6. Whenever the broadphase produces an improvement, show the result with the
   required 🟢 marker. Use 🟠 for mixed results and 🔴 for regressions:

   ```text
   🟢 Physics 6.20 ms -> 2.45 ms, speed-up 60.5%
   🟢 Narrowphase pairs 124,750 -> 3,400, reduction 97.3%
   🟠 Broadphase 0.00 ms -> 0.55 ms, acceptable because total physics improved
   🔴 Solver 1.40 ms -> 1.95 ms, regression 39.3%
   ```
7. Keep the profiler visible beside the grid visuals while tuning. The human
   programmer should see both the colored cells and the timing improvement as
   the system evolves.
8. Keep vsync on for these measurements. The goal is to record consistent
   engine-observed profiler deltas during the same visual test conditions, not
   to switch into a separate vsync-off benchmark mode.

Expected:

```text
Candidate pairs are dramatically lower than naive pairs in spread scenes.
Broadphase cost stays stable enough to justify the extra pass.
No per-frame heap allocation occurs in the optimized grid.
The improvement is visible as profiler ms/% numbers, not just asserted.
```

Stop conditions:

- If candidate pair count approaches naive count in normal scenes, cell size may
  be too large or objects too clustered.
- If entry pool overflows, capacity assumptions are wrong.
- If bucket probe chains are long, table size or hash quality needs adjustment.

## Phase 14 - Tune Cell Size Visually And Quantitatively

Goal: settle on a default cell size using both what the programmer sees and what
the counters report.

Procedure:

1. Test `cellSize = 12`.
   - Visual: many small boxes.
   - Expected: more cell entries per object.
   - Risk: broadphase insert cost rises.
2. Test `cellSize = 24`.
   - Visual: cells are easy to read.
   - Expected: most objects occupy one cell, boundary cases occupy up to eight.
   - Risk: moderate extra candidates in clusters.
3. Test `cellSize = 48`.
   - Visual: fewer large boxes.
   - Expected: fewer cell entries but many more candidates per occupied cell.
   - Risk: narrowphase work rises.
4. For each size, record:
   - active cells,
   - candidate pairs,
   - broadphase time,
   - narrowphase time,
   - visual readability.

Expected default:

```text
cellSize = 24
```

Accept it only if:

- falling ball cells are readable,
- boundary probes behave correctly,
- candidate counts are much lower than all-pairs in spread scenes,
- clustered scenes do not overflow storage,
- physics validation remains deterministic.

## Phase 15 - Make The Overlay Production-Usable

Goal: turn the OpenGL development visualization into a polished debug tool.

Build:

1. Keep debug visuals forced on for all broadphase development and validation
   scenes.
2. A runtime toggle may exist for general engine use only after the feature is
   complete, but this plan's scenes must force the overlay on.
3. Update overlay state after physics each frame.
4. Render overlay after normal scene geometry.
5. Keep overlay line drawing OpenGL-only for this implementation.
6. Keep overlay data separate from physics data:
   - copy active cell summaries out of the grid,
   - copy collision keys out of the physics step,
   - never let the visualizer mutate grid storage.
7. Keep tracked cells fixed-capacity.
8. Remove temporary candidate-only colors unless they are behind a separate
   explicit debug mode.
9. Remove temporary hardcoded cells.
10. Remove temporary diagnostic spam.
11. Keep optional debug counters available through profiler or development logs.
12. Keep the profiler visible or logged during these runs so 🟢/🟠/🔴 ms/% notes
    can be recorded.

Visual checkpoint:

1. Run a scene with moving balls.
2. Confirm the overlay is forced on before touching input.
3. Confirm the overlay renders correctly in OpenGL.
4. Reset scene.
5. Run a mixed sphere/box scene.
6. Confirm profiler timing remains visible or captured.

Expected:

```text
The overlay behaves like a normal debug view.
It is useful but never required for physics correctness.
During this broadphase collision detection work, it is always visible.
It is OpenGL-only for this implementation.
```

## Phase 16 - Build A Permanent Visual Test Ladder

Goal: preserve the human debugging process as test scenes so each later phase
can be inspected quickly.

Create a small ladder of tests:

| Test | What It Proves |
|------|----------------|
| Grid cube probe | Cell-to-world rendering is correct. |
| Falling ball | Dynamic occupancy follows motion. |
| Face boundary | Neighbor cell lights on one crossed axis. |
| Edge boundary | Neighbor cells light on two crossed axes. |
| Corner boundary | Up to eight cells light correctly. |
| Negative coordinate | `floor` behavior is correct below zero. |
| Same-cell pair | Candidate pair appears for shared cell. |
| Boundary pair | Candidate pair appears across cell boundary. |
| Sphere collision | Red comes from actual narrowphase collision. |
| Box collision | Conservative radius supports shape mixes. |
| Sleeper wake | Sleeping objects stay broadphase-visible. |
| Many-object spread | Candidate count is lower than naive count. |
| Many-object cluster | Capacity and worst-case behavior are understood. |

Each visual test should include:

- deterministic setup,
- fixed step,
- vsync on,
- stable camera,
- clear expected cell behavior,
- a short note saying what should be visible.
- debug visuals forced on,
- profiler timing captured where physics is running.

Required execution order:

1. Build and pass the falling-ball scene first.
2. Add boundary and neighbor-cell probes.
3. Add candidate-pair scenes.
4. Add two-ball collision scenes.
5. Add ball/box and box/box collision scenes.
6. Add sleeper wake and resting-contact scenes.
7. Run larger solver stress scenes, including `physics_bench_varied.scene`.
8. Run legacy run mode last.

Each step should leave a short measurement note. When the broadphase improves
milliseconds or pair counts, record a 🟢 ms/% line before moving to the next
step. Use 🟠 for mixed results and 🔴 for regressions.

## Phase 17 - Validation Strategy

After the first renderer work:

```text
vsync on
OpenGL shader compilation validation
OpenGL debug line visibility check
first cube-probe scene check
```

After occupancy logic:

```text
vsync on
physics validation
manual falling-ball visual check
manual boundary visual checks
profiler baseline captured
```

After candidate pair generation:

```text
vsync on
physics validation
candidate count comparison
known-collision pair presence check
🟢 pair-count reduction note when applicable
```

After replacing all-pairs collision:

```text
vsync on
physics validation
deterministic fixed-step scenes
before/after behavior comparison
🟢 ms/% speed-up note when applicable
```

After box and sleep integration:

```text
vsync on
physics validation
mixed-shape scenes
sleep/wake scenes
debug visuals forced on in every scene
```

After no-allocation optimization:

```text
vsync on
physics validation
performance validation
allocation/high-water diagnostics
all visual probes repeated
🟢 ms/% speed-up note required if measured improvement appears
```

After completion integration:

```text
vsync on
full validation
OpenGL overlay validation
physics determinism validation
performance validation
large solver stress scene such as physics_bench_varied.scene
legacy run mode
```

## Failure Table

| Symptom | Likely Cause | Fix |
|---------|--------------|-----|
| Ball visibly crosses boundary but neighbor cell does not light | Used center cell only | Insert the full radius AABB range. |
| Negative positions light positive cells | Integer truncation | Use `floor` for cell conversion. |
| Red cells appear before impact | Candidate cells treated as collision cells | Record red keys only after narrowphase contact. |
| Collision disappears after broadphase integration | Candidate pair missed | Inspect boundary occupancy and radius. |
| Collision timing changes | Pair order or time-remaining logic changed | Keep narrowphase body identical; stabilize pair order. |
| Debug visuals change simulation | Visualizer mutates physics data | Copy data out; keep overlay read-only. |
| Box collisions are missed | Box radius too small | Use distance from box center to farthest corner. |
| Sleeping objects never wake | Sleepers not inserted into grid | Insert sleepers; skip only sleep/sleep collision handling. |
| Broadphase is slower than all-pairs | Cell size too small or storage allocates | Tune size; move to fixed-capacity storage. |
| Candidate count equals naive count | Cell size too large or scene too clustered | Tune cell size; inspect max cell occupancy. |
| Optimized grid differs from simple grid | Storage rewrite changed behavior | Re-run visual ladder and compare cell lists. |
| Pair duplicates happen | Missing dedup or pair normalization | Normalize and use triangular bitset. |

## Final Done Definition

The system is complete when:

1. Broadphase development and validation scenes force on a world-space wireframe
   spatial grid.
2. The grid cell size is configurable and defaults to the tuned value.
3. A falling ball visibly lights the cells predicted by
   `floor((position +/- radius) / cellSize)`.
4. Face, edge, corner, and negative-coordinate probes behave correctly.
5. Candidate pairs are generated from shared cells and deduplicated.
6. The baseline all-pairs object loop is replaced by broadphase candidate pairs.
7. Narrowphase remains authoritative.
8. Red cell coloring is driven only by actual narrowphase contact/collision.
9. Spheres and boxes both participate using conservative broadphase radii.
10. Sleeping objects remain inserted so wake-up behavior still works.
11. Resting/contact solver generation reuses the broadphase candidate list.
12. Pair generation is deterministic enough for fixed-step validation.
13. The optimized grid uses fixed-capacity storage, generation stamping, active
    bucket iteration, and bitset pair deduplication.
14. The overlay is OpenGL-only for this implementation and works reliably in
    the OpenGL path.
15. The permanent visual test ladder exists and documents expected visual
    progress.
16. Larger solver stress scenes, including `physics_bench_varied.scene`, have
    been run after the smaller visual ladder.
17. Legacy run mode has been run last.
18. Profiler notes include 🟢/🟠/🔴 ms/% markers for measured speed-ups, mixed
    results, and regressions.
19. Renderer, physics, performance, and full validation all pass.
