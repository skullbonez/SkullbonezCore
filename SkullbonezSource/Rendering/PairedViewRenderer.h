#pragma once
#include "RenderGraph.h"
#include "../Core/Config.h"
#include "RenderInstanceStore.h"
#include "ContactManifoldPresentation.h"
#include "DX12/FramebufferDX12.h"
#include "DX12/ShaderDX12.h"
#include <array>
#include <memory>
#include <span>

namespace SkullbonezCore::Math::CollisionDetection
{
class ConvexHullShape;
}

namespace SkullbonezCore::Rendering
{
class Dx12ResourceBuilder;
class Dx12GeometryOwner;
class Dx12TextureOwner;
class Dx12FrameOwner;
class Dx12GraphTransientPool;
class PrimitiveBatchRenderer;

struct ModelViewItem
{
    Math::Transformation::Matrix4 transform;
    RenderMaterial material;
    RenderInstanceShapeKind shape = RenderInstanceShapeKind::Box;
    const Math::CollisionDetection::ConvexHullShape* hull = nullptr;
};

// Invariant: model spans and hull pointers remain valid through synchronous
// DrawSide/Composite submission. Pixel mode excludes the separate contact overlay.
// Generic two-image presentation. The caller owns model identity, sampling and
// material choices; this renderer only draws immutable model lists and images.
struct PairedViewFrame
{
    std::array<std::span<const ModelViewItem>, 2> models;
    std::array<ContactManifoldPresentation, 2> contacts;
    Math::Transformation::Matrix4 view;
    Math::Transformation::Matrix4 projection;
    int x = 0, y = 0, width = 1, height = 1;
    int mode = 0; // 0 split, 1 outline overlay, 2 first, 3 second, 4 absolute RGB difference.
    float gain = 4.0f;
    float outlineAlpha = 0.65f;
    bool occludedOutline = false;
    bool stacked = false;
    int ImageWidth() const noexcept
    {
        return mode == 0 && !stacked ? width / 2 : width;
    }
    int ImageHeight() const noexcept
    {
        return mode == 0 && stacked ? height / 2 : height;
    }
};

class PairedViewRenderer
{
  public:
    bool Prepare( Dx12ResourceBuilder& resources, Dx12GeometryOwner& geometry, int width, int height );
    void Release( Dx12GeometryOwner& geometry );
    bool Ready() const noexcept
    {
        return m_targets[0] && m_targets[1] && m_shader && m_quad;
    }
    FramebufferDX12& Target( int side )
    {
        return *m_targets[side];
    }
    void DrawSide( const PairedViewFrame& frame, PrimitiveBatchRenderer& primitives,
                   const Core::OrdinaryRenderConfig& lighting, Dx12FrameOwner& commands, Dx12TextureOwner& textures,
                   int side );
    void Composite( const PairedViewFrame& frame, Dx12FrameOwner& commands, Dx12GeometryOwner& geometry,
                    Dx12TextureOwner& textures );

  private:
    void DrawModels( std::span<const ModelViewItem> models, const PairedViewFrame& frame, PrimitiveBatchRenderer& primitives,
                     const Core::OrdinaryRenderConfig& lighting );
    std::array<std::unique_ptr<FramebufferDX12>, 2> m_targets;
    std::unique_ptr<ShaderDX12> m_shader;
    uint32_t m_quad = 0;
};
} // namespace SkullbonezCore::Rendering
