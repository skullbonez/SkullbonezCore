/*-----------------------------------------------------------------------------------
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
-----------------------------------------------------------------------------------*/


/* -- INCLUDES --------------------------------------------------------------------*/
#include "SkullbonezCommon.h"
#include "SkullbonezRun.h"
#include "SkullbonezWindow.h"
#include "SkullbonezTimer.h"
#include <float.h>
#include <cstring>
#include <vector>
#include <string>


/* -- USING CLAUSES ---------------------------------------------------------------*/
using namespace SkullbonezCore::Basics;


/* -- WIN MAIN --------------------------------------------------------------------*/
// "Windows Main" - this function is the execution entry point for the application
int WINAPI WinMain( HINSTANCE hInstance,     // Holds info on instance of app
                    HINSTANCE hPrevInstance, // Some useless Win32 junk
                    PSTR szCmdLine,          // Params passed from command line
                    int iCmdShow )           // Window state (maximised, hide etc)
{
    // Heap debug code - breaks program at specified allocation
    // _CrtSetBreakAlloc(89);

    // floating point check routine
    // _controlfp(0, _MCW_EM ^ _EM_INEXACT);

    // Supress benign (level 4) warnings
    hPrevInstance;
    iCmdShow;

    // Build the ordered list of scene paths to run.
    // Each entry is either a .scene path (scene/suite mode) or "" (legacy mode).
    std::vector<std::string> sceneList;
    bool isSuiteOrSceneMode = false;

    if ( szCmdLine && szCmdLine[0] != '\0' )
    {
        const char* suiteArg = strstr( szCmdLine, "--suite" );
        const char* sceneArg = strstr( szCmdLine, "--scene" );

        if ( suiteArg )
        {
            suiteArg += 7;
            while ( *suiteArg == ' ' )
            {
                ++suiteArg;
            }

            // Read suite file: one scene path per line, # comments ignored
            FILE* f = nullptr;
            if ( fopen_s( &f, suiteArg, "r" ) == 0 && f )
            {
                char line[512];
                while ( fgets( line, sizeof( line ), f ) )
                {
                    size_t len = strlen( line );
                    while ( len > 0 && ( line[len - 1] == '\r' || line[len - 1] == '\n' || line[len - 1] == ' ' ) )
                    {
                        line[--len] = '\0';
                    }
                    if ( len > 0 && line[0] != '#' )
                    {
                        sceneList.push_back( line );
                    }
                }
                fclose( f );
            }
            isSuiteOrSceneMode = true;
        }
        else if ( sceneArg )
        {
            sceneArg += 7;
            while ( *sceneArg == ' ' )
            {
                ++sceneArg;
            }
            if ( *sceneArg != '\0' )
            {
                sceneList.push_back( sceneArg );
                isSuiteOrSceneMode = true;
            }
        }
    }

    if ( sceneList.empty() )
    {
        sceneList.push_back( "" ); // legacy mode — empty string maps to nullptr
    }

    Cfg().Load( "SkullbonezData/engine.cfg" );

    // Create an instance of our window class
    // (holds everything associated with our window)
    SkullbonezWindow* m_cWindow = SkullbonezWindow::Instance();

    // Create the application window
    m_cWindow->CreateAppWindow( hInstance, Cfg().fullscreen );

    // Get the device context for our window
    m_cWindow->m_sDevice = GetDC( m_cWindow->m_sWindow );

    bool abortAll = false;
    for ( size_t sceneIdx = 0; sceneIdx < sceneList.size() && !abortAll; ++sceneIdx )
    {
        const char* currentScene = sceneList[sceneIdx].empty() ? nullptr : sceneList[sceneIdx].c_str();

        for ( ;; )
        {
            // Init OpenGL
            m_cWindow->InitialiseOpenGL();

            bool shouldRestart = false;
            bool sceneCompleted = false;
            {
                // Create the Skullbonez Core instance (scoped so destructor runs
                // BEFORE GL context deletion — ensures GL cleanup calls work)
                SkullbonezRun cRun( currentScene );

                try
                {
                    // Attempt to initialise the Skullbonez Core
                    cRun.Initialise();

                    // Attempt to run the Skullbonez Core
                    if ( !cRun.Run() )
                    {
                        if ( currentScene )
                        {
                            sceneCompleted = true;
                            break; // clean scene exit — move to next scene
                        }
                        else
                        {
                            throw std::runtime_error( "Thanks for using the Skullbonez Core!" );
                        }
                    }

                    shouldRestart = true;
                }
                catch ( const std::exception& e ) // Catch all exceptions thrown by the Skullbonez Core
                {
                    if ( !isSuiteOrSceneMode )
                    {
                        m_cWindow->MsgBox( e.what(), "Alert!", MB_OK );
                    }

                    abortAll = true;
                    break;
                }
                // cRun destroyed here — GL context still alive for proper cleanup
            }

            // Cleanup rendering context AFTER cRun is destroyed
            if ( shouldRestart && m_cWindow->m_sRenderContext )
            {
                wglMakeCurrent( NULL, NULL );
                wglDeleteContext( m_cWindow->m_sRenderContext );
            }
            else
            {
                break;
            }
        }

        // Between scenes: destroy GL context so the next scene gets a fresh one
        if ( !abortAll && sceneIdx + 1 < sceneList.size() && m_cWindow->m_sRenderContext )
        {
            wglMakeCurrent( NULL, NULL );
            wglDeleteContext( m_cWindow->m_sRenderContext );
        }
    }

    // Cleanup rendering context
    if ( m_cWindow->m_sRenderContext )
    {
        // Free rendering memory, rollback all changes
        wglMakeCurrent( NULL, NULL );

        // Delete the rendering context
        wglDeleteContext( m_cWindow->m_sRenderContext );
    }

    // Cleanup device context (Free device context associated with our window)
    if ( m_cWindow->m_sDevice )
    {
        ReleaseDC( m_cWindow->m_sWindow,
                   m_cWindow->m_sDevice );
    }

    // Restore desktop settings
    if ( m_cWindow->m_fIsFullScreenMode )
    {
        ChangeDisplaySettings( NULL, 0 ); // Switch back to desktop mode
        ShowCursor( true );               // Bring mouse pointer back
    }

    // Free up memory associated with the window class
    UnregisterClass( WINDOW_NAME, hInstance );

    // Delete our window class
    m_cWindow->Destroy();

    // Write memory leaks to output window
    // _CrtDumpMemoryLeaks();

    // Return wParam of our msg struct
    return 0;
}