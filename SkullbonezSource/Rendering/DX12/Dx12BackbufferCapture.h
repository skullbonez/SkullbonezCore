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
  - The owner never stores a backend or frame-owner pointer.

Related:
  - SkullbonezSource/Rendering/DX12/Dx12BackbufferCapture.cpp
  - SkullbonezSource/Rendering/DX12/Dx12FrameOwner.h
  - SkullbonezSource/Rendering/IRenderCaptureBackend.h
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

class Dx12BackbufferCapture
{
  public:
    SkullbonezCore::Core::SbResult Capture( Dx12CaptureFrame& frame,
                                            int width,
                                            int height,
                                            std::vector<uint8_t>& outPixels,
                                            int& outWidth,
                                            int& outHeight );
    void ReleaseAfterTerminalDrain();

  private:
    static constexpr size_t MAX_QUARANTINED_READBACKS = 2;
    void Quarantine( ID3D12Resource* resource, const char* failedOperation );

    std::array<ID3D12Resource*, MAX_QUARANTINED_READBACKS> m_quarantined = {};
    size_t m_quarantinedCount = 0;
};
} // namespace Rendering
} // namespace SkullbonezCore
