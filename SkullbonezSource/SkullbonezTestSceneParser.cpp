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
struct SceneIntOption
{
    const char* name;
    int value;
};

template <size_t N>
bool TryParseIntOption( const char* token, const SceneIntOption ( &options )[N], int& out )
{
    if ( !token )
    {
        return false;
    }

    for ( const SceneIntOption& option : options )
    {
        if ( strcmp( token, option.name ) == 0 )
        {
            out = option.value;
            return true;
        }
    }
    return false;
}

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
    static const SceneIntOption kTabs[] = {
        { "profiler", 0 },
        { "profile", 0 },
        { "scene", 1 },
        { "overview", 1 },
        { "info", 1 },
        { "physics", 2 },
        { "options", 3 },
        { "params", 3 },
        { "renderer", 3 },
        { "keys", 4 },
        { "controls", 4 },
        { "cinematic", 5 },
        { "cine", 5 },
        { "look", 5 },
    };
    return TryParseIntOption( value, kTabs, outTab );
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
    int m_styleIncludeDepth = 0;
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

    void ParseNextToken( const char* directive, const char*& cursor, char* out, size_t outSize, const char* expected )
    {
        if ( !ReadToken( cursor, out, outSize ) )
        {
            Fail( "Invalid %s at line %d (expected: %s)", directive, m_lineNumber, expected );
        }
    }

    int ParseTokenList( const char* directive, const char* args, const char* expected, char tokens[][64], int maxTokens )
    {
        const char* cursor = RequireArgs( directive, args, expected );
        int count = 0;
        while ( true )
        {
            while ( IsSpace( *cursor ) )
            {
                ++cursor;
            }
            if ( *cursor == '\0' )
            {
                return count;
            }

            char discard[64] = {};
            char* target = ( count < maxTokens ) ? tokens[count] : discard;
            ParseNextToken( directive, cursor, target, 64, expected );
            ++count;
        }
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

    float ParseNextFloatToken( const char* directive, const char*& cursor, const char* expected )
    {
        char value[64] = {};
        if ( !ReadToken( cursor, value, sizeof( value ) ) )
        {
            Fail( "Invalid %s at line %d (expected: %s)", directive, m_lineNumber, expected );
        }
        return ParseFloatValue( directive, value );
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

    float ParseMaterialModeValue( const char* directive, const char* value )
    {
        float parsed = 0.0f;
        if ( TryParseFloat( value, parsed ) )
        {
            return parsed;
        }

        static const SceneIntOption kMaterialModes[] = {
            { "texture", -1 },
            { "beachball", -1 },
            { "matte", 1 },
            { "solid", 1 },
            { "metal", 2 },
            { "chrome", 2 },
            { "emissive", 3 },
            { "neon", 3 },
            { "glass", 4 },
            { "toon", 5 },
            { "pixar", 5 },
            { "lowpoly", 6 },
            { "shadow", 7 },
            { "black", 7 },
            { "foliage", 8 },
            { "leaf", 8 },
            { "leaves", 8 },
            { "bark", 9 },
            { "trunk", 9 },
            { "stone", 10 },
            { "rock", 10 },
            { "ridge", 11 },
            { "distant", 11 },
            { "shore", 12 },
            { "sand", 12 },
        };
        int mode = 0;
        if ( TryParseIntOption( value, kMaterialModes, mode ) )
        {
            return static_cast<float>( mode );
        }

        Fail( "Invalid %s material mode at line %d: %s", directive, m_lineNumber, value ? value : "" );
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
        const char* expected = "screenshot <path> frame|ms <N>";
        const char* cursor = RequireArgs( "screenshot", args, expected );
        char outPath[256] = {};
        char triggerType[16] = {};
        ParseNextToken( "screenshot", cursor, outPath, sizeof( outPath ), expected );
        ParseNextToken( "screenshot", cursor, triggerType, sizeof( triggerType ), expected );
        const int triggerValue = ParseNextIntToken( "screenshot", cursor, expected );

        if ( triggerValue <= 0 )
        {
            Fail( "Invalid screenshot at line %d (expected: %s)", m_lineNumber, expected );
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
        static const SceneIntOption kWaterReflectionModes[] = {
            { "fbo", 0 },
            { "on", 0 },
            { "dxr", 1 },
            { "rt", 1 },
            { "none", 2 },
            { "off", 2 },
        };
        int mode = 0;
        if ( !TryParseIntOption( value, kWaterReflectionModes, mode ) )
        {
            Fail( "Invalid water_reflection value at line %d", m_lineNumber );
        }
        m_scene.m_sceneOptions.waterReflectionMode = mode;
    }

    void ParseScreenshotInterval( const char* args )
    {
        const char* expected = "screenshot_interval <dir> <N>";
        const char* cursor = RequireArgs( "screenshot_interval", args, expected );
        char outDir[256] = {};
        ParseNextToken( "screenshot_interval", cursor, outDir, sizeof( outDir ), expected );
        const int intervalFrames = ParseNextIntToken( "screenshot_interval", cursor, expected );

        if ( intervalFrames <= 0 )
        {
            Fail( "Invalid screenshot_interval at line %d (expected: %s)", m_lineNumber, expected );
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

        const char* expected = "camera <name> <pos> <view> <up>";
        const char* cursor = RequireArgs( "camera", args, expected );
        SceneCamera cam;
        memset( &cam, 0, sizeof( cam ) );

        ParseNextToken( "camera", cursor, cam.name, sizeof( cam.name ), expected );
        cam.m_position.x = ParseNextFloatToken( "camera", cursor, expected );
        cam.m_position.y = ParseNextFloatToken( "camera", cursor, expected );
        cam.m_position.z = ParseNextFloatToken( "camera", cursor, expected );
        cam.view.x = ParseNextFloatToken( "camera", cursor, expected );
        cam.view.y = ParseNextFloatToken( "camera", cursor, expected );
        cam.view.z = ParseNextFloatToken( "camera", cursor, expected );
        cam.up.x = ParseNextFloatToken( "camera", cursor, expected );
        cam.up.y = ParseNextFloatToken( "camera", cursor, expected );
        cam.up.z = ParseNextFloatToken( "camera", cursor, expected );

        m_scene.m_cameras.push_back( cam );
    }

    void ParseStyle( const char* args )
    {
        const char* expected = "style <name|path>";
        const char* cursor = RequireArgs( "style", args, expected );
        char token[260] = {};
        ParseNextToken( "style", cursor, token, sizeof( token ), expected );

        if ( m_styleIncludeDepth >= 8 )
        {
            Fail( "Style include depth exceeded at line %d", m_lineNumber );
        }

        char stylePath[300] = {};
        if ( strchr( token, '/' ) || strchr( token, '\\' ) || strstr( token, ".style" ) )
        {
            strcpy_s( stylePath, sizeof( stylePath ), token );
        }
        else
        {
            sprintf_s( stylePath, sizeof( stylePath ), "SkullbonezData/styles/%s.style", token );
        }

        FILE* rawStyleFile = nullptr;
        const errno_t err = fopen_s( &rawStyleFile, stylePath, "r" );
        if ( err != 0 || !rawStyleFile )
        {
            Fail( "Failed to open style file at line %d: %s", m_lineNumber, stylePath );
        }

        SceneFileHandle styleFile( rawStyleFile );
        const int parentLineNumber = m_lineNumber;
        ++m_styleIncludeDepth;

        char line[512];
        int styleLineNumber = 0;
        while ( fgets( line, sizeof( line ), styleFile.get() ) )
        {
            ++styleLineNumber;
            m_lineNumber = styleLineNumber;

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
                Fail( "Unknown directive in style file %s at line %d: %.64s", stylePath, styleLineNumber, line );
            }
        }

        --m_styleIncludeDepth;
        m_lineNumber = parentLineNumber;
    }

    void ParseBallCommon( const char* args, bool isFixed )
    {
        const char* directive = isFixed ? "floating_ball" : "ball";
        const char* expected = isFixed ? "floating_ball <name> <pos> <radius> <mass> <moment> <restitution> [force forcePos] [euler]" : "ball <name> <pos> <radius> <mass> <moment> <restitution> [force forcePos] [euler]";
        char tokens[17][64] = {};
        const int parsed = ParseTokenList( directive, args, expected, tokens, 17 );
        if ( parsed != 8 && parsed != 11 && parsed != 14 && parsed != 17 )
        {
            Fail( "Invalid %s at line %d (expected 8, 11, 14 or 17 fields, got %d)", directive, m_lineNumber, parsed );
        }

        SceneBall ball;
        memset( &ball, 0, sizeof( ball ) );
        ball.hasInitOrient = false;
        ball.isFixed = isFixed;

        strcpy_s( ball.name, sizeof( ball.name ), tokens[0] );
        ball.posX = ParseFloatValue( directive, tokens[1] );
        ball.posY = ParseFloatValue( directive, tokens[2] );
        ball.posZ = ParseFloatValue( directive, tokens[3] );
        ball.m_radius = ParseFloatValue( directive, tokens[4] );
        ball.m_mass = ParseFloatValue( directive, tokens[5] );
        ball.moment = ParseFloatValue( directive, tokens[6] );
        ball.restitution = ParseFloatValue( directive, tokens[7] );

        if ( parsed == 11 )
        {
            ball.eulerX = ParseFloatValue( directive, tokens[8] );
            ball.eulerY = ParseFloatValue( directive, tokens[9] );
            ball.eulerZ = ParseFloatValue( directive, tokens[10] );
            ball.hasInitOrient = true;
        }
        else if ( parsed == 14 || parsed == 17 )
        {
            ball.forceX = ParseFloatValue( directive, tokens[8] );
            ball.forceY = ParseFloatValue( directive, tokens[9] );
            ball.forceZ = ParseFloatValue( directive, tokens[10] );
            ball.forcePosX = ParseFloatValue( directive, tokens[11] );
            ball.forcePosY = ParseFloatValue( directive, tokens[12] );
            ball.forcePosZ = ParseFloatValue( directive, tokens[13] );
            if ( parsed == 17 )
            {
                ball.eulerX = ParseFloatValue( directive, tokens[14] );
                ball.eulerY = ParseFloatValue( directive, tokens[15] );
                ball.eulerZ = ParseFloatValue( directive, tokens[16] );
                ball.hasInitOrient = true;
            }
        }

        m_scene.m_balls.push_back( ball );
    }

    void ParseBall( const char* args )
    {
        ParseBallCommon( args, false );
    }

    void ParseFloatingBall( const char* args )
    {
        ParseBallCommon( args, true );
    }

    void ParseBoxCommon( const char* args, bool isFixed )
    {
        const char* directive = isFixed ? "floating_box" : "box";
        const char* expected = isFixed ? "floating_box <name> <pos> <halfExtents> <mass> <restitution> [euler] [velocity]" : "box <name> <pos> <halfExtents> <mass> <restitution> [euler] [velocity]";
        char tokens[15][64] = {};
        const int parsed = ParseTokenList( directive, args, expected, tokens, 15 );
        if ( parsed != 9 && parsed != 12 && parsed != 15 )
        {
            Fail( "Invalid box/floating_box at line %d (expected 9, 12, or 15 fields, got %d)", m_lineNumber, parsed );
        }

        SceneBox box;
        memset( &box, 0, sizeof( box ) );
        box.hasInitOrient = false;
        box.hasInitVelocity = false;
        box.isFixed = isFixed;

        strcpy_s( box.name, sizeof( box.name ), tokens[0] );
        box.posX = ParseFloatValue( directive, tokens[1] );
        box.posY = ParseFloatValue( directive, tokens[2] );
        box.posZ = ParseFloatValue( directive, tokens[3] );
        box.halfX = ParseFloatValue( directive, tokens[4] );
        box.halfY = ParseFloatValue( directive, tokens[5] );
        box.halfZ = ParseFloatValue( directive, tokens[6] );
        box.mass = ParseFloatValue( directive, tokens[7] );
        box.restitution = ParseFloatValue( directive, tokens[8] );

        if ( parsed == 12 || parsed == 15 )
        {
            box.eulerX = ParseFloatValue( directive, tokens[9] );
            box.eulerY = ParseFloatValue( directive, tokens[10] );
            box.eulerZ = ParseFloatValue( directive, tokens[11] );
            box.hasInitOrient = true;
        }
        if ( parsed == 15 )
        {
            box.velX = ParseFloatValue( directive, tokens[12] );
            box.velY = ParseFloatValue( directive, tokens[13] );
            box.velZ = ParseFloatValue( directive, tokens[14] );
            box.hasInitVelocity = true;
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
        const char* expected = "flat_slope <baseY> <slopeX> <slopeZ>";
        const char* cursor = RequireArgs( "flat_slope", args, expected );
        const float baseY = ParseNextFloatToken( "flat_slope", cursor, expected );
        const float slopeX = ParseNextFloatToken( "flat_slope", cursor, expected );
        const float slopeZ = ParseNextFloatToken( "flat_slope", cursor, expected );
        m_scene.m_terrainOverride.hasFlatSlope = true;
        m_scene.m_terrainOverride.flatBaseY = baseY;
        m_scene.m_terrainOverride.flatSlopeX = slopeX;
        m_scene.m_terrainOverride.flatSlopeZ = slopeZ;
    }

    void ParseBallState( const char* args )
    {
        const char* expected = "ball_state <name> <position> <velocity> <angular_velocity> <orientation> <radius> <mass> <restitution> <inertia>";
        const char* cursor = RequireArgs( "ball_state", args, expected );
        SceneBallState bs;
        memset( &bs, 0, sizeof( bs ) );

        ParseNextToken( "ball_state", cursor, bs.name, sizeof( bs.name ), expected );
        bs.posX = ParseNextFloatToken( "ball_state", cursor, expected );
        bs.posY = ParseNextFloatToken( "ball_state", cursor, expected );
        bs.posZ = ParseNextFloatToken( "ball_state", cursor, expected );
        bs.velX = ParseNextFloatToken( "ball_state", cursor, expected );
        bs.velY = ParseNextFloatToken( "ball_state", cursor, expected );
        bs.velZ = ParseNextFloatToken( "ball_state", cursor, expected );
        bs.angVelX = ParseNextFloatToken( "ball_state", cursor, expected );
        bs.angVelY = ParseNextFloatToken( "ball_state", cursor, expected );
        bs.angVelZ = ParseNextFloatToken( "ball_state", cursor, expected );
        bs.orientX = ParseNextFloatToken( "ball_state", cursor, expected );
        bs.orientY = ParseNextFloatToken( "ball_state", cursor, expected );
        bs.orientZ = ParseNextFloatToken( "ball_state", cursor, expected );
        bs.orientW = ParseNextFloatToken( "ball_state", cursor, expected );
        bs.radius = ParseNextFloatToken( "ball_state", cursor, expected );
        bs.mass = ParseNextFloatToken( "ball_state", cursor, expected );
        bs.restitution = ParseNextFloatToken( "ball_state", cursor, expected );
        bs.inertiaX = ParseNextFloatToken( "ball_state", cursor, expected );
        bs.inertiaY = ParseNextFloatToken( "ball_state", cursor, expected );
        bs.inertiaZ = ParseNextFloatToken( "ball_state", cursor, expected );

        m_scene.m_ballStates.push_back( bs );
    }

    void ParseWorld( const char* args )
    {
        const char* expected = "world <gravity> <fluidHeight> <fluidDensity>";
        const char* cursor = RequireArgs( "world", args, expected );
        const float gravity = ParseNextFloatToken( "world", cursor, expected );
        const float fluidHeight = ParseNextFloatToken( "world", cursor, expected );
        const float fluidDensity = ParseNextFloatToken( "world", cursor, expected );
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

    static uint64_t ConceptLookOverrideMask()
    {
        return SCENE_CINE_RENDERING |
               SCENE_CINE_SKY_ATMOSPHERE |
               SCENE_CINE_CLOUDS |
               SCENE_CINE_GOD_RAYS |
               SCENE_CINE_VOLUMETRIC_LIGHTING |
               SCENE_CINE_BLOOM |
               SCENE_CINE_FOG |
               SCENE_CINE_TERRAIN_RELIEF_ENABLED |
               SCENE_CINE_EXPOSURE |
               SCENE_CINE_GAMMA |
               SCENE_CINE_SUN_SCREEN_X |
               SCENE_CINE_SUN_SCREEN_Y |
               SCENE_CINE_SUN_COLOR_R |
               SCENE_CINE_SUN_COLOR_G |
               SCENE_CINE_SUN_COLOR_B |
               SCENE_CINE_SUN_INTENSITY |
               SCENE_CINE_SKY_HORIZON_R |
               SCENE_CINE_SKY_HORIZON_G |
               SCENE_CINE_SKY_HORIZON_B |
               SCENE_CINE_SKY_ZENITH_R |
               SCENE_CINE_SKY_ZENITH_G |
               SCENE_CINE_SKY_ZENITH_B |
               SCENE_CINE_SKY_GLOW_STRENGTH |
               SCENE_CINE_CLOUD_COVERAGE |
               SCENE_CINE_CLOUD_SOFTNESS |
               SCENE_CINE_CLOUD_SCALE |
               SCENE_CINE_CLOUD_INTENSITY |
               SCENE_CINE_SUN_SHAFT_STRENGTH |
               SCENE_CINE_SUN_SHAFT_FALLOFF |
               SCENE_CINE_VOLUMETRIC_STRENGTH |
               SCENE_CINE_VOLUMETRIC_DENSITY |
               SCENE_CINE_VOLUMETRIC_DECAY |
               SCENE_CINE_BLOOM_THRESHOLD |
               SCENE_CINE_BLOOM_KNEE |
               SCENE_CINE_BLOOM_STRENGTH |
               SCENE_CINE_BLOOM_RADIUS |
               SCENE_CINE_TERRAIN_RELIEF |
               SCENE_CINE_BASIN_DEPTH |
               SCENE_CINE_BASIN_RIM_LIFT |
               SCENE_CINE_FOG_COLOR_R |
               SCENE_CINE_FOG_COLOR_G |
               SCENE_CINE_FOG_COLOR_B |
               SCENE_CINE_FOG_START |
               SCENE_CINE_FOG_END |
               SCENE_CINE_FOG_DENSITY |
               SCENE_CINE_FOG_MAX_OPACITY |
               SCENE_CINE_STYLE_MODES |
               SCENE_CINE_STYLE_GRADE |
               SCENE_CINE_TERRAIN_TINT |
               SCENE_CINE_TERRAIN_ACCENT |
               SCENE_CINE_TERRAIN_GRID |
               SCENE_CINE_WATER_TINT |
               SCENE_CINE_WATER_PROFILE |
               SCENE_CINE_BASIN_MASK;
    }

    static void SetLookColors( CinematicRenderConfig& c,
                               float sunR,
                               float sunG,
                               float sunB,
                               float horizonR,
                               float horizonG,
                               float horizonB,
                               float zenithR,
                               float zenithG,
                               float zenithB,
                               float terrainR,
                               float terrainG,
                               float terrainB,
                               float accentR,
                               float accentG,
                               float accentB )
    {
        c.sunColorR = sunR;
        c.sunColorG = sunG;
        c.sunColorB = sunB;
        c.skyHorizonR = horizonR;
        c.skyHorizonG = horizonG;
        c.skyHorizonB = horizonB;
        c.skyZenithR = zenithR;
        c.skyZenithG = zenithG;
        c.skyZenithB = zenithB;
        c.terrainTintR = terrainR;
        c.terrainTintG = terrainG;
        c.terrainTintB = terrainB;
        c.terrainAccentR = accentR;
        c.terrainAccentG = accentG;
        c.terrainAccentB = accentB;
    }

    static void SetLookWater( CinematicRenderConfig& c, int mode, float r, float g, float b, float alpha, float reflection, float glint )
    {
        c.waterMode = mode;
        c.waterTintR = r;
        c.waterTintG = g;
        c.waterTintB = b;
        c.waterAlpha = alpha;
        c.waterReflectionStrength = reflection;
        c.waterGlintStrength = glint;
    }

    void ParseLook( const char* args )
    {
        const char* name = RequireArgs( "look", args, "look <concept_name>" );

        CinematicRenderConfig c;
        c.enabled = true;
        c.skyAtmosphereEnabled = true;
        c.cloudsEnabled = true;
        c.godRaysEnabled = true;
        c.volumetricLightingEnabled = true;
        c.bloomEnabled = true;
        c.fogEnabled = true;
        c.terrainReliefEnabled = false;
        c.terrainRelief = 0.0f;
        c.basinDepth = 0.0f;
        c.basinRimLift = 0.0f;
        c.basinCenterX = 620.0f;
        c.basinCenterZ = 615.0f;
        c.basinRadiusX = 220.0f;
        c.basinRadiusZ = 150.0f;
        c.basinFeather = 0.20f;
        c.terrainGridScale = 42.0f;
        c.terrainGridStrength = 0.0f;

        if ( strcmp( name, "golden_hour_realism" ) == 0 )
        {
            c.skyMode = 0;
            c.terrainMode = 0;
            c.objectStyle = 0;
            c.exposure = 0.70f;
            c.gamma = 2.05f;
            c.sunScreenX = 0.28f;
            c.sunScreenY = 0.76f;
            c.sunIntensity = 23.0f;
            c.styleSaturation = 1.10f;
            c.styleContrast = 1.08f;
            c.styleVignette = 0.74f;
            SetLookColors( c, 1.00f, 0.68f, 0.32f, 0.88f, 0.34f, 0.08f, 0.26f, 0.13f, 0.12f, 0.78f, 0.60f, 0.38f, 0.20f, 0.09f, 0.02f );
            SetLookWater( c, 1, 0.24f, 0.13f, 0.055f, 0.94f, 0.25f, 0.32f );
        }
        else if ( strcmp( name, "brutal_industrial" ) == 0 )
        {
            c.skyMode = 1;
            c.terrainMode = 1;
            c.objectStyle = 0;
            c.exposure = 0.58f;
            c.gamma = 2.15f;
            c.sunScreenX = 0.42f;
            c.sunScreenY = 0.82f;
            c.sunIntensity = 16.0f;
            c.styleSaturation = 0.42f;
            c.styleContrast = 1.34f;
            c.styleVignette = 0.66f;
            c.cloudCoverage = 0.84f;
            c.cloudIntensity = 1.05f;
            c.fogDensity = 0.0011f;
            c.fogMaxOpacity = 0.48f;
            SetLookColors( c, 0.74f, 0.70f, 0.62f, 0.42f, 0.42f, 0.38f, 0.10f, 0.11f, 0.12f, 0.42f, 0.42f, 0.39f, 0.15f, 0.15f, 0.13f );
            SetLookWater( c, 1, 0.055f, 0.060f, 0.058f, 0.90f, 0.38f, 0.10f );
        }
        else if ( strcmp( name, "studio_lighting_showcase" ) == 0 )
        {
            c.skyMode = 2;
            c.terrainMode = 2;
            c.objectStyle = 2;
            c.exposure = 0.90f;
            c.gamma = 2.05f;
            c.sunScreenX = 0.70f;
            c.sunScreenY = 0.76f;
            c.sunIntensity = 14.0f;
            c.styleSaturation = 1.02f;
            c.styleContrast = 1.16f;
            c.styleVignette = 0.82f;
            c.cloudsEnabled = false;
            c.fogEnabled = false;
            SetLookColors( c, 1.00f, 0.96f, 0.88f, 0.42f, 0.45f, 0.48f, 0.025f, 0.027f, 0.030f, 0.88f, 0.89f, 0.90f, 0.86f, 0.90f, 1.00f );
            SetLookWater( c, 1, 0.68f, 0.70f, 0.72f, 0.78f, 0.78f, 0.08f );
        }
        else if ( strcmp( name, "neon_cyberpunk" ) == 0 )
        {
            c.skyMode = 3;
            c.terrainMode = 3;
            c.objectStyle = 3;
            c.exposure = 0.82f;
            c.gamma = 1.92f;
            c.sunScreenX = 0.58f;
            c.sunScreenY = 0.70f;
            c.sunIntensity = 12.0f;
            c.styleSaturation = 1.55f;
            c.styleContrast = 1.24f;
            c.styleVignette = 0.60f;
            c.bloomThreshold = 0.45f;
            c.bloomStrength = 1.45f;
            c.bloomRadius = 6.4f;
            c.terrainGridScale = 28.0f;
            c.terrainGridStrength = 1.25f;
            SetLookColors( c, 0.30f, 0.95f, 1.80f, 0.05f, 0.02f, 0.16f, 0.00f, 0.01f, 0.05f, 0.02f, 0.05f, 0.12f, 0.10f, 0.95f, 1.00f );
            SetLookWater( c, 1, 0.035f, 0.05f, 0.11f, 0.92f, 0.62f, 0.68f );
        }
        else if ( strcmp( name, "alien_planet" ) == 0 )
        {
            c.skyMode = 4;
            c.terrainMode = 4;
            c.objectStyle = 0;
            c.exposure = 0.76f;
            c.gamma = 2.00f;
            c.sunScreenX = 0.22f;
            c.sunScreenY = 0.70f;
            c.sunIntensity = 18.0f;
            c.styleSaturation = 1.28f;
            c.styleContrast = 1.10f;
            c.styleVignette = 0.72f;
            SetLookColors( c, 0.95f, 0.46f, 1.35f, 0.48f, 0.18f, 0.72f, 0.12f, 0.04f, 0.20f, 0.50f, 0.18f, 0.68f, 0.10f, 0.95f, 0.62f );
            SetLookWater( c, 1, 0.16f, 0.06f, 0.26f, 0.88f, 0.34f, 0.42f );
        }
        else if ( strcmp( name, "desert_storm" ) == 0 )
        {
            c.skyMode = 5;
            c.terrainMode = 5;
            c.objectStyle = 1;
            c.exposure = 0.72f;
            c.gamma = 2.18f;
            c.sunScreenX = 0.36f;
            c.sunScreenY = 0.64f;
            c.sunIntensity = 26.0f;
            c.styleSaturation = 0.92f;
            c.styleContrast = 1.18f;
            c.styleVignette = 0.68f;
            c.cloudCoverage = 0.94f;
            c.cloudIntensity = 1.30f;
            c.fogDensity = 0.0042f;
            c.fogMaxOpacity = 0.82f;
            c.fogStart = 18.0f;
            c.fogEnd = 820.0f;
            SetLookColors( c, 1.20f, 0.72f, 0.28f, 0.76f, 0.48f, 0.20f, 0.18f, 0.13f, 0.08f, 0.84f, 0.52f, 0.24f, 0.36f, 0.18f, 0.05f );
            SetLookWater( c, 0, 0.08f, 0.06f, 0.04f, 0.0f, 0.0f, 0.0f );
        }
        else if ( strcmp( name, "painterly" ) == 0 )
        {
            c.skyMode = 6;
            c.terrainMode = 6;
            c.objectStyle = 5;
            c.exposure = 0.86f;
            c.gamma = 2.02f;
            c.sunScreenX = 0.68f;
            c.sunScreenY = 0.78f;
            c.sunIntensity = 18.0f;
            c.styleSaturation = 1.30f;
            c.styleContrast = 0.96f;
            c.styleVignette = 0.82f;
            SetLookColors( c, 1.00f, 0.70f, 0.48f, 0.72f, 0.48f, 0.74f, 0.24f, 0.44f, 0.78f, 0.58f, 0.68f, 0.34f, 0.80f, 0.44f, 0.28f );
            SetLookWater( c, 1, 0.18f, 0.42f, 0.50f, 0.82f, 0.20f, 0.18f );
        }
        else if ( strcmp( name, "retro_future_2005" ) == 0 )
        {
            c.skyMode = 7;
            c.terrainMode = 9;
            c.objectStyle = 0;
            c.exposure = 1.04f;
            c.gamma = 1.88f;
            c.sunScreenX = 0.72f;
            c.sunScreenY = 0.82f;
            c.sunIntensity = 31.0f;
            c.styleSaturation = 1.48f;
            c.styleContrast = 1.20f;
            c.styleVignette = 0.72f;
            c.bloomThreshold = 0.62f;
            c.bloomStrength = 1.20f;
            c.bloomRadius = 8.2f;
            SetLookColors( c, 1.00f, 0.76f, 0.42f, 0.95f, 0.44f, 0.16f, 0.02f, 0.10f, 0.16f, 0.54f, 0.58f, 0.48f, 0.92f, 0.60f, 0.22f );
            SetLookWater( c, 2, 0.03f, 0.18f, 0.30f, 0.92f, 0.44f, 0.62f );
        }
        else if ( strcmp( name, "atmospheric_fog_world" ) == 0 )
        {
            c.skyMode = 8;
            c.terrainMode = 8;
            c.objectStyle = 7;
            c.exposure = 0.62f;
            c.gamma = 2.12f;
            c.sunScreenX = 0.56f;
            c.sunScreenY = 0.86f;
            c.sunIntensity = 10.0f;
            c.styleSaturation = 0.46f;
            c.styleContrast = 0.92f;
            c.styleVignette = 0.58f;
            c.fogDensity = 0.0070f;
            c.fogMaxOpacity = 0.90f;
            c.fogStart = 8.0f;
            c.fogEnd = 620.0f;
            SetLookColors( c, 0.70f, 0.84f, 0.88f, 0.56f, 0.62f, 0.62f, 0.22f, 0.28f, 0.32f, 0.36f, 0.44f, 0.44f, 0.16f, 0.20f, 0.20f );
            SetLookWater( c, 1, 0.05f, 0.12f, 0.14f, 0.78f, 0.20f, 0.05f );
        }
        else if ( strcmp( name, "ocean_world" ) == 0 )
        {
            c.skyMode = 9;
            c.terrainMode = 9;
            c.objectStyle = 0;
            c.exposure = 0.82f;
            c.gamma = 2.00f;
            c.sunScreenX = 0.50f;
            c.sunScreenY = 0.70f;
            c.sunIntensity = 25.0f;
            c.styleSaturation = 1.12f;
            c.styleContrast = 1.04f;
            c.styleVignette = 0.78f;
            c.fogDensity = 0.0010f;
            c.fogMaxOpacity = 0.28f;
            SetLookColors( c, 1.00f, 0.72f, 0.36f, 0.88f, 0.58f, 0.28f, 0.08f, 0.22f, 0.42f, 0.25f, 0.32f, 0.35f, 0.75f, 0.54f, 0.24f );
            SetLookWater( c, 2, 0.02f, 0.20f, 0.32f, 0.96f, 0.48f, 0.72f );
        }
        else if ( strcmp( name, "scifi_test_chamber" ) == 0 )
        {
            c.skyMode = 10;
            c.terrainMode = 10;
            c.objectStyle = 4;
            c.exposure = 0.88f;
            c.gamma = 2.05f;
            c.sunScreenX = 0.50f;
            c.sunScreenY = 0.84f;
            c.sunIntensity = 11.0f;
            c.styleSaturation = 0.72f;
            c.styleContrast = 1.12f;
            c.styleVignette = 0.86f;
            c.cloudsEnabled = false;
            c.fogEnabled = false;
            c.terrainGridScale = 52.0f;
            c.terrainGridStrength = 0.55f;
            SetLookColors( c, 0.86f, 0.98f, 1.00f, 0.16f, 0.18f, 0.20f, 0.02f, 0.025f, 0.03f, 0.80f, 0.82f, 0.84f, 0.72f, 0.88f, 1.00f );
            SetLookWater( c, 1, 0.72f, 0.78f, 0.82f, 0.62f, 0.54f, 0.12f );
        }
        else if ( strcmp( name, "low_poly_art_style" ) == 0 )
        {
            c.skyMode = 11;
            c.terrainMode = 7;
            c.objectStyle = 6;
            c.exposure = 0.84f;
            c.gamma = 2.08f;
            c.sunScreenX = 0.64f;
            c.sunScreenY = 0.76f;
            c.sunIntensity = 7.8f;
            c.skyGlowStrength = 0.74f;
            c.styleSaturation = 1.28f;
            c.styleContrast = 1.04f;
            c.styleVignette = 0.88f;
            c.cloudCoverage = 0.44f;
            c.cloudSoftness = 0.26f;
            c.cloudScale = 3.9f;
            c.cloudIntensity = 0.34f;
            c.sunShaftStrength = 0.22f;
            c.sunShaftFalloff = 2.4f;
            c.volumetricStrength = 0.10f;
            c.volumetricDensity = 0.65f;
            c.bloomThreshold = 1.18f;
            c.bloomStrength = 0.18f;
            c.bloomRadius = 3.0f;
            c.fogColorR = 0.86f;
            c.fogColorG = 0.82f;
            c.fogColorB = 0.66f;
            c.fogStart = 160.0f;
            c.fogEnd = 1180.0f;
            c.fogDensity = 0.00092f;
            c.fogMaxOpacity = 0.27f;
            c.basinCenterX = 620.0f;
            c.basinCenterZ = 650.0f;
            c.basinRadiusX = 250.0f;
            c.basinRadiusZ = 170.0f;
            c.basinFeather = 0.16f;
            SetLookColors( c, 1.00f, 0.92f, 0.65f, 1.00f, 0.95f, 0.80f, 0.55f, 0.75f, 1.00f, 0.46f, 0.62f, 0.28f, 0.30f, 0.38f, 0.16f );
            SetLookWater( c, 4, 0.20f, 0.58f, 0.62f, 0.74f, 0.06f, 0.03f );
        }
        else if ( strcmp( name, "massive_scale" ) == 0 )
        {
            c.skyMode = 12;
            c.terrainMode = 8;
            c.objectStyle = 1;
            c.exposure = 0.66f;
            c.gamma = 2.18f;
            c.sunScreenX = 0.18f;
            c.sunScreenY = 0.72f;
            c.sunIntensity = 15.0f;
            c.styleSaturation = 0.78f;
            c.styleContrast = 1.18f;
            c.styleVignette = 0.58f;
            c.fogDensity = 0.0018f;
            c.fogMaxOpacity = 0.62f;
            c.fogStart = 60.0f;
            c.fogEnd = 2200.0f;
            SetLookColors( c, 0.68f, 0.78f, 1.00f, 0.55f, 0.62f, 0.72f, 0.006f, 0.008f, 0.020f, 0.50f, 0.54f, 0.56f, 0.20f, 0.24f, 0.32f );
            SetLookWater( c, 2, 0.08f, 0.12f, 0.18f, 0.86f, 0.26f, 0.12f );
        }
        else if ( strcmp( name, "storm_front" ) == 0 )
        {
            c.skyMode = 13;
            c.terrainMode = 1;
            c.objectStyle = 2;
            c.exposure = 0.56f;
            c.gamma = 2.22f;
            c.sunScreenX = 0.48f;
            c.sunScreenY = 0.82f;
            c.sunIntensity = 19.0f;
            c.styleSaturation = 0.58f;
            c.styleContrast = 1.36f;
            c.styleVignette = 0.56f;
            c.cloudCoverage = 0.96f;
            c.cloudIntensity = 1.40f;
            c.bloomThreshold = 0.72f;
            c.bloomStrength = 1.10f;
            c.fogDensity = 0.0030f;
            c.fogMaxOpacity = 0.72f;
            SetLookColors( c, 0.90f, 0.96f, 1.65f, 0.22f, 0.25f, 0.28f, 0.03f, 0.04f, 0.05f, 0.30f, 0.32f, 0.32f, 0.68f, 0.82f, 1.00f );
            SetLookWater( c, 1, 0.035f, 0.05f, 0.06f, 0.92f, 0.58f, 0.20f );
        }
        else if ( strcmp( name, "photogrammetry_ground" ) == 0 )
        {
            c.skyMode = 0;
            c.terrainMode = 12;
            c.objectStyle = 0;
            c.exposure = 0.78f;
            c.gamma = 2.10f;
            c.sunScreenX = 0.62f;
            c.sunScreenY = 0.76f;
            c.sunIntensity = 19.0f;
            c.styleSaturation = 0.98f;
            c.styleContrast = 1.10f;
            c.styleVignette = 0.78f;
            c.cloudCoverage = 0.42f;
            c.fogDensity = 0.0008f;
            c.fogMaxOpacity = 0.22f;
            SetLookColors( c, 0.95f, 0.90f, 0.78f, 0.70f, 0.72f, 0.68f, 0.22f, 0.30f, 0.34f, 0.62f, 0.58f, 0.48f, 0.18f, 0.24f, 0.16f );
            SetLookWater( c, 0, 0.10f, 0.12f, 0.10f, 0.0f, 0.0f, 0.0f );
        }
        else if ( strcmp( name, "tron_grid" ) == 0 )
        {
            c.skyMode = 15;
            c.terrainMode = 3;
            c.objectStyle = 7;
            c.exposure = 0.62f;
            c.gamma = 1.90f;
            c.sunScreenX = 0.50f;
            c.sunScreenY = 0.34f;
            c.sunIntensity = 9.0f;
            c.styleSaturation = 1.45f;
            c.styleContrast = 1.48f;
            c.styleVignette = 0.40f;
            c.bloomThreshold = 0.30f;
            c.bloomStrength = 1.70f;
            c.bloomRadius = 6.8f;
            c.fogDensity = 0.0006f;
            c.fogMaxOpacity = 0.28f;
            c.terrainGridScale = 36.0f;
            c.terrainGridStrength = 1.85f;
            SetLookColors( c, 0.00f, 0.80f, 1.80f, 0.00f, 0.02f, 0.05f, 0.00f, 0.00f, 0.01f, 0.00f, 0.02f, 0.05f, 0.00f, 0.75f, 1.00f );
            SetLookWater( c, 0, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f );
        }
        else if ( strcmp( name, "dreamscape" ) == 0 )
        {
            c.skyMode = 16;
            c.terrainMode = 4;
            c.objectStyle = 5;
            c.exposure = 0.92f;
            c.gamma = 1.96f;
            c.sunScreenX = 0.42f;
            c.sunScreenY = 0.80f;
            c.sunIntensity = 22.0f;
            c.styleSaturation = 1.34f;
            c.styleContrast = 1.02f;
            c.styleVignette = 0.72f;
            c.bloomThreshold = 0.70f;
            c.bloomStrength = 0.98f;
            c.cloudCoverage = 0.62f;
            c.cloudIntensity = 0.86f;
            SetLookColors( c, 1.00f, 0.58f, 1.18f, 0.94f, 0.58f, 0.82f, 0.36f, 0.22f, 0.70f, 0.64f, 0.34f, 0.72f, 0.32f, 0.86f, 1.00f );
            SetLookWater( c, 1, 0.22f, 0.14f, 0.32f, 0.78f, 0.26f, 0.30f );
        }
        else if ( strcmp( name, "nordic_winter" ) == 0 )
        {
            c.skyMode = 17;
            c.terrainMode = 11;
            c.objectStyle = 4;
            c.exposure = 0.86f;
            c.gamma = 2.12f;
            c.sunScreenX = 0.60f;
            c.sunScreenY = 0.74f;
            c.sunIntensity = 16.0f;
            c.styleSaturation = 0.78f;
            c.styleContrast = 1.14f;
            c.styleVignette = 0.78f;
            c.cloudCoverage = 0.38f;
            c.fogDensity = 0.0009f;
            c.fogMaxOpacity = 0.30f;
            SetLookColors( c, 0.72f, 0.84f, 1.00f, 0.78f, 0.86f, 0.96f, 0.36f, 0.56f, 0.82f, 0.88f, 0.94f, 1.00f, 0.42f, 0.60f, 0.82f );
            SetLookWater( c, 0, 0.58f, 0.74f, 0.86f, 0.0f, 0.0f, 0.0f );
        }
        else if ( strcmp( name, "abstract_render_showcase" ) == 0 )
        {
            c.skyMode = 18;
            c.terrainMode = 13;
            c.objectStyle = 2;
            c.exposure = 0.84f;
            c.gamma = 2.04f;
            c.sunScreenX = 0.66f;
            c.sunScreenY = 0.80f;
            c.sunIntensity = 17.0f;
            c.styleSaturation = 1.10f;
            c.styleContrast = 1.08f;
            c.styleVignette = 0.84f;
            c.cloudsEnabled = false;
            c.fogEnabled = false;
            SetLookColors( c, 0.92f, 0.92f, 0.88f, 0.60f, 0.60f, 0.62f, 0.25f, 0.25f, 0.27f, 0.76f, 0.76f, 0.76f, 0.18f, 0.16f, 0.15f );
            SetLookWater( c, 1, 0.58f, 0.58f, 0.60f, 0.70f, 0.58f, 0.08f );
        }
        else if ( strcmp( name, "pixar_inspired" ) == 0 )
        {
            c.skyMode = 19;
            c.terrainMode = 14;
            c.objectStyle = 5;
            c.exposure = 0.92f;
            c.gamma = 2.00f;
            c.sunScreenX = 0.62f;
            c.sunScreenY = 0.76f;
            c.sunIntensity = 18.0f;
            c.styleSaturation = 1.22f;
            c.styleContrast = 0.96f;
            c.styleVignette = 0.88f;
            c.cloudCoverage = 0.46f;
            c.cloudIntensity = 0.52f;
            SetLookColors( c, 1.00f, 0.78f, 0.48f, 0.92f, 0.68f, 0.46f, 0.36f, 0.68f, 1.00f, 0.48f, 0.72f, 0.32f, 0.94f, 0.70f, 0.34f );
            SetLookWater( c, 1, 0.18f, 0.46f, 0.56f, 0.78f, 0.18f, 0.18f );
        }
        else
        {
            Fail( "Unknown look '%s' at line %d", name, m_lineNumber );
        }

        m_scene.m_sceneOptions.cinematicRender = c;
        m_scene.m_sceneOptions.cinematicOverrideMask |= ConceptLookOverrideMask();
        m_scene.m_sceneOptions.hasCinematicRenderingOverride = true;
        m_scene.m_sceneOptions.cinematicRendering = true;
    }

    void ParseObjectMaterial( const char* args )
    {
        const char* expected = "object_material <name|all|balls|boxes|prefix:...> <r> <g> <b> <mode>";
        const char* cursor = RequireArgs( "object_material", args, expected );

        SceneObjectMaterialOverride material;
        memset( &material, 0, sizeof( material ) );
        material.tintR = 1.0f;
        material.tintG = 1.0f;
        material.tintB = 1.0f;
        material.materialMode = 1.0f;

        char mode[64] = {};
        ParseNextToken( "object_material", cursor, material.target, sizeof( material.target ), expected );
        material.tintR = ParseNextFloatToken( "object_material", cursor, expected );
        material.tintG = ParseNextFloatToken( "object_material", cursor, expected );
        material.tintB = ParseNextFloatToken( "object_material", cursor, expected );
        ParseNextToken( "object_material", cursor, mode, sizeof( mode ), expected );
        material.materialMode = ParseMaterialModeValue( "object_material", mode );
        m_scene.m_objectMaterials.push_back( material );
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
        const char* lookArgs = nullptr;
        if ( MatchDirective( line, "cinematic_look", lookArgs ) )
        {
            ParseLook( lookArgs );
            return true;
        }

        if ( strncmp( line, "cinematic_", 10 ) != 0 )
        {
            return false;
        }

        const char* multiArgs = nullptr;
        if ( MatchDirective( line, "cinematic_style_modes", multiArgs ) )
        {
            const char* expected = "cinematic_style_modes <sky> <terrain> <object> <water>";
            const char* cursor = RequireArgs( "cinematic_style_modes", multiArgs, expected );
            m_scene.m_sceneOptions.cinematicRender.skyMode = ParseNextIntToken( "cinematic_style_modes", cursor, expected );
            m_scene.m_sceneOptions.cinematicRender.terrainMode = ParseNextIntToken( "cinematic_style_modes", cursor, expected );
            m_scene.m_sceneOptions.cinematicRender.objectStyle = ParseNextIntToken( "cinematic_style_modes", cursor, expected );
            m_scene.m_sceneOptions.cinematicRender.waterMode = ParseNextIntToken( "cinematic_style_modes", cursor, expected );
            m_scene.m_sceneOptions.cinematicOverrideMask |= SCENE_CINE_STYLE_MODES;
            return true;
        }
        if ( MatchDirective( line, "cinematic_style_grade", multiArgs ) )
        {
            const char* expected = "cinematic_style_grade <saturation> <contrast> <vignette>";
            const char* cursor = RequireArgs( "cinematic_style_grade", multiArgs, expected );
            m_scene.m_sceneOptions.cinematicRender.styleSaturation = ParseNextFloatToken( "cinematic_style_grade", cursor, expected );
            m_scene.m_sceneOptions.cinematicRender.styleContrast = ParseNextFloatToken( "cinematic_style_grade", cursor, expected );
            m_scene.m_sceneOptions.cinematicRender.styleVignette = ParseNextFloatToken( "cinematic_style_grade", cursor, expected );
            m_scene.m_sceneOptions.cinematicOverrideMask |= SCENE_CINE_STYLE_GRADE;
            return true;
        }
        if ( MatchDirective( line, "cinematic_terrain_tint", multiArgs ) )
        {
            const char* expected = "cinematic_terrain_tint <r> <g> <b>";
            const char* cursor = RequireArgs( "cinematic_terrain_tint", multiArgs, expected );
            m_scene.m_sceneOptions.cinematicRender.terrainTintR = ParseNextFloatToken( "cinematic_terrain_tint", cursor, expected );
            m_scene.m_sceneOptions.cinematicRender.terrainTintG = ParseNextFloatToken( "cinematic_terrain_tint", cursor, expected );
            m_scene.m_sceneOptions.cinematicRender.terrainTintB = ParseNextFloatToken( "cinematic_terrain_tint", cursor, expected );
            m_scene.m_sceneOptions.cinematicOverrideMask |= SCENE_CINE_TERRAIN_TINT;
            return true;
        }
        if ( MatchDirective( line, "cinematic_terrain_accent", multiArgs ) )
        {
            const char* expected = "cinematic_terrain_accent <r> <g> <b>";
            const char* cursor = RequireArgs( "cinematic_terrain_accent", multiArgs, expected );
            m_scene.m_sceneOptions.cinematicRender.terrainAccentR = ParseNextFloatToken( "cinematic_terrain_accent", cursor, expected );
            m_scene.m_sceneOptions.cinematicRender.terrainAccentG = ParseNextFloatToken( "cinematic_terrain_accent", cursor, expected );
            m_scene.m_sceneOptions.cinematicRender.terrainAccentB = ParseNextFloatToken( "cinematic_terrain_accent", cursor, expected );
            m_scene.m_sceneOptions.cinematicOverrideMask |= SCENE_CINE_TERRAIN_ACCENT;
            return true;
        }
        if ( MatchDirective( line, "cinematic_terrain_grid", multiArgs ) )
        {
            const char* expected = "cinematic_terrain_grid <scale> <strength>";
            const char* cursor = RequireArgs( "cinematic_terrain_grid", multiArgs, expected );
            m_scene.m_sceneOptions.cinematicRender.terrainGridScale = ParseNextFloatToken( "cinematic_terrain_grid", cursor, expected );
            m_scene.m_sceneOptions.cinematicRender.terrainGridStrength = ParseNextFloatToken( "cinematic_terrain_grid", cursor, expected );
            m_scene.m_sceneOptions.cinematicOverrideMask |= SCENE_CINE_TERRAIN_GRID;
            return true;
        }
        if ( MatchDirective( line, "cinematic_water_tint", multiArgs ) )
        {
            const char* expected = "cinematic_water_tint <r> <g> <b>";
            const char* cursor = RequireArgs( "cinematic_water_tint", multiArgs, expected );
            m_scene.m_sceneOptions.cinematicRender.waterTintR = ParseNextFloatToken( "cinematic_water_tint", cursor, expected );
            m_scene.m_sceneOptions.cinematicRender.waterTintG = ParseNextFloatToken( "cinematic_water_tint", cursor, expected );
            m_scene.m_sceneOptions.cinematicRender.waterTintB = ParseNextFloatToken( "cinematic_water_tint", cursor, expected );
            m_scene.m_sceneOptions.cinematicOverrideMask |= SCENE_CINE_WATER_TINT;
            return true;
        }
        if ( MatchDirective( line, "cinematic_water_profile", multiArgs ) )
        {
            const char* expected = "cinematic_water_profile <alpha> <reflection> <glint>";
            const char* cursor = RequireArgs( "cinematic_water_profile", multiArgs, expected );
            m_scene.m_sceneOptions.cinematicRender.waterAlpha = ParseNextFloatToken( "cinematic_water_profile", cursor, expected );
            m_scene.m_sceneOptions.cinematicRender.waterReflectionStrength = ParseNextFloatToken( "cinematic_water_profile", cursor, expected );
            m_scene.m_sceneOptions.cinematicRender.waterGlintStrength = ParseNextFloatToken( "cinematic_water_profile", cursor, expected );
            m_scene.m_sceneOptions.cinematicOverrideMask |= SCENE_CINE_WATER_PROFILE;
            return true;
        }
        if ( MatchDirective( line, "cinematic_basin_mask", multiArgs ) )
        {
            const char* expected = "cinematic_basin_mask <centerX> <centerZ> <radiusX> <radiusZ> <feather>";
            const char* cursor = RequireArgs( "cinematic_basin_mask", multiArgs, expected );
            m_scene.m_sceneOptions.cinematicRender.basinCenterX = ParseNextFloatToken( "cinematic_basin_mask", cursor, expected );
            m_scene.m_sceneOptions.cinematicRender.basinCenterZ = ParseNextFloatToken( "cinematic_basin_mask", cursor, expected );
            m_scene.m_sceneOptions.cinematicRender.basinRadiusX = ParseNextFloatToken( "cinematic_basin_mask", cursor, expected );
            m_scene.m_sceneOptions.cinematicRender.basinRadiusZ = ParseNextFloatToken( "cinematic_basin_mask", cursor, expected );
            m_scene.m_sceneOptions.cinematicRender.basinFeather = ParseNextFloatToken( "cinematic_basin_mask", cursor, expected );
            m_scene.m_sceneOptions.cinematicOverrideMask |= SCENE_CINE_BASIN_MASK;
            return true;
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
            { "floating_ball", &TestSceneParser::ParseFloatingBall, "floating_ball <name> ..." },
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
            { "style", &TestSceneParser::ParseStyle, "style <name|path>" },
            { "look", &TestSceneParser::ParseLook, "look <concept_name>" },
            { "object_material", &TestSceneParser::ParseObjectMaterial, "object_material <target> <r> <g> <b> <mode>" },
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
