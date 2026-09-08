#include "PairedViewRenderer.h"
#include "PrimitiveBatchRenderer.h"
#include "DX12/RenderBackendDX12.h"
#include "DX12/Dx12ResourceBuilder.h"
#include "DX12/Dx12GraphTransientPool.h"
#include "DX12/Dx12FrameOwner.h"
#include "../Physics/ConvexHullShape.h"
#include <algorithm>
#include <cmath>
#include <numbers>

using namespace SkullbonezCore::Rendering;
using SkullbonezCore::Math::Vector::Vector3;

template <typename EmitEdge>
static void EmitSphereOutline( const ModelViewItem& model, const PairedViewFrame& frame, EmitEdge emitEdge )
{
    const auto inverse = ( frame.view * model.transform ).Inverse();
    Vector3 axis( inverse.m[12], inverse.m[13], inverse.m[14] );
    Vector3 center( 0, 0, 0 );
    float radius = 1.0f;
    if ( std::abs( frame.projection.m[15] ) > 0.5f )
    {
        axis = Vector3( inverse.m[8], inverse.m[9], inverse.m[10] );
    }
    else
    {
        const float distanceSquared = axis.x * axis.x + axis.y * axis.y + axis.z * axis.z;
        if ( !std::isfinite( distanceSquared ) || distanceSquared <= 1.0f )
        {
            return;
        }
        // Perspective silhouette: tangent points satisfy dot(camera, point)=1
        // on the unit sphere. Work locally so model scale remains authoritative.
        center = axis * ( 1.0f / distanceSquared );
        radius = std::sqrt( 1.0f - 1.0f / distanceSquared );
    }
    if ( !axis.TryNormalise() )
    {
        return;
    }
    auto right = SkullbonezCore::Math::Vector::CrossProduct( axis, std::abs( axis.y ) < 0.9f ? Vector3( 0, 1, 0 )
                                                                                             : Vector3( 1, 0, 0 ) );
    if ( !right.TryNormalise() )
    {
        return;
    }
    const auto up = SkullbonezCore::Math::Vector::CrossProduct( axis, right );
    constexpr int segments = 128;
    static_assert( segments <= SkullbonezCore::Math::CollisionDetection::ConvexHullShape::MAX_EDGES );
    const Vector3 first = center + right * radius;
    Vector3 previous = first;
    for ( int i = 1; i <= segments; ++i )
    {
        const float angle = 2.0f * std::numbers::pi_v<float> * static_cast<float>( i ) / segments;
        const Vector3 next = i == segments ? first
                                           : center + ( right * std::cos( angle ) + up * std::sin( angle ) ) * radius;
        emitEdge( previous, next );
        previous = next;
    }
}

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
    if ( !m_outlineShader )
    {
        m_outlineShader = resources.CreateShader( "shaders/paired_outline", "paired_outline" );
    }
    if ( !m_outlineVB )
    {
        const int attributes[] = { 3, 3, 2 };
        m_outlineVB = geometry.CreateDynamicVB( attributes, 3,
                                                SkullbonezCore::Math::CollisionDetection::ConvexHullShape::MAX_EDGES * 6 );
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
    m_outlineShader.reset();
    if ( m_outlineVB )
    {
        geometry.DestroyDynamicVB( m_outlineVB );
    }
    m_outlineVB = 0;
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

void PairedViewRenderer::DrawOutlineModel( const ModelViewItem& model, const PairedViewFrame& frame,
                                           Dx12GeometryOwner& geometry )
{
    using SkullbonezCore::Math::CollisionDetection::ConvexHullShape;
    // Lifetime: one fixed scratch block is reused for each object's edge quads.
    // Hull topology supplies polygon edges, never triangle-fan diagonals.
    std::array<float, ConvexHullShape::MAX_EDGES * 6 * 8> vertices;
    std::size_t count = 0;
    auto edge = [&]( const Vector3& a, const Vector3& b )
    {
        constexpr float corners[][2] = { { 0, -1 }, { 1, -1 }, { 1, 1 }, { 0, -1 }, { 1, 1 }, { 0, 1 } };
        for ( const auto& corner : corners )
        {
            const float vertex[] = { a.x, a.y, a.z, b.x, b.y, b.z, corner[0], corner[1] };
            std::copy( std::begin( vertex ), std::end( vertex ), vertices.begin() + count );
            count += 8;
        }
    };
    if ( model.shape == RenderInstanceShapeKind::Box )
    {
        auto corner = []( int index )
        { return Vector3( index & 1 ? 1.0f : -1.0f, index & 2 ? 1.0f : -1.0f, index & 4 ? 1.0f : -1.0f ); };
        for ( int index = 0; index < 8; ++index )
        {
            for ( int bit = 1; bit <= 4; bit *= 2 )
            {
                if ( !( index & bit ) )
                {
                    edge( corner( index ), corner( index | bit ) );
                }
            }
        }
    }
    else if ( model.shape == RenderInstanceShapeKind::Sphere )
    {
        EmitSphereOutline( model, frame, edge );
    }
    else if ( model.shape == RenderInstanceShapeKind::ConvexHull && model.hull )
    {
        for ( uint16_t i = 0; i < model.hull->GetEdgeCount(); ++i )
        {
            const auto& segment = model.hull->GetEdge( i );
            edge( model.hull->GetPosition() + model.hull->GetVertex( segment.vertexA ),
                  model.hull->GetPosition() + model.hull->GetVertex( segment.vertexB ) );
        }
    }
    if ( count )
    {
        m_outlineShader->SetMat4( "uModelViewProjection", frame.projection * frame.view * model.transform );
        constexpr auto raster = MakePassRasterStateBucket( 0, { false, false, true, BlendFactor::SrcAlpha,
                                                                BlendFactor::OneMinusSrcAlpha, CullMode::None } );
        geometry.UploadAndDrawDynamicVB( m_outlineVB, std::span<const float>( vertices.data(), count ), raster );
    }
}

void PairedViewRenderer::DrawOutlines( const PairedViewFrame& frame, Dx12FrameOwner& commands, Dx12GeometryOwner& geometry,
                                       Dx12TextureOwner& textures )
{
    for ( int slot = 0; slot < 6; ++slot )
    {
        textures.BindTexture( 0, slot );
    }
    m_targets[2]->Bind();
    commands.SetViewport( 0, 0, m_targets[2]->GetWidth(), m_targets[2]->GetHeight() );
    commands.Clear( {} );
    if ( frame.mode == 1 )
    {
        textures.BindTexture( m_targets[0]->GetDepthTextureHandle(), 0 );
        textures.BindTexture( m_targets[1]->GetDepthTextureHandle(), 1 );
        m_outlineShader->Use();
        m_outlineShader->SetVec4( "uOutlineViewport", static_cast<float>( m_targets[2]->GetWidth() ),
                                  static_cast<float>( m_targets[2]->GetHeight() ), 0.9f,
                                  frame.occludedOutline ? 1.0f : 0.0f );
        for ( const auto& model : frame.models[0] )
        {
            DrawOutlineModel( model, frame, geometry );
        }
    }
    m_targets[2]->Unbind();
}
void PairedViewRenderer::Composite( const PairedViewFrame& frame, Dx12FrameOwner& commands, Dx12GeometryOwner& geometry,
                                    Dx12TextureOwner& textures )
{
    commands.SetViewport( frame.x, frame.y, frame.width, frame.height );
    textures.BindTexture( m_targets[0]->GetColorTextureHandle(), 0 );
    textures.BindTexture( m_targets[1]->GetColorTextureHandle(), 1 );
    textures.BindTexture( m_targets[0]->GetDepthTextureHandle(), 2 );
    textures.BindTexture( m_targets[1]->GetDepthTextureHandle(), 3 );
    textures.BindTexture( m_targets[2]->GetColorTextureHandle(), 4 );
    m_shader->Use();
    m_shader->SetVec4( "uPair", static_cast<float>( frame.mode ), frame.gain, frame.outlineAlpha,
                       frame.occludedOutline ? 1.0f : 0.0f );
    m_shader->SetVec4( "uTexel", 1.0f / m_targets[0]->GetWidth(), 1.0f / m_targets[0]->GetHeight(),
                       frame.stacked ? 1.0f : 0.0f, 0 );
    constexpr float quad[] = { -1, -1, 0, 1, 1, -1, 1, 1, 1, 1, 1, 0, -1, -1, 0, 1, 1, 1, 1, 0, -1, 1, 0, 0 };
    constexpr auto raster = MakePassRasterStateBucket( 0, { false, false, false } );
    geometry.UploadAndDrawDynamicVB( m_quad, quad, raster );
}
