#include "PairedViewRenderer.h"
#include "PrimitiveBatchRenderer.h"
#include "DX12/RenderBackendDX12.h"
#include "DX12/Dx12ResourceBuilder.h"
#include "DX12/Dx12GraphTransientPool.h"
#include "DX12/Dx12FrameOwner.h"

using namespace SkullbonezCore::Rendering;

bool PairedViewRenderer::Prepare( Dx12ResourceBuilder& resources, Dx12GeometryOwner& geometry, int width, int height )
{
    if ( width < 1 || height < 1 )
    {
        return false;
    }
    if ( !m_shader )
    {
        m_shader = resources.CreateShader( "shaders/image_pair", "image_pair" );
    }
    if ( !m_quad )
    {
        const int attributes[] = { 2, 2 };
        m_quad = geometry.CreateDynamicVB( attributes, 2, 6 );
    }
    for ( auto& target : m_targets )
    {
        if ( !target || target->GetWidth() != width || target->GetHeight() != height )
        {
            target = resources.CreateFramebuffer( width, height );
        }
    }
    return Ready();
}
void PairedViewRenderer::Release( Dx12GeometryOwner& geometry )
{
    for ( auto& target : m_targets )
    {
        target.reset();
    }
    m_shader.reset();
    if ( m_quad )
    {
        geometry.DestroyDynamicVB( m_quad );
    }
    m_quad = 0;
}
void PairedViewRenderer::DrawModels( std::span<const ModelViewItem> models, const PairedViewFrame& frame,
                                     PrimitiveBatchRenderer& primitives, const Core::OrdinaryRenderConfig& lighting )
{
    constexpr float light[] = { 0.4f, 1.0f, 0.3f, 0.0f };
    {
        auto boxes = primitives.BeginBoxBatch( lighting, "lit_textured_instanced", frame.view, frame.projection, light );
        for ( const auto& model : models )
        {
            if ( model.shape == RenderInstanceShapeKind::Box )
            {
                boxes.DrawModel( model.transform, model.material );
            }
        }
    }
    {
        auto spheres = primitives.BeginSphereBatch( lighting, "lit_textured_instanced", frame.view, frame.projection,
                                                    light );
        for ( const auto& model : models )
        {
            if ( model.shape == RenderInstanceShapeKind::Sphere )
            {
                spheres.DrawModel( model.transform, model.material );
            }
        }
    }
    primitives.BeginConvexHullBatch( lighting, "lit_textured_instanced", frame.view, frame.projection, light, false, nullptr,
                                     nullptr, 1 );
    for ( const auto& model : models )
    {
        if ( model.shape == RenderInstanceShapeKind::ConvexHull && model.hull )
        {
            primitives.DrawConvexHullModel( *model.hull, model.transform, model.material );
        }
    }
    primitives.EndConvexHullBatch();
}
void PairedViewRenderer::DrawSide( const PairedViewFrame& frame, PrimitiveBatchRenderer& primitives,
                                   const Core::OrdinaryRenderConfig& lighting, Dx12FrameOwner& commands,
                                   Dx12TextureOwner& textures, int side )
{
    m_targets[side]->Bind();
    commands.SetViewport( 0, 0, m_targets[side]->GetWidth(), m_targets[side]->GetHeight() );
    commands.Clear( {} );
    for ( int slot = 0; slot < 6; ++slot )
    {
        textures.BindTexture( 0, slot );
    }
    DrawModels( frame.models[side], frame, primitives, lighting );
    m_targets[side]->Unbind();
}
void PairedViewRenderer::Composite( const PairedViewFrame& frame, Dx12FrameOwner& commands, Dx12GeometryOwner& geometry,
                                    Dx12TextureOwner& textures )
{
    commands.SetViewport( frame.x, frame.y, frame.width, frame.height );
    textures.BindTexture( m_targets[0]->GetColorTextureHandle(), 0 );
    textures.BindTexture( m_targets[1]->GetColorTextureHandle(), 1 );
    textures.BindTexture( m_targets[0]->GetDepthTextureHandle(), 2 );
    textures.BindTexture( m_targets[1]->GetDepthTextureHandle(), 3 );
    m_shader->Use();
    m_shader->SetVec4( "uPair", static_cast<float>( frame.mode ), frame.gain, frame.outlineAlpha,
                       frame.occludedOutline ? 1.0f : 0.0f );
    m_shader->SetVec4( "uTexel", 1.0f / m_targets[0]->GetWidth(), 1.0f / m_targets[0]->GetHeight(),
                       frame.stacked ? 1.0f : 0.0f, 0 );
    constexpr float quad[] = { -1, -1, 0, 1, 1, -1, 1, 1, 1, 1, 1, 0, -1, -1, 0, 1, 1, 1, 1, 0, -1, 1, 0, 0 };
    constexpr auto raster = MakePassRasterStateBucket( 0, { false, false, false } );
    geometry.UploadAndDrawDynamicVB( m_quad, quad, raster );
}
