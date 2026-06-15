/*
File: SkullbonezSource/SkullbonezCaptureSystem.h
Purpose:
  Captures frame output to screenshots for scenes, validation, and look-dev.

Mental model:
  Renderer-facing code translates engine concepts into backend resources, draw
  calls, shader bindings, and validation artifacts.

Glossary:
  Descriptor: Small binding record that tells a renderer how to interpret a
  resource.
  Back buffer: Swap-chain image that will be presented to the window.

Related:
  - SkullbonezSource/SkullbonezCaptureSystem.cpp
  - Agentic/Reference/comment-style-guide.md
*/
#pragma once

namespace SkullbonezCore
{
namespace Rendering
{
class IRenderBackend;
}

namespace Basics
{
class CaptureSystem
{
  public:
    static void SaveBackbufferBmp( Rendering::IRenderBackend& backend, const char* path );
};
} // namespace Basics
} // namespace SkullbonezCore
