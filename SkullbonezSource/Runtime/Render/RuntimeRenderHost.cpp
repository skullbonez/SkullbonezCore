/*
File: SkullbonezSource/Runtime/Render/RuntimeRenderHost.cpp
Purpose:
  Keeps the runtime render-host callback translation unit available.

Mental model:
  RuntimeRenderHost now owns only callback forwarding. Renderer and pass state
  travel through typed bindings owned by RuntimeRenderer.

Glossary:
  Render host: Callback holder used while Run remains the broader composition root.
  Pass: Ordered unit of frame rendering owned by RuntimeRenderer.
  Replay ghost: Transparent predicted-body draw used to preview replay future
  path samples.

Invariants:
  - Host callbacks borrow the Run-owned target and must not take ownership.
  - RuntimeRenderer owns pass order and render scratch state.

Related:
  - SkullbonezSource/Runtime/Render/RuntimeRenderHost.h
  - SkullbonezSource/Runtime/Render/RuntimeRenderPasses.h
  - Agentic/Plans/run-composition-root-shrink-plan.md
*/
#include "RuntimeRenderHost.h"
