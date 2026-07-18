/*
File: SkullbonezSource/Rendering/DX12/RenderBackendDX12.Readback.cpp
Purpose:
  Adapts the retained render-capture interface to the concrete DX12 capture owner.

Summary:
  The backend remains the interface composition root, but screenshot resources,
  conversion, submission policy, and quarantine state live entirely in
  Dx12BackbufferCapture.

Glossary:
  Capture owner: Concrete value member responsible for the complete screenshot
    readback lifecycle.
  Frame capability: Restricted borrow exposing only backbuffer copy and
    synchronous submission operations.

Invariants:
  - This translation unit retains no capture state or raw COM ownership.
  - Output dimensions publish only for a valid current backend extent.
  - The public IRenderCaptureBackend signature remains unchanged.

Related:
  - SkullbonezSource/Rendering/DX12/Dx12BackbufferCapture.cpp
  - SkullbonezSource/Rendering/DX12/RenderBackendDX12.h
  - SkullbonezSource/Rendering/IRenderCaptureBackend.h
*/
#include "RenderBackendDX12.h"

using namespace SkullbonezCore::Rendering;

SkullbonezCore::Core::SbResult
RenderBackendDX12::CaptureBackbuffer( std::vector<uint8_t>& outPixels, int& outWidth, int& outHeight )
{
    return m_backbufferCapture.Capture( m_frameOwner.CaptureFrame(),
                                        m_renderDevice.Width(),
                                        m_renderDevice.Height(),
                                        outPixels,
                                        outWidth,
                                        outHeight );
}
