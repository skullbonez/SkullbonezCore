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

#include "SkullbonezGameModelCollection.h"
#include "SkullbonezWorldEnvironment.h"

#include <cstdio>

using namespace SkullbonezCore::Environment;
using namespace SkullbonezCore::GameObjects;
using SkullbonezCore::Math::Vector::Vector3;


bool SceneSnapshotWriter::Save( GameModelCollection& collection, const char* path, bool physicsOn, bool textOn, WorldEnvironment& worldEnv, const Vector3& camEye, const Vector3& camView, const Vector3& camUp )
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
    fprintf( f, "frames unlimited\n" );
    fprintf( f, "world %f %f %f\n", worldEnv.GetGravity(), worldEnv.GetFluidSurfaceHeight(), worldEnv.GetFluidDensity() );
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
        float qx, qy, qz, qw;
        m_gameModels[i].GetOrientation().GetComponents( qx, qy, qz, qw );
        float r = m_gameModels[i].GetBoundingRadius();
        float mass = m_gameModels[i].GetMass();
        float rest = m_gameModels[i].GetCoefficientRestitution();

        fprintf( f,
                 "ball_state %s  %.6f %.6f %.6f  %.6f %.6f %.6f  %.6f %.6f %.6f"
                 "  %.8f %.8f %.8f %.8f  %.4f %.4f %.4f  %.4f %.4f %.4f\n",
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
                 r,
                 mass,
                 rest,
                 ri.x,
                 ri.y,
                 ri.z );
    }

    fclose( f );
    return true;
}
