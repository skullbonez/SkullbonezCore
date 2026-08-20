# Full Source Comment Truth Audit And Deferred Replacement

Date: 2026-08-20
Status: Registered; 0/6 phases complete; begins after `INVARIANT_HARDENING` IH7.
Snapshot base: `154506e0312e42d1bfa0065fba900b24d8225889`
Impact areas: repository-wide source comments, tests, shaders, and substantial tools
Owner: Repository comment truth with each touched subsystem retaining semantic authority
Priority: Second active queue item; begins after `INVARIANT_HARDENING` IH7
Commit name: `COMMENT_TRUTH`

## Registration And Ordering

The master ledger activates CT0 immediately after invariant hardening and before
`VALIDATION_TIME_AUDIT`. IH4-IH7 remain authoritative for every source and
comment they change. The frozen patch is audit evidence, not a post-IH source of
truth: CT0 must discard a hunk when IH made it redundant, regenerate it when the
implementation or invariant changed, and preserve the current IH wording when
the queued replacement would weaken or contradict it.

Do not apply the frozen patch wholesale. Its strict check now conflicts in
`SkullbonezTests/TestUIDrawValues.cpp` and `SkullbonezTests/TestVector3.cpp`;
seven completed IH selected files and seventeen pending IH selected files also
overlap the queued replacements. A textual three-way application is not
semantic proof. Each overlapping file requires a fresh full-file comment audit
against the post-IH implementation before its checklist disposition changes.

## Outcome

Every one of the 843 currently tracked first-party source-bearing files was
inspected against `Agentic/Skills/comment-style-audit/skill.md`,
`Agentic/Reference/comment-style-guide.md`, and the implementation it describes.
The frozen audit contained 840 files. The three at-rest-stability files added
by the in-flight agent during the audit were read from their live staged versions and
are already compliant.

- 582 files require no replacement and are checked below.
- 261 files contain a ready-to-apply replacement and remain unchecked until
  `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch` is applied.
- 0 files were silently skipped or left without a disposition.
- The patch also updates `Agentic/Reference/engine-glossary.md`.
- Patch SHA-256: `8148588d282fa339d216f266135c2751f6dd1826ac14b51085286d994f5e5e61`.

The patch changes comments and documentation only. It deliberately does not edit the
live source tree while the at-rest-stability agent owns overlapping files.

## Inventory

The inventory is the sorted output of `git ls-files`, restricted to first-party
`.cpp`, `.c`, `.h`, `.hpp`, `.inl`, `.hlsl`, `.py`, `.bat`, and `.ps1` files.
`ThirdPtySource/` is excluded. Generated first-party source is included.

| Area | Files |
|---|---:|
| `.githooks` | 5 |
| `Agentic` | 15 |
| `SkullbonezData` | 24 |
| `SkullbonezSource` | 611 |
| `SkullbonezTests` | 73 |
| `tools` | 115 |

| Suffix | Files |
|---|---:|
| `.bat` | 63 |
| `.cpp` | 340 |
| `.h` | 348 |
| `.hlsl` | 23 |
| `.inl` | 1 |
| `.ps1` | 4 |
| `.py` | 64 |

## Findings And Prepared Replacements

Categories overlap; they explain why a file changed rather than forming a sum.

| Finding | Measured result | Prepared disposition |
|---|---:|---|
| Missing required header fields | 87 files | Add truthful `File`, `Purpose`, and `Summary` fields; trivial wrappers remain concise. |
| Retired `Mental model:` headings | 91 files | Convert file-level text to `Summary:` and local teaching text to `Concept:`. |
| Shared glossary duplication | 46 terms across 75 files | Remove local duplicates, add 30 missing canonical definitions to the engine glossary, and add 52 durable `Related:` citations. |
| Tautological shader summaries | 18 files | Describe pipeline role, data flow, and ownership instead of restating filenames. |
| Generic UI purpose text | 35 files | Replace it with the concrete widget, layout, or state responsibility. |
| Missing nearby structured teaching comments | Production physics, rendering, runtime, UI, and maths hotspots | Add local `Concept:`, `Why:`, `Invariant:`, `Lifetime:`, or `Hazard:` notes for non-obvious behavior. |
| Historical task-code or future-work prose | 8 locations | Replace it with the current invariant or observable contract. |
| Legacy decorative banners | 1 detected file plus DX12 and shader separators | Remove or convert them to precise headings. |

### Confirmed Comment/Code Disagreements

| File | Offending claim | Code truth and queued replacement |
|---|---|---|
| `SkullbonezSource/Rendering/DX12/RenderBackendDX12.cpp` | Publishing a GPU-timer fence would perform a blocking consume to preserve an older sample. | `Dx12Diagnostics::PublishResolvedGpuTimerFence` overwrites stale pending diagnostic state without waiting; the queued invariant says Present remains non-blocking. |
| `SkullbonezData/shaders/water_ocean.hlsl` | The C++ path skipped cinematic ocean mode pending a future re-enable. | `WorldEnvironment::BindCommonWaterStyle` selects cinematic mode for the ocean and basin; the queued `Why:` documents the authored response. |
| `SkullbonezSource/Physics/PhysicsSceneVectorReserve.h` | Existing raw-vector owners currently use the helper. | No production caller exists; the replacement describes an adoption adapter requiring owner, phase, and hard-cap evidence. |
| `SkullbonezSource/Runtime/Render/RuntimeRenderer.cpp` | A resource-readiness location existed mainly for a future extraction. | The calls enforce same-frame resource-generation readiness; the replacement states that invariant without promising future work. |
| `SkullbonezTests/TestStartup.cpp` | The unsigned-wrap behavior belonged to a separate future task. | The test freezes the observable MSVC `strtoul` compatibility result; the replacement names the present product contract. |

All other ownership, sequencing, lifetime, blocking, and current-state claims found by
the broad scan were checked against their surrounding implementation. The surviving
words such as "currently" and "blocking" describe a value at the call site or a real
wait policy; they are not stale migration promises.

## Superseded DX12 Plan Disposition

This audit absorbs `Agentic/Plans/WNF/dx12-frame-path-comment-rot-sweep.md`.
Its verified timer-readback contradiction is the first row above. The old redundant
`readPending` write no longer exists after diagnostics ownership moved into
`Dx12Diagnostics`, so no behavior edit is required. The current `Present`, `FlushGPU`,
`WaitForGpu`, `FlushUploadBuffer`, and `SubmitClosed` paths were read against their
failure and fence comments; no second mismatch was found. The old `Finish` symbol no
longer exists. The parked plan can therefore be deleted without losing an open code
finding.

## Generated Reflection Durability Note

The strict patch adds the required learning header directly to
`SkullbonezData/generated/GeneratedShaderReflection.h`, but the next shader bake will
regenerate that file. When source mutation is safe, mirror the same header in
`generated_reflection_header()` by replacing the first generated line at
`tools/bake_shaders.py:293` with these emitted rows before `#pragma once`:

```python
"/*",
"File: SkullbonezData/generated/GeneratedShaderReflection.h",
"Purpose:",
"  Publish baked shader reflection metadata as compile-time C++ values.",
"",
"Summary:",
"  tools/bake_shaders.py writes flattened field, resource, input, and stage",
"  tables from pinned DXC reflection. Runtime validation consumes these values",
"  without parsing compiler output or allocating reflection objects.",
"",
"Invariants:",
"  - Regeneration replaces this file; edit tools/bake_shaders.py instead.",
"  - Table ranges remain within their corresponding flattened arrays.",
"",
"Related:",
"  - tools/bake_shaders.py",
"*/",
```

That producer synchronization is recorded here rather than hidden inside the strict
comment-only patch because changing a Python string literal changes the tool's AST.

## Validation Evidence

- Frozen inventory: 840/840 files inspected; final inventory: 843/843 reconciled.
- Proposed-tree scan: 0 missing header-field files, 0 retired headings, and 0 legacy
  banners. The 26 remaining density heuristics are self-describing tests, generated
  data, or bounded tools with complete file contracts; review found no missing local
  teaching comment.
- Full-scope glossary scan: 1,175 local definitions and 0 terms defined in multiple
  tracked source files after the replacement.
- Comment-only semantic comparison: 0 failures across Python ASTs, comment-stripped
  C/C++/HLSL streams, batch commands, and PowerShell commands.
- `python tools/check_related_paths.py --repo .`: 0 existing findings; all 52 new
  glossary citations resolve to the durable engine glossary.
- At audit time, both strict and three-way `git apply --check` passed against
  the snapshot worktree. They are frozen evidence, not a current applicability
  claim. Registration-time strict checking now reports the two conflicts named
  above; CT0 owns their semantic reconciliation and patch regeneration.

Repository build or runtime validation is not required now because no live source is
changed and the queued diff is strictly comments and docs. If the generated-header
producer synchronization is added later, validate shader regeneration separately.

## Application Protocol

After IH7 has committed its final source and comment work:

1. Re-run the same `git ls-files` inventory and append a checklist disposition for
   any source file added after this document's 843-file reconciliation.
2. Enumerate every patched file changed after snapshot `154506e03` and classify
   each queued hunk as still required, redundant because later work already
   supplied the truth, or stale because ownership or behavior changed. Record
   the disposition; do not retain a hunk merely to preserve the old 261 count.
3. Regenerate the patch from the current tree. Require both strict and three-way
   checks to apply without conflicts before any batched application. Current
   source and the owning plan win over frozen replacement prose.
4. Synchronize the generated reflection producer using the exact rows above, then
   regenerate and check the reflection header if that non-comment tool edit is authorized.
5. Re-run the comment audit, full-scope glossary inventory, Related-path check, and
   comment-only semantic comparison before checking the remaining queued rows.
6. Run `tools\validate_fast.bat` at CT5 so comment-driven line movement, body
   digests, exact ownership rulings, generated metadata, and durable paths are
   proven against the final tree. Refresh a ruling only after its owner judgment
   is re-read; never change a ruling merely to clear the gate.

## Phases

- [ ] **CT0 — Rebase and adjudicate the frozen replacement.** Reconcile the
  current tracked inventory, every file changed since the snapshot, all IH
  overlaps, and both known direct conflicts. Mark redundant replacements as
  satisfied by current truth, regenerate stale replacements, and produce a
  conflict-free patch plus exact per-file dispositions before live source edits.
- [ ] **CT1 — Apply infrastructure, generated-data, shader, test, and tool
  replacements.** Land the non-engine batches, synchronize the generated shader
  reflection producer, and prove script/command semantics plus generated output.
- [ ] **CT2 — Apply lower-layer engine replacements.** Re-audit and update
  Assets, Core, Maths, Physics, Scene, and World without replacing any later IH
  invariant, ownership, enforcement-lane, or hazard truth.
- [ ] **CT3 — Apply Rendering and DX12 replacements.** Preserve feature-neutral
  Rendering vocabulary and the final IH4 lifecycle/capacity contracts while
  correcting stale summaries, fence semantics, and local teaching comments.
- [ ] **CT4 — Apply Runtime and UI replacements.** Preserve the final IH5 owner,
  lifecycle, package-direction, replay-growth, and input-router contracts while
  replacing only comments still false or incomplete on the post-IH tree.
- [ ] **CT5 — Reconcile and close the full-source truth pass.** Re-run the exact
  inventory, require every row checked or explicitly superseded with no silent
  deferral, run glossary/Related/semantic checks and `validate_fast`, obtain an
  independent whole-pass truth review, then delete the completed live plan and
  frozen patch under the repository archive convention.

## Checklist Contract

A checked row means the current file was inspected and needs no replacement. An
unchecked row was also inspected, but its exact replacement remains queued in the
patch. Every row has a disposition; unchecked does not mean unreviewed.

## File Checklist

### `.githooks`

- [ ] `.githooks/check-braces.py` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [ ] `.githooks/check-headers.py` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [ ] `.githooks/fix-line-endings.py` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [ ] `.githooks/run-clang-format.py` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [ ] `.githooks/trim-whitespace.py` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`

### `Agentic`

- [ ] `Agentic/Manuals/SkullbonezCoreManual/build_manual.py` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [ ] `Agentic/Skills/collapse_params.py` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [ ] `Agentic/Skills/loc_count.py` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [ ] `Agentic/Skills/orchestrator/scripts/resolve_nightrunner_branch.ps1` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [x] `Agentic/Skills/orchestrator/scripts/work_ledger.bat` -- inspected; no replacement required
- [x] `Agentic/Skills/orchestrator/scripts/work_ledger.ps1` -- inspected; no replacement required
- [x] `Agentic/Skills/render-work-ledger/scripts/render_work_ledger.py` -- inspected; no replacement required
- [ ] `Agentic/Skills/skore-cpu-profiler/analyze_markers.py` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [ ] `Agentic/Skills/skore-cpu-profiler/cleanup_markers.py` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [ ] `Agentic/Skills/skore-render-test/analyze_perf.py` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [ ] `Agentic/Skills/skore-render-test/perf_compare.py` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [ ] `Agentic/Tests/Dx12ArchUnitTests/Dx12ArchUnitTests.cpp` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [ ] `Agentic/Tests/RuntimeInteractionPolicyTests/RuntimeInteractionPolicyTests.cpp` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [ ] `Agentic/Tests/SceneParserUnitTests/SceneParserUnitTests.cpp` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [x] `Agentic/Tests/UiBoundaryUnitTests/UiBoundaryUnitTests.cpp` -- inspected; no replacement required

### `SkullbonezData`

- [ ] `SkullbonezData/generated/GeneratedShaderReflection.h` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`; producer synchronization is specified above
- [ ] `SkullbonezData/shaders/UIBackdropBlur.hlsl` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [ ] `SkullbonezData/shaders/collision_visualizer.hlsl` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [ ] `SkullbonezData/shaders/generate_mips.hlsl` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [ ] `SkullbonezData/shaders/grid_line.hlsl` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [ ] `SkullbonezData/shaders/launcher_laser.hlsl` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [ ] `SkullbonezData/shaders/lit_textured.hlsl` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [ ] `SkullbonezData/shaders/lit_textured_instanced.hlsl` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [ ] `SkullbonezData/shaders/post_tonemap.hlsl` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [ ] `SkullbonezData/shaders/post_volumetric_light.hlsl` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [ ] `SkullbonezData/shaders/reflect.rt.hlsl` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [ ] `SkullbonezData/shaders/shadow_depth.hlsl` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [ ] `SkullbonezData/shaders/shadow_depth_instanced.hlsl` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [ ] `SkullbonezData/shaders/sky_atmosphere.hlsl` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [ ] `SkullbonezData/shaders/soft_additive_ribbon.hlsl` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [ ] `SkullbonezData/shaders/solid_color.hlsl` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [ ] `SkullbonezData/shaders/solid_color_batch.hlsl` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [ ] `SkullbonezData/shaders/text.hlsl` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [ ] `SkullbonezData/shaders/trajectory_ribbon.hlsl` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [ ] `SkullbonezData/shaders/transient_colored_triangles.hlsl` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [ ] `SkullbonezData/shaders/ui_render_target_preview.hlsl` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [ ] `SkullbonezData/shaders/unlit_textured.hlsl` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [ ] `SkullbonezData/shaders/water_calm.hlsl` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [ ] `SkullbonezData/shaders/water_ocean.hlsl` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`

### `SkullbonezSource`

- [x] `SkullbonezSource/Assets/AssetKeys.h` -- inspected; no replacement required
- [ ] `SkullbonezSource/Assets/AssetSystem.cpp` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [x] `SkullbonezSource/Assets/AssetSystem.h` -- inspected; no replacement required
- [x] `SkullbonezSource/Assets/TextureCollection.cpp` -- inspected; no replacement required
- [x] `SkullbonezSource/Assets/TextureCollection.h` -- inspected; no replacement required
- [x] `SkullbonezSource/Core/Allocation/DevelopmentToolAllocation.cpp` -- inspected; no replacement required
- [x] `SkullbonezSource/Core/Allocation/DevelopmentToolAllocation.h` -- inspected; no replacement required
- [x] `SkullbonezSource/Core/Allocation/DevelopmentToolsCapability.h` -- inspected; no replacement required
- [x] `SkullbonezSource/Core/Allocation/RuntimeAllocationTracker.cpp` -- inspected; no replacement required
- [ ] `SkullbonezSource/Core/Allocation/RuntimeAllocationTracker.h` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [x] `SkullbonezSource/Core/Allocation/RuntimeReserveAllocator.cpp` -- inspected; no replacement required
- [ ] `SkullbonezSource/Core/Allocation/RuntimeReserveAllocator.h` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [x] `SkullbonezSource/Core/AmortizedTask.cpp` -- inspected; no replacement required
- [x] `SkullbonezSource/Core/AmortizedTask.h` -- inspected; no replacement required
- [x] `SkullbonezSource/Core/AtomicTextFileWriter.cpp` -- inspected; no replacement required
- [x] `SkullbonezSource/Core/AtomicTextFileWriter.h` -- inspected; no replacement required
- [x] `SkullbonezSource/Core/ByteView.h` -- inspected; no replacement required
- [x] `SkullbonezSource/Core/Common.h` -- inspected; no replacement required
- [ ] `SkullbonezSource/Core/Config.cpp` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [x] `SkullbonezSource/Core/Config.h` -- inspected; no replacement required
- [x] `SkullbonezSource/Core/FatalError.cpp` -- inspected; no replacement required
- [x] `SkullbonezSource/Core/FatalError.h` -- inspected; no replacement required
- [x] `SkullbonezSource/Core/Fence.h` -- inspected; no replacement required
- [x] `SkullbonezSource/Core/FloatingPointContract.h` -- inspected; no replacement required
- [x] `SkullbonezSource/Core/LockOrderValidator.cpp` -- inspected; no replacement required
- [x] `SkullbonezSource/Core/LockOrderValidator.h` -- inspected; no replacement required
- [x] `SkullbonezSource/Core/Log.cpp` -- inspected; no replacement required
- [x] `SkullbonezSource/Core/Log.h` -- inspected; no replacement required
- [x] `SkullbonezSource/Core/MainMemoryStats.h` -- inspected; no replacement required
- [x] `SkullbonezSource/Core/PlatformPosix.h` -- inspected; no replacement required
- [x] `SkullbonezSource/Core/PlatformProfiler.cpp` -- inspected; no replacement required
- [x] `SkullbonezSource/Core/PlatformProfiler.h` -- inspected; no replacement required
- [x] `SkullbonezSource/Core/PlatformWin32.h` -- inspected; no replacement required
- [ ] `SkullbonezSource/Core/Profiler.cpp` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [x] `SkullbonezSource/Core/Profiler.h` -- inspected; no replacement required
- [x] `SkullbonezSource/Core/SbDiagnosticStore.h` -- inspected; no replacement required
- [x] `SkullbonezSource/Core/SbResult.cpp` -- inspected; no replacement required
- [x] `SkullbonezSource/Core/SbResult.h` -- inspected; no replacement required
- [x] `SkullbonezSource/Core/SceneCapacity.h` -- inspected; no replacement required
- [x] `SkullbonezSource/Core/StringHash.h` -- inspected; no replacement required
- [x] `SkullbonezSource/Core/Timer.cpp` -- inspected; no replacement required
- [x] `SkullbonezSource/Core/Timer.h` -- inspected; no replacement required
- [x] `SkullbonezSource/Core/TracyClientOwner.cpp` -- inspected; no replacement required
- [x] `SkullbonezSource/Core/TracyClientOwner.h` -- inspected; no replacement required
- [x] `SkullbonezSource/Core/WindowConstants.h` -- inspected; no replacement required
- [x] `SkullbonezSource/Core/WorkerPool.cpp` -- inspected; no replacement required
- [x] `SkullbonezSource/Core/WorkerPool.h` -- inspected; no replacement required
- [x] `SkullbonezSource/Gameplay/TornadoField.cpp` -- inspected; no replacement required
- [x] `SkullbonezSource/Gameplay/TornadoField.h` -- inspected; no replacement required
- [x] `SkullbonezSource/Gameplay/TornadoGameplay.cpp` -- inspected; no replacement required
- [x] `SkullbonezSource/Gameplay/TornadoGameplay.h` -- inspected; no replacement required
- [x] `SkullbonezSource/Gameplay/TornadoVisualPass.cpp` -- inspected; no replacement required
- [x] `SkullbonezSource/Gameplay/TornadoVisualPass.h` -- inspected; no replacement required
- [x] `SkullbonezSource/Maths/DeterministicMath.cpp` -- inspected; no replacement required
- [x] `SkullbonezSource/Maths/DeterministicMath.h` -- inspected; no replacement required
- [x] `SkullbonezSource/Maths/Frustum.cpp` -- inspected; no replacement required
- [x] `SkullbonezSource/Maths/Frustum.h` -- inspected; no replacement required
- [x] `SkullbonezSource/Maths/GeometricMath.cpp` -- inspected; no replacement required
- [x] `SkullbonezSource/Maths/GeometricMath.h` -- inspected; no replacement required
- [x] `SkullbonezSource/Maths/GeometricStructures.h` -- inspected; no replacement required
- [x] `SkullbonezSource/Maths/MathsCommon.h` -- inspected; no replacement required
- [ ] `SkullbonezSource/Maths/Matrix4.cpp` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [x] `SkullbonezSource/Maths/Matrix4.h` -- inspected; no replacement required
- [x] `SkullbonezSource/Maths/OrbitalMechanics.cpp` -- inspected; no replacement required
- [x] `SkullbonezSource/Maths/OrbitalMechanics.h` -- inspected; no replacement required
- [x] `SkullbonezSource/Maths/Quaternion.cpp` -- inspected; no replacement required
- [ ] `SkullbonezSource/Maths/Quaternion.h` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [x] `SkullbonezSource/Maths/RotationMatrix.cpp` -- inspected; no replacement required
- [x] `SkullbonezSource/Maths/RotationMatrix.h` -- inspected; no replacement required
- [x] `SkullbonezSource/Maths/Vector3.h` -- inspected; no replacement required
- [ ] `SkullbonezSource/Physics/BoundingBox.cpp` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [ ] `SkullbonezSource/Physics/BoundingBox.h` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [x] `SkullbonezSource/Physics/BoundingSphere.cpp` -- inspected; no replacement required
- [ ] `SkullbonezSource/Physics/BoundingSphere.h` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [x] `SkullbonezSource/Physics/BuoyancySystem.cpp` -- inspected; no replacement required
- [x] `SkullbonezSource/Physics/BuoyancySystem.h` -- inspected; no replacement required
- [x] `SkullbonezSource/Physics/ColliderStore.cpp` -- inspected; no replacement required
- [x] `SkullbonezSource/Physics/ColliderStore.h` -- inspected; no replacement required
- [x] `SkullbonezSource/Physics/CollisionShape.h` -- inspected; no replacement required
- [x] `SkullbonezSource/Physics/ContactEnergyOracle.h` -- inspected; no replacement required
- [x] `SkullbonezSource/Physics/ContactSolverCommon.h` -- inspected; no replacement required
- [x] `SkullbonezSource/Physics/ConvexHullShape.cpp` -- inspected; no replacement required
- [x] `SkullbonezSource/Physics/ConvexHullShape.h` -- inspected; no replacement required
- [x] `SkullbonezSource/Physics/Diagnostics/SkullScope.cpp` -- inspected; no replacement required
- [x] `SkullbonezSource/Physics/Diagnostics/SkullScope.h` -- inspected; no replacement required
- [x] `SkullbonezSource/Physics/DisjointSet.h` -- inspected; no replacement required
- [x] `SkullbonezSource/Physics/ObjectContactManifold.cpp` -- inspected; no replacement required
- [x] `SkullbonezSource/Physics/ObjectContactManifold.h` -- inspected; no replacement required
- [ ] `SkullbonezSource/Physics/PersistentContactSolver.cpp` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [x] `SkullbonezSource/Physics/PersistentContactSolver.h` -- inspected; no replacement required
- [x] `SkullbonezSource/Physics/PhysicsApi.h` -- inspected; no replacement required
- [x] `SkullbonezSource/Physics/PhysicsBodyStore.cpp` -- inspected; no replacement required
- [x] `SkullbonezSource/Physics/PhysicsBodyStore.h` -- inspected; no replacement required
- [x] `SkullbonezSource/Physics/PhysicsBroadphaseDebugView.h` -- inspected; no replacement required
- [x] `SkullbonezSource/Physics/PhysicsDebugData.h` -- inspected; no replacement required
- [x] `SkullbonezSource/Physics/PhysicsDiagnosticsModel.h` -- inspected; no replacement required
- [x] `SkullbonezSource/Physics/PhysicsDiagnosticsSink.cpp` -- inspected; no replacement required
- [x] `SkullbonezSource/Physics/PhysicsDiagnosticsSink.h` -- inspected; no replacement required
- [x] `SkullbonezSource/Physics/PhysicsDiagnosticsView.h` -- inspected; no replacement required
- [x] `SkullbonezSource/Physics/PhysicsEngine.ReplayPredictionCloneScope.h` -- inspected; no replacement required
- [ ] `SkullbonezSource/Physics/PhysicsEngine.cpp` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [x] `SkullbonezSource/Physics/PhysicsEngine.h` -- inspected; no replacement required
- [x] `SkullbonezSource/Physics/PhysicsFixedList.h` -- inspected; no replacement required
- [x] `SkullbonezSource/Physics/PhysicsHandles.h` -- inspected; no replacement required
- [x] `SkullbonezSource/Physics/PhysicsMass.h` -- inspected; no replacement required
- [x] `SkullbonezSource/Physics/PhysicsObjectPolicy.cpp` -- inspected; no replacement required
- [x] `SkullbonezSource/Physics/PhysicsObjectPolicy.h` -- inspected; no replacement required
- [x] `SkullbonezSource/Physics/PhysicsPoseIntegration.cpp` -- inspected; no replacement required
- [x] `SkullbonezSource/Physics/PhysicsPoseIntegration.h` -- inspected; no replacement required
- [ ] `SkullbonezSource/Physics/PhysicsRuntimeSettings.h` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [ ] `SkullbonezSource/Physics/PhysicsSceneVectorReserve.h` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [x] `SkullbonezSource/Physics/PhysicsSolverSnapshot.h` -- inspected; no replacement required
- [x] `SkullbonezSource/Physics/PhysicsStageCapacity.h` -- inspected; no replacement required
- [x] `SkullbonezSource/Physics/PhysicsTerrainView.cpp` -- inspected; no replacement required
- [x] `SkullbonezSource/Physics/PhysicsTerrainView.h` -- inspected; no replacement required
- [x] `SkullbonezSource/Physics/PhysicsTimestep.h` -- inspected; no replacement required
- [x] `SkullbonezSource/Physics/PhysicsWorld.cpp` -- inspected; no replacement required
- [x] `SkullbonezSource/Physics/PhysicsWorld.h` -- inspected; no replacement required
- [x] `SkullbonezSource/Physics/PhysicsWorldForces.h` -- inspected; no replacement required
- [x] `SkullbonezSource/Physics/Ragdoll.cpp` -- inspected; no replacement required
- [x] `SkullbonezSource/Physics/Ragdoll.h` -- inspected; no replacement required
- [x] `SkullbonezSource/Physics/SleepIslandSystem.cpp` -- inspected; no replacement required
- [x] `SkullbonezSource/Physics/SleepIslandSystem.h` -- inspected; no replacement required
- [x] `SkullbonezSource/Physics/SolverBroadphaseStage.h` -- inspected; no replacement required
- [x] `SkullbonezSource/Physics/SpatialGrid.cpp` -- inspected; no replacement required
- [ ] `SkullbonezSource/Physics/SpatialGrid.h` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [x] `SkullbonezSource/Physics/Stages/ExternalForceStage.cpp` -- inspected; no replacement required
- [x] `SkullbonezSource/Physics/Stages/ExternalForceStage.h` -- inspected; no replacement required
- [x] `SkullbonezSource/Physics/Stages/PhysicsBroadphaseStage.cpp` -- inspected; no replacement required
- [x] `SkullbonezSource/Physics/Stages/PhysicsBroadphaseStage.h` -- inspected; no replacement required
- [x] `SkullbonezSource/Physics/Stages/PhysicsContactSolverStage.cpp` -- inspected; no replacement required
- [x] `SkullbonezSource/Physics/Stages/PhysicsContactSolverStage.h` -- inspected; no replacement required
- [x] `SkullbonezSource/Physics/Stages/PhysicsForceStage.cpp` -- inspected; no replacement required
- [x] `SkullbonezSource/Physics/Stages/PhysicsForceStage.h` -- inspected; no replacement required
- [x] `SkullbonezSource/Physics/Stages/PhysicsNarrowphaseStage.Execution.cpp` -- inspected; no replacement required
- [x] `SkullbonezSource/Physics/Stages/PhysicsNarrowphaseStage.cpp` -- inspected; no replacement required
- [x] `SkullbonezSource/Physics/Stages/PhysicsNarrowphaseStage.h` -- inspected; no replacement required
- [x] `SkullbonezSource/Physics/Stages/PhysicsSleepController.State.cpp` -- inspected; no replacement required
- [x] `SkullbonezSource/Physics/Stages/PhysicsSleepController.Wake.cpp` -- inspected; no replacement required
- [x] `SkullbonezSource/Physics/Stages/PhysicsSleepController.cpp` -- inspected; no replacement required
- [ ] `SkullbonezSource/Physics/Stages/PhysicsSleepController.h` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [x] `SkullbonezSource/Physics/Stages/PhysicsStepDiagnostics.cpp` -- inspected; no replacement required
- [x] `SkullbonezSource/Physics/Stages/PhysicsStepDiagnostics.h` -- inspected; no replacement required
- [ ] `SkullbonezSource/Physics/Stages/PhysicsTerrainStage.cpp` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [x] `SkullbonezSource/Physics/Stages/PhysicsTerrainStage.h` -- inspected; no replacement required
- [ ] `SkullbonezSource/Physics/TerrainContactManifold.cpp` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [x] `SkullbonezSource/Physics/TerrainContactManifold.h` -- inspected; no replacement required
- [ ] `SkullbonezSource/Physics/TerrainSupportClassifier.h` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [x] `SkullbonezSource/Rendering/ContactManifoldPresentation.h` -- inspected; no replacement required
- [ ] `SkullbonezSource/Rendering/DX12/BLASDX12.cpp` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [x] `SkullbonezSource/Rendering/DX12/BLASDX12.h` -- inspected; no replacement required
- [x] `SkullbonezSource/Rendering/DX12/Dx12BackbufferCapture.cpp` -- inspected; no replacement required
- [x] `SkullbonezSource/Rendering/DX12/Dx12BackbufferCapture.h` -- inspected; no replacement required
- [x] `SkullbonezSource/Rendering/DX12/Dx12CachedPsoStore.cpp` -- inspected; no replacement required
- [x] `SkullbonezSource/Rendering/DX12/Dx12CachedPsoStore.h` -- inspected; no replacement required
- [x] `SkullbonezSource/Rendering/DX12/Dx12DeferredReleaseOwner.cpp` -- inspected; no replacement required
- [x] `SkullbonezSource/Rendering/DX12/Dx12DescriptorHeaps.cpp` -- inspected; no replacement required
- [x] `SkullbonezSource/Rendering/DX12/Dx12DescriptorHeaps.h` -- inspected; no replacement required
- [x] `SkullbonezSource/Rendering/DX12/Dx12Diagnostics.cpp` -- inspected; no replacement required
- [x] `SkullbonezSource/Rendering/DX12/Dx12Diagnostics.h` -- inspected; no replacement required
- [x] `SkullbonezSource/Rendering/DX12/Dx12FrameOwner.cpp` -- inspected; no replacement required
- [x] `SkullbonezSource/Rendering/DX12/Dx12FrameOwner.h` -- inspected; no replacement required
- [x] `SkullbonezSource/Rendering/DX12/Dx12GraphTransientPool.cpp` -- inspected; no replacement required
- [x] `SkullbonezSource/Rendering/DX12/Dx12GraphTransientPool.h` -- inspected; no replacement required
- [x] `SkullbonezSource/Rendering/DX12/Dx12ImGuiRendererOwner.cpp` -- inspected; no replacement required
- [x] `SkullbonezSource/Rendering/DX12/Dx12ImGuiRendererOwner.h` -- inspected; no replacement required
- [ ] `SkullbonezSource/Rendering/DX12/Dx12RenderGraphExecutor.cpp` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [x] `SkullbonezSource/Rendering/DX12/Dx12RenderGraphExecutor.h` -- inspected; no replacement required
- [x] `SkullbonezSource/Rendering/DX12/Dx12ResourceBuilder.h` -- inspected; no replacement required
- [x] `SkullbonezSource/Rendering/DX12/Dx12ShaderDevelopment.cpp` -- inspected; no replacement required
- [x] `SkullbonezSource/Rendering/DX12/Dx12ShaderDevelopment.h` -- inspected; no replacement required
- [x] `SkullbonezSource/Rendering/DX12/Dx12TextureRegistry.h` -- inspected; no replacement required
- [x] `SkullbonezSource/Rendering/DX12/FramebufferDX12.cpp` -- inspected; no replacement required
- [x] `SkullbonezSource/Rendering/DX12/FramebufferDX12.h` -- inspected; no replacement required
- [x] `SkullbonezSource/Rendering/DX12/MeshDX12.cpp` -- inspected; no replacement required
- [x] `SkullbonezSource/Rendering/DX12/MeshDX12.h` -- inspected; no replacement required
- [ ] `SkullbonezSource/Rendering/DX12/RenderBackendDX12.CommandRecordingState.h` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [ ] `SkullbonezSource/Rendering/DX12/RenderBackendDX12.DXR.cpp` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [ ] `SkullbonezSource/Rendering/DX12/RenderBackendDX12.DynamicGeometry.cpp` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [ ] `SkullbonezSource/Rendering/DX12/RenderBackendDX12.Pipeline.cpp` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [x] `SkullbonezSource/Rendering/DX12/RenderBackendDX12.PipelineState.h` -- inspected; no replacement required
- [ ] `SkullbonezSource/Rendering/DX12/RenderBackendDX12.Resources.cpp` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [ ] `SkullbonezSource/Rendering/DX12/RenderBackendDX12.Textures.cpp` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [ ] `SkullbonezSource/Rendering/DX12/RenderBackendDX12.cpp` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [x] `SkullbonezSource/Rendering/DX12/RenderBackendDX12.h` -- inspected; no replacement required
- [x] `SkullbonezSource/Rendering/DX12/RenderDeviceDX12.cpp` -- inspected; no replacement required
- [x] `SkullbonezSource/Rendering/DX12/RenderDeviceDX12.h` -- inspected; no replacement required
- [x] `SkullbonezSource/Rendering/DX12/RenderGraphTransientDX12.h` -- inspected; no replacement required
- [ ] `SkullbonezSource/Rendering/DX12/SBTDX12.cpp` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [x] `SkullbonezSource/Rendering/DX12/SBTDX12.h` -- inspected; no replacement required
- [x] `SkullbonezSource/Rendering/DX12/ShaderBytecodeManifest.cpp` -- inspected; no replacement required
- [x] `SkullbonezSource/Rendering/DX12/ShaderBytecodeManifest.h` -- inspected; no replacement required
- [x] `SkullbonezSource/Rendering/DX12/ShaderDX12.cpp` -- inspected; no replacement required
- [x] `SkullbonezSource/Rendering/DX12/ShaderDX12.h` -- inspected; no replacement required
- [ ] `SkullbonezSource/Rendering/DX12/TLASDX12.cpp` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [x] `SkullbonezSource/Rendering/DX12/TLASDX12.h` -- inspected; no replacement required
- [ ] `SkullbonezSource/Rendering/DrawCallTrace.cpp` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [x] `SkullbonezSource/Rendering/DrawCallTrace.h` -- inspected; no replacement required
- [ ] `SkullbonezSource/Rendering/PrimitiveBatchRenderer.cpp` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [x] `SkullbonezSource/Rendering/PrimitiveBatchRenderer.h` -- inspected; no replacement required
- [x] `SkullbonezSource/Rendering/PrimitiveMeshBuilder.h` -- inspected; no replacement required
- [ ] `SkullbonezSource/Rendering/RenderCommandTypes.h` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [x] `SkullbonezSource/Rendering/RenderDiagnosticsTypes.h` -- inspected; no replacement required
- [x] `SkullbonezSource/Rendering/RenderGpuTimingOwner.cpp` -- inspected; no replacement required
- [x] `SkullbonezSource/Rendering/RenderGpuTimingOwner.h` -- inspected; no replacement required
- [x] `SkullbonezSource/Rendering/RenderGraph.cpp` -- inspected; no replacement required
- [x] `SkullbonezSource/Rendering/RenderGraph.h` -- inspected; no replacement required
- [x] `SkullbonezSource/Rendering/RenderInstanceRenderer.cpp` -- inspected; no replacement required
- [x] `SkullbonezSource/Rendering/RenderInstanceRenderer.h` -- inspected; no replacement required
- [x] `SkullbonezSource/Rendering/RenderInstanceStore.cpp` -- inspected; no replacement required
- [x] `SkullbonezSource/Rendering/RenderInstanceStore.h` -- inspected; no replacement required
- [ ] `SkullbonezSource/Rendering/RenderMaterial.h` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [x] `SkullbonezSource/Rendering/RenderPipeline.cpp` -- inspected; no replacement required
- [x] `SkullbonezSource/Rendering/RenderPipeline.h` -- inspected; no replacement required
- [x] `SkullbonezSource/Rendering/RenderRasterBindingContract.h` -- inspected; no replacement required
- [x] `SkullbonezSource/Rendering/RenderRaytracingTypes.h` -- inspected; no replacement required
- [x] `SkullbonezSource/Rendering/RenderResourceTypes.h` -- inspected; no replacement required
- [x] `SkullbonezSource/Rendering/RenderSceneSnapshot.h` -- inspected; no replacement required
- [x] `SkullbonezSource/Rendering/ShaderContracts.h` -- inspected; no replacement required
- [x] `SkullbonezSource/Rendering/ShaderReflectionContracts.h` -- inspected; no replacement required
- [ ] `SkullbonezSource/Rendering/Shadow.h` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [ ] `SkullbonezSource/Rendering/Text.cpp` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [x] `SkullbonezSource/Rendering/Text.h` -- inspected; no replacement required
- [x] `SkullbonezSource/Rendering/WorldRenderExtension.h` -- inspected; no replacement required
- [ ] `SkullbonezSource/Runtime/App/ApplicationExitState.cpp` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [ ] `SkullbonezSource/Runtime/App/ApplicationExitState.h` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [ ] `SkullbonezSource/Runtime/App/Init.cpp` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [x] `SkullbonezSource/Runtime/App/InputFrame.cpp` -- inspected; no replacement required
- [x] `SkullbonezSource/Runtime/App/InputFrame.h` -- inspected; no replacement required
- [x] `SkullbonezSource/Runtime/App/InputFrameExecution.cpp` -- inspected; no replacement required
- [x] `SkullbonezSource/Runtime/App/InputRouter.Interactions.cpp` -- inspected; no replacement required
- [x] `SkullbonezSource/Runtime/App/ReplayReserveInventory.h` -- inspected; no replacement required
- [x] `SkullbonezSource/Runtime/App/ReplayRuntime.cpp` -- inspected; no replacement required
- [x] `SkullbonezSource/Runtime/App/ReplayRuntime.h` -- inspected; no replacement required
- [x] `SkullbonezSource/Runtime/App/ReplayRuntimePackets.h` -- inspected; no replacement required
- [x] `SkullbonezSource/Runtime/App/ReplayScrubberTools.cpp` -- inspected; no replacement required
- [x] `SkullbonezSource/Runtime/App/ReplayValidation.Internal.h` -- inspected; no replacement required
- [x] `SkullbonezSource/Runtime/App/ReplayValidation.Probes.cpp` -- inspected; no replacement required
- [x] `SkullbonezSource/Runtime/App/ReplayValidation.cpp` -- inspected; no replacement required
- [x] `SkullbonezSource/Runtime/App/Run.cpp` -- inspected; no replacement required
- [ ] `SkullbonezSource/Runtime/App/Run.h` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [ ] `SkullbonezSource/Runtime/App/RunFrame.cpp` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [x] `SkullbonezSource/Runtime/App/RunLaunchOptions.Renderer.h` -- inspected; no replacement required
- [x] `SkullbonezSource/Runtime/App/RunLaunchOptions.h` -- inspected; no replacement required
- [x] `SkullbonezSource/Runtime/App/RunRender.cpp` -- inspected; no replacement required
- [x] `SkullbonezSource/Runtime/App/RunStartupState.h` -- inspected; no replacement required
- [x] `SkullbonezSource/Runtime/App/RunTimerState.h` -- inspected; no replacement required
- [x] `SkullbonezSource/Runtime/App/Window.cpp` -- inspected; no replacement required
- [x] `SkullbonezSource/Runtime/App/Window.h` -- inspected; no replacement required
- [x] `SkullbonezSource/Runtime/Automation/InteractionAutomationController.cpp` -- inspected; no replacement required
- [x] `SkullbonezSource/Runtime/Automation/InteractionAutomationController.h` -- inspected; no replacement required
- [x] `SkullbonezSource/Runtime/Automation/InteractionAutomationInputDriver.cpp` -- inspected; no replacement required
- [x] `SkullbonezSource/Runtime/Automation/InteractionAutomationInputDriver.h` -- inspected; no replacement required
- [ ] `SkullbonezSource/Runtime/Automation/InteractionAutomationReportWriter.cpp` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [x] `SkullbonezSource/Runtime/Automation/InteractionAutomationReportWriter.h` -- inspected; no replacement required
- [x] `SkullbonezSource/Runtime/Automation/RuntimeValidationHarness.cpp` -- inspected; no replacement required
- [x] `SkullbonezSource/Runtime/Automation/RuntimeValidationHarness.h` -- inspected; no replacement required
- [ ] `SkullbonezSource/Runtime/Camera/AttachedCameraController.InspectionPolicy.cpp` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [x] `SkullbonezSource/Runtime/Camera/AttachedCameraController.InspectionPolicy.h` -- inspected; no replacement required
- [x] `SkullbonezSource/Runtime/Camera/AttachedCameraController.cpp` -- inspected; no replacement required
- [x] `SkullbonezSource/Runtime/Camera/AttachedCameraController.h` -- inspected; no replacement required
- [x] `SkullbonezSource/Runtime/Camera/Camera.cpp` -- inspected; no replacement required
- [x] `SkullbonezSource/Runtime/Camera/Camera.h` -- inspected; no replacement required
- [x] `SkullbonezSource/Runtime/Camera/CameraCollection.cpp` -- inspected; no replacement required
- [x] `SkullbonezSource/Runtime/Camera/CameraCollection.h` -- inspected; no replacement required
- [x] `SkullbonezSource/Runtime/Camera/CameraControlState.cpp` -- inspected; no replacement required
- [x] `SkullbonezSource/Runtime/Camera/CameraControlState.h` -- inspected; no replacement required
- [x] `SkullbonezSource/Runtime/Camera/RuntimeCameraMode.h` -- inspected; no replacement required
- [x] `SkullbonezSource/Runtime/Capture/CaptureController.cpp` -- inspected; no replacement required
- [x] `SkullbonezSource/Runtime/Capture/CaptureController.h` -- inspected; no replacement required
- [x] `SkullbonezSource/Runtime/Capture/CaptureSystem.cpp` -- inspected; no replacement required
- [x] `SkullbonezSource/Runtime/Capture/CaptureSystem.h` -- inspected; no replacement required
- [x] `SkullbonezSource/Runtime/Capture/GraphicsStressController.h` -- inspected; no replacement required
- [x] `SkullbonezSource/Runtime/Capture/RuntimeStressController.cpp` -- inspected; no replacement required
- [x] `SkullbonezSource/Runtime/Capture/RuntimeStressController.h` -- inspected; no replacement required
- [ ] `SkullbonezSource/Runtime/Debug/BroadphaseVisualizer.cpp` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [x] `SkullbonezSource/Runtime/Debug/BroadphaseVisualizer.h` -- inspected; no replacement required
- [ ] `SkullbonezSource/Runtime/Debug/CollisionVisualizer.cpp` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [x] `SkullbonezSource/Runtime/Debug/CollisionVisualizer.h` -- inspected; no replacement required
- [x] `SkullbonezSource/Runtime/Debug/PhysicsDebugVisualizer.cpp` -- inspected; no replacement required
- [x] `SkullbonezSource/Runtime/Debug/PhysicsDebugVisualizer.h` -- inspected; no replacement required
- [x] `SkullbonezSource/Runtime/DevelopmentTools/ImGuiEditorCausalityProjection.h` -- inspected; no replacement required
- [x] `SkullbonezSource/Runtime/DevelopmentTools/ImGuiEditorInputPolicy.h` -- inspected; no replacement required
- [x] `SkullbonezSource/Runtime/DevelopmentTools/ImGuiEditorLayoutPolicy.cpp` -- inspected; no replacement required
- [x] `SkullbonezSource/Runtime/DevelopmentTools/ImGuiEditorLayoutPolicy.h` -- inspected; no replacement required
- [x] `SkullbonezSource/Runtime/DevelopmentTools/ImGuiEditorOwner.cpp` -- inspected; no replacement required
- [x] `SkullbonezSource/Runtime/DevelopmentTools/ImGuiEditorOwner.h` -- inspected; no replacement required
- [x] `SkullbonezSource/Runtime/Diagnostics/DiagnosticsController.cpp` -- inspected; no replacement required
- [x] `SkullbonezSource/Runtime/Diagnostics/DiagnosticsController.h` -- inspected; no replacement required
- [x] `SkullbonezSource/Runtime/Diagnostics/DiagnosticsPhysicsUI.cpp` -- inspected; no replacement required
- [x] `SkullbonezSource/Runtime/Diagnostics/DiagnosticsPhysicsUI.h` -- inspected; no replacement required
- [x] `SkullbonezSource/Runtime/Diagnostics/DiagnosticsRuntime.cpp` -- inspected; no replacement required
- [x] `SkullbonezSource/Runtime/Diagnostics/DiagnosticsRuntime.h` -- inspected; no replacement required
- [x] `SkullbonezSource/Runtime/Diagnostics/OverlayDebugState.h` -- inspected; no replacement required
- [x] `SkullbonezSource/Runtime/Diagnostics/RuntimeDiagnostics.cpp` -- inspected; no replacement required
- [x] `SkullbonezSource/Runtime/Diagnostics/RuntimeDiagnostics.h` -- inspected; no replacement required
- [x] `SkullbonezSource/Runtime/Diagnostics/RuntimeOverlayDiagnostics.cpp` -- inspected; no replacement required
- [x] `SkullbonezSource/Runtime/Diagnostics/RuntimeOverlayDiagnostics.h` -- inspected; no replacement required
- [x] `SkullbonezSource/Runtime/Diagnostics/SceneMemoryDiagnostics.cpp` -- inspected; no replacement required
- [x] `SkullbonezSource/Runtime/Diagnostics/SceneMemoryDiagnostics.h` -- inspected; no replacement required
- [x] `SkullbonezSource/Runtime/Direction/DemoDirector.cpp` -- inspected; no replacement required
- [x] `SkullbonezSource/Runtime/Direction/DemoDirector.h` -- inspected; no replacement required
- [x] `SkullbonezSource/Runtime/Direction/DemoDirectorPlayback.cpp` -- inspected; no replacement required
- [x] `SkullbonezSource/Runtime/Direction/DemoDirectorPlayback.h` -- inspected; no replacement required
- [x] `SkullbonezSource/Runtime/Direction/LiveStyleController.cpp` -- inspected; no replacement required
- [x] `SkullbonezSource/Runtime/Direction/LiveStyleController.h` -- inspected; no replacement required
- [ ] `SkullbonezSource/Runtime/Direction/LookLabBundleWriter.cpp` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [x] `SkullbonezSource/Runtime/Direction/LookLabBundleWriter.h` -- inspected; no replacement required
- [x] `SkullbonezSource/Runtime/Direction/LookLabController.cpp` -- inspected; no replacement required
- [x] `SkullbonezSource/Runtime/Direction/LookLabController.h` -- inspected; no replacement required
- [ ] `SkullbonezSource/Runtime/Direction/LookLabGenerator.cpp` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [x] `SkullbonezSource/Runtime/Direction/LookLabGenerator.h` -- inspected; no replacement required
- [x] `SkullbonezSource/Runtime/Editor/EditorCommandHistory.cpp` -- inspected; no replacement required
- [x] `SkullbonezSource/Runtime/Editor/EditorCommandHistory.h` -- inspected; no replacement required
- [x] `SkullbonezSource/Runtime/Editor/EditorGizmoTools.cpp` -- inspected; no replacement required
- [x] `SkullbonezSource/Runtime/Editor/EditorHistory.cpp` -- inspected; no replacement required
- [ ] `SkullbonezSource/Runtime/Editor/EditorHullAssets.h` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [x] `SkullbonezSource/Runtime/Editor/EditorInteractionTools.cpp` -- inspected; no replacement required
- [x] `SkullbonezSource/Runtime/Editor/EditorObjectPlacement.cpp` -- inspected; no replacement required
- [x] `SkullbonezSource/Runtime/Editor/EditorOverlayTools.cpp` -- inspected; no replacement required
- [x] `SkullbonezSource/Runtime/Editor/EditorOverlayTools.h` -- inspected; no replacement required
- [x] `SkullbonezSource/Runtime/Editor/EditorPlacementAssets.cpp` -- inspected; no replacement required
- [x] `SkullbonezSource/Runtime/Editor/EditorPlacementAssets.h` -- inspected; no replacement required
- [x] `SkullbonezSource/Runtime/Editor/EditorTerrainOrientation.cpp` -- inspected; no replacement required
- [x] `SkullbonezSource/Runtime/Editor/EditorTerrainOrientation.h` -- inspected; no replacement required
- [x] `SkullbonezSource/Runtime/Editor/EditorTools.cpp` -- inspected; no replacement required
- [x] `SkullbonezSource/Runtime/Editor/EditorTools.h` -- inspected; no replacement required
- [x] `SkullbonezSource/Runtime/Editor/EditorTracer.cpp` -- inspected; no replacement required
- [x] `SkullbonezSource/Runtime/Editor/LauncherLaser.cpp` -- inspected; no replacement required
- [x] `SkullbonezSource/Runtime/Editor/LauncherLaser.h` -- inspected; no replacement required
- [x] `SkullbonezSource/Runtime/Editor/LauncherTools.cpp` -- inspected; no replacement required
- [x] `SkullbonezSource/Runtime/Editor/MousePickupTools.cpp` -- inspected; no replacement required
- [x] `SkullbonezSource/Runtime/Input/Input.cpp` -- inspected; no replacement required
- [x] `SkullbonezSource/Runtime/Input/Input.h` -- inspected; no replacement required
- [x] `SkullbonezSource/Runtime/Input/InputController.Bindings.cpp` -- inspected; no replacement required
- [ ] `SkullbonezSource/Runtime/Input/InputController.Bindings.h` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [x] `SkullbonezSource/Runtime/Input/InputController.cpp` -- inspected; no replacement required
- [x] `SkullbonezSource/Runtime/Input/InputController.h` -- inspected; no replacement required
- [ ] `SkullbonezSource/Runtime/Input/InputRouter.cpp` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [ ] `SkullbonezSource/Runtime/Input/InputRouter.h` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [ ] `SkullbonezSource/Runtime/Interaction/OperatorCommandTransaction.Commands.cpp` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [x] `SkullbonezSource/Runtime/Interaction/OperatorCommandTransaction.cpp` -- inspected; no replacement required
- [ ] `SkullbonezSource/Runtime/Interaction/OperatorCommandTransaction.h` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [x] `SkullbonezSource/Runtime/Interaction/RuntimeInteractionCommands.h` -- inspected; no replacement required
- [x] `SkullbonezSource/Runtime/Interaction/RuntimeInteractionController.cpp` -- inspected; no replacement required
- [x] `SkullbonezSource/Runtime/Interaction/RuntimeInteractionController.h` -- inspected; no replacement required
- [x] `SkullbonezSource/Runtime/Interaction/RuntimePickGeometry.cpp` -- inspected; no replacement required
- [x] `SkullbonezSource/Runtime/Interaction/RuntimePickGeometry.h` -- inspected; no replacement required
- [ ] `SkullbonezSource/Runtime/Interaction/RuntimePickService.cpp` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [x] `SkullbonezSource/Runtime/Interaction/RuntimePickService.h` -- inspected; no replacement required
- [x] `SkullbonezSource/Runtime/Planning/ContinuousOrbitalForecast.cpp` -- inspected; no replacement required
- [x] `SkullbonezSource/Runtime/Planning/ContinuousOrbitalForecast.h` -- inspected; no replacement required
- [x] `SkullbonezSource/Runtime/Planning/ContinuousOrbitalStability.cpp` -- inspected; no replacement required
- [x] `SkullbonezSource/Runtime/Planning/ContinuousOrbitalStability.h` -- inspected; no replacement required
- [x] `SkullbonezSource/Runtime/Planning/ReplayCauseInspection.cpp` -- inspected; no replacement required
- [x] `SkullbonezSource/Runtime/Planning/ReplayCauseInspection.h` -- inspected; no replacement required
- [x] `SkullbonezSource/Runtime/Planning/ReplayGuideArcs.cpp` -- inspected; no replacement required
- [x] `SkullbonezSource/Runtime/Planning/ReplayGuideArcs.h` -- inspected; no replacement required
- [x] `SkullbonezSource/Runtime/Planning/ReplayInterceptReadout.cpp` -- inspected; no replacement required
- [x] `SkullbonezSource/Runtime/Planning/ReplayInterceptReadout.h` -- inspected; no replacement required
- [x] `SkullbonezSource/Runtime/Planning/ReplayOverlayPackets.h` -- inspected; no replacement required
- [x] `SkullbonezSource/Runtime/Planning/ReplayOverlayRenderer.cpp` -- inspected; no replacement required
- [ ] `SkullbonezSource/Runtime/Planning/ReplayOverlayRenderer.h` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [x] `SkullbonezSource/Runtime/Planning/ReplayPlanningOverlayLayout.cpp` -- inspected; no replacement required
- [x] `SkullbonezSource/Runtime/Planning/ReplayPlanningOverlayLayout.h` -- inspected; no replacement required
- [ ] `SkullbonezSource/Runtime/Planning/ReplayPlanningRuntime.cpp` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [x] `SkullbonezSource/Runtime/Planning/ReplayPlanningRuntime.h` -- inspected; no replacement required
- [x] `SkullbonezSource/Runtime/Planning/ReplayPorkchopPanel.cpp` -- inspected; no replacement required
- [x] `SkullbonezSource/Runtime/Planning/ReplayPorkchopPanel.h` -- inspected; no replacement required
- [x] `SkullbonezSource/Runtime/Planning/ReplayTripPlanner.cpp` -- inspected; no replacement required
- [x] `SkullbonezSource/Runtime/Planning/ReplayTripPlanner.h` -- inspected; no replacement required
- [x] `SkullbonezSource/Runtime/Prediction/ContinuousPredictionProducer.cpp` -- inspected; no replacement required
- [x] `SkullbonezSource/Runtime/Prediction/ContinuousPredictionProducer.h` -- inspected; no replacement required
- [x] `SkullbonezSource/Runtime/Prediction/ContinuousPredictionSampleRing.cpp` -- inspected; no replacement required
- [x] `SkullbonezSource/Runtime/Prediction/ContinuousPredictionSampleRing.h` -- inspected; no replacement required
- [x] `SkullbonezSource/Runtime/Prediction/ReplayAuthoringCauseTree.cpp` -- inspected; no replacement required
- [x] `SkullbonezSource/Runtime/Prediction/ReplayCauseFocusSubmission.cpp` -- inspected; no replacement required
- [x] `SkullbonezSource/Runtime/Prediction/ReplayPrediction.cpp` -- inspected; no replacement required
- [x] `SkullbonezSource/Runtime/Prediction/ReplayPrediction.h` -- inspected; no replacement required
- [x] `SkullbonezSource/Runtime/Prediction/ReplayPredictionArchive.Automation.cpp` -- inspected; no replacement required
- [x] `SkullbonezSource/Runtime/Prediction/ReplayPredictionArchive.cpp` -- inspected; no replacement required
- [x] `SkullbonezSource/Runtime/Prediction/ReplayPredictionArchive.h` -- inspected; no replacement required
- [x] `SkullbonezSource/Runtime/Prediction/ReplayPredictionDrawing.cpp` -- inspected; no replacement required
- [ ] `SkullbonezSource/Runtime/Prediction/ReplayPredictionDrawing.h` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [x] `SkullbonezSource/Runtime/Prediction/ReplayPredictionPackets.h` -- inspected; no replacement required
- [x] `SkullbonezSource/Runtime/Prediction/ReplayPredictionPresentation.cpp` -- inspected; no replacement required
- [x] `SkullbonezSource/Runtime/Prediction/ReplayPredictionPresentation.h` -- inspected; no replacement required
- [x] `SkullbonezSource/Runtime/Prediction/ReplayPredictionPublication.MarkerScan.inl` -- inspected; no replacement required
- [x] `SkullbonezSource/Runtime/Prediction/ReplayPredictionPublication.cpp` -- inspected; no replacement required
- [x] `SkullbonezSource/Runtime/Prediction/ReplayPredictionPublication.h` -- inspected; no replacement required
- [x] `SkullbonezSource/Runtime/Prediction/ReplayPredictionPublicationOperations.h` -- inspected; no replacement required
- [x] `SkullbonezSource/Runtime/Prediction/ReplayPredictionReserve.cpp` -- inspected; no replacement required
- [x] `SkullbonezSource/Runtime/Prediction/ReplayPredictionReserve.h` -- inspected; no replacement required
- [x] `SkullbonezSource/Runtime/Prediction/ReplayPredictionRetainedGeometry.h` -- inspected; no replacement required
- [x] `SkullbonezSource/Runtime/Prediction/ReplayPredictionRetainedMemory.h` -- inspected; no replacement required
- [x] `SkullbonezSource/Runtime/Prediction/ReplayPredictionScheduling.cpp` -- inspected; no replacement required
- [x] `SkullbonezSource/Runtime/Prediction/ReplayPredictionScheduling.h` -- inspected; no replacement required
- [x] `SkullbonezSource/Runtime/Prediction/ReplayPredictionSolverEvidenceStore.cpp` -- inspected; no replacement required
- [ ] `SkullbonezSource/Runtime/Prediction/ReplayPredictionSolverEvidenceStore.h` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [x] `SkullbonezSource/Runtime/Prediction/ReplayPredictionTopologyPublication.cpp` -- inspected; no replacement required
- [x] `SkullbonezSource/Runtime/Prediction/ReplayPredictionView.h` -- inspected; no replacement required
- [x] `SkullbonezSource/Runtime/Prediction/TrajectoryStore.cpp` -- inspected; no replacement required
- [x] `SkullbonezSource/Runtime/Prediction/TrajectoryStore.h` -- inspected; no replacement required
- [x] `SkullbonezSource/Runtime/Render/RenderDefaultsStore.Persistence.cpp` -- inspected; no replacement required
- [x] `SkullbonezSource/Runtime/Render/RenderDefaultsStore.cpp` -- inspected; no replacement required
- [x] `SkullbonezSource/Runtime/Render/RenderDefaultsStore.h` -- inspected; no replacement required
- [x] `SkullbonezSource/Runtime/Render/RenderModelFramePublisher.cpp` -- inspected; no replacement required
- [x] `SkullbonezSource/Runtime/Render/RenderModelFramePublisher.h` -- inspected; no replacement required
- [x] `SkullbonezSource/Runtime/Render/RenderPresentationSettings.h` -- inspected; no replacement required
- [ ] `SkullbonezSource/Runtime/Render/RenderResourceLifecycle.cpp` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [x] `SkullbonezSource/Runtime/Render/RenderResourceLifecycle.h` -- inspected; no replacement required
- [ ] `SkullbonezSource/Runtime/Render/RuntimeRenderFrameValues.h` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [x] `SkullbonezSource/Runtime/Render/RuntimeRenderHost.h` -- inspected; no replacement required
- [x] `SkullbonezSource/Runtime/Render/RuntimeRenderPasses.cpp` -- inspected; no replacement required
- [x] `SkullbonezSource/Runtime/Render/RuntimeRenderPasses.h` -- inspected; no replacement required
- [x] `SkullbonezSource/Runtime/Render/RuntimeRenderResources.h` -- inspected; no replacement required
- [ ] `SkullbonezSource/Runtime/Render/RuntimeRenderer.cpp` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [x] `SkullbonezSource/Runtime/Render/RuntimeRenderer.h` -- inspected; no replacement required
- [x] `SkullbonezSource/Runtime/Render/UiDrawSubmission.cpp` -- inspected; no replacement required
- [ ] `SkullbonezSource/Runtime/Render/UiDrawSubmission.h` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [ ] `SkullbonezSource/Runtime/Render/UiTextPass.cpp` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [x] `SkullbonezSource/Runtime/Replay/ReplayArtifactHashLog.cpp` -- inspected; no replacement required
- [x] `SkullbonezSource/Runtime/Replay/ReplayArtifactHashLog.h` -- inspected; no replacement required
- [x] `SkullbonezSource/Runtime/Replay/ReplayArtifactSource.h` -- inspected; no replacement required
- [x] `SkullbonezSource/Runtime/Replay/ReplayAuthoring.h` -- inspected; no replacement required
- [x] `SkullbonezSource/Runtime/Replay/ReplayAuthoringCauseTreeInput.cpp` -- inspected; no replacement required
- [x] `SkullbonezSource/Runtime/Replay/ReplayAuthoringPackets.h` -- inspected; no replacement required
- [x] `SkullbonezSource/Runtime/Replay/ReplayAuthoringVelocity.cpp` -- inspected; no replacement required
- [x] `SkullbonezSource/Runtime/Replay/ReplayCaptureLimits.h` -- inspected; no replacement required
- [x] `SkullbonezSource/Runtime/Replay/ReplayCapturePackets.h` -- inspected; no replacement required
- [x] `SkullbonezSource/Runtime/Replay/ReplayCoordination.h` -- inspected; no replacement required
- [x] `SkullbonezSource/Runtime/Replay/ReplayEventCommand.h` -- inspected; no replacement required
- [x] `SkullbonezSource/Runtime/Replay/ReplayIdentity.h` -- inspected; no replacement required
- [x] `SkullbonezSource/Runtime/Replay/ReplayOverlayLayout.cpp` -- inspected; no replacement required
- [x] `SkullbonezSource/Runtime/Replay/ReplayOverlayLayout.h` -- inspected; no replacement required
- [x] `SkullbonezSource/Runtime/Replay/ReplayOverlaySurface.h` -- inspected; no replacement required
- [x] `SkullbonezSource/Runtime/Replay/ReplayPathPackets.h` -- inspected; no replacement required
- [x] `SkullbonezSource/Runtime/Replay/ReplayPresentation.cpp` -- inspected; no replacement required
- [x] `SkullbonezSource/Runtime/Replay/ReplayPresentation.h` -- inspected; no replacement required
- [x] `SkullbonezSource/Runtime/Replay/ReplayPresentationPackets.h` -- inspected; no replacement required
- [x] `SkullbonezSource/Runtime/Replay/ReplayPresentationSubmission.h` -- inspected; no replacement required
- [x] `SkullbonezSource/Runtime/Replay/ReplayProbeState.h` -- inspected; no replacement required
- [x] `SkullbonezSource/Runtime/Replay/ReplayRecorder.cpp` -- inspected; no replacement required
- [ ] `SkullbonezSource/Runtime/Replay/ReplayRecorder.h` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [x] `SkullbonezSource/Runtime/Replay/ReplayRestoreService.h` -- inspected; no replacement required
- [x] `SkullbonezSource/Runtime/Replay/ReplayRestoreTransactions.h` -- inspected; no replacement required
- [x] `SkullbonezSource/Runtime/Replay/ReplayRetainedMemory.h` -- inspected; no replacement required
- [x] `SkullbonezSource/Runtime/Replay/ReplayScrubber.h` -- inspected; no replacement required
- [x] `SkullbonezSource/Runtime/Replay/ReplayTimeline.cpp` -- inspected; no replacement required
- [x] `SkullbonezSource/Runtime/Replay/ReplayTimeline.h` -- inspected; no replacement required
- [x] `SkullbonezSource/Runtime/Replay/ReplayTimelinePackets.h` -- inspected; no replacement required
- [x] `SkullbonezSource/Runtime/Replay/ReplayToolPackets.h` -- inspected; no replacement required
- [x] `SkullbonezSource/Runtime/Replay/ReplayTrajectoryPackets.h` -- inspected; no replacement required
- [x] `SkullbonezSource/Runtime/Replay/ReplayV2Artifact.cpp` -- inspected; no replacement required
- [x] `SkullbonezSource/Runtime/Replay/ReplayV2Artifact.h` -- inspected; no replacement required
- [ ] `SkullbonezSource/Runtime/Replay/ReplayVisualPacket.h` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [ ] `SkullbonezSource/Runtime/Replay/ReplayVisualPacketFingerprint.cpp` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [x] `SkullbonezSource/Runtime/Replay/ReplayVisualPacketFingerprint.h` -- inspected; no replacement required
- [x] `SkullbonezSource/Runtime/RuntimeFrameViews.h` -- inspected; no replacement required
- [x] `SkullbonezSource/Runtime/Scene/SceneAuthoredSetup.CameraSlots.h` -- inspected; no replacement required
- [x] `SkullbonezSource/Runtime/Scene/SceneAuthoredSetup.InitialImpulse.h` -- inspected; no replacement required
- [x] `SkullbonezSource/Runtime/Scene/SceneAuthoredSetup.cpp` -- inspected; no replacement required
- [x] `SkullbonezSource/Runtime/Scene/SceneAuthoredSetup.h` -- inspected; no replacement required
- [x] `SkullbonezSource/Runtime/Scene/SceneAutomationGateConfiguration.h` -- inspected; no replacement required
- [x] `SkullbonezSource/Runtime/Scene/SceneCinematicPolicy.h` -- inspected; no replacement required
- [x] `SkullbonezSource/Runtime/Scene/SceneController.Creation.cpp` -- inspected; no replacement required
- [x] `SkullbonezSource/Runtime/Scene/SceneController.Load.cpp` -- inspected; no replacement required
- [x] `SkullbonezSource/Runtime/Scene/SceneController.Navigation.cpp` -- inspected; no replacement required
- [x] `SkullbonezSource/Runtime/Scene/SceneController.Style.cpp` -- inspected; no replacement required
- [x] `SkullbonezSource/Runtime/Scene/SceneController.cpp` -- inspected; no replacement required
- [x] `SkullbonezSource/Runtime/Scene/SceneController.h` -- inspected; no replacement required
- [x] `SkullbonezSource/Runtime/Scene/SceneControllerState.h` -- inspected; no replacement required
- [ ] `SkullbonezSource/Runtime/Scene/SceneEntityStore.cpp` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [ ] `SkullbonezSource/Runtime/Scene/SceneEntityStore.h` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [x] `SkullbonezSource/Runtime/Scene/SceneGeneratedControlTransaction.cpp` -- inspected; no replacement required
- [x] `SkullbonezSource/Runtime/Scene/SceneGeneratedControlTransaction.h` -- inspected; no replacement required
- [ ] `SkullbonezSource/Runtime/Scene/SceneGeneratedSetup.cpp` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [x] `SkullbonezSource/Runtime/Scene/SceneGeneratedSetup.h` -- inspected; no replacement required
- [ ] `SkullbonezSource/Runtime/Scene/SceneLifecycle.h` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [x] `SkullbonezSource/Runtime/Scene/SceneLoadPreparation.h` -- inspected; no replacement required
- [x] `SkullbonezSource/Runtime/Scene/SceneLoadPresentation.h` -- inspected; no replacement required
- [x] `SkullbonezSource/Runtime/Scene/SceneLoadRequest.h` -- inspected; no replacement required
- [x] `SkullbonezSource/Runtime/Scene/SceneLoadTransaction.Preparation.cpp` -- inspected; no replacement required
- [x] `SkullbonezSource/Runtime/Scene/SceneLoadTransaction.Presentation.cpp` -- inspected; no replacement required
- [x] `SkullbonezSource/Runtime/Scene/SceneLoadTransaction.Reset.cpp` -- inspected; no replacement required
- [x] `SkullbonezSource/Runtime/Scene/SceneLoadTransaction.h` -- inspected; no replacement required
- [x] `SkullbonezSource/Runtime/Scene/SceneNavigationModel.Browser.cpp` -- inspected; no replacement required
- [x] `SkullbonezSource/Runtime/Scene/SceneNavigationModel.cpp` -- inspected; no replacement required
- [x] `SkullbonezSource/Runtime/Scene/SceneRequestExecution.cpp` -- inspected; no replacement required
- [x] `SkullbonezSource/Runtime/Scene/SceneRequestQueue.cpp` -- inspected; no replacement required
- [x] `SkullbonezSource/Runtime/Scene/SceneRequestQueue.h` -- inspected; no replacement required
- [x] `SkullbonezSource/Runtime/Scene/SceneResetPreservation.h` -- inspected; no replacement required
- [x] `SkullbonezSource/Runtime/Scene/SceneSaveOperations.cpp` -- inspected; no replacement required
- [x] `SkullbonezSource/Runtime/Scene/SceneSaveOperations.h` -- inspected; no replacement required
- [x] `SkullbonezSource/Runtime/Scene/SceneSessionState.cpp` -- inspected; no replacement required
- [x] `SkullbonezSource/Runtime/Scene/SceneSessionState.h` -- inspected; no replacement required
- [x] `SkullbonezSource/Runtime/Scene/SceneSleepingDynamicBodyGatePolicy.h` -- inspected; no replacement required
- [x] `SkullbonezSource/Runtime/Scene/SceneTerrain.h` -- inspected; no replacement required
- [x] `SkullbonezSource/Runtime/Scene/SceneWorld.cpp` -- inspected; no replacement required
- [x] `SkullbonezSource/Runtime/Scene/SceneWorld.h` -- inspected; no replacement required
- [x] `SkullbonezSource/Runtime/Simulation/SimulationSystem.cpp` -- inspected; no replacement required
- [x] `SkullbonezSource/Runtime/Simulation/SimulationSystem.h` -- inspected; no replacement required
- [x] `SkullbonezSource/Runtime/Startup/StartupCommandLine.cpp` -- inspected; no replacement required
- [x] `SkullbonezSource/Runtime/Startup/StartupCommandLine.h` -- inspected; no replacement required
- [x] `SkullbonezSource/Runtime/Startup/StartupCrashLogging.cpp` -- inspected; no replacement required
- [x] `SkullbonezSource/Runtime/Startup/StartupCrashLogging.h` -- inspected; no replacement required
- [x] `SkullbonezSource/Runtime/Startup/StartupLaunchResolution.cpp` -- inspected; no replacement required
- [x] `SkullbonezSource/Runtime/Startup/StartupLaunchResolution.h` -- inspected; no replacement required
- [x] `SkullbonezSource/Runtime/Startup/StartupProbeHarnesses.cpp` -- inspected; no replacement required
- [x] `SkullbonezSource/Runtime/Startup/StartupProbeHarnesses.h` -- inspected; no replacement required
- [x] `SkullbonezSource/Runtime/Tools/RuntimeFileWriter.cpp` -- inspected; no replacement required
- [x] `SkullbonezSource/Runtime/Tools/RuntimeFileWriter.h` -- inspected; no replacement required
- [x] `SkullbonezSource/Runtime/Tools/RuntimeTools.cpp` -- inspected; no replacement required
- [x] `SkullbonezSource/Runtime/Tools/RuntimeTools.h` -- inspected; no replacement required
- [x] `SkullbonezSource/Runtime/UI/OperatorEditorFrameComposer.cpp` -- inspected; no replacement required
- [x] `SkullbonezSource/Runtime/UI/RenderDiagnosticsProjection.cpp` -- inspected; no replacement required
- [x] `SkullbonezSource/Runtime/UI/RenderDiagnosticsProjection.h` -- inspected; no replacement required
- [x] `SkullbonezSource/Runtime/UI/RuntimeUiSurface.h` -- inspected; no replacement required
- [x] `SkullbonezSource/Runtime/UI/RuntimeViewModel.cpp` -- inspected; no replacement required
- [x] `SkullbonezSource/Runtime/UI/RuntimeViewModel.h` -- inspected; no replacement required
- [x] `SkullbonezSource/Scene/AuthoredScene.cpp` -- inspected; no replacement required
- [x] `SkullbonezSource/Scene/AuthoredScene.h` -- inspected; no replacement required
- [x] `SkullbonezSource/Scene/AuthoredSceneParser.cpp` -- inspected; no replacement required
- [x] `SkullbonezSource/Scene/AuthoredSceneParserAssets.cpp` -- inspected; no replacement required
- [x] `SkullbonezSource/Scene/AuthoredSceneParserBodies.cpp` -- inspected; no replacement required
- [x] `SkullbonezSource/Scene/AuthoredSceneParserPresentation.cpp` -- inspected; no replacement required
- [x] `SkullbonezSource/Scene/AuthoredSceneParserRuntime.cpp` -- inspected; no replacement required
- [x] `SkullbonezSource/Scene/AuthoredSceneParserSchema.h` -- inspected; no replacement required
- [x] `SkullbonezSource/Scene/AuthoredTornadoConfig.h` -- inspected; no replacement required
- [x] `SkullbonezSource/Scene/OrbitalStabilityContract.h` -- inspected; no replacement required
- [x] `SkullbonezSource/Scene/SceneSnapshotWriter.cpp` -- inspected; no replacement required
- [x] `SkullbonezSource/Scene/SceneSnapshotWriter.h` -- inspected; no replacement required
- [ ] `SkullbonezSource/Scene/StandaloneStyleWriter.cpp` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [x] `SkullbonezSource/Scene/StandaloneStyleWriter.h` -- inspected; no replacement required
- [x] `SkullbonezSource/UI/OperatorEditorExchange.cpp` -- inspected; no replacement required
- [x] `SkullbonezSource/UI/OperatorEditorExchange.h` -- inspected; no replacement required
- [x] `SkullbonezSource/UI/UI.cpp` -- inspected; no replacement required
- [x] `SkullbonezSource/UI/UI.h` -- inspected; no replacement required
- [x] `SkullbonezSource/UI/UIBackdropBlur.cpp` -- inspected; no replacement required
- [x] `SkullbonezSource/UI/UIBackdropBlur.h` -- inspected; no replacement required
- [ ] `SkullbonezSource/UI/UIButton.cpp` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [ ] `SkullbonezSource/UI/UIButton.h` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [ ] `SkullbonezSource/UI/UICache.cpp` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [ ] `SkullbonezSource/UI/UICache.h` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [ ] `SkullbonezSource/UI/UICheckBox.cpp` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [ ] `SkullbonezSource/UI/UICheckBox.h` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [ ] `SkullbonezSource/UI/UIComboBox.cpp` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [ ] `SkullbonezSource/UI/UIComboBox.h` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [x] `SkullbonezSource/UI/UICommands.h` -- inspected; no replacement required
- [x] `SkullbonezSource/UI/UIDraw.cpp` -- inspected; no replacement required
- [x] `SkullbonezSource/UI/UIDraw.h` -- inspected; no replacement required
- [x] `SkullbonezSource/UI/UIDrawList.cpp` -- inspected; no replacement required
- [x] `SkullbonezSource/UI/UIDrawList.h` -- inspected; no replacement required
- [ ] `SkullbonezSource/UI/UIDrawWidgets.cpp` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [ ] `SkullbonezSource/UI/UIDrawWidgets.h` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [x] `SkullbonezSource/UI/UIEditorMiniPalette.cpp` -- inspected; no replacement required
- [x] `SkullbonezSource/UI/UIEditorMiniPaletteDraw.cpp` -- inspected; no replacement required
- [x] `SkullbonezSource/UI/UIFontMetrics.cpp` -- inspected; no replacement required
- [x] `SkullbonezSource/UI/UIFontMetrics.h` -- inspected; no replacement required
- [x] `SkullbonezSource/UI/UIFrameComposition.cpp` -- inspected; no replacement required
- [x] `SkullbonezSource/UI/UIFrameComposition.h` -- inspected; no replacement required
- [ ] `SkullbonezSource/UI/UIIconButton.cpp` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [ ] `SkullbonezSource/UI/UIIconButton.h` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [x] `SkullbonezSource/UI/UIInput.cpp` -- inspected; no replacement required
- [x] `SkullbonezSource/UI/UIInput.h` -- inspected; no replacement required
- [ ] `SkullbonezSource/UI/UILayout.cpp` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [ ] `SkullbonezSource/UI/UILayout.h` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [x] `SkullbonezSource/UI/UIProfilerOverlayPresenter.cpp` -- inspected; no replacement required
- [x] `SkullbonezSource/UI/UIProfilerOverlayPresenter.h` -- inspected; no replacement required
- [x] `SkullbonezSource/UI/UIRenderAuthoringCatalog.h` -- inspected; no replacement required
- [x] `SkullbonezSource/UI/UIRenderDiagnostics.h` -- inspected; no replacement required
- [x] `SkullbonezSource/UI/UISceneNavigationModel.h` -- inspected; no replacement required
- [ ] `SkullbonezSource/UI/UIScrollBar.cpp` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [ ] `SkullbonezSource/UI/UIScrollBar.h` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [ ] `SkullbonezSource/UI/UISlider.cpp` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [ ] `SkullbonezSource/UI/UISlider.h` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [ ] `SkullbonezSource/UI/UIState.h` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [ ] `SkullbonezSource/UI/UIStyle.cpp` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [ ] `SkullbonezSource/UI/UIStyle.h` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [ ] `SkullbonezSource/UI/UITabBar.cpp` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [ ] `SkullbonezSource/UI/UITabBar.h` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [x] `SkullbonezSource/UI/UITabCinematic.cpp` -- inspected; no replacement required
- [x] `SkullbonezSource/UI/UITabCinematic.h` -- inspected; no replacement required
- [ ] `SkullbonezSource/UI/UITabControls.cpp` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [ ] `SkullbonezSource/UI/UITabControls.h` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [x] `SkullbonezSource/UI/UITabEditor.cpp` -- inspected; no replacement required
- [x] `SkullbonezSource/UI/UITabEditor.h` -- inspected; no replacement required
- [x] `SkullbonezSource/UI/UITabMemory.cpp` -- inspected; no replacement required
- [x] `SkullbonezSource/UI/UITabMemory.h` -- inspected; no replacement required
- [ ] `SkullbonezSource/UI/UITabOptions.cpp` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [ ] `SkullbonezSource/UI/UITabOptions.h` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [ ] `SkullbonezSource/UI/UITabPhysics.cpp` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [ ] `SkullbonezSource/UI/UITabPhysics.h` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [x] `SkullbonezSource/UI/UITabProfiler.cpp` -- inspected; no replacement required
- [ ] `SkullbonezSource/UI/UITabProfiler.h` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [x] `SkullbonezSource/UI/UITabProfilerHistogram.cpp` -- inspected; no replacement required
- [ ] `SkullbonezSource/UI/UITabScene.cpp` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [ ] `SkullbonezSource/UI/UITabScene.h` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [x] `SkullbonezSource/UI/UITabSky.cpp` -- inspected; no replacement required
- [x] `SkullbonezSource/UI/UITabSky.h` -- inspected; no replacement required
- [ ] `SkullbonezSource/UI/UIWindowChrome.cpp` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [ ] `SkullbonezSource/UI/UIWindowChrome.h` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [x] `SkullbonezSource/UI/UIWindowInteractionOwner.cpp` -- inspected; no replacement required
- [ ] `SkullbonezSource/UI/UIWindowInteractionOwner.h` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [x] `SkullbonezSource/World/FluidSurfaceAdjustment.h` -- inspected; no replacement required
- [x] `SkullbonezSource/World/SkyBox.cpp` -- inspected; no replacement required
- [x] `SkullbonezSource/World/SkyBox.h` -- inspected; no replacement required
- [x] `SkullbonezSource/World/Terrain.cpp` -- inspected; no replacement required
- [x] `SkullbonezSource/World/Terrain.h` -- inspected; no replacement required
- [ ] `SkullbonezSource/World/WorldEnvironment.cpp` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [x] `SkullbonezSource/World/WorldEnvironment.h` -- inspected; no replacement required

### `SkullbonezTests`

- [ ] `SkullbonezTests/TestApplicationExitState.cpp` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [ ] `SkullbonezTests/TestAssetSystem.cpp` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [x] `SkullbonezTests/TestBounds.cpp` -- inspected; no replacement required
- [x] `SkullbonezTests/TestCamera.cpp` -- inspected; no replacement required
- [ ] `SkullbonezTests/TestColliderStoreFixtures.h` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [ ] `SkullbonezTests/TestCollisionShapeFixtures.h` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [x] `SkullbonezTests/TestConfig.cpp` -- inspected; no replacement required
- [x] `SkullbonezTests/TestContinuousOrbitalStability.cpp` -- inspected; no replacement required
- [x] `SkullbonezTests/TestContinuousPredictionProducer.cpp` -- inspected; no replacement required
- [x] `SkullbonezTests/TestConvexHull.cpp` -- inspected; no replacement required
- [x] `SkullbonezTests/TestDeterminism.cpp` -- inspected; no replacement required
- [ ] `SkullbonezTests/TestDeterministicMath.cpp` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [x] `SkullbonezTests/TestDx12CachedPsoStore.cpp` -- inspected; no replacement required
- [x] `SkullbonezTests/TestDx12OnlyRuntime.cpp` -- inspected; no replacement required
- [x] `SkullbonezTests/TestEditorCommandHistory.cpp` -- inspected; no replacement required
- [x] `SkullbonezTests/TestEditorTerrainOrientation.cpp` -- inspected; no replacement required
- [ ] `SkullbonezTests/TestFatalCases.h` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [x] `SkullbonezTests/TestFixedSeed.h` -- inspected; no replacement required
- [x] `SkullbonezTests/TestFrustum.cpp` -- inspected; no replacement required
- [x] `SkullbonezTests/TestGeometricMath.cpp` -- inspected; no replacement required
- [ ] `SkullbonezTests/TestInputRouter.cpp` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [ ] `SkullbonezTests/TestLookLabController.cpp` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [x] `SkullbonezTests/TestLookLabGenerator.cpp` -- inspected; no replacement required
- [ ] `SkullbonezTests/TestLookLabSerialization.cpp` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [x] `SkullbonezTests/TestMain.cpp` -- inspected; no replacement required
- [x] `SkullbonezTests/TestMatrix4.cpp` -- inspected; no replacement required
- [ ] `SkullbonezTests/TestObjectContactManifold.cpp` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [ ] `SkullbonezTests/TestOperatorCommandTransaction.cpp` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [x] `SkullbonezTests/TestOrbitalMechanics.cpp` -- inspected; no replacement required
- [ ] `SkullbonezTests/TestOwnerRequestQueues.cpp` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [ ] `SkullbonezTests/TestPersistentContactSolver.cpp` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [ ] `SkullbonezTests/TestPhysicsApi.cpp` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [ ] `SkullbonezTests/TestPhysicsHandles.cpp` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [x] `SkullbonezTests/TestPhysicsPoseIntegration.cpp` -- inspected; no replacement required
- [ ] `SkullbonezTests/TestPhysicsStageState.cpp` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [ ] `SkullbonezTests/TestQuaternion.cpp` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [x] `SkullbonezTests/TestRagdoll.cpp` -- inspected; no replacement required
- [x] `SkullbonezTests/TestRenderGraph.cpp` -- inspected; no replacement required
- [x] `SkullbonezTests/TestReplayArtifact.cpp` -- inspected; no replacement required
- [ ] `SkullbonezTests/TestReplayCauseInspection.cpp` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [x] `SkullbonezTests/TestReplayDeterminism.cpp` -- inspected; no replacement required
- [x] `SkullbonezTests/TestReplayGuideArcs.cpp` -- inspected; no replacement required
- [x] `SkullbonezTests/TestReplayInterceptReadout.cpp` -- inspected; no replacement required
- [x] `SkullbonezTests/TestReplayPorkchopPanel.cpp` -- inspected; no replacement required
- [ ] `SkullbonezTests/TestReplayPredictionScheduling.cpp` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [x] `SkullbonezTests/TestReplayPredictionSolverEvidenceStore.cpp` -- inspected; no replacement required
- [x] `SkullbonezTests/TestReplayRecorder.cpp` -- inspected; no replacement required
- [x] `SkullbonezTests/TestReplayRecorderFullCaptureBoundary.cpp` -- inspected; no replacement required
- [x] `SkullbonezTests/TestReplayTripPlanner.cpp` -- inspected; no replacement required
- [ ] `SkullbonezTests/TestReplayVisualPacket.cpp` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [ ] `SkullbonezTests/TestReserveAllocator.cpp` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [ ] `SkullbonezTests/TestResultLoadFixtures.h` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [ ] `SkullbonezTests/TestRuntimeContracts.cpp` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [ ] `SkullbonezTests/TestRuntimeInputBindings.cpp` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [ ] `SkullbonezTests/TestRuntimeValueSeams.cpp` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [x] `SkullbonezTests/TestSbResult.cpp` -- inspected; no replacement required
- [x] `SkullbonezTests/TestSbResultAccess.h` -- inspected; no replacement required
- [x] `SkullbonezTests/TestSceneAuthoredImpulseSetup.cpp` -- inspected; no replacement required
- [x] `SkullbonezTests/TestSceneAutomationGates.cpp` -- inspected; no replacement required
- [ ] `SkullbonezTests/TestSceneEntityStore.cpp` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [x] `SkullbonezTests/TestSceneParserUnit.cpp` -- inspected; no replacement required
- [ ] `SkullbonezTests/TestSceneSnapshotWriter.cpp` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [x] `SkullbonezTests/TestShaderReflectionContracts.cpp` -- inspected; no replacement required
- [x] `SkullbonezTests/TestShadow.cpp` -- inspected; no replacement required
- [x] `SkullbonezTests/TestSimulationSystem.cpp` -- inspected; no replacement required
- [ ] `SkullbonezTests/TestSleepController.cpp` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [x] `SkullbonezTests/TestSmoke.cpp` -- inspected; no replacement required
- [x] `SkullbonezTests/TestSolverBroadphaseStage.cpp` -- inspected; no replacement required
- [x] `SkullbonezTests/TestSpatialGrid.cpp` -- inspected; no replacement required
- [ ] `SkullbonezTests/TestStartup.cpp` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [ ] `SkullbonezTests/TestTerrain.cpp` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [ ] `SkullbonezTests/TestUIDrawValues.cpp` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [ ] `SkullbonezTests/TestVector3.cpp` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`

### `tools`

- [ ] `tools/agent_validate.bat` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [ ] `tools/align_header_inline_comments.py` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [x] `tools/analyze_at_rest_stability.py` -- inspected; no replacement required (late-arriving in-flight file)
- [x] `tools/analyze_replay_prediction_spikes.py` -- inspected; no replacement required
- [ ] `tools/archive_validation_artifacts.bat` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [ ] `tools/archive_validation_artifacts.py` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [ ] `tools/bake_hulls.bat` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [ ] `tools/bake_hulls.py` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [x] `tools/bake_shaders.bat` -- inspected; no replacement required
- [ ] `tools/bake_shaders.py` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [ ] `tools/capture_ui_screenshot.bat` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [ ] `tools/check_allocation_policy.py` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [x] `tools/check_at_rest_stability_analyzer.py` -- inspected; no replacement required (late-arriving in-flight file)
- [ ] `tools/check_broadphase_pair_stream_oracle.py` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [ ] `tools/check_build_config_consistency.py` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [x] `tools/check_causal_tree_interaction.py` -- inspected; no replacement required
- [ ] `tools/check_contact_energy_scenes.py` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [ ] `tools/check_coverage.py` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [x] `tools/check_dependency_graph.py` -- inspected; no replacement required
- [x] `tools/check_determinism_math_policy.py` -- inspected; no replacement required
- [ ] `tools/check_dx12_baselines.py` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [ ] `tools/check_dx12_validation.bat` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [ ] `tools/check_perf_budgets.py` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [ ] `tools/check_physics_known_issue_regression.py` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [ ] `tools/check_physics_query_regression.py` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [ ] `tools/check_physics_regression.py` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [ ] `tools/check_related_paths.py` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [x] `tools/check_replay_prediction_determinism.py` -- inspected; no replacement required
- [ ] `tools/check_replay_scrub_regression.py` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [x] `tools/check_replay_v2_artifact.py` -- inspected; no replacement required
- [ ] `tools/check_replay_visual_fidelity.py` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [ ] `tools/check_shooting_reaction.py` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [ ] `tools/check_staged_file_sizes.py` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [ ] `tools/check_ui_blur.py` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [x] `tools/codex_usage_daily.bat` -- inspected; no replacement required
- [x] `tools/cpp_source_scan.py` -- inspected; no replacement required
- [ ] `tools/export_screenshot_png.py` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [ ] `tools/find_clang_format.bat` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [ ] `tools/find_git.bat` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [ ] `tools/find_msbuild.bat` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [ ] `tools/find_python.bat` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [ ] `tools/format_fix.bat` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [ ] `tools/generate_physics_scale_sleepy_scene.py` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [ ] `tools/inventory_authority_free_aggregates.py` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [ ] `tools/inventory_extraction_scars.py` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [ ] `tools/inventory_function_complexity.py` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [ ] `tools/inventory_glossary_terms.py` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [ ] `tools/inventory_unreachable_symbols.py` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [x] `tools/inventory_wide_signatures.py` -- inspected; no replacement required
- [ ] `tools/launch_tracy_viewer.bat` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [ ] `tools/loc_count.bat` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [x] `tools/measure_causal_inspection_perf.py` -- inspected; no replacement required
- [x] `tools/measure_dense_pile_sleep.py` -- inspected; no replacement required
- [x] `tools/migrate_data_formats.py` -- inspected; no replacement required
- [ ] `tools/physics_query.bat` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [x] `tools/physics_query.py` -- inspected; no replacement required
- [ ] `tools/refresh_hulls.bat` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [ ] `tools/replay_query.bat` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [x] `tools/replay_query.py` -- inspected; no replacement required
- [ ] `tools/run_graphics_stress.bat` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [ ] `tools/run_graphics_stress.ps1` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [x] `tools/separate_multiline_cpp_declarations.py` -- inspected; no replacement required
- [ ] `tools/style_harness.bat` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [ ] `tools/style_harness.ps1` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [x] `tools/test_analyze_replay_prediction_spikes.py` -- inspected; no replacement required
- [ ] `tools/update_baselines.bat` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [ ] `tools/update_baselines.py` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [ ] `tools/validate_all_cpu_tests.bat` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [ ] `tools/validate_alt_velocity_visualization.bat` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [x] `tools/validate_at_rest_stability.bat` -- inspected; no replacement required (late-arriving in-flight file)
- [ ] `tools/validate_automation.bat` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [ ] `tools/validate_build.bat` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [ ] `tools/validate_build_all.bat` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [ ] `tools/validate_concepts.bat` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [ ] `tools/validate_concepts.py` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [ ] `tools/validate_coverage.bat` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [ ] `tools/validate_deep.bat` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [ ] `tools/validate_demo_stress.bat` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [x] `tools/validate_dependency_graph.bat` -- inspected; no replacement required
- [ ] `tools/validate_dx12_arch_tests.bat` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [ ] `tools/validate_dx12_fault_injection.bat` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [ ] `tools/validate_dx12_renderer.bat` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [x] `tools/validate_fast.bat` -- inspected; no replacement required
- [ ] `tools/validate_format.bat` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [x] `tools/validate_full.bat` -- inspected; no replacement required
- [ ] `tools/validate_interaction_clicks.bat` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [x] `tools/validate_look_lab_reuse.py` -- inspected; no replacement required
- [x] `tools/validate_native_diagnostics.bat` -- inspected; no replacement required
- [x] `tools/validate_native_diagnostics.py` -- inspected; no replacement required
- [ ] `tools/validate_perf.bat` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [x] `tools/validate_physics.bat` -- inspected; no replacement required
- [ ] `tools/validate_physics_deep.bat` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [ ] `tools/validate_physics_query.bat` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [ ] `tools/validate_project_filters.bat` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [x] `tools/validate_project_filters.py` -- inspected; no replacement required
- [ ] `tools/validate_ready_builds.bat` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [ ] `tools/validate_renderers.bat` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [x] `tools/validate_replay_allocation_policy.bat` -- inspected; no replacement required
- [x] `tools/validate_replay_prediction_frame_spikes.bat` -- inspected; no replacement required
- [ ] `tools/validate_replay_scrub.bat` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [ ] `tools/validate_replay_v2_artifact.bat` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [x] `tools/validate_replay_visual_fidelity.bat` -- inspected; no replacement required
- [ ] `tools/validate_runtime_interaction_policy.bat` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [ ] `tools/validate_scene_loads.bat` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [ ] `tools/validate_scene_loads.py` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [ ] `tools/validate_scene_parser_tests.bat` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [ ] `tools/validate_select.bat` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [ ] `tools/validate_shaders.bat` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [ ] `tools/validate_shaders.py` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [ ] `tools/validate_tests.bat` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [x] `tools/validate_ui.bat` -- inspected; no replacement required
- [x] `tools/validate_ui_boundary_tests.bat` -- inspected; no replacement required
- [ ] `tools/validate_ui_stress.bat` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [ ] `tools/watch_demo_stress.bat` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`
- [ ] `tools/watch_ui_stress.bat` -- replacement queued in `Agentic/Plans/TODO/full-source-comment-truth-replacement.patch`

## Reconciliation Totals

- Current tracked inventory: 843
- Inspected: 843
- Checked / no replacement required: 582
- Unchecked / exact replacement queued: 261
- Deferred without an exact disposition: 0
