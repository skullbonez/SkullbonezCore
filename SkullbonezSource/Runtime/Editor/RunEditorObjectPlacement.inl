/*
File: SkullbonezSource/Runtime/Editor/RunEditorObjectPlacement.inl
Purpose:
  Contains editor object placement preflight and commit logic.

Mental model:
  Placement is the ownership boundary between UI-selected asset recipes and live
  scene model creation. This slice validates the terrain point, computes object
  transforms, and adds the requested body or compound asset to the model store.

Glossary:
  Preflight: Placement validation that checks object type, terrain point, and
    asset recipe availability without mutating the scene.
  Placement request: User-selected object type, static/dynamic mode, and target
    terrain point.

Invariants:
  - Preflight and commit must use matching geometry decisions.
  - Placement names and model order are replay-visible and must stay stable.

Related:
  - SkullbonezSource/Runtime/Editor/RunEditorTools.cpp
  - SkullbonezSource/Runtime/Editor/RunEditorPlacementAssets.inl
*/
namespace SkullbonezCore
{
namespace Basics
{
namespace RunInternal
{
static bool TryResolveEditorObjectPlacementPreflight( EditorObjectPlacementContext context,
                                                      EditorObjectPlacementRequest request,
                                                      int& outType,
                                                      bool reportErrors )
{
    // Invariant: This preflight is the single capacity and asset-count gate
    // for both CanPlace and Place. Add new multi-part object families here
    // before adding their placement branch below.
    const int modelCount = context.models.GetModelCount();
    const int type = std::clamp( request.objectType, 0, UI::EditorTab::OBJECT_TYPE_COUNT - 1 );
    const EditorTreeDefinition* tree = EditorTreeDefinitionForType( type );
    const EditorHouseDefinition* house = EditorHouseDefinitionForType( type );
    const EditorBuildingDefinition* building = EditorBuildingDefinitionForType( type );
    const int buildingPartCount = building ? EditorBuildingPartCount( type, context.assets ) : 0;
    const bool isRagdollType = type == UI::EditorTab::OBJECT_RAGDOLL || type == UI::EditorTab::OBJECT_RAGDOLL_SLEEP;
    if ( building && buildingPartCount <= 0 )
    {
        if ( reportErrors )
        {
            fprintf( stderr, "[editor] Cannot place building asset: %s is missing or empty.\n", building->assetName );
        }
        return false;
    }
    const int requiredModelCount =
        isRagdollType
            ? Ragdoll::SIMPLE_PART_COUNT
            : ( building ? buildingPartCount : ( house ? house->partCount : ( tree ? tree->partCount : 1 ) ) );
    if ( modelCount + requiredModelCount > context.activeModelCapacity )
    {
        if ( reportErrors )
        {
            fprintf( stderr, "[editor] Cannot place object: model capacity reached.\n" );
        }
        return false;
    }
    outType = type;
    return true;
}


bool CanPlaceEditorObjectAtTerrainPoint( EditorObjectPlacementContext context, EditorObjectPlacementRequest request )
{
    int type = 0;
    return TryResolveEditorObjectPlacementPreflight( context, request, type, true );
}


bool PlaceEditorObjectAtTerrainPoint( EditorObjectPlacementContext context,
                                      EditorObjectPlacementRequest request,
                                      EditorObjectPlacementResult& outResult )
{
    int type = 0;
    if ( !TryResolveEditorObjectPlacementPreflight( context, request, type, false ) )
    {
        outResult = EditorObjectPlacementResult{};
        return false;
    }

    const int modelCount = context.models.GetModelCount();
    const EditorTreeDefinition* tree = EditorTreeDefinitionForType( type );
    const EditorHouseDefinition* house = EditorHouseDefinitionForType( type );
    const EditorBuildingDefinition* building = EditorBuildingDefinitionForType( type );
    const Vector3& terrainPoint = request.terrainPoint;
    const bool fixedObject = request.fixedObject;
    const Vector3 placementScale = EditorClampPlacementScale( type, context.editor.placementScale );
    const int serial = context.editor.placedObjectSerial++;
    Vector3 terrainNormal( 0.0f, 1.0f, 0.0f );
    if ( context.terrain && context.terrain->IsInBounds( terrainPoint.x, terrainPoint.z ) )
    {
        float ignoredHeight = 0.0f;
        context.terrain->GetTerrainHeightAndNormalAt( terrainPoint.x, terrainPoint.z, ignoredHeight, terrainNormal );
    }
    const bool alignToTerrain = EditorObjectAlignsToTerrainNormal( type, context.editor.autoTerrainAlign );
    const Quaternion placementOrientation = EditorPlacementOrientation( type,
                                                                        terrainNormal,
                                                                        context.editor.autoTerrainAlign,
                                                                        context.editor.placementYawRadians );
    Quaternion placementOrientationCopy = placementOrientation;
    const RotationMatrix placementRotation = placementOrientationCopy.GetOrientationMatrix();
    const bool placementFixed = tree && tree->forceFixed ? true : fixedObject;
    const bool ragdollStartsAsleep = type == UI::EditorTab::OBJECT_RAGDOLL_SLEEP;
    const char* modePrefix = placementFixed ? "static"
                                            : ( ( tree && tree->seedAsleep ) || ( house && house->seedAsleep ) ||
                                                        building || ragdollStartsAsleep
                                                    ? "sleeping"
                                                    : "dynamic" );

    auto addModel = [&]( GameModel model, bool modelFixed, bool modelStartsAsleep = false )
    {
        // Lifetime: The new model becomes owned by GameModelCollection here.
        // Physics sleep state must be seeded immediately, while the returned
        // placement result reports only the before/after count.
        model.SetFixed( modelFixed );
        const int index = context.models.GetModelCount();
        context.models.AddGameModel( std::move( model ) );
        if ( !modelFixed )
        {
            if ( modelStartsAsleep )
            {
                context.models.SeedModelAsleep( index );
            }
            else
            {
                context.models.WakeModel( index );
            }
        }
    };

    auto addSphere = [&]( const char* label, float radius, float restitution )
    {
        const float mass = CalculateSphereMass( radius );
        const Vector3 inertia = CalculateSphereInertia( radius, mass );
        const Vector3 center( terrainPoint.x,
                              terrainPoint.y + radius + EDITOR_PLACEMENT_SURFACE_EPSILON,
                              terrainPoint.z );
        GameModel model( &context.world, center, inertia, mass );
        model.SetTerrain( context.terrain );
        model.SetCoefficientRestitution( restitution );
        model.AddBoundingSphere( radius );
        model.SetRenderTint( 1.0f, 1.0f, 1.0f, EDITOR_TEXTURE_MODE_INVERTED );
        char name[64];
        sprintf_s( name, sizeof( name ), "%s_%s_%03d", modePrefix, label, serial );
        model.SetName( name );
        addModel( std::move( model ), placementFixed );
    };

    auto addBox = [&]()
    {
        const Vector3 halfExtents = placementScale;
        const float mass = CalculateBoxMass( halfExtents );
        Vector3 center;
        if ( !TryComputeEditorObjectCenter( type,
                                            terrainPoint,
                                            placementScale,
                                            placementOrientation,
                                            context.assets,
                                            center ) )
        {
            return;
        }
        GameModel model( &context.world, center, CalculateBoxInertiaForHalfExtents( halfExtents, mass ), mass );
        model.SetTerrain( context.terrain );
        model.SetCoefficientRestitution( 0.25f );
        model.AddBoundingBox( halfExtents );
        if ( alignToTerrain )
        {
            model.SetOrientation( placementOrientation );
        }
        ApplyEditorSpawnMaterial( model, fixedObject, true );
        char name[64];
        sprintf_s( name, sizeof( name ), "%s_box_%03d", modePrefix, serial );
        model.SetName( name );
        addModel( std::move( model ), placementFixed );
    };

    auto addHull = [&]( EditorHullAsset asset )
    {
        const char* label = EditorHullAssetToken( asset );
        const char* path = EditorHullAssetPath( asset );
        if ( !path )
        {
            return;
        }
        const ConvexHullShape hull = ConvexHullShape::LoadFromFile( path );
        ConvexHullShape scaledHull = hull;
        scaledHull.ScaleAxis( 0, placementScale.x );
        scaledHull.ScaleAxis( 1, placementScale.y );
        scaledHull.ScaleAxis( 2, placementScale.z );
        const float mass = scaledHull.GetDefaultMass();
        const bool alignHull = alignToTerrain;
        const RotationMatrix hullRotation = alignHull ? placementRotation : IDENTITY_MATRIX;
        const Quaternion hullOrientation = alignHull ? placementOrientation : IDENTITY_QUATERNION;
        const Vector3 authoredOrigin =
            terrainPoint +
            hullRotation *
                Vector3( 0.0f, HullAuthoredBottomOffset( scaledHull ) + EDITOR_PLACEMENT_SURFACE_EPSILON, 0.0f );
        const Vector3 center = authoredOrigin + hullRotation * scaledHull.GetAuthoredCenterOfMass();
        GameModel model( &context.world, center, scaledHull.ComputeBoxApproxInertia( mass ), mass );
        model.SetTerrain( context.terrain );
        model.SetCoefficientRestitution( 0.25f );
        model.AddConvexHull( scaledHull );
        model.SetOrientation( hullOrientation );
        SkullbonezCore::Rendering::RenderMaterial rockMaterial;
        if ( TryEditorRockMaterial( asset, rockMaterial ) )
        {
            model.SetRenderMaterial( rockMaterial );
        }
        else if ( TryEditorRootMaterial( asset, rockMaterial ) )
        {
            model.SetRenderMaterial( rockMaterial );
        }
        else
        {
            ApplyEditorSpawnMaterial( model, fixedObject, false );
        }
        char name[64];
        sprintf_s( name, sizeof( name ), "%s_%s_%03d", modePrefix, label, serial );
        model.SetName( name );
        addModel( std::move( model ), placementFixed );
    };

    auto addTree = [&]( const EditorTreeDefinition& treeDefinition )
    {
        for ( int partIndex = 0; partIndex < treeDefinition.partCount; ++partIndex )
        {
            const EditorTreePartDefinition& part = treeDefinition.parts[partIndex];
            if ( !CachedEditorHullForAsset( part.hullAsset ) )
            {
                fprintf( stderr,
                         "[editor] Cannot place tree: missing hull asset %s.\n",
                         EditorHullAssetToken( part.hullAsset ) );
                return;
            }
        }

        for ( int partIndex = 0; partIndex < treeDefinition.partCount; ++partIndex )
        {
            const EditorTreePartDefinition& part = treeDefinition.parts[partIndex];
            const ConvexHullShape* sourceHull = CachedEditorHullForAsset( part.hullAsset );
            if ( !sourceHull )
            {
                continue;
            }
            ConvexHullShape hull = *sourceHull;
            const Vector3 localOffset( part.offsetX, part.offsetY, part.offsetZ );
            const Vector3 authoredOrigin =
                terrainPoint +
                placementRotation * ( localOffset + Vector3( 0.0f, EDITOR_PLACEMENT_SURFACE_EPSILON, 0.0f ) );
            const Vector3 center = authoredOrigin + placementRotation * hull.GetAuthoredCenterOfMass();
            const float mass = hull.GetDefaultMass();
            GameModel model( &context.world, center, hull.ComputeBoxApproxInertia( mass ), mass );
            model.SetTerrain( context.terrain );
            model.SetCoefficientRestitution( part.restitution );
            model.AddConvexHull( hull );
            model.SetOrientation( placementOrientation );
            model.SetContactReleaseOnImpact( part.contactReleaseOnImpact, part.contactReleaseImpulseThreshold );
            model.SetRenderMaterial( EditorTreePartMaterial( part ) );
            char name[64];
            sprintf_s( name, sizeof( name ), "%s_%s_%03d_%s", modePrefix, treeDefinition.label, serial, part.suffix );
            model.SetName( name );
            const bool partFixed = treeDefinition.forceFixed || part.startsFixed || placementFixed;
            addModel( std::move( model ), partFixed, treeDefinition.seedAsleep && !partFixed );
        }
    };

    auto addHouse = [&]( const EditorHouseDefinition& houseDefinition )
    {
        const Vector3 base = terrainPoint + placementRotation * Vector3( 0.0f, EDITOR_PLACEMENT_SURFACE_EPSILON, 0.0f );
        for ( int partIndex = 0; partIndex < houseDefinition.partCount; ++partIndex )
        {
            const EditorHousePartDefinition& part = houseDefinition.parts[partIndex];
            const Vector3 halfExtents( part.halfX, part.halfY, part.halfZ );
            const float mass = CalculateBoxMass( halfExtents );
            const Vector3 center = base + placementRotation * Vector3( part.offsetX, part.offsetY, part.offsetZ );
            GameModel model( &context.world, center, CalculateBoxInertiaForHalfExtents( halfExtents, mass ), mass );
            model.SetTerrain( context.terrain );
            model.SetCoefficientRestitution( part.restitution );
            model.AddBoundingBox( halfExtents );
            model.SetOrientation( placementOrientation );
            model.SetRenderMaterial( EditorHousePartMaterial( part ) );
            char name[64];
            sprintf_s( name, sizeof( name ), "%s_%s_%03d_%s", modePrefix, houseDefinition.label, serial, part.suffix );
            model.SetName( name );
            addModel( std::move( model ), placementFixed, houseDefinition.seedAsleep && !placementFixed );
        }
    };

    auto addBuilding = [&]( const EditorBuildingDefinition& buildingDefinition )
    {
        bool failed = false;
        const Vector3 base = terrainPoint + placementRotation * Vector3( 0.0f, EDITOR_PLACEMENT_SURFACE_EPSILON, 0.0f );
        const bool ok = ForEachEditorBuildingPart(
            type,
            context.assets,
            [&]( const Json& part )
            {
                if ( failed )
                {
                    return;
                }
                const std::string hullPath = EditorJsonStringOr( part, "hull", "" );
                const ConvexHullShape* sourceHull = hullPath.empty() ? nullptr : CachedEditorBuildingHull( hullPath );
                if ( !sourceHull )
                {
                    failed = true;
                    return;
                }

                ConvexHullShape hull = *sourceHull;
                const float mass = EditorJsonFloatOr( part, "mass", hull.GetDefaultMass() );
                const float restitution = EditorJsonFloatOr( part, "restitution", 0.08f );
                const Vector3 offset = EditorJsonVec3Or( part, "offset", Vector3( 0.0f, 0.0f, 0.0f ) );
                const Quaternion partOrientation = EditorBuildingPartOrientation( placementOrientation, part );
                Quaternion partCopy = partOrientation;
                const RotationMatrix partRotation = partCopy.GetOrientationMatrix();
                const Vector3 authoredOrigin = base + placementRotation * offset;
                const Vector3 center = authoredOrigin + partRotation * hull.GetAuthoredCenterOfMass();
                GameModel model( &context.world, center, hull.ComputeBoxApproxInertia( mass ), mass );
                model.SetTerrain( context.terrain );
                model.SetCoefficientRestitution( restitution );
                model.SetContactReleaseOnImpact(
                    EditorJsonBoolOr( part, "contactReleaseOnImpact", false ),
                    (std::max)( 0.0f, EditorJsonFloatOr( part, "contactReleaseImpulseThreshold", 1.0f ) ) );
                model.AddConvexHull( hull );
                model.SetOrientation( partOrientation );
                model.SetRenderMaterial( EditorBuildingPartMaterial( part ) );
                if ( const Json* velocity = EditorJsonFindMember( part, "velocity" ) )
                {
                    Vector3 authoredVelocity;
                    if ( TryReadEditorJsonVec3( *velocity, authoredVelocity ) )
                    {
                        model.SetLinearVelocity( authoredVelocity );
                    }
                }

                char name[64];
                const std::string partName = EditorJsonStringOr( part, "name", "part" );
                snprintf( name,
                          sizeof( name ),
                          "%s_%s_%03d_%s",
                          modePrefix,
                          buildingDefinition.label,
                          serial,
                          partName.c_str() );
                name[sizeof( name ) - 1] = '\0';
                model.SetName( name );
                const bool partFixed = placementFixed || EditorJsonBoolOr( part, "fixed", false );
                const bool partSleeping = EditorJsonBoolOr( part, "sleeping", true );
                addModel( std::move( model ), partFixed, partSleeping && !partFixed );
            } );
        if ( failed || !ok )
        {
            fprintf( stderr, "[editor] Cannot place building asset: %s.\n", buildingDefinition.assetName );
        }
    };

    auto addRagdoll = [&]()
    {
        RagdollBuildOptions options;
        char prefix[64];
        sprintf_s( prefix, sizeof( prefix ), "%s_ragdoll_%03d", modePrefix, serial );
        options.namePrefix = prefix;
        options.terrainPoint = terrainPoint;
        options.orientation = placementOrientation;
        options.scale = placementScale.x;
        options.fixed = placementFixed;
        options.startsAsleep = ragdollStartsAsleep && !placementFixed;
        Ragdoll::AddSimpleHumanoid( context.models,
                                    context.models.GetPhysicsEngine(),
                                    context.world,
                                    context.terrain,
                                    options );
    };

    switch ( type )
    {
    case UI::EditorTab::OBJECT_BOX:
        addBox();
        break;
    case UI::EditorTab::OBJECT_BALL:
        addSphere( "ball", placementScale.x, 0.45f );
        break;
    case UI::EditorTab::OBJECT_SPHERE:
        addSphere( "sphere", placementScale.x, 0.35f );
        break;
    case UI::EditorTab::OBJECT_HULL_WEDGE:
        addHull( EditorHullAsset::WEDGE );
        break;
    case UI::EditorTab::OBJECT_HULL_TRI_PRISM:
        addHull( EditorHullAsset::TRI_PRISM );
        break;
    case UI::EditorTab::OBJECT_HULL_TAPERED_BLOCK:
        addHull( EditorHullAsset::TAPERED_BLOCK );
        break;
    case UI::EditorTab::OBJECT_HULL_PYRAMID:
        addHull( EditorHullAsset::PYRAMID );
        break;
    case UI::EditorTab::OBJECT_HULL_HEX_PRISM:
        addHull( EditorHullAsset::HEX_PRISM );
        break;
    case UI::EditorTab::OBJECT_HULL_DIAMOND:
        addHull( EditorHullAsset::DIAMOND );
        break;
    case UI::EditorTab::OBJECT_ROCK_SLAB:
        addHull( EditorHullAsset::ROCK_SLAB_FLAT );
        break;
    case UI::EditorTab::OBJECT_ROCK_LUMP:
        addHull( EditorHullAsset::ROCK_LUMP_LARGE );
        break;
    case UI::EditorTab::OBJECT_ROCK_SHARD:
        addHull( EditorHullAsset::ROCK_SHARD_TALL );
        break;
    case UI::EditorTab::OBJECT_ROCK_CHIPPED:
        addHull( EditorHullAsset::ROCK_CHIPPED_BLOCK );
        break;
    case UI::EditorTab::OBJECT_ROOT_SMALL:
        addHull( EditorHullAsset::TREE_ROOT_SMALL );
        break;
    case UI::EditorTab::OBJECT_ROOT_LARGE:
        addHull( EditorHullAsset::TREE_ROOT_LARGE );
        break;
    case UI::EditorTab::OBJECT_TREE_SMALL:
        if ( tree )
        {
            addTree( *tree );
        }
        break;
    case UI::EditorTab::OBJECT_TREE_BIG:
    case UI::EditorTab::OBJECT_TREE_CEDAR:
    case UI::EditorTab::OBJECT_TREE_SMALL_SLOPE:
    case UI::EditorTab::OBJECT_TREE_BIG_SLOPE:
    case UI::EditorTab::OBJECT_TREE_CEDAR_SLOPE:
    case UI::EditorTab::OBJECT_TREE_SMALL_SLEEP:
    case UI::EditorTab::OBJECT_TREE_BIG_SLEEP:
    case UI::EditorTab::OBJECT_TREE_CEDAR_SLEEP:
    case UI::EditorTab::OBJECT_TREE_SMALL_ROOTED:
    case UI::EditorTab::OBJECT_TREE_BIG_ROOTED:
    case UI::EditorTab::OBJECT_TREE_CEDAR_ROOTED:
    case UI::EditorTab::OBJECT_TREE_PINE_SHEDDING:
        if ( tree )
        {
            addTree( *tree );
        }
        break;
    case UI::EditorTab::OBJECT_BRICK_HOUSE_SLEEP:
    case UI::EditorTab::OBJECT_BRICK_HOUSE_HIGH_SLEEP:
    case UI::EditorTab::OBJECT_CUTE_HOUSE_SLEEP:
    case UI::EditorTab::OBJECT_CUTE_HOUSE_HIGH_SLEEP:
    case UI::EditorTab::OBJECT_TRIPLE_DECKER_SLEEP:
    case UI::EditorTab::OBJECT_TRIPLE_DECKER_HIGH_SLEEP:
        if ( building )
        {
            addBuilding( *building );
        }
        break;
    case UI::EditorTab::OBJECT_RAGDOLL:
    case UI::EditorTab::OBJECT_RAGDOLL_SLEEP:
        addRagdoll();
        break;
    default:
        break;
    }

    context.scene.modelCount = context.models.GetModelCount();
    const bool placed = context.scene.modelCount > modelCount;
    outResult.placed = placed;
    outResult.modelCountBefore = modelCount;
    outResult.modelCountAfter = context.scene.modelCount;
    outResult.objectType = type;
    outResult.fixedObject = fixedObject;
    outResult.autoTerrainAlign = context.editor.autoTerrainAlign;
    outResult.terrainPoint = terrainPoint;
    outResult.placementScale = placementScale;
    outResult.placementYawRadians = context.editor.placementYawRadians;
    return placed;
}
} // namespace RunInternal
} // namespace Basics
} // namespace SkullbonezCore
