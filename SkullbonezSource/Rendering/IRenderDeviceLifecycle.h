/*
File: SkullbonezSource/Rendering/IRenderDeviceLifecycle.h
Purpose:
  Declares the narrow render capability for backend startup, shutdown, resize,
  presentation, and GPU drain operations.

Summary:
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
  Terminal resource drain: Shutdown-only proof that previously submitted work
    is complete without reopening or submitting a failed recording epoch.
  HWND (Window Handle): Win32 identifier for the native application window.
  HDC (Handle to Device Context): Win32 drawing context paired with an HWND.
  Lane R result: Recoverable device, window, or driver failure reported with an
    owner/message so startup or the frame loop can exit cleanly.

Invariants:
  - Lifecycle methods are valid only on the thread/path that owns renderer
    startup and shutdown.
  - Finish/FlushGPU must complete submitted GPU work before resources they may
    reference are destroyed. Both report recording, submission-drain, or reopen
    failures so callers cannot continue into unsafe resource mutation.
  - DrainForResourceRelease is terminal: it may prove already-submitted work
    complete after a retained recording failure, but it never reopens or submits
    the failed recording epoch.
  - Shutdown is terminal: it must prove command-queue and present-queue drains
    before releasing resources, and uses Lane F when that proof cannot return
    safely to a caller.
  - Init, Present, Finish, FlushGPU, and Resize return Lane R results for
    environment failures; callers decide whether to show UI, write logs, or end
    the message loop.
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
    virtual Basics::SbResult Present() = 0;
    virtual void SetVsyncEnabled( bool enabled ) = 0;
    virtual bool IsVsyncEnabled() const = 0;
    virtual Basics::SbResult Finish() = 0;
    virtual Basics::SbResult FlushGPU() = 0;
    virtual Basics::SbResult DrainForResourceRelease() = 0;
    virtual Basics::SbResult Resize( int width, int height ) = 0;
    virtual int GetWidth() const = 0;
    virtual int GetHeight() const = 0;
};

} // namespace Rendering
} // namespace SkullbonezCore
