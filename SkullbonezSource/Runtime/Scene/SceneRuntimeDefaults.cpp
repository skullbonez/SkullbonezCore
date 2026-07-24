/*
File: SkullbonezSource/Runtime/Scene/SceneRuntimeDefaults.cpp
Purpose:
  Persists scene UI render defaults to engine.cfg outside the Run composition root.

Summary:
  The Render and Sky tabs mutate live config structs. Saving defaults is a
  narrow file-rewrite operation over those structs, so callers pass the payloads
  directly instead of bouncing through private Run methods.

Glossary:
  engine.cfg: User-facing engine configuration file.
  Render defaults: Ordinary and cinematic render settings persisted for future
    launches.
  Config rewrite: Read/modify/write operation that preserves unrelated lines
    where possible.

Invariants:
  - engine.cfg key spellings and numeric formatting are user-facing compatibility
    surface.

Related:
  - SkullbonezSource/Runtime/Scene/SceneRuntimeDefaults.h
  - SkullbonezSource/Runtime/Scene/SceneController.Load.cpp
  - Agentic/Reports/2026-07-11/runtime-shell-final-ownership-review.md
*/
#include "SceneRuntimeDefaults.h"
#include "../../Core/WindowConstants.h"
#include "../../Core/Common.h"

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

namespace SkullbonezCore
{
namespace Runtime
{
namespace
{

bool ConfigLineMatchesKey( const std::string& line, const char* key )
{
    std::size_t start = line.find_first_not_of( " \t" );
    if ( start == std::string::npos || line[start] == '#' )
    {
        return false;
    }

    const std::size_t keyLen = std::strlen( key );
    if ( line.compare( start, keyLen, key ) != 0 )
    {
        return false;
    }

    std::size_t pos = start + keyLen;
    while ( pos < line.size() && ( line[pos] == ' ' || line[pos] == '\t' ) )
    {
        ++pos;
    }
    return pos < line.size() && line[pos] == '=';
}

bool ReplaceConfigLine( std::vector<std::string>& lines, const char* key, const std::string& value )
{
    const std::string lineText = std::string( key ) + " = " + value;
    bool replaced = false;
    // Invariant: Keep the first occurrence in place and delete duplicate keys so
    // engine.cfg remains deterministic after repeated Save Defaults actions.
    for ( std::size_t i = 0; i < lines.size(); )
    {
        if ( ConfigLineMatchesKey( lines[i], key ) )
        {
            if ( !replaced )
            {
                lines[i] = lineText;
                replaced = true;
                ++i;
            }
            else
            {
                lines.erase( lines.begin() + static_cast<std::ptrdiff_t>( i ) );
            }
            continue;
        }
        ++i;
    }
    return replaced;
}

void EraseConfigLines( std::vector<std::string>& lines, const char* key )
{
    lines.erase( std::remove_if( lines.begin(),
                                 lines.end(),
                                 [key]( const std::string& line ) { return ConfigLineMatchesKey( line, key ); } ),
                 lines.end() );
}

void EraseConfigLinesWithPrefix( std::vector<std::string>& lines, const char* prefix )
{
    const std::size_t prefixLength = std::strlen( prefix );
    lines.erase( std::remove_if( lines.begin(),
                                 lines.end(),
                                 [prefix, prefixLength]( const std::string& line )
                                 {
                                     const std::size_t start = line.find_first_not_of( " \t" );
                                     return start != std::string::npos && line[start] != '#' &&
                                            line.compare( start, prefixLength, prefix ) == 0;
                                 } ),
                 lines.end() );
}

std::size_t OrdinaryConfigInsertIndex( const std::vector<std::string>& lines )
{
    for ( std::size_t i = 0; i < lines.size(); ++i )
    {
        if ( lines[i].find( "Ordinary rendering" ) != std::string::npos )
        {
            std::size_t sectionBody = i + 1;
            while ( sectionBody < lines.size() &&
                    ( lines[sectionBody].empty() ||
                      lines[sectionBody].find(
                          "# ---------------------------------------------------------------------------" ) !=
                          std::string::npos ) )
            {
                ++sectionBody;
            }

            for ( std::size_t j = sectionBody; j < lines.size(); ++j )
            {
                if ( lines[j].find( "# ---------------------------------------------------------------------------" ) !=
                     std::string::npos )
                {
                    return j;
                }
            }
            return lines.size();
        }
    }

    for ( std::size_t i = 0; i < lines.size(); ++i )
    {
        if ( lines[i].find( "Cinematic rendering" ) != std::string::npos )
        {
            return i > 1 ? i - 1 : i;
        }
    }
    return lines.size();
}

std::size_t CinematicConfigInsertIndex( const std::vector<std::string>& lines )
{
    for ( std::size_t i = 0; i < lines.size(); ++i )
    {
        if ( lines[i].find( "Cinematic rendering" ) != std::string::npos )
        {
            std::size_t sectionBody = i + 1;
            while ( sectionBody < lines.size() &&
                    ( lines[sectionBody].empty() ||
                      lines[sectionBody].find(
                          "# ---------------------------------------------------------------------------" ) !=
                          std::string::npos ) )
            {
                ++sectionBody;
            }

            for ( std::size_t j = sectionBody; j < lines.size(); ++j )
            {
                if ( lines[j].find( "# ---------------------------------------------------------------------------" ) !=
                     std::string::npos )
                {
                    return j;
                }
            }
            return lines.size();
        }
    }

    for ( std::size_t i = 0; i < lines.size(); ++i )
    {
        if ( lines[i].find( "Physics" ) != std::string::npos )
        {
            return i > 1 ? i - 1 : i;
        }
    }
    return lines.size();
}

void AppendMissingOrdinaryConfigLines( std::vector<std::string>& lines, std::vector<std::string>& missing )
{
    if ( missing.empty() )
    {
        return;
    }

    std::vector<std::string> insertLines;
    // Why: Missing keys should land near their owning section when possible,
    // preserving user comments and unrelated config ordering.
    const bool hasOrdinarySection =
        std::any_of( lines.begin(),
                     lines.end(),
                     []( const std::string& line ) { return line.find( "Ordinary rendering" ) != std::string::npos; } );
    if ( !hasOrdinarySection )
    {
        insertLines.push_back( "# ---------------------------------------------------------------------------" );
        insertLines.push_back( "# Ordinary rendering" );
        insertLines.push_back( "# ---------------------------------------------------------------------------" );
    }
    insertLines.insert( insertLines.end(), missing.begin(), missing.end() );
    insertLines.push_back( "" );

    const std::size_t insertIndex = OrdinaryConfigInsertIndex( lines );
    lines.insert( lines.begin() + static_cast<std::ptrdiff_t>( insertIndex ), insertLines.begin(), insertLines.end() );
}

void AppendMissingCinematicConfigLines( std::vector<std::string>& lines, std::vector<std::string>& missing )
{
    if ( missing.empty() )
    {
        return;
    }

    std::vector<std::string> insertLines;
    const bool hasCinematicSection = std::any_of( lines.begin(),
                                                  lines.end(),
                                                  []( const std::string& line )
                                                  { return line.find( "Cinematic rendering" ) != std::string::npos; } );
    if ( !hasCinematicSection )
    {
        insertLines.push_back( "# ---------------------------------------------------------------------------" );
        insertLines.push_back( "# Cinematic rendering" );
        insertLines.push_back( "# ---------------------------------------------------------------------------" );
    }
    insertLines.insert( insertLines.end(), missing.begin(), missing.end() );
    insertLines.push_back( "" );

    const std::size_t insertIndex = CinematicConfigInsertIndex( lines );
    lines.insert( lines.begin() + static_cast<std::ptrdiff_t>( insertIndex ), insertLines.begin(), insertLines.end() );
}

bool LoadConfigLines( const std::string& configPath, std::vector<std::string>& lines )
{
    std::ifstream input( configPath );
    if ( !input )
    {
        return false;
    }

    std::string line;
    while ( std::getline( input, line ) )
    {
        if ( !line.empty() && line.back() == '\r' )
        {
            line.pop_back();
        }
        lines.push_back( line );
    }
    return true;
}

bool WriteConfigLines( const std::string& configPath, const std::vector<std::string>& lines )
{
    std::ofstream output( configPath, std::ios::trunc );
    if ( !output )
    {
        return false;
    }
    for ( const std::string& outLine : lines )
    {
        output << outLine << '\n';
    }
    return output.good();
}

void StampCurrentConfigVersion( std::vector<std::string>& lines )
{
    // Invariant: native Save Defaults is a config writer and must apply owned
    // deletion migrations, not merely relabel legacy text as current. Unknown
    // rows remain untouched; only explicitly retired settings are removed.
    EraseConfigLines( lines, "physics_simd_kernels" );
    EraseConfigLinesWithPrefix( lines, "contact_audio_" );
    EraseConfigLines( lines, "terrain_render_step_size" );

    char version[16] = {};
    sprintf_s( version, "%u", SkullbonezCore::Core::ENGINE_CONFIG_FORMAT_VERSION );
    if ( ReplaceConfigLine( lines, "format_version", version ) )
    {
        return;
    }

    // Writer invariant: Save Defaults preserves the introductory comments but
    // upgrades an unversioned v0 file before persisting any edited values.
    const auto firstBlank = std::find( lines.begin(), lines.end(), std::string{} );
    lines.insert( firstBlank, std::string( "format_version = " ) + version );
}

} // namespace

SkullbonezCore::Core::SbResult SaveRenderDefaults( const SkullbonezCore::Core::OrdinaryRenderConfig& ordinary )
{
    // Concept: Saving ordinary defaults is a text rewrite, not a full config
    // serialization. Unknown keys and comments must survive the round trip.
    const std::string configPath = std::string( DATA_ROOT ) + "engine.cfg";
    SkullbonezCore::Core::EngineConfig versionProbe;
    const SkullbonezCore::Core::SbResult versionResult = versionProbe.Load( configPath.c_str() );
    if ( !versionResult.ok )
    {
        // Hazard: writers must never turn a future document into an older
        // version merely because this build does not understand its fields.
        return versionResult;
    }
    std::vector<std::string> lines;
    if ( !LoadConfigLines( configPath, lines ) )
    {
        // Lane R: the user-facing config may be missing, locked, or unreadable.
        return SkullbonezCore::Core::SbResult::Failure( "Runtime/RenderDefaultsStore",
                                                        "Could not read render defaults file: %s",
                                                        configPath.c_str() );
    }

    std::vector<std::string> missing;
    char buf[128] = {};

    const auto setText = [&]( const char* key, const char* value )
    {
        const std::string lineText = std::string( key ) + " = " + value;
        if ( !ReplaceConfigLine( lines, key, value ) )
        {
            missing.push_back( lineText );
        }
    };
    const auto setBool = [&]( const char* key, bool value )
    {
        std::snprintf( buf, sizeof( buf ), "%d", value ? 1 : 0 );
        setText( key, buf );
    };
    const auto setInt = [&]( const char* key, int value )
    {
        std::snprintf( buf, sizeof( buf ), "%d", value );
        setText( key, buf );
    };
    const auto setFloat = [&]( const char* key, float value, const char* format )
    {
        std::snprintf( buf, sizeof( buf ), format, value );
        setText( key, buf );
    };

    setFloat( "ordinary_sun_intensity", ordinary.sunIntensity, "%.2f" );
    setFloat( "ordinary_sun_color_r", ordinary.sunColorR, "%.2f" );
    setFloat( "ordinary_sun_color_g", ordinary.sunColorG, "%.2f" );
    setFloat( "ordinary_sun_color_b", ordinary.sunColorB, "%.2f" );
    setFloat( "ordinary_ambient_strength", ordinary.ambientStrength, "%.2f" );
    setFloat( "ordinary_sky_ambient_r", ordinary.skyAmbientR, "%.2f" );
    setFloat( "ordinary_sky_ambient_g", ordinary.skyAmbientG, "%.2f" );
    setFloat( "ordinary_sky_ambient_b", ordinary.skyAmbientB, "%.2f" );
    setFloat( "ordinary_ground_ambient_r", ordinary.groundAmbientR, "%.2f" );
    setFloat( "ordinary_ground_ambient_g", ordinary.groundAmbientG, "%.2f" );
    setFloat( "ordinary_ground_ambient_b", ordinary.groundAmbientB, "%.2f" );
    setBool( "ordinary_shadows", ordinary.shadow.enabled );
    setBool( "ordinary_shadow_terrain_casts", ordinary.shadow.terrainCasts );
    setBool( "ordinary_shadow_objects_cast", ordinary.shadow.objectsCast );
    setBool( "ordinary_shadow_terrain_receives", ordinary.shadow.terrainReceives );
    setBool( "ordinary_shadow_objects_receive", ordinary.shadow.objectsReceive );
    setInt( "ordinary_shadow_map_size", ordinary.shadow.mapSize );
    setInt( "ordinary_shadow_pcf_radius", ordinary.shadow.pcfRadius );
    setFloat( "ordinary_shadow_strength", ordinary.shadow.strength, "%.2f" );
    setFloat( "ordinary_shadow_softness", ordinary.shadow.softness, "%.2f" );
    setFloat( "ordinary_shadow_depth_bias", ordinary.shadow.depthBias, "%.5f" );
    setFloat( "ordinary_shadow_slope_bias", ordinary.shadow.slopeBias, "%.5f" );
    setFloat( "ordinary_shadow_max_distance", ordinary.shadow.maxDistance, "%.1f" );
    setFloat( "ordinary_water_tint_r", ordinary.waterTintR, "%.3f" );
    setFloat( "ordinary_water_tint_g", ordinary.waterTintG, "%.3f" );
    setFloat( "ordinary_water_tint_b", ordinary.waterTintB, "%.3f" );
    setFloat( "ordinary_water_alpha", ordinary.waterAlpha, "%.2f" );
    setFloat( "ordinary_water_reflection_strength", ordinary.waterReflectionStrength, "%.2f" );
    setFloat( "ordinary_water_fresnel_f0", ordinary.waterFresnelF0, "%.3f" );
    setFloat( "ordinary_ball_roughness_scale", ordinary.ballRoughnessScale, "%.2f" );
    setFloat( "ordinary_ball_specular_scale", ordinary.ballSpecularScale, "%.2f" );
    setFloat( "ordinary_box_roughness_scale", ordinary.boxRoughnessScale, "%.2f" );
    setFloat( "ordinary_box_specular_scale", ordinary.boxSpecularScale, "%.2f" );
    setFloat( "replay_trajectory_future_width", ordinary.replayTrajectory.futureWidth, "%.2f" );
    setFloat( "replay_trajectory_future_alpha", ordinary.replayTrajectory.futureAlpha, "%.2f" );
    setFloat( "replay_trajectory_future_edge_feather", ordinary.replayTrajectory.futureEdgeFeather, "%.2f" );
    setFloat( "replay_trajectory_causal_width", ordinary.replayTrajectory.causalWidth, "%.2f" );
    setFloat( "replay_trajectory_causal_alpha", ordinary.replayTrajectory.causalAlpha, "%.2f" );
    setFloat( "replay_trajectory_causal_edge_feather", ordinary.replayTrajectory.causalEdgeFeather, "%.2f" );
    setFloat( "replay_trajectory_baseline_width", ordinary.replayTrajectory.baselineWidth, "%.2f" );
    setFloat( "replay_trajectory_baseline_alpha", ordinary.replayTrajectory.baselineAlpha, "%.2f" );
    setFloat( "replay_trajectory_baseline_edge_feather", ordinary.replayTrajectory.baselineEdgeFeather, "%.2f" );
    setFloat( "replay_trajectory_marker_width", ordinary.replayTrajectory.markerWidth, "%.2f" );
    setFloat( "replay_trajectory_marker_alpha", ordinary.replayTrajectory.markerAlpha, "%.2f" );
    setFloat( "replay_trajectory_marker_edge_feather", ordinary.replayTrajectory.markerEdgeFeather, "%.2f" );
    setFloat( "replay_trajectory_selected_emphasis", ordinary.replayTrajectory.selectedEmphasis, "%.2f" );

    AppendMissingOrdinaryConfigLines( lines, missing );
    StampCurrentConfigVersion( lines );
    if ( !WriteConfigLines( configPath, lines ) )
    {
        return SkullbonezCore::Core::SbResult::Failure( "Runtime/RenderDefaultsStore",
                                                        "Could not write render defaults file: %s",
                                                        configPath.c_str() );
    }
    return SkullbonezCore::Core::SbResult::Success();
}

SkullbonezCore::Core::SbResult SaveSkyDefaults( const SkullbonezCore::Core::CinematicRenderConfig& cinematic )
{
    const std::string configPath = std::string( DATA_ROOT ) + "engine.cfg";
    SkullbonezCore::Core::EngineConfig versionProbe;
    const SkullbonezCore::Core::SbResult versionResult = versionProbe.Load( configPath.c_str() );
    if ( !versionResult.ok )
    {
        return versionResult;
    }
    std::vector<std::string> lines;
    if ( !LoadConfigLines( configPath, lines ) )
    {
        return SkullbonezCore::Core::SbResult::Failure( "Runtime/RenderDefaultsStore",
                                                        "Could not read cinematic defaults file: %s",
                                                        configPath.c_str() );
    }

    std::vector<std::string> missing;
    char buf[128] = {};

    const auto setText = [&]( const char* key, const char* value )
    {
        const std::string lineText = std::string( key ) + " = " + value;
        if ( !ReplaceConfigLine( lines, key, value ) )
        {
            missing.push_back( lineText );
        }
    };
    const auto setBool = [&]( const char* key, bool value )
    {
        std::snprintf( buf, sizeof( buf ), "%d", value ? 1 : 0 );
        setText( key, buf );
    };
    const auto setInt = [&]( const char* key, int value )
    {
        std::snprintf( buf, sizeof( buf ), "%d", value );
        setText( key, buf );
    };
    const auto setFloat = [&]( const char* key, float value, const char* format )
    {
        std::snprintf( buf, sizeof( buf ), format, value );
        setText( key, buf );
    };

    setBool( "cinematic_sky_atmosphere", cinematic.skyAtmosphereEnabled );
    setBool( "cinematic_clouds", cinematic.cloudsEnabled );
    setBool( "cinematic_god_rays", cinematic.godRaysEnabled );
    setBool( "cinematic_volumetric_lighting", cinematic.volumetricLightingEnabled );
    setFloat( "cinematic_exposure", cinematic.exposure, "%.2f" );
    setFloat( "cinematic_gamma", cinematic.gamma, "%.2f" );
    // Compatibility: Save Defaults continues writing the established config
    // keys so older tools and user files round-trip without migration.
    setFloat( "cinematic_sun_screen_x", cinematic.sunAzimuth, "%.3f" );
    setFloat( "cinematic_sun_screen_y", cinematic.sunElevation, "%.3f" );
    setFloat( "cinematic_sun_color_r", cinematic.sunColorR, "%.2f" );
    setFloat( "cinematic_sun_color_g", cinematic.sunColorG, "%.2f" );
    setFloat( "cinematic_sun_color_b", cinematic.sunColorB, "%.2f" );
    setFloat( "cinematic_sun_intensity", cinematic.sunIntensity, "%.2f" );
    setFloat( "cinematic_sky_horizon_r", cinematic.skyHorizonR, "%.2f" );
    setFloat( "cinematic_sky_horizon_g", cinematic.skyHorizonG, "%.2f" );
    setFloat( "cinematic_sky_horizon_b", cinematic.skyHorizonB, "%.2f" );
    setFloat( "cinematic_sky_zenith_r", cinematic.skyZenithR, "%.2f" );
    setFloat( "cinematic_sky_zenith_g", cinematic.skyZenithG, "%.2f" );
    setFloat( "cinematic_sky_zenith_b", cinematic.skyZenithB, "%.2f" );
    setFloat( "cinematic_sky_glow_strength", cinematic.skyGlowStrength, "%.2f" );
    setFloat( "cinematic_cloud_coverage", cinematic.cloudCoverage, "%.2f" );
    setFloat( "cinematic_cloud_softness", cinematic.cloudSoftness, "%.2f" );
    setFloat( "cinematic_cloud_scale", cinematic.cloudScale, "%.2f" );
    setFloat( "cinematic_cloud_intensity", cinematic.cloudIntensity, "%.2f" );
    setFloat( "cinematic_sun_shaft_strength", cinematic.sunShaftStrength, "%.2f" );
    setFloat( "cinematic_sun_shaft_falloff", cinematic.sunShaftFalloff, "%.2f" );
    setFloat( "cinematic_volumetric_strength", cinematic.volumetricStrength, "%.2f" );
    setFloat( "cinematic_volumetric_density", cinematic.volumetricDensity, "%.2f" );
    setFloat( "cinematic_volumetric_decay", cinematic.volumetricDecay, "%.3f" );
    setInt( "cinematic_sky_mode", cinematic.skyMode );
    setFloat( "cinematic_style_saturation", cinematic.styleSaturation, "%.2f" );
    setFloat( "cinematic_style_contrast", cinematic.styleContrast, "%.2f" );
    setFloat( "cinematic_style_vignette", cinematic.styleVignette, "%.2f" );

    AppendMissingCinematicConfigLines( lines, missing );
    StampCurrentConfigVersion( lines );
    if ( !WriteConfigLines( configPath, lines ) )
    {
        return SkullbonezCore::Core::SbResult::Failure( "Runtime/RenderDefaultsStore",
                                                        "Could not write cinematic defaults file: %s",
                                                        configPath.c_str() );
    }
    return SkullbonezCore::Core::SbResult::Success();
}

} // namespace Runtime
} // namespace SkullbonezCore
