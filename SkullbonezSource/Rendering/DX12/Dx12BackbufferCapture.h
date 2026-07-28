/*
File: SkullbonezSource/Rendering/DX12/Dx12BackbufferCapture.h
Purpose:
  Owns DX12 screenshot readback and uncertain-submission quarantine state.

Summary:
  Backbuffer capture is a cold, synchronous GPU-to-CPU transfer. This owner
  records the copy through a restricted frame capability, converts the mapped
  pixels to BMP-ready BGR, and retains any readback whose safe release cannot
  be proved until terminal queue drain.

Glossary:
  Readback buffer: CPU-readable landing resource for a GPU texture copy.
  Quarantine: Fixed COM-reference array retained after an uncertain Close or
    fence-wait result.
  Terminal drain: Shutdown proof that all submitted GPU and present work has
    completed, making quarantined release legal.

Invariants:
  - Pixel memory is mapped only after a successful covering-fence wait.
  - Quarantine is fixed capacity and never grows during runtime.
  - Quarantined resources release only through ReleaseAfterTerminalDrain.
  - The owner borrows one restricted capture-frame capability and the device
    extent owner for the complete renderer lifetime; it cannot reach uploads,
    descriptors, pipelines, or unrelated frame policy.

Related:
  - SkullbonezSource/Rendering/DX12/Dx12BackbufferCapture.cpp
  - SkullbonezSource/Rendering/DX12/Dx12FrameOwner.h
  - SkullbonezSource/Runtime/Capture/CaptureSystem.h
*/
#pragma once

#include "../../Core/SbResult.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <d3d12.h>
#include <vector>

namespace SkullbonezCore
{
namespace Rendering
{
class Dx12CaptureFrame;
class Dx12RenderDevice;

class Dx12BackbufferCapture
{
  public:
    Dx12BackbufferCapture( SkullbonezCore::Core::SbDiagnosticStore& resultDiagnostics, Dx12CaptureFrame& frame,
                           const Dx12RenderDevice& device );
    SkullbonezCore::Core::SbResult CaptureBackbuffer( std::vector<uint8_t>& outPixels, int& outWidth, int& outHeight );
    bool SupportsBackbufferCapture() const
    {
        return true;
    }
    void ReleaseAfterTerminalDrain();

  private:
    static constexpr size_t MAX_QUARANTINED_READBACKS = 2;
    void Quarantine( ID3D12Resource* resource, const char* failedOperation );

    SkullbonezCore::Core::SbDiagnosticStore& m_resultDiagnostics;
    Dx12CaptureFrame& m_frame;
    const Dx12RenderDevice& m_device;
    std::array<ID3D12Resource*, MAX_QUARANTINED_READBACKS> m_quarantined = {};
    size_t m_quarantinedCount = 0;
};
} // namespace Rendering
} // namespace SkullbonezCore
