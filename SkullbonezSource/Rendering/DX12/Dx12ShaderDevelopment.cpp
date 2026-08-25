/*
File: SkullbonezSource/Rendering/DX12/Dx12ShaderDevelopment.cpp
Purpose:
  Implements the cold DX12 shader rebake and all-or-nothing adoption transaction.

Summary:
  The owner launches the repository's pinned bake tool, validates replacement
  bytecode for every registered raster shader, stages the generate-mips compute
  PSO, drains the frame owner, and then performs a no-fail publication. It also
  prepares the fixed first-gameplay manifest projection before frame rendering.

Glossary:
  Offline DXC bake: Repository tool invocation that creates one pinned manifest
    and its complete shader-stage generation.
  Adoption: No-fail swap from staged bytecode/PSOs to the live generation.
  Dependent PSO: Native pipeline whose recipe embeds a shader bytecode identity.

Invariants:
  - Candidate preparation completes for every shader before any live object changes.
  - The commit contains no recoverable operation after old PSOs are released.
  - Registered rows are fixed-capacity and contain no backend or command-list pointer.
  - Failed cold preparation remains recoverable and never caches a partial program.

Related:
  - SkullbonezSource/Rendering/DX12/Dx12ShaderDevelopment.h
  - SkullbonezSource/Rendering/DX12/RenderBackendDX12.Pipeline.cpp
  - tools/bake_shaders.bat
*/
#include "Dx12ShaderDevelopment.h"
#include "../../Core/SbDiagnosticStore.h"
#include "../../Core/WindowConstants.h"

#include "RenderBackendDX12.h"
#include "ShaderBytecodeManifest.h"
#include "ShaderDX12.h"
#include "../../Core/Log.h"
#include "../../Core/FatalError.h"

#include <array>
#include <cctype>
#include <cstring>
#include <cstdio>

using namespace SkullbonezCore::Rendering;
using Microsoft::WRL::ComPtr;

namespace
{
bool Dx12RenderSuiteRequested()
{
    constexpr char SUITE_TOKEN[] = "--suite";
    const char* cursor = GetCommandLineA();

    while ( cursor && *cursor )
    {
        while ( *cursor && std::isspace( static_cast<unsigned char>( *cursor ) ) )
        {
            ++cursor;
        }

        const bool quoted = *cursor == '"';

        if ( quoted )
        {
            ++cursor;
        }

        const char* begin = cursor;

        while ( *cursor && ( quoted ? *cursor != '"' : !std::isspace( static_cast<unsigned char>( *cursor ) ) ) )
        {
            ++cursor;
        }

        if ( static_cast<std::size_t>( cursor - begin ) == std::strlen( SUITE_TOKEN ) &&
             std::strncmp( begin, SUITE_TOKEN, std::strlen( SUITE_TOKEN ) ) == 0 )
        {
            return true;
        }

        if ( quoted && *cursor == '"' )
        {
            ++cursor;
        }
    }

    return false;
}
} // namespace


Dx12ShaderDevelopment::Dx12ShaderDevelopment( SkullbonezCore::Core::SbDiagnosticStore& resultDiagnostics,
                                              Dx12PipelineOwner& pipeline, Dx12TextureOwner& textures,
                                              Dx12GeometryOwner& geometry, Dx12RenderDevice& device, Dx12FrameOwner& frame,
                                              Dx12Diagnostics& diagnostics )
    : m_resultDiagnostics( resultDiagnostics ), m_pipeline( pipeline ), m_textures( textures ), m_geometry( geometry ),
      m_device( device ), m_frame( frame ), m_diagnostics( diagnostics )
{
}


bool Dx12ShaderDevelopment::Enabled() const
{
    return DevShaderHotReloadEnabled();
}


Dx12InitialRasterShaderBytecodePreparationSummary Dx12ShaderDevelopment::PrepareInitialRasterShaderBytecode()
{
    std::string error;
    const ShaderBytecodeManifestCache::PreparationSummary cacheSummary = m_bytecodeCache.PrepareFirstGameplayPrograms( error );
    const Dx12InitialRasterShaderBytecodePreparationSummary summary { cacheSummary.attempted, cacheSummary.complete,
                                                                      cacheSummary.newlyPublished, cacheSummary.cacheHits,
                                                                      cacheSummary.stageLoads };

    // Bounded cold evidence: stdout survives Profile builds and records only
    // this invocation. Lifetime totals cannot distinguish a partial retry from
    // an idempotent warm call.
    std::fprintf( stdout,
                  "dx12_shader_manifest_warm attempted=%zu complete=%zu newly_published=%zu cache_hits=%zu "
                  "stage_loads=%zu\n",
                  summary.attempted, summary.complete, summary.newlyPublished, summary.cacheHits, summary.stageLoads );
    std::fflush( stdout );

    if ( summary.complete != summary.attempted )
    {
        // Recoverable error: warm-up follows the same external-input policy as
        // CreateShader. Leave failed rows uncached so their actual lazy owners
        // retry and publish the existing path-specific rejection diagnostics.
        std::fprintf( stderr, "dx12_shader_manifest_warm_incomplete reason=%s\n", error.c_str() );
        std::fflush( stderr );
    }

    // The existing --suite launch is the renderer-owned validation boundary.
    // External manifest overrides remain recoverable: the assertion probe runs
    // only after the production warm call proved the pinned 13-row projection.
    if ( Dx12RenderSuiteRequested() && summary.attempted == 13u && summary.complete == 13u )
    {
        ValidateInitialRasterShaderBytecodeCache();
    }

    return summary;
}


bool Dx12ShaderDevelopment::LoadCurrentProgramBytecode( const char* hlslPath, ComPtr<ID3DBlob>& outVertex,
                                                        ComPtr<ID3DBlob>& outPixel, std::string& outError )
{
    return m_bytecodeCache.LoadProgram( hlslPath, outVertex, outPixel, outError ).complete;
}


void Dx12ShaderDevelopment::ValidateInitialRasterShaderBytecodeCache() const
{
    auto require = []( bool condition, const char* reason )
    {
        if ( !condition )
        {
            // Fatal invariant: this path is entered only by the repository's
            // pinned DX12 suite after ordinary production preparation succeeds.
            SB_FATAL( "Dx12ShaderDevelopment", "Shader manifest cache validation failed: %s", reason );
        }
    };

    ShaderBytecodeManifestCache isolated;
    std::string error;
    const ShaderBytecodeManifestCache::PreparationSummary first = isolated.PrepareFirstGameplayPrograms( error );
    require( first.attempted == 13u && first.complete == 13u && first.newlyPublished == 13u && first.cacheHits == 0u &&
                 first.stageLoads == 26u,
             "first warm did not publish exactly 13 two-stage programs" );

    const ShaderBytecodeManifestCache::PreparationSummary second = isolated.PrepareFirstGameplayPrograms( error );
    require( second.attempted == 13u && second.complete == 13u && second.newlyPublished == 0u && second.cacheHits == 13u &&
                 second.stageLoads == 0u,
             "second warm did not use exactly 13 manifest-free cache hits" );

    isolated.Reset();
    const std::string missingPath = std::string( DATA_ROOT ) + "shaders/__manifest_cache_validation_missing.hlsl";
    ComPtr<ID3DBlob> vertex;
    ComPtr<ID3DBlob> pixel;
    const ShaderBytecodeManifestCache::ProgramLoadSummary missingFirst = isolated.LoadProgram( missingPath.c_str(), vertex,
                                                                                               pixel, error );
    const ShaderBytecodeManifestCache::ProgramLoadSummary missingSecond = isolated.LoadProgram( missingPath.c_str(), vertex,
                                                                                                pixel, error );
    require( !missingFirst.complete && !missingFirst.newlyPublished && !missingFirst.cacheHit &&
                 missingFirst.stageLoads == 1u && !missingSecond.complete && !missingSecond.newlyPublished &&
                 !missingSecond.cacheHit && missingSecond.stageLoads == 1u && isolated.m_programCount == 0u,
             "missing program was cached or did not retry its manifest load" );

    isolated.Reset();
    const std::string directPath = std::string( DATA_ROOT ) + "shaders/lit_textured.hlsl";
    const ShaderBytecodeManifestCache::ProgramLoadSummary direct = isolated.LoadProgram( directPath.c_str(), vertex, pixel,
                                                                                         error );
    require( direct.complete && direct.newlyPublished && !direct.cacheHit && direct.stageLoads == 2u &&
                 isolated.m_programCount == 1u,
             "direct load did not publish one verified raster pair" );

    isolated.Reset();
    const ShaderBytecodeManifestCache::PreparationSummary afterInvalidation = isolated.PrepareFirstGameplayPrograms( error );
    require( afterInvalidation.attempted == 13u && afterInvalidation.complete == 13u &&
                 afterInvalidation.newlyPublished == 13u && afterInvalidation.cacheHits == 0u &&
                 afterInvalidation.stageLoads == 26u,
             "cache invalidation did not force the next warm to reload all stages" );

    std::fprintf( stdout,
                  "dx12_shader_manifest_cache_validation pass=1 first_attempted=%zu first_complete=%zu "
                  "first_newly_published=%zu first_cache_hits=%zu first_stage_loads=%zu second_newly_published=%zu "
                  "second_cache_hits=%zu second_stage_loads=%zu missing_first_stage_loads=%zu "
                  "missing_second_stage_loads=%zu "
                  "direct_stage_loads=%zu reload_stage_loads=%zu\n",
                  first.attempted, first.complete, first.newlyPublished, first.cacheHits, first.stageLoads,
                  second.newlyPublished, second.cacheHits, second.stageLoads, missingFirst.stageLoads,
                  missingSecond.stageLoads, direct.stageLoads, afterInvalidation.stageLoads );
    std::fflush( stdout );
}


SkullbonezCore::Core::SbResult Dx12ShaderDevelopment::ReloadShadersFromSource()
{
    const SkullbonezCore::Core::SbResult bakeResult = BakeSourceGeneration();

    if ( !bakeResult.Ok() )
    {
        return bakeResult;
    }

    // Lifetime: every current PSO may still be referenced by submitted command
    // lists. The shader owner publishes no replacement until the frame owner
    // proves those lists complete and reopens the recording epoch.
    const SkullbonezCore::Core::SbResult drainResult = m_frame.FinishAndReopen( m_diagnostics );

    if ( !drainResult.Ok() )
    {
        return drainResult;
    }

    return ReloadBakedGeneration( m_device.Device() );
}


void Dx12ShaderDevelopment::RegisterShader( ShaderDX12* shader )
{
    if ( !shader )
    {
        return;
    }

    for ( size_t index = 0; index < m_liveShaderCount; ++index )
    {
        if ( m_liveShaders[index] == shader )
        {
            return;
        }
    }

    if ( m_liveShaderCount >= m_liveShaders.size() )
    {
        // Fatal invariant: the registry is a fixed backend-lifetime contract. Growing it
        // during resource creation would violate the runtime allocation policy.
        SB_FATAL( "Dx12ShaderDevelopment", "Live raster shader registry exhausted. capacity=%zu", m_liveShaders.size() );
    }

    m_liveShaders[m_liveShaderCount++] = shader;
}


void Dx12ShaderDevelopment::UnregisterShader( ShaderDX12* shader )
{
    for ( size_t index = 0; index < m_liveShaderCount; ++index )
    {
        if ( m_liveShaders[index] != shader )
        {
            continue;
        }

        m_liveShaders[index] = m_liveShaders[m_liveShaderCount - 1];
        m_liveShaders[m_liveShaderCount - 1] = nullptr;
        --m_liveShaderCount;
        break;
    }

    if ( m_pipeline.ActiveShader() == shader )
    {
        // Invariant: deleting a live shader cannot leave pipeline desired state
        // borrowing its identity for the next draw.
        m_pipeline.SetActiveShader( nullptr );
    }
}


SkullbonezCore::Core::SbResult Dx12ShaderDevelopment::BakeSourceGeneration() const
{
    if ( !Enabled() )
    {
        return m_resultDiagnostics.Failure( "Rendering/DX12", "Shader hot reload requires --dev-shader-hot-reload" );
    }

    constexpr char BAKE_PATH[] = "tools\\bake_shaders.bat";

    if ( GetFileAttributesA( BAKE_PATH ) == INVALID_FILE_ATTRIBUTES )
    {
        return m_resultDiagnostics.Failure( "Rendering/DX12",
                                            "Shader bake tool is unavailable from this working directory" );
    }

    // Cold utility action: invoke the same pinned DXC bake used by validation.
    // Synchronous completion makes the manifest and stage files one generation.
    char commandLine[] = "cmd.exe /d /c tools\\bake_shaders.bat";
    STARTUPINFOA startup = {};

    startup.cb = sizeof( startup );
    PROCESS_INFORMATION process = {};

    fprintf( stdout, "[shader-hot-reload] bake begin\n" );
    fflush( stdout );

    if ( !CreateProcessA( nullptr, commandLine, nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startup,
                          &process ) )
    {
        return m_resultDiagnostics.Failure( "Rendering/DX12", "Shader bake process failed to start (error=%lu)",
                                            GetLastError() );
    }

    const DWORD waitResult = WaitForSingleObject( process.hProcess, INFINITE );
    DWORD exitCode = ERROR_PROCESS_ABORTED;
    const bool exited = waitResult == WAIT_OBJECT_0 && GetExitCodeProcess( process.hProcess, &exitCode );
    CloseHandle( process.hThread );
    CloseHandle( process.hProcess );

    if ( !exited || exitCode != 0 )
    {
        // Recoverable error: the external bake can fail without changing the live generation.
        return m_resultDiagnostics.Failure( "Rendering/DX12", "Shader bake failed (wait=%lu exit=%lu)", waitResult,
                                            exitCode );
    }

    return SkullbonezCore::Core::SbResult::Success();
}


SkullbonezCore::Core::SbResult Dx12ShaderDevelopment::ReloadBakedGeneration( ID3D12Device* device )
{
    // Runtime allocation policy: candidate reflection containers may allocate only in
    // the explicit BackendInit developer scope held by the F9 caller.
    ID3D12PipelineState* generateMipsCandidate = nullptr;
    const SkullbonezCore::Core::SbResult computeResult = m_textures.PrepareGenerateMipsShaderReload( device,
                                                                                                     generateMipsCandidate );

    if ( !computeResult.Ok() )
    {
        return computeResult;
    }

    std::array<ShaderDX12ReloadPayload, LIVE_SHADER_CAPACITY> candidates;

    for ( size_t index = 0; index < m_liveShaderCount; ++index )
    {
        ShaderDX12* live = m_liveShaders[index];

        if ( live && !live->PrepareReload( candidates[index] ) )
        {
            generateMipsCandidate->Release();

            // Recoverable error: a changed shader interface needs a rebuilt executable;
            // every live shader and PSO still names the previous generation.
            return m_resultDiagnostics.Failure( "Rendering/DX12",
                                                "Shader hot reload rejected changed or invalid bytecode contract" );
        }
    }

    // Lifetime: the composition root drained the GPU before this method. From
    // here through publication every operation is a bounded no-fail release/swap.
    m_bytecodeCache.Reset();
    m_pipeline.ReleaseShaderPipelinesForReload();

    for ( size_t index = 0; index < m_liveShaderCount; ++index )
    {
        if ( m_liveShaders[index] )
        {
            m_liveShaders[index]->AdoptReload( candidates[index] );
        }
    }

    m_pipeline.RestoreShaderPipelinesAfterReload();
    m_textures.AdoptGenerateMipsShaderReload( generateMipsCandidate );
    m_geometry.InvalidateGridLinePipelinesForShaderReload();
    m_pipeline.InvalidateCommandState();

    fprintf( stdout, "[shader-hot-reload] committed\n" );
    fflush( stdout );
    SkullbonezCore::Core::Log().WriteEventf( "dx12_shader_hot_reload_complete owner=Dx12ShaderDevelopment shaders=%llu",
                                             static_cast<unsigned long long>( m_liveShaderCount ) );

    return SkullbonezCore::Core::SbResult::Success();
}


void Dx12ShaderDevelopment::ResetAfterShutdown()
{
    // Lifetime: runtime and geometry resource owners must destroy every shader
    // before the renderer tears down pipeline/device state. A remaining row
    // would become a dangling owner reference after backend destruction.
    if ( m_liveShaderCount != 0 )
    {
        SB_FATAL( "Dx12ShaderDevelopment", "Shader registry remained live at backend shutdown. count=%zu capacity=%zu",
                  m_liveShaderCount, m_liveShaders.size() );
    }

    m_liveShaders = {};
    m_bytecodeCache.Reset();
}
