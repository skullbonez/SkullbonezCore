/*
File: SkullbonezSource/Rendering/IRenderCaptureBackend.h
Purpose:
  Declares the narrow render capability used for screenshot and readback capture.

Mental model:
  Capture paths need only feature metadata and a backbuffer readback. They should
  not receive the full render backend surface that can create resources, mutate
  frame state, or submit draw calls.

Glossary:
  Back buffer: Swap-chain image that will be presented to the window.
  BGR (Blue, Green, Red): Capture byte order used by BMP files.
  BMP (Bitmap): Simple image file format used by validation backbuffer captures.

Invariants:
  - This interface reports only capture/readback support. GPU timer, DXR, debug
    draw, and resource-creation capabilities belong to broader render services.
  - Capture data is BGR and bottom-up so validation artifacts can be written to
    BMP without a second image-layout conversion.
  - The capture capability is borrowed from the active renderer through runtime
    startup wiring and must not be retained across backend teardown.

Related:
  - SkullbonezSource/Rendering/IRenderBackend.h
  - SkullbonezSource/Runtime/CaptureSystem.h
*/
#pragma once

#include <cstdint>
#include <vector>

namespace SkullbonezCore
{
namespace Rendering
{
class IRenderCaptureBackend
{
  public:
    virtual ~IRenderCaptureBackend() = default;

    virtual bool SupportsBackbufferCapture() const = 0;
    virtual std::vector<uint8_t> CaptureBackbuffer( int& outWidth, int& outHeight ) = 0;
};

} // namespace Rendering
} // namespace SkullbonezCore
