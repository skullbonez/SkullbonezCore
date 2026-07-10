/*
File: SkullbonezSource/Rendering/DX12/RenderBackendDX12.Readback.cpp
Purpose:
  Reads GPU-rendered image data back to the CPU for screenshots and validation.

Mental model:
  RenderBackendDX12.Readback.cpp reads GPU-rendered image data back to the CPU
  for screenshots and validation. As an implementation unit, keep edits
  anchored on DX12 ownership, descriptors, resources, and command submission
  and on the glossary/invariants below.

Glossary:
  Readback quarantine: Bounded ownership of a screenshot buffer whose GPU use
    cannot be disproved after Close or fence-wait failure.
  Descriptor: Small binding record that tells a renderer how to interpret a
  resource.
  Back buffer: Swap-chain image that will be presented to the window.
  Lane R result: Recoverable device/readback failure returned to screenshot
    automation instead of throwing through the render backend.

Invariants:
  - DX12 object lifetime, resource states, descriptor rows, and fence ordering
  must stay explicit.
  - A readback buffer is mapped or released only after a successful wait. An
    uncertain submission transfers it to a bounded quarantine that terminal
    shutdown releases only after proving a full queue drain.

Related:
  - Agentic/Reference/skullbonez-core-class-structure.md
  - Agentic/Reference/comment-style-guide.md
*/
#include "RenderBackendDX12.h"
#include "ShaderDX12.h"
#include "MeshDX12.h"
#include "FramebufferDX12.h"
#include "../RenderGraph.h"
#include "../../Core/Log.h"
#include "../../Core/PlatformProfiler.h"
#include <stdexcept>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <string>
#include <fstream>
#include <memory>
#include <vector>
#include <wrl/client.h>


using namespace SkullbonezCore::Math::Transformation;
using namespace SkullbonezCore::Rendering;
using Microsoft::WRL::ComPtr;
using SkullbonezCore::Basics::SbResult;


// --- Helpers ---
static void ReportDX12DescriptorHeapExhausted( const char* heapName, UINT nextIndex, UINT capacity )
{
    const char* name = heapName ? heapName : "unknown";
    fprintf( stderr, "FATAL: DX12 %s heap exhausted (next=%u capacity=%u)\n", name, nextIndex, capacity );
    fprintf( stdout, "FATAL: DX12 %s heap exhausted (next=%u capacity=%u)\n", name, nextIndex, capacity );
    fflush( stderr );
    fflush( stdout );
    Log().WriteEventf( "dx12_descriptor_heap_exhausted heap=%s next=%u capacity=%u", name, nextIndex, capacity );
    Log().FlushAll();
}

// --- RenderBackendDX12 Readback methods ---


SbResult RenderBackendDX12::CaptureBackbuffer( std::vector<uint8_t>& outPixels, int& outWidth, int& outHeight )
{
    outPixels.clear();
    const SbResult openResult = EnsureCommandListOpen();
    if ( !openResult.ok )
    {
        return openResult;
    }
    outWidth = m_width;
    outHeight = m_height;

    // F3 screenshots are taken in input handling before Clear()/Render, so the
    // backbuffer is usually still in PRESENT state. Scene-driven captures can
    // happen after render where the backbuffer is in RENDER_TARGET state.
    // Preserve whichever concrete graph-visible state we're currently in.
    const RenderGraphResourceAccess backBufferAccessBeforeCopy = m_backBufferAccess;

    // Transition backbuffer to COPY_SOURCE for readback.
    TransitionBackbuffer( "BackbufferReadbackBegin", RenderGraphResourceAccess::CopySource );
    if ( m_commandRecording.HasFailure() )
    {
        return m_commandRecording.CurrentResult();
    }

    D3D12_RESOURCE_DESC bbDesc = m_renderTargets[m_frameIndex]->GetDesc();
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint = {};
    UINT numRows = 0;
    UINT64 rowSizeBytes = 0;
    UINT64 totalBytes = 0;
    Device()->GetCopyableFootprints( &bbDesc, 0, 1, 0, &footprint, &numRows, &rowSizeBytes, &totalBytes );

    // Create a CPU-readable landing buffer for the screenshot. The back buffer
    // itself lives in GPU-only memory, so the CPU cannot read it directly. The
    // command list copies pixels into this readback buffer, WaitForGpu proves
    // that copy has finished, and only then do we Map() the bytes below.
    Dx12ReadbackBuffer readbackBuffer;
    // Buffers are always created in COMMON state in D3D12 regardless of the initial state
    // specified. Specifying any other state fires warning #1328 (CREATERESOURCE_STATE_IGNORED).
    // READBACK buffers are accessed via CPU Map/Unmap — no GPU state barrier is needed.
    // Docs:
    // https://learn.microsoft.com/en-us/windows/win32/direct3d12/using-resource-barriers-to-synchronize-resource-states-in-direct3d-12
    if ( !readbackBuffer.InitBuffer( Device(), totalBytes, L"Skullbonez DX12 Screenshot Readback Buffer" ) )
    {
        // Why: screenshot readback is a Lane R device/resource boundary. Restore
        // the backbuffer state before returning so the caller can report the
        // failure without leaving the command stream in COPY_SOURCE.
        TransitionBackbuffer( "BackbufferReadbackRestoreAfterFailure", backBufferAccessBeforeCopy );
        outWidth = 0;
        outHeight = 0;
        return SbResult::Failure( "Rendering/DX12",
                                  "CreateCommittedResource (screenshot readback) failed  "
                                  "(RenderBackendDX12::CaptureBackbuffer)" );
    }

    D3D12_TEXTURE_COPY_LOCATION dstLoc = {};
    dstLoc.pResource = readbackBuffer.Resource();
    dstLoc.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    dstLoc.PlacedFootprint = footprint;

    D3D12_TEXTURE_COPY_LOCATION srcLoc = {};
    srcLoc.pResource = m_renderTargets[m_frameIndex];
    srcLoc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;

    CommandList()->CopyTextureRegion( &dstLoc, 0, 0, 0, &srcLoc, nullptr );

    // Restore the exact state we found before the capture.
    TransitionBackbuffer( "BackbufferReadbackRestore", backBufferAccessBeforeCopy );
    if ( m_commandRecording.HasFailure() )
    {
        return m_commandRecording.CurrentResult();
    }

    // Execute and wait
    AssertPlatformProfilerGpuStackClosed( "CaptureBackbuffer" );
    const SbResult closeResult =
        m_commandRecording.CommitClose( CommandList()->Close(), "CaptureBackbuffer command list Close" );
    if ( !closeResult.ok )
    {
        if ( m_uncertainReadbackResourceCount >= m_uncertainReadbackResources.size() )
        {
            SB_FATAL( "RenderBackendDX12", "DX12 uncertain readback quarantine exhausted after Close failure." );
        }
        m_uncertainReadbackResources[m_uncertainReadbackResourceCount++] =
            readbackBuffer.DetachAfterUncertainSubmission();
        return closeResult;
    }
    SubmitClosedCommandList();
    const SbResult waitResult = m_commandRecording.CommitWait( WaitForGpu() );
    if ( !waitResult.ok )
    {
        if ( m_uncertainReadbackResourceCount >= m_uncertainReadbackResources.size() )
        {
            SB_FATAL( "RenderBackendDX12", "DX12 uncertain readback quarantine exhausted after wait failure." );
        }
        m_uncertainReadbackResources[m_uncertainReadbackResourceCount++] =
            readbackBuffer.DetachAfterUncertainSubmission();
        return waitResult;
    }

    // Map and read pixels
    const void* mappedData = readbackBuffer.MapRead( totalBytes );
    if ( !mappedData )
    {
        return SbResult::Failure( "Rendering/DX12", "Map readback buffer failed" );
    }

    // Convert RGBA top-down → BGR bottom-up (BMP format)
    int rowStride = ( m_width * 3 + 3 ) & ~3;
    std::vector<uint8_t> result( (size_t)rowStride * m_height );

    const uint8_t* src = static_cast<const uint8_t*>( mappedData );
    for ( int y = 0; y < m_height; ++y )
    {
        int flippedY = m_height - 1 - y;
        const uint8_t* srcRow = src + (size_t)y * footprint.Footprint.RowPitch;
        uint8_t* dstRow = result.data() + (size_t)flippedY * rowStride;
        for ( int x = 0; x < m_width; ++x )
        {
            dstRow[x * 3 + 0] = srcRow[x * 4 + 2]; // B
            dstRow[x * 3 + 1] = srcRow[x * 4 + 1]; // G
            dstRow[x * 3 + 2] = srcRow[x * 4 + 0]; // R
        }
    }

    readbackBuffer.UnmapNoWrite();

    outPixels = std::move( result );
    return SbResult::Success();
}
