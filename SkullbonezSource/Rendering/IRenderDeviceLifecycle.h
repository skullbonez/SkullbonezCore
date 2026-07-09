/*
File: SkullbonezSource/Rendering/IRenderDeviceLifecycle.h
Purpose:
  Declares the narrow render capability for backend startup, shutdown, resize,
  presentation, and GPU drain operations.

Mental model:
  Device-lifecycle callers own process or window lifetime. They may initialize
  the renderer, resize swap-chain-sized resources, present a completed frame,
  and wait for GPU work to finish before resource destruction.

Glossary:
  Render device: Engine-facing object that owns the active GPU backend.
  Swap chain: Window-presented image queue whose current image becomes the back
    buffer.
  Back buffer: Swap-chain image that will be presented to the window.
  Vsync (Vertical Synchronization): Presentation pacing that waits for monitor
    refresh instead of presenting immediately.
  GPU flush: CPU wait until submitted GPU work has completed.
  HWND (Window Handle): Win32 identifier for the native application window.
  HDC (Handle to Device Context): Win32 drawing context paired with an HWND.

Invariants:
  - Lifecycle methods are valid only on the thread/path that owns renderer
    startup and shutdown.
  - Finish/FlushGPU must complete submitted GPU work before resources they may
    reference are destroyed.
  - Width and height describe the active backend surface after initialization or
    resize.

Related:
  - SkullbonezSource/Rendering/DX12/RenderBackendDX12.h
  - SkullbonezSource/Runtime/Init.cpp
*/
#pragma once

#include "../Core/SbResult.h"

#include <windows.h>

namespace SkullbonezCore
{
namespace Rendering
{

class IRenderDeviceLifecycle
{
  public:
    virtual ~IRenderDeviceLifecycle() = default;

    virtual Basics::SbResult Init( HWND hwnd, HDC hdc, int width, int height ) = 0;
    virtual void Shutdown() = 0;
    virtual void Present() = 0;
    virtual void SetVsyncEnabled( bool enabled ) = 0;
    virtual bool IsVsyncEnabled() const = 0;
    virtual void Finish() = 0;
    virtual void FlushGPU() = 0;
    virtual void Resize( int width, int height ) = 0;
    virtual int GetWidth() const = 0;
    virtual int GetHeight() const = 0;
};

} // namespace Rendering
} // namespace SkullbonezCore
