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
