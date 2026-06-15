/*
File: SkullbonezSource/SkullbonezGameModelRenderer.h
Purpose:
  Converts GameModel data into backend draw calls for normal and shadow rendering.

Mental model:
  Renderer-facing code translates engine concepts into backend resources, draw
  calls, shader bindings, and validation artifacts.

Glossary:
  Descriptor: Small binding record that tells a renderer how to interpret a
  resource.
  Back buffer: Swap-chain image that will be presented to the window.

Related:
  - SkullbonezSource/SkullbonezGameModelRenderer.cpp
  - Agentic/Reference/comment-style-guide.md
*/
#pragma once

#include "SkullbonezMatrix4.h"
#include "SkullbonezShadow.h"
#include "SkullbonezVector3.h"

namespace SkullbonezCore
{
namespace Basics
{
struct CinematicRenderConfig;
}

namespace GameObjects
{
class GameModelCollection;

class GameModelRenderer
{
  public:
    static void RenderModels( GameModelCollection& collection, const Math::Transformation::Matrix4& view, const Math::Transformation::Matrix4& proj, const float lightPos[4], const Basics::CinematicRenderConfig* cinematic, const Rendering::ShadowFrameData* shadow, float materialAlpha );
    static void RenderShadowCasters( GameModelCollection& collection, const Math::Transformation::Matrix4& view, const Math::Transformation::Matrix4& proj, const Basics::CinematicRenderConfig* cinematic );
    static bool GetObjectShadowBounds( GameModelCollection& collection, const Math::Vector::Vector3& focus, float maxDistance, Math::Vector::Vector3& outCenter, float& outRadius, float& outHeightRange );
    static void ResetRenderResources();
};
} // namespace GameObjects
} // namespace SkullbonezCore
