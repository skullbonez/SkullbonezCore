#pragma once


// --- Includes ---
#include "SkullbonezCommon.h"
#include "SkullbonezMatrix4.h"

namespace SkullbonezCore
{
namespace Basics
{
class SkullbonezWindow
{

  private:
    inline static SkullbonezWindow* pInstance = nullptr;

    SkullbonezWindow();
    ~SkullbonezWindow();

  public:
    HWND m_sWindow;
    HDC m_sDevice;
    HGLRC m_sRenderContext;
    POINT m_sWindowDimensions;
    bool m_fIsFullScreenMode;

    Math::Transformation::Matrix4 projectionMatrix;

    static SkullbonezWindow* Instance();
    static void Destroy();
    void HandleScreenResize();
    bool SetupPixelFormat();
    void InitialiseOpenGL();
    void SetTitleText( const char* cText );
    const Math::Transformation::Matrix4& GetProjectionMatrix() const
    {
        return projectionMatrix;
    }
    void SetWindowDimensions( const RECT dimensions );
    void SetWindowDimensions( int width, int height );
    void CreateAppWindow( HINSTANCE hInstance, bool isFullScreenMode );
    void ChangeToFullScreen( int xResolution, int yResolution );
    int MsgBox( const char* cMsgBoxText, const char* cMsgBoxTitle, const UINT iMsgBoxType );
};
} // namespace Basics
} // namespace SkullbonezCore
