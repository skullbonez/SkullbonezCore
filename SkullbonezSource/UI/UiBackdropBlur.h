#pragma once

#include "UiDraw.h"
#include "../SkullbonezIShader.h"
#include <cstdint>
#include <memory>
#include <vector>

namespace SkullbonezCore
{
namespace Ui
{

class UiBackdropBlur
{
  public:
    ~UiBackdropBlur();

    void Draw( const UiDrawContext& draw, const UiRect& bounds, int screenW, int screenH, int currentFrame, double now, bool enabled );
    void Invalidate();
    void ResetResources();

  private:
    void EnsureDrawResources();
    void RefreshTexture( const UiRect& bounds, int screenW, int screenH );
    void BlurPass( std::vector<uint8_t>& src, std::vector<uint8_t>& tmp, int width, int height );

    std::unique_ptr<Rendering::IShader> m_shader;
    uint32_t m_dynamicVB = 0;
    uint32_t m_texture = 0;
    std::vector<uint8_t> m_blurPixels;
    std::vector<uint8_t> m_scratchPixels;

    int m_textureW = 0;
    int m_textureH = 0;
    int m_lastScreenW = 0;
    int m_lastScreenH = 0;
    int m_lastX = -1;
    int m_lastY = -1;
    int m_lastW = 0;
    int m_lastH = 0;
    bool m_invalidated = true;
};

} // namespace Ui
} // namespace SkullbonezCore
