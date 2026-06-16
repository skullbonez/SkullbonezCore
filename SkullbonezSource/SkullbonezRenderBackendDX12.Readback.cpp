/*
File: SkullbonezSource/SkullbonezRenderBackendDX12.Readback.cpp
Purpose:
  Reads GPU-rendered image data back to the CPU for screenshots and validation.

Mental model:
  DX12 separates resource memory, descriptor rows, command recording, and GPU
  execution. Ownership, state transitions, descriptor lifetime, and fence
  ordering are the important ideas.

Glossary:
  DX12 (DirectX 12): Production renderer API used for explicit GPU resource,
  descriptor, and command-list control.
  GPU (Graphics Processing Unit): Processor that executes rendering, compute,
  and raytracing commands asynchronously from the CPU.
  CPU (Central Processing Unit): Host processor running engine code and
  recording GPU commands.
  Descriptor: Small binding record that tells a renderer how to interpret a
  resource.
  Back buffer: Swap-chain image that will be presented to the window.

Invariants:
  - DX12 object lifetime, resource states, descriptor rows, and fence ordering
  must stay explicit.

Related:
  - Agentic/Reference/skullbonez-core-class-structure.md
  - Agentic/Reference/comment-style-guide.md
*/
#include "SkullbonezRenderBackendDX12.h"
#include "SkullbonezShaderDX12.h"
#include "SkullbonezMeshDX12.h"
#include "SkullbonezFramebufferDX12.h"
#include "SkullbonezRenderGraph.h"
#include "SkullbonezPlatformProfiler.h"
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

static inline void ThrowIfFailed( HRESULT hr, const char* msg )
{
    if ( FAILED( hr ) )
    {
        throw std::runtime_error( msg );
    }
}

// --- RenderBackendDX12 Readback methods ---


std::vector<uint8_t> RenderBackendDX12::CaptureBackbuffer( int& outWidth, int& outHeight )
{
    EnsureCommandListOpen();
    outWidth = m_width;
    outHeight = m_height;

    // F3 screenshots are taken in input handling before Clear()/Render, so the backbuffer is
    // usually still in PRESENT state at this point. Scene-driven captures can happen after render
    // where the backbuffer is in RENDER_TARGET state. Preserve whichever state we're currently in.
    const RenderGraphResourceAccess backBufferAccessBeforeCopy =
        m_backBufferIsRT ? RenderGraphResourceAccess::RenderTarget : RenderGraphResourceAccess::Present;

    // Transition backbuffer to COPY_SOURCE for readback.
    ExecuteGraphTransitionBarrier( "BackbufferReadbackBegin", "SwapchainBackbuffer", m_renderTargets[m_frameIndex], backBufferAccessBeforeCopy, RenderGraphResourceAccess::CopySource );

    // Get copyable footprint
    D3D12_RESOURCE_DESC bbDesc = m_renderTargets[m_frameIndex]->GetDesc();
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint = {};
    UINT numRows = 0;
    UINT64 rowSizeBytes = 0;
    UINT64 totalBytes = 0;
    m_device->GetCopyableFootprints( &bbDesc, 0, 1, 0, &footprint, &numRows, &rowSizeBytes, &totalBytes );

    // Create a CPU-readable landing buffer for the screenshot. The back buffer
    // itself lives in GPU-only memory, so the CPU cannot read it directly. The
    // command list copies pixels into this readback buffer, WaitForGpu proves
    // that copy has finished, and only then do we Map() the bytes below.
    Dx12ReadbackBuffer readbackBuffer;
    // Buffers are always created in COMMON state in D3D12 regardless of the initial state
    // specified. Specifying any other state fires warning #1328 (CREATERESOURCE_STATE_IGNORED).
    // READBACK buffers are accessed via CPU Map/Unmap — no GPU state barrier is needed.
    // Docs: https://learn.microsoft.com/en-us/windows/win32/direct3d12/using-resource-barriers-to-synchronize-resource-states-in-direct3d-12
    if ( !readbackBuffer.InitBuffer( m_device, totalBytes, L"Skullbonez DX12 Screenshot Readback Buffer" ) )
    {
        throw std::runtime_error( "CreateCommittedResource (screenshot readback) failed" );
    }

    // Copy texture to readback buffer
    D3D12_TEXTURE_COPY_LOCATION dstLoc = {};
    dstLoc.pResource = readbackBuffer.Resource();
    dstLoc.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    dstLoc.PlacedFootprint = footprint;

    D3D12_TEXTURE_COPY_LOCATION srcLoc = {};
    srcLoc.pResource = m_renderTargets[m_frameIndex];
    srcLoc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;

    m_commandList->CopyTextureRegion( &dstLoc, 0, 0, 0, &srcLoc, nullptr );

    // Restore the exact state we found before the capture.
    ExecuteGraphTransitionBarrier( "BackbufferReadbackRestore", "SwapchainBackbuffer", m_renderTargets[m_frameIndex], RenderGraphResourceAccess::CopySource, backBufferAccessBeforeCopy );

    // Execute and wait
    AssertPlatformProfilerGpuStackClosed( "CaptureBackbuffer" );
    m_commandList->Close();
    m_commandListOpen = false;
    ID3D12CommandList* ppCLs[] = { m_commandList };
    m_commandQueue->ExecuteCommandLists( 1, ppCLs );
    WaitForGpu();

    // Map and read pixels
    const void* mappedData = readbackBuffer.MapRead( totalBytes );

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

    return result;
}
