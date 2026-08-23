/*
File: SkullbonezSource/Runtime/Scene/SceneRenderPolicy.h
Purpose:
  Carries authored scene render policy and cold activation requests as detached values.

Summary:
  Scene resolves authored/default policy and the capacity needed by a newly
  populated scene. App applies these values to the Render owner at the ordered
  cold-load boundary; Scene never borrows RuntimeRenderer.

Invariants:
  - Values contain no Render owner, callback, or retained capability.
  - App completes ray-tracing activation before Scene publishes AfterSceneActivated.

Related:
  - SkullbonezSource/Runtime/App/SceneLoadApplication.cpp
  - SkullbonezSource/Runtime/Scene/SceneLoadTransaction.h
*/
#pragma once

namespace SkullbonezCore
{
namespace Runtime
{
struct SceneRenderPolicyState
{
    bool vsyncEnabled = true;
    bool pipelineSyncEnabled = false;
};

} // namespace Runtime
} // namespace SkullbonezCore
