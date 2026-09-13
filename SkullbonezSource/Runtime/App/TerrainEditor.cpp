// App sequences editable terrain changes before Physics and GPU consumers.
#include "Run.h"
#include "../Startup/Window.h"
#include "../../Core/Log.h"
#include <commdlg.h>
#include "../../Rendering/DX12/RenderBackendDX12.h"
#include <algorithm>
#include <cmath>

namespace SkullbonezCore::Runtime
{
bool Run::ChooseTerrainHeightMap( char ( &heightMap )[260] )
{
    OPENFILENAMEA dialog = {};
    dialog.lStructSize = sizeof( dialog );
    dialog.hwndOwner = m_window.NativeWindowHandle();
    dialog.lpstrFile = heightMap;
    dialog.nMaxFile = MAX_PATH;
    dialog.lpstrFilter = "Height maps (*.heightmap;*.raw)\0*.heightmap;*.raw\0\0";
    dialog.lpstrTitle = "Choose height map for new level";
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    bool selected = false;
#if defined( SKULLBONEZ_SKARNESS )
    const bool supplied = m_skarness.TakeFileDialogResponse( "terrain.import", heightMap, selected );
    if ( !supplied )
#endif
        selected = GetOpenFileNameA( &dialog ) != FALSE;
    if ( !selected )
    {
        return false;
    }
    std::unique_ptr<Geometry::Terrain> imported;
    const auto checked = Geometry::Terrain::LoadSavedHeightMap( m_resultDiagnostics, heightMap, m_config, imported );
    if ( !checked.Ok() )
    {
        Core::Log().WriteEventf( "terrain_import_failed reason=%s", checked.ErrorMessage() );
        return false;
    }
    return true;
}

bool ReplayRuntime::PrepareTerrainEdit()
{
    if ( m_bluePrediction )
    {
        return false;
    }
    // Hazard: prediction workers borrow the collision grid. Join before a stroke
    // overwrites cells and discard geometry computed against the previous grid.
    Prediction().CancelJob( true );
    return true;
}

bool Run::RouteTerrainBrush( const RuntimePointerEvent& pointer, bool hasRay, const Math::Vector::Vector3& origin, const Math::Vector::Vector3& direction )
{
    auto& editor = m_editorTools.Editor();
    editor.terrainBrushVisible = false;
    if ( !editor.editorModeEnabled || !editor.terrainBrushEnabled )
    {
        return false;
    }
    auto& world = m_sceneController.Scene();
    Geometry::Terrain* terrain = world.Terrain().Get();
    EditorTerrainPlacement hit;
    if ( !terrain || pointer.uiBlocksCameraMouse || pointer.suppressWorldAction || !hasRay || !TryGetEditorTerrainPlacement( terrain, origin, direction, hit ) )
    {
        return true;
    }
    editor.placementPreviewVisible = false;
    editor.hotGizmoAxis = -1;
    editor.hotRotationAxis = -1;
    const float sign = static_cast<float>( pointer.leftDown ) - static_cast<float>( pointer.rightDown );
    if ( sign != 0.0f && terrain->IsEditingPrepared() && m_replayRuntime.PrepareTerrainEdit() )
    {
        const float seconds = static_cast<float>( std::clamp( m_timers.Publish().secondsPerFrame, 0.0, 0.05 ) );
        if ( terrain->Sculpt( hit.position, editor.terrainBrushRadius, sign * 30.0f * seconds ) )
        {
            // Physics owns body sleep state; a changed support surface must wake
            // sleepers before the next collision pass evaluates their support.
            world.Physics().SetTerrainView( terrain->PhysicsView() );
            for ( int row = 0; row < world.BodyStore().Count(); ++row )
            {
                world.Physics().WakeBody( world.BodyStore().HandleForModelIndex( row ) );
            }
            if ( !terrain->UploadEditedMesh() )
            {
                SB_FATAL( "Runtime/TerrainEditor", "Terrain vertex upload failed." );
            }
            if ( !Renderer().ResourceLifecycle().RefreshTerrainGeometry() )
            {
                SB_FATAL( "Runtime/TerrainEditor", "Terrain ray geometry update failed." );
            }
        }
    }
    editor.terrainBrushVisible = true;
    const auto bounds = terrain->GetXZBounds();
    for ( size_t i = 0; i < editor.terrainBrushOutline.size(); ++i )
    {
        const float angle = static_cast<float>( i ) * 6.28318530718f / 64.0f;
        const float x = std::clamp( hit.position.x + std::cos( angle ) * editor.terrainBrushRadius, bounds.m_xMin, bounds.m_xMax - 0.001f );
        const float z = std::clamp( hit.position.z + std::sin( angle ) * editor.terrainBrushRadius, bounds.m_zMin, bounds.m_zMax - 0.001f );
        editor.terrainBrushOutline[i] = Math::Vector::Vector3( x, terrain->GetTerrainHeightAt( x, z ) + 0.3f, z );
    }
    return true;
}
} // namespace SkullbonezCore::Runtime
