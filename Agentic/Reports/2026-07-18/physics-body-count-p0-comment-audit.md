# Physics Body-Count P0 Comment Audit

Date: 2026-07-18

Guide: `Agentic/Reference/comment-style-guide.md`

Audit skill: `Agentic/Skills/comment-style-audit/skill.md`

Result: **16/16 touched source-bearing files inspected and compliant; 0
deferred.** Learning headers and nearby concept, invariant, lifetime, hazard,
units, and validation comments were checked in the context of the P0 changes.

- [x] `Agentic/Skills/skore-render-test/analyze_perf.py`
- [x] `SkullbonezSource/Core/Profiler.cpp`
- [x] `SkullbonezSource/Core/Profiler.h`
- [x] `SkullbonezSource/Physics/PhysicsWorld.cpp`
- [x] `SkullbonezSource/Physics/Stages/PhysicsSleepController.State.cpp`
- [x] `SkullbonezSource/Physics/Stages/PhysicsSleepController.cpp`
- [x] `SkullbonezSource/Physics/Stages/PhysicsSleepController.h`
- [x] `SkullbonezSource/Runtime/InputFrameExecution.cpp`
- [x] `SkullbonezSource/Runtime/Run.cpp`
- [x] `SkullbonezSource/Runtime/RunFrame.cpp`
- [x] `SkullbonezSource/Runtime/RuntimeStressController.cpp`
- [x] `SkullbonezSource/Runtime/Scene/RunScene.cpp`
- [x] `SkullbonezTests/TestPhysicsStageState.cpp`
- [x] `SkullbonezTests/TestStartup.cpp`
- [x] `tools/generate_physics_scale_sleepy_scene.py`
- [x] `tools/validate_perf.bat`

No file is intentionally deferred. The generated JSON scene, Markdown files,
and `tools/README.md` are not source-bearing files under this audit contract.
