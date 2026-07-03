/*
File: SkullbonezSource/Rendering/GameModelRenderer.h
Purpose:
  Converts GameModel data into backend draw calls for normal and shadow rendering.

Mental model:
  Renderer-facing code translates engine concepts into backend resources, draw
  calls, shader bindings, and validation artifacts.

Glossary:
  Descriptor: Small binding record that tells a renderer how to interpret a
  resource.
  Back buffer: Swap-chain image that will be presented to the window.

Invariants:
  - Methods translate model collections into render work; they do not own model
    storage or backend lifetime.
  - Shadow batch structs are CPU-side preparation data and must be submitted
    through backend-facing helpers.

Related:
  - SkullbonezSource/Rendering/GameModelRenderer.cpp
  - Agentic/Reference/comment-style-guide.md
*/
#pragma once

#include "../Maths/Matrix4.h"
#include "Shadow.h"
#include "../Maths/Vector3.h"

#include <cstdint>
#include <vector>

namespace SkullbonezCore
{
namespace Basics
{
struct CinematicRenderConfig;
struct RenderHelperContext;
} // namespace Basics

namespace GameObjects
{
class GameModelCollection;

class GameModelRenderer
{
  public:
    static void RenderModels( const Basics::RenderHelperContext& helperContext,
                              GameModelCollection& collection,
                              const Math::Transformation::Matrix4& view,
                              const Math::Transformation::Matrix4& proj,
                              const float lightPos[4],
                              const Basics::CinematicRenderConfig* cinematic,
                              const Rendering::ShadowFrameData* shadow,
                              float materialAlpha,
                              const std::vector<uint8_t>* modelMask = nullptr,
                              bool drawMaskedModels = true );
    static void BuildShadowCasterBatches( GameModelCollection& collection, Rendering::ShadowCasterBatches& outBatches );
    static void SubmitShadowCasterBatches( const Basics::RenderHelperContext& helperContext,
                                           const Rendering::ShadowCasterBatches& batches,
                                           const Math::Transformation::Matrix4& view,
                                           const Math::Transformation::Matrix4& proj,
                                           const Basics::CinematicRenderConfig* cinematic );
    static void RenderShadowCasters( const Basics::RenderHelperContext& helperContext,
                                     GameModelCollection& collection,
                                     const Math::Transformation::Matrix4& view,
                                     const Math::Transformation::Matrix4& proj,
                                     const Basics::CinematicRenderConfig* cinematic );
    static bool GetObjectShadowBounds( GameModelCollection& collection,
                                       const Math::Vector::Vector3& focus,
                                       float maxDistance,
                                       Math::Vector::Vector3& outCenter,
                                       float& outRadius,
                                       float& outHeightRange );
    static void ResetRenderResources();
};
} // namespace GameObjects
} // namespace SkullbonezCore
