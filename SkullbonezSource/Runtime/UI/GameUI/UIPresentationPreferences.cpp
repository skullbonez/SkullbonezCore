// Layout persistence is cold presentation work. Runtime state and drawer
// visibility never enter the file; automation uses an explicit isolated path.
#include "UI.h"
#include "../../../Core/AtomicTextFileWriter.h"
#include "../../../Core/PlatformWin32.h"

#include <cstdio>
#include <cstring>

using namespace SkullbonezCore::UI;

namespace
{
bool ResolvePreferencesPath( char* path, std::size_t capacity )
{
    const std::size_t explicitLength = SkullbonezCore::Core::Platform::ReadEnvironmentVariable( "SKULLBONEZ_UI_LAYOUT_FILE",
                                                                                                path, capacity );
    if ( explicitLength != 0 )
    {
        return explicitLength < capacity;
    }
#if defined( SKULLBONEZ_AUTOMATION_DIAGNOSTICS )
    // Repeatable probes must never consume or overwrite a person's layout.
    return false;
#else
    char local[768] {};
    const std::size_t length = SkullbonezCore::Core::Platform::ReadEnvironmentVariable( "LOCALAPPDATA", local,
                                                                                        sizeof( local ) );
    if ( length == 0 || length >= sizeof( local ) )
    {
        return false;
    }
    const int written = std::snprintf( path, capacity, "%s/SkullbonezCore/ui-layout.preferences", local );
    return written > 0 && static_cast<std::size_t>( written ) < capacity;
#endif
}

GameLayout::PresentationPreferences ReadPreferences( const char* path )
{
    GameLayout::PresentationPreferences preferences;
    FILE* file = nullptr;
    if ( fopen_s( &file, path, "rb" ) != 0 || !file )
    {
        return preferences;
    }
    char bytes[512] {};
    const std::size_t count = std::fread( bytes, 1, sizeof( bytes ) - 1, file );
    const bool complete = !std::ferror( file ) && std::fgetc( file ) == EOF;
    std::fclose( file );
    unsigned version = 0;
    int layout = 0, leftFolded = 0, rightFolded = 0, consumed = 0;
    const int fields = sscanf_s( bytes,
                                 "version %u layout %d left %f right %f drawer %f diagnostics %f folded %u tool %d "
                                 "leftFolded %d rightFolded %d %n",
                                 &version, &layout, &preferences.leftWidth, &preferences.rightWidth,
                                 &preferences.drawerHeight, &preferences.diagnosticsHeight, &preferences.foldedSections,
                                 &preferences.lastTool, &leftFolded, &rightFolded, &consumed );
    // Version 1 predates themes. Preserve its layout and migrate to Blue.
    if ( complete && fields == 10 && version >= 2 && version <= GameLayout::PresentationPreferences::VERSION )
    {
        int theme = 0, themeBytes = 0;
        if ( sscanf_s( bytes + consumed, "theme %d %n", &theme, &themeBytes ) != 1 )
        {
            return {};
        }
        preferences.theme = theme >= 0 && theme < static_cast<int>( Style::Theme::Count )
                                ? static_cast<Style::Theme>( theme )
                                : Style::Theme::Blue;
        consumed += themeBytes;
    }
    if ( version >= 3 )
    {
        int folded = 1, readBytes = 0;
        if ( sscanf_s( bytes + consumed, "replayFolded %d %n", &folded, &readBytes ) != 1 )
        {
            return {};
        }
        preferences.replayFolded = folded == 1;
        consumed += readBytes;
    }
    if ( !complete || fields != 10 || consumed != static_cast<int>( count ) ||
         ( version < 1 || version > GameLayout::PresentationPreferences::VERSION ) )
    {
        return {};
    }
    // Earlier versions used different summary defaults. Start all sections folded.
    if ( version < 5 )
    {
        preferences.foldedSections = 7;
    }
    preferences.layout = layout == 1 ? GameLayout::LayoutMode::Editor : GameLayout::LayoutMode::Canvas;
    preferences.leftFolded = leftFolded == 1;
    preferences.rightFolded = rightFolded == 1;
    return GameLayout::SanitizePreferences( preferences );
}
} // namespace

int InGameUI::EvidenceSummarySectionsPreference() const noexcept
{
    return static_cast<int>( 7u & ~m_windowInteraction.m_presentation.preferences.foldedSections );
}

void InGameUI::RememberEvidenceSummarySections( int sections ) noexcept
{
    m_windowInteraction.m_presentation.preferences.foldedSections = 7u & ~static_cast<uint32_t>( sections );
}

void InGameUI::LoadPresentationPreferences()
{
    if ( !ResolvePreferencesPath( m_layoutPreferencesPath, sizeof( m_layoutPreferencesPath ) ) )
    {
        m_layoutPreferencesPath[0] = '\0';
        return;
    }
    m_windowInteraction.m_presentation.preferences = ReadPreferences( m_layoutPreferencesPath );
    Style::SelectTheme( m_windowInteraction.m_presentation.preferences.theme );
    SetActiveTab( static_cast<InGameUITab>( m_windowInteraction.m_presentation.preferences.lastTool ) );
    SetMinimized( true );
}

void InGameUI::SavePresentationPreferences( SkullbonezCore::Core::SbDiagnosticStore& diagnostics ) const
{
    if ( m_layoutPreferencesPath[0] == '\0' )
    {
        return;
    }
    const auto preferences = GameLayout::SanitizePreferences( m_windowInteraction.m_presentation.preferences );
    char bytes[512] {};
    const int count = std::snprintf( bytes, sizeof( bytes ),
                                     "version %u\nlayout %d\nleft %.9g\nright %.9g\ndrawer %.9g\ndiagnostics %.9g\nfolded "
                                     "%u\ntool %d\nleftFolded %d\nrightFolded %d\ntheme %d\nreplayFolded %d\n",
                                     GameLayout::PresentationPreferences::VERSION, static_cast<int>( preferences.layout ),
                                     preferences.leftWidth, preferences.rightWidth, preferences.drawerHeight,
                                     preferences.diagnosticsHeight, preferences.foldedSections, preferences.lastTool,
                                     preferences.leftFolded ? 1 : 0, preferences.rightFolded ? 1 : 0,
                                     static_cast<int>( Style::CurrentTheme() ), preferences.replayFolded ? 1 : 0 );
    if ( count <= 0 || count >= static_cast<int>( sizeof( bytes ) ) )
    {
        return;
    }
    const auto result = SkullbonezCore::Core::WriteFileAtomic( diagnostics, "UI layout", m_layoutPreferencesPath,
                                                               { bytes, static_cast<std::size_t>( count ) } );
    if ( !result.Ok() )
    {
        std::fprintf( stderr, "[UI layout] Save failed: %s\n", result.ErrorMessage() );
    }
}
