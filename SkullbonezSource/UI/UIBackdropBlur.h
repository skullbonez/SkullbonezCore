#pragma once

#include "UIDraw.h"
#include "../SkullbonezIShader.h"
#include <cstdint>
#include <memory>
#include <vector>

namespace SkullbonezCore
{
namespace UI
{

enum class UIBackdropBlurInvalidationReason : uint8_t
{
    Unknown,
    Visibility,
    WindowState,
    Bounds,
    Content,
    Toggle,
    ResourceReset
};

class UIBackdropBlur
{
  public:
    ~UIBackdropBlur();

    void Draw( const UIDrawContext& draw, const UIRect& bounds, int screenW, int screenH, int currentFrame, double now, bool enabled );
    void Invalidate( UIBackdropBlurInvalidationReason reason = UIBackdropBlurInvalidationReason::Unknown );
    void ResetResources();
    UIBackdropBlurInvalidationReason LastInvalidationReason() const;

  private:
    void EnsureDrawResources();
    void RefreshSourceTexture( const UIRect& bounds, int screenW, int screenH );

    std::unique_ptr<Rendering::IShader> m_shader;
    uint32_t m_dynamicVB = 0;
    uint32_t m_texture = 0;
    std::vector<uint8_t> m_sourcePixels;

    int m_textureW = 0;
    int m_textureH = 0;
    int m_lastScreenW = 0;
    int m_lastScreenH = 0;
    int m_lastX = -1;
    int m_lastY = -1;
    int m_lastW = 0;
    int m_lastH = 0;
    bool m_invalidated = true;
    UIBackdropBlurInvalidationReason m_lastInvalidationReason = UIBackdropBlurInvalidationReason::Unknown;
};

} // namespace UI
} // namespace SkullbonezCore
