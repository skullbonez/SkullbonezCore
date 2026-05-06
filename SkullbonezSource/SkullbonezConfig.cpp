// --- Includes ---
#include "SkullbonezCommon.h"
#include "SkullbonezConfig.h"
#include <string.h>


// --- Usings ---
using namespace SkullbonezCore::Basics;


/* ---------------------------------------------------------------------------------*/
SkullbonezConfig& SkullbonezConfig::Instance()
{
    static SkullbonezConfig s_instance;
    return s_instance;
}


/* ---------------------------------------------------------------------------------*/
void SkullbonezConfig::Load( const char* path )
{
    FILE* f = nullptr;
    if ( fopen_s( &f, path, "r" ) != 0 || !f )
    {
        return; // config file is optional
    }

    char line[512];
    while ( fgets( line, sizeof( line ), f ) )
    {
        size_t len = strlen( line );
        while ( len > 0 && ( line[len - 1] == '\r' || line[len - 1] == '\n' ) )
        {
            line[--len] = '\0';
        }

        if ( len == 0 || line[0] == '#' )
        {
            continue;
        }

        char* eq = strchr( line, '=' );
        if ( !eq )
        {
            continue;
        }

        // Trim key
        *eq = '\0';
        char* k = line;
        while ( *k == ' ' || *k == '\t' )
        {
            ++k;
        }
        char* ke = eq - 1;
        while ( ke >= k && ( *ke == ' ' || *ke == '\t' ) )
        {
            --ke;
        }
        *( ke + 1 ) = '\0';

        // Trim value
        char* v = eq + 1;
        while ( *v == ' ' || *v == '\t' )
        {
            ++v;
        }
        size_t vlen = strlen( v );
        while ( vlen > 0 && ( v[vlen - 1] == ' ' || v[vlen - 1] == '\t' ) )
        {
            v[--vlen] = '\0';
        }

        // Strip inline comments from value
        char* hash = strchr( v, '#' );
        if ( hash )
        {
            *hash = '\0';
            vlen = strlen( v );
            while ( vlen > 0 && ( v[vlen - 1] == ' ' || v[vlen - 1] == '\t' ) )
            {
                v[--vlen] = '\0';
            }
        }

        if ( *k == '\0' || *v == '\0' )
        {
            continue;
        }

        // --- match keys ---

        // Window
        if ( strcmp( k, "screen_x" ) == 0 )
        {
            screenX = atoi( v );
        }
        else if ( strcmp( k, "screen_y" ) == 0 )
        {
            screenY = atoi( v );
        }
        else if ( strcmp( k, "fullscreen" ) == 0 )
        {
            fullscreen = atoi( v ) != 0;
        }
        else if ( strcmp( k, "bits_per_pixel" ) == 0 )
        {
            bitsPerPixel = atoi( v );
        }
        else if ( strcmp( k, "refresh_rate" ) == 0 )
        {
            refreshRate = atoi( v );
        }

        // Frustum
        else if ( strcmp( k, "frustum_near" ) == 0 )
        {
            frustumNear = static_cast<float>( atof( v ) );
        }
        else if ( strcmp( k, "frustum_far" ) == 0 )
        {
            frustumFar = static_cast<float>( atof( v ) );
        }

        // Camera controls
        else if ( strcmp( k, "mouse_sensitivity" ) == 0 )
        {
            mouseSensitivity = static_cast<float>( atof( v ) );
        }
        else if ( strcmp( k, "key_speed" ) == 0 )
        {
            keySpeed = static_cast<float>( atof( v ) );
        }
        else if ( strcmp( k, "camera_tween_rate" ) == 0 )
        {
            cameraTweenRate = static_cast<float>( atof( v ) );
        }
        else if ( strcmp( k, "camera_collision_threshold" ) == 0 )
        {
            cameraCollisionThreshold = static_cast<float>( atof( v ) );
        }
        else if ( strcmp( k, "min_camera_height" ) == 0 )
        {
            minCameraHeight = static_cast<float>( atof( v ) );
        }
        else if ( strcmp( k, "max_camera_height" ) == 0 )
        {
            maxCameraHeight = static_cast<float>( atof( v ) );
        }
        else if ( strcmp( k, "min_view_mag" ) == 0 )
        {
            minViewMag = static_cast<float>( atof( v ) );
        }
        else if ( strcmp( k, "max_view_mag" ) == 0 )
        {
            maxViewMag = static_cast<float>( atof( v ) );
        }

        // Terrain
        else if ( strcmp( k, "terrain_scale" ) == 0 )
        {
            terrainScale = static_cast<float>( atof( v ) );
        }
        else if ( strcmp( k, "terrain_height_scale" ) == 0 )
        {
            terrainHeightScale = static_cast<float>( atof( v ) );
        }

        // Skybox
        else if ( strcmp( k, "skybox_render_height" ) == 0 )
        {
            skyboxRenderHeight = static_cast<float>( atof( v ) );
        }
        else if ( strcmp( k, "skybox_overflow" ) == 0 )
        {
            skyboxOverflow = atoi( v );
        }
        else if ( strcmp( k, "skybox_scale" ) == 0 )
        {
            skyboxScale = static_cast<float>( atof( v ) );
        }

        // Physics
        else if ( strcmp( k, "gravity" ) == 0 )
        {
            gravity = static_cast<float>( atof( v ) );
        }
        else if ( strcmp( k, "fluid_height" ) == 0 )
        {
            fluidHeight = static_cast<float>( atof( v ) );
        }
        else if ( strcmp( k, "fluid_density" ) == 0 )
        {
            fluidDensity = static_cast<float>( atof( v ) );
        }
        else if ( strcmp( k, "gas_density" ) == 0 )
        {
            gasDensity = static_cast<float>( atof( v ) );
        }
        else if ( strcmp( k, "velocity_limit" ) == 0 )
        {
            velocityLimit = static_cast<float>( atof( v ) );
        }
        else if ( strcmp( k, "sphere_drag_coeff" ) == 0 )
        {
            sphereDragCoeff = static_cast<float>( atof( v ) );
        }
        else if ( strcmp( k, "friction_coeff" ) == 0 )
        {
            frictionCoeff = static_cast<float>( atof( v ) );
        }
        else if ( strcmp( k, "rolling_friction_coeff" ) == 0 )
        {
            rollingFrictionCoeff = static_cast<float>( atof( v ) );
        }
        else if ( strcmp( k, "spin_friction_coeff" ) == 0 )
        {
            spinFrictionCoeff = static_cast<float>( atof( v ) );
        }
        else if ( strcmp( k, "roll_align_rate" ) == 0 )
        {
            rollAlignRate = static_cast<float>( atof( v ) );
        }
        else if ( strcmp( k, "broadphase_cell" ) == 0 )
        {
            broadphaseCell = static_cast<float>( atof( v ) );
        }

        // Shadows
        else if ( strcmp( k, "shadow_max_height" ) == 0 )
        {
            shadowMaxHeight = static_cast<float>( atof( v ) );
        }
        else if ( strcmp( k, "shadow_max_alpha" ) == 0 )
        {
            shadowMaxAlpha = static_cast<float>( atof( v ) );
        }
        else if ( strcmp( k, "shadow_offset" ) == 0 )
        {
            shadowOffset = static_cast<float>( atof( v ) );
        }
        else if ( strcmp( k, "shadow_segments" ) == 0 )
        {
            shadowSegments = atoi( v );
        }
        else if ( strcmp( k, "shadow_scale" ) == 0 )
        {
            shadowScale = static_cast<float>( atof( v ) );
        }

        // Ball spawn ranges
        else if ( strcmp( k, "spawn_x_base" ) == 0 )
        {
            spawnXBase = static_cast<float>( atof( v ) );
        }
        else if ( strcmp( k, "spawn_x_range" ) == 0 )
        {
            spawnXRange = atoi( v );
        }
        else if ( strcmp( k, "spawn_y_base" ) == 0 )
        {
            spawnYBase = static_cast<float>( atof( v ) );
        }
        else if ( strcmp( k, "spawn_y_range" ) == 0 )
        {
            spawnYRange = atoi( v );
        }
        else if ( strcmp( k, "spawn_z_base" ) == 0 )
        {
            spawnZBase = static_cast<float>( atof( v ) );
        }
        else if ( strcmp( k, "spawn_z_range" ) == 0 )
        {
            spawnZRange = atoi( v );
        }
        else if ( strcmp( k, "ball_mass_min" ) == 0 )
        {
            ballMassMin = static_cast<float>( atof( v ) );
        }
        else if ( strcmp( k, "ball_mass_range" ) == 0 )
        {
            ballMassRange = atoi( v );
        }
        else if ( strcmp( k, "ball_moment_min" ) == 0 )
        {
            ballMomentMin = static_cast<float>( atof( v ) );
        }
        else if ( strcmp( k, "ball_moment_range" ) == 0 )
        {
            ballMomentRange = atoi( v );
        }
        else if ( strcmp( k, "ball_restitution_min" ) == 0 )
        {
            ballRestitutionMin = static_cast<float>( atof( v ) );
        }
        else if ( strcmp( k, "ball_restitution_range" ) == 0 )
        {
            ballRestitutionRange = atoi( v );
        }
        else if ( strcmp( k, "ball_radius_range" ) == 0 )
        {
            ballRadiusRange = atoi( v );
        }
        else if ( strcmp( k, "ball_force_range" ) == 0 )
        {
            ballForceRange = atoi( v );
        }

        // Asset paths
        else if ( strcmp( k, "sky_front" ) == 0 )
        {
            skyFront = v;
        }
        else if ( strcmp( k, "sky_left" ) == 0 )
        {
            skyLeft = v;
        }
        else if ( strcmp( k, "sky_back" ) == 0 )
        {
            skyBack = v;
        }
        else if ( strcmp( k, "sky_right" ) == 0 )
        {
            skyRight = v;
        }
        else if ( strcmp( k, "sky_up" ) == 0 )
        {
            skyUp = v;
        }
        else if ( strcmp( k, "sky_down" ) == 0 )
        {
            skyDown = v;
        }
        else if ( strcmp( k, "terrain_texture" ) == 0 )
        {
            terrainTexture = v;
        }
        else if ( strcmp( k, "sphere_texture" ) == 0 )
        {
            sphereTexture = v;
        }
        else if ( strcmp( k, "terrain_raw" ) == 0 )
        {
            terrainRaw = v;
        }

        // Water
        else if ( strcmp( k, "ocean_wave_height" ) == 0 )
        {
            oceanWaveHeight = static_cast<float>( atof( v ) );
        }
        else if ( strcmp( k, "ocean_perturb_strength" ) == 0 )
        {
            oceanPerturbStrength = static_cast<float>( atof( v ) );
        }

        // Debug
        else if ( strcmp( k, "render_collision_volumes" ) == 0 )
        {
            renderCollisionVolumes = atoi( v ) != 0;
        }
    }

    fclose( f );
}
