/*------------------------------------------------------------------------------------------------------------------------------------------------------------------------
                                                                          THE SKULLBONEZ CORE
                                                                                _______
                                                                             .-"       "-.
                                                                            /             \
                                                                           /               \
                                                                           |   .--. .--.   |
                                                                           | )/   | |   \( |
                                                                           |/ \__/   \__/ \|
                                                                           /      /^\      \
                                                                           \__    '='    __/
                                                                             |\         /|
                                                                             |\'"VUUUV"'/|
                                                                             \ `"""""""` /
                                                                              `-._____.-'

                                                                         www.simoneschbach.com
-------------------------------------------------------------------------------------------------------------------------------------------------------------------------*/

#include "SkullbonezPendulumScene.h"

namespace SkullbonezCore
{
namespace Scenes
{
PendulumScene::PendulumScene( float gravity, int screenWidth, int screenHeight )
    : m_gravity( gravity ), m_screenWidth( screenWidth ), m_screenHeight( screenHeight )
{
}

PendulumScene::~PendulumScene()
{
}

void PendulumScene::Initialise()
{
    m_p2DRenderer = std::make_unique<Geometry::Renderer2D>();
    m_p2DRenderer->Initialise( m_screenWidth, m_screenHeight );

    m_pPendulum = std::make_unique<Physics::DoublePendulum>( 0.3f, 0.3f, 1.0f, 1.0f, m_gravity );
    m_pPendulum->SetInitialAngles( 1.2f, 0.4f );
}

void PendulumScene::Update( float deltaTime )
{
    if ( m_pPendulum )
    {
        m_pPendulum->Update( deltaTime );
    }
}

void PendulumScene::Render()
{
    if ( !m_pPendulum || !m_p2DRenderer )
    {
        return;
    }

    glClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT );

    m_p2DRenderer->BeginFrame( m_screenWidth, m_screenHeight );

    float pivotX = 0.5f;
    float pivotY = 0.15f;

    float pos1X = m_pPendulum->GetPosition1X( pivotX );
    float pos1Y = m_pPendulum->GetPosition1Y( pivotY );
    float pos2X = m_pPendulum->GetPosition2X( pivotX );
    float pos2Y = m_pPendulum->GetPosition2Y( pivotY );

    // Draw first rod
    m_p2DRenderer->DrawLine( pivotX, pivotY, pos1X, pos1Y, 1.0f, 0.0f, 0.0f, 1.0f );

    // Draw second rod
    m_p2DRenderer->DrawLine( pos1X, pos1Y, pos2X, pos2Y, 0.0f, 1.0f, 0.0f, 1.0f );

    // Draw pivot
    m_p2DRenderer->DrawCircle( pivotX, pivotY, 0.02f, 16, 1.0f, 1.0f, 1.0f );

    // Draw bob 1
    m_p2DRenderer->DrawCircle( pos1X, pos1Y, 0.02f, 16, 1.0f, 1.0f, 0.0f );

    // Draw bob 2
    m_p2DRenderer->DrawCircle( pos2X, pos2Y, 0.02f, 16, 1.0f, 0.0f, 1.0f );

    m_p2DRenderer->EndFrame();
}

void PendulumScene::Destroy()
{
    if ( m_p2DRenderer )
    {
        m_p2DRenderer->Destroy();
    }
}

} // namespace Scenes
} // namespace SkullbonezCore
