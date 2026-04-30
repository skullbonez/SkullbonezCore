#pragma once


// --- Includes ---
#include "SkullbonezCommon.h"
#include "SkullbonezMatrix4.h"
#include <d3d11.h>
#include <dxgi.h>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

namespace SkullbonezCore
{
namespace Basics
{
/* -- Skullbonez Window ------------------------------------------------------------------------------------------------------------------------------------------

    A singleton class representing a Windows OS application window with DirectX 11 rendering context.
-----------------------------------------------------------------------------------------------------------------------------------------------------------------*/
class SkullbonezWindow
{

  private:
    inline static SkullbonezWindow* pInstance = nullptr;

    SkullbonezWindow();  // Default constructor
    ~SkullbonezWindow(); // Default destructor

  public:
    HWND m_sWindow;                                    // Handle to window
    POINT m_sWindowDimensions;                         // Window m_width and m_height
    bool m_fIsFullScreenMode;                          // Flag for fullscreen mode
    ComPtr<ID3D11Device> m_d3dDevice;                  // DirectX 11 device
    ComPtr<ID3D11DeviceContext> m_d3dContext;          // DirectX 11 device context
    ComPtr<IDXGISwapChain> m_swapChain;                // DXGI swap chain for presentation
    ComPtr<ID3D11RenderTargetView> m_renderTargetView; // Render target view
    ComPtr<ID3D11DepthStencilView> m_depthStencilView; // Depth stencil view
    D3D_FEATURE_LEVEL m_featureLevel;                  // Supported DirectX feature level

    Math::Transformation::Matrix4 projectionMatrix; // Current perspective projection matrix

    static SkullbonezWindow* Instance();    // Call to request a pointer to the singleton instance
    static void Destroy();                  // Call to destroy the singleton instance
    void HandleScreenResize();              // Reset viewport and aspect ratio when the screen is resized
    void InitialiseDirectX();               // For all DirectX API initialisation code (after the window has been created)
    void SetTitleText( const char* cText ); // Draws text to title bar of window
    void Present();                         // Present the backbuffer to the screen
    const Math::Transformation::Matrix4& GetProjectionMatrix() const
    {
        return projectionMatrix;
    } // Returns the current perspective projection matrix
    void SetWindowDimensions( const RECT dimensions );                                       // Sets window dimensions by RECT struct
    void SetWindowDimensions( int width, int height );                                       // Sets window dimensions by integer values
    void CreateAppWindow( HINSTANCE hInstance, bool isFullScreenMode );                      // Creates our application window, returns a handle to it
    void ChangeToFullScreen( int xResolution, int yResolution );                             // Changes screeen to full screen mode
    int MsgBox( const char* cMsgBoxText, const char* cMsgBoxTitle, const UINT iMsgBoxType ); // Draws a message box to the screen
};
} // namespace Basics
} // namespace SkullbonezCore
