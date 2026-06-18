/*
File: SkullbonezSource/SkullbonezSceneSnapshotWriter.cpp
Purpose:
  Serializes the current scene state back into a scene file.

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
  - SkullbonezSource/SkullbonezSceneSnapshotWriter.h
  - Agentic/Reference/runtime-reference.md
  - Agentic/Reference/comment-style-guide.md
*/
#include "SkullbonezSceneSnapshotWriter.h"

#include "SkullbonezBoundingBox.h"
#include "SkullbonezBoundingSphere.h"
#include "SkullbonezConvexHullShape.h"
#include "SkullbonezEditorHullAssets.h"
#include "SkullbonezGameModelCollection.h"
#include "SkullbonezWorldEnvironment.h"

#include <cstdio>
#include <variant>

using namespace SkullbonezCore::Environment;
using namespace SkullbonezCore::GameObjects;
using SkullbonezCore::Assets::EditorHullAsset;
using SkullbonezCore::Assets::EditorHullAssetFromToken;
using SkullbonezCore::Assets::EditorHullAssetToken;
using SkullbonezCore::Math::CollisionDetection::BoundingBox;
using SkullbonezCore::Math::CollisionDetection::BoundingSphere;
using SkullbonezCore::Math::CollisionDetection::ConvexHullShape;
using SkullbonezCore::Math::Vector::Vector3;


bool SceneSnapshotWriter::Save( GameModelCollection& collection,
                                const char* path,
                                bool physicsOn,
                                bool textOn,
                                WorldEnvironment& worldEnv,
                                const Vector3& camEye,
                                const Vector3& camView,
                                const Vector3& camUp,
                                bool editableScene,
                                bool fixedStep,
                                bool waterHidden,
                                bool terrainHidden,
                                bool hasFlatSlope,
                                float flatBaseY,
                                float flatSlopeX,
                                float flatSlopeZ )
{
    auto& m_gameModels = collection.m_gameModels;

    FILE* f = nullptr;
    if ( fopen_s( &f, path, "w" ) != 0 || !f )
    {
        return false;
    }

    fprintf( f, "# Snapshot - %d models\n", static_cast<int>( m_gameModels.size() ) );
    fprintf( f, "physics %s\n", physicsOn ? "on" : "off" );
    fprintf( f, "text %s\n", textOn ? "on" : "off" );
    if ( editableScene )
    {
        fprintf( f, "editable_scene on\n" );
    }
    fprintf( f, "frames unlimited\n" );
    if ( fixedStep )
    {
        fprintf( f, "fixed_step\n" );
    }
    fprintf( f, "world %f %f %f\n", worldEnv.GetGravity(), worldEnv.GetFluidSurfaceHeight(), worldEnv.GetFluidDensity() );
    if ( waterHidden )
    {
        fprintf( f, "water_hidden on\n" );
    }
    if ( terrainHidden )
    {
        fprintf( f, "terrain_hidden on\n" );
    }
    if ( hasFlatSlope )
    {
        fprintf( f, "flat_slope %.6f %.6f %.6f\n", flatBaseY, flatSlopeX, flatSlopeZ );
    }
    fprintf( f, "camera main  %.4f %.4f %.4f  %.4f %.4f %.4f  %.4f %.4f %.4f\n", camEye.x, camEye.y, camEye.z, camView.x, camView.y, camView.z, camUp.x, camUp.y, camUp.z );
    fprintf( f, "\n" );

    for ( int i = 0; i < static_cast<int>( m_gameModels.size() ); ++i )
    {
        const char* name = m_gameModels[i].GetName();
        char safeName[64];
        if ( !name[0] )
        {
            sprintf_s( safeName, sizeof( safeName ), "_ball_%d", i );
            name = safeName;
        }

        const Vector3& pos = m_gameModels[i].GetPosition();
        const Vector3& vel = m_gameModels[i].GetVelocity();
        const Vector3& avel = m_gameModels[i].GetAngularVelocity();
        const Vector3& ri = m_gameModels[i].GetRotationalInertia();
        const auto& shape = m_gameModels[i].GetCollisionShape();
        float qx, qy, qz, qw;
        m_gameModels[i].GetOrientation().GetComponents( qx, qy, qz, qw );
        float mass = m_gameModels[i].GetMass();
        float rest = m_gameModels[i].GetCoefficientRestitution();

        if ( std::holds_alternative<BoundingSphere>( shape ) )
        {
            const BoundingSphere& sphere = std::get<BoundingSphere>( shape );
            fprintf( f,
                     "ball_state %s  %.6f %.6f %.6f  %.6f %.6f %.6f  %.6f %.6f %.6f"
                     "  %.8f %.8f %.8f %.8f  %.4f %.4f %.4f  %.4f %.4f %.4f  %d\n",
                     name,
                     pos.x,
                     pos.y,
                     pos.z,
                     vel.x,
                     vel.y,
                     vel.z,
                     avel.x,
                     avel.y,
                     avel.z,
                     qx,
                     qy,
                     qz,
                     qw,
                     sphere.GetRadius(),
                     mass,
                     rest,
                     ri.x,
                     ri.y,
                     ri.z,
                     m_gameModels[i].IsFixed() ? 1 : 0 );
        }
        else if ( std::holds_alternative<BoundingBox>( shape ) )
        {
            const BoundingBox& box = std::get<BoundingBox>( shape );
            const Vector3& halfExtents = box.GetHalfExtents();
            fprintf( f,
                     "box_state %s  %.6f %.6f %.6f  %.6f %.6f %.6f  %.6f %.6f %.6f"
                     "  %.8f %.8f %.8f %.8f  %.4f %.4f %.4f  %.4f %.4f  %.4f %.4f %.4f  %d\n",
                     name,
                     pos.x,
                     pos.y,
                     pos.z,
                     vel.x,
                     vel.y,
                     vel.z,
                     avel.x,
                     avel.y,
                     avel.z,
                     qx,
                     qy,
                     qz,
                     qw,
                     halfExtents.x,
                     halfExtents.y,
                     halfExtents.z,
                     mass,
                     rest,
                     ri.x,
                     ri.y,
                     ri.z,
                     m_gameModels[i].IsFixed() ? 1 : 0 );
        }
        else if ( std::holds_alternative<ConvexHullShape>( shape ) )
        {
            const ConvexHullShape& hull = std::get<ConvexHullShape>( shape );
            const EditorHullAsset hullAsset = EditorHullAssetFromToken( hull.GetName() );
            const char* hullToken = hullAsset == EditorHullAsset::UNKNOWN ? hull.GetName() : EditorHullAssetToken( hullAsset );
            fprintf( f,
                     "convex_hull_state %s hull=%s  %.6f %.6f %.6f  %.6f %.6f %.6f  %.6f %.6f %.6f"
                     "  %.8f %.8f %.8f %.8f  %.4f %.4f  %.4f %.4f %.4f  %d\n",
                     name,
                     hullToken,
                     pos.x,
                     pos.y,
                     pos.z,
                     vel.x,
                     vel.y,
                     vel.z,
                     avel.x,
                     avel.y,
                     avel.z,
                     qx,
                     qy,
                     qz,
                     qw,
                     mass,
                     rest,
                     ri.x,
                     ri.y,
                     ri.z,
                     m_gameModels[i].IsFixed() ? 1 : 0 );
        }
    }

    fclose( f );
    return true;
}
