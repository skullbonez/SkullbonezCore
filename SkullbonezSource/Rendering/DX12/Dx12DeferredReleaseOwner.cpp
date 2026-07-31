/*
File: SkullbonezSource/Rendering/DX12/Dx12DeferredReleaseOwner.cpp
Purpose:
  Implements fixed-capacity, fence-proven DX12 resource and descriptor retirement.

Summary:
  Invalidated COM resources and descriptor rows enter a bounded quarantine.
  A row is released only after its assigned covering fence completes, or after
  terminal device drain proves that no submitted work can reference it.

Invariants:
  - Queue exhaustion is fatal with owner and high-water diagnostics; it never grows at runtime.
  - The last release snapshot derives released rows from one input/survivor pair.
  - Unfenced retirement is legal only when submitted-work state proves no GPU reference remains.
  - Descriptor rows retire under the same fence proof as their associated resource.

Related:
  - SkullbonezSource/Rendering/DX12/Dx12FrameOwner.h
  - SkullbonezSource/Rendering/DX12/Dx12FrameOwner.cpp
  - Agentic/Reference/comment-style-guide.md
  - Agentic/Reference/engine-glossary.md
*/
#include "Dx12FrameOwner.h"
#include "../../Core/FatalError.h"

using namespace SkullbonezCore::Rendering;

void Dx12DeferredReleaseOwner::AssignFence( UINT64 fenceValue )
{

    if ( fenceValue == 0 )
    {
        return;
    }

    for ( size_t index = 0; index < m_pendingCount; ++index )
    {
        DeferredResourceReleaseDX12& retired = m_pending[index];

        if ( ( retired.resource || retired.staticDescriptorIndex != UINT_MAX ||
               retired.cpuDescriptorKind != Dx12CpuDescriptorKind::None ) &&
             !retired.fenceAssigned )
        {
            retired.fenceValue = fenceValue;
            retired.fenceAssigned = true;
        }
    }
}


void Dx12DeferredReleaseOwner::ReleaseCompleted( Dx12RenderDevice& device, Dx12DescriptorHeaps& descriptors,
                                                 Dx12SubmittedWorkState& submittedWork, bool releaseUnfenced )
{
    const bool fenceReady = device.FrameFence().IsReady();
    const UINT64 completedFence = fenceReady ? device.FrameFence().CompletedValue() : 0;
    const size_t releaseInputCount = m_pendingCount;

    if ( fenceReady )
    {
        submittedWork.ObserveCompletedFence( completedFence );
    }

    if ( m_pendingCount == 0 )
    {
        m_diagnostics.ObserveRelease( releaseInputCount, 0, fenceReady, completedFence );
        return;
    }

    const bool canReleaseUnfenced = releaseUnfenced && submittedWork.CanReleaseWithoutFence();
    size_t writeIndex = 0;

    for ( size_t readIndex = 0; readIndex < m_pendingCount; ++readIndex )
    {
        DeferredResourceReleaseDX12& retired = m_pending[readIndex];
        const bool empty = retired.resource == nullptr && retired.staticDescriptorIndex == UINT_MAX &&
                           retired.cpuDescriptorKind == Dx12CpuDescriptorKind::None;

        const bool canRelease = empty || canReleaseUnfenced ||
                                ( retired.fenceAssigned && fenceReady && retired.fenceValue <= completedFence );

        if ( canRelease )
        {

            if ( retired.resource )
            {
                retired.resource->Release();
                retired.resource = nullptr;
            }

            if ( retired.staticDescriptorIndex != UINT_MAX )
            {
                descriptors.FreeStatic( retired.staticDescriptorIndex );
                retired.staticDescriptorIndex = UINT_MAX;
            }

            if ( retired.cpuDescriptorKind != Dx12CpuDescriptorKind::None && retired.cpuDescriptorIndex != UINT_MAX )
            {
                descriptors.FreeCpu( retired.cpuDescriptorKind, retired.cpuDescriptorIndex );
                retired.cpuDescriptorKind = Dx12CpuDescriptorKind::None;
                retired.cpuDescriptorIndex = UINT_MAX;
            }

            continue;
        }

        if ( writeIndex != readIndex )
        {
            m_pending[writeIndex] = retired;
        }

        ++writeIndex;
    }

    for ( size_t index = writeIndex; index < m_pendingCount; ++index )
    {
        m_pending[index] = {};
    }

    m_diagnostics.ObserveRelease( releaseInputCount, writeIndex, fenceReady, completedFence );
    m_pendingCount = writeIndex;
}


bool Dx12DeferredReleaseOwner::Empty() const
{
    return m_pendingCount == 0;
}


size_t Dx12DeferredReleaseOwner::Count() const
{
    return m_pendingCount;
}
