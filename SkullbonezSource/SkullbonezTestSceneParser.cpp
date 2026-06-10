// --- Includes ---
#include "SkullbonezTestScene.h"
#include <cerrno>
#include <climits>
#include <cstdarg>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <stdexcept>


namespace SkullbonezCore
{
namespace Basics
{
namespace
{
bool ParseOnOff( const char* value, bool& out )
{
    if ( strcmp( value, "on" ) == 0 || strcmp( value, "open" ) == 0 || strcmp( value, "all" ) == 0 )
    {
        out = true;
        return true;
    }
    if ( strcmp( value, "off" ) == 0 || strcmp( value, "closed" ) == 0 || strcmp( value, "none" ) == 0 )
    {
        out = false;
        return true;
    }
    return false;
}


bool ParseUITab( const char* value, int& outTab )
{
    if ( strcmp( value, "profiler" ) == 0 || strcmp( value, "profile" ) == 0 )
    {
        outTab = 0;
        return true;
    }
    if ( strcmp( value, "scene" ) == 0 || strcmp( value, "overview" ) == 0 || strcmp( value, "info" ) == 0 )
    {
        outTab = 1;
        return true;
    }
    if ( strcmp( value, "physics" ) == 0 )
    {
        outTab = 2;
        return true;
    }
    if ( strcmp( value, "options" ) == 0 || strcmp( value, "params" ) == 0 || strcmp( value, "renderer" ) == 0 )
    {
        outTab = 3;
        return true;
    }
    if ( strcmp( value, "keys" ) == 0 || strcmp( value, "controls" ) == 0 )
    {
        outTab = 4;
        return true;
    }
    if ( strcmp( value, "cinematic" ) == 0 || strcmp( value, "cine" ) == 0 || strcmp( value, "look" ) == 0 )
    {
        outTab = 5;
        return true;
    }
    return false;
}

struct FileCloser
{
    void operator()( FILE* file ) const
    {
        if ( file )
        {
            fclose( file );
        }
    }
};

using SceneFileHandle = std::unique_ptr<FILE, FileCloser>;
} // namespace


class TestSceneParser
{
  private:
    using ParseFn = void ( TestSceneParser::* )( const char* args );

    struct SceneDirective
    {
        const char* name;
        ParseFn parse;
        const char* expected;
    };

    struct UIDirective
    {
        const char* name;
        ParseFn parse;
        const char* expected;
    };

    const char* m_path = nullptr;
    SceneFileHandle m_file;
    int m_lineNumber = 0;
    TestScene m_scene;

    static bool IsSpace( char c )
    {
        return c == ' ' || c == '\t';
    }

    static bool MatchDirective( const char* line, const char* name, const char*& outArgs )
    {
        const size_t nameLen = strlen( name );
        if ( strncmp( line, name, nameLen ) != 0 )
        {
            return false;
        }

        if ( line[nameLen] == '\0' )
        {
            outArgs = line + nameLen;
            return true;
        }

        if ( !IsSpace( line[nameLen] ) )
        {
            return false;
        }

        outArgs = line + nameLen;
        while ( IsSpace( *outArgs ) )
        {
            ++outArgs;
        }
        return true;
    }

    static bool ReadToken( const char*& cursor, char* out, size_t outSize )
    {
        while ( IsSpace( *cursor ) )
        {
            ++cursor;
        }

        const char* start = cursor;
        while ( *cursor != '\0' && !IsSpace( *cursor ) )
        {
            ++cursor;
        }

        const size_t len = static_cast<size_t>( cursor - start );
        if ( len == 0 || outSize == 0 )
        {
            if ( outSize > 0 )
            {
                out[0] = '\0';
            }
            return false;
        }

        const size_t copyLen = ( len < outSize - 1 ) ? len : outSize - 1;
        memcpy( out, start, copyLen );
        out[copyLen] = '\0';
        return true;
    }

    [[noreturn]] void Fail( const char* fmt, ... )
    {
        if ( m_file )
        {
            m_file.reset();
        }

        char detail[256] = {};
        va_list args;
        va_start( args, fmt );
        vsprintf_s( detail, sizeof( detail ), fmt, args );
        va_end( args );

        char msg[384];
        sprintf_s( msg, sizeof( msg ), "%s  (TestScene::LoadFromFile)", detail );
        throw std::runtime_error( msg );
    }

    const char* RequireArgs( const char* directive, const char* args, const char* expected )
    {
        if ( !args || args[0] == '\0' )
        {
            Fail( "Invalid %s at line %d (expected: %s)", directive, m_lineNumber, expected );
        }
        return args;
    }

    static bool TryParseInt( const char* value, int& out )
    {
        if ( !value || value[0] == '\0' )
        {
            return false;
        }

        errno = 0;
        char* end = nullptr;
        const long parsed = strtol( value, &end, 10 );
        if ( end == value || *end != '\0' || errno == ERANGE || parsed < INT_MIN || parsed > INT_MAX )
        {
            return false;
        }

        out = static_cast<int>( parsed );
        return true;
    }

    static bool TryParseFloat( const char* value, float& out )
    {
        if ( !value || value[0] == '\0' )
        {
            return false;
        }

        errno = 0;
        char* end = nullptr;
        const double parsed = strtod( value, &end );
        if ( end == value || *end != '\0' || errno == ERANGE )
        {
            return false;
        }

        out = static_cast<float>( parsed );
        return true;
    }

    int ParseIntValue( const char* directive, const char* value )
    {
        int parsed = 0;
        if ( !TryParseInt( value, parsed ) )
        {
            Fail( "Invalid %s at line %d: %s", directive, m_lineNumber, value ? value : "" );
        }
        return parsed;
    }

    int ParseIntArg( const char* directive, const char* args, const char* expected )
    {
        return ParseIntValue( directive, RequireArgs( directive, args, expected ) );
    }

    int ParseNextIntToken( const char* directive, const char*& cursor, const char* expected )
    {
        char value[64] = {};
        if ( !ReadToken( cursor, value, sizeof( value ) ) )
        {
            Fail( "Invalid %s at line %d (expected: %s)", directive, m_lineNumber, expected );
        }
        return ParseIntValue( directive, value );
    }

    float ParseFloatValue( const char* directive, const char* value )
    {
        float parsed = 0.0f;
        if ( !TryParseFloat( value, parsed ) )
        {
            Fail( "Invalid %s at line %d: %s", directive, m_lineNumber, value ? value : "" );
        }
        return parsed;
    }

    float ParseFloatArg( const char* directive, const char* args, const char* expected )
    {
        return ParseFloatValue( directive, RequireArgs( directive, args, expected ) );
    }

    bool ParseOnOffOnly( const char* value, bool& out ) const
    {
        if ( strcmp( value, "on" ) == 0 )
        {
            out = true;
            return true;
        }
        if ( strcmp( value, "off" ) == 0 )
        {
            out = false;
            return true;
        }
        return false;
    }

    void ParseStrictOnOff( const char* directive, const char* value, bool& out )
    {
        if ( !ParseOnOffOnly( value, out ) )
        {
            Fail( "Invalid %s value at line %d: %s", directive, m_lineNumber, value );
        }
    }

    void ParseAliasOnOff( const char* directive, const char* value, bool& out )
    {
        if ( !ParseOnOff( value, out ) )
        {
            Fail( "Invalid %s value at line %d: %s", directive, m_lineNumber, value );
        }
    }

    void ParsePhysics( const char* args )
    {
        ParseStrictOnOff( "physics", RequireArgs( "physics", args, "physics on|off" ), m_scene.m_sceneOptions.isPhysicsEnabled );
    }

    void ParseText( const char* args )
    {
        ParseStrictOnOff( "text", RequireArgs( "text", args, "text on|off" ), m_scene.m_sceneOptions.isTextEnabled );
    }

    void ParseTextOnly( const char* args )
    {
        ParseStrictOnOff( "text_only", RequireArgs( "text_only", args, "text_only on|off" ), m_scene.m_sceneOptions.isTextOnly );
    }

    void ParseUI( const char* args )
    {
        args = RequireArgs( "ui", args, "ui <command> <value>" );

        char command[64] = {};
        const char* valueArgs = args;
        if ( !ReadToken( valueArgs, command, sizeof( command ) ) )
        {
            Fail( "Invalid UI directive at line %d", m_lineNumber );
        }
        while ( IsSpace( *valueArgs ) )
        {
            ++valueArgs;
        }
        if ( valueArgs[0] == '\0' )
        {
            Fail( "Invalid UI directive at line %d", m_lineNumber );
        }

        m_scene.m_UIOptions.hasDirective = true;

        static const UIDirective directives[] = {
            { "visible", &TestSceneParser::ParseUIVisible, "ui visible on|off" },
            { "minimized", &TestSceneParser::ParseUIMinimized, "ui minimized on|off" },
            { "tab", &TestSceneParser::ParseUITabDirective, "ui tab <profiler|scene|physics|options|controls|cine>" },
            { "rect", &TestSceneParser::ParseUIRect, "ui rect <x> <y> <w> <h>" },
            { "blur", &TestSceneParser::ParseUIBlur, "ui blur on|off" },
            { "renderer_combo", &TestSceneParser::ParseUIRendererCombo, "ui renderer_combo open|closed" },
            { "water_combo", &TestSceneParser::ParseUIWaterCombo, "ui water_combo open|closed" },
            { "scene_combo", &TestSceneParser::ParseUISceneCombo, "ui scene_combo open|closed" },
            { "scene_filter", &TestSceneParser::ParseUISceneFilter, "ui scene_filter <text>" },
            { "profiler_expand", &TestSceneParser::ParseUIProfilerExpand, "ui profiler_expand on|off" },
            { "timeline", &TestSceneParser::ParseUITimeline, "ui timeline on|off" },
            { "histogram", &TestSceneParser::ParseUIHistogram, "ui histogram on|off" },
            { "scroll", &TestSceneParser::ParseUIScroll, "ui scroll <y|bottom>" },
            { "mouse", &TestSceneParser::ParseUIMouse, "ui mouse <x> <y>" },
            { "stress", &TestSceneParser::ParseUIStress, "ui stress on|off" },
            { "stress_seed", &TestSceneParser::ParseUIStressSeed, "ui stress_seed <seed>" },
            { "stress_actions", &TestSceneParser::ParseUIStressActions, "ui stress_actions <count>" },
        };

        for ( const UIDirective& directive : directives )
        {
            if ( strcmp( command, directive.name ) == 0 )
            {
                ( this->*directive.parse )( valueArgs );
                return;
            }
        }

        Fail( "Unknown UI directive at line %d: %s", m_lineNumber, command );
    }

    void ParseUIVisible( const char* args )
    {
        bool parsedValue = false;
        ParseAliasOnOff( "UI visible", args, parsedValue );
        m_scene.m_UIOptions.hasVisible = true;
        m_scene.m_UIOptions.isVisible = parsedValue;
    }

    void ParseUIMinimized( const char* args )
    {
        bool parsedValue = false;
        ParseAliasOnOff( "UI minimized", args, parsedValue );
        m_scene.m_UIOptions.hasMinimized = true;
        m_scene.m_UIOptions.isMinimized = parsedValue;
    }

    void ParseUITabDirective( const char* args )
    {
        char value[64] = {};
        const char* cursor = args;
        ReadToken( cursor, value, sizeof( value ) );
        int tab = 0;
        if ( !ParseUITab( value, tab ) )
        {
            Fail( "Invalid UI tab value at line %d: %s", m_lineNumber, value );
        }
        m_scene.m_UIOptions.hasActiveTab = true;
        m_scene.m_UIOptions.activeTab = tab;
    }

    void ParseUIRect( const char* args )
    {
        const char* cursor = RequireArgs( "UI rect", args, "UI rect <x> <y> <w> <h>" );
        const int x = ParseNextIntToken( "UI rect", cursor, "UI rect <x> <y> <w> <h>" );
        const int y = ParseNextIntToken( "UI rect", cursor, "UI rect <x> <y> <w> <h>" );
        const int w = ParseNextIntToken( "UI rect", cursor, "UI rect <x> <y> <w> <h>" );
        const int h = ParseNextIntToken( "UI rect", cursor, "UI rect <x> <y> <w> <h>" );
        if ( w <= 0 || h <= 0 )
        {
            Fail( "Invalid UI rect at line %d (expected: UI rect <x> <y> <w> <h>)", m_lineNumber );
        }
        m_scene.m_UIOptions.hasWindowRect = true;
        m_scene.m_UIOptions.windowX = x;
        m_scene.m_UIOptions.windowY = y;
        m_scene.m_UIOptions.windowW = w;
        m_scene.m_UIOptions.windowH = h;
    }

    void ParseUIBlur( const char* args )
    {
        bool parsedValue = false;
        ParseAliasOnOff( "UI blur", args, parsedValue );
        m_scene.m_UIOptions.hasBlur = true;
        m_scene.m_UIOptions.blurEnabled = parsedValue;
    }

    void ParseUIRendererCombo( const char* args )
    {
        bool parsedValue = false;
        ParseAliasOnOff( "UI renderer_combo", args, parsedValue );
        m_scene.m_UIOptions.hasRendererComboOpen = true;
        m_scene.m_UIOptions.rendererComboOpen = parsedValue;
    }

    void ParseUIWaterCombo( const char* args )
    {
        bool parsedValue = false;
        ParseAliasOnOff( "UI water_combo", args, parsedValue );
        m_scene.m_UIOptions.hasWaterComboOpen = true;
        m_scene.m_UIOptions.waterComboOpen = parsedValue;
    }

    void ParseUISceneCombo( const char* args )
    {
        bool parsedValue = false;
        ParseAliasOnOff( "UI scene_combo", args, parsedValue );
        m_scene.m_UIOptions.hasSceneComboOpen = true;
        m_scene.m_UIOptions.sceneComboOpen = parsedValue;
    }

    void ParseUISceneFilter( const char* args )
    {
        char value[64] = {};
        const char* cursor = args;
        if ( !ReadToken( cursor, value, sizeof( value ) ) )
        {
            Fail( "Invalid UI scene_filter value at line %d", m_lineNumber );
        }
        m_scene.m_UIOptions.hasSceneFilter = true;
        strncpy_s( m_scene.m_UIOptions.sceneFilter, sizeof( m_scene.m_UIOptions.sceneFilter ), value, _TRUNCATE );
    }

    void ParseUIProfilerExpand( const char* args )
    {
        bool parsedValue = false;
        ParseAliasOnOff( "UI profiler_expand", args, parsedValue );
        m_scene.m_UIOptions.hasProfilerExpandAll = true;
        m_scene.m_UIOptions.profilerExpandAll = parsedValue;
    }

    void ParseUITimeline( const char* args )
    {
        bool parsedValue = false;
        ParseAliasOnOff( "UI timeline", args, parsedValue );
        m_scene.m_UIOptions.hasProfilerTimeline = true;
        m_scene.m_UIOptions.profilerTimeline = parsedValue;
    }

    void ParseUIHistogram( const char* args )
    {
        bool parsedValue = false;
        ParseAliasOnOff( "UI histogram", args, parsedValue );
        m_scene.m_UIOptions.hasPerformanceHistogram = true;
        m_scene.m_UIOptions.performanceHistogram = parsedValue;
    }

    void ParseUIScroll( const char* args )
    {
        char value[64] = {};
        const char* cursor = args;
        if ( !ReadToken( cursor, value, sizeof( value ) ) )
        {
            Fail( "Invalid UI scroll value at line %d", m_lineNumber );
        }
        m_scene.m_UIOptions.hasScrollY = true;
        m_scene.m_UIOptions.scrollY = ( strcmp( value, "bottom" ) == 0 ) ? 1000000.0f : ParseFloatValue( "UI scroll", value );
    }

    void ParseUIMouse( const char* args )
    {
        const char* cursor = RequireArgs( "UI mouse", args, "UI mouse <x> <y>" );
        const int x = ParseNextIntToken( "UI mouse", cursor, "UI mouse <x> <y>" );
        const int y = ParseNextIntToken( "UI mouse", cursor, "UI mouse <x> <y>" );
        m_scene.m_UIOptions.hasMouseOverride = true;
        m_scene.m_UIOptions.mouseX = x;
        m_scene.m_UIOptions.mouseY = y;
    }

    void ParseUIStress( const char* args )
    {
        bool parsedValue = false;
        ParseAliasOnOff( "UI stress", args, parsedValue );
        m_scene.m_UIOptions.hasStress = true;
        m_scene.m_UIOptions.stressEnabled = parsedValue;
    }

    void ParseUIStressSeed( const char* args )
    {
        const int seed = ParseIntArg( "UI stress_seed", args, "UI stress_seed <N>" );
        if ( seed <= 0 )
        {
            Fail( "Invalid UI stress_seed value at line %d: %s", m_lineNumber, args );
        }
        m_scene.m_UIOptions.hasStressSeed = true;
        m_scene.m_UIOptions.stressSeed = static_cast<unsigned int>( seed );
    }

    void ParseUIStressActions( const char* args )
    {
        const int actions = ParseIntArg( "UI stress_actions", args, "UI stress_actions <N>" );
        if ( actions <= 0 )
        {
            Fail( "Invalid UI stress_actions value at line %d: %s", m_lineNumber, args );
        }
        m_scene.m_UIOptions.hasStressActions = true;
        m_scene.m_UIOptions.stressActionsPerFrame = actions > 32 ? 32 : actions;
    }

    void ParseUITestPattern( const char* args )
    {
        bool parsedValue = false;
        ParseAliasOnOff( "UI_test_pattern", RequireArgs( "ui_test_pattern", args, "ui_test_pattern on|off" ), parsedValue );
        m_scene.m_UIOptions.hasTestPattern = true;
        m_scene.m_UIOptions.testPatternEnabled = parsedValue;
    }

    void ParseFrames( const char* args )
    {
        args = RequireArgs( "frames", args, "frames <N|unlimited>" );
        if ( strcmp( args, "unlimited" ) == 0 )
        {
            m_scene.m_sceneOptions.frameCount = -1;
            return;
        }

        m_scene.m_sceneOptions.frameCount = ParseIntValue( "frames", args );
        if ( m_scene.m_sceneOptions.frameCount <= 0 && strcmp( args, "-1" ) != 0 )
        {
            Fail( "Invalid frame count at line %d: %s", m_lineNumber, args );
        }
        if ( m_scene.m_sceneOptions.frameCount <= 0 )
        {
            m_scene.m_sceneOptions.frameCount = -1;
        }
    }

    void ParseScreenshot( const char* args )
    {
        char outPath[256] = {};
        char triggerType[16] = {};
        int triggerValue = 0;
        const int parsed = sscanf_s( RequireArgs( "screenshot", args, "screenshot <path> frame|ms <N>" ),
                                     "%255s %15s %d",
                                     outPath,
                                     static_cast<unsigned>( sizeof( outPath ) ),
                                     triggerType,
                                     static_cast<unsigned>( sizeof( triggerType ) ),
                                     &triggerValue );

        if ( parsed != 3 || triggerValue <= 0 )
        {
            Fail( "Invalid screenshot at line %d (expected: screenshot <path> frame|ms <N>)", m_lineNumber );
        }

        strcpy_s( m_scene.m_captureOptions.screenshotPath, sizeof( m_scene.m_captureOptions.screenshotPath ), outPath );

        if ( strcmp( triggerType, "frame" ) == 0 )
        {
            m_scene.m_captureOptions.screenshotFrame = triggerValue;
            m_scene.m_captureOptions.screenshotMs = -1;
        }
        else if ( strcmp( triggerType, "ms" ) == 0 )
        {
            m_scene.m_captureOptions.screenshotMs = triggerValue;
            m_scene.m_captureOptions.screenshotFrame = -1;
        }
        else
        {
            Fail( "Invalid screenshot trigger '%s' at line %d (expected 'frame' or 'ms')", triggerType, m_lineNumber );
        }
    }

    void ParseSeed( const char* args )
    {
        m_scene.m_sceneOptions.seed = static_cast<unsigned int>( ParseIntArg( "seed", args, "seed <N>" ) );
        if ( m_scene.m_sceneOptions.seed == 0 )
        {
            Fail( "Invalid seed at line %d (must be > 0)", m_lineNumber );
        }
    }

    void ParsePerfLog( const char* args )
    {
        strcpy_s( m_scene.m_loggingOptions.perfLogPath, sizeof( m_scene.m_loggingOptions.perfLogPath ), RequireArgs( "perf_log", args, "perf_log <path>" ) );
    }

    void ParsePerfLogFlush( const char* args )
    {
        ParseStrictOnOff( "perf_log_flush", RequireArgs( "perf_log_flush", args, "perf_log_flush on|off" ), m_scene.m_loggingOptions.isPerfLogFlush );
    }

    void ParsePerfLogFlushInterval( const char* args )
    {
        m_scene.m_loggingOptions.perfLogFlushInterval = ParseIntArg( "perf_log_flush_interval", args, "perf_log_flush_interval <N>" );
        if ( m_scene.m_loggingOptions.perfLogFlushInterval < 0 )
        {
            Fail( "Invalid perf_log_flush_interval at line %d (must be >= 0)", m_lineNumber );
        }
    }

    void ParseVsync( const char* args )
    {
        m_scene.m_runtimeOverrides.hasVsyncOverride = true;
        ParseStrictOnOff( "vsync", RequireArgs( "vsync", args, "vsync on|off" ), m_scene.m_runtimeOverrides.isVsyncEnabled );
    }

    void ParsePipelineSync( const char* args )
    {
        m_scene.m_runtimeOverrides.hasPipelineSyncOverride = true;
        ParseStrictOnOff( "pipeline_sync", RequireArgs( "pipeline_sync", args, "pipeline_sync on|off" ), m_scene.m_runtimeOverrides.isPipelineSyncEnabled );
    }

    void ParseScreenshotAndExit( const char* )
    {
        m_scene.m_sceneOptions.screenshotAndExit = true;
    }

    void ParseExitOnComplete( const char* )
    {
        m_scene.m_sceneOptions.exitOnComplete = true;
    }

    void ParseCollisionVisualizer( const char* args )
    {
        ParseAliasOnOff( "collision_visualizer", RequireArgs( "collision_visualizer", args, "collision_visualizer on|off" ), m_scene.m_sceneOptions.collisionVisualizer );
    }

    void ParseBroadphaseOverlay( const char* args )
    {
        ParseAliasOnOff( "broadphase_overlay", RequireArgs( "broadphase_overlay", args, "broadphase_overlay on|off" ), m_scene.m_sceneOptions.broadphaseOverlay );
    }

    void ParseWaterFreeze( const char* args )
    {
        ParseAliasOnOff( "water_freeze", RequireArgs( "water_freeze", args, "water_freeze on|off" ), m_scene.m_sceneOptions.waterFreezeDebug );
    }

    void ParseWaterFlat( const char* args )
    {
        ParseAliasOnOff( "water_flat", RequireArgs( "water_flat", args, "water_flat on|off" ), m_scene.m_sceneOptions.waterFlatDebug );
    }

    void ParseWaterReflection( const char* args )
    {
        const char* value = RequireArgs( "water_reflection", args, "water_reflection fbo|dxr|none" );
        if ( strcmp( value, "fbo" ) == 0 || strcmp( value, "on" ) == 0 )
        {
            m_scene.m_sceneOptions.waterReflectionMode = 0;
        }
        else if ( strcmp( value, "dxr" ) == 0 || strcmp( value, "rt" ) == 0 )
        {
            m_scene.m_sceneOptions.waterReflectionMode = 1;
        }
        else if ( strcmp( value, "none" ) == 0 || strcmp( value, "off" ) == 0 )
        {
            m_scene.m_sceneOptions.waterReflectionMode = 2;
        }
        else
        {
            Fail( "Invalid water_reflection value at line %d", m_lineNumber );
        }
    }

    void ParseScreenshotInterval( const char* args )
    {
        char outDir[256] = {};
        int intervalFrames = 0;
        const int parsed = sscanf_s( RequireArgs( "screenshot_interval", args, "screenshot_interval <dir> <N>" ), "%255s %d", outDir, static_cast<unsigned>( sizeof( outDir ) ), &intervalFrames );

        if ( parsed != 2 || intervalFrames <= 0 )
        {
            Fail( "Invalid screenshot_interval at line %d (expected: screenshot_interval <dir> <N>)", m_lineNumber );
        }

        strcpy_s( m_scene.m_captureOptions.screenshotDir, sizeof( m_scene.m_captureOptions.screenshotDir ), outDir );
        m_scene.m_captureOptions.screenshotInterval = intervalFrames;
    }

    void ParseCamera( const char* args )
    {
        if ( static_cast<int>( m_scene.m_cameras.size() ) >= TOTAL_CAMERA_COUNT )
        {
            Fail( "Too many cameras at line %d (max %d)", m_lineNumber, TOTAL_CAMERA_COUNT );
        }

        SceneCamera cam;
        memset( &cam, 0, sizeof( cam ) );

        const int parsed = sscanf_s( RequireArgs( "camera", args, "camera <name> <pos> <view> <up>" ),
                                     "%63s %f %f %f %f %f %f %f %f %f",
                                     cam.name,
                                     static_cast<unsigned>( sizeof( cam.name ) ),
                                     &cam.m_position.x,
                                     &cam.m_position.y,
                                     &cam.m_position.z,
                                     &cam.view.x,
                                     &cam.view.y,
                                     &cam.view.z,
                                     &cam.up.x,
                                     &cam.up.y,
                                     &cam.up.z );

        if ( parsed != 10 )
        {
            Fail( "Invalid camera at line %d (expected 10 fields, got %d)", m_lineNumber, parsed );
        }

        m_scene.m_cameras.push_back( cam );
    }

    void ParseBall( const char* args )
    {
        SceneBall ball;
        memset( &ball, 0, sizeof( ball ) );
        ball.hasInitOrient = false;

        args = RequireArgs( "ball", args, "ball <name> ..." );
        int parsed = sscanf_s( args,
                               "%63s %f %f %f %f %f %f %f %f %f %f %f %f %f %f %f %f",
                               ball.name,
                               static_cast<unsigned>( sizeof( ball.name ) ),
                               &ball.posX,
                               &ball.posY,
                               &ball.posZ,
                               &ball.m_radius,
                               &ball.m_mass,
                               &ball.moment,
                               &ball.restitution,
                               &ball.forceX,
                               &ball.forceY,
                               &ball.forceZ,
                               &ball.forcePosX,
                               &ball.forcePosY,
                               &ball.forcePosZ,
                               &ball.eulerX,
                               &ball.eulerY,
                               &ball.eulerZ );

        if ( parsed == 17 )
        {
            ball.hasInitOrient = true;
        }
        else if ( parsed != 14 )
        {
            parsed = sscanf_s( args,
                               "%63s %f %f %f %f %f %f %f %f %f %f",
                               ball.name,
                               static_cast<unsigned>( sizeof( ball.name ) ),
                               &ball.posX,
                               &ball.posY,
                               &ball.posZ,
                               &ball.m_radius,
                               &ball.m_mass,
                               &ball.moment,
                               &ball.restitution,
                               &ball.eulerX,
                               &ball.eulerY,
                               &ball.eulerZ );

            if ( parsed == 11 )
            {
                ball.hasInitOrient = true;
            }
            else if ( parsed != 8 )
            {
                Fail( "Invalid ball at line %d (expected 8, 11, 14 or 17 fields, got %d)", m_lineNumber, parsed );
            }
        }

        m_scene.m_balls.push_back( ball );
    }

    void ParseBoxCommon( const char* args, bool isFixed )
    {
        SceneBox box;
        memset( &box, 0, sizeof( box ) );
        box.hasInitOrient = false;
        box.hasInitVelocity = false;
        box.isFixed = isFixed;

        int parsed = sscanf_s( RequireArgs( isFixed ? "floating_box" : "box", args, "box <name> ..." ),
                               "%63s %f %f %f %f %f %f %f %f %f %f %f %f %f %f",
                               box.name,
                               static_cast<unsigned>( sizeof( box.name ) ),
                               &box.posX,
                               &box.posY,
                               &box.posZ,
                               &box.halfX,
                               &box.halfY,
                               &box.halfZ,
                               &box.mass,
                               &box.restitution,
                               &box.eulerX,
                               &box.eulerY,
                               &box.eulerZ,
                               &box.velX,
                               &box.velY,
                               &box.velZ );

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
            Fail( "Invalid box/floating_box at line %d (expected 9, 12, or 15 fields, got %d)", m_lineNumber, parsed );
        }

        m_scene.m_boxes.push_back( box );
    }

    void ParseBox( const char* args )
    {
        ParseBoxCommon( args, false );
    }

    void ParseFloatingBox( const char* args )
    {
        ParseBoxCommon( args, true );
    }

    void ParseTimeScale( const char* args )
    {
        const float val = ParseFloatArg( "time_scale", args, "time_scale <value>" );
        if ( val <= 0.0f )
        {
            Fail( "Invalid time_scale at line %d (must be > 0)", m_lineNumber );
        }
        m_scene.m_sceneOptions.timeScale = val;
    }

    void ParseFixedStep( const char* )
    {
        m_scene.m_sceneOptions.isFixedStep = true;
    }

    void ParsePhysicsDebug( const char* args )
    {
        const char* value = RequireArgs( "physics_debug", args, "physics_debug none|axes|contacts|sleep|pipeline|all" );
        if ( strcmp( value, "none" ) == 0 || strcmp( value, "off" ) == 0 )
        {
            m_scene.m_sceneOptions.physicsDebugFlags = Physics::PHYSICS_DEBUG_NONE;
        }
        else if ( strcmp( value, "axes" ) == 0 )
        {
            m_scene.m_sceneOptions.physicsDebugFlags = Physics::PHYSICS_DEBUG_AXES;
        }
        else if ( strcmp( value, "contacts" ) == 0 )
        {
            m_scene.m_sceneOptions.physicsDebugFlags = Physics::PHYSICS_DEBUG_CONTACTS;
        }
        else if ( strcmp( value, "sleep" ) == 0 )
        {
            m_scene.m_sceneOptions.physicsDebugFlags = Physics::PHYSICS_DEBUG_SLEEP;
        }
        else if ( strcmp( value, "pipeline" ) == 0 )
        {
            m_scene.m_sceneOptions.physicsDebugFlags = Physics::PHYSICS_DEBUG_PIPELINE;
        }
        else if ( strcmp( value, "all" ) == 0 || strcmp( value, "on" ) == 0 )
        {
            m_scene.m_sceneOptions.physicsDebugFlags = Physics::PHYSICS_DEBUG_ALL;
        }
        else
        {
            Fail( "Invalid physics_debug value at line %d", m_lineNumber );
        }
    }

    void ParsePhysicsDebugComponent( const char* args, uint32_t flag )
    {
        bool enabled = false;
        ParseStrictOnOff( "physics_debug component", RequireArgs( "physics_debug component", args, "physics_debug_<component> on|off" ), enabled );
        if ( enabled )
        {
            m_scene.m_sceneOptions.physicsDebugFlags |= flag;
        }
        else
        {
            m_scene.m_sceneOptions.physicsDebugFlags &= ~flag;
        }
    }

    void ParsePhysicsDebugAxes( const char* args )
    {
        ParsePhysicsDebugComponent( args, Physics::PHYSICS_DEBUG_AXES );
    }

    void ParsePhysicsDebugContacts( const char* args )
    {
        ParsePhysicsDebugComponent( args, Physics::PHYSICS_DEBUG_CONTACTS );
    }

    void ParsePhysicsDebugSleep( const char* args )
    {
        ParsePhysicsDebugComponent( args, Physics::PHYSICS_DEBUG_SLEEP );
    }

    void ParsePhysicsDebugPipeline( const char* args )
    {
        ParsePhysicsDebugComponent( args, Physics::PHYSICS_DEBUG_PIPELINE );
    }

    void ParsePhysicsDebugTransparent( const char* args )
    {
        ParseStrictOnOff( "physics_debug_transparent", RequireArgs( "physics_debug_transparent", args, "physics_debug_transparent on|off" ), m_scene.m_sceneOptions.physicsDebugTransparent );
    }

    void ParsePhysicsDebugAlpha( const char* args )
    {
        const float val = ParseFloatArg( "physics_debug_alpha", args, "physics_debug_alpha <0.05..1.0>" );
        if ( val < 0.05f || val > 1.0f )
        {
            Fail( "Invalid physics_debug_alpha at line %d (expected 0.05..1.0)", m_lineNumber );
        }
        m_scene.m_sceneOptions.physicsDebugAlpha = val;
    }

    void ParsePhysicsDebugContactLinger( const char* args )
    {
        const float val = ParseFloatArg( "physics_debug_contact_linger", args, "physics_debug_contact_linger <0.0..5.0>" );
        if ( val < 0.0f || val > 5.0f )
        {
            Fail( "Invalid physics_debug_contact_linger at line %d (expected 0.0..5.0 seconds)", m_lineNumber );
        }
        m_scene.m_sceneOptions.physicsDebugContactLinger = val;
    }

    void ParseTrackHeight( const char* args )
    {
        const float val = ParseFloatArg( "track_height", args, "track_height <height>" );
        if ( val <= 0.0f )
        {
            Fail( "Invalid track_height at line %d (must be > 0)", m_lineNumber );
        }
        m_scene.m_sceneOptions.trackHeight = val;
    }

    void ParseAutoCycleInterval( const char* args )
    {
        const float val = ParseFloatArg( "auto_cycle_interval", args, "auto_cycle_interval <seconds>" );
        if ( val <= 0.0f )
        {
            Fail( "Invalid auto_cycle_interval at line %d (must be > 0)", m_lineNumber );
        }
        m_scene.m_sceneOptions.autoCycleInterval = val;
    }

    void ParseFlatSlope( const char* args )
    {
        float baseY = 0.0f;
        float slopeX = 0.0f;
        float slopeZ = 0.0f;
        const int parsed = sscanf_s( RequireArgs( "flat_slope", args, "flat_slope <baseY> <slopeX> <slopeZ>" ), "%f %f %f", &baseY, &slopeX, &slopeZ );
        if ( parsed != 3 )
        {
            Fail( "Invalid flat_slope at line %d (expected: flat_slope <baseY> <slopeX> <slopeZ>)", m_lineNumber );
        }
        m_scene.m_terrainOverride.hasFlatSlope = true;
        m_scene.m_terrainOverride.flatBaseY = baseY;
        m_scene.m_terrainOverride.flatSlopeX = slopeX;
        m_scene.m_terrainOverride.flatSlopeZ = slopeZ;
    }

    void ParseBallState( const char* args )
    {
        SceneBallState bs;
        memset( &bs, 0, sizeof( bs ) );

        const int parsed = sscanf_s( RequireArgs( "ball_state", args, "ball_state <name> ..." ),
                                     "%63s %f %f %f %f %f %f %f %f %f %f %f %f %f %f %f %f %f %f %f",
                                     bs.name,
                                     static_cast<unsigned>( sizeof( bs.name ) ),
                                     &bs.posX,
                                     &bs.posY,
                                     &bs.posZ,
                                     &bs.velX,
                                     &bs.velY,
                                     &bs.velZ,
                                     &bs.angVelX,
                                     &bs.angVelY,
                                     &bs.angVelZ,
                                     &bs.orientX,
                                     &bs.orientY,
                                     &bs.orientZ,
                                     &bs.orientW,
                                     &bs.radius,
                                     &bs.mass,
                                     &bs.restitution,
                                     &bs.inertiaX,
                                     &bs.inertiaY,
                                     &bs.inertiaZ );

        if ( parsed != 20 )
        {
            Fail( "Invalid ball_state at line %d (expected 20 fields, got %d)", m_lineNumber, parsed );
        }

        m_scene.m_ballStates.push_back( bs );
    }

    void ParseWorld( const char* args )
    {
        float gravity = 0.0f;
        float fluidHeight = 0.0f;
        float fluidDensity = 0.0f;
        const int parsed = sscanf_s( RequireArgs( "world", args, "world <gravity> <fluidHeight> <fluidDensity>" ), "%f %f %f", &gravity, &fluidHeight, &fluidDensity );
        if ( parsed != 3 )
        {
            Fail( "Invalid world at line %d (expected: world <gravity> <fluidHeight> <fluidDensity>)", m_lineNumber );
        }
        m_scene.m_worldOverride.hasWorldOverride = true;
        m_scene.m_worldOverride.worldGravity = gravity;
        m_scene.m_worldOverride.worldFluidHeight = fluidHeight;
        m_scene.m_worldOverride.worldFluidDensity = fluidDensity;
    }

    void ParseWaterHidden( const char* args )
    {
        ParseStrictOnOff( "water_hidden", RequireArgs( "water_hidden", args, "water_hidden on|off" ), m_scene.m_sceneOptions.waterHidden );
    }

    void ParseTerrainHidden( const char* args )
    {
        ParseStrictOnOff( "terrain_hidden", RequireArgs( "terrain_hidden", args, "terrain_hidden on|off" ), m_scene.m_sceneOptions.terrainHidden );
    }

    void ParseSolverBalls( const char* args )
    {
        m_scene.m_sceneOptions.solverBallCount = ParseIntArg( "solver_balls", args, "solver_balls <count>" );
        if ( m_scene.m_sceneOptions.solverBallCount < 0 )
        {
            Fail( "Invalid solver_balls count at line %d (must be >= 0)", m_lineNumber );
        }
    }

    void ParseSolverBoxes( const char* args )
    {
        m_scene.m_sceneOptions.solverBoxCount = ParseIntArg( "solver_boxes", args, "solver_boxes <count>" );
        if ( m_scene.m_sceneOptions.solverBoxCount < 0 )
        {
            Fail( "Invalid solver_boxes count at line %d (must be >= 0)", m_lineNumber );
        }
    }

    bool TryParseCinematicDirective( const char* line )
    {
        // Cinematic scene directives all share the cinematic_ prefix. Keeping
        // them in one parser function makes it easy to compare the scene-file
        // names with CinematicRenderConfig fields.
        if ( strncmp( line, "cinematic_", 10 ) != 0 )
        {
            return false;
        }

        const char* cursor = line;
        char key[96] = {};
        char value[96] = {};
        if ( !ReadToken( cursor, key, sizeof( key ) ) || !ReadToken( cursor, value, sizeof( value ) ) )
        {
            Fail( "Invalid cinematic directive at line %d", m_lineNumber );
        }

        struct BoolDirective
        {
            // Boolean directives toggle whole passes/features such as bloom,
            // fog, clouds, or the master cinematic renderer.
            const char* name;
            bool CinematicRenderConfig::* field;
            uint64_t bit;
        };
        static constexpr BoolDirective kBoolDirectives[] = {
            { "cinematic_rendering", &CinematicRenderConfig::enabled, SCENE_CINE_RENDERING },
            { "cinematic_sky_atmosphere", &CinematicRenderConfig::skyAtmosphereEnabled, SCENE_CINE_SKY_ATMOSPHERE },
            { "cinematic_clouds", &CinematicRenderConfig::cloudsEnabled, SCENE_CINE_CLOUDS },
            { "cinematic_god_rays", &CinematicRenderConfig::godRaysEnabled, SCENE_CINE_GOD_RAYS },
            { "cinematic_volumetric_lighting", &CinematicRenderConfig::volumetricLightingEnabled, SCENE_CINE_VOLUMETRIC_LIGHTING },
            { "cinematic_bloom", &CinematicRenderConfig::bloomEnabled, SCENE_CINE_BLOOM },
            { "cinematic_fog", &CinematicRenderConfig::fogEnabled, SCENE_CINE_FOG },
            { "cinematic_terrain_relief_enabled", &CinematicRenderConfig::terrainReliefEnabled, SCENE_CINE_TERRAIN_RELIEF_ENABLED },
        };
        for ( const BoolDirective& directive : kBoolDirectives )
        {
            if ( strcmp( key, directive.name ) == 0 )
            {
                bool parsedValue = false;
                if ( !ParseOnOff( value, parsedValue ) )
                {
                    Fail( "Invalid %s value at line %d: %s", key, m_lineNumber, value );
                }
                m_scene.m_sceneOptions.cinematicRender.*( directive.field ) = parsedValue;
                m_scene.m_sceneOptions.cinematicOverrideMask |= directive.bit;
                if ( directive.bit == SCENE_CINE_RENDERING )
                {
                    m_scene.m_sceneOptions.hasCinematicRenderingOverride = true;
                    m_scene.m_sceneOptions.cinematicRendering = parsedValue;
                }
                return true;
            }
        }

        struct FloatDirective
        {
            // Float directives are the slider-like values. Each entry names the
            // scene token, the config field it writes, the override bit to mark,
            // and a safe authoring range.
            const char* name;
            float CinematicRenderConfig::* field;
            uint64_t bit;
            float minValue;
            float maxValue;
        };
        static constexpr FloatDirective kFloatDirectives[] = {
            { "cinematic_exposure", &CinematicRenderConfig::exposure, SCENE_CINE_EXPOSURE, 0.0f, 16.0f },
            { "cinematic_gamma", &CinematicRenderConfig::gamma, SCENE_CINE_GAMMA, 0.1f, 8.0f },
            { "cinematic_sun_screen_x", &CinematicRenderConfig::sunScreenX, SCENE_CINE_SUN_SCREEN_X, 0.0f, 1.0f },
            { "cinematic_sun_screen_y", &CinematicRenderConfig::sunScreenY, SCENE_CINE_SUN_SCREEN_Y, 0.0f, 1.0f },
            { "cinematic_sun_color_r", &CinematicRenderConfig::sunColorR, SCENE_CINE_SUN_COLOR_R, 0.0f, 4.0f },
            { "cinematic_sun_color_g", &CinematicRenderConfig::sunColorG, SCENE_CINE_SUN_COLOR_G, 0.0f, 4.0f },
            { "cinematic_sun_color_b", &CinematicRenderConfig::sunColorB, SCENE_CINE_SUN_COLOR_B, 0.0f, 4.0f },
            { "cinematic_sun_intensity", &CinematicRenderConfig::sunIntensity, SCENE_CINE_SUN_INTENSITY, 0.0f, 80.0f },
            { "cinematic_sky_horizon_r", &CinematicRenderConfig::skyHorizonR, SCENE_CINE_SKY_HORIZON_R, 0.0f, 4.0f },
            { "cinematic_sky_horizon_g", &CinematicRenderConfig::skyHorizonG, SCENE_CINE_SKY_HORIZON_G, 0.0f, 4.0f },
            { "cinematic_sky_horizon_b", &CinematicRenderConfig::skyHorizonB, SCENE_CINE_SKY_HORIZON_B, 0.0f, 4.0f },
            { "cinematic_sky_zenith_r", &CinematicRenderConfig::skyZenithR, SCENE_CINE_SKY_ZENITH_R, 0.0f, 4.0f },
            { "cinematic_sky_zenith_g", &CinematicRenderConfig::skyZenithG, SCENE_CINE_SKY_ZENITH_G, 0.0f, 4.0f },
            { "cinematic_sky_zenith_b", &CinematicRenderConfig::skyZenithB, SCENE_CINE_SKY_ZENITH_B, 0.0f, 4.0f },
            { "cinematic_sky_glow_strength", &CinematicRenderConfig::skyGlowStrength, SCENE_CINE_SKY_GLOW_STRENGTH, 0.0f, 16.0f },
            { "cinematic_cloud_coverage", &CinematicRenderConfig::cloudCoverage, SCENE_CINE_CLOUD_COVERAGE, 0.0f, 1.0f },
            { "cinematic_cloud_softness", &CinematicRenderConfig::cloudSoftness, SCENE_CINE_CLOUD_SOFTNESS, 0.001f, 1.0f },
            { "cinematic_cloud_scale", &CinematicRenderConfig::cloudScale, SCENE_CINE_CLOUD_SCALE, 0.1f, 64.0f },
            { "cinematic_cloud_intensity", &CinematicRenderConfig::cloudIntensity, SCENE_CINE_CLOUD_INTENSITY, 0.0f, 4.0f },
            { "cinematic_sun_shaft_strength", &CinematicRenderConfig::sunShaftStrength, SCENE_CINE_SUN_SHAFT_STRENGTH, 0.0f, 8.0f },
            { "cinematic_sun_shaft_falloff", &CinematicRenderConfig::sunShaftFalloff, SCENE_CINE_SUN_SHAFT_FALLOFF, 0.1f, 10.0f },
            { "cinematic_volumetric_strength", &CinematicRenderConfig::volumetricStrength, SCENE_CINE_VOLUMETRIC_STRENGTH, 0.0f, 8.0f },
            { "cinematic_volumetric_density", &CinematicRenderConfig::volumetricDensity, SCENE_CINE_VOLUMETRIC_DENSITY, 0.0f, 8.0f },
            { "cinematic_volumetric_decay", &CinematicRenderConfig::volumetricDecay, SCENE_CINE_VOLUMETRIC_DECAY, 0.0f, 1.0f },
            { "cinematic_bloom_threshold", &CinematicRenderConfig::bloomThreshold, SCENE_CINE_BLOOM_THRESHOLD, 0.0f, 16.0f },
            { "cinematic_bloom_knee", &CinematicRenderConfig::bloomKnee, SCENE_CINE_BLOOM_KNEE, 0.001f, 8.0f },
            { "cinematic_bloom_strength", &CinematicRenderConfig::bloomStrength, SCENE_CINE_BLOOM_STRENGTH, 0.0f, 8.0f },
            { "cinematic_bloom_radius", &CinematicRenderConfig::bloomRadius, SCENE_CINE_BLOOM_RADIUS, 0.1f, 32.0f },
            { "cinematic_terrain_relief", &CinematicRenderConfig::terrainRelief, SCENE_CINE_TERRAIN_RELIEF, 0.0f, 4.0f },
            { "cinematic_basin_depth", &CinematicRenderConfig::basinDepth, SCENE_CINE_BASIN_DEPTH, 0.0f, 256.0f },
            { "cinematic_basin_rim_lift", &CinematicRenderConfig::basinRimLift, SCENE_CINE_BASIN_RIM_LIFT, 0.0f, 256.0f },
            { "cinematic_fog_color_r", &CinematicRenderConfig::fogColorR, SCENE_CINE_FOG_COLOR_R, 0.0f, 4.0f },
            { "cinematic_fog_color_g", &CinematicRenderConfig::fogColorG, SCENE_CINE_FOG_COLOR_G, 0.0f, 4.0f },
            { "cinematic_fog_color_b", &CinematicRenderConfig::fogColorB, SCENE_CINE_FOG_COLOR_B, 0.0f, 4.0f },
            { "cinematic_fog_start", &CinematicRenderConfig::fogStart, SCENE_CINE_FOG_START, 0.0f, 10000.0f },
            { "cinematic_fog_end", &CinematicRenderConfig::fogEnd, SCENE_CINE_FOG_END, 0.0f, 20000.0f },
            { "cinematic_fog_density", &CinematicRenderConfig::fogDensity, SCENE_CINE_FOG_DENSITY, 0.0f, 0.1f },
            { "cinematic_fog_max_opacity", &CinematicRenderConfig::fogMaxOpacity, SCENE_CINE_FOG_MAX_OPACITY, 0.0f, 1.0f },
        };
        for ( const FloatDirective& directive : kFloatDirectives )
        {
            if ( strcmp( key, directive.name ) == 0 )
            {
                const float parsedValue = ParseFloatValue( key, value );
                if ( parsedValue < directive.minValue || parsedValue > directive.maxValue )
                {
                    Fail( "Invalid %s at line %d (expected %.3f..%.3f)", key, m_lineNumber, directive.minValue, directive.maxValue );
                }
                m_scene.m_sceneOptions.cinematicRender.*( directive.field ) = parsedValue;
                m_scene.m_sceneOptions.cinematicOverrideMask |= directive.bit;
                if ( directive.bit == SCENE_CINE_EXPOSURE )
                {
                    m_scene.m_sceneOptions.hasCinematicExposure = true;
                    m_scene.m_sceneOptions.cinematicExposure = parsedValue;
                }
                else if ( directive.bit == SCENE_CINE_GAMMA )
                {
                    m_scene.m_sceneOptions.hasCinematicGamma = true;
                    m_scene.m_sceneOptions.cinematicGamma = parsedValue;
                }
                return true;
            }
        }

        Fail( "Unknown cinematic directive at line %d: %s", m_lineNumber, key );
    }

    bool DispatchLine( const char* line )
    {
        if ( TryParseCinematicDirective( line ) )
        {
            return true;
        }

        static const SceneDirective directives[] = {
            { "physics", &TestSceneParser::ParsePhysics, "physics on|off" },
            { "text", &TestSceneParser::ParseText, "text on|off" },
            { "text_only", &TestSceneParser::ParseTextOnly, "text_only on|off" },
            { "ui", &TestSceneParser::ParseUI, "ui <command> <value>" },
            { "UI", &TestSceneParser::ParseUI, "UI <command> <value>" },
            { "ui_test_pattern", &TestSceneParser::ParseUITestPattern, "ui_test_pattern on|off" },
            { "UI_test_pattern", &TestSceneParser::ParseUITestPattern, "UI_test_pattern on|off" },
            { "frames", &TestSceneParser::ParseFrames, "frames <N|unlimited>" },
            { "screenshot", &TestSceneParser::ParseScreenshot, "screenshot <path> frame|ms <N>" },
            { "seed", &TestSceneParser::ParseSeed, "seed <N>" },
            { "perf_log", &TestSceneParser::ParsePerfLog, "perf_log <path>" },
            { "perf_log_flush", &TestSceneParser::ParsePerfLogFlush, "perf_log_flush on|off" },
            { "perf_log_flush_interval", &TestSceneParser::ParsePerfLogFlushInterval, "perf_log_flush_interval <N>" },
            { "vsync", &TestSceneParser::ParseVsync, "vsync on|off" },
            { "pipeline_sync", &TestSceneParser::ParsePipelineSync, "pipeline_sync on|off" },
            { "screenshot_and_exit", &TestSceneParser::ParseScreenshotAndExit, "screenshot_and_exit" },
            { "exit_on_complete", &TestSceneParser::ParseExitOnComplete, "exit_on_complete" },
            { "collision_visualizer", &TestSceneParser::ParseCollisionVisualizer, "collision_visualizer on|off" },
            { "broadphase_overlay", &TestSceneParser::ParseBroadphaseOverlay, "broadphase_overlay on|off" },
            { "water_freeze", &TestSceneParser::ParseWaterFreeze, "water_freeze on|off" },
            { "water_flat", &TestSceneParser::ParseWaterFlat, "water_flat on|off" },
            { "water_reflection", &TestSceneParser::ParseWaterReflection, "water_reflection fbo|dxr|none" },
            { "screenshot_interval", &TestSceneParser::ParseScreenshotInterval, "screenshot_interval <dir> <N>" },
            { "camera", &TestSceneParser::ParseCamera, "camera <name> <pos> <view> <up>" },
            { "ball", &TestSceneParser::ParseBall, "ball <name> ..." },
            { "box", &TestSceneParser::ParseBox, "box <name> ..." },
            { "floating_box", &TestSceneParser::ParseFloatingBox, "floating_box <name> ..." },
            { "time_scale", &TestSceneParser::ParseTimeScale, "time_scale <value>" },
            { "fixed_step", &TestSceneParser::ParseFixedStep, "fixed_step" },
            { "physics_debug", &TestSceneParser::ParsePhysicsDebug, "physics_debug none|axes|contacts|sleep|pipeline|all" },
            { "physics_debug_axes", &TestSceneParser::ParsePhysicsDebugAxes, "physics_debug_axes on|off" },
            { "physics_debug_contacts", &TestSceneParser::ParsePhysicsDebugContacts, "physics_debug_contacts on|off" },
            { "physics_debug_sleep", &TestSceneParser::ParsePhysicsDebugSleep, "physics_debug_sleep on|off" },
            { "physics_debug_pipeline", &TestSceneParser::ParsePhysicsDebugPipeline, "physics_debug_pipeline on|off" },
            { "physics_debug_transparent", &TestSceneParser::ParsePhysicsDebugTransparent, "physics_debug_transparent on|off" },
            { "physics_debug_alpha", &TestSceneParser::ParsePhysicsDebugAlpha, "physics_debug_alpha <0.05..1.0>" },
            { "physics_debug_contact_linger", &TestSceneParser::ParsePhysicsDebugContactLinger, "physics_debug_contact_linger <0.0..5.0>" },
            { "track_height", &TestSceneParser::ParseTrackHeight, "track_height <height>" },
            { "auto_cycle_interval", &TestSceneParser::ParseAutoCycleInterval, "auto_cycle_interval <seconds>" },
            { "flat_slope", &TestSceneParser::ParseFlatSlope, "flat_slope <baseY> <slopeX> <slopeZ>" },
            { "ball_state", &TestSceneParser::ParseBallState, "ball_state <name> ..." },
            { "world", &TestSceneParser::ParseWorld, "world <gravity> <fluidHeight> <fluidDensity>" },
            { "water_hidden", &TestSceneParser::ParseWaterHidden, "water_hidden on|off" },
            { "terrain_hidden", &TestSceneParser::ParseTerrainHidden, "terrain_hidden on|off" },
            { "solver_balls", &TestSceneParser::ParseSolverBalls, "solver_balls <count>" },
            { "solver_boxes", &TestSceneParser::ParseSolverBoxes, "solver_boxes <count>" },
        };

        const char* args = nullptr;
        for ( const SceneDirective& directive : directives )
        {
            if ( MatchDirective( line, directive.name, args ) )
            {
                ( this->*directive.parse )( args );
                return true;
            }
        }
        return false;
    }

  public:
    explicit TestSceneParser( const char* path )
        : m_path( path )
    {
    }

    ~TestSceneParser() = default;

    TestScene Load()
    {
        FILE* rawFile = nullptr;
        const errno_t err = fopen_s( &rawFile, m_path, "r" );
        if ( err != 0 || !rawFile )
        {
            char msg[256];
            sprintf_s( msg, sizeof( msg ), "Failed to open scene file: %s  (TestScene::LoadFromFile)", m_path );
            throw std::runtime_error( msg );
        }
        m_file.reset( rawFile );

        char line[512];
        while ( fgets( line, sizeof( line ), m_file.get() ) )
        {
            ++m_lineNumber;

            size_t len = strlen( line );
            while ( len > 0 && ( line[len - 1] == '\n' || line[len - 1] == '\r' ) )
            {
                line[--len] = '\0';
            }

            if ( line[0] == '\0' || line[0] == '#' )
            {
                continue;
            }

            if ( !DispatchLine( line ) )
            {
                Fail( "Unknown directive at line %d: %.64s", m_lineNumber, line );
            }
        }

        m_file.reset();

        if ( m_scene.m_cameras.empty() )
        {
            throw std::runtime_error( "Scene file must define at least one camera.  (TestScene::LoadFromFile)" );
        }

        return m_scene;
    }
};


TestScene LoadTestSceneFromFileImpl( const char* path )
{
    return TestSceneParser( path ).Load();
}
} // namespace Basics
} // namespace SkullbonezCore
