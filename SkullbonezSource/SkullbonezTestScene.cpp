// --- Includes ---
#include "SkullbonezTestScene.h"
#include <cstring>


// --- Usings ---
using namespace SkullbonezCore::Basics;


TestScene::TestScene()
{
}


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
                scene.m_sceneOptions.isPhysicsEnabled = false;
            }
            else if ( strcmp( line + 8, "on" ) == 0 )
            {
                scene.m_sceneOptions.isPhysicsEnabled = true;
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
                scene.m_sceneOptions.isTextEnabled = false;
            }
            else if ( strcmp( line + 5, "on" ) == 0 )
            {
                scene.m_sceneOptions.isTextEnabled = true;
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

        // parse text_only directive: suppress all 3D rendering, show a solid background with large text
        if ( strncmp( line, "text_only ", 10 ) == 0 )
        {
            if ( strcmp( line + 10, "on" ) == 0 )
            {
                scene.m_sceneOptions.isTextOnly = true;
            }
            else if ( strcmp( line + 10, "off" ) == 0 )
            {
                scene.m_sceneOptions.isTextOnly = false;
            }
            else
            {
                fclose( file );
                char msg[256];
                sprintf_s( msg, sizeof( msg ), "Invalid text_only value at line %d: %s  (TestScene::LoadFromFile)", lineNumber, line + 10 );
                throw std::runtime_error( msg );
            }
            continue;
        }

        // parse frames directive
        if ( strncmp( line, "frames ", 7 ) == 0 )
        {
            if ( strcmp( line + 7, "unlimited" ) == 0 )
            {
                scene.m_sceneOptions.frameCount = -1;
            }
            else
            {
                scene.m_sceneOptions.frameCount = atoi( line + 7 );
                if ( scene.m_sceneOptions.frameCount <= 0 && strcmp( line + 7, "-1" ) != 0 )
                {
                    fclose( file );
                    char msg[256];
                    sprintf_s( msg, sizeof( msg ), "Invalid frame count at line %d: %s  (TestScene::LoadFromFile)", lineNumber, line + 7 );
                    throw std::runtime_error( msg );
                }
                if ( scene.m_sceneOptions.frameCount <= 0 )
                {
                    scene.m_sceneOptions.frameCount = -1;
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

            strcpy_s( scene.m_captureOptions.screenshotPath, sizeof( scene.m_captureOptions.screenshotPath ), outPath );

            if ( strcmp( triggerType, "frame" ) == 0 )
            {
                scene.m_captureOptions.screenshotFrame = triggerValue;
                scene.m_captureOptions.screenshotMs = -1;
            }
            else if ( strcmp( triggerType, "ms" ) == 0 )
            {
                scene.m_captureOptions.screenshotMs = triggerValue;
                scene.m_captureOptions.screenshotFrame = -1;
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
            scene.m_sceneOptions.seed = static_cast<unsigned int>( atoi( line + 5 ) );
            if ( scene.m_sceneOptions.seed == 0 )
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
            scene.m_sceneOptions.legacyBallCount = atoi( line + 13 );
            if ( scene.m_sceneOptions.legacyBallCount <= 0 )
            {
                fclose( file );
                char msg[256];
                sprintf_s( msg, sizeof( msg ), "Invalid legacy_balls count at line %d (must be > 0)  (TestScene::LoadFromFile)", lineNumber );
                throw std::runtime_error( msg );
            }
            continue;
        }

        // parse perf_log directive
        if ( strncmp( line, "perf_log ", 9 ) == 0 )
        {
            strcpy_s( scene.m_loggingOptions.perfLogPath, sizeof( scene.m_loggingOptions.perfLogPath ), line + 9 );
            continue;
        }

        // parse perf_log_flush directive
        if ( strncmp( line, "perf_log_flush ", 15 ) == 0 )
        {
            if ( strcmp( line + 15, "on" ) == 0 )
            {
                scene.m_loggingOptions.isPerfLogFlush = true;
            }
            else if ( strcmp( line + 15, "off" ) == 0 )
            {
                scene.m_loggingOptions.isPerfLogFlush = false;
            }
            else
            {
                fclose( file );
                char msg[256];
                sprintf_s( msg, sizeof( msg ), "Invalid perf_log_flush value at line %d: %s  (TestScene::LoadFromFile)", lineNumber, line + 15 );
                throw std::runtime_error( msg );
            }
            continue;
        }

        // parse perf_log_flush_interval directive
        if ( strncmp( line, "perf_log_flush_interval ", 24 ) == 0 )
        {
            scene.m_loggingOptions.perfLogFlushInterval = atoi( line + 24 );
            if ( scene.m_loggingOptions.perfLogFlushInterval < 0 )
            {
                fclose( file );
                char msg[256];
                sprintf_s( msg, sizeof( msg ), "Invalid perf_log_flush_interval at line %d (must be >= 0)  (TestScene::LoadFromFile)", lineNumber );
                throw std::runtime_error( msg );
            }
            continue;
        }

        // parse vsync directive
        if ( strncmp( line, "vsync ", 6 ) == 0 )
        {
            scene.m_runtimeOverrides.hasVsyncOverride = true;
            if ( strcmp( line + 6, "on" ) == 0 )
            {
                scene.m_runtimeOverrides.isVsyncEnabled = true;
            }
            else if ( strcmp( line + 6, "off" ) == 0 )
            {
                scene.m_runtimeOverrides.isVsyncEnabled = false;
            }
            else
            {
                fclose( file );
                char msg[256];
                sprintf_s( msg, sizeof( msg ), "Invalid vsync value at line %d: %s  (TestScene::LoadFromFile)", lineNumber, line + 6 );
                throw std::runtime_error( msg );
            }
            continue;
        }

        // parse pipeline_sync directive
        if ( strncmp( line, "pipeline_sync ", 14 ) == 0 )
        {
            scene.m_runtimeOverrides.hasPipelineSyncOverride = true;
            if ( strcmp( line + 14, "on" ) == 0 )
            {
                scene.m_runtimeOverrides.isPipelineSyncEnabled = true;
            }
            else if ( strcmp( line + 14, "off" ) == 0 )
            {
                scene.m_runtimeOverrides.isPipelineSyncEnabled = false;
            }
            else
            {
                fclose( file );
                char msg[256];
                sprintf_s( msg, sizeof( msg ), "Invalid pipeline_sync value at line %d: %s  (TestScene::LoadFromFile)", lineNumber, line + 14 );
                throw std::runtime_error( msg );
            }
            continue;
        }

        // parse roll_align directive
        if ( strncmp( line, "roll_align ", 11 ) == 0 )
        {
            scene.m_runtimeOverrides.hasRollAlignOverride = true;
            if ( strcmp( line + 11, "on" ) == 0 )
            {
                scene.m_runtimeOverrides.isRollAlignEnabled = true;
            }
            else if ( strcmp( line + 11, "off" ) == 0 )
            {
                scene.m_runtimeOverrides.isRollAlignEnabled = false;
            }
            else
            {
                fclose( file );
                char msg[256];
                sprintf_s( msg, sizeof( msg ), "Invalid roll_align value at line %d: %s  (TestScene::LoadFromFile)", lineNumber, line + 11 );
                throw std::runtime_error( msg );
            }
            continue;
        }

        // parse screenshot_and_exit directive: capture frame 1 as SCENENAME.bmp then quit
        if ( strcmp( line, "screenshot_and_exit" ) == 0 )
        {
            scene.m_sceneOptions.screenshotAndExit = true;
            continue;
        }

        // parse exit_on_complete directive: automatically exit when targetFrameCount is reached
        if ( strcmp( line, "exit_on_complete" ) == 0 )
        {
            scene.m_sceneOptions.exitOnComplete = true;
            continue;
        }

        // parse screenshot_interval directive: screenshot_interval <dir> <N>
        if ( strncmp( line, "screenshot_interval ", 20 ) == 0 )
        {
            char outDir[256] = {};
            int intervalFrames = 0;
            int parsed = sscanf_s( line + 20, "%255s %d", outDir, static_cast<unsigned>( sizeof( outDir ) ), &intervalFrames );

            if ( parsed != 2 || intervalFrames <= 0 )
            {
                fclose( file );
                char msg[256];
                sprintf_s( msg, sizeof( msg ), "Invalid screenshot_interval at line %d (expected: screenshot_interval <dir> <N>)  (TestScene::LoadFromFile)", lineNumber );
                throw std::runtime_error( msg );
            }

            strcpy_s( scene.m_captureOptions.screenshotDir, sizeof( scene.m_captureOptions.screenshotDir ), outDir );
            scene.m_captureOptions.screenshotInterval = intervalFrames;
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
            ball.hasInitOrient = false;

            // try full line (with force + euler orient)
            int parsed = sscanf_s( line + 5, "%63s %f %f %f %f %f %f %f %f %f %f %f %f %f %f %f %f", ball.name, static_cast<unsigned>( sizeof( ball.name ) ), &ball.posX, &ball.posY, &ball.posZ, &ball.m_radius, &ball.m_mass, &ball.moment, &ball.restitution, &ball.forceX, &ball.forceY, &ball.forceZ, &ball.forcePosX, &ball.forcePosY, &ball.forcePosZ, &ball.eulerX, &ball.eulerY, &ball.eulerZ );

            if ( parsed == 17 )
            {
                ball.hasInitOrient = true;
            }
            else if ( parsed != 14 )
            {
                // Try no-force format (8 base + optional 3 euler)
                parsed = sscanf_s( line + 5, "%63s %f %f %f %f %f %f %f %f %f %f", ball.name, static_cast<unsigned>( sizeof( ball.name ) ), &ball.posX, &ball.posY, &ball.posZ, &ball.m_radius, &ball.m_mass, &ball.moment, &ball.restitution, &ball.eulerX, &ball.eulerY, &ball.eulerZ );

                if ( parsed == 11 )
                {
                    ball.hasInitOrient = true;
                }
                else if ( parsed != 8 )
                {
                    fclose( file );
                    char msg[256];
                    sprintf_s( msg, sizeof( msg ), "Invalid ball at line %d (expected 8, 11, 14 or 17 fields, got %d)  (TestScene::LoadFromFile)", lineNumber, parsed );
                    throw std::runtime_error( msg );
                }
            }

            scene.m_balls.push_back( ball );
            continue;
        }

        // parse box line: box <name> <posX> <posY> <posZ> <halfX> <halfY> <halfZ> <mass> <restitution> [eulerX eulerY eulerZ] [velX velY velZ]
        if ( strncmp( line, "box ", 4 ) == 0 )
        {
            SceneBox box;
            memset( &box, 0, sizeof( box ) );
            box.hasInitOrient = false;
            box.hasInitVelocity = false;

            // Try full line: 9 base + 3 euler + 3 velocity = 15 fields
            int parsed = sscanf_s( line + 4, "%63s %f %f %f %f %f %f %f %f %f %f %f %f %f %f", box.name, static_cast<unsigned>( sizeof( box.name ) ), &box.posX, &box.posY, &box.posZ, &box.halfX, &box.halfY, &box.halfZ, &box.mass, &box.restitution, &box.eulerX, &box.eulerY, &box.eulerZ, &box.velX, &box.velY, &box.velZ );

            if ( parsed == 15 )
            {
                box.hasInitOrient = true;
                box.hasInitVelocity = true;
            }
            else if ( parsed == 12 )
            {
                box.hasInitOrient = true;
            }
            else if ( parsed != 9 )
            {
                fclose( file );
                char msg[256];
                sprintf_s( msg, sizeof( msg ), "Invalid box at line %d (expected 9, 12, or 15 fields, got %d)  (TestScene::LoadFromFile)", lineNumber, parsed );
                throw std::runtime_error( msg );
            }

            scene.m_boxes.push_back( box );
            continue;
        }

        // parse time_scale directive
        if ( strncmp( line, "time_scale ", 11 ) == 0 )
        {
            float val = static_cast<float>( atof( line + 11 ) );
            if ( val <= 0.0f )
            {
                fclose( file );
                char msg[256];
                sprintf_s( msg, sizeof( msg ), "Invalid time_scale at line %d (must be > 0)  (TestScene::LoadFromFile)", lineNumber );
                throw std::runtime_error( msg );
            }
            scene.m_sceneOptions.timeScale = val;
            continue;
        }

        // parse fixed_step directive — one physics tick per render frame at PHYSICS_FIXED_DT
        if ( strcmp( line, "fixed_step" ) == 0 )
        {
            scene.m_sceneOptions.isFixedStep = true;
            continue;
        }

        // parse debug_vectors directive
        if ( strncmp( line, "debug_vectors ", 14 ) == 0 )
        {
            if ( strcmp( line + 14, "on" ) == 0 )
            {
                scene.m_sceneOptions.isDebugVectors = true;
            }
            else if ( strcmp( line + 14, "off" ) == 0 )
            {
                scene.m_sceneOptions.isDebugVectors = false;
            }
            else
            {
                fclose( file );
                char msg[256];
                sprintf_s( msg, sizeof( msg ), "Invalid debug_vectors value at line %d  (TestScene::LoadFromFile)", lineNumber );
                throw std::runtime_error( msg );
            }
            continue;
        }

        // parse physics_debug directive: none|axes|contacts|sleep|all
        if ( strncmp( line, "physics_debug ", 14 ) == 0 )
        {
            const char* value = line + 14;
            if ( strcmp( value, "none" ) == 0 || strcmp( value, "off" ) == 0 )
            {
                scene.m_sceneOptions.physicsDebugFlags = Physics::PHYSICS_DEBUG_NONE;
            }
            else if ( strcmp( value, "axes" ) == 0 )
            {
                scene.m_sceneOptions.physicsDebugFlags = Physics::PHYSICS_DEBUG_AXES;
            }
            else if ( strcmp( value, "contacts" ) == 0 )
            {
                scene.m_sceneOptions.physicsDebugFlags = Physics::PHYSICS_DEBUG_CONTACTS;
            }
            else if ( strcmp( value, "sleep" ) == 0 )
            {
                scene.m_sceneOptions.physicsDebugFlags = Physics::PHYSICS_DEBUG_SLEEP;
            }
            else if ( strcmp( value, "all" ) == 0 || strcmp( value, "on" ) == 0 )
            {
                scene.m_sceneOptions.physicsDebugFlags = Physics::PHYSICS_DEBUG_ALL;
            }
            else
            {
                fclose( file );
                char msg[256];
                sprintf_s( msg, sizeof( msg ), "Invalid physics_debug value at line %d  (TestScene::LoadFromFile)", lineNumber );
                throw std::runtime_error( msg );
            }
            continue;
        }

        if ( strncmp( line, "physics_debug_axes ", 19 ) == 0 ||
             strncmp( line, "physics_debug_contacts ", 23 ) == 0 ||
             strncmp( line, "physics_debug_sleep ", 20 ) == 0 )
        {
            const bool isAxes = strncmp( line, "physics_debug_axes ", 19 ) == 0;
            const bool isContacts = strncmp( line, "physics_debug_contacts ", 23 ) == 0;
            const int prefixLen = isAxes ? 19 : ( isContacts ? 23 : 20 );
            const uint32_t flag = isAxes ? Physics::PHYSICS_DEBUG_AXES : ( isContacts ? Physics::PHYSICS_DEBUG_CONTACTS : Physics::PHYSICS_DEBUG_SLEEP );
            if ( strcmp( line + prefixLen, "on" ) == 0 )
            {
                scene.m_sceneOptions.physicsDebugFlags |= flag;
            }
            else if ( strcmp( line + prefixLen, "off" ) == 0 )
            {
                scene.m_sceneOptions.physicsDebugFlags &= ~flag;
            }
            else
            {
                fclose( file );
                char msg[256];
                sprintf_s( msg, sizeof( msg ), "Invalid physics_debug component value at line %d  (TestScene::LoadFromFile)", lineNumber );
                throw std::runtime_error( msg );
            }
            continue;
        }

        if ( strncmp( line, "physics_debug_transparent ", 26 ) == 0 )
        {
            if ( strcmp( line + 26, "on" ) == 0 )
            {
                scene.m_sceneOptions.physicsDebugTransparent = true;
            }
            else if ( strcmp( line + 26, "off" ) == 0 )
            {
                scene.m_sceneOptions.physicsDebugTransparent = false;
            }
            else
            {
                fclose( file );
                char msg[256];
                sprintf_s( msg, sizeof( msg ), "Invalid physics_debug_transparent value at line %d  (TestScene::LoadFromFile)", lineNumber );
                throw std::runtime_error( msg );
            }
            continue;
        }

        if ( strncmp( line, "physics_debug_alpha ", 20 ) == 0 )
        {
            float val = static_cast<float>( atof( line + 20 ) );
            if ( val < 0.05f || val > 1.0f )
            {
                fclose( file );
                char msg[256];
                sprintf_s( msg, sizeof( msg ), "Invalid physics_debug_alpha at line %d (expected 0.05..1.0)  (TestScene::LoadFromFile)", lineNumber );
                throw std::runtime_error( msg );
            }
            scene.m_sceneOptions.physicsDebugAlpha = val;
            continue;
        }

        if ( strncmp( line, "physics_debug_contact_linger ", 29 ) == 0 )
        {
            float val = static_cast<float>( atof( line + 29 ) );
            if ( val < 0.0f || val > 5.0f )
            {
                fclose( file );
                char msg[256];
                sprintf_s( msg, sizeof( msg ), "Invalid physics_debug_contact_linger at line %d (expected 0.0..5.0 seconds)  (TestScene::LoadFromFile)", lineNumber );
                throw std::runtime_error( msg );
            }
            scene.m_sceneOptions.physicsDebugContactLinger = val;
            continue;
        }

        // parse track_height directive
        if ( strncmp( line, "track_height ", 13 ) == 0 )
        {
            float val = static_cast<float>( atof( line + 13 ) );
            if ( val <= 0.0f )
            {
                fclose( file );
                char msg[256];
                sprintf_s( msg, sizeof( msg ), "Invalid track_height at line %d (must be > 0)  (TestScene::LoadFromFile)", lineNumber );
                throw std::runtime_error( msg );
            }
            scene.m_sceneOptions.trackHeight = val;
            continue;
        }

        // parse auto_cycle_interval directive
        if ( strncmp( line, "auto_cycle_interval ", 20 ) == 0 )
        {
            float val = static_cast<float>( atof( line + 20 ) );
            if ( val <= 0.0f )
            {
                fclose( file );
                char msg[256];
                sprintf_s( msg, sizeof( msg ), "Invalid auto_cycle_interval at line %d (must be > 0)  (TestScene::LoadFromFile)", lineNumber );
                throw std::runtime_error( msg );
            }
            scene.m_sceneOptions.autoCycleInterval = val;
            continue;
        }

        // parse flat_slope directive: flat_slope <baseY> <slopeX> <slopeZ>
        if ( strncmp( line, "flat_slope ", 11 ) == 0 )
        {
            float baseY = 0.0f, slopeX = 0.0f, slopeZ = 0.0f;
            int parsed = sscanf_s( line + 11, "%f %f %f", &baseY, &slopeX, &slopeZ );
            if ( parsed != 3 )
            {
                fclose( file );
                char msg[256];
                sprintf_s( msg, sizeof( msg ), "Invalid flat_slope at line %d (expected: flat_slope <baseY> <slopeX> <slopeZ>)  (TestScene::LoadFromFile)", lineNumber );
                throw std::runtime_error( msg );
            }
            scene.m_terrainOverride.hasFlatSlope = true;
            scene.m_terrainOverride.flatBaseY = baseY;
            scene.m_terrainOverride.flatSlopeX = slopeX;
            scene.m_terrainOverride.flatSlopeZ = slopeZ;
            continue;
        }

        // parse ball_state line (snapshot with full dynamic state)
        if ( strncmp( line, "ball_state ", 11 ) == 0 )
        {
            SceneBallState bs;
            memset( &bs, 0, sizeof( bs ) );

            int parsed = sscanf_s( line + 11, "%63s %f %f %f %f %f %f %f %f %f %f %f %f %f %f %f %f %f %f %f", bs.name, static_cast<unsigned>( sizeof( bs.name ) ), &bs.posX, &bs.posY, &bs.posZ, &bs.velX, &bs.velY, &bs.velZ, &bs.angVelX, &bs.angVelY, &bs.angVelZ, &bs.orientX, &bs.orientY, &bs.orientZ, &bs.orientW, &bs.radius, &bs.mass, &bs.restitution, &bs.inertiaX, &bs.inertiaY, &bs.inertiaZ );

            if ( parsed != 20 )
            {
                fclose( file );
                char msg[256];
                sprintf_s( msg, sizeof( msg ), "Invalid ball_state at line %d (expected 20 fields, got %d)  (TestScene::LoadFromFile)", lineNumber, parsed );
                throw std::runtime_error( msg );
            }

            scene.m_ballStates.push_back( bs );
            continue;
        }

        // parse world directive: world <gravity> <fluidHeight> <fluidDensity>
        if ( strncmp( line, "world ", 6 ) == 0 )
        {
            float gravity = 0.0f, fluidHeight = 0.0f, fluidDensity = 0.0f;
            int parsed = sscanf_s( line + 6, "%f %f %f", &gravity, &fluidHeight, &fluidDensity );
            if ( parsed != 3 )
            {
                fclose( file );
                char msg[256];
                sprintf_s( msg, sizeof( msg ), "Invalid world at line %d (expected: world <gravity> <fluidHeight> <fluidDensity>)  (TestScene::LoadFromFile)", lineNumber );
                throw std::runtime_error( msg );
            }
            scene.m_worldOverride.hasWorldOverride = true;
            scene.m_worldOverride.worldGravity = gravity;
            scene.m_worldOverride.worldFluidHeight = fluidHeight;
            scene.m_worldOverride.worldFluidDensity = fluidDensity;
            continue;
        }

        // parse water_hidden directive
        if ( strncmp( line, "water_hidden ", 13 ) == 0 )
        {
            if ( strcmp( line + 13, "on" ) == 0 )
            {
                scene.m_sceneOptions.waterHidden = true;
            }
            else if ( strcmp( line + 13, "off" ) == 0 )
            {
                scene.m_sceneOptions.waterHidden = false;
            }
            else
            {
                fclose( file );
                char msg[256];
                sprintf_s( msg, sizeof( msg ), "Invalid water_hidden value at line %d  (TestScene::LoadFromFile)", lineNumber );
                throw std::runtime_error( msg );
            }
            continue;
        }

        // parse terrain_hidden directive
        if ( strncmp( line, "terrain_hidden ", 15 ) == 0 )
        {
            if ( strcmp( line + 15, "on" ) == 0 )
            {
                scene.m_sceneOptions.terrainHidden = true;
            }
            else if ( strcmp( line + 15, "off" ) == 0 )
            {
                scene.m_sceneOptions.terrainHidden = false;
            }
            else
            {
                fclose( file );
                char msg[256];
                sprintf_s( msg, sizeof( msg ), "Invalid terrain_hidden value at line %d  (TestScene::LoadFromFile)", lineNumber );
                throw std::runtime_error( msg );
            }
            continue;
        }

        // parse physics_mode directive: forces legacy or solver physics for this scene,
        // overriding whatever --legacy flag was passed on the command line.
        // Use this to benchmark or compare both modes within a single suite run.
        if ( strncmp( line, "physics_mode ", 13 ) == 0 )
        {
            if ( strcmp( line + 13, "legacy" ) == 0 )
            {
                scene.m_sceneOptions.physicsMode = 1;
            }
            else if ( strcmp( line + 13, "solver" ) == 0 )
            {
                scene.m_sceneOptions.physicsMode = 2;
            }
            else
            {
                fclose( file );
                char msg[256];
                sprintf_s( msg, sizeof( msg ), "Invalid physics_mode at line %d: %s (expected 'legacy' or 'solver')  (TestScene::LoadFromFile)", lineNumber, line + 13 );
                throw std::runtime_error( msg );
            }
            continue;
        }

        // parse solver_balls directive: spawns exactly N impulse-solver sphere objects.
        // Paired with solver_boxes for precise control over the ball/box split in bench scenes.
        if ( strncmp( line, "solver_balls ", 13 ) == 0 )
        {
            scene.m_sceneOptions.solverBallCount = atoi( line + 13 );
            if ( scene.m_sceneOptions.solverBallCount < 0 )
            {
                fclose( file );
                char msg[256];
                sprintf_s( msg, sizeof( msg ), "Invalid solver_balls count at line %d (must be >= 0)  (TestScene::LoadFromFile)", lineNumber );
                throw std::runtime_error( msg );
            }
            continue;
        }

        // parse solver_boxes directive: spawns exactly N impulse-solver OBB objects.
        if ( strncmp( line, "solver_boxes ", 13 ) == 0 )
        {
            scene.m_sceneOptions.solverBoxCount = atoi( line + 13 );
            if ( scene.m_sceneOptions.solverBoxCount < 0 )
            {
                fclose( file );
                char msg[256];
                sprintf_s( msg, sizeof( msg ), "Invalid solver_boxes count at line %d (must be >= 0)  (TestScene::LoadFromFile)", lineNumber );
                throw std::runtime_error( msg );
            }
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


bool TestScene::IsPhysicsEnabled() const
{
    return m_sceneOptions.isPhysicsEnabled;
}


bool TestScene::IsTextEnabled() const
{
    return m_sceneOptions.isTextEnabled;
}


bool TestScene::IsTextOnly() const
{
    return m_sceneOptions.isTextOnly;
}


bool TestScene::IsWaterHidden() const
{
    return m_sceneOptions.waterHidden;
}


bool TestScene::IsTerrainHidden() const
{
    return m_sceneOptions.terrainHidden;
}


int TestScene::GetFrameCount() const
{
    return m_sceneOptions.frameCount;
}


const char* TestScene::GetScreenshotPath() const
{
    return m_captureOptions.screenshotPath;
}


int TestScene::GetScreenshotFrame() const
{
    return m_captureOptions.screenshotFrame;
}


int TestScene::GetScreenshotMs() const
{
    return m_captureOptions.screenshotMs;
}


unsigned int TestScene::GetSeed() const
{
    return m_sceneOptions.seed;
}


int TestScene::GetLegacyBallCount() const
{
    return m_sceneOptions.legacyBallCount;
}


int TestScene::GetPhysicsMode() const
{
    return m_sceneOptions.physicsMode;
}


int TestScene::GetSolverBallCount() const
{
    return m_sceneOptions.solverBallCount;
}


int TestScene::GetSolverBoxCount() const
{
    return m_sceneOptions.solverBoxCount;
}


const char* TestScene::GetPerfLogPath() const
{
    return m_loggingOptions.perfLogPath;
}


bool TestScene::IsPerfLogFlushEnabled() const
{
    return m_loggingOptions.isPerfLogFlush;
}


int TestScene::GetPerfLogFlushInterval() const
{
    return m_loggingOptions.perfLogFlushInterval;
}

bool TestScene::HasVsyncOverride() const
{
    return m_runtimeOverrides.hasVsyncOverride;
}


bool TestScene::IsVsyncEnabled() const
{
    return m_runtimeOverrides.isVsyncEnabled;
}


bool TestScene::HasPipelineSyncOverride() const
{
    return m_runtimeOverrides.hasPipelineSyncOverride;
}


bool TestScene::IsPipelineSyncEnabled() const
{
    return m_runtimeOverrides.isPipelineSyncEnabled;
}


bool TestScene::HasRollAlignOverride() const
{
    return m_runtimeOverrides.hasRollAlignOverride;
}


bool TestScene::IsRollAlignEnabled() const
{
    return m_runtimeOverrides.isRollAlignEnabled;
}


int TestScene::GetScreenshotInterval() const
{
    return m_captureOptions.screenshotInterval;
}


const char* TestScene::GetScreenshotDir() const
{
    return m_captureOptions.screenshotDir;
}


int TestScene::GetCameraCount() const
{
    return static_cast<int>( m_cameras.size() );
}


float TestScene::GetTimeScale() const
{
    return m_sceneOptions.timeScale;
}


bool TestScene::IsFixedStep() const
{
    return m_sceneOptions.isFixedStep;
}


bool TestScene::IsDebugVectors() const
{
    return m_sceneOptions.isDebugVectors;
}


uint32_t TestScene::GetPhysicsDebugFlags() const
{
    return m_sceneOptions.physicsDebugFlags;
}


bool TestScene::IsPhysicsDebugTransparent() const
{
    return m_sceneOptions.physicsDebugTransparent;
}


float TestScene::GetPhysicsDebugAlpha() const
{
    return m_sceneOptions.physicsDebugAlpha;
}


float TestScene::GetPhysicsDebugContactLinger() const
{
    return m_sceneOptions.physicsDebugContactLinger;
}


float TestScene::GetTrackHeight() const
{
    return m_sceneOptions.trackHeight;
}


float TestScene::GetAutoCycleInterval() const
{
    return m_sceneOptions.autoCycleInterval;
}


bool TestScene::IsScreenshotAndExit() const
{
    return m_sceneOptions.screenshotAndExit;
}


bool TestScene::IsExitOnComplete() const
{
    return m_sceneOptions.exitOnComplete;
}


bool TestScene::HasFlatSlope() const
{
    return m_terrainOverride.hasFlatSlope;
}


float TestScene::GetFlatBaseY() const
{
    return m_terrainOverride.flatBaseY;
}


float TestScene::GetFlatSlopeX() const
{
    return m_terrainOverride.flatSlopeX;
}


float TestScene::GetFlatSlopeZ() const
{
    return m_terrainOverride.flatSlopeZ;
}


int TestScene::GetBallCount() const
{
    return static_cast<int>( m_balls.size() );
}


const SceneCamera& TestScene::GetCamera( int index ) const
{
    if ( index < 0 || index >= static_cast<int>( m_cameras.size() ) )
    {
        throw std::runtime_error( "Camera index out of range.  (TestScene::GetCamera)" );
    }

    return m_cameras[index];
}


const SceneBall& TestScene::GetBall( int index ) const
{
    if ( index < 0 || index >= static_cast<int>( m_balls.size() ) )
    {
        throw std::runtime_error( "Ball index out of range.  (TestScene::GetBall)" );
    }

    return m_balls[index];
}


int TestScene::GetBallStateCount() const
{
    return static_cast<int>( m_ballStates.size() );
}


const SceneBallState& TestScene::GetBallState( int index ) const
{
    if ( index < 0 || index >= static_cast<int>( m_ballStates.size() ) )
    {
        throw std::runtime_error( "BallState index out of range.  (TestScene::GetBallState)" );
    }

    return m_ballStates[index];
}


int TestScene::GetBoxCount() const
{
    return static_cast<int>( m_boxes.size() );
}


const SceneBox& TestScene::GetBox( int index ) const
{
    if ( index < 0 || index >= static_cast<int>( m_boxes.size() ) )
    {
        throw std::runtime_error( "Box index out of range.  (TestScene::GetBox)" );
    }

    return m_boxes[index];
}


bool TestScene::HasWorldOverride() const
{
    return m_worldOverride.hasWorldOverride;
}


float TestScene::GetWorldGravity() const
{
    return m_worldOverride.worldGravity;
}


float TestScene::GetWorldFluidHeight() const
{
    return m_worldOverride.worldFluidHeight;
}


float TestScene::GetWorldFluidDensity() const
{
    return m_worldOverride.worldFluidDensity;
}
