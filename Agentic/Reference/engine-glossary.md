# Engine Glossary

This reference owns the canonical meaning of vocabulary defined by more than
one tracked Skullbonez source file. It is a documentation boundary, not a C++
dependency: source learning headers cite this file from `Related:` instead of
copying a shared definition.

A term defined in exactly one tracked `.cpp`, `.h`, `.hpp`, `.inl`, or `.hlsl`
file stays in that file's `Glossary:` block. Once the same exact term is
defined in more than one tracked source file, this glossary owns its single
definition and the source copies must be removed. Counts describe current
structure; they are not thresholds, budgets, or permission to retain copies.

These 321 definitions are owner adjudications. Terms are distinguished by exact
spelling and capitalization, so case-distinct terms remain distinct until an
owner explicitly unifies them. Run `python tools/inventory_glossary_terms.py
--repo .` to re-derive the current multi-file definition set.

| Term | Canonical definition |
|---|---|
| AABB (Axis-Aligned Bounding Box) | Box aligned to world axes, often used for cheap broadphase overlap tests. |
| ABI (Application Binary Interface) | The compiled binding contract between C++ root parameters, shader registers, and draw-time texture slots. |
| Acceptance ledger | Detached facts describing commands accepted this frame. |
| Accumulator | Stored fractional tick state that carries time across frames. |
| Active cell | Occupied persistent or swept-overlay grid cell in the latest committed physics step. |
| Active rotation | Rotation that moves a vector in a fixed world basis. |
| Active vortex | An authored vortex after spawn, growth, shrink, drift, and pair-repulsion have been evaluated at the current gameplay time. |
| All-body trajectory | Mutual-gravity path record retained for every body, independent of the contact-derived future tree. |
| Amortized build | Bounded worker slices spread prediction work across frames. |
| Antiparallel normal | A terrain normal pointing exactly opposite world up. |
| Artifact | File written by runtime tools, diagnostics, captures, or saves. |
| Artifact path | Validation-facing output path that must stay stable. |
| Asset affiliation | Library/asset/instance/part provenance kept separately from behavior grouping. |
| Asset primitive | Single spawned collision body inside a placeable asset container, such as a box, sphere, or convex hull. |
| Asset system | Runtime-owned registry that resolves editor asset-library names without querying process-global state. |
| Authored hull | Baked convex hull asset used for editor-placeable collision geometry and preview outlines. |
| Authored path colour | Scene material base colour reused by orbital guide and predicted trajectory ribbons. |
| Authored scene | Parsed `.scene.json` data that explicitly drives runtime setup. |
| Authoring row | Cold scene round-trip text paired with one hot collider row. |
| Auto-cycle | Screenshot automation that advances capture targets over time. |
| Automation scene | Scene with screenshot/perf/exit behavior that should keep the UI hidden unless explicitly authored otherwise. |
| Awake index list | Ascending dense body rows owned by the sleep controller and borrowed by work-producing stages for one sequenced fixed-step interval. |
| Awake slot | Dispatch position mapped to one ascending dynamic body index. |
| Back buffer | Swap-chain image that will be presented to the window. |
| Backdrop | Translucent panel drawn before chrome to separate controls from the world view. |
| Billboard | Camera-facing quad built from a world-space segment and view direction. |
| BLAS (Bottom-Level Acceleration Structure) | Raytracing spatial index for one mesh's triangles. |
| Body | Simulated object state such as pose, velocity, mass, and sleep flag. |
| Body record | Physics-owned snapshot of pose, velocity, mass, and inertia used by the joint solver. |
| Body simulation limit | Scalar cap enforced by a body before solver rows see velocity state. |
| Body store | Physics-owned live body records used for pose and velocity authority while legacy object-record mirrors are retired. |
| Broadphase | Cheap collision pass that finds object pairs worth testing more precisely. |
| Buoyancy | Upward force from displaced fluid volume; depends on gravity, fluid density, and submerged volume. |
| Callback bridge | The process-local state that lets Win32 callbacks enqueue mouse data until the frame loop consumes it. |
| Candidate | Absolute live-world linear velocity requested for the ship. |
| Candidate pair | Broadphase-selected body pair awaiting narrowphase testing. |
| Canonical pair order | Ascending normalized `(minIndex, maxIndex)` order, independent of cell-bucket discovery history. |
| Canonical publisher | The single claimed list instance allowed to mutate one conceptual owner's capacity row; copies and same-name clones remain silent. |
| Capacity bytes | Vector storage already reserved for records or point arrays. |
| Capacity row | Fixed registry storage carrying one store's live sizing telemetry without building a heap-backed report. |
| Capacity session | One loaded scene's live-usage window, ending immediately before its store rows are cleared or replaced. |
| Capacity snapshot | Fixed value rows copied from the allocator registry only while the Memory tab is visible. |
| Capture owner | Concrete DX12 component that supplies screenshot readback. |
| Capture result | Value outcome folded into the fixed accepted-request batch. |
| Capture state | Window drag, resize, slider, and native-mouse ownership that can span multiple frames. |
| Cause row | One body, contact, solver, or prediction explanation in the replay causality tree. |
| Cause tree | Contact, solver-row, and predicted-motion graph explaining replay body influence. |
| Cause window | Resizable replay inspection panel that lists body/contact rows. |
| CBV (Constant Buffer View) | Descriptor row used when shaders read a packed block of constants. |
| CCD (Continuous Collision Detection) | Swept collision test that asks whether objects hit during a tick, not only where they end the tick. |
| Cinematic deck | A queue of concept/cinematic scenes cycled as one authored visual look set. |
| Cinematic override | Bitmask-selected render fields layered over defaults. |
| CLI (Command-Line Interface) | Text arguments or scripts used to launch validation and tooling paths. |
| Cold flush | Submit/wait/reset retry allowed outside steady gameplay when an upload reservation does not fit. |
| Collider | Shape metadata used to decide what precise collision test applies. |
| Collider authoring row | Cold material text paired with the live collider row for exact scene round trips. |
| Collider descriptor | Value packet carrying parsed shape and contact material facts into the physics collider store. |
| Collider store | Physics-owned shape, material, and radius records paired with body handles. |
| COM (Component Object Model) | Windows interface lifetime model used by DX12 through reference-counted objects. |
| Commit count | Number of fixed physics ticks the runtime owner must execute after the scheduler has updated accumulator state. |
| Compiled transition | Render-graph state edge assigned to a specific pass and resource before callbacks record live commands. |
| Contact body view | Pose-only body input used by narrowphase so the manifold builder does not need to borrow unrelated owner storage. |
| Contact highlight | Render-only feedback alpha for red fixed-body hits. |
| Contact policy | Terrain and contact thresholds owned by PhysicsEngine so existing and newly added models receive the same physics policy. |
| Contact release | Editor/authored behavior that lets fixed decoration become dynamic after a large impact. |
| Contact row | Solver constraint row used to apply impulses at a contact point. |
| Contact sweep | Conservative object/object time-of-impact query used before exact manifold generation and solver response. |
| Content signature | Hash of UI-visible values used to invalidate cached draws. |
| Control surface | Fixed-capacity per-frame table shared by scrubber hit testing and, in later phases, drawing. |
| Convergence trace | Bounded per-iteration attribution for the solver's squared-impulse stopping metric. |
| Convex hull | Collision shape made from a closed convex set of authored points. |
| Covering fence | Queue counter proving all earlier GPU references are finished. |
| Cross-scene pause lock | Scene-owned fact that forces step-held physics even when the active camera or tool would normally keep simulation running. |
| CSV (Comma-Separated Values) | Text table format used for byte-exact physics regression output. |
| Debug build | Configuration where validation asserts are active. |
| Dense row | Compact store array index used by hot simulation scans. |
| Descriptor | Small binding record that tells the GPU or output-merger how to interpret a resource. |
| Descriptor heap | DX12 table of descriptor rows; shader-visible heaps can be indexed by GPU commands. |
| Development tool owner | A thread-local, hard-capped ImGui or Tracy scope that is permitted only when the shared development capability is compiled. |
| Development UI command | Fixed presentation or native-window request emitted by the sequencer and applied synchronously by this automation owner. |
| Diagnostic-name table | Fixed pointer table whose pointed-to scene names remain owned by stable scene metadata. |
| Diagnostics artifact | File produced for validation, profiling, or analysis. |
| Diagnostics view | Synchronous spans and references into one PhysicsEngine. |
| Director playback | Runtime camera mode that applies authored shot-list poses plus optional phase styles and prediction reveal pacing. |
| Draw command | Lightweight record describing a UI shape or text batch to render later in the frame. |
| DRED (Device Removed Extended Data) | DX12 diagnostic report for GPU device loss, breadcrumbs, and page-fault clues. |
| DSV (Depth Stencil View) | Descriptor row used when the GPU reads or writes depth/stencil data for depth testing. |
| DTO (Data Transfer Object) | Plain value record passed across a subsystem boundary so the receiver can serialize data without owning the source. |
| Durable artifact | Saved replay payload reloaded to prove report facts survive the writer/reader boundary. |
| DXR (DirectX Raytracing) | DX12 API used for hardware ray traversal and reflection dispatch. |
| Early-exit probe | Bounded validation or generation mode that does not enter the application run loop. |
| Editor command | Intent emitted by a widget and applied later by runtime code. |
| Engine module | A source file with one focused responsibility inside the SkullbonezCore runtime. |
| Event cursor | Monotonic sequence marker stored on checkpoints so restore can resume timeline events without replaying old side effects. |
| Fault injection | Debug-only synthetic failure used to prove that queue work stops before the first unsafe submission. |
| FBO (Framebuffer Object) | Engine shorthand for an off-screen render target exposed through the renderer abstraction. |
| Feature ID | Deterministic contact key used to match rows across frames for warm starting. |
| Fence | GPU/CPU synchronization counter used to prove submitted command work has completed before memory is reused. |
| Fixed-step | Deterministic mode that advances physics by one fixed delta per requested tick instead of wall-clock time. |
| Fixed-tree release | Store-owned command that turns authored fixed props into dynamic bodies and wakes same-tree parts after an accepted impulse. |
| Fluid surface | World-space Y plane where the fluid medium begins. |
| Fluid surface adjustment | Typed signed velocity issued by input in world units. |
| Flyout | Secondary variant row anchored to one palette entry. |
| FNV (Fowler-Noll-Vo) | Small string hash used here to identify stable scope paths without storing dynamic lookup tables. |
| FNV-1a | Small deterministic hash used only to prove each surface implementation consumed the same frame values; it is not durable identity or serialization. |
| Focus mask | Dense frame-local rows faded around the selected path family. |
| Force frame | Ordered cylindrical field values plus per-body timer spans borrowed by Physics for exactly one fixed tick. |
| Fork-join | Pattern where the main thread splits work, workers run chunks, and the main thread waits before merging results. |
| Frame publication | One-time projection of owner-backed rows and values for synchronous render-pass consumption during the current frame. |
| Freshness manifest | Checked-in JSON map from compiler inputs to baked bytes. |
| Future node | Causal topology row naming the predicted body, parent, activation frame, contact evidence, and depth that make a child path visible. |
| Generated scene | Runtime-created demo scene with deterministic cameras and model placement. |
| Generation | Reuse identity carried with a slot or handle so stale references can be rejected. |
| Geometry owner | Renderer owner borrowed while creating or destroying debug vertex and instance buffers. |
| Gesture | Active pointer operation that owns capture until it ends. |
| Ghost request | Typed predicted pose and material treatment consumed by the ordinary object-shape renderer. |
| Gizmo | World-space editor axes or rotation rings used to transform selected models. |
| Glyph advance | Horizontal distance added after laying out one character. |
| GPU (Graphics Processing Unit) | Hardware device that owns renderer resources such as meshes, shaders, textures, and reflection targets. |
| GPU drain | Ordered close, submit, fence wait, and command-list reopen that must finish before a runtime owner destroys resources. |
| GPU timing sample | Completed renderer measurement submitted as a value keyed by the Core-owned marker hash. |
| Graphics stress | Deterministic fuzzer that mutates render settings, UI state, and scene loads to reproduce DX12 lifetime or resource bugs. |
| HDC (Handle to Device Context) | Win32 drawing context associated with the window. |
| HDR (High Dynamic Range) | Floating-point scene color that can hold values brighter than display white until tonemapping resolves it. |
| Heat | Per-cell collision count used only to darken the debug color. |
| Hit box | Screen-space rectangle used to decide whether mouse input targets a widget. |
| Hitch event | A fixed-step request whose whole-tick demand exceeds the per-frame catch-up cap; excess whole ticks are intentionally discarded. |
| Hold mode | Press-duration gesture that opens tree or ragdoll variants. |
| Hot body fields | Physics-owned arrays holding fixed/sleep/velocity state for the current tick. |
| Hot control | Pointer control when that row is enabled. |
| Hot reload | Explicit developer action that reruns the offline bake, then asks live shader owners to adopt hash-verified bytes transactionally. |
| HUD (Heads-Up Display) | On-screen diagnostics and control overlay. |
| Hull identity | Cold normalized authored path plus exact canonical scale bits. |
| Hull scale | Per-axis size multiplier for convex hull editor assets. |
| HWND (Window Handle) | Win32 identifier for the native application window. |
| Input edge | Transition from not pressed to pressed, used for one-shot commands. |
| Input turn | Ordered frame interval that samples hardware, offers actions to UI/tools/replay, and commits accepted capture/default/scene requests. |
| Input turn result | Value-only process request emitted after semantic actions are interpreted; Run applies process-wide policy without rescanning input. |
| Instant build | One worker submission that completes the remaining horizon. |
| Interaction owner | Concrete owner of persistent UI controls and cross-frame pointer/capture state; it emits typed command values rather than mutating runtime subsystems. |
| Interaction signature | Hash of pointer/focus state used to invalidate hit data. |
| Intercept assertion | Lane P proof over the replay-owned closest-approach snapshot; it observes distance, ETA, and contact without owning the scan. |
| JSON (JavaScript Object Notation) | Text metadata format used inside the manifest chunk. |
| Lane F | Fatal invariant lane for should-never-happen owned engine state; it records diagnostics and does not return. |
| Lane P | Bounded validation or probe-result lane; it reports proof evidence and is not production error handling. |
| Lane R | Recoverable error-handling lane for external input or environment failure, represented by an owner/message result. |
| Lane R result | Recoverable owner/message result for external input or environment failure, reported without exceptions or fatal termination. |
| Lifecycle generation | Monotonic identity for one accepted scene-load attempt, independent of scene index or successful activation. |
| Live edge | The newest retained replay sample. |
| Live graph | Production callback schedule accumulated across the frame. |
| Live style | Control-folder protocol that applies style JSON and requests a screenshot without restarting the process. |
| Load preparation | Failure-safe phase before teardown and object population. |
| Load request | Accepted navigation result containing an optional scene load and whether the runtime should become interactive first. |
| Manifold | Set of contact points and normals describing one colliding pair. |
| Marker epoch | Core identity generation advanced when the registry resets. |
| Material intent | Renderer-neutral description of surface style and texture selection. |
| Material table | Fixed t4 texture storing default material response values by material kind for the current object shader. |
| Memory waterline | Compact F6 overlay that tracks known engine memory and pinned reserve-growth events without polling process memory. |
| Mini palette | Compact editor placement surface shown while UI is minimized. |
| Model capacity | Active object capacity limit. |
| Model frame view | Borrowed render, physics, debug, and policy facts whose lifetime ends before the next frame begins. |
| Model row hint | Caller-owned cached dense-row guess that must be repaired or invalidated against stable identity before use. |
| Mutual-gravity pair scratch | Preallocated triangular force table whose unique slots let workers compute pairs without racing or regrouping additions. |
| Narrowphase | Precise collision pass that computes contact points, normals, and penetration. |
| Numbered path | Prefix plus sequence number chosen to avoid overwriting an existing artifact. |
| OBB (Oriented Bounding Box) | Box with rotation, used for exact object-space collision tests. |
| Operator-owned state | Live runtime choice made after scene load. |
| Orthogonal basis | Three perpendicular unit axes; its transpose is also its inverse. |
| Overlay state view | Read-only replay publication borrowed for one late pass. |
| Overlay viewport | Coupled pixel width and height used by overlay layout; the render-command target remains an explicit synchronous borrow. |
| Override mask | Bitfield that records which optional JSON fields were authored so unspecified values keep engine.cfg defaults. |
| Owner | The tool or subsystem currently allowed to consume world input. |
| Owner event | Stable wire-coded record of accepted owner work. |
| Owner view | Three synchronous const store references plus Gameplay byte values projected by SceneWorld. |
| Pair island | Candidate pairs connected through shared body indices. |
| Pair-source cell | Current-generation cell reached by an awake body; dormant membership remains resident even when the cell is not visited this step. |
| Pair-source stamp | Frame generation marking a cell reached by an awake body; production candidate collection skips unstamped sleep-only cells. |
| Parent directory | Folder portion of a requested output path. |
| Pending awake queue | Fixed-capacity worker publication rows folded into the sorted owner list at sequencer barriers. |
| Perf log | CSV-style runtime performance artifact written during runs. |
| Persistent contact | Solver row retained long enough to warm-start a matching contact feature on the next fixed tick. |
| Persistent membership | Cell occupancy retained across fixed steps until a body's integer cell range changes. |
| Persistent tail | Fixed suffix excluded from ordinary frame resets so retained GPU geometry can reuse cold-created upload memory across frames. |
| PGS (Projected Gauss-Seidel) | Iterative constraint-solver method used for bounded contact impulses. |
| Phase cursor | Value that permits only the adjacent OC0 phase walk. |
| Physics body handle | Generational id for the picked body-store row. |
| Physics diagnostic command | One-frame key or UI request that changes debug presentation state, not simulation state. |
| Physics material | Runtime policy for collider friction and sphere drag. |
| Physics-debug override | Visualization-only startup request that must not alter solver state. |
| Pick purpose | The tool-specific policy for interpreting a mouse ray. |
| Pipeline cursor | Selected physics pipeline stage rendered by the debug pass. |
| PIX | Microsoft GPU debugger/profiler that can read engine markers and DX12 object names. |
| Placement gesture | Mouse drag and wheel input used to size an object before placement commits. |
| Placement recipe | Typed editor data that describes a tree, house, building, or hull-backed primitive selected from the editor tab. |
| Platform profiler GPU stack | Fixed nesting state that must be closed before any command list is submitted. |
| POD (Plain Old Data) | Simple value type with no ownership or behavior. |
| Point joint | Constraint that keeps two local anchor points close together without yet modelling a full hinge, cone, or motor. |
| Pointer arbitration | Ordered phase cursor that gives the first consuming world-pointer stage exclusive ownership. |
| Pool slot | Compiler-assigned alias bucket for non-overlapping transient lifetimes with matching descriptor needs. |
| Post-step output | Bounded physics facts borrowed synchronously by presentation. |
| Prefix digest | Digest rebuilt from every currently published trajectory point. |
| Prepared prefix | Published rows whose topology and trajectories were brought into coherence by the frame thread for one render pass. |
| Presentation alpha | Bounded leftover accumulator fraction used only to display between the previous and current completed physics poses. |
| Presentation sample | Render-facing pose/state captured from a frame. |
| Presentation state | Operator-selected overlay, water, terrain, and physics debug policy sampled into render values each frame. |
| Presentation track | Body poses, camera, and world display fields used for smooth visual scrubbing. |
| Presented generation | Replacement prefix prepared by the frame thread and therefore safe to compare with the retained prediction. |
| Preview catalog | Renderer-owned frame snapshot that maps the UI's stable catalog index to one current texture handle and its presentation metadata. |
| Private working set | Resident process pages not shared with other processes; matching it requires a page-level OS query. |
| Probe failure | CLI validation failure reported as bounded result/report data so automation exits nonzero without throwing through the frame loop. |
| Proceed policy | Value packet that freezes the sampled step edge and cross-scene pause decision for one frame. |
| Profiler connection snapshot | Three fixed booleans copied from the Tracy owner without a process scan, socket probe, string construction, or growth. |
| Projection | Conversion from the common queue back into established narrow UI command structs consumed by concrete runtime owners. |
| PSO (Pipeline State Object) | Precompiled bundle of shaders and fixed render state that DX12 binds before drawing or dispatching. |
| Publication | Owner-produced save value; SceneWorld's publication borrows its stores only for the duration of this operation. |
| Published prefix | Contiguous completed rows that a reader may consume after the owning publication boundary. |
| Ragdoll part | One model body in the generated simple ragdoll assembly. |
| RayT | Distance along the supplied pick ray to the first shape hit. |
| Readback buffer | CPU-readable landing resource for a GPU texture copy. |
| Record version | Monotonic identity for a replaced record; readers can detect replacement without comparing point arrays. |
| Recording epoch | One reusable command-list lifetime from successful Reset to Close. |
| Render command context | Renderer capability borrowed only while drawing a collision-visualizer frame. |
| Render diagnostics | Renderer capability borrowed to name child draw-trace scopes without reopening global renderer access. |
| Render instance | CPU-side record describing one model's draw transform and material intent. |
| Render pass | A named slice of frame rendering with explicit inputs, outputs, and GPU resource ownership. |
| Render pose | The eye/view/up triple actually used for the current frame; it can differ from the selected camera slot while a tween is active. |
| Replay probe | Debug-only command-line workflow that validates one replay behavior and reports a machine-readable Lane P result. |
| Replay ribbon | Screen-space-width overlay stroke generated from replay path segments, with an analytic edge and optional selected-path halo. |
| Replay target marker | Debug overlay outline/ring drawn around a replay body from live body/collider store values. |
| Replay transfer | Deterministic copy between owned sleep rows and the solver snapshot. |
| Replay visual sample | Compact snapshot of tool visuals restored while replay scrubbing so debug feedback follows recorded frames. |
| Request batch | Ordered fixed-capacity copy drained at one frame checkpoint. |
| Required contact | Named body pair that must touch before automation completes. |
| Reset snapshot | Value-only copy of owner state preserved across same-scene reset. |
| Resource builder | Cold renderer owner borrowed only while compiling the laser shader. |
| Resource context | Creation/rebuild-only render factory bundle used by EnsureGpuResources methods, not by draw methods. |
| Resource state | DX12 usage mode for a resource, such as render target, shader read, copy source, or present. |
| Restitution | Bounce response copied from collider material data into contact views for diagnostics and future solver inputs. |
| Retained draw stream | Fixed-capacity UI command/text storage reused by this pass instead of growing or consuming large nested stack frames. |
| Retained ribbon chunk | Fixed compact segment slice appended by prediction; its physical handle is stable while packet commands sort it canonically. |
| Retention window | Maximum authored duration requested for retained past samples. |
| Retirement quarantine | Fixed queue holding resources or descriptor rows until a covering fence completes. |
| Reveal cursor | Monotonic presentation frame reached by the prediction clock. |
| Ribbon | Thin render strip used for the laser core and glow. |
| Ring buffer | Fixed-size history where new launcher/raycast entries overwrite the oldest slots. |
| Root signature | DX12 binding contract that declares which descriptor tables and constants shaders may access. |
| RTV (Render Target View) | Descriptor row used when the GPU writes color pixels into a texture or back buffer. |
| Run-value directive | Value-bearing Run, replay, UI-stress, or graphics-stress option whose result belongs to the launch-policy packet. |
| RVIS | Ordered packet identity, typed counts, and exact render-buffer rows. |
| RVPD | Bounded typed prediction state used by non-presenting round-trip checks. |
| Save publication | Detached owner-produced value containing that owner's persisted fields; the world publication borrows stable stores synchronously. |
| SBT (Shader Binding Table) | DXR table that maps ray records to ray-generation, miss, and hit shaders. |
| Scale lock | Rule that keeps authored multi-part tree/root proportions stable. |
| Scene browser | UI-facing list of scene files discovered on disk. |
| Scene capacity | Maximum number of model/body/collider rows the runtime can address in one loaded scene. |
| Scene entity | Durable scene-owned identity, display, material, and asset row committed beside the live physics body. |
| Scene object group | Parsed metadata that ties multi-part authored objects, such as releasable trees, to a single root scene object. |
| Scene object id | Stable per-scene physics identity used to correlate one body across dense-row movement, Replay, and diagnostics. |
| Scene queue | Ordered list of authored scene paths, where an empty path means the generated demo scene. |
| Scene request | Deferred load, reset, create, or defaults-save owner intent. |
| Scene session | Current scene state plus controller-owned queue navigation data. |
| Scene-object group | Scene-owned behavior metadata that keeps multi-part editor prefabs, such as releasable trees, tied to one stable root object id. |
| Schema domain | Cohesive authored section translated without creating another scene owner or intermediate model. |
| Screenshot request | Runtime state describing when and where to capture pixels. |
| Scrubber | Timeline control for seeking retained replay frames and future prediction frames. |
| SDF (Signed Distance Field) | Texture representation used for crisp scalable text rendering. |
| SEH (Structured Exception Handling) | Windows process exception mechanism used to capture access violations and similar faults. |
| Semantic action | Fixed ordered input event derived from sampled key edges, independent of the platform's live hardware state. |
| Shader handle | Runtime id that resolves to renderer-owned shader state. |
| Shadow caster stream | Owner-prepared opaque bin selecting one primitive submission path without inspecting material or asset content here. |
| Shared editor view | Frame-owned storage passed to the operator-editor composer and then consumed by the selected development frontend. |
| SkullScope | Structured Physics diagnostic capture and query surface used by validation and tooling. |
| Sky feature | Toggle for sky, clouds, god rays, or volumetric lighting. |
| Sky slider | Focused cinematic parameter slider owned by this tab. |
| Slack | Allowed anchor separation before the solver applies correction. |
| Sleep | Optimization that stops simulating stable bodies until something wakes them. |
| Sleep island | Connected body group that may deactivate only as a unit. |
| Snapshot | Detached value copy that remains safe after the producing owner advances or mutates. |
| Solver object | Exact-count validation object used by deterministic physics scenes. |
| Solver sample | Physics-facing state retained for rollback and diagnostics. |
| Solver snapshot | Physics state retained at a tick boundary for deterministic restore and diagnostic comparison. |
| Sphere cap | Portion of a sphere below the fluid surface; its analytic volume gives a deterministic submerged fraction without sampling. |
| SRV (Shader Resource View) | Descriptor row used when shaders read textures or buffers. |
| Step policy | Once-per-solve normalized view of authored contact bounds used by both object and terrain rows. |
| Sticky failure | First active command-path failure retained until a new device initialization establishes a fresh command-list lifetime. |
| Style scene | Authored scene used as material/cinematic source data. |
| Submission | Conversion of selected replay values into bounded draw commands. |
| Submitted-frame mark | One Tracy frame boundary emitted only after DX12 Present succeeds. |
| Support edge budget | Fixed four-edges-per-body storage ceiling shared by contact and point-joint producers. |
| Surface | Presentation boundary or ordered control surface exposed for one UI or operator domain. |
| Swept overlay | One-step grid coverage of a body's start-to-end path that cannot pollute its persistent current-position membership. |
| Terrain sweep | Continuous collision query against the terrain plane under a body. |
| TLAS (Top-Level Acceleration Structure) | Raytracing spatial index for scene instances that point at BLAS geometry. |
| Topology drift | Temporary mismatch between editor model count and physics store rows after scene/editor construction or deletion. |
| Trajectory lane | Named path category such as past root, future root, child incoming/outgoing, retained trail, or baseline root. |
| Transport command | Presentation-independent record, scrub, prediction, or artifact intent translated by ReplayRuntime into existing replay owners. |
| Tween | Time-based interpolation between camera poses for non-jarring cuts. |
| UAV (Unordered Access View) | Descriptor row used when compute or raytracing shaders write textures or buffers. |
| UI (User Interface) | Runtime controls and overlays drawn over the 3D scene. |
| UI (user interface) | Interactive engine controls evaluated between the input router's pre-UI and after-UI phases. |
| UI options | Optional `ui` block parsed from a `.scene.json` file. |
| UI override | Live Scene/Run-tab value that survives an interactive reset and feeds the next generated-scene rebuild. |
| UI stress | Deterministic diagnostics input churn driven by scene data. |
| UIRect | Pixel-space rectangle shared by hit testing and drawing. |
| Underwater lock | Policy keeping a fully submerged sleeping ball dormant. |
| Underwater sleep lock | Sleep policy that keeps fully submerged balls dormant so buoyancy jitter does not repeatedly wake them. |
| Uniform scale | One shared size value applied to all axes. |
| Upload arena | Frame-scoped CPU-visible staging storage reusable only after the covering GPU fence completes. |
| Upload category | Caller-owned reason for consuming frame upload bytes, used only for attribution and never for allocation priority. |
| UV (Texture Coordinates) | Two-dimensional texture/sample coordinates used by water shaders when perturbing reflection lookup. |
| Velocity drag preview | First-order selected-path estimate retained until the release-triggered authoritative generation commits. |
| Velocity edit | Replay tool that displays and edits linear/angular velocity on the current path target. |
| View model | Read-only presentation snapshot assembled from runtime owners. |
| Virtual key | Win32 integer key code sampled in DeviceInputFrame. |
| Visual-state hash | Digest of presentation-bearing typed values, excluding process-local allocation and budget telemetry. |
| Wake fan-out | Expansion through visual, point-joint, and resting-contact islands. |
| Warmup frame | Completed frame intentionally excluded from profiler stats and perf CSV rows while a scene/pass settles. |
| Widget view | Short-lived typed references to owner-held controls whose bounds are shared by input hit testing and drawing. |
| Win32 | Windows desktop API used for the app window, messages, and process integration. |
| WndProc | Win32 window callback that receives mouse wheel and raw mouse packets before the frame boundary captures input. |
| Worker pool | Persistent thread group that runs bounded jobs outside the main thread. |
| Workspace | Coarse runtime mode such as live, inspect, edit, or replay. |
