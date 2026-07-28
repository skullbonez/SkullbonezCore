/*
File: SkullbonezSource/Rendering/DX12/Dx12BackbufferCapture.cpp
Purpose:
  Implements synchronous DX12 backbuffer capture and bounded uncertain release.

Summary:
  A capture preserves the tracked backbuffer state, copies through a temporary
  readback resource, submits and waits through a restricted frame capability,
  then converts RGBA rows to BMP-ready bottom-up BGR bytes.

Glossary:
  Footprint: DX12 row-pitch and placement description for a texture copy.
  Covering fence: Queue counter proving the copy no longer references readback
    storage.
  Uncertain result: Close or wait failure for which immediate COM release is
    forbidden by the capture policy.

Invariants:
  - The exact pre-capture backbuffer state is restored before submission.
  - Close/wait uncertainty transfers the COM reference into quarantine.
  - Submission refusal before ExecuteCommandLists leaves local release safe.
  - Output allocation is permitted only on this explicit cold capture path.

Related:
  - SkullbonezSource/Rendering/DX12/Dx12BackbufferCapture.h
  - SkullbonezSource/Rendering/DX12/Dx12FrameOwner.cpp
  - Agentic/Reference/comment-style-guide.md
*/
#include "Dx12BackbufferCapture.h"

#include "Dx12FrameOwner.h"
#include "RenderDeviceDX12.h"
#include "../../Core/FatalError.h"
#include "../../Core/SbDiagnosticStore.h"

#include <utility>

using namespace SkullbonezCore::Rendering;

Dx12BackbufferCapture::Dx12BackbufferCapture( SkullbonezCore::Core::SbDiagnosticStore& resultDiagnostics,
                                              Dx12CaptureFrame& frame, const Dx12RenderDevice& device )
    : m_resultDiagnostics( resultDiagnostics ), m_frame( frame ), m_device( device )
{
}


SkullbonezCore::Core::SbResult Dx12BackbufferCapture::CaptureBackbuffer( std::vector<uint8_t>& outPixels, int& outWidth,
                                                                         int& outHeight )
{
    Dx12CaptureFrame& frame = m_frame;
    const int width = m_device.Width();
    const int height = m_device.Height();
    outPixels.clear();
    const SkullbonezCore::Core::SbResult openResult = frame.EnsureOpen();

    if ( !openResult.Ok() )
    {
        return openResult;
    }

    outWidth = width;
    outHeight = height;

    ID3D12Device* device = frame.Device();
    ID3D12GraphicsCommandList* commandList = frame.CommandList();
    ID3D12Resource* backbuffer = frame.BackBuffer();

    if ( !device || !commandList || !backbuffer || width <= 0 || height <= 0 )
    {

        // Lane R: capture dimensions and device resources are external frame
        // readiness, so report the unavailable operation to automation.
        outWidth = 0;
        outHeight = 0;
        return m_resultDiagnostics.Failure( "Dx12BackbufferCapture",
                                            "Backbuffer capture is unavailable. device=%p list=%p "
                                            "backbuffer=%p extent=%dx%d",
                                            device, commandList, backbuffer, width, height );
    }

    // F3 captures normally begin in Present, while scene-suite captures may
    // begin after rendering. Preserve the exact tracked state for both paths.
    const RenderGraphResourceAccess accessBeforeCopy = frame.BackBufferAccess();
    frame.TransitionBackbuffer( "BackbufferReadbackBegin", RenderGraphResourceAccess::CopySource );

    if ( frame.HasFailure() )
    {
        return frame.CurrentResult();
    }

    const D3D12_RESOURCE_DESC backbufferDesc = backbuffer->GetDesc();
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint = {};

    UINT rowCount = 0;
    UINT64 rowSizeBytes = 0;
    UINT64 totalBytes = 0;
    device->GetCopyableFootprints( &backbufferDesc, 0, 1, 0, &footprint, &rowCount, &rowSizeBytes, &totalBytes );

    Dx12ReadbackBuffer readback;

    if ( !readback.InitBuffer( device, totalBytes, L"Skullbonez DX12 Screenshot Readback Buffer" ) )
    {

        // Why: restore the command stream before returning a recoverable
        // allocation failure; leaving CopySource active would poison Present.
        frame.TransitionBackbuffer( "BackbufferReadbackRestoreAfterFailure", accessBeforeCopy );
        outWidth = 0;
        outHeight = 0;
        return m_resultDiagnostics.Failure( "Dx12BackbufferCapture",
                                            "CreateCommittedResource (screenshot readback) failed" );
    }

    D3D12_TEXTURE_COPY_LOCATION destination = {};
    destination.pResource = readback.Resource();
    destination.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    destination.PlacedFootprint = footprint;

    D3D12_TEXTURE_COPY_LOCATION source = {};
    source.pResource = backbuffer;
    source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    commandList->CopyTextureRegion( &destination, 0, 0, 0, &source, nullptr );

    frame.TransitionBackbuffer( "BackbufferReadbackRestore", accessBeforeCopy );

    if ( frame.HasFailure() )
    {
        return frame.CurrentResult();
    }

    const Dx12CaptureSubmitOutcome submit = frame.SubmitAndWait();

    if ( !submit.result.Ok() )
    {

        if ( submit.readbackUseUncertain )
        {
            Quarantine( readback.DetachAfterUncertainSubmission(), submit.failedOperation );
        }

        return submit.result;
    }

    const uint8_t* mappedData = readback.MapRead( totalBytes );

    if ( !mappedData )
    {
        return m_resultDiagnostics.Failure( "Dx12BackbufferCapture", "Map readback buffer failed" );
    }

    // Cold utility allocation: screenshot bytes leave the renderer through the
    // concrete capture owner and are not steady-frame storage.
    const int rowStride = ( width * 3 + 3 ) & ~3;
    std::vector<uint8_t> result( static_cast<size_t>( rowStride ) * static_cast<size_t>( height ) );
    const uint8_t* sourcePixels = mappedData;

    for ( int y = 0; y < height; ++y )
    {
        const int flippedY = height - 1 - y;
        const uint8_t* sourceRow = sourcePixels + static_cast<size_t>( y ) * footprint.Footprint.RowPitch;
        uint8_t* destinationRow = result.data() + static_cast<size_t>( flippedY ) * rowStride;

        for ( int x = 0; x < width; ++x )
        {
            destinationRow[x * 3 + 0] = sourceRow[x * 4 + 2];
            destinationRow[x * 3 + 1] = sourceRow[x * 4 + 1];
            destinationRow[x * 3 + 2] = sourceRow[x * 4 + 0];
        }
    }

    readback.UnmapNoWrite();
    outPixels = std::move( result );
    return SkullbonezCore::Core::SbResult::Success();
}

void Dx12BackbufferCapture::ReleaseAfterTerminalDrain()
{

    // Lifetime: the caller's terminal drain is the proof that makes every
    // detached readback reference safe to release.

    for ( size_t index = 0; index < m_quarantinedCount; ++index )
    {

        if ( m_quarantined[index] )
        {
            m_quarantined[index]->Release();
            m_quarantined[index] = nullptr;
        }
    }

    m_quarantinedCount = 0;
}

void Dx12BackbufferCapture::Quarantine( ID3D12Resource* resource, const char* failedOperation )
{

    if ( !resource )
    {
        SB_FATAL( "Dx12BackbufferCapture", "Uncertain capture did not transfer a readback resource." );
    }

    if ( m_quarantinedCount >= m_quarantined.size() )
    {
        SB_FATAL( "Dx12BackbufferCapture",
                  "Uncertain readback quarantine exhausted. operation=%s capacity=%zu high_water=%zu",
                  failedOperation ? failedOperation : "unknown", m_quarantined.size(), m_quarantinedCount );
    }

    m_quarantined[m_quarantinedCount++] = resource;
}
