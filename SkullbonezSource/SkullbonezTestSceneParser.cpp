/*
File: SkullbonezSource/SkullbonezTestSceneParser.cpp
Purpose:
  Parses plain-text .scene files into TestScene directives.

Mental model:
  Runtime code connects authored scene data, input, simulation, render
  backends, and validation-oriented launch modes. Follow who owns state and
  when that state changes.

Glossary:
  Validation gate: Repository script that proves a class of changes before
  commit or PR.

Invariants:
  - Command-line and scene-file spellings are user-facing compatibility
  surface.

Related:
  - Agentic/Reference/runtime-reference.md
  - Agentic/Reference/comment-style-guide.md
*/
#include "SkullbonezTestScene.h"
#include <algorithm>
#include <cerrno>
#include <climits>
#include <cstdarg>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <thread>


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
        { "editor", 2 },
        { "placement", 2 },
        { "physics", 3 },
        { "options", 4 },
        { "params", 4 },
        { "render", 5 },
        { "renderer", 5 },
        { "keys", 6 },
        { "controls", 6 },
        { "cinematic", 7 },
        { "cine", 7 },
        { "look", 7 },
    };
    return TryParseIntOption( value, kTabs, outTab );
}

int MaxConfigurableWorkerThreadCount()
{
    const unsigned int hardwareThreads = std::thread::hardware_concurrency();
    return (std::max)( 1, static_cast<int>( hardwareThreads ) );
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

    // Concept: scene parsing is table-driven command dispatch.
    //
    // Each plain-text directive name maps to one parser member function and an
    // "expected" string used in error messages. That keeps scene-file syntax
    // visible in one table instead of scattering strcmp chains throughout the
    // parser. These names are user-facing compatibility surface; changing a
    // spelling can break checked-in scenes and validation suites.
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

    // Style files may include other style files. Keep a depth counter so a bad
    // include loop reports a clean parser error instead of recursing forever.
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
        // Fail closes the file before throwing so Windows does not keep a stale
        // handle open after parser errors. The line number is intentionally part
        // of most messages because scene files are hand-authored text.
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

    static bool TryParseUnsignedInt( const char* value, unsigned int& out )
    {
        if ( !value || value[0] == '\0' || value[0] == '-' )
        {
            return false;
        }

        errno = 0;
        char* end = nullptr;
        const unsigned long long parsed = strtoull( value, &end, 10 );
        if ( end == value || *end != '\0' || errno == ERANGE || parsed > UINT_MAX )
        {
            return false;
        }

        out = static_cast<unsigned int>( parsed );
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

    unsigned int ParseUnsignedIntValue( const char* directive, const char* value )
    {
        unsigned int parsed = 0;
        if ( !TryParseUnsignedInt( value, parsed ) )
        {
            Fail( "Invalid %s at line %d: %s", directive, m_lineNumber, value ? value : "" );
        }
        return parsed;
    }

    unsigned int ParseUnsignedIntArg( const char* directive, const char* args, const char* expected )
    {
        return ParseUnsignedIntValue( directive, RequireArgs( directive, args, expected ) );
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
            { "texture", static_cast<int>( Rendering::RenderMaterialKindLegacyMode( Rendering::RenderMaterialKind::Textured ) ) },
            { "beachball", static_cast<int>( Rendering::RenderMaterialKindLegacyMode( Rendering::RenderMaterialKind::Textured ) ) },
            { "matte", static_cast<int>( Rendering::RenderMaterialKind::Matte ) },
            { "solid", static_cast<int>( Rendering::RenderMaterialKind::Matte ) },
            { "metal", static_cast<int>( Rendering::RenderMaterialKind::Metal ) },
            { "chrome", static_cast<int>( Rendering::RenderMaterialKind::Metal ) },
            { "emissive", static_cast<int>( Rendering::RenderMaterialKind::Emissive ) },
            { "neon", static_cast<int>( Rendering::RenderMaterialKind::Emissive ) },
            { "glass", static_cast<int>( Rendering::RenderMaterialKind::Glass ) },
            { "toon", static_cast<int>( Rendering::RenderMaterialKind::Toon ) },
            { "pixar", static_cast<int>( Rendering::RenderMaterialKind::Toon ) },
            { "lowpoly", static_cast<int>( Rendering::RenderMaterialKind::LowPoly ) },
            { "shadow", static_cast<int>( Rendering::RenderMaterialKind::Shadow ) },
            { "black", static_cast<int>( Rendering::RenderMaterialKind::Shadow ) },
            { "foliage", static_cast<int>( Rendering::RenderMaterialKind::Foliage ) },
            { "leaf", static_cast<int>( Rendering::RenderMaterialKind::Foliage ) },
            { "leaves", static_cast<int>( Rendering::RenderMaterialKind::Foliage ) },
            { "bark", static_cast<int>( Rendering::RenderMaterialKind::Bark ) },
            { "trunk", static_cast<int>( Rendering::RenderMaterialKind::Bark ) },
            { "stone", static_cast<int>( Rendering::RenderMaterialKind::Stone ) },
            { "rock", static_cast<int>( Rendering::RenderMaterialKind::Stone ) },
            { "ridge", static_cast<int>( Rendering::RenderMaterialKind::Ridge ) },
            { "distant", static_cast<int>( Rendering::RenderMaterialKind::Ridge ) },
            { "shore", static_cast<int>( Rendering::RenderMaterialKind::Shore ) },
            { "sand", static_cast<int>( Rendering::RenderMaterialKind::Shore ) },
            { "pine", static_cast<int>( Rendering::RenderMaterialKind::Pine ) },
            { "conifer", static_cast<int>( Rendering::RenderMaterialKind::Pine ) },
        };
        int mode = 0;
        if ( TryParseIntOption( value, kMaterialModes, mode ) )
        {
            return static_cast<float>( mode );
        }

        Fail( "Invalid %s material mode at line %d: %s", directive, m_lineNumber, value ? value : "" );
    }

    bool SplitKeyValueToken( const char* token, char* key, size_t keySize, const char*& value ) const
    {
        const char* equals = strchr( token, '=' );
        if ( !equals || equals == token || equals[1] == '\0' || keySize == 0 )
        {
            return false;
        }

        const size_t keyLen = static_cast<size_t>( equals - token );
        if ( keyLen >= keySize )
        {
            return false;
        }

        memcpy( key, token, keyLen );
        key[keyLen] = '\0';
        value = equals + 1;
        return true;
    }

    float ParseMaterialOptionFloat( const char* key, const char* value )
    {
        float parsed = 0.0f;
        if ( !TryParseFloat( value, parsed ) )
        {
            Fail( "Invalid object_material %s value at line %d: %s", key, m_lineNumber, value ? value : "" );
        }
        return parsed;
    }

    float ParseMaterialOptionUnitFloat( const char* key, const char* value )
    {
        return std::clamp( ParseMaterialOptionFloat( key, value ), 0.0f, 1.0f );
    }

    bool ReadCommaToken( const char*& cursor, char* out, size_t outSize, bool& outHadDelimiter ) const
    {
        outHadDelimiter = false;
        if ( !cursor || !out || outSize == 0 || *cursor == '\0' )
        {
            return false;
        }

        const char* tokenBegin = cursor;
        const char* comma = strchr( cursor, ',' );
        const char* tokenEnd = comma ? comma : cursor + strlen( cursor );
        const size_t tokenLength = static_cast<size_t>( tokenEnd - tokenBegin );
        if ( tokenLength == 0 || tokenLength >= outSize )
        {
            return false;
        }

        memcpy( out, tokenBegin, tokenLength );
        out[tokenLength] = '\0';
        outHadDelimiter = comma != nullptr;
        cursor = comma ? comma + 1 : tokenEnd;
        return true;
    }

    void ParseMaterialOptionVec3( const char* key, const char* value, float& outR, float& outG, float& outB )
    {
        char partR[64] = {};
        char partG[64] = {};
        char partB[64] = {};
        const char* cursor = value;
        bool hasDelimiterAfterR = false;
        bool hasDelimiterAfterG = false;
        bool hasDelimiterAfterB = false;
        if ( !ReadCommaToken( cursor, partR, sizeof( partR ), hasDelimiterAfterR ) ||
             !hasDelimiterAfterR ||
             !ReadCommaToken( cursor, partG, sizeof( partG ), hasDelimiterAfterG ) ||
             !hasDelimiterAfterG ||
             !ReadCommaToken( cursor, partB, sizeof( partB ), hasDelimiterAfterB ) ||
             hasDelimiterAfterB ||
             *cursor != '\0' )
        {
            Fail( "Invalid object_material %s value at line %d (expected r,g,b): %s", key, m_lineNumber, value ? value : "" );
        }

        outR = ParseMaterialOptionFloat( key, partR );
        outG = ParseMaterialOptionFloat( key, partG );
        outB = ParseMaterialOptionFloat( key, partB );
    }

    void SetObjectMaterialBaseColor( SceneObjectMaterialOverride& material, float r, float g, float b )
    {
        const bool mirrorEmissiveToBase =
            material.material.kind == Rendering::RenderMaterialKind::Emissive &&
            material.material.emissiveColor[0] == material.material.baseColor[0] &&
            material.material.emissiveColor[1] == material.material.baseColor[1] &&
            material.material.emissiveColor[2] == material.material.baseColor[2];

        material.tintR = r;
        material.tintG = g;
        material.tintB = b;
        material.material.baseColor[0] = r;
        material.material.baseColor[1] = g;
        material.material.baseColor[2] = b;
        material.material.baseColor[3] = 1.0f;

        if ( mirrorEmissiveToBase )
        {
            material.material.emissiveColor[0] = r;
            material.material.emissiveColor[1] = g;
            material.material.emissiveColor[2] = b;
        }
    }

    void ApplyObjectMaterialOption( SceneObjectMaterialOverride& material, const char* token )
    {
        char key[64] = {};
        const char* value = nullptr;
        if ( !SplitKeyValueToken( token, key, sizeof( key ), value ) )
        {
            Fail( "Invalid object_material option at line %d (expected key=value): %s", m_lineNumber, token ? token : "" );
        }

        if ( strcmp( key, "tint" ) == 0 || strcmp( key, "base" ) == 0 || strcmp( key, "base_color" ) == 0 )
        {
            float r = 1.0f;
            float g = 1.0f;
            float b = 1.0f;
            ParseMaterialOptionVec3( key, value, r, g, b );
            SetObjectMaterialBaseColor( material, r, g, b );
        }
        else if ( strcmp( key, "roughness" ) == 0 )
        {
            material.material.roughness = ParseMaterialOptionUnitFloat( key, value );
        }
        else if ( strcmp( key, "metallic" ) == 0 || strcmp( key, "metalness" ) == 0 )
        {
            material.material.metallic = ParseMaterialOptionUnitFloat( key, value );
        }
        else if ( strcmp( key, "specular" ) == 0 )
        {
            material.material.specular = ParseMaterialOptionUnitFloat( key, value );
        }
        else if ( strcmp( key, "transmission" ) == 0 )
        {
            material.material.transmission = ParseMaterialOptionUnitFloat( key, value );
        }
        else if ( strcmp( key, "stylization" ) == 0 || strcmp( key, "style" ) == 0 )
        {
            material.material.stylization = ParseMaterialOptionUnitFloat( key, value );
        }
        else if ( strcmp( key, "emissive" ) == 0 || strcmp( key, "emissive_color" ) == 0 || strcmp( key, "emit_color" ) == 0 )
        {
            ParseMaterialOptionVec3( key,
                                     value,
                                     material.material.emissiveColor[0],
                                     material.material.emissiveColor[1],
                                     material.material.emissiveColor[2] );
        }
        else if ( strcmp( key, "strength" ) == 0 || strcmp( key, "emissive_strength" ) == 0 || strcmp( key, "emit" ) == 0 )
        {
            material.material.emissiveStrength = (std::max)( 0.0f, ParseMaterialOptionFloat( key, value ) );
        }
        else if ( strcmp( key, "flags" ) == 0 )
        {
            material.material.flags = static_cast<uint32_t>( ParseIntValue( key, value ) );
        }
        else if ( strcmp( key, "name" ) == 0 )
        {
            strncpy_s( material.material.name, sizeof( material.material.name ), value, _TRUNCATE );
        }
        else
        {
            Fail( "Unknown object_material option at line %d: %s", m_lineNumber, key );
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
            { "tab", &TestSceneParser::ParseUITabDirective, "ui tab <profiler|scene|physics|options|render|controls|cine>" },
            { "rect", &TestSceneParser::ParseUIRect, "ui rect <x> <y> <w> <h>" },
            { "blur", &TestSceneParser::ParseUIBlur, "ui blur on|off" },
            { "renderer_combo", &TestSceneParser::ParseUIRendererCombo, "ui renderer_combo open|closed" },
            { "water_combo", &TestSceneParser::ParseUIWaterCombo, "ui water_combo open|closed" },
            { "scene_combo", &TestSceneParser::ParseUISceneCombo, "ui scene_combo open|closed" },
            { "scene_filter", &TestSceneParser::ParseUISceneFilter, "ui scene_filter <text>" },
            { "profiler_expand", &TestSceneParser::ParseUIProfilerExpand, "ui profiler_expand on|off" },
            { "timeline", &TestSceneParser::ParseUITimeline, "ui timeline on|off" },
            { "histogram", &TestSceneParser::ParseUIHistogram, "ui histogram on|off" },
            { "hitboxes", &TestSceneParser::ParseUIHitboxes, "ui hitboxes on|off" },
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

    void ParseUIHitboxes( const char* args )
    {
        bool parsedValue = false;
        ParseAliasOnOff( "UI hitboxes", args, parsedValue );
        m_scene.m_UIOptions.hasHitboxOverlay = true;
        m_scene.m_UIOptions.hitboxOverlay = parsedValue;
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
        m_scene.m_sceneOptions.seed = ParseUnsignedIntArg( "seed", args, "seed <N>" );
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

    void IncludeStyleFile( const char* token )
    {
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

            if ( !DispatchStyleLine( line ) )
            {
                Fail( "Unknown directive in style file %s at line %d: %.64s", stylePath, styleLineNumber, line );
            }
        }

        --m_styleIncludeDepth;
        m_lineNumber = parentLineNumber;
    }

    void ParseStyleReference( const char* directive, const char* args, const char* expected )
    {
        const char* cursor = RequireArgs( directive, args, expected );
        char token[260] = {};
        ParseNextToken( directive, cursor, token, sizeof( token ), expected );
        IncludeStyleFile( token );
    }

    void ParseStyle( const char* args )
    {
        ParseStyleReference( "style", args, "style <name|path>" );
    }

    void ParseLook( const char* args )
    {
        ParseStyleReference( "look", args, "look <name|path>" );
    }

    void ParseCinematicLook( const char* args )
    {
        ParseStyleReference( "cinematic_look", args, "cinematic_look <name|path>" );
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

    void ParseConvexHullCommon( const char* args, bool isFixed )
    {
        const char* directive = isFixed ? "floating_convex_hull" : "convex_hull";
        const char* expected = isFixed ? "floating_convex_hull <name> <pos> <mass> <restitution> <hull|hull=path> [euler] [velocity]" : "convex_hull <name> <pos> <mass> <restitution> <hull|hull=path> [euler] [velocity]";
        char tokens[13][64] = {};
        const int parsed = ParseTokenList( directive, args, expected, tokens, 13 );
        if ( parsed != 7 && parsed != 10 && parsed != 13 )
        {
            Fail( "Invalid convex_hull/floating_convex_hull at line %d (expected 7, 10, or 13 fields, got %d)", m_lineNumber, parsed );
        }

        SceneConvexHull hull;
        memset( &hull, 0, sizeof( hull ) );
        hull.hasInitOrient = false;
        hull.hasInitVelocity = false;
        hull.isFixed = isFixed;

        strcpy_s( hull.name, sizeof( hull.name ), tokens[0] );
        hull.posX = ParseFloatValue( directive, tokens[1] );
        hull.posY = ParseFloatValue( directive, tokens[2] );
        hull.posZ = ParseFloatValue( directive, tokens[3] );
        hull.mass = ParseFloatValue( directive, tokens[4] );
        hull.restitution = ParseFloatValue( directive, tokens[5] );

        const char* hullPath = tokens[6];
        if ( strncmp( hullPath, "hull=", 5 ) == 0 )
        {
            hullPath += 5;
        }
        if ( hullPath[0] == '\0' )
        {
            Fail( "Invalid %s hull path at line %d", directive, m_lineNumber );
        }
        strcpy_s( hull.hullPath, sizeof( hull.hullPath ), hullPath );

        if ( parsed == 10 || parsed == 13 )
        {
            hull.eulerX = ParseFloatValue( directive, tokens[7] );
            hull.eulerY = ParseFloatValue( directive, tokens[8] );
            hull.eulerZ = ParseFloatValue( directive, tokens[9] );
            hull.hasInitOrient = true;
        }
        if ( parsed == 13 )
        {
            hull.velX = ParseFloatValue( directive, tokens[10] );
            hull.velY = ParseFloatValue( directive, tokens[11] );
            hull.velZ = ParseFloatValue( directive, tokens[12] );
            hull.hasInitVelocity = true;
        }

        m_scene.m_convexHulls.push_back( hull );
    }

    void ParseConvexHull( const char* args )
    {
        ParseConvexHullCommon( args, false );
    }

    void ParseFloatingConvexHull( const char* args )
    {
        ParseConvexHullCommon( args, true );
    }

    void ParseRequiredContact( const char* args )
    {
        char tokens[2][64] = {};
        const int parsed = ParseTokenList( "required_contact", args, "required_contact <nameA> <nameB>", tokens, 2 );
        if ( parsed != 2 )
        {
            Fail( "Invalid required_contact at line %d (expected 2 fields, got %d)", m_lineNumber, parsed );
        }

        SceneRequiredContact contact;
        strcpy_s( contact.nameA, sizeof( contact.nameA ), tokens[0] );
        strcpy_s( contact.nameB, sizeof( contact.nameB ), tokens[1] );
        m_scene.m_requiredContacts.push_back( contact );
    }

    void ParseRequiredBroadphaseXCells( const char* args )
    {
        const char* expected = "required_broadphase_x_cells <minCellX> <maxCellX> <cellY> <cellZ>";
        const char* cursor = RequireArgs( "required_broadphase_x_cells", args, expected );

        SceneRequiredBroadphaseXCells cells;
        cells.minCellX = ParseNextIntToken( "required_broadphase_x_cells", cursor, expected );
        cells.maxCellX = ParseNextIntToken( "required_broadphase_x_cells", cursor, expected );
        cells.cellY = ParseNextIntToken( "required_broadphase_x_cells", cursor, expected );
        cells.cellZ = ParseNextIntToken( "required_broadphase_x_cells", cursor, expected );
        if ( cells.maxCellX < cells.minCellX )
        {
            Fail( "Invalid required_broadphase_x_cells at line %d (maxCellX must be >= minCellX)", m_lineNumber );
        }

        m_scene.m_requiredBroadphaseXCells.push_back( cells );
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
        const char* value = RequireArgs( "physics_debug", args, "physics_debug none|axes|contacts|sleep|pipeline|terrain|all" );
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
        else if ( strcmp( value, "terrain" ) == 0 ||
                  strcmp( value, "terrain_contact" ) == 0 ||
                  strcmp( value, "terrain-probe" ) == 0 ||
                  strcmp( value, "terrain_probe" ) == 0 )
        {
            m_scene.m_sceneOptions.physicsDebugFlags = Physics::PHYSICS_DEBUG_TERRAIN_CONTACT;
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

    void ParsePhysicsDebugTerrainContact( const char* args )
    {
        ParsePhysicsDebugComponent( args, Physics::PHYSICS_DEBUG_TERRAIN_CONTACT );
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
        const char* expected = "ball_state <name> <position> <velocity> <angular_velocity> <orientation> <radius> <mass> <restitution> <inertia> [fixed]";
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
        char fixedToken[64] = {};
        if ( ReadToken( cursor, fixedToken, sizeof( fixedToken ) ) )
        {
            bs.isFixed = ParseIntValue( "ball_state", fixedToken ) != 0;
        }

        m_scene.m_ballStates.push_back( bs );
    }

    void ParseBoxState( const char* args )
    {
        const char* expected = "box_state <name> <position> <velocity> <angular_velocity> <orientation> <halfExtents> <mass> <restitution> <inertia> <fixed>";
        const char* cursor = RequireArgs( "box_state", args, expected );
        SceneBoxState bs;
        memset( &bs, 0, sizeof( bs ) );

        ParseNextToken( "box_state", cursor, bs.name, sizeof( bs.name ), expected );
        bs.posX = ParseNextFloatToken( "box_state", cursor, expected );
        bs.posY = ParseNextFloatToken( "box_state", cursor, expected );
        bs.posZ = ParseNextFloatToken( "box_state", cursor, expected );
        bs.velX = ParseNextFloatToken( "box_state", cursor, expected );
        bs.velY = ParseNextFloatToken( "box_state", cursor, expected );
        bs.velZ = ParseNextFloatToken( "box_state", cursor, expected );
        bs.angVelX = ParseNextFloatToken( "box_state", cursor, expected );
        bs.angVelY = ParseNextFloatToken( "box_state", cursor, expected );
        bs.angVelZ = ParseNextFloatToken( "box_state", cursor, expected );
        bs.orientX = ParseNextFloatToken( "box_state", cursor, expected );
        bs.orientY = ParseNextFloatToken( "box_state", cursor, expected );
        bs.orientZ = ParseNextFloatToken( "box_state", cursor, expected );
        bs.orientW = ParseNextFloatToken( "box_state", cursor, expected );
        bs.halfX = ParseNextFloatToken( "box_state", cursor, expected );
        bs.halfY = ParseNextFloatToken( "box_state", cursor, expected );
        bs.halfZ = ParseNextFloatToken( "box_state", cursor, expected );
        bs.mass = ParseNextFloatToken( "box_state", cursor, expected );
        bs.restitution = ParseNextFloatToken( "box_state", cursor, expected );
        bs.inertiaX = ParseNextFloatToken( "box_state", cursor, expected );
        bs.inertiaY = ParseNextFloatToken( "box_state", cursor, expected );
        bs.inertiaZ = ParseNextFloatToken( "box_state", cursor, expected );
        bs.isFixed = ParseNextIntToken( "box_state", cursor, expected ) != 0;

        m_scene.m_boxStates.push_back( bs );
    }

    void ParseConvexHullState( const char* args )
    {
        const char* expected = "convex_hull_state <name> <hull|hull=path> <position> <velocity> <angular_velocity> <orientation> <mass> <restitution> <inertia> <fixed>";
        const char* cursor = RequireArgs( "convex_hull_state", args, expected );
        SceneConvexHullState hs;
        memset( &hs, 0, sizeof( hs ) );

        ParseNextToken( "convex_hull_state", cursor, hs.name, sizeof( hs.name ), expected );
        ParseNextToken( "convex_hull_state", cursor, hs.hullPath, sizeof( hs.hullPath ), expected );
        if ( strncmp( hs.hullPath, "hull=", 5 ) == 0 )
        {
            memmove( hs.hullPath, hs.hullPath + 5, strlen( hs.hullPath + 5 ) + 1 );
        }
        hs.posX = ParseNextFloatToken( "convex_hull_state", cursor, expected );
        hs.posY = ParseNextFloatToken( "convex_hull_state", cursor, expected );
        hs.posZ = ParseNextFloatToken( "convex_hull_state", cursor, expected );
        hs.velX = ParseNextFloatToken( "convex_hull_state", cursor, expected );
        hs.velY = ParseNextFloatToken( "convex_hull_state", cursor, expected );
        hs.velZ = ParseNextFloatToken( "convex_hull_state", cursor, expected );
        hs.angVelX = ParseNextFloatToken( "convex_hull_state", cursor, expected );
        hs.angVelY = ParseNextFloatToken( "convex_hull_state", cursor, expected );
        hs.angVelZ = ParseNextFloatToken( "convex_hull_state", cursor, expected );
        hs.orientX = ParseNextFloatToken( "convex_hull_state", cursor, expected );
        hs.orientY = ParseNextFloatToken( "convex_hull_state", cursor, expected );
        hs.orientZ = ParseNextFloatToken( "convex_hull_state", cursor, expected );
        hs.orientW = ParseNextFloatToken( "convex_hull_state", cursor, expected );
        hs.mass = ParseNextFloatToken( "convex_hull_state", cursor, expected );
        hs.restitution = ParseNextFloatToken( "convex_hull_state", cursor, expected );
        hs.inertiaX = ParseNextFloatToken( "convex_hull_state", cursor, expected );
        hs.inertiaY = ParseNextFloatToken( "convex_hull_state", cursor, expected );
        hs.inertiaZ = ParseNextFloatToken( "convex_hull_state", cursor, expected );
        hs.isFixed = ParseNextIntToken( "convex_hull_state", cursor, expected ) != 0;

        m_scene.m_convexHullStates.push_back( hs );
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

    void ParseEditableScene( const char* args )
    {
        ParseStrictOnOff( "editable_scene", RequireArgs( "editable_scene", args, "editable_scene on|off" ), m_scene.m_sceneOptions.editableScene );
    }

    void ParseObjectMaterial( const char* args )
    {
        const char* expected = "object_material <target> <r> <g> <b> <mode> [key=value...] or object_material <target> <mode> tint=<r,g,b> [key=value...]";
        const char* cursor = RequireArgs( "object_material", args, expected );

        SceneObjectMaterialOverride material;
        memset( &material, 0, sizeof( material ) );
        material.tintR = 1.0f;
        material.tintG = 1.0f;
        material.tintB = 1.0f;
        material.materialMode = 1.0f;

        ParseNextToken( "object_material", cursor, material.target, sizeof( material.target ), expected );
        char firstValue[64] = {};
        ParseNextToken( "object_material", cursor, firstValue, sizeof( firstValue ), expected );

        float firstTint = 0.0f;
        if ( TryParseFloat( firstValue, firstTint ) )
        {
            material.tintR = firstTint;
            material.tintG = ParseNextFloatToken( "object_material", cursor, expected );
            material.tintB = ParseNextFloatToken( "object_material", cursor, expected );

            char mode[64] = {};
            ParseNextToken( "object_material", cursor, mode, sizeof( mode ), expected );
            material.materialMode = ParseMaterialModeValue( "object_material", mode );
        }
        else
        {
            material.materialMode = ParseMaterialModeValue( "object_material", firstValue );
        }

        material.material = Rendering::MakeRenderMaterialFromLegacyTint( material.tintR, material.tintG, material.tintB, material.materialMode );
        strcpy_s( material.material.name, sizeof( material.material.name ), Rendering::RenderMaterialKindName( material.material.kind ) );

        char option[128] = {};
        while ( ReadToken( cursor, option, sizeof( option ) ) )
        {
            ApplyObjectMaterialOption( material, option );
        }

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

    void ParseModelCapacity( const char* args )
    {
        m_scene.m_sceneOptions.modelCapacity = ParseIntArg( "model_capacity", args, "model_capacity <count>" );
        if ( m_scene.m_sceneOptions.modelCapacity < 1 || m_scene.m_sceneOptions.modelCapacity > MAX_GAME_MODELS )
        {
            Fail( "Invalid model_capacity at line %d (expected 1..%d)", m_lineNumber, MAX_GAME_MODELS );
        }
    }

    void ParseWorkerThreads( const char* args )
    {
        m_scene.m_sceneOptions.workerThreads = ParseIntArg( "worker_threads", args, "worker_threads <-1|0|count>" );
        const int maxWorkerThreads = MaxConfigurableWorkerThreadCount();
        if ( m_scene.m_sceneOptions.workerThreads < -1 || m_scene.m_sceneOptions.workerThreads > maxWorkerThreads )
        {
            Fail( "Invalid worker_threads at line %d (expected -1, 0, or 1..%d)", m_lineNumber, maxWorkerThreads );
        }
    }

    bool TryParseCinematicDirective( const char* line )
    {
        // Cinematic scene directives normally share the cinematic_ prefix.
        // "shadows" is a user-facing scene alias that maps to the same config.
        const char* lookArgs = nullptr;
        if ( MatchDirective( line, "cinematic_look", lookArgs ) )
        {
            ParseCinematicLook( lookArgs );
            return true;
        }

        const char* shadowAliasArgs = nullptr;
        if ( strncmp( line, "cinematic_", 10 ) != 0 &&
             !MatchDirective( line, "shadows", shadowAliasArgs ) )
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
        if ( strcmp( key, "cinematic_legacy_shadow_discs" ) == 0 )
        {
            bool ignoredValue = false;
            if ( !ParseOnOff( value, ignoredValue ) )
            {
                Fail( "Invalid %s value at line %d: %s", key, m_lineNumber, value );
            }
            return true;
        }

        struct BoolDirective
        {
            // Boolean directives toggle whole passes/features such as bloom,
            // fog, clouds, or the master cinematic renderer. Shadows deliberately
            // do not force the cinematic renderer: the shadow-map pass is a depth
            // resource that can feed normal backbuffer rendering as well as the
            // cinematic HDR/post path.
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
            { "cinematic_shadows", &CinematicRenderConfig::shadowsEnabled, SCENE_CINE_SHADOWS },
            { "shadows", &CinematicRenderConfig::shadowsEnabled, SCENE_CINE_SHADOWS },
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

        struct IntDirective
        {
            const char* name;
            int CinematicRenderConfig::* field;
            uint64_t bit;
            int minValue;
            int maxValue;
        };
        static constexpr IntDirective kIntDirectives[] = {
            { "cinematic_shadow_map_size", &CinematicRenderConfig::shadowMapSize, SCENE_CINE_SHADOW_MAP_SIZE, 256, 8192 },
            { "cinematic_shadow_pcf_radius", &CinematicRenderConfig::shadowPcfRadius, SCENE_CINE_SHADOW_PCF_RADIUS, 0, 3 },
        };
        for ( const IntDirective& directive : kIntDirectives )
        {
            if ( strcmp( key, directive.name ) == 0 )
            {
                const int parsedValue = ParseIntValue( key, value );
                if ( parsedValue < directive.minValue || parsedValue > directive.maxValue )
                {
                    Fail( "Invalid %s at line %d (expected %d..%d)", key, m_lineNumber, directive.minValue, directive.maxValue );
                }
                m_scene.m_sceneOptions.cinematicRender.*( directive.field ) = parsedValue;
                m_scene.m_sceneOptions.cinematicOverrideMask |= directive.bit;
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
            { "cinematic_shadow_strength", &CinematicRenderConfig::shadowStrength, SCENE_CINE_SHADOW_STRENGTH, 0.0f, 1.0f },
            { "cinematic_shadow_softness", &CinematicRenderConfig::shadowSoftness, SCENE_CINE_SHADOW_SOFTNESS, 0.25f, 4.0f },
            { "cinematic_shadow_depth_bias", &CinematicRenderConfig::shadowDepthBias, SCENE_CINE_SHADOW_DEPTH_BIAS, 0.0f, 0.05f },
            { "cinematic_shadow_slope_bias", &CinematicRenderConfig::shadowSlopeBias, SCENE_CINE_SHADOW_SLOPE_BIAS, 0.0f, 0.05f },
            { "cinematic_shadow_max_distance", &CinematicRenderConfig::shadowMaxDistance, SCENE_CINE_SHADOW_MAX_DISTANCE, 128.0f, 10000.0f },
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

    bool DispatchStyleLine( const char* line )
    {
        if ( TryParseCinematicDirective( line ) )
        {
            return true;
        }

        static const SceneDirective directives[] = {
            { "style", &TestSceneParser::ParseStyle, "style <name|path>" },
            { "object_material", &TestSceneParser::ParseObjectMaterial, "object_material <target> <r> <g> <b> <mode> [key=value...]" },
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
            { "convex_hull", &TestSceneParser::ParseConvexHull, "convex_hull <name> ..." },
            { "floating_convex_hull", &TestSceneParser::ParseFloatingConvexHull, "floating_convex_hull <name> ..." },
            { "required_contact", &TestSceneParser::ParseRequiredContact, "required_contact <nameA> <nameB>" },
            { "required_broadphase_x_cells", &TestSceneParser::ParseRequiredBroadphaseXCells, "required_broadphase_x_cells <minCellX> <maxCellX> <cellY> <cellZ>" },
            { "time_scale", &TestSceneParser::ParseTimeScale, "time_scale <value>" },
            { "fixed_step", &TestSceneParser::ParseFixedStep, "fixed_step" },
            { "physics_debug", &TestSceneParser::ParsePhysicsDebug, "physics_debug none|axes|contacts|sleep|pipeline|terrain|all" },
            { "physics_debug_axes", &TestSceneParser::ParsePhysicsDebugAxes, "physics_debug_axes on|off" },
            { "physics_debug_contacts", &TestSceneParser::ParsePhysicsDebugContacts, "physics_debug_contacts on|off" },
            { "physics_debug_sleep", &TestSceneParser::ParsePhysicsDebugSleep, "physics_debug_sleep on|off" },
            { "physics_debug_pipeline", &TestSceneParser::ParsePhysicsDebugPipeline, "physics_debug_pipeline on|off" },
            { "physics_debug_terrain_contact", &TestSceneParser::ParsePhysicsDebugTerrainContact, "physics_debug_terrain_contact on|off" },
            { "physics_debug_transparent", &TestSceneParser::ParsePhysicsDebugTransparent, "physics_debug_transparent on|off" },
            { "physics_debug_alpha", &TestSceneParser::ParsePhysicsDebugAlpha, "physics_debug_alpha <0.05..1.0>" },
            { "physics_debug_contact_linger", &TestSceneParser::ParsePhysicsDebugContactLinger, "physics_debug_contact_linger <0.0..5.0>" },
            { "track_height", &TestSceneParser::ParseTrackHeight, "track_height <height>" },
            { "auto_cycle_interval", &TestSceneParser::ParseAutoCycleInterval, "auto_cycle_interval <seconds>" },
            { "flat_slope", &TestSceneParser::ParseFlatSlope, "flat_slope <baseY> <slopeX> <slopeZ>" },
            { "ball_state", &TestSceneParser::ParseBallState, "ball_state <name> ..." },
            { "box_state", &TestSceneParser::ParseBoxState, "box_state <name> ..." },
            { "convex_hull_state", &TestSceneParser::ParseConvexHullState, "convex_hull_state <name> ..." },
            { "world", &TestSceneParser::ParseWorld, "world <gravity> <fluidHeight> <fluidDensity>" },
            { "water_hidden", &TestSceneParser::ParseWaterHidden, "water_hidden on|off" },
            { "terrain_hidden", &TestSceneParser::ParseTerrainHidden, "terrain_hidden on|off" },
            { "editable_scene", &TestSceneParser::ParseEditableScene, "editable_scene on|off" },
            { "style", &TestSceneParser::ParseStyle, "style <name|path>" },
            { "look", &TestSceneParser::ParseLook, "look <name|path>" },
            { "object_material", &TestSceneParser::ParseObjectMaterial, "object_material <target> <r> <g> <b> <mode> [key=value...]" },
            { "model_capacity", &TestSceneParser::ParseModelCapacity, "model_capacity <count>" },
            { "worker_threads", &TestSceneParser::ParseWorkerThreads, "worker_threads <-1|0|count>" },
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

    TestScene LoadStyle()
    {
        IncludeStyleFile( m_path );
        return m_scene;
    }
};


TestScene LoadTestSceneFromFileImpl( const char* path )
{
    return TestSceneParser( path ).Load();
}


TestScene LoadStyleSceneFromFileImpl( const char* path )
{
    return TestSceneParser( path ).LoadStyle();
}
} // namespace Basics
} // namespace SkullbonezCore
