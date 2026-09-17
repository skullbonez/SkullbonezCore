**SkullbonezCore — source-code quality evaluation**

18 September 2026 · Static review of the working tree in `C:\SkullbonezCore`.

**Verdict**

Yes: this is good code, and substantial parts demonstrate professional systems engineering. The strongest evidence is the handling of identity, object lifetime, deterministic simulation, GPU resource retirement, and behavioral tests. Those qualities are present in executable logic, not just in comments describing intentions.

The qualification is that the codebase is uneven. Its lower-level components often have clearer boundaries than its application coordination. There is considerable implementation complexity, and some external-input checks are incomplete despite extensive defensive infrastructure. I would be comfortable investing in this codebase. I would prioritize reducing the cost of changing it and closing the input-validation gaps before expanding its scope further.

I would not recommend a rewrite. Nor would I describe it as a uniformly mature, production-proven engine on the evidence available here. It is a capable engine and application codebase with strong engineering foundations and material maintenance debt.

| Question | Assessment |
|---|---|
| Is it professional? | Yes in many concrete implementation practices; consistency needs improvement. |
| Is it technically any good? | Yes. The simulation, lifetime management, and test design show meaningful technical substance. |
| Is it easy to maintain? | Mixed. Focused components are approachable; application and replay coordination require substantial context. |
| Is defensive programming effective? | Often, but external numeric validation and replay loading have identifiable gaps. |
| Are the tests valuable? | The sampled tests are valuable and check behavior beyond successful execution. Their current pass status was not assessed. |
| Is it fast, stable, and ready to ship? | Not established by a source-only review. |

**Scope and evidence**

Only source files informed this assessment. I did not read READMEs, plans, previous audits, session notes, build logs, configuration data, or recorded results. Source comments were treated as claims to compare with implementation, not as independent proof. Git metadata and file inventories established the review context; CodeGraph provided an initial source map, followed by direct source inspection.

The checkout reported branch `nightrunner-17th-SEP-26`, with HEAD `c686f653bb4c0102daa1132b5fe171d0ccf4c816`. Many source files already had staged, unstaged, or untracked changes. This report assesses the observed working tree, including in-progress work, rather than attributing every observation to that commit. References and counts describe the source observed during this review and can move with later edits.

The source inventory contained 695 files and 278,770 raw lines under `SkullbonezSource`; 84 files and 53,473 lines under `SkullbonezTests`; and four C++ source files and 4,186 lines under `Agentic/Tests`. Raw lines include comments and blanks and are not a measure of quality or test coverage. Shader and tool source were also inventoried, with selected implementations inspected.

This was a selective architectural and implementation review, not a line-by-line audit of all those files. Detailed samples covered Core results, allocation, workers and persistence; physics identity, stepping, contacts and convex distance; simulation pacing; DX12 frame and resource lifetime; render graph compilation; scene parsing; replay serialization and prediction scheduling; application composition; UI value boundaries; assets; terrain creation; shader code; and related tests.

No build, tests, application sessions, benchmarks, or runtime reproductions were executed. The findings below distinguish directly visible omissions from their inferred consequences. No existing source was edited.

**What earns the positive assessment**

1. **Identity is handled as a real correctness problem.** Physics bodies and colliders have typed index/generation handles, while dense-row hints are explicitly separate. More importantly, the tests destroy a middle body, verify the moved body's identity, reuse the vacant handle slot, and check that the old generation stays invalid. This is useful protection against subtle editor, replay, and storage-compaction bugs. See [PhysicsHandles.h](../../SkullbonezSource/Physics/PhysicsHandles.h), lines 58–115, and [TestPhysicsHandles.cpp](../../SkullbonezTests/TestPhysicsHandles.cpp), lines 553–603.

2. **Lifetime and synchronization receive deliberate treatment.** Worker fork/join dispatch waits before stack-owned callback state expires, and nested work on an existing worker runs inline. Prediction destruction waits for outstanding work, and promotion joins the worker before transferring the published frame storage. DX12 retirement checks covering fence completion or an explicit no-outstanding-work condition before releasing resources and descriptors. These are substantive safeguards. See [WorkerPool.h](../../SkullbonezSource/Core/WorkerPool.h), lines 252–296; [ReplayPredictionScheduling.cpp](../../SkullbonezSource/Runtime/Prediction/ReplayPredictionScheduling.cpp), lines 175–226; and [Dx12DeferredReleaseOwner.cpp](../../SkullbonezSource/Rendering/DX12/Dx12DeferredReleaseOwner.cpp), lines 49–117. This is positive source evidence, not a claim that every concurrency path is race-free.

3. **The numerical code shows care beyond the ordinary case.** Quaternion normalization handles both degenerate values and overflow when squaring large finite components. Convex-distance calculation translates into one body's local frame to reduce precision loss and bounds its iteration count. Simulation pacing caps catch-up work, preserves fractional time, and reports dropped ticks. See [Quaternion.cpp](../../SkullbonezSource/Maths/Quaternion.cpp), lines 64–112; [ConvexDistance.cpp](../../SkullbonezSource/Physics/ConvexDistance.cpp), lines 245–304; and [SimulationSystem.cpp](../../SkullbonezSource/Runtime/Simulation/SimulationSystem.cpp), lines 93–204. These choices support a judgment of competence; they do not establish accuracy across all scenes.

4. **Several tests have meaningful independent expectations.** Contact tests check momentum and energy bounds across restitution values. They also deliberately inject excessive impulses and stale geometry and require the checks to reject them. Replay testing restores an earlier state and compares the future it produces against uninterrupted execution. Scheduler tests verify what happens on the frame after a hitch, not just the capped tick count during the hitch. See [TestPersistentContactSolver.cpp](../../SkullbonezTests/TestPersistentContactSolver.cpp), lines 824–867 and 988–1025; [TestReplayDeterminism.cpp](../../SkullbonezTests/TestReplayDeterminism.cpp), lines 367–398; and [TestSimulationSystem.cpp](../../SkullbonezTests/TestSimulationSystem.cpp), lines 275–344. This is stronger evidence than test-file volume alone.

5. **Some failure paths are properly transactional.** Atomic file writing creates an exclusive temporary sibling, handles partial writes, flushes and closes it, then replaces the destination. Scene parsing publishes the candidate only after its failure checks. Terrain creation retains a candidate until the necessary construction steps succeed. See [AtomicTextFileWriter.cpp](../../SkullbonezSource/Core/AtomicTextFileWriter.cpp), lines 69–208; [AuthoredSceneParser.cpp](../../SkullbonezSource/Scene/AuthoredSceneParser.cpp), lines 779–823; and [Terrain.cpp](../../SkullbonezSource/World/Terrain.cpp), lines 168–251. These patterns deserve preservation and wider consistency.

6. **Useful boundaries exist in actual types.** UI capture intent is a detached value, and the draw list stores bounded presentation commands with observable overflow diagnostics. Render graph compilation represents transitions and lifetimes separately from device execution. The compact, `[[nodiscard]]` result type has explicit copy/move lifetime semantics. See [UIInputCaptureIntent.h](../../SkullbonezSource/UI/UIInputCaptureIntent.h), lines 20–28; [UIDrawList.cpp](../../SkullbonezSource/UI/UIDrawList.cpp), lines 291–369; [RenderGraph.cpp](../../SkullbonezSource/Rendering/RenderGraph.cpp), lines 542–650 and 831–895; and [SbResult.h](../../SkullbonezSource/Core/SbResult.h), lines 61–88. The implementation is not simply one undifferentiated engine object.

**Findings and weaknesses, in recommended action order**

**1. Scene numeric validation is inconsistent at the input boundary.**

Priority: high for correctness hardening. Confidence: high in the missing validation; downstream runtime effects were not reproduced.

`ReadFloat` verifies only that a JSON value is numeric before calling `get<float>()`. `ReadVec3` uses that helper and checks the parser failure flag, but adds no finite-range validation. Ball parsing validates radius, mass, moment, and restitution separately while leaving position, force, and Euler components without the corresponding check. World gravity and fluid values likewise pass through `ReadFloat` without a general finite check in that parsing path.

Evidence: [AuthoredSceneParserSchema.h](../../SkullbonezSource/Scene/AuthoredSceneParserSchema.h), lines 638–648 and 864–875; [AuthoredSceneParserBodies.cpp](../../SkullbonezSource/Scene/AuthoredSceneParserBodies.cpp), lines 67–125; and [AuthoredSceneParserRuntime.cpp](../../SkullbonezSource/Scene/AuthoredSceneParserRuntime.cpp), lines 367–385.

An otherwise valid scene containing `"position": [1e100, 0, 0]` reaches a conversion from a JSON number outside the finite `float` range without a range rejection. On the usual IEEE floating-point conversion path this can produce infinity. The important source-level defect is that the parser does not reject that input before narrowing and publishing it. Invalid coordinates or forces can then contaminate calculations that assume finite values; the particular visible symptom requires reproduction.

The existing tests already cover overflow for a material mode and invalid physical dimensions, so the project has a useful pattern to extend: [TestSceneParserUnit.cpp](../../SkullbonezTests/TestSceneParserUnit.cpp), lines 811–851 and 1009–1016.

Recommended change: establish a checked finite numeric conversion before narrowing, then retain field-specific rules for positive values and intervals. Add cases for positions, velocities, forces, camera values, and world parameters; require a diagnostic and preservation of the caller's existing scene on failure.

**2. Replay loading has no practical input-memory budget before allocation.**

Priority: medium. Confidence: high in the allocation path; an out-of-memory failure was not induced.

`LoadBinaryFile` checks whether file length fits the platform's size types, then resizes a byte vector to the entire file length. Header validation happens later. There is no application-level byte limit before this allocation, so a very large invalid file can consume substantial memory before being rejected. Decoded vectors add further memory requirements. In the supplied allocation implementation, failure of ordinary global `new` reaches an abort path.

Evidence: [ReplayV2Artifact.cpp](../../SkullbonezSource/Runtime/Replay/ReplayV2Artifact.cpp), lines 900–936 and 3152–3168; [RuntimeAllocationTracker.cpp](../../SkullbonezSource/Core/Allocation/RuntimeAllocationTracker.cpp), lines 627–660 and 961–968.

The count-versus-payload checks inside the codec are good and prevent a different class of malformed-count problems. They do not bound the initial whole-file allocation or the total decoded footprint.

Recommended change: validate a fixed-size header before loading the whole file, define supported file and decoded-memory budgets, and reject excess input with a recoverable diagnostic. If large captures are a supported use case, use bounded or incremental reads. Add a test proving rejection occurs before a large allocation.

**3. The replay duplicate-tag test does not prove tag uniqueness.**

Priority: medium. Confidence: high from the reader and test control flow; no crafted replay was executed.

`ReadChunkTable` validates individual ranges but never rejects duplicate IDs. `FindChunk` returns the first matching entry. A valid required chunk followed by a duplicate can therefore be ignored rather than rejected.

The existing test changes the `MANI` entry into a `BODY` entry. The writer places `MANI` before the real `BODY`, so the loader first attempts to parse manifest bytes as a body dictionary. That can reject this particular fixture without implementing a uniqueness rule at all. The test's successful rejection would not establish the property its name suggests.

Evidence: [ReplayV2Artifact.cpp](../../SkullbonezSource/Runtime/Replay/ReplayV2Artifact.cpp), lines 939–1021 and 2789–2802; [TestReplayArtifact.cpp](../../SkullbonezTests/TestReplayArtifact.cpp), lines 522–528. `CheckRejected`, at lines 262–267, only observes load failure and an empty output.

Recommended change: enforce uniqueness for singleton chunk IDs in the table reader. Test valid duplicate payloads, duplicate placement both before and after the original, and optional as well as required singleton chunks. This is a small, concrete example of why test names and test counts cannot substitute for inspecting the assertion's cause.

**4. Application coordination retains too much feature-specific responsibility.**

Priority: high for long-term maintainability. This is an architectural judgment, not a demonstrated functional bug.

`Run` owns the expected process-level components, but it also retains detailed comparison-workspace state, camera flags, editor state, prediction startup state, and feature-specific command methods. Its methods have access to the full collection of owners. The automation entry points pass many of those owners through long handler chains. Separating these definitions into multiple `.cpp` files does not limit their access to shared state.

Evidence: [Run.h](../../SkullbonezSource/Runtime/App/Run.h), lines 164–254 and 374–405; [InteractionAutomationApplication.cpp](../../SkullbonezSource/Runtime/App/InteractionAutomationApplication.cpp), lines 5231–5335. The application package contained 50 source files and 43,379 raw lines; 35 production `.cpp` files exceeded 1,500 lines. These counts indicate review scope, not automatic defects.

The central frame loop itself is reasonably readable and sequenced: [RunFrame.cpp](../../SkullbonezSource/Runtime/App/RunFrame.cpp), beginning at line 1151. The difficulty is how much feature knowledge its coordinator and adjacent routines retain. A small behavior change can require understanding input arbitration, replay transport, scene replacement, camera behavior, and rendering publication together.

Recommended change: extract cohesive feature state and transitions when touching those workflows, starting with a clearly bounded owner such as the comparison workspace. Give operations the values and capabilities they need. Judge improvement by reduced knowledge and state access required for a change, not by shorter files or additional wrapper classes.

**5. Custom infrastructure and explanatory overhead raise the learning cost.**

Priority: medium. This is a design tradeoff, not a recommendation to remove safety mechanisms.

The codebase asks maintainers to understand a custom result lease store, allocation phases, fixed-capacity queues, publication prefixes, explicit phase transitions, and several layers of frame values. Much of that is justified by the engine's timing and lifetime requirements. However, the cost is real, especially when business logic and coordination still remain broad.

For example, copying an `SbResult` is a reference-counted lease operation against an external store. The store has 256 entries and terminates on exhaustion; it must outlive every failure result. Those are important operational constraints for what otherwise looks like a small return value. See [SbDiagnosticStore.h](../../SkullbonezSource/Core/SbDiagnosticStore.h), lines 48–101, and [SbResult.cpp](../../SkullbonezSource/Core/SbResult.cpp), lines 221–273.

Comments often explain important units and hazards well, as in the convex-distance calculation. Elsewhere long file preambles repeat purpose, glossary, invariants, and related-file lists before relatively small implementations. The result can be more reading without a corresponding reduction in the state a maintainer must understand. This judgment comes from the source itself, including the worker, result, and prediction headers sampled here.

Recommended change: preserve comments explaining non-obvious lifetime, ordering, numerical, and capacity decisions. Prefer enforcing a rule in a narrow API over repeatedly describing it. Add new infrastructure only when it removes a demonstrated source of complexity or satisfies a concrete runtime requirement.

**Where I would put the next effort**

1. Close the scene numeric-conversion gap with focused negative tests and unchanged-output assertions.
2. Bound replay input and decoded memory, enforce singleton chunk uniqueness, and strengthen the duplicate tests so the intended check is the reason they fail.
3. Reduce one application workflow's shared-state reach, preserving its behavior with focused tests. Use that result to decide whether further extraction helps.
4. Keep the strongest existing habits: stable identity, transactional publication, explicit worker/GPU lifetime, and tests based on physical or behavioral expectations.

I would assess future progress through concrete outcomes: fewer malformed inputs reaching live state, recoverable failures instead of resource exhaustion, fewer owners involved in a localized change, and tests that fail for the intended reason. More code, more abstractions, and more assertions would not by themselves establish improvement.

**Limits on the verdict**

The source supports a positive judgment about engineering quality and a qualified judgment about maintainability. It does not establish frame rate, memory use under real workloads, visual polish, compatibility across hardware, current build health, actual test coverage, long-run stability, or release readiness. Those would need separate execution-based evidence. Nothing in this report claims that the existing tests passed during this review.
