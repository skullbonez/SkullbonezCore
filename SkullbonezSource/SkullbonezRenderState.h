#pragma once

namespace SkullbonezCore
{
namespace Rendering
{
class RenderState
{
  public:
    static void SetupInitial();
    static void EnableDepthTest();
    static void DisableDepthTest();
    static void EnableBlending();
    static void DisableBlending();
    static void SetClearColor( float r, float g, float b, float a );
    static void SetClearDepth( float depth );
    static void ClearFramebuffer();
};
} // namespace Rendering
} // namespace SkullbonezCore
