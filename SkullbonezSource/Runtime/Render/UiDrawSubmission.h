/*
File: SkullbonezSource/Runtime/Render/UiDrawSubmission.h
Purpose:
  Declares the renderer-owned translator from backend-neutral UI commands to
  DX12 text, geometry, and render-target-preview submissions.

Summary:
  UI authors a bounded UIDrawList without seeing renderer capabilities.
  UiDrawSubmission consumes that value stream during the late UI pass, owns the
  preview shader and dynamic vertex buffer, and releases them before backend
  teardown.

Glossary:
  Draw list: Ordered backend-neutral UI commands and copied text bytes.
  Immediate submitter: Stack-local pixel-to-text-space translator used while
    replaying one draw list.
  Preview identity: Stable catalog index recorded by UI and resolved against
    the current renderer snapshot only when submitted.

Invariants:
  - No renderer pointer or frame borrow is retained after Submit returns.
  - Preview resources are owned here and released before geometry teardown.
  - Preview commands split queued text/quad batches so authored order survives
    translation to the backend.
  - Draw trace and GPU timing labels remain stable validation vocabulary.

Related:
  - SkullbonezSource/UI/UIDrawList.h
  - SkullbonezSource/Runtime/Render/UiTextPass.cpp
  - Agentic/Reports/2026-07-25/ui-renderer-hard-boundary-closure.md
*/
#pragma once

#include <cstdint>
#include <memory>

namespace SkullbonezCore
{
namespace Assets
{
class AssetSystem;
}
namespace Rendering
{
class Dx12Diagnostics;
class Dx12GeometryOwner;
class Dx12ResourceBuilder;
class Dx12TextureOwner;
class RenderGpuTimingOwner;
class ShaderDX12;
} // namespace Rendering
namespace Text
{
class TextBatch;
}
namespace UI
{
class UIDrawList;
struct InGameUIFrameData;
} // namespace UI
namespace Runtime
{
struct RuntimeRenderTargetPreviewSnapshot;

class UiDrawSubmission
{
  public:
    UiDrawSubmission() = default;
    ~UiDrawSubmission();

    UiDrawSubmission( const UiDrawSubmission& ) = delete;
    UiDrawSubmission& operator=( const UiDrawSubmission& ) = delete;

    void Submit( const UI::UIDrawList& drawList,
                 Text::TextBatch& textBatch,
                 Rendering::RenderGpuTimingOwner* gpuTiming,
                 Rendering::Dx12TextureOwner& renderTextures,
                 Rendering::Dx12GeometryOwner& renderGeometry,
                 Rendering::Dx12Diagnostics& renderDiagnostics,
                 int screenW,
                 int screenH );

    void SubmitWithPreviews( const UI::UIDrawList& drawList,
                             const RuntimeRenderTargetPreviewSnapshot& previewData,
                             Text::TextBatch& textBatch,
                             Rendering::RenderGpuTimingOwner* gpuTiming,
                             Assets::AssetSystem& assets,
                             Rendering::Dx12ResourceBuilder& renderResources,
                             Rendering::Dx12TextureOwner& renderTextures,
                             Rendering::Dx12GeometryOwner& renderGeometry,
                             Rendering::Dx12Diagnostics& renderDiagnostics,
                             int screenW,
                             int screenH );

    void ReleaseGpuResources( Rendering::Dx12GeometryOwner* renderGeometry );

  private:
    void SubmitCommands( const UI::UIDrawList& drawList,
                         const RuntimeRenderTargetPreviewSnapshot* previewData,
                         Text::TextBatch& textBatch,
                         Rendering::RenderGpuTimingOwner* gpuTiming,
                         Assets::AssetSystem* assets,
                         Rendering::Dx12ResourceBuilder* renderResources,
                         Rendering::Dx12TextureOwner& renderTextures,
                         Rendering::Dx12GeometryOwner& renderGeometry,
                         Rendering::Dx12Diagnostics& renderDiagnostics,
                         int screenW,
                         int screenH );

    void EnsurePreviewResources( Assets::AssetSystem& assets,
                                 Rendering::Dx12ResourceBuilder& renderResources,
                                 Rendering::Dx12GeometryOwner& renderGeometry );

    std::unique_ptr<Rendering::ShaderDX12> m_previewShader;
    uint32_t m_previewVertexBuffer = 0;
};
} // namespace Runtime
} // namespace SkullbonezCore
