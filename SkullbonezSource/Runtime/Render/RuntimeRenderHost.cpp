/*
File: SkullbonezSource/Runtime/Render/RuntimeRenderHost.cpp
Purpose:
  Keeps the runtime render-host binding translation unit available.

Mental model:
  Runtime render host state was reduced to typed startup bindings. Renderer and
  pass state travel through those bindings owned by RuntimeRenderer.

Glossary:
  Render binding: Startup borrow set used while Run remains the broader composition root.
  Pass: Ordered unit of frame rendering owned by RuntimeRenderer.
  Replay ghost: Transparent predicted-body draw used to preview replay future
  path samples.

Invariants:
  - Render bindings borrow Run-owned targets and must not take ownership.
  - RuntimeRenderer owns pass order and render scratch state.

Related:
  - SkullbonezSource/Runtime/Render/RuntimeRenderHost.h
  - SkullbonezSource/Runtime/Render/RuntimeRenderPasses.h
  - Agentic/Plans/TODO/runtime-shell-decomposition.md
*/
#include "RuntimeRenderHost.h"
