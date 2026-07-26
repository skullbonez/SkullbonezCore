/*
File: SkullbonezSource/Rendering/DX12/Dx12DeferredReleaseOwner.cpp
Purpose:
  Implements fixed-capacity, fence-proven DX12 resource and descriptor retirement.

Summary:
  Invalidated COM resources and descriptor rows enter a bounded quarantine.
  A row is released only after its assigned covering fence completes, or after
  terminal device drain proves that no submitted work can reference it.

Glossary:
  Covering fence: Queue counter proving all earlier GPU references are finished.
  Retirement quarantine: Fixed queue of invalidated resources awaiting that proof.

Invariants:
  - Queue exhaustion is fatal with owner and high-water diagnostics; it never grows at runtime.
  - Unfenced retirement is legal only when submitted-work state proves no GPU reference remains.
  - Descriptor rows retire under the same fence proof as their associated resource.

Related:
  - SkullbonezSource/Rendering/DX12/Dx12FrameOwner.h
  - SkullbonezSource/Rendering/DX12/Dx12FrameOwner.cpp
  - Agentic/Reference/comment-style-guide.md
*/
#include "Dx12FrameOwner.h"
#include "../../Core/FatalError.h"

using namespace SkullbonezCore::Rendering;

void Dx12DeferredReleaseOwner::Quarantine( ID3D12Resource* resource,
                                           UINT descriptorIndex,
                                           Dx12CpuDescriptorKind cpuKind,
                                           UINT cpuDescriptorIndex )
{
    if ( resource || descriptorIndex != UINT_MAX || cpuKind != Dx12CpuDescriptorKind::None )
    {
        if ( m_pendingCount >= MAX_PENDING_RETIREMENTS )
        {
            SB_FATAL( "Dx12DeferredReleaseOwner",
                      "Retirement capacity exhausted. owner=Rendering/DX12 capacity=%zu high_water=%zu",
                      MAX_PENDING_RETIREMENTS,
                      m_pendingCount );
        }

        DeferredResourceReleaseDX12 retired;
        retired.resource = resource;
        retired.staticDescriptorIndex = descriptorIndex;
        retired.cpuDescriptorKind = cpuKind;
        retired.cpuDescriptorIndex = cpuDescriptorIndex;
        m_pending[m_pendingCount++] = retired;
    }
}


void Dx12DeferredReleaseOwner::QuarantineStaticDescriptor( UINT descriptorIndex )
{
    if ( descriptorIndex != UINT_MAX )
    {
        Quarantine( nullptr, descriptorIndex );
    }
}


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


void Dx12DeferredReleaseOwner::ReleaseCompleted( Dx12RenderDevice& device,
                                                 Dx12DescriptorHeaps& descriptors,
                                                 Dx12SubmittedWorkState& submittedWork,
                                                 bool releaseUnfenced )
{
    const bool fenceReady = device.FrameFence().IsReady();
    const UINT64 completedFence = fenceReady ? device.FrameFence().CompletedValue() : 0;
    if ( fenceReady )
    {
        submittedWork.ObserveCompletedFence( completedFence );
    }

    if ( m_pendingCount == 0 )
    {
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
