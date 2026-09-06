// Purpose:
//   Exercises result values, logger concurrency, worker-task lifetime, and
//   fatal invariant probes, including disabled development-profiler compile contracts.

// Invariants:
//   - Fatal child cases return normally only when the case name is unknown.
//   - A diagnostic store may not finish destruction while a failed result lease
//     still names it.
//   - Diagnostic counter overflow and same-thread lock re-entry terminate in
//     isolated children instead of wrapping, aliasing, or spinning.
//   - Blocking task tests release the worker before local state is destroyed.
//   - Every threaded worker test shuts its pool down before local task state expires.
//   - Disabled development-profiler macros never evaluate caller expressions.
//   - Foreign page-boundary deletes reach allocation fatal invariant without faulting
//     while probing their inaccessible candidate header.
//   - Release foreign frees are proved in a child so their process-lifetime
//     counter cannot contaminate later parent-process diagnostics.
//   - Allocation-size overflow reaches allocation fatal invariant before CRT malloc.
//   - The contact-solve phase cursor admits only its full ordered walk and two
//     existing no-work terminal edges; every other edge terminates with fatal invariant.
//   - Physics storage seeding rejects missing allocation/owner scopes,
//     SceneLoad phase, missing Replay owner, and any Replay owner other than
//     the canonical prediction working set.
//   - Spatial-grid backing reserves only during SceneLoad; fixed-step
//     exhaustion reports the exact owner, capacity, high-water, and phase.
//   - Sleep support edges fail before either the scene-committed reservation or
//     the semantic ceiling can be exceeded.
//   - Physics scratch, shape, hot-row, sleep-export, and every contact
//     consequence lane fail in isolated children before an invalid access.
//   - Pipeline batch counting rejects full-record mode so retained row count
//     cannot diverge from the recorder's canonical event count.
//   - DX12 retirement accounting records a real below-capacity peak, resets at
//     device boundaries, and reports release/fence facts at exhaustion.
//   - The texture table admits its exact final slot and terminates before an
//     exhausted fixed table can produce an index.
//   - DX12 texture upload keeps one- and four-channel inputs direct, expands
//     luminance-alpha and RGB inputs to RGBA, and does not cross caller spans.
//   - DX12 geometry rejects attribute layouts before fixed backend arrays can overflow.
//   - Tornado visual frame borrows remain valid until release; missing and
//     release-cleared borrows terminate before capacity or draw dereference.
//   - Lock-order invalid-id, cycle, and held-stack tripwires are classified by
//     the same Debug policy used by acquisition without opening CRT dialogs.
//   - Rendering lifecycle, primitive-scope, world-resource, and preview-capacity
//     failures terminate in Profile children before stale access or indexing.
//   - Targeted assert-only controls exit cleanly outside Debug, proving each
//     rendering fatal invariant group would detect its former missed-failure implementation.
//   - Targeted IH2/IH3 controls retain the retired assert-only tornado,
//     collider, disjoint-set, and physics-body shapes under NDEBUG.
//   - Runtime lifecycle probes exercise the exact Run, Input, SkyPass, and UiTextPass
//     behavior owners through valid, absent, and teardown-closed transitions.
//   - The production frame-resource policy schedules Sky once for ordinary and
//     cinematic frames while keeping the four post-chain owners cinematic-only.
//   - Runtime debug visualizers bound frame staging and clear visual-only state
//     at scene and enable epoch boundaries.
//   - Targeted assert-only controls prove the retired Run/sky/profiler
//     checks would return cleanly outside Debug.
//   - Automation output aliases fail before either truncating owner can replace
//     immutable interaction-script input.
//   - Automation malformed-input admission, authored same-frame order, report
//     publication, and startup-failure precedence use production policy seams.
//   - SceneWorld swap-last deletion carries Gameplay's dense tornado timers with
//     the stable body that Physics moves into the removed row.
//   - Replay restore publishes the recorded eye, target, and up basis as one
//     camera pose instead of retaining live-slot roll.
//   - Orderly process exit returns an interaction-recorder save failure, and raw
//     mouse startup registration reports native rejection before renderer startup.

#include "../ThirdPtySource/doctest/doctest.h"

#include "../SkullbonezSource/Core/AmortizedTask.h"
#include "../SkullbonezSource/Core/AtomicTextFileWriter.h"
#include "../SkullbonezSource/Core/Allocation/RuntimeAllocationTracker.h"
#include "../SkullbonezSource/Core/Allocation/RuntimeReserveAllocator.h"
#if defined( SKULLBONEZ_DEVELOPMENT_TOOLS )
#include "../SkullbonezSource/Core/Allocation/DevelopmentToolAllocation.h"
#endif
#include "../SkullbonezSource/Core/Config.h"
#include "../SkullbonezSource/Core/FatalError.h"
#include "../SkullbonezSource/Core/Log.h"
#include "../SkullbonezSource/Core/SbResult.h"
#include "../SkullbonezSource/Core/WorkerPool.h"
#include "../SkullbonezSource/Assets/TextureCollection.h"
#include "../SkullbonezSource/Gameplay/TornadoVisualPass.h"
#include "../SkullbonezSource/Physics/SpatialGrid.h"
#include "../SkullbonezSource/Physics/SleepIslandSystem.h"
#include "../SkullbonezSource/Physics/ColliderStore.h"
#include "../SkullbonezSource/Physics/DisjointSet.h"
#include "../SkullbonezSource/Physics/PhysicsApi.h"
#include "../SkullbonezSource/Physics/PhysicsBodyStore.h"
#include "../SkullbonezSource/Physics/PhysicsEngine.h"
#include "../SkullbonezSource/Physics/PhysicsFixedList.h"
#include "../SkullbonezSource/Physics/Stages/PhysicsContactSolverStage.h"
#include "../SkullbonezSource/Physics/Stages/PhysicsStepDiagnostics.h"
#include "../SkullbonezSource/Rendering/DX12/Dx12FrameOwner.h"
#include "../SkullbonezSource/Rendering/DX12/RenderBackendDX12.h"
#include "../SkullbonezSource/Rendering/RenderInstanceRenderer.h"
#include "../SkullbonezSource/Rendering/PrimitiveBatchRenderer.h"
#include "../SkullbonezSource/Rendering/RenderMaterial.h"
#include "../SkullbonezSource/Rendering/RenderInstanceStore.h"
#include "../SkullbonezSource/Rendering/RenderPipeline.h"
#include "../SkullbonezSource/Rendering/Text.h"
#include "../SkullbonezSource/Core/TracyClientOwner.h"
#include "../SkullbonezSource/Runtime/App/Run.h"
#include "../SkullbonezSource/Runtime/App/InteractionAutomationApplication.h"
#include "../SkullbonezSource/Runtime/App/ReplayRestoreOperations.h"
#include "../SkullbonezSource/Runtime/App/StartupInputApplication.h"
#include "../SkullbonezSource/Runtime/Automation/InteractionAutomationController.h"
#include "../SkullbonezSource/Runtime/Diagnostics/RuntimeDiagnostics.h"
#include "../SkullbonezSource/Runtime/Diagnostics/DiagnosticsRuntime.h"
#include "../SkullbonezSource/Runtime/Diagnostics/SceneMemoryDiagnostics.h"
#include "../SkullbonezSource/Runtime/Diagnostics/UIStressPolicy.h"
#include "../SkullbonezSource/Runtime/Input/Input.h"
#include "../SkullbonezSource/Runtime/Render/RuntimeRenderer.h"
#include "../SkullbonezSource/Runtime/Render/RuntimeRenderPasses.h"
#include "../SkullbonezSource/Runtime/Render/BroadphaseVisualizer.h"
#include "../SkullbonezSource/Runtime/Render/CollisionVisualizer.h"
#include "../SkullbonezSource/Runtime/Render/PhysicsDebugVisualizer.h"
#include "../SkullbonezSource/Runtime/UI/OperatorUiProjection.h"
#include "../SkullbonezSource/Runtime/Interaction/OperatorCommandTransaction.h"
#include "../SkullbonezSource/Runtime/Replay/ReplayRestoreTransactions.h"
#include "../SkullbonezSource/Runtime/Scene/SceneWorld.h"
#include "../SkullbonezSource/World/Terrain.h"
#include "../SkullbonezSource/World/SkyBox.h"
#include "../SkullbonezSource/World/WorldEnvironment.h"
#include "TestFatalCases.h"
#include "TestSbResultAccess.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <type_traits>
#include <initializer_list>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <thread>
#include <vector>
#include "../SkullbonezSource/Core/SbDiagnosticStore.h"

namespace SkullbonezCore::Rendering
{
struct Dx12DeferredReleaseOwnerTestAccess
{
    static void ObservePendingCount( Dx12DeferredReleaseOwner& owner, size_t pendingCount )
    {
        owner.m_diagnostics.ObservePendingCount( pendingCount );
    }
};

struct Dx12TextureOwnerTestAccess
{
    static int UploadBytesPerPixel( int channels )
    {
        return Dx12TextureOwner::TextureUploadBytesPerPixel( channels );
    }
    static bool RequiresRgbaExpansion( int channels )
    {
        return Dx12TextureOwner::TextureUploadRequiresRgbaExpansion( channels );
    }
    static bool ExpandToRgba( std::span<const uint8_t> source, int channels, std::span<uint8_t> destination )
    {
        return Dx12TextureOwner::ExpandTextureUploadToRgba( source, channels, destination );
    }

    class EpochProbe
    {
      public:
        void Bind()
        {
            m_epoch.Bind( &m_deviceIdentity, &m_frameIdentity, &m_pipelineIdentity );
        }
        void Begin()
        {
            m_epoch.Begin();
        }
        void Close()
        {
            m_epoch.Close();
        }
        void Require( const char* operation ) const
        {
            m_epoch.Require( operation );
        }
        bool Active() const
        {
            return m_epoch.Active();
        }

      private:
        int m_deviceIdentity = 0;
        int m_frameIdentity = 0;
        int m_pipelineIdentity = 0;
        Dx12TextureOwner::ResourceEpoch m_epoch;
    };
};

struct Dx12GeometryOwnerTestAccess
{
    static uint32_t AddInstancedUploadProbe( Dx12GeometryOwner& owner, int staticVertexCount, int staticStride,
                                             int instanceFloats )
    {
        InstancedMeshDX12 mesh = {};
        mesh.staticVB = reinterpret_cast<ID3D12Resource*>( static_cast<uintptr_t>( 1u ) );
        mesh.staticStride = staticStride;
        mesh.staticVBV.SizeInBytes = static_cast<UINT>( staticVertexCount * staticStride );
        mesh.instanceFloats = instanceFloats;
        mesh.instanceStride = instanceFloats * static_cast<int>( sizeof( float ) );
        owner.m_instancedMeshes.push_back( mesh );
        return static_cast<uint32_t>( owner.m_instancedMeshes.size() );
    }

    static D3D12_GPU_VIRTUAL_ADDRESS InstanceDataAddress( const Dx12GeometryOwner& owner, uint32_t handle )
    {
        return handle > 0 && handle <= owner.m_instancedMeshes.size() ? owner.m_instancedMeshes[handle - 1].instanceDataAddr
                                                                      : 0;
    }

    static void ClearInstancedUploadProbes( Dx12GeometryOwner& owner )
    {
        for ( InstancedMeshDX12& mesh : owner.m_instancedMeshes )
        {
            mesh.staticVB = nullptr;
        }

        owner.m_instancedMeshes.clear();
    }

    static bool TryBuildInstancedAttributeLayout( std::span<const int> instanceAttributeSizes,
                                                  std::span<const int> staticAttributeSizes, std::size_t& outInstanceCount,
                                                  std::size_t& outStaticCount, std::size_t& outInputElementCount )
    {
        Dx12GeometryOwner::InstancedAttributeLayout layout;
        const bool accepted = Dx12GeometryOwner::TryBuildInstancedAttributeLayout( instanceAttributeSizes,
                                                                                   staticAttributeSizes, layout );
        outInstanceCount = layout.instanceCount;
        outStaticCount = layout.staticCount;
        outInputElementCount = layout.inputElementCount;
        return accepted;
    }

    class EpochProbe
    {
      public:
        void Bind()
        {
            m_epoch.Bind( &m_deviceIdentity, &m_frameIdentity, &m_pipelineIdentity, &m_diagnosticsIdentity );
        }
        void Begin()
        {
            m_epoch.Begin();
        }
        void Close()
        {
            m_epoch.Close();
        }
        void Require( const char* operation ) const
        {
            m_epoch.Require( operation );
        }
        bool Active() const
        {
            return m_epoch.Active();
        }

      private:
        int m_deviceIdentity = 0;
        int m_frameIdentity = 0;
        int m_pipelineIdentity = 0;
        int m_diagnosticsIdentity = 0;
        Dx12GeometryOwner::SubmissionEpoch m_epoch;
    };
};

struct PrimitiveBatchRendererTestAccess
{
    static bool MaterialTableCreationSucceeded( uint32_t textureHandle )
    {
        return PrimitiveBatchRenderer::MaterialTableCreationSucceeded( textureHandle );
    }

    static bool ResolveVisibleBatchReadiness( bool materialTableReady, bool& shaderPublicationCalled )
    {
        return PrimitiveBatchRenderer::ResolveVisibleBatchReadiness( [materialTableReady]() { return materialTableReady; },
                                                                     [&shaderPublicationCalled]()
                                                                     {
                                                                         shaderPublicationCalled = true;
                                                                         return true;
                                                                     } );
    }
};
} // namespace SkullbonezCore::Rendering

namespace SkullbonezCore::Hardware
{
struct InputWindowBridgeTestAccess
{
    struct RawMouseRegistrationProbe
    {
        RAWINPUTDEVICE observed = {};
        DWORD error = ERROR_SUCCESS;
        bool called = false;

        static DWORD Invoke( const RAWINPUTDEVICE& device, void* context ) noexcept
        {
            auto& probe = *static_cast<RawMouseRegistrationProbe*>( context );
            probe.observed = device;
            probe.called = true;
            return probe.error;
        }
    };

    static SkullbonezCore::Core::SbResult RegisterRawMouse( SkullbonezCore::Core::SbDiagnosticStore& diagnostics,
                                                            HWND window, bool callbackBridgeBound,
                                                            RawMouseRegistrationProbe& probe )
    {
        return Input::RegisterRawMouseInputWithOperation( diagnostics, window, callbackBridgeBound,
                                                          RawMouseRegistrationProbe::Invoke, &probe );
    }

    class Probe
    {
      public:
        void Bind( const void* identity )
        {
            m_bridge.Bind( reinterpret_cast<HWND>( const_cast<void*>( identity ) ) );
        }
        void Unbind( const void* identity )
        {
            m_bridge.Unbind( reinterpret_cast<HWND>( const_cast<void*>( identity ) ) );
        }
        bool IsBoundTo( const void* identity ) const
        {
            return m_bridge.BoundHandle() == reinterpret_cast<HWND>( const_cast<void*>( identity ) );
        }
        bool IsUnbound() const
        {
            return m_bridge.BoundHandle() == nullptr;
        }

      private:
        Input::NativeWindowBinding m_bridge;
    };
};
} // namespace SkullbonezCore::Hardware

namespace SkullbonezCore::Geometry
{
struct TerrainRenderLifecycleTestAccess
{
    static void RequireClipPlane( const float* clipPlane )
    {
        Terrain::RequireClipPlane( clipPlane );
    }
    static bool PublicationPreservesExistingOnFailure( bool meshReady, bool shaderReady )
    {
        SkullbonezCore::Core::EngineConfig config;
        std::unique_ptr<Terrain> existing = std::make_unique<Terrain>( 1.0f, 0.0f, 0.0f, config );
        std::unique_ptr<Terrain> candidate = std::make_unique<Terrain>( 2.0f, 0.0f, 0.0f, config );
        Terrain* const existingIdentity = existing.get();
        Terrain* const candidateIdentity = candidate.get();
        const Terrain::RequiredRenderResourceFailure failure = Terrain::TryPublishRenderReadyCandidate( existing, candidate,
                                                                                                        meshReady,
                                                                                                        shaderReady );

        return failure != Terrain::RequiredRenderResourceFailure::None && existing.get() == existingIdentity &&
               candidate.get() == candidateIdentity;
    }
    static bool PublicationMovesReadyCandidate()
    {
        SkullbonezCore::Core::EngineConfig config;
        std::unique_ptr<Terrain> existing = std::make_unique<Terrain>( 1.0f, 0.0f, 0.0f, config );
        std::unique_ptr<Terrain> candidate = std::make_unique<Terrain>( 2.0f, 0.0f, 0.0f, config );
        Terrain* const candidateIdentity = candidate.get();
        const Terrain::RequiredRenderResourceFailure failure = Terrain::TryPublishRenderReadyCandidate( existing, candidate,
                                                                                                        true, true );

        return failure == Terrain::RequiredRenderResourceFailure::None && existing.get() == candidateIdentity && !candidate;
    }
};

struct SkyBoxRenderLifecycleTestAccess
{
    static SkullbonezCore::Core::SbResult RequiredResourcesResult( SkullbonezCore::Core::SbDiagnosticStore& diagnostics,
                                                                   const std::array<bool, 6>& meshesReady, bool shaderReady )
    {
        return SkyBox::RequiredRenderResourcesResult( diagnostics, meshesReady, shaderReady );
    }
};
} // namespace SkullbonezCore::Geometry

namespace SkullbonezCore::Environment
{
struct WorldEnvironmentRenderLifecycleTestAccess
{
    static const float* CalmData( const WorldEnvironment& world )
    {
        return world.m_calmVertices.data();
    }
    static const float* OceanData( const WorldEnvironment& world )
    {
        return world.m_oceanVertices.data();
    }
    static std::size_t CalmCapacity( const WorldEnvironment& world )
    {
        return world.m_calmVertices.capacity();
    }
    static std::size_t OceanCapacity( const WorldEnvironment& world )
    {
        return world.m_oceanVertices.capacity();
    }
};
} // namespace SkullbonezCore::Environment

namespace SkullbonezCore::Textures
{
struct TextureCollectionTestAccess
{
    static int FirstFreeSlot( std::size_t residentCount )
    {
        std::array<TextureCollection::GpuTextureRecord, SkullbonezCore::Scene::Capacity::TOTAL_TEXTURE_COUNT> textures;

        for ( std::size_t index = 0; index < residentCount && index < textures.size(); ++index )
        {
            textures[index].backendHandle = static_cast<uint32_t>( index + 1u );
        }

        return TextureCollection::FindFreeSlotOrFatal( textures );
    }

    static bool ReplacementTransactionPreservesFailuresAndRetiresAfterPublication()
    {
        std::array<TextureCollection::GpuTextureRecord, SkullbonezCore::Scene::Capacity::TOTAL_TEXTURE_COUNT> textures {};
        TextureCollection::GpuTextureRecord& destination = textures[0];
        destination.legacyHash = 0xA11CEu;
        destination.backendHandle = 17u;
        destination.sourceId = 3u;
        destination.width = 64;
        destination.height = 32;
        destination.channels = 4;
        const TextureCollection::GpuTextureRecord original = destination;
        SkullbonezCore::Core::SbDiagnosticStore diagnostics;
        int eventOrder = 0;
        bool retired = false;

        SkullbonezCore::Core::SbResult decodeFailure = TextureCollection::CreateOrReplaceTextureRecord(
            textures, original.legacyHash,
            [&]( TextureCollection::GpuTextureRecord& )
            {
                eventOrder = 1;
                return diagnostics.Failure( "TextureCollectionTest", "planted decode failure" );
            },
            [&]( uint32_t ) { retired = true; } );

        if ( decodeFailure.Ok() || eventOrder != 1 || retired || destination.legacyHash != original.legacyHash ||
             destination.backendHandle != original.backendHandle || destination.sourceId != original.sourceId ||
             destination.width != original.width || destination.height != original.height ||
             destination.channels != original.channels )
        {
            return false;
        }

        eventOrder = 0;
        SkullbonezCore::Core::SbResult backendFailure = TextureCollection::CreateOrReplaceTextureRecord(
            textures, original.legacyHash,
            [&]( TextureCollection::GpuTextureRecord& )
            {
                eventOrder = 2;
                return diagnostics.Failure( "TextureCollectionTest", "planted backend failure" );
            },
            [&]( uint32_t ) { retired = true; } );

        if ( backendFailure.Ok() || eventOrder != 2 || retired || destination.legacyHash != original.legacyHash ||
             destination.backendHandle != original.backendHandle || destination.sourceId != original.sourceId ||
             destination.width != original.width || destination.height != original.height ||
             destination.channels != original.channels )
        {
            return false;
        }

        TextureCollection::GpuTextureRecord candidate;
        candidate.legacyHash = original.legacyHash;
        candidate.backendHandle = 29u;
        candidate.sourceId = 8u;
        candidate.width = 128;
        candidate.height = 64;
        candidate.channels = 3;
        eventOrder = 0;
        uint32_t retiredHandle = 0;

        const SkullbonezCore::Core::SbResult success = TextureCollection::CreateOrReplaceTextureRecord(
            textures, original.legacyHash,
            [&]( TextureCollection::GpuTextureRecord& loadedCandidate )
            {
                if ( destination.backendHandle != original.backendHandle )
                {
                    return diagnostics.Failure( "TextureCollectionTest", "resident row changed before candidate load" );
                }

                eventOrder = 3;
                loadedCandidate = candidate;
                return SkullbonezCore::Core::SbResult::Success();
            },
            [&]( uint32_t handle )
            {
                retiredHandle = handle;

                if ( destination.backendHandle == candidate.backendHandle )
                {
                    eventOrder = 4;
                }
            } );

        return success.Ok() && eventOrder == 4 && retiredHandle == original.backendHandle &&
               destination.legacyHash == candidate.legacyHash && destination.backendHandle == candidate.backendHandle &&
               destination.sourceId == candidate.sourceId && destination.width == candidate.width &&
               destination.height == candidate.height && destination.channels == candidate.channels;
    }
};
} // namespace SkullbonezCore::Textures

namespace SkullbonezCore::Gameplay
{
struct TornadoVisualPassTestAccess
{
    static void Prepare( TornadoVisualPass& pass, const TornadoFieldConfig& field, const TornadoSystemConfig& system )
    {
        pass.m_frame.field = &field;
        pass.m_frame.system = &system;
    }

    static void RequirePrepared( const TornadoVisualPass& pass, const char* operation )
    {
        pass.RequirePreparedFrame( operation );
    }

    static bool IsPrepared( const TornadoVisualPass& pass )
    {
        return pass.m_frame.field != nullptr && pass.m_frame.system != nullptr;
    }

    static float RotationPhase( double time, float rotationSpeed, int sourceIndex )
    {
        return TornadoVisualPass::ResolveRotationPhase( time, rotationSpeed, sourceIndex );
    }
};
} // namespace SkullbonezCore::Gameplay

namespace SkullbonezCore::Threading
{
struct LockOrderValidatorTestAccess
{
    static bool IsInvalidId( uint32_t lockId )
    {
        return LockOrderValidator::ClassifyAcquisitionProbe( lockId, false, false ) ==
               LockOrderValidator::AcquisitionProbeFinding::InvalidId;
    }

    static bool IsCycle()
    {
        return LockOrderValidator::ClassifyAcquisitionProbe( 1u, true, false ) ==
               LockOrderValidator::AcquisitionProbeFinding::Cycle;
    }

    static bool IsHeldStackExhausted()
    {
        return LockOrderValidator::ClassifyAcquisitionProbe( 1u, false, true ) ==
               LockOrderValidator::AcquisitionProbeFinding::HeldStackExhausted;
    }

    static bool IsValid()
    {
        return LockOrderValidator::ClassifyAcquisitionProbe( 1u, false, false ) ==
               LockOrderValidator::AcquisitionProbeFinding::None;
    }
};
} // namespace SkullbonezCore::Threading

namespace
{
SkullbonezCore::Core::SbDiagnosticStore diagnostics;
}

using SkullbonezCore::Core::EngineLog;
using SkullbonezCore::Core::SbResult;
using SkullbonezCore::Core::Allocation::RuntimeAllocationPhase;
using SkullbonezCore::Core::Allocation::RuntimeAllocationScope;
using SkullbonezCore::Math::CollisionDetection::SpatialGrid;
using SkullbonezCore::Math::Vector::Vector3;
using SkullbonezCore::Physics::AppendSleepSupportEdge;
using SkullbonezCore::Physics::MAX_SLEEP_SUPPORT_EDGES;
using SkullbonezCore::Physics::PhysicsEngine;
using SkullbonezCore::Runtime::AdmitInteractionAutomationPressKeyOptions;
using SkullbonezCore::Runtime::InteractionAutomationController;
using SkullbonezCore::Runtime::InteractionAutomationReportWriter;
using SkullbonezCore::Runtime::InteractionAutomationRunStatus;
using SkullbonezCore::Runtime::RunInteractionAutomationAction;
using SkullbonezCore::Runtime::RunPerfLogState;
using SkullbonezCore::Runtime::RuntimeDiagnostics;
using SkullbonezCore::Threading::AmortizedTask;
using SkullbonezCore::Threading::LockOrderValidator;
using SkullbonezCore::Threading::RunWorkerSystemSelfTest;
using SkullbonezCore::Threading::WorkerChunkRange;
using SkullbonezCore::Threading::WorkerPool;

namespace SkullbonezCore
{
namespace Physics
{
struct PersistentContactSolveTransactionTestAccess
{
    static void Advance( PersistentContactSolveTransaction& transaction, PersistentContactSolvePhaseCursor::Phase next )
    {
        transaction.AdvanceOrFatal( next, "ExhaustiveFatalProbe" );
    }
};

struct ColliderStoreTestAccess
{
    static void RequireShapeStorage( ColliderShapeKind shapeKind, std::size_t index, std::size_t shapeCount,
                                     std::size_t identityCount, const char* operation )
    {
        ColliderStore::RequireShapeStorage( shapeKind, index, shapeCount, identityCount, operation );
    }
};

struct PhysicsBodyStoreTestAccess
{
    static PhysicsBodyHotState ReadHotState( const PhysicsBodyStore& store, int modelIndex )
    {
        return store.HotStateForModelIndex( modelIndex );
    }
};

struct PhysicsContactSolverStageTestAccess
{
    static void ReserveAndPrepare( PhysicsContactSolverStage& stage, std::size_t collisionVisualCapacity,
                                   std::size_t fixedContactCapacity, std::size_t releaseWakeCapacity,
                                   std::size_t fixedTreeCapacity, std::size_t pipelineCapacity )
    {
        {
            Core::Allocation::RuntimeAllocationScope sceneLoadScope( Core::Allocation::RuntimeAllocationPhase::SceneLoad );
            stage.m_sideEffects.collisionVisualBodies.Reserve( collisionVisualCapacity );
            stage.m_sideEffects.fixedContactBodies.Reserve( fixedContactCapacity );
            stage.m_sideEffects.releaseWakeBodies.Reserve( releaseWakeCapacity );
            stage.m_sideEffects.fixedTreeReleases.Reserve( fixedTreeCapacity );
            stage.m_sideEffects.pipelineRecords.Reserve( pipelineCapacity );
        }

        stage.PrepareSideEffects( 2, 2u, 2 );
    }
};
} // namespace Physics

namespace Runtime
{
struct RunRendererLifecycleTestAccess
{
    class Probe
    {
      public:
        void Bind()
        {
            m_renderer = Identity();
        }
        void Close()
        {
            m_renderer = nullptr;
        }
        bool Available() const
        {
            return m_renderer != nullptr;
        }
        const void* Require( const char* operation )
        {
            return Run::RequireRenderer( m_renderer, operation );
        }

      private:
        RuntimeRenderer* Identity() const
        {
            return reinterpret_cast<RuntimeRenderer*>( const_cast<int*>( &m_rendererIdentity ) );
        }

        int m_rendererIdentity = 0;
        RuntimeRenderer* m_renderer = nullptr;
    };
};

struct SkyPassTestAccess
{
    class Probe
    {
      public:
        void Open( const void* identity )
        {
            m_lease.Open( reinterpret_cast<Geometry::SkyBox*>( const_cast<void*>( identity ) ) );
        }
        void Close()
        {
            m_lease.Close();
        }
        const void* Require( const char* operation ) const
        {
            return m_lease.Require( operation );
        }

      private:
        SkyPass::WorldViewLease m_lease;
    };

    static bool UsesCinematicAtmosphere( const SkullbonezCore::Core::CinematicRenderConfig* cinematic, SkyPassMode mode )
    {
        return SkyPass::UsesCinematicAtmosphere( cinematic, mode );
    }
};

struct UiTextPassTestAccess
{
    class ProfilerProbe
    {
      public:
        explicit ProfilerProbe( SkullbonezCore::Core::Profiler* profiler ) : m_lifecycle( profiler )
        {
        }
        void Activate()
        {
            m_lifecycle.Activate();
        }
        void Close()
        {
            m_lifecycle.Close();
        }
        SkullbonezCore::Core::Profiler& Require( const char* operation ) const
        {
            return m_lifecycle.Require( operation );
        }

      private:
        UiTextPass::ProfilerLifecycle m_lifecycle;
    };
};

struct OperatorCommandTransactionTestAccess
{
    static void Advance( OperatorCommandTransaction& transaction, OperatorCommandPhaseCursor::Phase next )
    {
        transaction.AdvanceOrFatal( next, "ExhaustiveFatalProbe" );
    }
};

struct RuntimeRendererWorldOverlayTransactionTestAccess
{
    static RuntimeRenderer::WorldOverlayTransaction Open()
    {
        return RuntimeRenderer::WorldOverlayTransaction( nullptr );
    }

    static void SubmitOverlays( RuntimeRenderer::WorldOverlayTransaction& transaction )
    {
        transaction.BeginOverlaySubmissionOrFatal();
    }
};

#ifdef _DEBUG
struct ReplayStartupProbeContinuationTestAccess
{
    static void SeedPendingPresentationActivation( ReplayStartupProbeContinuation& continuation )
    {
        continuation.m_phase = ReplayStartupProbeContinuation::Phase::AwaitingApplication;
        continuation.m_pendingAction = ReplayStartupProbeContinuation::PendingAction::ActivateLoadedPresentation;
    }

    static void RejectPendingApplication( ReplayStartupProbeContinuation& continuation )
    {
        continuation.RejectPendingApplicationOrFatal( "RejectPresentationActivationTest" );
    }
};
#endif
} // namespace Runtime
} // namespace SkullbonezCore

TEST_CASE( "Runtime renderer world-to-overlay transaction rejects abandonment and duplicate submission" )
{
    using Transaction = SkullbonezCore::Runtime::RuntimeRenderer::WorldOverlayTransaction;
    using OverlaySubmission = SkullbonezCore::Runtime::RuntimeRenderer::OverlayFrameSubmission;

    static_assert( !std::is_copy_constructible_v<Transaction> );
    static_assert( !std::is_copy_assignable_v<Transaction> );
    static_assert( !std::is_move_constructible_v<Transaction> );
    static_assert( !std::is_move_assignable_v<Transaction> );
    static_assert( std::is_same_v<decltype( &Transaction::SubmitOverlays ), bool ( Transaction::* )( const OverlaySubmission& )> );
    CHECK( std::is_destructible_v<Transaction> );
    ExpectRuntimeFatalCase( "renderer-frame-transaction-abandoned",
                            { "FATAL[RunRender]", "ended before overlay submission" } );
    ExpectRuntimeFatalCase( "renderer-frame-transaction-duplicate",
                            { "FATAL[RunRender]", "received overlay submission twice" } );
}


TEST_CASE( "Legacy render material modes classify extreme numeric inputs without integer conversion" )
{
    using SkullbonezCore::Rendering::RenderMaterialKind;
    using SkullbonezCore::Rendering::RenderMaterialKindFromLegacyMode;

    CHECK( RenderMaterialKindFromLegacyMode( ( std::numeric_limits<float>::max )() ) == RenderMaterialKind::Matte );
    CHECK( RenderMaterialKindFromLegacyMode( std::numeric_limits<float>::infinity() ) == RenderMaterialKind::Matte );
    CHECK( RenderMaterialKindFromLegacyMode( -std::numeric_limits<float>::infinity() ) == RenderMaterialKind::Textured );
    CHECK( RenderMaterialKindFromLegacyMode( std::numeric_limits<float>::quiet_NaN() ) == RenderMaterialKind::Textured );
    CHECK( RenderMaterialKindFromLegacyMode( 13.0f ) == RenderMaterialKind::Pine );
}

TEST_CASE( "Primitive batches require a published material table texture" )
{
    using SkullbonezCore::Rendering::PrimitiveBatchRendererTestAccess;

    CHECK_FALSE( PrimitiveBatchRendererTestAccess::MaterialTableCreationSucceeded( 0u ) );
    CHECK( PrimitiveBatchRendererTestAccess::MaterialTableCreationSucceeded( 1u ) );
    bool shaderPublicationCalled = false;
    CHECK_FALSE( PrimitiveBatchRendererTestAccess::ResolveVisibleBatchReadiness( false, shaderPublicationCalled ) );
    CHECK_FALSE( shaderPublicationCalled );
    CHECK( PrimitiveBatchRendererTestAccess::ResolveVisibleBatchReadiness( true, shaderPublicationCalled ) );
    CHECK( shaderPublicationCalled );
}

TEST_CASE( "Frame graph diagnostics retry unchanged frames until publication succeeds" )
{
    SkullbonezCore::Rendering::RenderPipelineDiagnosticWriteState state;
    SkullbonezCore::Rendering::RenderSceneSnapshot snapshot;
    constexpr uint64_t fingerprint = 0x1234u;
    const std::string dumpText = "frame graph diagnostic";

    CHECK_FALSE( state.MatchesFrame( snapshot, fingerprint ) );
    CHECK_FALSE( state.MatchesDumpText( dumpText ) );

    state.RecordPublicationResult( false, snapshot, fingerprint, dumpText );
    // A failed file write performs no commit. The identical next frame must
    // still reach the writer instead of being hidden by attempted state.
    CHECK_FALSE( state.MatchesFrame( snapshot, fingerprint ) );
    CHECK_FALSE( state.MatchesDumpText( dumpText ) );

    state.RecordPublicationResult( true, snapshot, fingerprint, dumpText );
    CHECK( state.MatchesFrame( snapshot, fingerprint ) );
    CHECK( state.MatchesDumpText( dumpText ) );
}

TEST_CASE( "SDF atlas contracts reject malformed metrics and incomplete publication" )
{
    using SkullbonezCore::Text::Text2d;

    const uint32_t originalTexture = Text2d::fontTexture;
    std::array<float, 96> originalAdvances;

    for ( std::size_t index = 0; index < originalAdvances.size(); ++index )
    {
        originalAdvances[index] = Text2d::charAdvance[index];
        Text2d::charAdvance[index] = 0.75f;
    }

    Text2d::fontTexture = 41u;
    std::array<float, 96> advances;
    advances.fill( 0.5f );
    CHECK( Text2d::SdfAdvanceMetricsValid( advances ) );

    CHECK_FALSE( Text2d::PublishSdfAtlasCandidate( 0u, advances ) );
    CHECK( Text2d::fontTexture == 41u );
    CHECK( Text2d::charAdvance[0] == 0.75f );

    advances[7] = 0.0f;
    CHECK_FALSE( Text2d::SdfAdvanceMetricsValid( advances ) );
    CHECK_FALSE( Text2d::PublishSdfAtlasCandidate( 77u, advances ) );
    CHECK( Text2d::fontTexture == 41u );
    CHECK( Text2d::charAdvance[7] == 0.75f );
    advances[7] = -0.25f;
    CHECK_FALSE( Text2d::SdfAdvanceMetricsValid( advances ) );
    advances[7] = std::numeric_limits<float>::quiet_NaN();
    CHECK_FALSE( Text2d::SdfAdvanceMetricsValid( advances ) );
    advances[7] = std::numeric_limits<float>::infinity();
    CHECK_FALSE( Text2d::SdfAdvanceMetricsValid( advances ) );
    advances[7] = 1.2501f;
    CHECK_FALSE( Text2d::SdfAdvanceMetricsValid( advances ) );
    CHECK_FALSE( Text2d::SdfAdvanceMetricsValid( std::span<const float>( advances.data(), advances.size() - 1u ) ) );

    CHECK( Text2d::SdfAtlasWriteSucceeded( 1u, 256u, 256u, 0, 0 ) );
    CHECK_FALSE( Text2d::SdfAtlasWriteSucceeded( 0u, 256u, 256u, 0, 0 ) );
    CHECK_FALSE( Text2d::SdfAtlasWriteSucceeded( 1u, 255u, 256u, 0, 0 ) );
    CHECK_FALSE( Text2d::SdfAtlasWriteSucceeded( 1u, 256u, 256u, EOF, 0 ) );
    CHECK_FALSE( Text2d::SdfAtlasWriteSucceeded( 1u, 256u, 256u, 0, EOF ) );

    using GdiResults = Text2d::SdfGdiOperationResults;
    CHECK( Text2d::SdfGdiOperationsSucceeded( GdiResults {} ) );
    const auto checkRejectedGdiFailure = []( bool GdiResults::* field )
    {
        GdiResults results;
        results.*field = false;
        CHECK_FALSE( Text2d::SdfGdiOperationsSucceeded( results ) );
    };
    checkRejectedGdiFailure( &GdiResults::bitmapSelected );
    checkRejectedGdiFailure( &GdiResults::brushCreated );
    checkRejectedGdiFailure( &GdiResults::backgroundFilled );
    checkRejectedGdiFailure( &GdiResults::brushDeleted );
    checkRejectedGdiFailure( &GdiResults::fontCreated );
    checkRejectedGdiFailure( &GdiResults::fontSelected );
    checkRejectedGdiFailure( &GdiResults::glyphWidthsMeasured );
    checkRejectedGdiFailure( &GdiResults::backgroundModeSet );
    checkRejectedGdiFailure( &GdiResults::textColorSet );
    checkRejectedGdiFailure( &GdiResults::glyphsDrawn );
    checkRejectedGdiFailure( &GdiResults::queueFlushed );
    checkRejectedGdiFailure( &GdiResults::fontRestored );
    checkRejectedGdiFailure( &GdiResults::bitmapRestored );
    checkRejectedGdiFailure( &GdiResults::fontDeleted );
    checkRejectedGdiFailure( &GdiResults::bitmapDeleted );
    checkRejectedGdiFailure( &GdiResults::dcDeleted );

    advances.fill( 0.625f );
    CHECK( Text2d::PublishSdfAtlasCandidate( 99u, advances ) );
    CHECK( Text2d::fontTexture == 99u );
    CHECK( Text2d::charAdvance[0] == 0.625f );
    CHECK( Text2d::charAdvance[95] == 0.625f );

    Text2d::fontTexture = originalTexture;

    for ( std::size_t index = 0; index < originalAdvances.size(); ++index )
    {
        Text2d::charAdvance[index] = originalAdvances[index];
    }
}

TEST_CASE( "Tracy disabled marker seams discard caller expressions" )
{
    int evaluatedArguments = 0;
    SKORE_TRACY_NAME_WORKER_THREAD( ++evaluatedArguments );
    SKORE_TRACY_MARK_SUBMITTED_FRAME();
    SKORE_TRACY_SCOPED_OWNER_ZONE( "Disabled", ++evaluatedArguments );
    const uint32_t sourceHandle = SKORE_TRACY_REGISTER_OWNER_ZONE( "Disabled", ++evaluatedArguments );
    const uint32_t zoneToken = SKORE_TRACY_BEGIN_OWNER_ZONE( ++evaluatedArguments );
    SKORE_TRACY_END_OWNER_ZONE( ++evaluatedArguments );
    SKORE_TRACY_PLOT_VALUE( "Disabled", ++evaluatedArguments );
    CHECK( evaluatedArguments == 0 );
    CHECK( sourceHandle == 0u );
    CHECK( zoneToken == 0u );
}


TEST_CASE( "Lock-order Debug probes classify invalid ids, cycles, and held-stack exhaustion" )
{
    using SkullbonezCore::Threading::LockOrderValidatorTestAccess;
    CHECK( LockOrderValidatorTestAccess::IsInvalidId( 0u ) );
    CHECK( LockOrderValidatorTestAccess::IsInvalidId( 257u ) );
    CHECK( LockOrderValidatorTestAccess::IsCycle() );
    CHECK( LockOrderValidatorTestAccess::IsHeldStackExhausted() );
    CHECK( LockOrderValidatorTestAccess::IsValid() );
}


TEST_CASE( "TextureCollection fixed capacity admits its exact final slot" )
{
    CHECK( SkullbonezCore::Textures::TextureCollectionTestAccess::FirstFreeSlot(
               SkullbonezCore::Scene::Capacity::TOTAL_TEXTURE_COUNT - 1u ) ==
           SkullbonezCore::Scene::Capacity::TOTAL_TEXTURE_COUNT - 1 );
}


TEST_CASE( "TextureCollection publishes a valid replacement without erasing the resident failure fallback" )
{
    CHECK( SkullbonezCore::Textures::TextureCollectionTestAccess::
               ReplacementTransactionPreservesFailuresAndRetiresAfterPublication() );
}


TEST_CASE( "DX12 shader requests distinguish default-root names from resolved asset paths" )
{
    CHECK( SkullbonezCore::Rendering::Dx12ResourceBuilder::DefaultShaderHlslPath( "shaders/unit" ) ==
           std::string( DATA_ROOT ) + "shaders/unit.hlsl" );
    CHECK( SkullbonezCore::Rendering::Dx12ResourceBuilder::ResolvedShaderHlslPath( "AlternateData/shaders/unit" ) ==
           "AlternateData/shaders/unit.hlsl" );
    CHECK( SkullbonezCore::Rendering::Dx12ResourceBuilder::ResolvedShaderHlslPath( "C:/ShaderRoot/absolute" ) ==
           "C:/ShaderRoot/absolute.hlsl" );
}


TEST_CASE( "World resource readiness rejects incomplete terrain and sky creation" )
{
    using SkullbonezCore::Geometry::SkyBoxRenderLifecycleTestAccess;
    using SkullbonezCore::Geometry::TerrainRenderLifecycleTestAccess;

    CHECK( TerrainRenderLifecycleTestAccess::PublicationPreservesExistingOnFailure( false, true ) );
    CHECK( TerrainRenderLifecycleTestAccess::PublicationPreservesExistingOnFailure( true, false ) );
    CHECK( TerrainRenderLifecycleTestAccess::PublicationMovesReadyCandidate() );

    SkullbonezCore::Core::SbDiagnosticStore resourceDiagnostics;
    std::array<bool, 6> skyMeshesReady = { true, true, true, true, true, true };
    CHECK( SkyBoxRenderLifecycleTestAccess::RequiredResourcesResult( resourceDiagnostics, skyMeshesReady, true ).Ok() );
    CHECK_FALSE(
        SkyBoxRenderLifecycleTestAccess::RequiredResourcesResult( resourceDiagnostics, skyMeshesReady, false ).Ok() );

    for ( std::size_t failedFace = 0; failedFace < skyMeshesReady.size(); ++failedFace )
    {
        skyMeshesReady.fill( true );
        skyMeshesReady[failedFace] = false;
        CHECK_FALSE(
            SkyBoxRenderLifecycleTestAccess::RequiredResourcesResult( resourceDiagnostics, skyMeshesReady, true ).Ok() );
    }
}


TEST_CASE( "DX12 texture upload preserves direct channels and expands two and three channel pixels within bounds" )
{
    using SkullbonezCore::Rendering::Dx12TextureOwnerTestAccess;

    CHECK( Dx12TextureOwnerTestAccess::UploadBytesPerPixel( 1 ) == 1 );
    CHECK_FALSE( Dx12TextureOwnerTestAccess::RequiresRgbaExpansion( 1 ) );
    CHECK( Dx12TextureOwnerTestAccess::UploadBytesPerPixel( 2 ) == 4 );
    CHECK( Dx12TextureOwnerTestAccess::RequiresRgbaExpansion( 2 ) );
    CHECK( Dx12TextureOwnerTestAccess::UploadBytesPerPixel( 3 ) == 4 );
    CHECK( Dx12TextureOwnerTestAccess::RequiresRgbaExpansion( 3 ) );
    CHECK( Dx12TextureOwnerTestAccess::UploadBytesPerPixel( 4 ) == 4 );
    CHECK_FALSE( Dx12TextureOwnerTestAccess::RequiresRgbaExpansion( 4 ) );

    constexpr uint8_t sourceGuard = 0xD1;
    constexpr uint8_t destinationGuard = 0xE2;
    std::array<uint8_t, 6> twoChannelSource = { sourceGuard, 10, 20, 30, 40, sourceGuard };
    std::array<uint8_t, 10> twoChannelDestination;
    twoChannelDestination.fill( destinationGuard );
    const std::array<uint8_t, 6> expectedTwoChannelSource = twoChannelSource;
    const std::array<uint8_t, 10> expectedTwoChannelDestination = { destinationGuard, 10, 10, 10, 20, 30, 30, 30, 40,
                                                                    destinationGuard };

    const bool
        twoChannelExpanded = Dx12TextureOwnerTestAccess::ExpandToRgba( std::span( twoChannelSource ).subspan( 1, 4 ), 2,
                                                                       std::span( twoChannelDestination ).subspan( 1, 8 ) );
    const bool twoChannelResultMatches = twoChannelExpanded && twoChannelDestination == expectedTwoChannelDestination;
    CHECK( twoChannelResultMatches );
    CHECK( twoChannelSource == expectedTwoChannelSource );

    std::array<uint8_t, 5> threeChannelSource = { sourceGuard, 50, 60, 70, sourceGuard };
    std::array<uint8_t, 6> threeChannelDestination;
    threeChannelDestination.fill( destinationGuard );
    const std::array<uint8_t, 5> expectedThreeChannelSource = threeChannelSource;
    const std::array<uint8_t, 6> expectedThreeChannelDestination = { destinationGuard, 50, 60, 70, 255, destinationGuard };

    CHECK( Dx12TextureOwnerTestAccess::ExpandToRgba( std::span( threeChannelSource ).subspan( 1, 3 ), 3,
                                                     std::span( threeChannelDestination ).subspan( 1, 4 ) ) );
    CHECK( threeChannelDestination == expectedThreeChannelDestination );
    CHECK( threeChannelSource == expectedThreeChannelSource );
}

TEST_CASE( "DX12 geometry admits exact attribute capacities and rejects oversized layouts" )
{
    using namespace SkullbonezCore::Rendering;

    std::array<int, MAX_DYNAMIC_VERTEX_ATTRIBUTES> exactDynamic;
    exactDynamic.fill( 1 );
    std::array<int, MAX_DYNAMIC_VERTEX_ATTRIBUTES + 1u> oversizedDynamic;
    oversizedDynamic.fill( 1 );
    Dx12GeometryOwner geometry;
    CHECK( geometry.CreateDynamicVB( exactDynamic.data(), static_cast<int>( exactDynamic.size() ), 1 ) != 0 );
    CHECK( geometry.DynamicCount() == 1u );
    CHECK( geometry.CreateDynamicVB( oversizedDynamic.data(), static_cast<int>( oversizedDynamic.size() ), 1 ) == 0 );
    CHECK( geometry.DynamicCount() == 1u );
    const std::array invalidDynamic = { 3, 0 };
    CHECK( geometry.CreateDynamicVB( invalidDynamic.data(), static_cast<int>( invalidDynamic.size() ), 1 ) == 0 );
    CHECK( geometry.DynamicCount() == 1u );

    std::array<int, MAX_INSTANCED_VERTEX_ATTRIBUTES_PER_STREAM> exactInstance;
    std::array<int, MAX_INSTANCED_VERTEX_ATTRIBUTES_PER_STREAM> exactStatic;
    exactInstance.fill( 4 );
    exactStatic.fill( 4 );
    std::size_t instanceCount = 0;
    std::size_t staticCount = 0;
    std::size_t inputElementCount = 0;
    CHECK( Dx12GeometryOwnerTestAccess::TryBuildInstancedAttributeLayout( exactInstance, exactStatic, instanceCount,
                                                                          staticCount, inputElementCount ) );
    CHECK( instanceCount == MAX_INSTANCED_VERTEX_ATTRIBUTES_PER_STREAM );
    CHECK( staticCount == MAX_INSTANCED_VERTEX_ATTRIBUTES_PER_STREAM );
    CHECK( inputElementCount == MAX_DX12_INPUT_ELEMENTS );

    const std::array<int, 0> legacyStatic = {};
    CHECK( Dx12GeometryOwnerTestAccess::TryBuildInstancedAttributeLayout( exactInstance, legacyStatic, instanceCount,
                                                                          staticCount, inputElementCount ) );
    CHECK( instanceCount == MAX_INSTANCED_VERTEX_ATTRIBUTES_PER_STREAM );
    CHECK( staticCount == 0u );
    CHECK( inputElementCount == MAX_INSTANCED_VERTEX_ATTRIBUTES_PER_STREAM + 1u );

    std::array<int, MAX_INSTANCED_VERTEX_ATTRIBUTES_PER_STREAM + 1u> oversizedInstance;
    oversizedInstance.fill( 4 );
    CHECK_FALSE( Dx12GeometryOwnerTestAccess::TryBuildInstancedAttributeLayout( oversizedInstance, exactStatic,
                                                                                instanceCount, staticCount,
                                                                                inputElementCount ) );
    CHECK( instanceCount == 0u );
    CHECK( staticCount == 0u );
    CHECK( inputElementCount == 0u );
    CHECK_FALSE( Dx12GeometryOwnerTestAccess::TryBuildInstancedAttributeLayout( exactInstance, oversizedInstance,
                                                                                instanceCount, staticCount,
                                                                                inputElementCount ) );
    const std::array invalidInstance = { 3, 0 };
    CHECK_FALSE( Dx12GeometryOwnerTestAccess::TryBuildInstancedAttributeLayout( invalidInstance, exactStatic, instanceCount,
                                                                                staticCount, inputElementCount ) );
}

TEST_CASE( "DX12 dynamic geometry reuses destroyed slots without reviving stale handles" )
{
    using namespace SkullbonezCore::Rendering;

    const int attributes[] = { 2, 2 };
    const std::array<float, 4> vertex = {};
    Dx12GeometryOwner geometry;
    uint32_t staleHandle = geometry.CreateDynamicVB( attributes, 2, 1 );
    REQUIRE( staleHandle != 0 );
    const uint32_t originalHandle = staleHandle;
    CHECK( geometry.DynamicCount() == 1u );
    CHECK( geometry.DynamicUploadBytes( staleHandle, vertex ) == vertex.size() * sizeof( float ) );

    for ( size_t reuse = 0; reuse < 300u; ++reuse )
    {
        geometry.DestroyDynamicVB( staleHandle );
        CHECK( geometry.DynamicCount() == 0u );
        CHECK( geometry.DynamicUploadBytes( staleHandle, vertex ) == 0u );
        CHECK( geometry.DynamicUploadBytes( originalHandle, vertex ) == 0u );

        const uint32_t replacementHandle = geometry.CreateDynamicVB( attributes, 2, 1 );
        REQUIRE( replacementHandle != 0 );
        CHECK( replacementHandle != staleHandle );
        CHECK( replacementHandle != originalHandle );
        CHECK( geometry.DynamicCount() == 1u );
        CHECK( geometry.DynamicUploadBytes( replacementHandle, vertex ) == vertex.size() * sizeof( float ) );
        staleHandle = replacementHandle;
    }

    uint32_t nextGeneration = 1u;
    CHECK( !Dx12DynamicGeometryHandleCodec::TryNextGeneration( Dx12DynamicGeometryHandleCodec::GENERATION_MAX,
                                                               nextGeneration ) );
    CHECK( nextGeneration == 0u );

    geometry.Shutdown();
    CHECK( geometry.DynamicCount() == 0u );
    CHECK( geometry.DynamicUploadBytes( originalHandle, vertex ) == 0u );
    CHECK( geometry.DynamicUploadBytes( staleHandle, vertex ) == 0u );

    const uint32_t restartedHandle = geometry.CreateDynamicVB( attributes, 2, 1 );
    REQUIRE( restartedHandle != 0 );
    CHECK( restartedHandle != originalHandle );
    CHECK( restartedHandle != staleHandle );
    CHECK( geometry.DynamicUploadBytes( restartedHandle, vertex ) == vertex.size() * sizeof( float ) );
}

TEST_CASE( "DX12 instanced geometry rejects draws beyond uploaded ranges" )
{
    using namespace SkullbonezCore::Rendering;

    CHECK( Dx12InstancedDrawFitsUploadedData( 96u, 24, 64u, 32, 4, 2 ) );
    CHECK_FALSE( Dx12InstancedDrawFitsUploadedData( 96u, 24, 64u, 32, 5, 2 ) );
    CHECK_FALSE( Dx12InstancedDrawFitsUploadedData( 96u, 24, 64u, 32, 4, 3 ) );
    CHECK_FALSE( Dx12InstancedDrawFitsUploadedData( 96u, 0, 64u, 32, 4, 2 ) );

    Dx12GeometryOwner geometry;
    const uint32_t handle = Dx12GeometryOwnerTestAccess::AddInstancedUploadProbe( geometry, 4, 24, 4 );
    std::array<float, 8> twoInstances = {};
    std::array<uint8_t, sizeof( twoInstances )> uploadBytes = {};
    CHECK( geometry.InstanceUploadBytes( handle, twoInstances ) == sizeof( twoInstances ) );
    geometry.UploadInstanceData( handle, twoInstances, 64u, uploadBytes.data() );
    CHECK( Dx12GeometryOwnerTestAccess::InstanceDataAddress( geometry, handle ) == 64u );

    const std::array<float, 5> partialInstance = {};
    CHECK( geometry.InstanceUploadBytes( handle, partialInstance ) == 0u );
    geometry.UploadInstanceData( handle, partialInstance, 128u, uploadBytes.data() );
    CHECK( Dx12GeometryOwnerTestAccess::InstanceDataAddress( geometry, handle ) == 0u );

    geometry.UploadInstanceData( handle, {}, 0u, nullptr );
    CHECK( Dx12GeometryOwnerTestAccess::InstanceDataAddress( geometry, handle ) == 0u );
    Dx12GeometryOwnerTestAccess::ClearInstancedUploadProbes( geometry );
}


TEST_CASE( "IH4 render lifecycle owners execute valid bind move and close transitions" )
{
    using SkullbonezCore::Rendering::Dx12GeometryOwnerTestAccess;
    using SkullbonezCore::Rendering::Dx12TextureOwnerTestAccess;

    Dx12GeometryOwnerTestAccess::EpochProbe geometryEpoch;
    geometryEpoch.Bind();
    geometryEpoch.Begin();
    CHECK( geometryEpoch.Active() );
    geometryEpoch.Require( "ValidFrameProbe" );
    geometryEpoch.Close();
    CHECK_FALSE( geometryEpoch.Active() );

    Dx12TextureOwnerTestAccess::EpochProbe textureEpoch;
    textureEpoch.Bind();
    textureEpoch.Begin();
    CHECK( textureEpoch.Active() );
    textureEpoch.Require( "ValidResourceProbe" );
    textureEpoch.Close();
    CHECK_FALSE( textureEpoch.Active() );

    int rendererIdentity = 0;
    SkullbonezCore::Rendering::PrimitiveBatchScopeLifecycle
        visibleScope( &rendererIdentity, SkullbonezCore::Rendering::PrimitiveBatchKind::Sphere );
    visibleScope.RequireVisible();
    SkullbonezCore::Rendering::PrimitiveBatchScopeLifecycle movedScope( std::move( visibleScope ) );
    CHECK_FALSE( visibleScope.Active() );
    CHECK( movedScope.Active() );
    movedScope.RequireVisible();

    SkullbonezCore::Rendering::PrimitiveBatchScopeLifecycle
        shadowScope( &rendererIdentity, SkullbonezCore::Rendering::PrimitiveBatchKind::ShadowSphere );
    shadowScope.RequireShadow();

    int resourcesIdentity = 0;
    int texturesIdentity = 0;
    int geometryIdentity = 0;
    SkullbonezCore::Rendering::PrimitiveResourceOwnerIdentity primitiveOwners;
    primitiveOwners.Bind( &resourcesIdentity, &texturesIdentity, &geometryIdentity );
    primitiveOwners.Bind( &resourcesIdentity, &texturesIdentity, &geometryIdentity );

    int configIdentity = 0;
    int assetsIdentity = 0;
    SkullbonezCore::Geometry::SkyBoxRenderRebuildLease skyBindings;
    skyBindings.BindTextures( &texturesIdentity );
    skyBindings.BindContexts( &configIdentity, &assetsIdentity, &resourcesIdentity );
    CHECK( skyBindings.Complete() );
    skyBindings.Require( "ValidRebuildProbe" );
    skyBindings.Release();
    CHECK_FALSE( skyBindings.Complete() );

    SkullbonezCore::Geometry::TerrainRenderRebuildLease terrainBindings;
    terrainBindings.Bind( &configIdentity, &assetsIdentity, &resourcesIdentity );
    CHECK( terrainBindings.Complete() );
    terrainBindings.PreserveAcrossResourceRelease();
    terrainBindings.Require( "ValidRebuildAfterReleaseProbe" );

    SkullbonezCore::Environment::WaterRenderRebuildLease waterBindings;
    waterBindings.Bind( &assetsIdentity, &resourcesIdentity );
    CHECK( waterBindings.Complete() );
    waterBindings.PreserveAcrossResourceRelease();
    waterBindings.Require( "ValidRebuildAfterReleaseProbe" );
}


TEST_CASE( "Water mesh staging retains its allocation across a Render-phase rebuild" )
{
    using SkullbonezCore::Core::Allocation::RuntimeAllocationPhase;
    using SkullbonezCore::Core::Allocation::RuntimeAllocationScope;
    using SkullbonezCore::Environment::WorldEnvironment;
    using SkullbonezCore::Environment::WorldEnvironmentRenderLifecycleTestAccess;

    SkullbonezCore::Core::EngineConfig config;
    WorldEnvironment world;
    world.BindRuntimeConfig( config );
    {
        RuntimeAllocationScope sceneLoadScope( RuntimeAllocationPhase::SceneLoad );
        world.SetTerrainBounds( 0.0f, 1000.0f, 0.0f, 1000.0f );
    }

    const float* const calmData = WorldEnvironmentRenderLifecycleTestAccess::CalmData( world );
    const float* const oceanData = WorldEnvironmentRenderLifecycleTestAccess::OceanData( world );
    const std::size_t calmCapacity = WorldEnvironmentRenderLifecycleTestAccess::CalmCapacity( world );
    const std::size_t oceanCapacity = WorldEnvironmentRenderLifecycleTestAccess::OceanCapacity( world );

    // Negative control: rebuilding the fixed topology while Render is guarded
    // must reuse the scene-prepared backing instead of reallocating it.
    {
        RuntimeAllocationScope renderScope( RuntimeAllocationPhase::Render );
        world.SetTerrainBounds( 0.0f, 1000.0f, 0.0f, 1000.0f );
    }

    CHECK( WorldEnvironmentRenderLifecycleTestAccess::CalmData( world ) == calmData );
    CHECK( WorldEnvironmentRenderLifecycleTestAccess::OceanData( world ) == oceanData );
    CHECK( WorldEnvironmentRenderLifecycleTestAccess::CalmCapacity( world ) == calmCapacity );
    CHECK( WorldEnvironmentRenderLifecycleTestAccess::OceanCapacity( world ) == oceanCapacity );
}


TEST_CASE( "IH5 runtime lifecycle owners preserve valid and unavailable policy" )
{
    using SkullbonezCore::Hardware::InputWindowBridgeTestAccess;
    using SkullbonezCore::Runtime::RunRendererLifecycleTestAccess;
    using SkullbonezCore::Runtime::SkyPassTestAccess;
    using SkullbonezCore::Runtime::UiTextPassTestAccess;

    int firstWindowIdentity = 0;
    InputWindowBridgeTestAccess::Probe inputBridge;
    CHECK( inputBridge.IsUnbound() );
    inputBridge.Bind( &firstWindowIdentity );
    CHECK( inputBridge.IsBoundTo( &firstWindowIdentity ) );

#if !defined( _DEBUG )
    int otherWindowIdentity = 0;
    inputBridge.Unbind( &otherWindowIdentity );
    CHECK( inputBridge.IsBoundTo( &firstWindowIdentity ) );
#endif

    RunRendererLifecycleTestAccess::Probe rendererLifecycle;
    CHECK_FALSE( rendererLifecycle.Available() );
    rendererLifecycle.Bind();
    CHECK( rendererLifecycle.Available() );
    CHECK( rendererLifecycle.Require( "ValidRendererProbe" ) != nullptr );
    rendererLifecycle.Close();
    CHECK_FALSE( rendererLifecycle.Available() );

    inputBridge.Unbind( &firstWindowIdentity );
    CHECK( inputBridge.IsUnbound() );

#if !defined( _DEBUG )
    inputBridge.Unbind( &firstWindowIdentity );
    CHECK( inputBridge.IsUnbound() );
#endif

    static SkullbonezCore::Core::Profiler profiler;
    UiTextPassTestAccess::ProfilerProbe profilerLifecycle( &profiler );
    profilerLifecycle.Activate();
    CHECK( &profilerLifecycle.Require( "ValidProfilerProbe" ) == &profiler );
    profilerLifecycle.Close();
    profilerLifecycle.Activate();
    CHECK( &profilerLifecycle.Require( "RebuiltProfilerProbe" ) == &profiler );

    SkullbonezCore::Core::MainMemoryStats sampledMemory;
    sampledMemory.sampleTimeSeconds = 3.0;
    sampledMemory.replay.totalBytes = 31u;
    const SkullbonezCore::Core::MainMemoryStats
        unavailableMemory = SkullbonezCore::Runtime::ProjectMemoryTabAvailability( false, sampledMemory );
    CHECK_FALSE( unavailableMemory.process.available );
    CHECK( unavailableMemory.replay.totalBytes == 0u );

    const SkullbonezCore::Core::MainMemoryStats
        availableMemory = SkullbonezCore::Runtime::ProjectMemoryTabAvailability( true, sampledMemory );
    CHECK( availableMemory.sampleTimeSeconds == doctest::Approx( 3.0 ) );
    CHECK( availableMemory.replay.totalBytes == 31u );

    int skyIdentity = 0;
    SkyPassTestAccess::Probe skyLease;
    skyLease.Open( &skyIdentity );
    CHECK( skyLease.Require( "ValidWorldViewProbe" ) == &skyIdentity );
    skyLease.Close();

    SkullbonezCore::Core::CinematicRenderConfig cinematic;
    cinematic.skyAtmosphereEnabled = true;
    CHECK(
        SkyPassTestAccess::UsesCinematicAtmosphere( &cinematic, SkullbonezCore::Runtime::SkyPassMode::CinematicIfEnabled ) );
    CHECK_FALSE(
        SkyPassTestAccess::UsesCinematicAtmosphere( &cinematic, SkullbonezCore::Runtime::SkyPassMode::CubemapOnly ) );
}


TEST_CASE( "IH7 frame resource schedule publishes ordinary sky without cinematic-only passes" )
{
    using SkullbonezCore::Runtime::RuntimeFrameResourcePass;
    using SkullbonezCore::Runtime::RuntimeFrameResourcePassRequired;

    constexpr std::array passes { RuntimeFrameResourcePass::Sky, RuntimeFrameResourcePass::FullscreenQuad,
                                  RuntimeFrameResourcePass::SceneTarget, RuntimeFrameResourcePass::Volumetric,
                                  RuntimeFrameResourcePass::Tonemap };

    int ordinaryRequired = 0;
    int ordinarySky = 0;
    int cinematicRequired = 0;
    int cinematicSky = 0;

    for ( const RuntimeFrameResourcePass pass : passes )
    {
        if ( RuntimeFrameResourcePassRequired( pass, false ) )
        {
            ++ordinaryRequired;
            ordinarySky += pass == RuntimeFrameResourcePass::Sky ? 1 : 0;
        }

        if ( RuntimeFrameResourcePassRequired( pass, true ) )
        {
            ++cinematicRequired;
            cinematicSky += pass == RuntimeFrameResourcePass::Sky ? 1 : 0;
        }
    }

    CHECK( ordinaryRequired == 1 );
    CHECK( ordinarySky == 1 );
    CHECK( cinematicRequired == static_cast<int>( passes.size() ) );
    CHECK( cinematicSky == 1 );
}

TEST_CASE( "Detached manifold strokes preserve contact geometry and body origins" )
{
    using namespace SkullbonezCore;
    Runtime::PhysicsDebugVisualizer visualizer;
    Rendering::ContactManifoldPresentation presentation;
    presentation.bodyCount = 2;
    presentation.bodies[0].valid = true;
    presentation.bodies[0].position = { 10.0f, 20.0f, 30.0f };
    presentation.bodies[1].valid = true;
    presentation.bodies[1].position = { -10.0f, -20.0f, -30.0f };
    presentation.pointCount = 1;
    presentation.points[0].point = { 3.0f, 4.0f, 5.0f };
    presentation.points[0].normal = { 0.0f, 1.0f, 0.0f };
    presentation.points[0].tangent1 = { 1.0f, 0.0f, 0.0f };
    presentation.points[0].tangent2 = { 0.0f, 0.0f, 1.0f };
    presentation.points[0].penetration = 0.5f;
    presentation.points[0].exactSourcePoint = true;
    const std::size_t lineCapacity = visualizer.DiagnosticLineFloatCapacity();
    const auto strokes = visualizer.BuildContactManifoldStrokes( presentation );
    constexpr std::size_t floatsPerSegment = 6u * 19u;
    // Six origin arrows, one pair link, and the existing eight contact segments.
    constexpr std::size_t segments = 6u * 3u + 1u + 8u;
    REQUIRE( strokes.size() == segments * floatsPerSegment * 2u );
    const auto core = strokes.subspan( segments * floatsPerSegment );
    CHECK( core[0] == 10.0f );
    CHECK( core[1] == 20.0f );
    CHECK( core[2] == 30.0f );
    CHECK( core[3] == 11.5f );
    const std::size_t normal = ( 19u + 3u ) * floatsPerSegment;
    CHECK( core[normal] == 3.0f );
    CHECK( core[normal + 1] == 4.0f );
    CHECK( core[normal + 2] == 5.0f );
    CHECK( core[normal + 3] == 3.0f );
    CHECK( core[normal + 4] == doctest::Approx( 6.9f ) );
    CHECK( core[normal + 5] == 5.0f );
    CHECK( core[25u * floatsPerSegment + 3] == 4.25f );
    CHECK( core[26u * floatsPerSegment + 5] == 6.25f );

    for ( std::size_t segment = 0; segment < segments; ++segment )
    {
        const std::size_t first = segment * floatsPerSegment;
        for ( std::size_t component = 0; component < 6; ++component )
        {
            CHECK( strokes[first + component] == core[first + component] );
        }
        CHECK( strokes[first + 6] > core[first + 6] );
        CHECK( core[first + 6] == doctest::Approx( 2.25f ) );
        CHECK( core[first + 12] == 0.0f ); // No HDR emphasis or glow.
    }
    CHECK( presentation.points[0].point.y == 4.0f );
    CHECK( presentation.points[0].penetration == 0.5f );
    CHECK( visualizer.DiagnosticTrackedContactCount() == 0u );
    CHECK( visualizer.DiagnosticLineFloatCapacity() == lineCapacity );

    for ( auto& point : presentation.points )
    {
        point = presentation.points[0];
    }
    presentation.pointCount = 255;
    CHECK( visualizer.BuildContactManifoldStrokes( presentation ).size() == ( 19u + 8u * 8u ) * floatsPerSegment * 2u );
    presentation.pointCount = 0;
    CHECK( visualizer.BuildContactManifoldStrokes( presentation ).empty() );
}

TEST_CASE( "Runtime debug visualizers bound staging and clear transient epochs" )
{
    using namespace SkullbonezCore;

    Runtime::BroadphaseVisualizer broadphase;
    CHECK( broadphase.DiagnosticLineFloatCapacity() ==
           Runtime::BroadphaseVisualizer::DiagnosticRequiredLineFloatCapacity() );
    broadphase.SetEnabled( true );
    const Physics::PhysicsBroadphaseActiveCell activeCell { 3, -2, 7, 1 };
    broadphase.Update( 0.0f, std::span<const Physics::PhysicsBroadphaseActiveCell>( &activeCell, 1 ), {} );
    CHECK( broadphase.DiagnosticTrackedCellCount() == 1 );
    broadphase.SetEnabled( false );
    CHECK( broadphase.DiagnosticTrackedCellCount() == 0 );
    broadphase.SetEnabled( true );
    broadphase.Update( 0.0f, std::span<const Physics::PhysicsBroadphaseActiveCell>( &activeCell, 1 ), {} );

    Physics::PhysicsBodyStore bodies;
    Physics::ColliderStore colliders;
    Rendering::RenderInstanceStore renderInstances;
    Runtime::CollisionVisualizer collision;
    const uint8_t collisionContact = 1u;
    const uint8_t awake = 0u;
    collision.Update( 0.0f, Runtime::CollisionVisualizerFrameView { colliders,
                                                                    renderInstances,
                                                                    std::span<const uint8_t>( &collisionContact, 1 ),
                                                                    std::span<const uint8_t>( &awake, 1 ),
                                                                    {},
                                                                    1 } );
    CHECK( collision.DiagnosticTrackedModelCount() == 1u );
    CHECK( collision.DiagnosticCollisionAmount( 0u ) == doctest::Approx( 1.0f ) );

    Runtime::PhysicsDebugVisualizer physicsDebug;
    CHECK( physicsDebug.DiagnosticLineFloatCapacity() ==
           Runtime::PhysicsDebugVisualizer::DiagnosticRequiredLineFloatCapacity() );
    physicsDebug.SetFlags( Physics::PHYSICS_DEBUG_CONTACTS );
    physicsDebug.SetContactLingerSeconds( 0.0f );
    std::vector<Physics::PhysicsDebugContact> contacts( Runtime::PhysicsDebugVisualizer::DiagnosticTrackedContactCapacity() +
                                                        1u );

    for ( std::size_t index = 0; index < contacts.size(); ++index )
    {
        contacts[index].featureId = static_cast<uint32_t>( index );
    }

    physicsDebug.Update( 0.0f, contacts );
    CHECK( physicsDebug.DiagnosticTrackedContactCount() ==
           Runtime::PhysicsDebugVisualizer::DiagnosticTrackedContactCapacity() );
    Runtime::RuntimeRenderer::ResetDebugVisualizerTransientState( collision, physicsDebug, broadphase );
    CHECK( broadphase.DiagnosticTrackedCellCount() == 0 );
    CHECK( collision.DiagnosticTrackedModelCount() == 0u );
    CHECK( physicsDebug.DiagnosticTrackedContactCount() == 0u );

    const uint8_t noCollisionContact = 0u;
    collision.Update( 0.0f, Runtime::CollisionVisualizerFrameView { colliders,
                                                                    renderInstances,
                                                                    std::span<const uint8_t>( &noCollisionContact, 1 ),
                                                                    std::span<const uint8_t>( &awake, 1 ),
                                                                    {},
                                                                    1 } );
    CHECK( collision.DiagnosticCollisionAmount( 0u ) == doctest::Approx( 0.0f ) );

    bool resourcesReady = false;
    int resourcePreparations = 0;
    CHECK( Runtime::PrepareCollisionVisualizerResourcePhase(
        [&]()
        {
            ++resourcePreparations;
            resourcesReady = true;
        },
        [&]() { return resourcesReady; } ) );
    CHECK( resourcePreparations == 1 );
    CHECK( Runtime::PrepareCollisionVisualizerResourcePhase( [&]() { ++resourcePreparations; },
                                                             [&]() { return resourcesReady; } ) );
    CHECK( resourcePreparations == 1 );
    CHECK( Runtime::ResolveCollisionVisualizerResourceAction( Runtime::CollisionVisualizerResourcePhase::Render,
                                                              resourcesReady ) ==
           Runtime::CollisionVisualizerResourceAction::Draw );
    CHECK( Runtime::ResolveCollisionVisualizerResourceAction( Runtime::CollisionVisualizerResourcePhase::Render, false ) ==
           Runtime::CollisionVisualizerResourceAction::Skip );
}


TEST_CASE( "Render target preview snapshot owns partial and exact-capacity append" )
{
    SkullbonezCore::Runtime::RuntimeRenderTargetPreviewSnapshot snapshot;
    CHECK( snapshot.count == 0 );

    for ( int index = 0; index < 10; ++index )
    {
        SkullbonezCore::Runtime::RuntimeRenderTargetPreview preview;
        preview.textureHandle = static_cast<uint32_t>( index + 1 );
        snapshot.AppendCatalogTarget( preview );
    }

    CHECK( snapshot.count == 10 );
    CHECK( snapshot.targets[9].textureHandle == 10u );

    SkullbonezCore::Runtime::RuntimeRenderTargetPreview dxrPreview;
    dxrPreview.label = "DXR Reflection";
    dxrPreview.textureHandle = 11u;
    snapshot.AppendOptionalDxrTarget( dxrPreview );
    CHECK( snapshot.count == 11 );
    CHECK( snapshot.targets[10].textureHandle == 11u );

    while ( snapshot.count < static_cast<int>( snapshot.targets.size() ) )
    {
        snapshot.AppendCatalogTarget( {} );
    }

    CHECK( snapshot.count == static_cast<int>( snapshot.targets.size() ) );
}


TEST_CASE( "Tornado visual frame remains prepared until explicit release" )
{
    SkullbonezCore::Gameplay::TornadoVisualPass pass;
    SkullbonezCore::Gameplay::TornadoFieldConfig field;
    SkullbonezCore::Gameplay::TornadoSystemConfig system;
    SkullbonezCore::Gameplay::TornadoVisualPassTestAccess::Prepare( pass, field, system );
    CHECK( SkullbonezCore::Gameplay::TornadoVisualPassTestAccess::IsPrepared( pass ) );
    SkullbonezCore::Gameplay::TornadoVisualPassTestAccess::RequirePrepared( pass, "ExactCapacityPositive" );
    pass.ReleaseResources();
    CHECK_FALSE( SkullbonezCore::Gameplay::TornadoVisualPassTestAccess::IsPrepared( pass ) );
}

TEST_CASE( "Tornado visual rotation preserves ordinary bytes and long-time progress" )
{
    constexpr float kRotationSpeed = 1.25f;
    constexpr int kSourceIndex = 2;
    constexpr float kCompatibleTime = 12.5f;
    const float
        compatiblePhase = SkullbonezCore::Gameplay::TornadoVisualPassTestAccess::RotationPhase( static_cast<double>(
                                                                                                    kCompatibleTime ),
                                                                                                kRotationSpeed,
                                                                                                kSourceIndex );
    CHECK( compatiblePhase == kCompatibleTime * kRotationSpeed + static_cast<float>( kSourceIndex ) * 1.73f );
    const float roundedCompatiblePhase = SkullbonezCore::Gameplay::TornadoVisualPassTestAccess::
        RotationPhase( static_cast<double>( kCompatibleTime ) + 1.0e-9, kRotationSpeed, kSourceIndex );
    CHECK( roundedCompatiblePhase == compatiblePhase );

    constexpr double kFloatFixedStepBoundary = 262144.0;
    constexpr float kFixedStep = 1.0f / 120.0f;
    const double advancedTime = kFloatFixedStepBoundary + static_cast<double>( kFixedStep );
    const float
        boundaryPhase = SkullbonezCore::Gameplay::TornadoVisualPassTestAccess::RotationPhase( kFloatFixedStepBoundary,
                                                                                              kRotationSpeed, kSourceIndex );
    const float advancedPhase = SkullbonezCore::Gameplay::TornadoVisualPassTestAccess::RotationPhase( advancedTime,
                                                                                                      kRotationSpeed,
                                                                                                      kSourceIndex );
    CHECK( advancedPhase != boundaryPhase );

    constexpr double kExactFloatRestoreTime = kFloatFixedStepBoundary * 2.0;
    const double exactFloatAdvancedTime = kExactFloatRestoreTime + static_cast<double>( kFixedStep );
    const double twiceAdvancedTime = exactFloatAdvancedTime + static_cast<double>( kFixedStep );
    const float exactFloatRestorePhase = SkullbonezCore::Gameplay::TornadoVisualPassTestAccess::
        RotationPhase( kExactFloatRestoreTime, kRotationSpeed, kSourceIndex );
    constexpr float kTwoPi = 6.28318530718f;
    const float expectedExactFloatRestorePhase = static_cast<float>(
        std::fmod( kExactFloatRestoreTime * static_cast<double>( kRotationSpeed ) +
                       static_cast<double>( kSourceIndex ) * 1.73,
                   static_cast<double>( kTwoPi ) ) );
    CHECK( exactFloatRestorePhase == expectedExactFloatRestorePhase );
    const float exactFloatAdvancedPhase = SkullbonezCore::Gameplay::TornadoVisualPassTestAccess::
        RotationPhase( exactFloatAdvancedTime, kRotationSpeed, kSourceIndex );
    const float twiceAdvancedPhase = SkullbonezCore::Gameplay::TornadoVisualPassTestAccess::RotationPhase( twiceAdvancedTime,
                                                                                                           kRotationSpeed,
                                                                                                           kSourceIndex );
    CHECK( exactFloatAdvancedPhase != exactFloatRestorePhase );
    CHECK( twiceAdvancedPhase != exactFloatAdvancedPhase );
}

#if defined( SKULLBONEZ_DEVELOPMENT_TOOLS )
TEST_CASE( "Tracy allocation events stay inactive outside heavy capture" )
{
    using namespace SkullbonezCore::Core::Allocation;
    int value = 0;
    SetTracyAllocationTracingEnabled( false );
    const uint64_t connectionId = RecordTracyAllocation( &value, sizeof( value ) );
    CHECK( connectionId == 0u );
    RecordTracyFree( &value, connectionId );
}
#endif

namespace
{
std::string ReadHandleText( HANDLE file )
{
    std::string text;

    if ( file == INVALID_HANDLE_VALUE || SetFilePointer( file, 0, nullptr, FILE_BEGIN ) == INVALID_SET_FILE_POINTER )
    {
        return text;
    }

    char buffer[4096] = {};
    DWORD bytesRead = 0;

    while ( ReadFile( file, buffer, sizeof( buffer ), &bytesRead, nullptr ) && bytesRead > 0 )
    {
        text.append( buffer, buffer + bytesRead );
    }

    return text;
}

std::string ReadSharedFileText( const char* path )
{
    HANDLE file = CreateFileA( path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                               OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr );

    if ( file == INVALID_HANDLE_VALUE )
    {
        return {};
    }

    std::string text = ReadHandleText( file );
    CloseHandle( file );
    return text;
}

struct FatalChildResult
{
    bool launched = false;
    bool timedOut = false;
    DWORD exitCode = 0;
    std::string output;
};

struct ForeignAllocationHeaderLayout
{
    void* raw = nullptr;
    uint64_t size = 0u;
    uint32_t phase = 0u;
    uint32_t flags = 0u;
    uint16_t owner = 0u;
    uint16_t reserved = 0u;
    uint32_t magic = 0u;
    uint64_t trackerAccountingGeneration = 0u;
    uint64_t ownerAccountingGeneration = 0u;
    uint64_t ownershipCookie = 0u;
    uint64_t tracyConnectionId = 0u;
};

static_assert( sizeof( void* ) == 8u, "Runtime allocation foreign-header probes require the supported x64 ABI." );
static_assert( offsetof( ForeignAllocationHeaderLayout, magic ) == 28u );
static_assert( offsetof( ForeignAllocationHeaderLayout, trackerAccountingGeneration ) == 32u );
static_assert( offsetof( ForeignAllocationHeaderLayout, ownerAccountingGeneration ) == 40u );
static_assert( offsetof( ForeignAllocationHeaderLayout, ownershipCookie ) == 48u );
static_assert( sizeof( ForeignAllocationHeaderLayout ) == 64u );

constexpr uint32_t FOREIGN_ALLOCATION_HEADER_MAGIC = 0xA110CA7Eu;

// Compatibility: these child-case names are command-line values used by older
// test launchers. Keep their exact bytes while using direct names in the code.
constexpr const char* LEGACY_IH4_DX12_MISSED_FAILURE_CASE = "ih4-legacy-dx12-false-"
                                                            "pass";
constexpr const char* LEGACY_IH23_TORNADO_MISSED_FAILURE_CASE = "ih23-legacy-tornado-false-"
                                                                "pass";
constexpr const char* LEGACY_IH23_COLLIDER_MISSED_FAILURE_CASE = "ih23-legacy-collider-false-"
                                                                 "pass";
constexpr const char* LEGACY_IH23_DISJOINT_SET_MISSED_FAILURE_CASE = "ih23-legacy-disjoint-set-false-"
                                                                     "pass";
constexpr const char* LEGACY_IH23_PHYSICS_BODY_MISSED_FAILURE_CASE = "ih23-legacy-physics-body-false-"
                                                                     "pass";
constexpr const char* LEGACY_IH4_PRIMITIVE_MISSED_FAILURE_CASE = "ih4-legacy-primitive-false-"
                                                                 "pass";
constexpr const char* LEGACY_IH4_WORLD_MISSED_FAILURE_CASE = "ih4-legacy-world-false-"
                                                             "pass";
constexpr const char* LEGACY_IH4_PREVIEW_MISSED_FAILURE_CASE = "ih4-legacy-preview-false-"
                                                               "pass";
constexpr const char* LEGACY_IH5_SKY_MISSED_FAILURE_CASE = "ih5-legacy-sky-pass-false-"
                                                           "pass";
constexpr const char* LEGACY_IH5_RUN_RENDERER_MISSED_FAILURE_CASE = "ih5-legacy-run-renderer-false-"
                                                                    "pass";
constexpr const char* LEGACY_IH5_UI_PROFILER_MISSED_FAILURE_CASE = "ih5-legacy-ui-profiler-false-"
                                                                   "pass";

FatalChildResult RunFatalChild( const char* caseName )
{
    FatalChildResult result;
    const char* executable = RuntimeTestExecutablePath();

    if ( !executable )
    {
        return result;
    }

    char temporaryDirectory[MAX_PATH] = {};
    char outputPath[MAX_PATH] = {};

    if ( GetTempPathA( MAX_PATH, temporaryDirectory ) == 0 ||
         GetTempFileNameA( temporaryDirectory, "sbf", 0, outputPath ) == 0 )
    {
        return result;
    }

    SECURITY_ATTRIBUTES security = { sizeof( security ), nullptr, TRUE };
    HANDLE output = CreateFileA( outputPath, GENERIC_READ | GENERIC_WRITE,
                                 FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, &security, CREATE_ALWAYS,
                                 FILE_ATTRIBUTE_TEMPORARY, nullptr );

    if ( output == INVALID_HANDLE_VALUE )
    {
        DeleteFileA( outputPath );
        return result;
    }

    char commandLine[4096] = {};
    snprintf( commandLine, sizeof( commandLine ), "\"%s\" --fatal-case \"%s\"", executable, caseName );
    STARTUPINFOA startup = {};
    startup.cb = sizeof( startup );
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = GetStdHandle( STD_INPUT_HANDLE );
    startup.hStdOutput = output;
    startup.hStdError = output;
    PROCESS_INFORMATION process = {};
    result.launched = CreateProcessA( nullptr, commandLine, nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr,
                                      &startup, &process ) != FALSE;

    if ( result.launched )
    {
        constexpr DWORD FATAL_CHILD_TIMEOUT_MS = 10000;
        const DWORD waitResult = WaitForSingleObject( process.hProcess, FATAL_CHILD_TIMEOUT_MS );

        if ( waitResult == WAIT_TIMEOUT )
        {
            // Hazard: terminate only the exact child process handle created for
            // this probe. A regressed fatal contract must not hang validation.
            result.timedOut = true;
            TerminateProcess( process.hProcess, 0xDEADu );
            WaitForSingleObject( process.hProcess, 5000 );
        }

        GetExitCodeProcess( process.hProcess, &result.exitCode );
        CloseHandle( process.hThread );
        CloseHandle( process.hProcess );
    }

    FlushFileBuffers( output );
    result.output = ReadHandleText( output );
    CloseHandle( output );
    DeleteFileA( outputPath );
    return result;
}

void ExpectFatalCase( const char* caseName, std::initializer_list<const char*> expectedDiagnostics )
{
#if defined( __SANITIZE_ADDRESS__ )
    // ASan reports the deliberate abort as a sanitizer signal. The healthy
    // ASan lane targets the concurrent logger test; normal CPU gates own fatal
    // child proof.
    static_cast<void>( caseName );
    static_cast<void>( expectedDiagnostics );
#else
    const FatalChildResult child = RunFatalChild( caseName );
    INFO( "fatal child output: " << child.output );
    REQUIRE( child.launched );
    REQUIRE_FALSE( child.timedOut );
    CHECK( child.exitCode != 0 );

    for ( const char* expected : expectedDiagnostics )
    {
        CHECK( child.output.find( expected ) != std::string::npos );
    }

    const bool checksStack = std::any_of( expectedDiagnostics.begin(), expectedDiagnostics.end(),
                                          []( const char* expected )
                                          {
                                              return expected &&
                                                     std::strcmp( expected, "FATAL[Tests/WorkerFatalProbe]" ) == 0;
                                          } );

    if ( checksStack )
    {
        CHECK( child.output.find( "STACK[0]=" ) != std::string::npos );
    }
#endif
}

void ExpectCleanControlCase( const char* caseName, std::initializer_list<const char*> expectedDiagnostics )
{
#if defined( __SANITIZE_ADDRESS__ )
    static_cast<void>( caseName );
    static_cast<void>( expectedDiagnostics );
#else
    const FatalChildResult child = RunFatalChild( caseName );
    INFO( "clean control child output: " << child.output );
    REQUIRE( child.launched );
    REQUIRE_FALSE( child.timedOut );
    CHECK( child.exitCode == 0 );

    for ( const char* expected : expectedDiagnostics )
    {
        CHECK( child.output.find( expected ) != std::string::npos );
    }
#endif
}

#if !defined( _DEBUG ) && !defined( SKULLBONEZ_PROFILE_ENABLED ) && !defined( SKULLBONEZ_TEST_PROFILE_ALLOCATION_FATAL )
void ExpectCleanChildCase( const char* caseName, std::initializer_list<const char*> expectedDiagnostics )
{
    const FatalChildResult child = RunFatalChild( caseName );
    INFO( "clean child output: " << child.output );
    REQUIRE( child.launched );
    REQUIRE_FALSE( child.timedOut );
    CHECK( child.exitCode == 0 );

    for ( const char* expected : expectedDiagnostics )
    {
        CHECK( child.output.find( expected ) != std::string::npos );
    }
}
#endif

struct WorkerFatalProbe
{
    void ExecuteWorkerTask()
    {
        SB_FATAL( "Tests/WorkerFatalProbe", "worker-thread fatal logging probe" );
    }
};

void RunLegacyAssertOnlyControl( bool condition, const char* group )
{
    // Negative control: this is the retired Profile shape. The assertion is
    // compiled out under NDEBUG and the safe return proves the paired fatal invariant
    // child would have passed incorrectly without its always-on owner guard.
    assert( condition );

    if ( !condition )
    {
        std::printf( "IH4 legacy assert-only %s path returned in a non-Debug child\n", group );
    }
}


void RunIh23LegacyAssertOnlyControl( bool condition, const char* group )
{
    // Negative control: the selected IH2/IH3 paths formerly asserted the
    // owner precondition and then continued toward a dereference, index, or
    // bounded write. NDEBUG removes that barrier and reaches this clean return.
    assert( condition );

    if ( !condition )
    {
        std::printf( "IH2/IH3 legacy assert-only %s path returned in a non-Debug child\n", group );
    }
}


void RunIh5LegacyAssertOnlyControl( bool condition, const char* group )
{
    // Negative control: the selected SkyPass/UiTextPass paths previously used
    // this exact assert-only shape before continuing toward a dereference.
    assert( condition );

    if ( !condition )
    {
        std::printf( "IH5 legacy assert-only %s path returned in a non-Debug child\n", group );
    }
}
} // namespace

void ExpectRuntimeFatalCase( const char* caseName, std::initializer_list<const char*> expectedDiagnostics )
{
    ExpectFatalCase( caseName, expectedDiagnostics );
}

bool RunRuntimeFatalStartupAndPrimitiveCase( const char* caseName )
{
#ifdef _DEBUG

    if ( std::strcmp( caseName, "replay-startup-illegal-transition" ) == 0 )
    {
        using Continuation = SkullbonezCore::Runtime::ReplayStartupProbeContinuation;
        Continuation::RequireLegalTransitionOrFatal( Continuation::Phase::Idle, Continuation::Phase::Complete,
                                                     "FatalContractProbe" );
        return true;
    }

    if ( std::strcmp( caseName, "replay-startup-restore-action-without-transaction" ) == 0 )
    {
        using Continuation = SkullbonezCore::Runtime::ReplayStartupProbeContinuation;
        Continuation::RequireApplicationStateOrFatal( Continuation::Phase::AwaitingApplication,
                                                      Continuation::PendingAction::ApplyRestoredBranchTimeline, false,
                                                      "FatalContractProbe" );
        return true;
    }
#endif

    if ( std::strcmp( caseName, "texture-slot-capacity" ) == 0 )
    {
        SkullbonezCore::Textures::TextureCollectionTestAccess::FirstFreeSlot(
            SkullbonezCore::Scene::Capacity::TOTAL_TEXTURE_COUNT );
        return true;
    }

    if ( std::strcmp( caseName, LEGACY_IH4_DX12_MISSED_FAILURE_CASE ) == 0 )
    {
        RunLegacyAssertOnlyControl( false, "dx12 lifecycle" );
        return true;
    }

    if ( std::strcmp( caseName, LEGACY_IH23_TORNADO_MISSED_FAILURE_CASE ) == 0 )
    {
        RunIh23LegacyAssertOnlyControl( false, "tornado lifecycle" );
        return true;
    }

    if ( std::strcmp( caseName, LEGACY_IH23_COLLIDER_MISSED_FAILURE_CASE ) == 0 )
    {
        RunIh23LegacyAssertOnlyControl( false, "collider shape/index" );
        return true;
    }

    if ( std::strcmp( caseName, LEGACY_IH23_DISJOINT_SET_MISSED_FAILURE_CASE ) == 0 )
    {
        RunIh23LegacyAssertOnlyControl( false, "disjoint-set scratch" );
        return true;
    }

    if ( std::strcmp( caseName, LEGACY_IH23_PHYSICS_BODY_MISSED_FAILURE_CASE ) == 0 )
    {
        RunIh23LegacyAssertOnlyControl( false, "physics-body index/span" );
        return true;
    }

    if ( std::strcmp( caseName, LEGACY_IH4_PRIMITIVE_MISSED_FAILURE_CASE ) == 0 )
    {
        RunLegacyAssertOnlyControl( false, "primitive scope" );
        return true;
    }

    if ( std::strcmp( caseName, LEGACY_IH4_WORLD_MISSED_FAILURE_CASE ) == 0 )
    {
        RunLegacyAssertOnlyControl( false, "world rebuild" );
        return true;
    }

    if ( std::strcmp( caseName, LEGACY_IH4_PREVIEW_MISSED_FAILURE_CASE ) == 0 )
    {
        RunLegacyAssertOnlyControl( false, "preview capacity" );
        return true;
    }

    if ( std::strcmp( caseName, LEGACY_IH5_SKY_MISSED_FAILURE_CASE ) == 0 )
    {
        RunIh5LegacyAssertOnlyControl( false, "sky pass" );
        return true;
    }

    if ( std::strcmp( caseName, LEGACY_IH5_RUN_RENDERER_MISSED_FAILURE_CASE ) == 0 )
    {
        RunIh5LegacyAssertOnlyControl( false, "Run renderer" );
        return true;
    }

    if ( std::strcmp( caseName, LEGACY_IH5_UI_PROFILER_MISSED_FAILURE_CASE ) == 0 )
    {
        RunIh5LegacyAssertOnlyControl( false, "UI profiler" );
        return true;
    }

    if ( std::strcmp( caseName, "render-preview-capacity" ) == 0 )
    {
        SkullbonezCore::Runtime::RuntimeRenderTargetPreviewSnapshot snapshot;

        for ( int index = 0; index < 10; ++index )
        {
            snapshot.AppendCatalogTarget( {} );
        }

        snapshot.AppendOptionalDxrTarget( {} );
        snapshot.AppendCatalogTarget( {} );
        snapshot.AppendOptionalDxrTarget( {} );

        return true;
    }

    const bool geometryBeforeInit = std::strcmp( caseName, "dx12-geometry-before-init" ) == 0;
    const bool geometryAfterShutdown = std::strcmp( caseName, "dx12-geometry-after-shutdown" ) == 0;

    if ( geometryBeforeInit || geometryAfterShutdown )
    {
        SkullbonezCore::Rendering::Dx12GeometryOwnerTestAccess::EpochProbe epoch;

        if ( geometryAfterShutdown )
        {
            epoch.Bind();
            epoch.Begin();
            epoch.Close();
        }

        epoch.Require( geometryAfterShutdown ? "AfterShutdownProbe" : "BeforeInitProbe" );
        return true;
    }

    const bool textureBeforeInit = std::strcmp( caseName, "dx12-texture-before-init" ) == 0;
    const bool textureAfterShutdown = std::strcmp( caseName, "dx12-texture-after-shutdown" ) == 0;

    if ( textureBeforeInit || textureAfterShutdown )
    {
        SkullbonezCore::Rendering::Dx12TextureOwnerTestAccess::EpochProbe epoch;

        if ( textureAfterShutdown )
        {
            epoch.Bind();
            epoch.Begin();
            epoch.Close();
        }

        epoch.Require( textureAfterShutdown ? "AfterShutdownProbe" : "BeforeInitProbe" );
        return true;
    }

    const bool primitiveMoved = std::strcmp( caseName, "primitive-scope-moved" ) == 0;
    const bool primitiveInactive = std::strcmp( caseName, "primitive-scope-inactive" ) == 0;
    const bool primitiveVisibleAsShadow = std::strcmp( caseName, "primitive-visible-as-shadow" ) == 0;
    const bool primitiveShadowAsVisible = std::strcmp( caseName, "primitive-shadow-as-visible" ) == 0;

    if ( primitiveMoved || primitiveInactive || primitiveVisibleAsShadow || primitiveShadowAsVisible )
    {
        int rendererIdentity = 0;
        const SkullbonezCore::Rendering::PrimitiveBatchKind
            kind = primitiveShadowAsVisible ? SkullbonezCore::Rendering::PrimitiveBatchKind::ShadowSphere
                                            : SkullbonezCore::Rendering::PrimitiveBatchKind::Sphere;
        SkullbonezCore::Rendering::PrimitiveBatchScopeLifecycle scope( &rendererIdentity, kind );

        if ( primitiveMoved )
        {
            SkullbonezCore::Rendering::PrimitiveBatchScopeLifecycle destination( std::move( scope ) );
            scope.RequireVisible();
        }
        else
        {
            if ( primitiveInactive )
            {
                scope.Close();
            }

            if ( primitiveVisibleAsShadow )
            {
                scope.RequireShadow();
            }
            else
            {
                scope.RequireVisible();
            }
        }

        return true;
    }

    if ( std::strcmp( caseName, "primitive-resource-owner-mismatch" ) == 0 )
    {
        int resourcesIdentity = 0;
        int texturesIdentity = 0;
        int replacementTexturesIdentity = 0;
        int geometryIdentity = 0;
        SkullbonezCore::Rendering::PrimitiveResourceOwnerIdentity owners;
        owners.Bind( &resourcesIdentity, &texturesIdentity, &geometryIdentity );
        owners.Bind( &resourcesIdentity, &replacementTexturesIdentity, &geometryIdentity );
        return true;
    }

    return false;
}

bool RunRuntimeFatalResourceAndPhysicsCase( const char* caseName )
{
    const bool skyBeforeInit = std::strcmp( caseName, "skybox-before-init" ) == 0;
    const bool skyAfterRelease = std::strcmp( caseName, "skybox-after-release" ) == 0;

    if ( skyBeforeInit || skyAfterRelease )
    {
        int texturesIdentity = 0;
        int configIdentity = 0;
        int assetsIdentity = 0;
        int resourcesIdentity = 0;
        SkullbonezCore::Geometry::SkyBoxRenderRebuildLease bindings;

        if ( skyAfterRelease )
        {
            bindings.BindTextures( &texturesIdentity );
            bindings.BindContexts( &configIdentity, &assetsIdentity, &resourcesIdentity );
            bindings.Release();
        }

        bindings.Require( skyAfterRelease ? "AfterReleaseProbe" : "BeforeInitProbe" );
        return true;
    }

    if ( std::strcmp( caseName, "world-water-before-init" ) == 0 )
    {
        SkullbonezCore::Environment::WaterRenderRebuildLease bindings;
        bindings.Require( "BeforeInitProbe" );
        return true;
    }

    if ( std::strcmp( caseName, "terrain-render-resources-before-init" ) == 0 )
    {
        SkullbonezCore::Geometry::TerrainRenderRebuildLease bindings;
        bindings.Require( "BeforeInitProbe" );
        return true;
    }

    if ( std::strcmp( caseName, "terrain-missing-clip-plane" ) == 0 )
    {
        SkullbonezCore::Geometry::TerrainRenderLifecycleTestAccess::RequireClipPlane( nullptr );
        return true;
    }

    const bool skyPassBeforeInit = std::strcmp( caseName, "sky-pass-before-init" ) == 0;
    const bool skyPassAfterRelease = std::strcmp( caseName, "sky-pass-after-release" ) == 0;

    if ( skyPassBeforeInit || skyPassAfterRelease )
    {
        SkullbonezCore::Runtime::SkyPassTestAccess::Probe skyLease;
        int skyIdentity = 0;

        if ( skyPassAfterRelease )
        {
            skyLease.Open( &skyIdentity );
            skyLease.Close();
        }

        skyLease.Require( skyPassAfterRelease ? "AfterReleaseProbe" : "BeforeInitProbe" );
        return true;
    }

    const bool runRendererBeforeInit = std::strcmp( caseName, "run-renderer-before-init" ) == 0;
    const bool runRendererAfterShutdown = std::strcmp( caseName, "run-renderer-after-shutdown" ) == 0;

    if ( runRendererBeforeInit || runRendererAfterShutdown )
    {
        SkullbonezCore::Runtime::RunRendererLifecycleTestAccess::Probe rendererLifecycle;

        if ( runRendererAfterShutdown )
        {
            rendererLifecycle.Bind();
            rendererLifecycle.Close();
        }

        rendererLifecycle.Require( runRendererAfterShutdown ? "AfterShutdownProbe" : "BeforeInitProbe" );
        return true;
    }

    const bool uiProfilerBeforeInit = std::strcmp( caseName, "ui-profiler-before-init" ) == 0;
    const bool uiProfilerAfterRelease = std::strcmp( caseName, "ui-profiler-after-release" ) == 0;

    if ( uiProfilerBeforeInit || uiProfilerAfterRelease )
    {
        auto profiler = std::make_unique<SkullbonezCore::Core::Profiler>();

        SkullbonezCore::Runtime::UiTextPassTestAccess::ProfilerProbe profilerLifecycle( profiler.get() );

        if ( uiProfilerAfterRelease )
        {
            profilerLifecycle.Activate();
            profilerLifecycle.Close();
        }

        profilerLifecycle.Require( uiProfilerAfterRelease ? "AfterReleaseProbe" : "BeforeInitProbe" );
        return true;
    }

    if ( std::strcmp( caseName, "collider-rebind-shape-index" ) == 0 )
    {
        SkullbonezCore::Physics::ColliderStoreTestAccess::
            RequireShapeStorage( SkullbonezCore::Physics::ColliderShapeKind::Sphere, 3u, 3u, 3u, "rebind" );
        return true;
    }

    if ( std::strcmp( caseName, "collider-remove-shape-index" ) == 0 )
    {
        SkullbonezCore::Physics::ColliderStoreTestAccess::
            RequireShapeStorage( SkullbonezCore::Physics::ColliderShapeKind::Box, 1u, 1u, 1u, "remove" );
        return true;
    }

    if ( std::strcmp( caseName, "collider-hull-shape-parity" ) == 0 )
    {
        SkullbonezCore::Physics::ColliderStoreTestAccess::
            RequireShapeStorage( SkullbonezCore::Physics::ColliderShapeKind::ConvexHull, 0u, 1u, 0u, "remove" );
        return true;
    }

    if ( std::strcmp( caseName, "disjoint-set-scratch-capacity" ) == 0 )
    {
        std::array<int, 1> parent {};
        std::array<uint8_t, 1> rank {};
        SkullbonezCore::Physics::DisjointSet disjointSet( std::span<int>( parent ), std::span<uint8_t>( rank ), 2 );
        disjointSet.Reset();
        return true;
    }

    if ( std::strcmp( caseName, "physics-body-hot-index" ) == 0 )
    {
        SkullbonezCore::Physics::PhysicsBodyStore store;
        SkullbonezCore::Physics::PhysicsBodyStoreTestAccess::ReadHotState( store, 0 );
        return true;
    }

    if ( std::strcmp( caseName, "physics-body-sleep-destination" ) == 0 )
    {
        SkullbonezCore::Physics::PhysicsBodyStore store;

        {
            RuntimeAllocationScope sceneLoadScope( RuntimeAllocationPhase::SceneLoad );
            store.ReserveCapacity( 1u );
            store.CreateBodyRecord( SkullbonezCore::Physics::PhysicsBodyCreateRecord {} );
        }

        std::array<uint8_t, 0> sleepStates {};
        store.CopySleepStatesTo( sleepStates );
        return true;
    }

    const bool collisionVisualShort = std::strcmp( caseName, "contact-side-effect-collision-visual" ) == 0;
    const bool fixedContactShort = std::strcmp( caseName, "contact-side-effect-fixed-contact" ) == 0;
    const bool releaseWakeShort = std::strcmp( caseName, "contact-side-effect-release-wake" ) == 0;
    const bool fixedTreeShort = std::strcmp( caseName, "contact-side-effect-fixed-tree" ) == 0;
    const bool pipelineShort = std::strcmp( caseName, "contact-side-effect-pipeline" ) == 0;

    if ( collisionVisualShort || fixedContactShort || releaseWakeShort || fixedTreeShort || pipelineShort )
    {
        SkullbonezCore::Physics::PhysicsContactSolverStage stage;
        SkullbonezCore::Physics::PhysicsContactSolverStageTestAccess::ReserveAndPrepare( stage,
                                                                                         collisionVisualShort ? 3u : 4u,
                                                                                         fixedContactShort ? 1u : 2u,
                                                                                         releaseWakeShort ? 1u : 2u,
                                                                                         fixedTreeShort ? 1u : 2u,
                                                                                         pipelineShort ? 1u : 2u );
        return true;
    }

    const bool tornadoVisualUnprepared = std::strcmp( caseName, "tornado-visual-unprepared-frame" ) == 0;
    const bool tornadoVisualReleased = std::strcmp( caseName, "tornado-visual-released-frame" ) == 0;

    if ( tornadoVisualUnprepared || tornadoVisualReleased )
    {
        SkullbonezCore::Gameplay::TornadoVisualPass pass;
        SkullbonezCore::Gameplay::TornadoFieldConfig field;
        SkullbonezCore::Gameplay::TornadoSystemConfig system;

        if ( tornadoVisualReleased )
        {
            SkullbonezCore::Gameplay::TornadoVisualPassTestAccess::Prepare( pass, field, system );
            pass.ReleaseResources();
        }

        SkullbonezCore::Gameplay::TornadoVisualPassTestAccess::RequirePrepared( pass, tornadoVisualReleased
                                                                                          ? "ReleasedProbe"
                                                                                          : "UnpreparedProbe" );
        return true;
    }

    if ( std::strcmp( caseName, "physics-pipeline-batch-full-mode" ) == 0 )
    {
        SkullbonezCore::Physics::PhysicsPipelineTraceRecorder recorder;
        recorder.BeginStep( true );
        recorder.RecordEvents( 1u );
    }

    return false;
}

bool RunRuntimeFatalTransactionAndTerrainCase( const char* caseName )
{
    if ( std::strcmp( caseName, "renderer-frame-transaction-abandoned" ) == 0 )
    {
        auto transaction = SkullbonezCore::Runtime::RuntimeRendererWorldOverlayTransactionTestAccess::Open();
        return true;
    }

    if ( std::strcmp( caseName, "renderer-frame-transaction-duplicate" ) == 0 )
    {
        auto transaction = SkullbonezCore::Runtime::RuntimeRendererWorldOverlayTransactionTestAccess::Open();
        SkullbonezCore::Runtime::RuntimeRendererWorldOverlayTransactionTestAccess::SubmitOverlays( transaction );
        SkullbonezCore::Runtime::RuntimeRendererWorldOverlayTransactionTestAccess::SubmitOverlays( transaction );
        return true;
    }

    if ( std::strcmp( caseName, "dx12-retirement-release-snapshot" ) == 0 )
    {
        SkullbonezCore::Rendering::Dx12RetirementDiagnosticState retirementDiagnostics;
        retirementDiagnostics.ObservePendingCount(
            SkullbonezCore::Rendering::Dx12DeferredReleaseOwner::MAX_PENDING_RETIREMENTS );
        retirementDiagnostics.ObserveRelease( 9u, 4u, true, 77u );
        retirementDiagnostics
            .FatalExhaustion( SkullbonezCore::Rendering::Dx12DeferredReleaseOwner::MAX_PENDING_RETIREMENTS,
                              SkullbonezCore::Rendering::Dx12DeferredReleaseOwner::MAX_PENDING_RETIREMENTS );
    }

    const bool resetForDevice = std::strcmp( caseName, "dx12-retirement-reset-live-device" ) == 0;
    const bool resetAfterShutdown = std::strcmp( caseName, "dx12-retirement-reset-live-shutdown" ) == 0;

    if ( resetForDevice || resetAfterShutdown )
    {
        SkullbonezCore::Rendering::Dx12DeferredReleaseOwner retirements;
        retirements.QuarantineStaticDescriptor( 7u );

        if ( resetForDevice )
        {
            retirements.ResetForDevice();
        }
        else
        {
            retirements.ResetAfterShutdown();
        }

        return true;
    }

    if ( std::strcmp( caseName, "dx12-retirement-capacity" ) == 0 )
    {
        SkullbonezCore::Rendering::Dx12DeferredReleaseOwner retirements;

        for ( size_t index = 0; index < retirements.MAX_PENDING_RETIREMENTS; ++index )
        {
            retirements.QuarantineStaticDescriptor( static_cast<UINT>( index ) );
        }

        retirements.QuarantineStaticDescriptor( static_cast<UINT>( retirements.MAX_PENDING_RETIREMENTS ) );
        return true;
    }

    unsigned int contactSolvePhaseFrom = 0u;
    unsigned int contactSolvePhaseTo = 0u;

    if ( sscanf_s( caseName, "contact-solve-phase-%u-%u", &contactSolvePhaseFrom, &contactSolvePhaseTo ) == 2 )
    {
        using SkullbonezCore::Physics::PersistentContactSolvePhaseCursor;
        using SkullbonezCore::Physics::PersistentContactSolveTransaction;
        using SkullbonezCore::Physics::PersistentContactSolveTransactionTestAccess;
        using Phase = PersistentContactSolvePhaseCursor::Phase;
        constexpr std::array phases { Phase::Idle,
                                      Phase::EntryPolicySetup,
                                      Phase::BodySetup,
                                      Phase::BuildManifolds,
                                      Phase::TerrainRows,
                                      Phase::Precompute,
                                      Phase::SolveRows,
                                      Phase::PointSupportInstability,
                                      Phase::TerrainRestPolicy,
                                      Phase::WriteBack,
                                      Phase::DebugContacts,
                                      Phase::PositionCorrection,
                                      Phase::CacheStore,
                                      Phase::FixedContactRelease,
                                      Phase::Complete,
                                      Phase::Count };

        if ( contactSolvePhaseFrom >= phases.size() - 1u || contactSolvePhaseTo >= phases.size() )
        {
            return false;
        }

        PersistentContactSolveTransaction transaction;

        for ( unsigned int phaseIndex = 1u; phaseIndex <= contactSolvePhaseFrom; ++phaseIndex )
        {
            PersistentContactSolveTransactionTestAccess::Advance( transaction, phases[phaseIndex] );
        }

        PersistentContactSolveTransactionTestAccess::Advance( transaction, phases[contactSolvePhaseTo] );
        return true;
    }

    unsigned int operatorPhaseFrom = 0u;
    unsigned int operatorPhaseTo = 0u;

    if ( sscanf_s( caseName, "operator-command-phase-%u-%u", &operatorPhaseFrom, &operatorPhaseTo ) == 2 )
    {
        using SkullbonezCore::Runtime::OperatorCommandPhaseCursor;
        using SkullbonezCore::Runtime::OperatorCommandTransaction;
        using SkullbonezCore::Runtime::OperatorCommandTransactionTestAccess;
        using Phase = OperatorCommandPhaseCursor::Phase;
        constexpr std::array phases { Phase::Idle,
                                      Phase::DeviceAndMode,
                                      Phase::PhysicsControl,
                                      Phase::RuntimePresentation,
                                      Phase::SimulationPolicy,
                                      Phase::PhysicsMaterial,
                                      Phase::WorldPolicy,
                                      Phase::CinematicPolicy,
                                      Phase::Complete,
                                      Phase::Count };

        if ( operatorPhaseFrom >= phases.size() - 1u || operatorPhaseTo >= phases.size() )
        {
            return false;
        }

        SkullbonezCore::UI::InGameUICommands commands;
        OperatorCommandTransaction transaction( commands );

        for ( unsigned int phaseIndex = 1u; phaseIndex <= operatorPhaseFrom; ++phaseIndex )
        {
            OperatorCommandTransactionTestAccess::Advance( transaction, phases[phaseIndex] );
        }

        OperatorCommandTransactionTestAccess::Advance( transaction, phases[operatorPhaseTo] );
        return true;
    }

    const bool pendingReplayTimeline = std::strcmp( caseName, "replay-restore-pending-timeline-complete" ) == 0;
    const bool unprovedReplayRollback = std::strcmp( caseName, "replay-restore-unproved-rollback" ) == 0;

    if ( pendingReplayTimeline || unprovedReplayRollback )
    {
        using SkullbonezCore::Runtime::ReplayRestoreTransaction;
        using SkullbonezCore::Runtime::ReplaySolverFrameSample;

        ReplayRestoreTransaction transaction;
        transaction.SelectArtifact( 1u, 2u );
        transaction.CaptureLiveBackup( ReplaySolverFrameSample {} );
        transaction.MarkTopologyPrepared( unprovedReplayRollback, unprovedReplayRollback );

        if ( unprovedReplayRollback )
        {
            transaction.MarkRolledBack( "unproved rollback" );
            return true;
        }

        transaction.MarkCheckpointApplied();
        transaction.BeginTargetStep( 0u );
        transaction.MarkTargetStepped( 3u );
        transaction.MarkTargetVerified();
        transaction.PrepareTimelineReset( 4u, 5, 0xA5u );
        transaction.Complete();
        return true;
    }

    if ( std::strcmp( caseName, "physics-fixed-list-runtime-capacity" ) == 0 )
    {
        SkullbonezCore::Physics::PhysicsFixedList<int, 4>
            values( "fatal.physics-fixed-list.runtime",
                    SkullbonezCore::Physics::PhysicsCapacityReason::ExplicitTestCapacity );

        {
            RuntimeAllocationScope sceneLoad( RuntimeAllocationPhase::SceneLoad );
            values.Reserve( 1u );
        }
        values.push_back( 1 );
        values.push_back( 2 );
        return true;
    }

    if ( std::strcmp( caseName, "physics-prediction-seed-wrong-replay-owner" ) == 0 )
    {
        using namespace SkullbonezCore::Core::Allocation;
        constexpr int wrongOwnerHardCapacity = 1024;
        const RuntimeReserveOwnerHandle owner = RuntimeReserveAllocator::RegisterOwner(
            { SkullbonezCore::Physics::PHYSICS_SOLVER_SNAPSHOT_RESERVE_OWNER, RuntimeReserveSubsystem::Replay,
              RuntimeReservePhase::Replay, 0, wrongOwnerHardCapacity, RUNTIME_RESERVE_REPLAY_GROWTH_LIMIT_UNBOUNDED, true,
              "Fatal probe for unrelated Replay growth authority" } );

        RuntimeReserveGrowthResult growth = RuntimeReserveAllocator::
            RequestGrowth( owner, { SkullbonezCore::Physics::PHYSICS_SOLVER_SNAPSHOT_RESERVE_OWNER, "PhysicsEngine seed",
                                    RuntimeReservePhase::Replay, 0, 0, wrongOwnerHardCapacity, 1 } );

        if ( !growth.granted )
        {
            return false;
        }

        auto source = std::make_unique<PhysicsEngine>();
        auto destination = std::make_unique<PhysicsEngine>();
        RuntimeAllocationScope replayScope( RuntimeAllocationPhase::Replay );
        RuntimeReserveOwnerScope ownerScope( owner );
        RuntimeReserveGrowthScope growthScope( owner, RuntimeReservePhase::Replay, growth );
        destination->SeedReplayPredictionStorageFrom( *source );
        return true;
    }

    const bool predictionSeedMissingScope = std::strcmp( caseName, "physics-prediction-seed-missing-scope" ) == 0;
    const bool predictionSeedSceneLoad = std::strcmp( caseName, "physics-prediction-seed-scene-load" ) == 0;
    const bool predictionSeedMissingOwner = std::strcmp( caseName, "physics-prediction-seed-missing-owner" ) == 0;

    if ( predictionSeedMissingScope || predictionSeedSceneLoad || predictionSeedMissingOwner )
    {
        auto source = std::make_unique<PhysicsEngine>();
        auto destination = std::make_unique<PhysicsEngine>();

        if ( predictionSeedSceneLoad )
        {
            RuntimeAllocationScope sceneLoadScope( RuntimeAllocationPhase::SceneLoad );
            destination->SeedReplayPredictionStorageFrom( *source );
            return true;
        }

        if ( predictionSeedMissingOwner )
        {
            RuntimeAllocationScope replayScope( RuntimeAllocationPhase::Replay );
            destination->SeedReplayPredictionStorageFrom( *source );
            return true;
        }

        destination->SeedReplayPredictionStorageFrom( *source );
        return true;
    }

    const bool terrainLocateCellRange = std::strcmp( caseName, "terrain-locate-cell-range" ) == 0;
    const bool terrainLocateNonFinite = std::strcmp( caseName, "terrain-locate-nonfinite" ) == 0;
    const bool terrainLocateUnrepresentable = std::strcmp( caseName, "terrain-locate-unrepresentable" ) == 0;
    const bool terrainLocateInvalidScale = std::strcmp( caseName, "terrain-locate-invalid-scale" ) == 0;

    if ( terrainLocateCellRange || terrainLocateNonFinite || terrainLocateUnrepresentable || terrainLocateInvalidScale )
    {
        char temporaryDirectory[MAX_PATH] = {};
        char heightMapPath[MAX_PATH] = {};

        if ( GetTempPathA( MAX_PATH, temporaryDirectory ) == 0 ||
             GetTempFileNameA( temporaryDirectory, "sbt", 0, heightMapPath ) == 0 )
        {
            return false;
        }

        HANDLE heightMap = CreateFileA( heightMapPath, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY,
                                        nullptr );

        if ( heightMap == INVALID_HANDLE_VALUE )
        {
            DeleteFileA( heightMapPath );
            return false;
        }

        constexpr std::array<unsigned char, 16> PIXELS = {};
        DWORD written = 0u;
        const bool wroteHeightMap = WriteFile( heightMap, PIXELS.data(), static_cast<DWORD>( PIXELS.size() ), &written,
                                               nullptr ) != FALSE &&
                                    written == static_cast<DWORD>( PIXELS.size() );

        CloseHandle( heightMap );

        SkullbonezCore::Core::EngineConfig config;
        config.terrainGeometry.scale = 1.0f;
        std::unique_ptr<SkullbonezCore::Geometry::Terrain> terrain;
        const SbResult created = wroteHeightMap
                                     ? SkullbonezCore::Geometry::Terrain::TryCreatePhysicsFromHeightMap( diagnostics,
                                                                                                         heightMapPath, 4, 1,
                                                                                                         1, config, terrain )
                                     : diagnostics.Failure( "Tests/Terrain", "height-map write failed" );

        DeleteFileA( heightMapPath );

        if ( !created.Ok() || !terrain )
        {
            return false;
        }

        if ( terrainLocateInvalidScale )
        {
            config.terrainGeometry.scale = ( std::numeric_limits<float>::quiet_NaN )();
        }

        // The exact upper X edge maps to cell 3 when only cells 0..2 exist.
        // NaN and an invalid scale must terminate before floor-to-integer
        // conversion; a finite maximum float must terminate before the integer
        // cast. These probes exercise LocatePolygon's local query guards.
        const float xPosition = terrainLocateNonFinite
                                    ? ( std::numeric_limits<float>::quiet_NaN )()
                                    : ( terrainLocateUnrepresentable ? ( std::numeric_limits<float>::max )() : 3.0f );

        (void)terrain->LocatePolygon( xPosition, 0.0f );
        return true;
    }

    return false;
}

bool RunRuntimeFatalAllocationAndGridCase( const char* caseName )
{
    if ( std::strcmp( caseName, "allocation-foreign-page-boundary" ) == 0 )
    {
        SYSTEM_INFO systemInfo = {};
        GetSystemInfo( &systemInfo );
        const std::size_t pageSize = static_cast<std::size_t>( systemInfo.dwPageSize );
        void* region = VirtualAlloc( nullptr, pageSize * 2u, MEM_RESERVE, PAGE_NOACCESS );

        if ( !region )
        {
            return false;
        }

        auto* committedPage = static_cast<unsigned char*>( region ) + pageSize;

        if ( VirtualAlloc( committedPage, pageSize, MEM_COMMIT, PAGE_READWRITE ) != committedPage )
        {
            VirtualFree( region, 0u, MEM_RELEASE );
            return false;
        }

        // The candidate begins eight bytes inside the inaccessible page, but
        // its magic field is readable in the committed page. A magic-only
        // probe would admit it and then fault on raw; the whole-header copy
        // must classify it as unreadable.
        auto* foreignPointer = committedPage + sizeof( ForeignAllocationHeaderLayout ) - sizeof( uint64_t );
        auto* candidate = foreignPointer - sizeof( ForeignAllocationHeaderLayout );
        auto* readableMagic = reinterpret_cast<uint32_t*>( candidate + offsetof( ForeignAllocationHeaderLayout, magic ) );
        *readableMagic = FOREIGN_ALLOCATION_HEADER_MAGIC;

        SkullbonezCore::Core::Allocation::SetRuntimeAllocationPhase( RuntimeAllocationPhase::Diagnostics );
        ::operator delete( foreignPointer );
        return true;
    }

    if ( std::strcmp( caseName, "allocation-foreign-shaped-header" ) == 0 )
    {
        auto* candidate = static_cast<ForeignAllocationHeaderLayout*>(
            std::malloc( sizeof( ForeignAllocationHeaderLayout ) ) );

        if ( !candidate )
        {
            return false;
        }

        std::memset( candidate, 0, sizeof( *candidate ) );

        SYSTEM_INFO systemInfo = {};
        GetSystemInfo( &systemInfo );
        candidate->raw = VirtualAlloc( nullptr, systemInfo.dwPageSize, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE );

        if ( !candidate->raw )
        {
            std::free( candidate );
            return false;
        }

        candidate->size = 64u;
        candidate->phase = static_cast<uint32_t>( RuntimeAllocationPhase::Diagnostics );
        candidate->magic = FOREIGN_ALLOCATION_HEADER_MAGIC;
        candidate->ownershipCookie = 0u;
        SkullbonezCore::Core::Allocation::SetRuntimeAllocationPhase( RuntimeAllocationPhase::Diagnostics );
        ::operator delete( candidate + 1 );
        return true;
    }

    if ( std::strcmp( caseName, "allocation-size-overflow" ) == 0 )
    {
        static_cast<void>( ::operator new( ( std::numeric_limits<std::size_t>::max )() ) );
        return true;
    }

    if ( std::strcmp( caseName, "allocation-foreign-crt-release" ) == 0 )
    {
        SkullbonezCore::Core::Allocation::SetRuntimeAllocationGuardMode(
            SkullbonezCore::Core::Allocation::RuntimeAllocationGuardMode::Measure );
        SkullbonezCore::Core::Allocation::SetRuntimeAllocationPhase( RuntimeAllocationPhase::Diagnostics );
        void* foreignPointer = std::malloc( 64u );

        if ( !foreignPointer )
        {
            return false;
        }

        ::operator delete( foreignPointer );
        const bool counted = SkullbonezCore::Core::Allocation::RuntimeAllocationForeignFreeCount() == 1u;

        const bool guardFailed = SkullbonezCore::Core::Allocation::RuntimeAllocationGuardHasGameplayViolations();

        SkullbonezCore::Core::Allocation::PrintRuntimeAllocationSummary( stdout );
        return counted && guardFailed;
    }

    if ( std::strcmp( caseName, "physics-fixed-list-compile-capacity" ) == 0 )
    {
        SkullbonezCore::Physics::PhysicsFixedList<int, 2>
            values( "fatal.physics-fixed-list.compile",
                    SkullbonezCore::Physics::PhysicsCapacityReason::ExplicitTestCapacity );

        RuntimeAllocationScope sceneLoad( RuntimeAllocationPhase::SceneLoad );
        values.Reserve( 3u );
        return true;
    }

    if ( std::strcmp( caseName, "physics-fixed-list-phase" ) == 0 )
    {
        SkullbonezCore::Physics::PhysicsFixedList<int, 2>
            values( "fatal.physics-fixed-list.phase", SkullbonezCore::Physics::PhysicsCapacityReason::ExplicitTestCapacity );
        values.Reserve( 1u );
        return true;
    }

    if ( std::strcmp( caseName, "spatial-grid-nan" ) == 0 )
    {
        static SpatialGrid grid( 10.0f );
        {
            RuntimeAllocationScope sceneLoadScope( RuntimeAllocationPhase::SceneLoad );
            grid.ReserveSceneCapacity( 8u );
        }
        grid.Insert( 7, Vector3( std::numeric_limits<float>::quiet_NaN(), 0.0f, 0.0f ), 1.0f );
        return true;
    }

    if ( std::strcmp( caseName, "spatial-grid-extent" ) == 0 )
    {
        static SpatialGrid grid( 10.0f );
        {
            RuntimeAllocationScope sceneLoadScope( RuntimeAllocationPhase::SceneLoad );
            grid.ReserveSceneCapacity( 12u );
        }
        grid.Insert( 11, Vector3( SpatialGrid::MAX_WORLD_COORDINATE + 1.0f, 0.0f, 0.0f ), 1.0f );
        return true;
    }

    if ( std::strcmp( caseName, "spatial-grid-zero-cell" ) == 0 )
    {
        static SpatialGrid grid( 0.0f );
        return true;
    }

    if ( std::strcmp( caseName, "spatial-grid-nan-cell" ) == 0 )
    {
        static SpatialGrid grid( std::numeric_limits<float>::quiet_NaN() );
        return true;
    }

    if ( std::strcmp( caseName, "spatial-grid-tiny-cell" ) == 0 )
    {
        static SpatialGrid grid( SpatialGrid::MIN_CELL_SIZE * 0.5f );
        return true;
    }

    if ( std::strcmp( caseName, "spatial-grid-reserve-phase" ) == 0 )
    {
        static SpatialGrid grid( 1.0f );
        grid.ReserveSceneCapacity( 1u );
        return true;
    }

    if ( std::strcmp( caseName, "spatial-grid-entry-capacity" ) == 0 )
    {
        static SpatialGrid grid( 1.0f );

        {
            RuntimeAllocationScope sceneLoadScope( RuntimeAllocationPhase::SceneLoad );
            grid.ReserveSceneCapacity( 1u );
        }

        grid.BeginFrame( 1 );
        RuntimeAllocationScope physicsScope( RuntimeAllocationPhase::Physics );
        grid.Insert( 0, Vector3( 0.25f, 0.25f, 0.25f ), 5.0f );
        return true;
    }

    if ( std::strcmp( caseName, "spatial-grid-overlay-entry-capacity" ) == 0 )
    {
        static SpatialGrid grid( 1.0f );

        {
            RuntimeAllocationScope sceneLoadScope( RuntimeAllocationPhase::SceneLoad );
            grid.ReserveSceneCapacity( 2u );
        }

        grid.BeginFrame( 2 );
        RuntimeAllocationScope physicsScope( RuntimeAllocationPhase::Physics );
        grid.InsertSwept( 0, Vector3( 0.25f, 0.25f, 0.25f ), Vector3( 2047.0f, 0.0f, 0.0f ), 0.0f );
        grid.InsertSwept( 1, Vector3( 5000.25f, 0.25f, 0.25f ), Vector3( 2050.0f, 0.0f, 0.0f ), 0.0f );
        std::fputs( "spatial-grid-overlay-fallback-complete\n", stdout );
        return true;
    }

    if ( std::strcmp( caseName, "spatial-grid-bucket-capacity" ) == 0 )
    {
        static SpatialGrid grid( 1.0f );

        {
            RuntimeAllocationScope sceneLoadScope( RuntimeAllocationPhase::SceneLoad );
            grid.ReserveSceneCapacity( SpatialGrid::MAX_BUCKETS );
        }

        constexpr int persistentCells = SpatialGrid::MAX_BUCKETS / 2;

        for ( int cell = 0; cell < persistentCells; ++cell )
        {
            grid.Insert( cell, Vector3( static_cast<float>( cell ) + 0.25f, 0.25f, 0.25f ), 0.0f );
        }

        // Hazard: repeated Insert calls now move one persistent body. Fill the
        // remaining legal cells through the bounded swept-overlay path, then
        // request one genuinely new cell to exercise the fatal invariant table limit.
        const Vector3 sweepStart( static_cast<float>( persistentCells ) + 0.25f, 0.25f, 0.25f );
        grid.InsertSwept( persistentCells, sweepStart, Vector3( static_cast<float>( persistentCells - 1 ), 0.0f, 0.0f ),
                          0.0f );

        grid.Insert( persistentCells + 1, Vector3( static_cast<float>( SpatialGrid::MAX_BUCKETS ) + 0.25f, 0.25f, 0.25f ),
                     0.0f );

        return true;
    }

    if ( std::strcmp( caseName, "sleep-support-edge-reserved-capacity" ) == 0 )
    {
        static SkullbonezCore::Physics::PhysicsCandidatePairList
            edges { "TestRuntimeContracts.sleepSupportEdgesReserved",
                    SkullbonezCore::Physics::PhysicsCapacityReason::ExplicitTestCapacity };
        {
            RuntimeAllocationScope sceneLoadScope( RuntimeAllocationPhase::SceneLoad );
            edges.Reserve( 2u );
        }
        edges.clear();
        AppendSleepSupportEdge( edges, 0, 1 );
        AppendSleepSupportEdge( edges, 1, 2 );

        // Hazard: requested=3 is far below the semantic ceiling. The owner must
        // still fail before PhysicsFixedList can silently exceed the actual
        // scene-committed reservation of two rows.
        AppendSleepSupportEdge( edges, 2, 3 );
        return true;
    }

    if ( std::strcmp( caseName, "sleep-support-edge-capacity" ) == 0 )
    {
        static SkullbonezCore::Physics::PhysicsCandidatePairList
            edges { "TestRuntimeContracts.sleepSupportEdges",
                    SkullbonezCore::Physics::PhysicsCapacityReason::ExplicitTestCapacity };
        {
            SkullbonezCore::Core::Allocation::RuntimeAllocationScope sceneLoadScope(
                SkullbonezCore::Core::Allocation::RuntimeAllocationPhase::SceneLoad );
            edges.Reserve( MAX_SLEEP_SUPPORT_EDGES );
        }
        edges.clear();

        for ( std::size_t edgeIndex = 0; edgeIndex <= MAX_SLEEP_SUPPORT_EDGES; ++edgeIndex )
        {
            AppendSleepSupportEdge( edges, 0, 1 );
        }

        return true;
    }

    return false;
}

bool RunRuntimeFatalConcurrencyAndDiagnosticsCase( const char* caseName )
{
    if ( std::strcmp( caseName, "amortized-task-in-flight-destroy" ) == 0 )
    {
        LockOrderValidator lockOrderValidator;
        WorkerPool pool( lockOrderValidator );
        pool.Initialise( 1 );
        std::atomic<bool> started { false };
        std::atomic<bool> release { false };
        {
            AmortizedTask task( 1, 1,
                                [&]( int, int )
                                {
                                    started.store( true, std::memory_order_release );

                                    while ( !release.load( std::memory_order_acquire ) )
                                    {
                                        std::this_thread::yield();
                                    }
                                } );
            task.SubmitTick( pool );

            while ( !started.load( std::memory_order_acquire ) )
            {
                std::this_thread::yield();
            }
        }
        return true;
    }

    if ( std::strcmp( caseName, "worker-fatal-log" ) == 0 )
    {
        LockOrderValidator lockOrderValidator;
        WorkerPool pool( lockOrderValidator );
        pool.Initialise( 1 );
        WorkerFatalProbe probe;
        pool.SubmitNoAlloc( probe );

        for ( ;; )
        {
            std::this_thread::yield();
        }
    }

    if ( std::strcmp( caseName, "scene-capacity-hard-ceiling" ) == 0 )
    {
        auto engine = std::make_unique<PhysicsEngine>();
        RuntimeAllocationScope sceneLoadScope( RuntimeAllocationPhase::SceneLoad );
        engine->ReserveAuthoredBodyCapacity( 9000u, 9000u, 0u, 0u, 0u );
        return true;
    }

    if ( std::strcmp( caseName, "point-joint-scene-capacity" ) == 0 )
    {
        auto engine = std::make_unique<PhysicsEngine>();
        SkullbonezCore::Physics::PhysicsBodyHandle bodies[2];
        {
            RuntimeAllocationScope sceneLoadScope( RuntimeAllocationPhase::SceneLoad );
            engine->ReserveAuthoredBodyCapacity( 2u, 2u, 0u, 0u, 12u );
            engine->ReserveAuthoredBodyCapacity( 2u, 2u, 0u, 0u, 8u );

            const SkullbonezCore::Math::CollisionDetection::CollisionShape
                shape = SkullbonezCore::Math::CollisionDetection::BoundingSphere( 1.0f, Vector3( 0.0f, 0.0f, 0.0f ) );

            for ( uint32_t bodyIndex = 0u; bodyIndex < 2u; ++bodyIndex )
            {
                const SkullbonezCore::Physics::PhysicsSceneObjectId sceneObjectId { bodyIndex + 1u };
                const auto bodyDesc = SkullbonezCore::Physics::
                    MakePhysicsBodyCreateDesc( sceneObjectId, shape,
                                               Vector3( static_cast<float>( bodyIndex ) * 3.0f, 0.0f, 0.0f ),
                                               SkullbonezCore::Math::Orientation::IDENTITY_QUATERNION,
                                               Vector3( 0.0f, 0.0f, 0.0f ), Vector3( 0.0f, 0.0f, 0.0f ),
                                               Vector3( 1.0f, 1.0f, 1.0f ), 1.0f, 0.0f,
                                               SkullbonezCore::Physics::PhysicsBodyMotionKind::Dynamic,
                                               "fatal-point-joint-body" );

                auto colliderDesc = SkullbonezCore::Physics::MakeColliderCreateDesc( shape, 0.0f, 0u, "fatal" );
                colliderDesc.sceneObjectId = sceneObjectId;
                const auto registration = engine->RegisterAuthoredBody( bodyDesc, colliderDesc );

                if ( !registration.IsValid() )
                {
                    return false;
                }

                bodies[bodyIndex] = registration.body;
            }
        }

        SkullbonezCore::Physics::PhysicsPointJointCreateDesc desc;
        desc.bodyA = bodies[0];
        desc.bodyB = bodies[1];

        for ( int jointIndex = 0; jointIndex < 9; ++jointIndex )
        {
            (void)engine->CreatePointJoint( desc );
        }

        return true;
    }

    if ( std::strcmp( caseName, "sb-diagnostic-capacity" ) == 0 )
    {
        auto store = std::make_unique<SkullbonezCore::Core::SbDiagnosticStore>();
        std::array<SkullbonezCore::Core::SbResult, SkullbonezCore::Core::SbDiagnosticStore::CAPACITY + 1u> leases;

        for ( std::size_t index = 0; index < leases.size(); ++index )
        {
            leases[index] = store->Failure( "FatalCapacity", "slot=%zu", index );
        }

        return true;
    }

    if ( std::strcmp( caseName, "sb-diagnostic-double-release" ) == 0 )
    {
        auto store = std::make_unique<SkullbonezCore::Core::SbDiagnosticStore>();
        const SkullbonezCore::Core::SbResult lease = store->Failure( "FatalRelease", "double release" );
        const SkullbonezCore::Core::SbDiagnosticIdentity identity = lease.DiagnosticIdentity();
        SkullbonezCore::Core::SbDiagnosticStoreTestAccess::Release( *store, identity.token );
        SkullbonezCore::Core::SbDiagnosticStoreTestAccess::Release( *store, identity.token );
        return true;
    }

    if ( std::strcmp( caseName, "sb-diagnostic-owner-overflow" ) == 0 )
    {
        auto store = std::make_unique<SkullbonezCore::Core::SbDiagnosticStore>();
        std::array<char, SkullbonezCore::Core::SbDiagnosticStore::OWNER_CAPACITY + 1u> owner = {};
        owner.fill( 'o' );
        owner.back() = '\0';
        (void)store->Failure( owner.data(), "owner overflow" );
        return true;
    }

    if ( std::strcmp( caseName, "sb-diagnostic-store-destroyed-with-active-lease" ) == 0 )
    {
        SkullbonezCore::Core::SbResult escapedLease;
        {
            SkullbonezCore::Core::SbDiagnosticStore store;
            escapedLease = store.Failure( "FatalLifetime", "lease escapes store scope" );
        }

        return true;
    }

    if ( std::strcmp( caseName, "sb-diagnostic-lease-overflow" ) == 0 )
    {
        auto store = std::make_unique<SkullbonezCore::Core::SbDiagnosticStore>();
        const SkullbonezCore::Core::SbResult lease = store->Failure( "FatalLeaseOverflow", "saturate lease count" );
        SkullbonezCore::Core::SbDiagnosticStoreTestAccess::SaturateLeaseCount( *store, lease.DiagnosticIdentity().token );
        const SkullbonezCore::Core::SbResult copy = lease;
        return copy.Ok();
    }

    if ( std::strcmp( caseName, "sb-diagnostic-generation-overflow" ) == 0 )
    {
        auto store = std::make_unique<SkullbonezCore::Core::SbDiagnosticStore>();
        SkullbonezCore::Core::SbDiagnosticStoreTestAccess::ExhaustFirstGeneration( *store );
        (void)store->Failure( "FatalGenerationOverflow", "generation must not wrap" );
        return true;
    }

    if ( std::strcmp( caseName, "sb-diagnostic-lock-reentry" ) == 0 )
    {
        auto store = std::make_unique<SkullbonezCore::Core::SbDiagnosticStore>();
        SkullbonezCore::Core::SbDiagnosticStoreTestAccess::ReenterLock( *store );
        return true;
    }

    return false;
}

bool RunRuntimeFatalCase( const char* caseName )
{
    return RunRenderGraphFatalCase( caseName ) || RunRuntimeFatalStartupAndPrimitiveCase( caseName ) ||
           RunRuntimeFatalResourceAndPhysicsCase( caseName ) || RunRuntimeFatalTransactionAndTerrainCase( caseName ) ||
           RunRuntimeFatalAllocationAndGridCase( caseName ) || RunRuntimeFatalConcurrencyAndDiagnosticsCase( caseName );
}

TEST_CASE( "Runtime diagnostics activates perf control only for an owned log artifact" )
{
    CHECK( SkullbonezCore::Runtime::ShouldApplyAuthoredScenePerfLog( nullptr ) );
    CHECK( SkullbonezCore::Runtime::ShouldApplyAuthoredScenePerfLog( "" ) );
    CHECK_FALSE( SkullbonezCore::Runtime::ShouldApplyAuthoredScenePerfLog( "Debug/physics.csv" ) );

    RunPerfLogState perfLog;
    perfLog.isPerfTest = true;
    CHECK_FALSE( RuntimeDiagnostics::OpenScenePerfLog( perfLog, "?:\\skullbonez_perf.csv", 0, nullptr ) );
    CHECK_FALSE( RuntimeDiagnostics::PerfTestActive( perfLog ) );
    CHECK( perfLog.perfLogFile == nullptr );

    char temporaryDirectory[MAX_PATH] = {};
    char perfLogPath[MAX_PATH] = {};
    REQUIRE( GetTempPathA( MAX_PATH, temporaryDirectory ) != 0 );
    REQUIRE( GetTempFileNameA( temporaryDirectory, "sbp", 0, perfLogPath ) != 0 );

    REQUIRE( RuntimeDiagnostics::OpenScenePerfLog( perfLog, perfLogPath, 0, nullptr ) );
    CHECK( RuntimeDiagnostics::PerfTestActive( perfLog ) );
    CHECK( perfLog.perfLogFile != nullptr );

    CHECK( RuntimeDiagnostics::ClosePerfLog( perfLog ) );
    CHECK_FALSE( RuntimeDiagnostics::PerfTestActive( perfLog ) );
    CHECK( perfLog.perfLogFile == nullptr );
    CHECK( DeleteFileA( perfLogPath ) != 0 );
}


TEST_CASE( "Runtime diagnostics fails required perf artifacts on write flush and close errors" )
{
    char temporaryDirectory[MAX_PATH] = {};
    char perfLogPath[MAX_PATH] = {};
    REQUIRE( GetTempPathA( MAX_PATH, temporaryDirectory ) != 0 );
    REQUIRE( GetTempFileNameA( temporaryDirectory, "sbf", 0, perfLogPath ) != 0 );

    RunPerfLogState perfLog;
    REQUIRE( RuntimeDiagnostics::OpenScenePerfLog( perfLog, perfLogPath, 0, nullptr ) );
    SkullbonezCore::Runtime::SetRuntimePerfLogTestFailure( SkullbonezCore::Runtime::RuntimePerfLogTestFailure::Write );
    CHECK_FALSE( RuntimeDiagnostics::TickPerfLog( perfLog, 1, 1, 0.001f, 0.002f, nullptr ) );
    CHECK_FALSE( RuntimeDiagnostics::PerfTestActive( perfLog ) );
    CHECK( perfLog.perfLogFile == nullptr );

    REQUIRE( RuntimeDiagnostics::OpenScenePerfLog( perfLog, perfLogPath, 0, nullptr ) );
    RuntimeDiagnostics::ConfigurePerfLogFlush( perfLog, true, 0 );
    SkullbonezCore::Runtime::SetRuntimePerfLogTestFailure( SkullbonezCore::Runtime::RuntimePerfLogTestFailure::Flush );
    CHECK_FALSE( RuntimeDiagnostics::TickPerfLog( perfLog, 1, 1, 0.001f, 0.002f, nullptr ) );
    CHECK_FALSE( RuntimeDiagnostics::PerfTestActive( perfLog ) );
    CHECK( perfLog.perfLogFile == nullptr );

    REQUIRE( RuntimeDiagnostics::OpenScenePerfLog( perfLog, perfLogPath, 0, nullptr ) );
    SkullbonezCore::Runtime::SetRuntimePerfLogTestFailure( SkullbonezCore::Runtime::RuntimePerfLogTestFailure::Close );
    CHECK_FALSE( RuntimeDiagnostics::ClosePerfLog( perfLog ) );

    SkullbonezCore::Core::SbDiagnosticStore resultDiagnostics;
    SkullbonezCore::Runtime::ApplicationExitState exitState( resultDiagnostics );
    const SkullbonezCore::Core::SbResult failure = SkullbonezCore::Runtime::ApplyPerfLogArtifactStatus( resultDiagnostics,
                                                                                                        exitState, false );
    const SkullbonezCore::Core::SbResult resolved = exitState.Resolve( 0 );
    CHECK_FALSE( resolved.Ok() );
    CHECK( std::strcmp( resolved.ErrorOwner(), "Runtime/Diagnostics" ) == 0 );
    CHECK( std::strstr( resolved.ErrorMessage(), "written, flushed, and closed" ) != nullptr );

    SkullbonezCore::Runtime::ApplicationExitState combinedExitState( resultDiagnostics );
    const SkullbonezCore::Core::SbResult sceneFailure = resultDiagnostics.Failure( "Runtime/Scene",
                                                                                   "scene population failed" );
    const SkullbonezCore::Core::SbResult
        combinedResult = SkullbonezCore::Runtime::ApplySceneLoadDiagnosticsStatus( resultDiagnostics, combinedExitState,
                                                                                   sceneFailure, false );
    CHECK_FALSE( combinedResult.Ok() );
    CHECK( std::strcmp( combinedResult.ErrorOwner(), "Runtime/Scene" ) == 0 );
    const SkullbonezCore::Core::SbResult combinedExitResult = combinedExitState.Resolve( 0 );
    CHECK_FALSE( combinedExitResult.Ok() );
    CHECK( std::strcmp( combinedExitResult.ErrorOwner(), "Runtime/Diagnostics" ) == 0 );
    CHECK( DeleteFileA( perfLogPath ) != 0 );
}


TEST_CASE( "Scene memory diagnostics count complete Physics store capacity" )
{
    using namespace SkullbonezCore::Core::Allocation;

    auto physics = std::make_unique<PhysicsEngine>();
    {
        RuntimeAllocationScope sceneLoadScope( RuntimeAllocationPhase::SceneLoad );
        physics->ReserveAuthoredBodyCapacity( 4u, 2u, 1u, 1u, 2u );
    }

    SkullbonezCore::Rendering::RenderInstanceStore renderInstances;
    const SkullbonezCore::Physics::ColliderStore& colliders = PhysicsEngine::ReadColliders( *physics );
    const uint64_t colliderBytes = colliders.CollectRuntimeCapacityMemoryBytes();
    const uint64_t sceneStoreBytes = physics->CollectSceneSizedStoreMemoryBytes();
    REQUIRE( sceneStoreBytes >= colliderBytes );

    const SkullbonezCore::Core::MainMemoryGameObjectStats stats = SkullbonezCore::Runtime::CollectSceneMemoryStats(
        SkullbonezCore::Runtime::SceneMemoryDiagnosticsView { 0u, 0u, 0u, *physics, renderInstances } );
    CHECK( stats.colliderStoreBytes == colliderBytes );
    CHECK( stats.physicsStoreBytes == sceneStoreBytes - colliderBytes );

    const uint64_t retiredRecordOnlyBytes = static_cast<uint64_t>( PhysicsEngine::ReadBodies( *physics ).RecordCapacity() ) *
                                                sizeof( SkullbonezCore::Physics::PhysicsBodyRecord ) +
                                            static_cast<uint64_t>( PhysicsEngine::ReadBuoyancyFactCapacity( *physics ) ) *
                                                sizeof( SkullbonezCore::Physics::BuoyancyBodyFacts );
    CHECK( stats.physicsStoreBytes > retiredRecordOnlyBytes );
}


TEST_CASE( "Replay prediction engine reuse rejects incompatible scene topology capacity" )
{
    using namespace SkullbonezCore::Core::Allocation;

    auto source = std::make_unique<PhysicsEngine>();
    auto noJoints = std::make_unique<PhysicsEngine>();
    auto compatible = std::make_unique<PhysicsEngine>();

    {
        RuntimeAllocationScope sceneLoadScope( RuntimeAllocationPhase::SceneLoad );
        source->ReserveAuthoredBodyCapacity( 12u, 2u, 10u, 0u, 9u );
        noJoints->ReserveAuthoredBodyCapacity( 12u, 2u, 10u, 0u, 0u );
        compatible->ReserveAuthoredBodyCapacity( 12u, 2u, 10u, 0u, 9u );
    }

    CHECK_FALSE( noJoints->ReplayPredictionStorageCanSeedFrom( *source ) );
    CHECK( compatible->ReplayPredictionStorageCanSeedFrom( *source ) );
}


TEST_CASE( "Main memory dump reports incomplete write flush and close operations" )
{
    char temporaryDirectory[MAX_PATH] = {};
    char dumpPath[MAX_PATH] = {};
    REQUIRE( GetTempPathA( MAX_PATH, temporaryDirectory ) != 0 );
    REQUIRE( GetTempFileNameA( temporaryDirectory, "sbm", 0, dumpPath ) != 0 );

    SkullbonezCore::Runtime::DiagnosticsRuntime runtime;
    runtime.SetMainMemoryDumpPath( dumpPath );
    const SkullbonezCore::Core::MainMemoryReplayStats replay;
    const SkullbonezCore::Core::MainMemoryGameObjectStats gameObjects;
    const SkullbonezCore::Runtime::RuntimeSceneDiagnosticFacts scene( 0, 0, 0, 7, 10, 0 );

    for ( const SkullbonezCore::Runtime::MainMemoryDumpTestFailure failure :
          { SkullbonezCore::Runtime::MainMemoryDumpTestFailure::Write,
            SkullbonezCore::Runtime::MainMemoryDumpTestFailure::Flush,
            SkullbonezCore::Runtime::MainMemoryDumpTestFailure::Close } )
    {
        SkullbonezCore::Runtime::SetMainMemoryDumpTestFailure( failure );
        CHECK_FALSE( runtime.WriteMainMemoryDump( replay, gameObjects, scene, "fault", 1.0 ) );
    }

    CHECK( runtime.WriteMainMemoryDump( replay, gameObjects, scene, "complete", 2.0 ) );
    std::ifstream input( dumpPath, std::ios::binary );
    const std::string bytes { std::istreambuf_iterator<char>( input ), std::istreambuf_iterator<char>() };
    CHECK( bytes.find( "skullbonez.main_memory.v1" ) != std::string::npos );
    input.close();
    CHECK( DeleteFileA( dumpPath ) != 0 );
}


TEST_CASE( "Interaction automation rejects canonical output aliases before truncating its input" )
{
    char temporaryDirectory[MAX_PATH] = {};
    char scriptPath[MAX_PATH] = {};
    char safeReportPath[MAX_PATH] = {};
    char safeTracePath[MAX_PATH] = {};
    REQUIRE( GetTempPathA( MAX_PATH, temporaryDirectory ) != 0 );
    REQUIRE( GetTempFileNameA( temporaryDirectory, "sbi", 0, scriptPath ) != 0 );
    REQUIRE( GetTempFileNameA( temporaryDirectory, "sbr", 0, safeReportPath ) != 0 );
    REQUIRE( GetTempFileNameA( temporaryDirectory, "sbt", 0, safeTracePath ) != 0 );

    constexpr const char* scriptBytes = "{\"actions\":[]}\n";
    {
        std::ofstream script( scriptPath, std::ios::binary | std::ios::trunc );
        REQUIRE( script.is_open() );
        script << scriptBytes;
        script.close();
        REQUIRE( script.good() );
    }

    const std::filesystem::path script( scriptPath );
    const std::string canonicalAlias = ( script.parent_path() / "." / script.filename() ).string();

    SkullbonezCore::Core::SbDiagnosticStore distinctDiagnostics;
    InteractionAutomationReportWriter distinctWriter( distinctDiagnostics );
    char copiedScriptPath[MAX_PATH] = {};
    char copiedTracePath[MAX_PATH] = {};
    std::ofstream safeTrace;
    CHECK( PrepareInteractionAutomationOutputPaths( scriptPath, safeReportPath, safeTracePath, copiedScriptPath,
                                                    sizeof( copiedScriptPath ), copiedTracePath, sizeof( copiedTracePath ),
                                                    safeTrace, distinctWriter ) == nullptr );
    CHECK( distinctWriter.OutputEnabled() );
    CHECK( safeTrace.is_open() );
    safeTrace.close();

    SkullbonezCore::Core::SbDiagnosticStore traceDiagnostics;
    InteractionAutomationReportWriter traceWriter( traceDiagnostics );
    char rejectedScriptPath[MAX_PATH] = {};
    char rejectedTracePath[MAX_PATH] = {};
    std::ofstream rejectedTrace;
    const char* traceFailure = PrepareInteractionAutomationOutputPaths( scriptPath, safeReportPath, canonicalAlias.c_str(),
                                                                        rejectedScriptPath, sizeof( rejectedScriptPath ),
                                                                        rejectedTracePath, sizeof( rejectedTracePath ),
                                                                        rejectedTrace, traceWriter );
    REQUIRE( traceFailure != nullptr );
    CHECK( std::strcmp( traceFailure, "interaction trace path resolves to interaction script path" ) == 0 );
    CHECK( traceWriter.OutputEnabled() );
    CHECK_FALSE( rejectedTrace.is_open() );
    CHECK( rejectedScriptPath[0] == '\0' );
    CHECK( ReadSharedFileText( scriptPath ) == scriptBytes );

    SkullbonezCore::Core::SbDiagnosticStore reportDiagnostics;
    InteractionAutomationReportWriter reportWriter( reportDiagnostics );
    char reportRejectedScriptPath[MAX_PATH] = {};
    char reportRejectedTracePath[MAX_PATH] = {};
    std::ofstream reportRejectedTrace;
    const char* reportFailure = PrepareInteractionAutomationOutputPaths( scriptPath, canonicalAlias.c_str(), nullptr,
                                                                         reportRejectedScriptPath,
                                                                         sizeof( reportRejectedScriptPath ),
                                                                         reportRejectedTracePath,
                                                                         sizeof( reportRejectedTracePath ),
                                                                         reportRejectedTrace, reportWriter );
    REQUIRE( reportFailure != nullptr );
    CHECK( std::strcmp( reportFailure, "interaction report path resolves to interaction script path" ) == 0 );
    CHECK_FALSE( reportWriter.OutputEnabled() );
    CHECK( reportWriter.CompleteSuppressedWrite() );
    CHECK( reportWriter.Written() );
    CHECK( ReadSharedFileText( scriptPath ) == scriptBytes );

    SkullbonezCore::Core::SbDiagnosticStore longPathDiagnostics;
    InteractionAutomationReportWriter longPathWriter( longPathDiagnostics );
    const std::string overCapacityScriptPath( 260u, 'x' );
    CHECK_FALSE( longPathWriter.ConfigurePathMetadata( canonicalAlias.c_str(), overCapacityScriptPath.c_str() ) );
    CHECK_FALSE( longPathWriter.OutputEnabled() );
    CHECK( longPathWriter.CompleteSuppressedWrite() );
    CHECK( ReadSharedFileText( scriptPath ) == scriptBytes );

    CHECK( DeleteFileA( safeTracePath ) != 0 );
    CHECK( DeleteFileA( safeReportPath ) != 0 );
    CHECK( DeleteFileA( scriptPath ) != 0 );
}

TEST_CASE( "Recorded interaction playback owns free-running presentation pacing" )
{
    using SkullbonezCore::Runtime::RecordedInteractionRequiresFreeRunningPresent;

    CHECK_FALSE( RecordedInteractionRequiresFreeRunningPresent( false, false ) );
    CHECK_FALSE( RecordedInteractionRequiresFreeRunningPresent( true, false ) );
    CHECK_FALSE( RecordedInteractionRequiresFreeRunningPresent( false, true ) );
    CHECK( RecordedInteractionRequiresFreeRunningPresent( true, true ) );
}

TEST_CASE( "Interaction automation recovers malformed JSON and preserves equal-frame order" )
{
    SkullbonezCore::Core::SbDiagnosticStore automationDiagnostics;
    InteractionAutomationController controller( automationDiagnostics );
    CHECK_FALSE( AdmitInteractionAutomationScriptRoot( controller, false ) );
    CHECK( controller.status.failed );
    CHECK( std::strcmp( controller.status.failure, "interaction script root must be an object" ) == 0 );

    controller.status = {};
    CHECK_FALSE( AdmitInteractionRecordingBaselineContainers( controller, true, true, true, true, false, false ) );
    CHECK( std::strcmp( controller.status.failure, "recorded manifest baseline state is incomplete or invalid" ) == 0 );

    std::string pressKeyError;
    CHECK_FALSE( AdmitInteractionAutomationPressKeyOptions( true, true, false, pressKeyError ) );
    CHECK( pressKeyError == "pressKey requires a string key, optional boolean control, and optional integer holdFrames" );

    std::vector<RunInteractionAutomationAction> actions( 64u );

    for ( std::size_t index = 0u; index < actions.size(); ++index )
    {
        actions[index].frame = static_cast<int>( index & 1u );
        actions[index].integerValue = static_cast<int>( index );
    }

    std::vector<RunInteractionAutomationAction> retiredUnstableOrder = actions;
    std::sort( retiredUnstableOrder.begin(), retiredUnstableOrder.end(),
               []( const RunInteractionAutomationAction& lhs, const RunInteractionAutomationAction& rhs )
               { return lhs.frame < rhs.frame; } );
    bool retiredPathDiffers = false;

    for ( std::size_t index = 0u; index < retiredUnstableOrder.size(); ++index )
    {
        const int expectedAuthoredId = index < 32u ? static_cast<int>( index * 2u )
                                                   : static_cast<int>( ( index - 32u ) * 2u + 1u );
        retiredPathDiffers = retiredPathDiffers || retiredUnstableOrder[index].integerValue != expectedAuthoredId;
    }

    // The negative control proves this concrete supported-STL fixture reaches
    // the retired unstable partition path rather than passing by coincidence.
    REQUIRE( retiredPathDiffers );
    SortInteractionAutomationActions( actions );

    for ( std::size_t index = 0u; index < actions.size(); ++index )
    {
        const int expectedAuthoredId = index < 32u ? static_cast<int>( index * 2u )
                                                   : static_cast<int>( ( index - 32u ) * 2u + 1u );
        CHECK( actions[index].integerValue == expectedAuthoredId );
    }
}

TEST_CASE( "Interaction report publication is atomic and replay artifact naming is disjoint" )
{
    const std::string extensionless = "TestOutput/dotted.parent/interaction_report";
    const std::string replayNamed = "TestOutput/dotted.parent/interaction_report.skreplay";
    CHECK( InteractionAutomationReportWriter::DeriveReplayArtifactPath( extensionless ) ==
           extensionless + ".replay.skreplay" );
    CHECK( InteractionAutomationReportWriter::DeriveReplayArtifactPath( replayNamed ) == replayNamed + ".replay.skreplay" );
    CHECK( InteractionAutomationReportWriter::DeriveReplayArtifactPath( replayNamed ) != replayNamed );

    char temporaryDirectory[MAX_PATH] = {};
    char reportPath[MAX_PATH] = {};
    char scriptPath[MAX_PATH] = {};
    REQUIRE( GetTempPathA( MAX_PATH, temporaryDirectory ) != 0 );
    REQUIRE( GetTempFileNameA( temporaryDirectory, "sbr", 0, reportPath ) != 0 );
    REQUIRE( GetTempFileNameA( temporaryDirectory, "sbi", 0, scriptPath ) != 0 );
    constexpr const char* retainedBytes = "retained-report\n";
    {
        std::ofstream report( reportPath, std::ios::binary | std::ios::trunc );
        report << retainedBytes;
    }

    const SkullbonezCore::Core::AtomicTextFileTestFailure failures[] = {
        SkullbonezCore::Core::AtomicTextFileTestFailure::Write,
        SkullbonezCore::Core::AtomicTextFileTestFailure::Flush,
        SkullbonezCore::Core::AtomicTextFileTestFailure::Close,
        SkullbonezCore::Core::AtomicTextFileTestFailure::Replace,
    };

    for ( const SkullbonezCore::Core::AtomicTextFileTestFailure failure : failures )
    {
        SkullbonezCore::Core::SbDiagnosticStore reportDiagnostics;
        InteractionAutomationReportWriter writer( reportDiagnostics );
        REQUIRE( writer.ConfigurePathMetadata( reportPath, scriptPath ) );
        InteractionAutomationRunStatus status;
        SkullbonezCore::Core::SetAtomicTextFileTestFailure( failure );
        const SkullbonezCore::Core::SbResult publication = writer.PublishReportBytes( status, "replacement-report\n" );
        SkullbonezCore::Core::SetAtomicTextFileTestFailure( SkullbonezCore::Core::AtomicTextFileTestFailure::None );
        CHECK_FALSE( publication.Ok() );
        CHECK( status.failed );
        CHECK( writer.Written() );
        CHECK( ReadSharedFileText( reportPath ) == retainedBytes );
    }

    CHECK( DeleteFileA( reportPath ) != 0 );
    CHECK( DeleteFileA( scriptPath ) != 0 );
}

TEST_CASE( "Interaction startup failure publishes its report without replacing process status" )
{
    char temporaryDirectory[MAX_PATH] = {};
    char reportPath[MAX_PATH] = {};
    char scriptPath[MAX_PATH] = {};
    REQUIRE( GetTempPathA( MAX_PATH, temporaryDirectory ) != 0 );
    REQUIRE( GetTempFileNameA( temporaryDirectory, "sbr", 0, reportPath ) != 0 );
    REQUIRE( GetTempFileNameA( temporaryDirectory, "sbi", 0, scriptPath ) != 0 );

    SkullbonezCore::Core::SbDiagnosticStore automationDiagnostics;
    InteractionAutomationController controller( automationDiagnostics );
    REQUIRE( controller.reportWriter.ConfigurePathMetadata( reportPath, scriptPath ) );
    strcpy_s( controller.scriptPath, sizeof( controller.scriptPath ), scriptPath );
    controller.enabled = true;

    SkullbonezCore::Core::SbDiagnosticStore startupDiagnostics;
    const SkullbonezCore::Core::SbResult
        startupFailure = startupDiagnostics.Failure( "Runtime/Scene", "recorded scene failed semantic validation" );
    const SkullbonezCore::Core::SbResult resolved = ResolveInteractionAutomationReportForExit(
        controller, startupFailure,
        []( void* context, InteractionAutomationRunStatus& status ) -> SkullbonezCore::Core::SbResult
        {
            auto* writer = static_cast<InteractionAutomationReportWriter*>( context );
            return writer->PublishReportBytes( status, "{\"status\":\"failed\"}\n" );
        },
        &controller.reportWriter );

    CHECK_FALSE( resolved.Ok() );
    CHECK( std::strcmp( resolved.ErrorOwner(), "Runtime/Scene" ) == 0 );
    CHECK( std::strcmp( resolved.ErrorMessage(), "recorded scene failed semantic validation" ) == 0 );
    CHECK( controller.status.failed );
    CHECK( std::strcmp( controller.status.failure, "recorded scene failed semantic validation" ) == 0 );
    CHECK( controller.reportWriter.Written() );
    CHECK( ReadSharedFileText( reportPath ) == "{\"status\":\"failed\"}\n" );

    CHECK( DeleteFileA( reportPath ) != 0 );
    CHECK( DeleteFileA( scriptPath ) != 0 );
}


TEST_CASE( "SkullbonezCore::Core::EngineLog: concurrent file and event writes share one safe owner boundary" )
{
#if defined( SKULLBONEZ_TEST_ENGINE_LOG )
    constexpr const char* path = "Debug/runtime_contract_log_test.log";
    constexpr int threadCount = 6;
    constexpr int writesPerThread = 64;
    std::remove( path );

    std::vector<std::thread> threads;
    threads.reserve( threadCount );

    for ( int threadIndex = 0; threadIndex < threadCount; ++threadIndex )
    {
        threads.emplace_back(
            [threadIndex, path, writesPerThread]()
            {
                for ( int writeIndex = 0; writeIndex < writesPerThread; ++writeIndex )
                {
                    SkullbonezCore::Core::EngineLog::Get().Writef( path, "%d,%d\n", threadIndex, writeIndex );

                    if ( writeIndex % 16 == 0 )
                    {
                        SkullbonezCore::Core::EngineLog::Get().WriteEventf( "runtime_contract_log_test thread=%d write=%d",
                                                                            threadIndex, writeIndex );
                    }
                }
            } );
    }

    for ( std::thread& thread : threads )
    {
        thread.join();
    }

    SkullbonezCore::Core::EngineLog::Get().FlushAll();
    SkullbonezCore::Core::EngineLog::Get().CloseAllForTests();

    const std::string contents = ReadSharedFileText( path );
    CHECK( static_cast<int>( std::count( contents.begin(), contents.end(), '\n' ) ) == threadCount * writesPerThread );
#endif
}

TEST_CASE( "SkullbonezCore::Core::EngineLog: ResetLog truncates a retained log so a re-run does not append" )
{
#if defined( SKULLBONEZ_TEST_ENGINE_LOG )
    // Pins the fix for the doubled physics-regression CSV: EngineLog retains one
    // open handle per path, so a same-process second run appended a second run
    // behind the first. ResetLog must drop the handle so the next write starts
    // the file over. This is exactly the append-on-replay hazard that made a
    // byte-identical golden read as a divergence.
    constexpr const char* path = "Debug/engine_log_reset_test.log";
    std::remove( path );

    EngineLog& log = SkullbonezCore::Core::EngineLog::Get();

    // First run: three rows land in a freshly opened file.
    log.Writef( path, "run1,%d\n", 0 );
    log.Writef( path, "run1,%d\n", 1 );
    log.Writef( path, "run1,%d\n", 2 );

    // Reset while the handle is still retained (no close): the next write must
    // truncate rather than append behind run 1.
    log.ResetLog( path );

    log.Writef( path, "run2,%d\n", 0 );
    log.Writef( path, "run2,%d\n", 1 );

    log.FlushAll();
    log.CloseAllForTests();

    const std::string contents = ReadSharedFileText( path );
    CHECK( static_cast<int>( std::count( contents.begin(), contents.end(), '\n' ) ) == 2 );
    CHECK( contents.find( "run1" ) == std::string::npos );
    CHECK( contents.find( "run2,0" ) != std::string::npos );
    CHECK( contents.find( "run2,1" ) != std::string::npos );
#endif
}

TEST_CASE( "AmortizedTask: Reset reports idle success and in-flight refusal" )
{
    LockOrderValidator lockOrderValidator;
    WorkerPool inlinePool( lockOrderValidator );
    int completedItems = 0;
    AmortizedTask idleTask( 2, 1, [&]( int begin, int end ) { completedItems += end - begin; } );
    idleTask.SubmitTick( inlinePool );
    idleTask.SubmitTick( inlinePool );
    REQUIRE( idleTask.IsComplete() );
    CHECK( completedItems == 2 );
    CHECK( idleTask.Reset() );
    CHECK_FALSE( idleTask.IsComplete() );

    WorkerPool workerPool( lockOrderValidator );
    workerPool.Initialise( 1 );
    std::atomic<bool> started { false };
    std::atomic<bool> release { false };
    AmortizedTask inFlightTask( 1, 1,
                                [&]( int, int )
                                {
                                    started.store( true, std::memory_order_release );

                                    while ( !release.load( std::memory_order_acquire ) )
                                    {
                                        std::this_thread::yield();
                                    }
                                } );
    inFlightTask.SubmitTick( workerPool );

    while ( !started.load( std::memory_order_acquire ) )
    {
        std::this_thread::yield();
    }

    CHECK_FALSE( inFlightTask.Reset() );
    release.store( true, std::memory_order_release );

    while ( inFlightTask.IsInFlight() )
    {
        std::this_thread::yield();
    }

    workerPool.Shutdown();
}

TEST_CASE( "AmortizedTask: partial work resumes at the first unfinished item" )
{
    LockOrderValidator lockOrderValidator;
    WorkerPool inlinePool( lockOrderValidator );
    std::array<int, 3> rangeBegins = {};
    int invocationCount = 0;
    AmortizedTask task( 5, 5,
                        [&]( int begin, int end ) -> int
                        {
                            rangeBegins[static_cast<std::size_t>( invocationCount )] = begin;
                            ++invocationCount;
                            return (std::min)( 2, end - begin );
                        } );

    task.SubmitTick( inlinePool );
    CHECK( task.GetProgress() == doctest::Approx( 0.4f ) );
    task.SubmitTick( inlinePool );
    CHECK( task.GetProgress() == doctest::Approx( 0.8f ) );
    task.SubmitTick( inlinePool );

    CHECK( task.IsComplete() );
    CHECK( rangeBegins == std::array<int, 3> { 0, 2, 4 } );
}

TEST_CASE( "WorkerPool: inline and threaded self-tests preserve deterministic collection" )
{
    for ( const int threadCount : { 0, 2 } )
    {
        LockOrderValidator lockOrderValidator;
        WorkerPool pool( lockOrderValidator );
        pool.Initialise( threadCount );
        FILE* output = nullptr;
        REQUIRE( tmpfile_s( &output ) == 0 );
        REQUIRE( output != nullptr );

        CHECK( RunWorkerSystemSelfTest( pool, output ) );
        CHECK( pool.GetThreadCount() == WorkerPool::ResolveThreadCount( threadCount ) );
        pool.Shutdown();
        std::fclose( output );
    }

    CHECK( WorkerPool::MaxThreadCount() >= 1 );
    CHECK( WorkerPool::ResolveThreadCount( -1 ) >= 0 );
    CHECK_FALSE( WorkerPool::IsCurrentThreadWorker() );
    CHECK( WorkerPool::CurrentWorkerIndex() == -1 );
}

TEST_CASE( "WorkerPool: chunk ranges cover a half-open interval once and in order" )
{
    LockOrderValidator lockOrderValidator;
    WorkerPool pool( lockOrderValidator );
    pool.Initialise( 2 );
    WorkerChunkRange chunks[8] = {};

    const int chunkCount = pool.BuildChunkRangesNoAlloc( 3, 14, 1, chunks, 8 );
    REQUIRE( chunkCount >= 1 );
    CHECK( chunks[0].begin == 3 );
    CHECK( chunks[chunkCount - 1].end == 14 );

    for ( int index = 0; index < chunkCount; ++index )
    {
        CHECK( chunks[index].chunkIndex == index );
        CHECK( chunks[index].begin < chunks[index].end );

        if ( index > 0 )
        {
            CHECK( chunks[index - 1].end == chunks[index].begin );
        }
    }

    CHECK( pool.BuildChunkRangesNoAlloc( 4, 4, 1, chunks, 8 ) == 0 );
    CHECK( pool.BuildChunkRangesNoAlloc( 0, 4, 1, nullptr, 8 ) == 0 );
    CHECK( pool.BuildChunkRangesNoAlloc( 0, 4, 1, chunks, 0 ) == 0 );
    pool.Shutdown();
}

TEST_CASE( "SbResult: success and formatted failure values propagate owner and message" )
{
#if defined( _WIN64 )
    static_assert( sizeof( SbResult ) == 16 );
#endif

    const SbResult success = SbResult::Success();
    CHECK( success.Ok() );
    CHECK( std::strcmp( success.ErrorOwner(), "" ) == 0 );
    CHECK( std::strcmp( success.ErrorMessage(), "" ) == 0 );

    SbResult reassignedSuccess = diagnostics.Failure( "Discarded", "discarded failure" );
    reassignedSuccess = success;
    CHECK( reassignedSuccess.Ok() );
    CHECK( std::strcmp( reassignedSuccess.ErrorOwner(), "" ) == 0 );
    CHECK( std::strcmp( reassignedSuccess.ErrorMessage(), "" ) == 0 );

    const SbResult failure = diagnostics.Failure( "SceneParser", "invalid body %d", 17 );
    CHECK_FALSE( failure.Ok() );
    CHECK( std::strcmp( failure.ErrorOwner(), "SceneParser" ) == 0 );
    CHECK( std::strcmp( failure.ErrorMessage(), "invalid body 17" ) == 0 );

    const SbResult copiedFailure = failure;
    CHECK_FALSE( copiedFailure.Ok() );
    CHECK( std::strcmp( copiedFailure.ErrorOwner(), "SceneParser" ) == 0 );
    CHECK( std::strcmp( copiedFailure.ErrorMessage(), "invalid body 17" ) == 0 );

    const SbResult defaultFailure = diagnostics.Failure( nullptr, nullptr );
    CHECK_FALSE( defaultFailure.Ok() );
    CHECK( std::strcmp( defaultFailure.ErrorOwner(), "" ) == 0 );
    CHECK( std::strcmp( defaultFailure.ErrorMessage(), "recoverable operation failed" ) == 0 );
}


TEST_CASE( "DX12 retirement diagnostics retain real peaks and reset at device boundaries" )
{
    using SkullbonezCore::Rendering::Dx12RetirementDiagnosticState;

    Dx12RetirementDiagnosticState retirementDiagnostics;
    retirementDiagnostics.ObservePendingCount( 3u );
    retirementDiagnostics.ObservePendingCount( 2u );
    CHECK( retirementDiagnostics.PendingHighWater() == 3u );
    CHECK( retirementDiagnostics.PendingHighWater() !=
           SkullbonezCore::Rendering::Dx12DeferredReleaseOwner::MAX_PENDING_RETIREMENTS );

    retirementDiagnostics.ObserveRelease( 9u, 4u, true, 77u );
    CHECK( retirementDiagnostics.LastReleaseInputCount() == 9u );
    CHECK( retirementDiagnostics.LastReleasedCount() == 5u );
    CHECK( retirementDiagnostics.LastReleaseSurvivorCount() == 4u );
    CHECK( retirementDiagnostics.LastFrameFenceReady() );
    CHECK( retirementDiagnostics.LastObservedCompletedFence() == 77u );

    retirementDiagnostics.ObserveRelease( 4u, 4u, false, 0u );
    CHECK( retirementDiagnostics.LastReleaseInputCount() == 4u );
    CHECK( retirementDiagnostics.LastReleasedCount() == 0u );
    CHECK( retirementDiagnostics.LastReleaseSurvivorCount() == 4u );
    CHECK_FALSE( retirementDiagnostics.LastFrameFenceReady() );
    CHECK( retirementDiagnostics.LastObservedCompletedFence() == 77u );

    SkullbonezCore::Rendering::Dx12DeferredReleaseOwner retirements;
    SkullbonezCore::Rendering::Dx12DeferredReleaseOwnerTestAccess::ObservePendingCount( retirements, 2u );
    CHECK( retirements.HighWater() == 2u );
    retirements.ResetForDevice();
    CHECK( retirements.HighWater() == 0u );

    SkullbonezCore::Rendering::Dx12DeferredReleaseOwnerTestAccess::ObservePendingCount( retirements, 3u );
    CHECK( retirements.HighWater() == 3u );
    retirements.ResetAfterShutdown();
    CHECK( retirements.HighWater() == 0u );
}


TEST_CASE( "IH4 rendering and world lifecycle misuse terminates before stale access" )
{
    ExpectFatalCase( "render-preview-capacity",
                     { "FATAL[Runtime/Render/RenderTargetPreviewSnapshot]", "count=12 capacity=12" } );
    ExpectFatalCase( "dx12-geometry-before-init", { "FATAL[Dx12GeometryOwner]", "operation=BeforeInitProbe", "active=0" } );
    ExpectFatalCase( "dx12-geometry-after-shutdown",
                     { "FATAL[Dx12GeometryOwner]", "operation=AfterShutdownProbe", "active=0" } );
    ExpectFatalCase( "dx12-texture-before-init", { "FATAL[Dx12TextureOwner]", "operation=BeforeInitProbe", "active=0" } );
    ExpectFatalCase( "dx12-texture-after-shutdown",
                     { "FATAL[Dx12TextureOwner]", "operation=AfterShutdownProbe", "active=0" } );
    ExpectFatalCase( "primitive-scope-moved", { "FATAL[Rendering/PrimitiveBatchScope]", "active=0", "requested=visible" } );
    ExpectFatalCase( "primitive-scope-inactive",
                     { "FATAL[Rendering/PrimitiveBatchScope]", "active=0", "requested=visible" } );
    ExpectFatalCase( "primitive-visible-as-shadow",
                     { "FATAL[Rendering/PrimitiveBatchScope]", "kind=0", "requested=shadow" } );
    ExpectFatalCase( "primitive-shadow-as-visible",
                     { "FATAL[Rendering/PrimitiveBatchScope]", "kind=3", "requested=visible" } );
    ExpectFatalCase( "primitive-resource-owner-mismatch",
                     { "FATAL[Rendering/PrimitiveBatchRenderer]", "resources=1 textures=0 geometry=1" } );
    ExpectFatalCase( "skybox-before-init",
                     { "FATAL[World/SkyBox]", "operation=BeforeInitProbe", "textures=0", "resources=0" } );
    ExpectFatalCase( "skybox-after-release",
                     { "FATAL[World/SkyBox]", "operation=AfterReleaseProbe", "textures=0", "resources=0" } );
    ExpectFatalCase( "world-water-before-init",
                     { "FATAL[World/WorldEnvironment]", "operation=BeforeInitProbe", "assets=0 resources=0" } );
    ExpectFatalCase( "terrain-render-resources-before-init",
                     { "FATAL[World/Terrain]", "operation=BeforeInitProbe", "assets=0 resources=0" } );
    ExpectFatalCase( "terrain-missing-clip-plane", { "FATAL[World/Terrain]", "Terrain color pass requires a clip plane" } );
}


#if !defined( _DEBUG )
TEST_CASE( "IH2 and IH3 old assert-only implementations are proven non-Debug missed failures" )
{
    ExpectCleanControlCase( LEGACY_IH23_TORNADO_MISSED_FAILURE_CASE,
                            { "IH2/IH3 legacy assert-only tornado lifecycle path returned in a non-Debug child" } );
    ExpectCleanControlCase( LEGACY_IH23_COLLIDER_MISSED_FAILURE_CASE,
                            { "IH2/IH3 legacy assert-only collider shape/index path returned in a non-Debug child" } );
    ExpectCleanControlCase( LEGACY_IH23_DISJOINT_SET_MISSED_FAILURE_CASE,
                            { "IH2/IH3 legacy assert-only disjoint-set scratch path returned in a non-Debug child" } );
    ExpectCleanControlCase( LEGACY_IH23_PHYSICS_BODY_MISSED_FAILURE_CASE,
                            { "IH2/IH3 legacy assert-only physics-body index/span path returned in a non-Debug child" } );
}


TEST_CASE( "IH4 old assert-only implementation is a proven non-Debug missed failure" )
{
    ExpectCleanControlCase( LEGACY_IH4_DX12_MISSED_FAILURE_CASE,
                            { "IH4 legacy assert-only dx12 lifecycle path returned in a non-Debug child" } );
    ExpectCleanControlCase( LEGACY_IH4_PRIMITIVE_MISSED_FAILURE_CASE,
                            { "IH4 legacy assert-only primitive scope path returned in a non-Debug child" } );
    ExpectCleanControlCase( LEGACY_IH4_WORLD_MISSED_FAILURE_CASE,
                            { "IH4 legacy assert-only world rebuild path returned in a non-Debug child" } );
    ExpectCleanControlCase( LEGACY_IH4_PREVIEW_MISSED_FAILURE_CASE,
                            { "IH4 legacy assert-only preview capacity path returned in a non-Debug child" } );
}
#endif


TEST_CASE( "IH5 render-pass lifecycle misuse terminates before stale access" )
{
    ExpectFatalCase( "run-renderer-before-init", { "FATAL[Runtime/Run]", "BeforeInitProbe requires the live renderer owner",
                                                   "renderer=0000000000000000" } );
    ExpectFatalCase( "run-renderer-after-shutdown",
                     { "FATAL[Runtime/Run]", "AfterShutdownProbe requires the live renderer owner",
                       "renderer=0000000000000000" } );
    ExpectFatalCase( "sky-pass-before-init",
                     { "FATAL[Runtime/Render/SkyPass]", "BeforeInitProbe requires the live world-view sky owner" } );
    ExpectFatalCase( "sky-pass-after-release",
                     { "FATAL[Runtime/Render/SkyPass]", "AfterReleaseProbe requires the live world-view sky owner" } );
    ExpectFatalCase( "ui-profiler-before-init",
                     { "FATAL[Runtime/Render/UiTextPass]", "BeforeInitProbe requires an active startup-bound profiler",
                       "active=0" } );
    ExpectFatalCase( "ui-profiler-after-release",
                     { "FATAL[Runtime/Render/UiTextPass]", "AfterReleaseProbe requires an active startup-bound profiler",
                       "active=0" } );
}


#if !defined( _DEBUG )
TEST_CASE( "IH5 old render-pass assert-only implementation is a proven non-Debug missed failure" )
{
    ExpectCleanControlCase( LEGACY_IH5_RUN_RENDERER_MISSED_FAILURE_CASE,
                            { "IH5 legacy assert-only Run renderer path returned in a non-Debug child" } );
    ExpectCleanControlCase( LEGACY_IH5_SKY_MISSED_FAILURE_CASE,
                            { "IH5 legacy assert-only sky pass path returned in a non-Debug child" } );
    ExpectCleanControlCase( LEGACY_IH5_UI_PROFILER_MISSED_FAILURE_CASE,
                            { "IH5 legacy assert-only UI profiler path returned in a non-Debug child" } );
}
#endif


TEST_CASE( "DX12 retirement exhaustion reports truthful queue and fence diagnostics" )
{
    ExpectFatalCase( "dx12-retirement-capacity",
                     { "FATAL[Dx12DeferredReleaseOwner]", "phase=quarantine", "capacity=512 count=512 high_water=512",
                       "last_release_input=0 last_released=0 last_survivors=0", "fence_ready=0 last_completed_fence=0" } );
    ExpectFatalCase( "dx12-retirement-release-snapshot",
                     { "FATAL[Dx12DeferredReleaseOwner]", "phase=quarantine", "capacity=512 count=512 high_water=512",
                       "last_release_input=9 last_released=5 last_survivors=4", "fence_ready=1 last_completed_fence=77" } );
    ExpectFatalCase( "dx12-retirement-reset-live-device",
                     { "FATAL[Dx12DeferredReleaseOwner]", "phase=device_reset", "count=1" } );
    ExpectFatalCase( "dx12-retirement-reset-live-shutdown",
                     { "FATAL[Dx12DeferredReleaseOwner]", "phase=shutdown_reset", "count=1" } );
}


TEST_CASE( "SbDiagnosticStore bound capacity and lease misuse terminate in child probes" )
{
    ExpectFatalCase( "sb-diagnostic-capacity", { "FATAL[Core/SbDiagnosticStore]", "all 256 diagnostic slots are leased" } );
    ExpectFatalCase( "sb-diagnostic-double-release",
                     { "FATAL[Core/SbDiagnosticStore]", "release used a stale or already released diagnostic token" } );

    ExpectFatalCase( "sb-diagnostic-owner-overflow",
                     { "FATAL[Core/SbDiagnosticStore]", "diagnostic owner exceeds 95-byte bound" } );

    ExpectFatalCase( "sb-diagnostic-store-destroyed-with-active-lease",
                     { "FATAL[Core/SbDiagnosticStore]", "diagnostic store destroyed with 1 active entries" } );

    ExpectFatalCase( "sb-diagnostic-lease-overflow",
                     { "FATAL[Core/SbDiagnosticStore]", "diagnostic lease count overflowed" } );

    ExpectFatalCase( "sb-diagnostic-generation-overflow",
                     { "FATAL[Core/SbDiagnosticStore]", "diagnostic generation exhausted for slot 0" } );

    ExpectFatalCase( "sb-diagnostic-lock-reentry",
                     { "FATAL[Core/SbDiagnosticStore]", "diagnostic store lock re-entered by its owning thread" } );
}

TEST_CASE( "Runtime contracts: invalid broadphase and task lifetimes terminate in child probes" )
{
    ExpectFatalCase( "physics-pipeline-batch-full-mode",
                     { "FATAL[Physics/PhysicsStepDiagnostics]",
                       "Count-only pipeline event batches cannot be recorded while full payload retention is active" } );

    ExpectFatalCase( "physics-fixed-list-runtime-capacity",
                     { "FATAL: PhysicsFixedList capacity exceeded", "owner=fatal.physics-fixed-list.runtime", "requested=2",
                       "runtime_capacity=1", "compile_capacity=4", "ceiling=runtime_reservation" } );

    ExpectFatalCase( "physics-fixed-list-compile-capacity",
                     { "FATAL: PhysicsFixedList capacity exceeded", "owner=fatal.physics-fixed-list.compile", "requested=3",
                       "runtime_capacity=0", "compile_capacity=2", "ceiling=compile_time_ceiling" } );

    ExpectFatalCase( "physics-fixed-list-phase",
                     { "FATAL: PhysicsFixedList reserve denied", "owner=fatal.physics-fixed-list.phase", "requested=1",
                       "runtime_capacity=0", "compile_capacity=2", "phase=startup" } );

    ExpectFatalCase( "collider-rebind-shape-index",
                     { "FATAL[Physics/ColliderStore]", "operation=rebind", "kind=0", "index=3", "shape_count=3" } );
    ExpectFatalCase( "collider-remove-shape-index",
                     { "FATAL[Physics/ColliderStore]", "operation=remove", "kind=1", "index=1", "shape_count=1" } );
    ExpectFatalCase( "collider-hull-shape-parity",
                     { "FATAL[Physics/ColliderStore]", "operation=remove", "kind=2", "shape_count=1", "identity_count=0" } );
    ExpectFatalCase( "disjoint-set-scratch-capacity",
                     { "FATAL[Physics/DisjointSet]", "count=2", "parent_rows=1", "rank_rows=1" } );
    ExpectFatalCase( "physics-body-hot-index",
                     { "FATAL[Physics/PhysicsBodyStore]", "operation=read-hot-state", "index=0", "count=0" } );
    ExpectFatalCase( "physics-body-sleep-destination", { "FATAL[Physics/PhysicsBodyStore]", "provided=0", "required=1" } );
    ExpectFatalCase( "contact-side-effect-collision-visual", { "FATAL[Physics/PhysicsContactSolverStage]",
                                                               "lane=collisionVisualBodies", "required=4", "capacity=3" } );
    ExpectFatalCase( "contact-side-effect-fixed-contact",
                     { "FATAL[Physics/PhysicsContactSolverStage]", "lane=fixedContactBodies", "required=2", "capacity=1" } );
    ExpectFatalCase( "contact-side-effect-release-wake",
                     { "FATAL[Physics/PhysicsContactSolverStage]", "lane=releaseWakeBodies", "required=2", "capacity=1" } );
    ExpectFatalCase( "contact-side-effect-fixed-tree",
                     { "FATAL[Physics/PhysicsContactSolverStage]", "lane=fixedTreeReleases", "required=2", "capacity=1" } );
    ExpectFatalCase( "contact-side-effect-pipeline",
                     { "FATAL[Physics/PhysicsContactSolverStage]", "lane=pipelineRecords", "required=2", "capacity=1" } );

    ExpectFatalCase( "physics-prediction-seed-wrong-replay-owner",
                     { "FATAL[Physics/ReplayPredictionClone]",
                       "PhysicsEngine seed requires the canonical ReplayPrediction owner scope",
                       "owner_name=replay_solver_snapshot", "required_owner=replay_prediction_working_set" } );

    ExpectFatalCase( "physics-prediction-seed-missing-scope",
                     { "FATAL[Physics/ReplayPredictionClone]",
                       "PhysicsEngine seed requires the canonical ReplayPrediction owner scope", "phase=startup", "owner=0",
                       "owner_name=<unregistered>", "required_owner=replay_prediction_working_set" } );

    ExpectFatalCase( "physics-prediction-seed-scene-load",
                     { "FATAL[Physics/ReplayPredictionClone]",
                       "PhysicsEngine seed requires the canonical ReplayPrediction owner scope", "phase=scene_load",
                       "owner=0", "owner_name=<unregistered>", "required_owner=replay_prediction_working_set" } );

    ExpectFatalCase( "physics-prediction-seed-missing-owner",
                     { "FATAL[Physics/ReplayPredictionClone]",
                       "PhysicsEngine seed requires the canonical ReplayPrediction owner scope", "phase=replay", "owner=0",
                       "owner_name=<unregistered>", "required_owner=replay_prediction_working_set" } );

    ExpectFatalCase( "terrain-locate-cell-range",
                     { "FATAL[Terrain]", "Terrain polygon cell out of range", "worldXCell=3", "quadsPerSide=3" } );

    ExpectFatalCase( "terrain-locate-nonfinite", { "FATAL[Terrain]", "Terrain polygon query is not finite", "x=nan" } );

    ExpectFatalCase( "terrain-locate-unrepresentable", { "FATAL[Terrain]", "Terrain polygon cell is not representable" } );

    ExpectFatalCase( "terrain-locate-invalid-scale",
                     { "FATAL[Terrain]", "Terrain polygon query is not finite", "scaledStepSize=nan" } );

    ExpectFatalCase( "spatial-grid-nan",
                     { "FATAL[Physics/SpatialGrid]", "body=7", "min=(nan,-1,-1)", "max_world_coordinate=100000" } );

    ExpectFatalCase( "spatial-grid-extent",
                     { "FATAL[Physics/SpatialGrid]", "body=11", "max=(100002,1,1)", "max_world_coordinate=100000" } );

    ExpectFatalCase( "spatial-grid-zero-cell",
                     { "FATAL[Physics/SpatialGrid]", "cell size invalid", "value=0", "minimum=0.5" } );

    ExpectFatalCase( "spatial-grid-nan-cell",
                     { "FATAL[Physics/SpatialGrid]", "cell size invalid", "value=nan", "minimum=0.5" } );

    ExpectFatalCase( "spatial-grid-tiny-cell",
                     { "FATAL[Physics/SpatialGrid]", "cell size invalid", "value=0.25", "minimum=0.5" } );

    ExpectFatalCase( "spatial-grid-reserve-phase",
                     { "FATAL: PhysicsFixedList reserve denied", "owner=SpatialGrid.entries", "requested=1032",
                       "runtime_capacity=0", "compile_capacity=131076", "phase=startup" } );

    ExpectFatalCase( "spatial-grid-entry-capacity",
                     { "FATAL: PhysicsFixedList capacity exceeded", "owner=SpatialGrid.entries", "requested=1033",
                       "runtime_capacity=1032", "compile_capacity=131076", "high_water=1032", "phase=physics" } );

    ExpectCleanControlCase( "spatial-grid-overlay-entry-capacity", { "spatial-grid-overlay-fallback-complete" } );

    ExpectFatalCase( "spatial-grid-bucket-capacity", { "FATAL[Physics/SpatialGrid]", "bucket capacity exceeded",
                                                       "capacity=8192", "active=8192", "phase=steady_gameplay" } );

    ExpectFatalCase( "sleep-support-edge-reserved-capacity",
                     { "FATAL[Physics/SleepSupportEdges]", "Sleep support edge capacity exceeded", "requested=3",
                       "capacity=32768", "reserved_capacity=2", "high_water=2", "phase=steady_gameplay" } );

    ExpectFatalCase( "sleep-support-edge-capacity",
                     { "FATAL[Physics/SleepSupportEdges]", "Sleep support edge capacity exceeded", "requested=32769",
                       "capacity=32768", "high_water=32768", "phase=steady_gameplay" } );

    ExpectFatalCase( "amortized-task-in-flight-destroy",
                     { "FATAL[Core/AmortizedTask]", "Destroying AmortizedTask while worker chunk is in flight" } );

    ExpectFatalCase( "worker-fatal-log", { "FATAL[Tests/WorkerFatalProbe]", "worker-thread fatal logging probe" } );
    ExpectFatalCase( "texture-slot-capacity",
                     { "FATAL[TextureCollection]", "Texture slot capacity exhausted", "capacity=8" } );
    ExpectFatalCase( "tornado-visual-unprepared-frame",
                     { "FATAL[Gameplay/TornadoVisualPass]", "Tornado visual frame is not prepared",
                       "operation=UnpreparedProbe", "field=0", "system=0" } );
    ExpectFatalCase( "tornado-visual-released-frame",
                     { "FATAL[Gameplay/TornadoVisualPass]", "Tornado visual frame is not prepared",
                       "operation=ReleasedProbe", "field=0", "system=0" } );
    ExpectFatalCase( "replay-restore-pending-timeline-complete",
                     { "FATAL[Runtime/ReplayRestoreTransaction]",
                       "Restore completion reached without satisfying branch timeline state", "required=1", "applied=0" } );

    ExpectFatalCase( "replay-restore-unproved-rollback", { "FATAL[Runtime/ReplayRestoreTransaction]",
                                                           "Rollback completed without verified live-backup application",
                                                           "mutated=1", "backup=1", "applied=0" } );

    ExpectFatalCase( "scene-capacity-hard-ceiling", { "FATAL[Physics/SceneCapacity]", "owner=Physics/PhysicsEngine",
                                                      "requested_bodies=9000", "ceiling=8192" } );

    ExpectFatalCase( "point-joint-scene-capacity", { "FATAL[Physics/PointJoint]", "owner=Physics/PhysicsWorld",
                                                     "requested=9", "capacity=8", "retained_capacity=12" } );

#if defined( _DEBUG ) || defined( SKULLBONEZ_PROFILE_ENABLED ) || defined( SKULLBONEZ_TEST_PROFILE_ALLOCATION_FATAL )
    ExpectFatalCase( "allocation-foreign-page-boundary",
                     { "FATAL[Runtime/Allocation]", "unprovable foreign pointer delete", "phase=diagnostics", "owner=0",
                       "header=unreadable", "foreign_free_count=1" } );

    ExpectFatalCase( "allocation-foreign-shaped-header",
                     { "FATAL[Runtime/Allocation]", "unprovable foreign pointer delete", "phase=diagnostics", "owner=0",
                       "header=bad_provenance", "foreign_free_count=1" } );
#else
    ExpectCleanChildCase( "allocation-foreign-crt-release",
                          { "[allocation-guard] FOREIGN_FREE", "phase=diagnostics", "owner=0", "header=bad_magic",
                            "foreign_free_count=1", "mode=measure", "foreign_frees=1", "VIOLATION:" } );
#endif
    ExpectFatalCase( "allocation-size-overflow", { "FATAL[Runtime/Allocation]", "global operator new failed",
                                                   "reason=size_arithmetic_overflow", "size=18446744073709551615" } );
}


TEST_CASE( "Replay restore applies the recorded camera up vector with the full pose" )
{
    using SkullbonezCore::Environment::CameraCollection;
    using SkullbonezCore::Math::Vector::Vector3;
    using SkullbonezCore::Runtime::ReplayCameraSample;
    using SkullbonezCore::Runtime::ReplayRestoreOperations;

    CameraCollection cameras;
    cameras.AddCamera( Vector3( 4.0f, 5.0f, 6.0f ), Vector3( 0.0f, 0.0f, 0.0f ), Vector3( 0.0f, 1.0f, 0.0f ), 0x5A11u );
    cameras.SetCamera();

    cameras.TweenPrimaryToPose( Vector3( 40.0f, 50.0f, 60.0f ), Vector3( 1.0f, 2.0f, 3.0f ), Vector3( 1.0f, 0.0f, 0.0f ) );
    cameras.SetTweenProgress( 0.5f );
    cameras.SetCamera();

    ReplayCameraSample recorded;
    recorded.eye = Vector3( 10.0f, 20.0f, 30.0f );
    recorded.view = Vector3( -3.0f, 7.0f, 11.0f );
    recorded.up = Vector3( 0.6f, 0.0f, 0.8f );
    CHECK( cameras.GetRenderCameraTranslation() != recorded.eye );
    ReplayRestoreOperations::ApplyCameraSample( cameras, recorded );

    CHECK( cameras.GetCameraTranslation() == recorded.eye );
    CHECK( cameras.GetCameraView() == recorded.view );
    CHECK( cameras.GetCameraUp() == recorded.up );
    CHECK( cameras.GetRenderCameraTranslation() == recorded.eye );
    CHECK( cameras.GetRenderCameraView() == recorded.view );
    CHECK( cameras.GetRenderCameraUp() == recorded.up );
}

TEST_CASE( "Physics invariant guards admit exact scratch and consequence capacities" )
{
    std::array<int, 2> parent {};
    std::array<uint8_t, 2> rank {};
    SkullbonezCore::Physics::DisjointSet disjointSet( std::span<int>( parent ), std::span<uint8_t>( rank ), 2 );
    disjointSet.Reset();
    CHECK( ( parent == std::array<int, 2> { 0, 1 } ) );
    CHECK( ( rank == std::array<uint8_t, 2> { 0u, 0u } ) );

    SkullbonezCore::Physics::PhysicsContactSolverStage stage;
    SkullbonezCore::Physics::PhysicsContactSolverStageTestAccess::ReserveAndPrepare( stage, 4u, 2u, 2u, 2u, 2u );
    CHECK( stage.GetSideEffects().collisionVisualBodies.empty() );
    CHECK( stage.GetSideEffects().fixedContactBodies.empty() );
    CHECK( stage.GetSideEffects().releaseWakeBodies.empty() );
    CHECK( stage.GetSideEffects().fixedTreeReleases.empty() );
    CHECK( stage.GetSideEffects().pipelineRecords.empty() );
}

TEST_CASE( "Persistent contact solve transaction enforces every phase edge through fatal invariant" )
{
    using SkullbonezCore::Physics::PersistentContactSolvePhaseCursor;
    using SkullbonezCore::Physics::PersistentContactSolveTransaction;
    using SkullbonezCore::Physics::PersistentContactSolveTransactionTestAccess;
    using Phase = PersistentContactSolvePhaseCursor::Phase;
    constexpr std::array phases { Phase::Idle,
                                  Phase::EntryPolicySetup,
                                  Phase::BodySetup,
                                  Phase::BuildManifolds,
                                  Phase::TerrainRows,
                                  Phase::Precompute,
                                  Phase::SolveRows,
                                  Phase::PointSupportInstability,
                                  Phase::TerrainRestPolicy,
                                  Phase::WriteBack,
                                  Phase::DebugContacts,
                                  Phase::PositionCorrection,
                                  Phase::CacheStore,
                                  Phase::FixedContactRelease,
                                  Phase::Complete,
                                  Phase::Count };
    constexpr std::size_t entryIndex = 1u;
    constexpr std::size_t terrainRowsIndex = 4u;
    constexpr std::size_t completeIndex = phases.size() - 2u;

    for ( std::size_t fromIndex = 0u; fromIndex < phases.size(); ++fromIndex )
    {
        for ( std::size_t toIndex = 0u; toIndex < phases.size(); ++toIndex )
        {
            const bool adjacent = fromIndex < completeIndex && toIndex == fromIndex + 1u;
            const bool emptyInput = fromIndex == entryIndex && toIndex == completeIndex;
            const bool emptyRows = fromIndex == terrainRowsIndex && toIndex == completeIndex;
            const bool expected = adjacent || emptyInput || emptyRows;
            CHECK( PersistentContactSolvePhaseCursor::IsLegalTransition( phases[fromIndex], phases[toIndex] ) == expected );

            // Count is a sentinel and cannot become the cursor's current state.
            if ( fromIndex == phases.size() - 1u || expected )
            {
                continue;
            }

            char caseName[96] = {};
            std::snprintf( caseName, sizeof( caseName ), "contact-solve-phase-%zu-%zu", fromIndex, toIndex );
            ExpectFatalCase( caseName, { "FATAL[Physics/PersistentContactSolveTransaction]", "Illegal phase transition",
                                         "operation=ExhaustiveFatalProbe" } );
        }
    }

    PersistentContactSolveTransaction transaction;
    CHECK( transaction.Phase() == Phase::Idle );

    for ( std::size_t phaseIndex = 1u; phaseIndex <= completeIndex; ++phaseIndex )
    {
        PersistentContactSolveTransactionTestAccess::Advance( transaction, phases[phaseIndex] );
    }

    CHECK( transaction.Phase() == Phase::Complete );
    static_assert( !std::is_copy_constructible_v<PersistentContactSolveTransaction> );
    static_assert( !std::is_copy_assignable_v<PersistentContactSolveTransaction> );
}

TEST_CASE( "UI stress policy publishes deterministic bounded commands without borrowed references" )
{
    using namespace SkullbonezCore::Runtime;

    UIStressPolicyOwner disabled;
    const UIStressFramePlan inactive = disabled.PlanFrame( 800, 600, 11 );
    CHECK_FALSE( inactive.active );
    CHECK( inactive.commandCount == 0u );

    UIStressPolicyOwner first;
    UIStressPolicyOwner replay;
    first.Configure( true, 21u, 7 );
    replay.Configure( true, 21u, 7 );

    const UIStressFramePlan firstFrame = first.PlanFrame( 800, 600, 11 );
    const UIStressFramePlan replayFrame = replay.PlanFrame( 800, 600, 11 );
    CHECK( firstFrame.active );
    CHECK( firstFrame.mouseX == 48 );
    CHECK( firstFrame.mouseY == 375 );
    CHECK( firstFrame.commandCount == 0u );
    CHECK( first.RandomState() == 549390540u );
    CHECK( replay.RandomState() == first.RandomState() );
    CHECK( replayFrame.mouseX == firstFrame.mouseX );
    CHECK( replayFrame.mouseY == firstFrame.mouseY );

    const UIStressFramePlan secondFrame = first.PlanFrame( 800, 600, 11 );
    REQUIRE( secondFrame.commandCount == 2u );
    CHECK( secondFrame.mouseX == 91 );
    CHECK( secondFrame.mouseY == 222 );
    CHECK( secondFrame.commands[0].kind == UIStressCommandKind::SetActiveTab );
    CHECK( secondFrame.commands[0].intValue == 1 );
    CHECK( secondFrame.commands[1].kind == UIStressCommandKind::SetScrollY );
    CHECK( secondFrame.commands[1].floatValue == doctest::Approx( 456.8734f ) );
    CHECK( first.RandomState() == 1975722012u );
    CHECK( first.FramesRun() == 2 );
}

TEST_CASE( "Operator UI projection owns hierarchy row order and selected identity" )
{
    using namespace SkullbonezCore::Runtime;

    SkullbonezCore::UI::OperatorEditorFrameView view;
    OperatorUiHierarchyFacts hierarchy;
    hierarchy.totalRowCount = 2u;
    hierarchy.selectedRow = 1;
    hierarchy.selectedObjectType = 3;
    hierarchy.objectTypeCount = 7;
    hierarchy.sceneDirty = true;
    BeginOperatorEditorHierarchy( view, hierarchy );

    // Call order is intentionally reversed: source identity, not sampling
    // sequence, owns the row slot exposed to both operator surfaces.
    AppendOperatorEditorHierarchyRow( view, hierarchy, { "second", 202u, 200u, 2, true, false, true }, 1u );
    AppendOperatorEditorHierarchyRow( view, hierarchy, { "first", 101u, 100u, 1, false, true, false }, 0u );

    REQUIRE( view.hierarchy.rowCount == 2u );
    CHECK( std::strcmp( view.hierarchy.rows[0].displayName, "first" ) == 0 );
    CHECK( std::strcmp( view.hierarchy.rows[1].displayName, "second" ) == 0 );
    CHECK_FALSE( view.hierarchy.rows[0].selected );
    CHECK( view.hierarchy.rows[1].selected );
    CHECK( view.hierarchy.selectedSceneObjectId == 202u );
    CHECK( view.scene.dirty );
    CHECK( view.assets.selectedObjectType == 3 );
    static_assert( std::is_trivially_copyable_v<OperatorUiHierarchyFacts> );
    static_assert( std::is_trivially_copyable_v<OperatorUiHierarchyEntityFacts> );
}

TEST_CASE( "Operator UI projection maps detached render-target facts without backend handles" )
{
    using namespace SkullbonezCore::Runtime;

    OperatorUiRenderTargetListFacts facts;
    CHECK( facts.Append( "color", 1280, 720, true, false, true ) );
    CHECK( facts.Append( "depth", 640, 360, false, true, false ) );
    SkullbonezCore::UI::InGameUIFrameData frame;
    ProjectOperatorUiRenderTargets( frame, facts );

    REQUIRE( frame.renderTargets.count == 2 );
    CHECK( std::strcmp( frame.renderTargets.previews[0].label, "color" ) == 0 );
    CHECK( frame.renderTargets.previews[0].available );
    CHECK( frame.renderTargets.previews[0].hdr );
    CHECK( std::strcmp( frame.renderTargets.previews[1].label, "depth" ) == 0 );
    CHECK_FALSE( frame.renderTargets.previews[1].available );
    CHECK( frame.renderTargets.previews[1].depth );

    for ( int index = 2; index < SkullbonezCore::UI::UI_RENDER_TARGET_PREVIEW_MAX; ++index )
    {
        CHECK( facts.Append( "extra", index, index + 1, true, false, false ) );
    }

    CHECK_FALSE( facts.Append( "overflow", 1, 1, true, false, false ) );
    ProjectOperatorUiRenderTargets( frame, facts );
    CHECK( frame.renderTargets.count == SkullbonezCore::UI::UI_RENDER_TARGET_PREVIEW_MAX );

    using AppendSignature = bool ( OperatorUiRenderTargetListFacts::* )( const char*, int, int, bool, bool, bool );
    static_assert( std::is_same_v<decltype( &OperatorUiRenderTargetListFacts::Append ), AppendSignature> );
}

#ifdef _DEBUG
void CheckReplayStartupTransitionMatrix()
{
    using Continuation = SkullbonezCore::Runtime::ReplayStartupProbeContinuation;
    using Phase = Continuation::Phase;
    constexpr std::array phases { Phase::Idle,     Phase::Running, Phase::AwaitingApplication, Phase::ApplicationApplied,
                                  Phase::Complete, Phase::Failed };

    for ( Phase from : phases )
    {
        for ( Phase to : phases )
        {
            const bool expected = ( from == Phase::Idle && to == Phase::Running ) ||
                                  ( from == Phase::Running && to == Phase::AwaitingApplication ) ||
                                  ( from == Phase::AwaitingApplication && to == Phase::ApplicationApplied ) ||
                                  ( from == Phase::AwaitingApplication && to == Phase::Failed ) ||
                                  ( from == Phase::ApplicationApplied && to == Phase::Running ) ||
                                  ( from == Phase::Running && ( to == Phase::Complete || to == Phase::Failed ) );
            CHECK( Continuation::IsLegalTransition( from, to ) == expected );
        }
    }
}

void CheckReplayStartupApplicationMatrix()
{
    using Continuation = SkullbonezCore::Runtime::ReplayStartupProbeContinuation;
    using PendingAction = Continuation::PendingAction;
    using Phase = Continuation::Phase;
    constexpr std::array phases { Phase::Idle,     Phase::Running, Phase::AwaitingApplication, Phase::ApplicationApplied,
                                  Phase::Complete, Phase::Failed };
    constexpr std::array actions { PendingAction::None, PendingAction::ActivateLoadedPresentation,
                                   PendingAction::ApplyRestoredBranchTimeline };

    constexpr std::size_t restoreStates = 2u;
    constexpr std::size_t caseCount = phases.size() * actions.size() * restoreStates;
    for ( std::size_t caseIndex = 0u; caseIndex < caseCount; ++caseIndex )
    {
        const Phase phase = phases[caseIndex / ( actions.size() * restoreStates )];
        const PendingAction action = actions[( caseIndex / restoreStates ) % actions.size()];
        const bool hasRestore = ( caseIndex % restoreStates ) != 0u;
        const bool expected = phase == Phase::AwaitingApplication
                                  ? action != PendingAction::None &&
                                        ( action != PendingAction::ApplyRestoredBranchTimeline || hasRestore )
                                  : action == PendingAction::None;
        CHECK( Continuation::IsApplicationStateCoherent( phase, action, hasRestore ) == expected );
    }
}

TEST_CASE( "Replay startup probe continuation admits only serviced finite-state edges" )
{
    using Continuation = SkullbonezCore::Runtime::ReplayStartupProbeContinuation;
    using ContinuationTestAccess = SkullbonezCore::Runtime::ReplayStartupProbeContinuationTestAccess;
    using PendingAction = Continuation::PendingAction;
    using Phase = Continuation::Phase;
    CheckReplayStartupTransitionMatrix();
    CheckReplayStartupApplicationMatrix();

    ExpectFatalCase( "replay-startup-illegal-transition",
                     { "FATAL[Runtime/ReplayStartupProbeContinuation]", "Illegal startup-probe continuation transition",
                       "operation=FatalContractProbe" } );
    ExpectFatalCase( "replay-startup-restore-action-without-transaction",
                     { "FATAL[Runtime/ReplayStartupProbeContinuation]", "application state is incoherent",
                       "operation=FatalContractProbe", "restore=0" } );

    Continuation rejectedActivation( 0.0 );
    ContinuationTestAccess::SeedPendingPresentationActivation( rejectedActivation );
    ContinuationTestAccess::RejectPendingApplication( rejectedActivation );
    CHECK( rejectedActivation.CurrentPhase() == Phase::Failed );
    CHECK( rejectedActivation.ApplicationAction() == PendingAction::None );
    CHECK( rejectedActivation.IsTerminal() );

    static_assert( !std::is_copy_constructible_v<Continuation> );
    static_assert( !std::is_move_constructible_v<Continuation> );
}
#endif

TEST_CASE( "Operator command transaction enforces every phase edge through fatal invariant" )
{
    using SkullbonezCore::Runtime::OperatorCommandPhaseCursor;
    using SkullbonezCore::Runtime::OperatorCommandTransaction;
    using SkullbonezCore::Runtime::OperatorCommandTransactionTestAccess;
    using Phase = OperatorCommandPhaseCursor::Phase;
    constexpr std::array phases { Phase::Idle,
                                  Phase::DeviceAndMode,
                                  Phase::PhysicsControl,
                                  Phase::RuntimePresentation,
                                  Phase::SimulationPolicy,
                                  Phase::PhysicsMaterial,
                                  Phase::WorldPolicy,
                                  Phase::CinematicPolicy,
                                  Phase::Complete,
                                  Phase::Count };

    for ( std::size_t fromIndex = 0u; fromIndex < phases.size(); ++fromIndex )
    {
        for ( std::size_t toIndex = 0u; toIndex < phases.size(); ++toIndex )
        {
            const bool expected = fromIndex < phases.size() - 2u && toIndex == fromIndex + 1u;
            CHECK( OperatorCommandPhaseCursor::IsLegalTransition( phases[fromIndex], phases[toIndex] ) == expected );

            // Count is a sentinel and cannot become the cursor's current state.
            if ( fromIndex == phases.size() - 1u || expected )
            {
                continue;
            }

            char caseName[96] = {};
            std::snprintf( caseName, sizeof( caseName ), "operator-command-phase-%zu-%zu", fromIndex, toIndex );
            ExpectFatalCase( caseName, { "FATAL[Runtime/OperatorCommandTransaction]", "Illegal phase transition",
                                         "operation=ExhaustiveFatalProbe" } );
        }
    }

    SkullbonezCore::UI::InGameUICommands commands;
    OperatorCommandTransaction transaction( commands );
    CHECK( transaction.Phase() == Phase::Idle );

    for ( std::size_t phaseIndex = 1u; phaseIndex < phases.size() - 1u; ++phaseIndex )
    {
        OperatorCommandTransactionTestAccess::Advance( transaction, phases[phaseIndex] );
    }

    CHECK( transaction.Phase() == Phase::Complete );
    CHECK_FALSE( transaction.Acceptance().toggledVsync );
    static_assert( !std::is_copy_constructible_v<OperatorCommandTransaction> );
    static_assert( !std::is_copy_assignable_v<OperatorCommandTransaction> );
}


TEST_CASE( "SceneWorld deletion preserves the moved tornado body timer history" )
{
    SkullbonezCore::Core::SbDiagnosticStore worldDiagnostics;
    SkullbonezCore::Runtime::SceneWorld world( worldDiagnostics );
    SkullbonezCore::Core::EngineConfig config;
    config.physicsExecution.parallel = false;
    world.ApplyRuntimeConfig( config );

    std::array<SkullbonezCore::Physics::PhysicsBodyHandle, 4u> bodyHandles {};

    for ( std::size_t index = 0; index < bodyHandles.size(); ++index )
    {
        const SkullbonezCore::Physics::PhysicsSceneObjectId sceneObjectId { static_cast<uint32_t>( 4100u + index ) };
        const SkullbonezCore::Math::CollisionDetection::CollisionShape
            shape = SkullbonezCore::Math::CollisionDetection::BoundingSphere( 1.0f, Vector3( 0.0f, 0.0f, 0.0f ) );
        SkullbonezCore::Runtime::SceneEntityCreateDesc entity;
        entity.sceneObjectId = sceneObjectId;
        entity.SetName( "tornado-state-remap-body" );

        auto bodyDesc = SkullbonezCore::Physics::
            MakePhysicsBodyCreateDesc( sceneObjectId, shape, Vector3( static_cast<float>( index ), 5.0f, 0.0f ),
                                       SkullbonezCore::Math::Orientation::IDENTITY_QUATERNION, Vector3( 0.0f, 0.0f, 0.0f ),
                                       Vector3( 0.0f, 0.0f, 0.0f ), Vector3( 0.4f, 0.4f, 0.4f ), 1.0f, 0.0f,
                                       SkullbonezCore::Physics::PhysicsBodyMotionKind::Dynamic, "tornado-state-remap-body" );
        auto colliderDesc = SkullbonezCore::Physics::MakeColliderCreateDesc( shape, 0.0f, 0u, "tornado-state-remap-body" );
        colliderDesc.sceneObjectId = sceneObjectId;

        const SkullbonezCore::Runtime::SceneEntityCreateResult created = world.TryCreateSceneEntity( entity, bodyDesc,
                                                                                                     colliderDesc );
        REQUIRE( created.status.Ok() );
        REQUIRE( created.body.IsValid() );
        bodyHandles[index] = created.body;
    }

    SkullbonezCore::Gameplay::TornadoSystemConfig system;
    system.enabled = true;
    system.vortices.resize( 1u );
    system.vortices[0].field.enabled = true;
    world.Tornado().SetReplayState( { 0.1f, 0.2f, 0.3f, 0.4f }, { 1.1f, 1.2f, 1.3f, 1.4f }, {}, system, 1.0 );

    REQUIRE( world.DestroySceneEntity( bodyHandles[1] ) );
    CHECK( world.BodyStore().ModelIndexForHandle( bodyHandles[3] ) == 1 );
    CHECK_FALSE( world.BodyStore().Contains( bodyHandles[1] ) );
    REQUIRE( world.Tornado().CaptureSeconds().size() == 3u );
    REQUIRE( world.Tornado().EjectCooldownSeconds().size() == 3u );
    CHECK( world.Tornado().CaptureSeconds()[0] == 0.1f );
    CHECK( world.Tornado().CaptureSeconds()[1] == 0.4f );
    CHECK( world.Tornado().CaptureSeconds()[2] == 0.3f );
    CHECK( world.Tornado().EjectCooldownSeconds()[0] == 1.1f );
    CHECK( world.Tornado().EjectCooldownSeconds()[1] == 1.4f );
    CHECK( world.Tornado().EjectCooldownSeconds()[2] == 1.3f );
}


TEST_CASE( "Orderly exit returns the interaction recording save failure" )
{
    namespace fs = std::filesystem;
    const fs::path root = "TestOutput/runtime_contracts/interaction_shutdown_failure";
    const fs::path manifest = root / "interaction.json";
    std::error_code error;
    fs::remove_all( root, error );
    REQUIRE_FALSE( error );
    REQUIRE( fs::create_directories( root, error ) );
    REQUIRE_FALSE( error );

    SkullbonezCore::Core::SbDiagnosticStore localDiagnostics;
    SkullbonezCore::Runtime::InteractionAutomationRecorder recorder;
    REQUIRE( recorder.Arm( localDiagnostics, manifest.string().c_str(), 1 ).Ok() );

    {
        std::ofstream scene( recorder.ScenePath(), std::ios::binary | std::ios::trunc );
        scene << "{}\n";
        REQUIRE( scene.good() );
    }

    struct ArmedBoundaryContext
    {
        SkullbonezCore::Runtime::InteractionAutomationRecorder* recorder = nullptr;
        SkullbonezCore::Core::SbDiagnosticStore* diagnostics = nullptr;
        const fs::path* root = nullptr;
        SbResult boundaryResult = SbResult::Success();
        std::error_code removalError;
        int attempts = 0;
    } context { &recorder, &localDiagnostics, &root };

    const auto captureArmedBoundary = []( void* opaque )
    {
        auto& capture = *static_cast<ArmedBoundaryContext*>( opaque );
        ++capture.attempts;
        SkullbonezCore::Runtime::InteractionRecordingBaseline baseline;
        capture.boundaryResult = capture.recorder->BeginAtBoundary( *capture.diagnostics, 1280, 720, 1u, baseline, false );

        if ( capture.boundaryResult.Ok() )
        {
            fs::remove_all( *capture.root, capture.removalError );
        }
    };

    SkullbonezCore::Runtime::ApplicationExitState applicationExit( localDiagnostics );
    RuntimeAllocationScope shutdownScope( RuntimeAllocationPhase::Shutdown );
    const SbResult result = SkullbonezCore::Runtime::ResolveRunExitAfterInteractionRecording( recorder, localDiagnostics,
                                                                                              applicationExit, 0,
                                                                                              captureArmedBoundary,
                                                                                              &context );

    CHECK( context.attempts == 1 );
    CHECK( context.boundaryResult.Ok() );
    CHECK_FALSE( context.removalError );
    REQUIRE_FALSE( result.Ok() );
    CHECK( applicationExit.HasOwnedFailure() );
    CHECK_FALSE( recorder.IsActive() );
    REQUIRE( result.ErrorOwner() );
    CHECK( std::strcmp( result.ErrorOwner(), "InteractionRecorder" ) == 0 );
    REQUIRE( result.ErrorMessage() );
    CHECK( std::strstr( result.ErrorMessage(), "failed to hash the recorded scene snapshot" ) != nullptr );

    const SbResult repeated = SkullbonezCore::Runtime::ResolveRunExitAfterInteractionRecording( recorder, localDiagnostics,
                                                                                                applicationExit, 0,
                                                                                                captureArmedBoundary,
                                                                                                &context );
    CHECK( context.attempts == 1 );
    REQUIRE_FALSE( repeated.Ok() );
    CHECK( std::strcmp( repeated.ErrorOwner(), "InteractionRecorder" ) == 0 );
}


TEST_CASE( "Raw mouse startup registration reports the native failure" )
{
    SkullbonezCore::Core::SbDiagnosticStore localDiagnostics;
    SkullbonezCore::Hardware::InputWindowBridgeTestAccess::RawMouseRegistrationProbe probe;
    probe.error = ERROR_ACCESS_DENIED;
    const HWND window = reinterpret_cast<HWND>( static_cast<uintptr_t>( 1u ) );

    const SbResult failure = SkullbonezCore::Hardware::InputWindowBridgeTestAccess::RegisterRawMouse( localDiagnostics,
                                                                                                      window, true, probe );
    REQUIRE_FALSE( failure.Ok() );
    CHECK( probe.called );
    CHECK( probe.observed.usUsagePage == 0x01u );
    CHECK( probe.observed.usUsage == 0x02u );
    CHECK( probe.observed.hwndTarget == window );
    REQUIRE( failure.ErrorOwner() );
    CHECK( std::strcmp( failure.ErrorOwner(), "Runtime/Input" ) == 0 );
    REQUIRE( failure.ErrorMessage() );
    CHECK( std::strstr( failure.ErrorMessage(), "win32_error=5" ) != nullptr );

    int reportedFailures = 0;
    int workerShutdowns = 0;
    int developmentToolShutdowns = 0;
    int windowCleanups = 0;
    int comUninitializations = 0;
    int rendererStarts = 0;
    std::string failureTitle;
    std::vector<int> startupSequence;
    const SbResult startupFailure = SkullbonezCore::Runtime::StartRendererAfterRawMouseRegistration(
        failure,
        [&]( const SbResult&, const char* title )
        {
            ++reportedFailures;
            failureTitle = title ? title : "";
            startupSequence.push_back( 1 );
        },
        [&]()
        {
            ++workerShutdowns;
            startupSequence.push_back( 2 );
        },
        [&]()
        {
            ++developmentToolShutdowns;
            startupSequence.push_back( 3 );
        },
        [&]()
        {
            ++windowCleanups;
            startupSequence.push_back( 4 );
        },
        [&]()
        {
            ++comUninitializations;
            startupSequence.push_back( 5 );
        },
        [&]()
        {
            ++rendererStarts;
            startupSequence.push_back( 6 );
            return SbResult::Success();
        } );
    REQUIRE_FALSE( startupFailure.Ok() );
    CHECK( std::strcmp( startupFailure.ErrorOwner(), "Runtime/Input" ) == 0 );
    CHECK( failureTitle == "SkullbonezCore Input Startup Failed" );
    CHECK( reportedFailures == 1 );
    CHECK( workerShutdowns == 1 );
    CHECK( developmentToolShutdowns == 1 );
    CHECK( windowCleanups == 1 );
    CHECK( comUninitializations == 1 );
    CHECK( rendererStarts == 0 );
    CHECK( startupSequence == std::vector<int> { 1, 2, 3, 4, 5 } );

    probe = {};
    const SbResult success = SkullbonezCore::Hardware::InputWindowBridgeTestAccess::RegisterRawMouse( localDiagnostics,
                                                                                                      window, true, probe );
    CHECK( success.Ok() );
    CHECK( probe.called );

    startupSequence.clear();
    const SbResult startupSuccess = SkullbonezCore::Runtime::StartRendererAfterRawMouseRegistration(
        success,
        [&]( const SbResult&, const char* )
        {
            ++reportedFailures;
            startupSequence.push_back( 1 );
        },
        [&]()
        {
            ++workerShutdowns;
            startupSequence.push_back( 2 );
        },
        [&]()
        {
            ++developmentToolShutdowns;
            startupSequence.push_back( 3 );
        },
        [&]()
        {
            ++windowCleanups;
            startupSequence.push_back( 4 );
        },
        [&]()
        {
            ++comUninitializations;
            startupSequence.push_back( 5 );
        },
        [&]()
        {
            ++rendererStarts;
            startupSequence.push_back( 6 );
            return SbResult::Success();
        } );
    CHECK( startupSuccess.Ok() );
    CHECK( reportedFailures == 1 );
    CHECK( workerShutdowns == 1 );
    CHECK( developmentToolShutdowns == 1 );
    CHECK( windowCleanups == 1 );
    CHECK( comUninitializations == 1 );
    CHECK( rendererStarts == 1 );
    CHECK( startupSequence == std::vector<int> { 6 } );
}


TEST_CASE( "DX12 mesh vertex data validates one complete packed layout" )
{
    using SkullbonezCore::Rendering::Dx12MeshUploadSlice;
    using SkullbonezCore::Rendering::MeshVertexDataView;
    using SkullbonezCore::Rendering::VertexFormat12;

    const float components[6] = {};
    const std::optional<MeshVertexDataView> vertices = MeshVertexDataView::TryCreate( components, 2, 3,
                                                                                      VertexFormat12::Pos3 );
    REQUIRE( vertices );
    CHECK( vertices->Components().size() == 6u );
    CHECK( vertices->ByteCount() == sizeof( components ) );
    CHECK( vertices->VertexCount() == 2 );
    CHECK( vertices->FloatsPerVertex() == 3 );
    CHECK( vertices->Format() == VertexFormat12::Pos3 );

    CHECK_FALSE( MeshVertexDataView::TryCreate( nullptr, 2, 3, VertexFormat12::Pos3 ) );
    CHECK_FALSE( MeshVertexDataView::TryCreate( components, 0, 3, VertexFormat12::Pos3 ) );
    CHECK_FALSE( MeshVertexDataView::TryCreate( components, 2, 0, VertexFormat12::Pos3 ) );
    CHECK_FALSE( Dx12MeshUploadSlice::TryCreate( 0, nullptr, 0, nullptr ) );
}


TEST_CASE( "Render model selection encodes all marked and unmarked modes" )
{
    const std::vector<uint8_t> mask { 0u, 1u };
    const SkullbonezCore::Rendering::RenderModelSelection all = SkullbonezCore::Rendering::RenderModelSelection::All();
    const SkullbonezCore::Rendering::RenderModelSelection marked = SkullbonezCore::Rendering::RenderModelSelection::Marked(
        mask );
    const SkullbonezCore::Rendering::RenderModelSelection
        unmarked = SkullbonezCore::Rendering::RenderModelSelection::Unmarked( mask );

    CHECK( all.Includes( 0 ) );
    CHECK( all.Includes( 2 ) );
    CHECK_FALSE( marked.Includes( 0 ) );
    CHECK( marked.Includes( 1 ) );
    CHECK_FALSE( marked.Includes( 2 ) );
    CHECK( unmarked.Includes( 0 ) );
    CHECK_FALSE( unmarked.Includes( 1 ) );
    CHECK( unmarked.Includes( 2 ) );
}
