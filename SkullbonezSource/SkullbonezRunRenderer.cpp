/*
File: SkullbonezSource/SkullbonezRunRenderer.cpp
Purpose:
  Keeps DX12 as the runtime renderer and owns backend-resource reset helpers.

Mental model:
  Renderer-facing code translates engine concepts into backend resources, draw
  calls, shader bindings, and validation artifacts.

Glossary:
  DX12 (DirectX 12): Production renderer API used for explicit GPU resource,
  descriptor, and command-list control.
  DX11 (DirectX 11): Retired runtime renderer that remains in source only until
  the backend deletion phase finishes.
  OpenGL: Retired runtime renderer that remains in source only until the backend
  deletion phase finishes.
  GPU (Graphics Processing Unit): Processor that executes rendering, compute,
  and raytracing commands asynchronously from the CPU.
  Descriptor: Small binding record that tells a renderer how to interpret a
  resource.
  Back buffer: Swap-chain image that will be presented to the window.

Related:
  - Agentic/Plans/dx12-only-renderer-retirement-plan.md
  - Agentic/Reference/comment-style-guide.md
*/
#include "SkullbonezRunInternal.h"

using namespace SkullbonezCore::Basics;
using namespace SkullbonezCore::Math::CollisionDetection;
using namespace SkullbonezCore::Math::Orientation;
using namespace SkullbonezCore::Math::Transformation;
using namespace SkullbonezCore::Physics;
using namespace SkullbonezCore::Basics::RunInternal;

namespace
{
struct RendererSwitchResourceStep
{
    const char* name;
    void ( SkullbonezRun::*release )();
    void ( SkullbonezRun::*rebuild )();
};
} // namespace

void SkullbonezRun::ReleaseReflectionResourcesForSwitch()
{
    if ( m_systems.reflectionFBO )
    {
        m_systems.reflectionFBO->ResetResources();
        m_systems.reflectionFBO.reset();
    }
}


void SkullbonezRun::RebuildReflectionResourcesAfterSwitch()
{
    const int fboW = Gfx().GetWidth() * 2;
    const int fboH = Gfx().GetHeight() * 2;
    m_systems.reflectionFBO = Gfx().CreateFramebuffer( fboW, fboH );
}


void SkullbonezRun::ReleaseTextResourcesForSwitch()
{
    Text2d::DeleteFont();
}


void SkullbonezRun::RebuildTextResourcesAfterSwitch()
{
    Text2d::BuildFont( "Verdana" );
}


void SkullbonezRun::ReleaseModelCollectionResourcesForSwitch()
{
    m_cGameModelCollection.ResetRenderResources();
}


void SkullbonezRun::ReleaseHelperResourcesForSwitch()
{
    SkullbonezHelper::ResetRenderResources();
}


void SkullbonezRun::ReleaseCollisionVisualizerResourcesForSwitch()
{
    m_collisionVisualizer.ResetResources();
}


void SkullbonezRun::ReleaseUIResourcesForSwitch()
{
    m_UI.ResetResources();
}


void SkullbonezRun::ReleaseTextureResourcesForSwitch()
{
    if ( m_systems.textures )
    {
        m_systems.textures->DeleteAllTextures();
    }
}


void SkullbonezRun::RebuildTerrainResourcesAfterSwitch()
{
    if ( m_systems.terrain )
    {
        m_systems.terrain->ResetRenderResources();
    }
}


void SkullbonezRun::RebuildSkyBoxResourcesAfterSwitch()
{
    if ( m_systems.skyBox )
    {
        m_systems.skyBox->ResetRenderResources();
    }
}


void SkullbonezRun::RebuildWorldResourcesAfterSwitch()
{
    m_cWorldEnvironment.ResetRenderResources();
}


void SkullbonezRun::RunRendererSwitchResourceReleaseSteps()
{
    const RendererSwitchResourceStep steps[] = {
        { "reflection_fbo", &SkullbonezRun::ReleaseReflectionResourcesForSwitch, nullptr },
        { "cinematic_targets", &SkullbonezRun::ResetCinematicRenderResources, nullptr },
        { "text", &SkullbonezRun::ReleaseTextResourcesForSwitch, nullptr },
        { "game_models", &SkullbonezRun::ReleaseModelCollectionResourcesForSwitch, nullptr },
        { "helper_cache", &SkullbonezRun::ReleaseHelperResourcesForSwitch, nullptr },
        { "collision_visualizer", &SkullbonezRun::ReleaseCollisionVisualizerResourcesForSwitch, nullptr },
        { "ui", &SkullbonezRun::ReleaseUIResourcesForSwitch, nullptr },
        { "textures", &SkullbonezRun::ReleaseTextureResourcesForSwitch, nullptr },
    };

    for ( const RendererSwitchResourceStep& step : steps )
    {
        (void)step.name;
        ( this->*step.release )();
    }
}


void SkullbonezRun::RunRendererSwitchResourceRebuildSteps()
{
    const RendererSwitchResourceStep steps[] = {
        { "terrain", nullptr, &SkullbonezRun::RebuildTerrainResourcesAfterSwitch },
        { "skybox", nullptr, &SkullbonezRun::RebuildSkyBoxResourcesAfterSwitch },
        { "world", nullptr, &SkullbonezRun::RebuildWorldResourcesAfterSwitch },
        { "reflection_fbo", nullptr, &SkullbonezRun::RebuildReflectionResourcesAfterSwitch },
        { "cinematic_targets", nullptr, &SkullbonezRun::EnsureCinematicRenderResources },
        { "text", nullptr, &SkullbonezRun::RebuildTextResourcesAfterSwitch },
    };

    for ( const RendererSwitchResourceStep& step : steps )
    {
        (void)step.name;
        ( this->*step.rebuild )();
    }
}


void SkullbonezRun::ReleaseBackendOwnedResourcesForSwitch()
{
    // All GPU-visible resources must be released while the backend that owns them is still alive.
    // The backend's FlushGPU() ensures all in-flight GPU work completes before resource destruction.
    Gfx().FlushGPU();
    RunRendererSwitchResourceReleaseSteps();
#if defined( SKULLBONEZ_PROFILE_ENABLED )
    Profiler::Instance().InvalidateGpuQueries();
#endif
}


void SkullbonezRun::RebuildBackendOwnedResourcesAfterSwitch()
{
    m_systems.window->HandleScreenResize();
    Gfx().SetVsyncEnabled( m_runtimeSettings.isVsyncEnabled );

    SetInitialOpenGlState();
    RunRendererSwitchResourceRebuildSteps();
}
