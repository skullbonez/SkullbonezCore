/* -- INCLUDES --------------------------------------------------------------------*/
#include "SkullbonezTestScene.h"
#include <cstring>

/* -- USING CLAUSES ---------------------------------------------------------------*/
using namespace SkullbonezCore::Basics;

/* -- DEFAULT CONSTRUCTOR ---------------------------------------------------------*/
TestScene::TestScene()
{
    m_isPhysicsEnabled = true;
    m_isTextEnabled = true;
    m_isGlResetTest = false;
    m_frameCount = -1;
    m_screenshotPath[0] = '\0';
    m_perfLogPath[0] = '\0';
    m_screenshotFrame = -1;
    m_screenshotMs = -1;
    m_seed = 0;
    m_legacyBallCount = 0;
}

/* -- LOAD FROM FILE --------------------------------------------------------------*/
TestScene TestScene::LoadFromFile( const char* path )
{
    TestScene scene;

    FILE* file = nullptr;
    errno_t err = fopen_s( &file, path, "r" );
    if ( err != 0 || !file )
    {
        char msg[256];
        sprintf_s( msg, sizeof( msg ), "Failed to open scene file: %s  (TestScene::LoadFromFile)", path );
        throw std::runtime_error( msg );
    }

    char line[512];
    int lineNumber = 0;

    while ( fgets( line, sizeof( line ), file ) )
    {
        ++lineNumber;

        // strip newline
        size_t len = strlen( line );
        if ( len > 0 && line[len - 1] == '\n' )
        {
            line[len - 1] = '\0';
        }
        if ( len > 1 && line[len - 2] == '\r' )
        {
            line[len - 2] = '\0';
        }

        // skip blank lines and comments
        if ( line[0] == '\0' || line[0] == '#' )
        {
            continue;
        }

        // parse physics directive
        if ( strncmp( line, "physics ", 8 ) == 0 )
        {
            if ( strcmp( line + 8, "off" ) == 0 )
            {
                scene.m_isPhysicsEnabled = false;
            }
            else if ( strcmp( line + 8, "on" ) == 0 )
            {
                scene.m_isPhysicsEnabled = true;
            }
            else
            {
                fclose( file );
                char msg[256];
                sprintf_s( msg, sizeof( msg ), "Invalid physics value at line %d: %s  (TestScene::LoadFromFile)", lineNumber, line + 8 );
                throw std::runtime_error( msg );
            }
            continue;
        }

        // parse text directive
        if ( strncmp( line, "text ", 5 ) == 0 )
        {
            if ( strcmp( line + 5, "off" ) == 0 )
            {
                scene.m_isTextEnabled = false;
            }
            else if ( strcmp( line + 5, "on" ) == 0 )
            {
                scene.m_isTextEnabled = true;
            }
            else
            {
                fclose( file );
                char msg[256];
                sprintf_s( msg, sizeof( msg ), "Invalid text value at line %d: %s  (TestScene::LoadFromFile)", lineNumber, line + 5 );
                throw std::runtime_error( msg );
            }
            continue;
        }

        // parse frames directive
        if ( strncmp( line, "frames ", 7 ) == 0 )
        {
            if ( strcmp( line + 7, "unlimited" ) == 0 )
            {
                scene.m_frameCount = -1;
            }
            else
            {
                scene.m_frameCount = atoi( line + 7 );
                if ( scene.m_frameCount <= 0 )
                {
                    fclose( file );
                    char msg[256];
                    sprintf_s( msg, sizeof( msg ), "Invalid frame count at line %d: %s  (TestScene::LoadFromFile)", lineNumber, line + 7 );
                    throw std::runtime_error( msg );
                }
            }
            continue;
        }

        // parse screenshot directive: screenshot <path> frame <N> | screenshot <path> ms <N>
        if ( strncmp( line, "screenshot ", 11 ) == 0 )
        {
            char outPath[256] = {};
            char triggerType[16] = {};
            int triggerValue = 0;
            int parsed = sscanf_s( line + 11, "%255s %15s %d", outPath, static_cast<unsigned>( sizeof( outPath ) ), triggerType, static_cast<unsigned>( sizeof( triggerType ) ), &triggerValue );

            if ( parsed != 3 || triggerValue <= 0 )
            {
                fclose( file );
                char msg[256];
                sprintf_s( msg, sizeof( msg ), "Invalid screenshot at line %d (expected: screenshot <path> frame|ms <N>)  (TestScene::LoadFromFile)", lineNumber );
                throw std::runtime_error( msg );
            }

            strcpy_s( scene.m_screenshotPath, sizeof( scene.m_screenshotPath ), outPath );

            if ( strcmp( triggerType, "frame" ) == 0 )
            {
                scene.m_screenshotFrame = triggerValue;
                scene.m_screenshotMs = -1;
            }
            else if ( strcmp( triggerType, "ms" ) == 0 )
            {
                scene.m_screenshotMs = triggerValue;
                scene.m_screenshotFrame = -1;
            }
            else
            {
                fclose( file );
                char msg[256];
                sprintf_s( msg, sizeof( msg ), "Invalid screenshot trigger '%s' at line %d (expected 'frame' or 'ms')  (TestScene::LoadFromFile)", triggerType, lineNumber );
                throw std::runtime_error( msg );
            }
            continue;
        }

        // parse seed directive
        if ( strncmp( line, "seed ", 5 ) == 0 )
        {
            scene.m_seed = static_cast<unsigned int>( atoi( line + 5 ) );
            if ( scene.m_seed == 0 )
            {
                fclose( file );
                char msg[256];
                sprintf_s( msg, sizeof( msg ), "Invalid seed at line %d (must be > 0)  (TestScene::LoadFromFile)", lineNumber );
                throw std::runtime_error( msg );
            }
            continue;
        }

        // parse legacy_balls directive
        if ( strncmp( line, "legacy_balls ", 13 ) == 0 )
        {
            scene.m_legacyBallCount = atoi( line + 13 );
            if ( scene.m_legacyBallCount <= 0 )
            {
                fclose( file );
                char msg[256];
                sprintf_s( msg, sizeof( msg ), "Invalid legacy_balls count at line %d (must be > 0)  (TestScene::LoadFromFile)", lineNumber );
                throw std::runtime_error( msg );
            }
            continue;
        }

        // parse test_gl_reset directive
        if ( strcmp( line, "test_gl_reset" ) == 0 )
        {
            scene.m_isGlResetTest = true;
            continue;
        }

        // parse perf_log directive
        if ( strncmp( line, "perf_log ", 9 ) == 0 )
        {
            strcpy_s( scene.m_perfLogPath, sizeof( scene.m_perfLogPath ), line + 9 );
            continue;
        }

        // parse camera line
        if ( strncmp( line, "camera ", 7 ) == 0 )
        {
            if ( static_cast<int>( scene.m_cameras.size() ) >= TOTAL_CAMERA_COUNT )
            {
                fclose( file );
                char msg[256];
                sprintf_s( msg, sizeof( msg ), "Too many cameras at line %d (max %d)  (TestScene::LoadFromFile)", lineNumber, TOTAL_CAMERA_COUNT );
                throw std::runtime_error( msg );
            }

            SceneCamera cam;
            memset( &cam, 0, sizeof( cam ) );

            int parsed = sscanf_s( line + 7, "%63s %f %f %f %f %f %f %f %f %f", cam.name, static_cast<unsigned>( sizeof( cam.name ) ), &cam.m_position.x, &cam.m_position.y, &cam.m_position.z, &cam.view.x, &cam.view.y, &cam.view.z, &cam.up.x, &cam.up.y, &cam.up.z );

            if ( parsed != 10 )
            {
                fclose( file );
                char msg[256];
                sprintf_s( msg, sizeof( msg ), "Invalid camera at line %d (expected 10 fields, got %d)  (TestScene::LoadFromFile)", lineNumber, parsed );
                throw std::runtime_error( msg );
            }

            scene.m_cameras.push_back( cam );
            continue;
        }

        // parse ball line
        if ( strncmp( line, "ball ", 5 ) == 0 )
        {
            SceneBall ball;
            memset( &ball, 0, sizeof( ball ) );

            // try full line (with force)
            int parsed = sscanf_s( line + 5, "%63s %f %f %f %f %f %f %f %f %f %f %f %f %f", ball.name, static_cast<unsigned>( sizeof( ball.name ) ), &ball.posX, &ball.posY, &ball.posZ, &ball.m_radius, &ball.m_mass, &ball.moment, &ball.restitution, &ball.forceX, &ball.forceY, &ball.forceZ, &ball.forcePosX, &ball.forcePosY, &ball.forcePosZ );

            if ( parsed != 14 && parsed != 8 )
            {
                fclose( file );
                char msg[256];
                sprintf_s( msg, sizeof( msg ), "Invalid ball at line %d (expected 8 or 14 fields, got %d)  (TestScene::LoadFromFile)", lineNumber, parsed );
                throw std::runtime_error( msg );
            }

            scene.m_balls.push_back( ball );
            continue;
        }

        // unknown directive
        fclose( file );
        char msg[256];
        sprintf_s( msg, sizeof( msg ), "Unknown directive at line %d: %.64s  (TestScene::LoadFromFile)", lineNumber, line );
        throw std::runtime_error( msg );
    }

    fclose( file );

    // validate
    if ( scene.m_cameras.empty() )
    {
        throw std::runtime_error( "Scene file must define at least one camera.  (TestScene::LoadFromFile)" );
    }

    return scene;
}

/* -- IS PHYSICS ENABLED ----------------------------------------------------------*/
bool TestScene::IsPhysicsEnabled() const
{
    return m_isPhysicsEnabled;
}

/* -- IS TEXT ENABLED -------------------------------------------------------------*/
bool TestScene::IsTextEnabled() const
{
    return m_isTextEnabled;
}

/* -- IS GL RESET TEST ------------------------------------------------------------*/
bool TestScene::IsGlResetTest() const
{
    return m_isGlResetTest;
}

/* -- GET FRAME COUNT -------------------------------------------------------------*/
int TestScene::GetFrameCount() const
{
    return m_frameCount;
}

/* -- GET SCREENSHOT PATH ---------------------------------------------------------*/
const char* TestScene::GetScreenshotPath() const
{
    return m_screenshotPath;
}

/* -- GET SCREENSHOT FRAME --------------------------------------------------------*/
int TestScene::GetScreenshotFrame() const
{
    return m_screenshotFrame;
}

/* -- GET SCREENSHOT MS -----------------------------------------------------------*/
int TestScene::GetScreenshotMs() const
{
    return m_screenshotMs;
}

/* -- GET SEED --------------------------------------------------------------------*/
unsigned int TestScene::GetSeed() const
{
    return m_seed;
}

/* -- GET LEGACY BALL COUNT -------------------------------------------------------*/
int TestScene::GetLegacyBallCount() const
{
    return m_legacyBallCount;
}

/* -- GET PERF LOG PATH -----------------------------------------------------------*/
const char* TestScene::GetPerfLogPath() const
{
    return m_perfLogPath;
}

/* -- GET CAMERA COUNT ------------------------------------------------------------*/
int TestScene::GetCameraCount() const
{
    return static_cast<int>( m_cameras.size() );
}

/* -- GET BALL COUNT --------------------------------------------------------------*/
int TestScene::GetBallCount() const
{
    return static_cast<int>( m_balls.size() );
}

/* -- GET CAMERA ------------------------------------------------------------------*/
const SceneCamera& TestScene::GetCamera( int index ) const
{
    if ( index < 0 || index >= static_cast<int>( m_cameras.size() ) )
    {
        throw std::runtime_error( "Camera index out of range.  (TestScene::GetCamera)" );
    }

    return m_cameras[index];
}

/* -- GET BALL --------------------------------------------------------------------*/
const SceneBall& TestScene::GetBall( int index ) const
{
    if ( index < 0 || index >= static_cast<int>( m_balls.size() ) )
    {
        throw std::runtime_error( "Ball index out of range.  (TestScene::GetBall)" );
    }

    return m_balls[index];
}
