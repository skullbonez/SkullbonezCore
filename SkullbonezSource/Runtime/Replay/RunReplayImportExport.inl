/*
File: SkullbonezSource/Runtime/Replay/RunReplayImportExport.inl
Purpose:
  Contains replay save/load helper glue used by the scrubber controls.

Mental model:
  Import/export work chooses artifact paths and reports user-facing status, but
  the actual serialization remains owned by ReplayRuntime and replay artifact
  modules.

Glossary:
  Replay artifact: On-disk solver or presentation replay file.
  Scrubber message: Short-lived UI status text shown near the replay controls.

Invariants:
  - Numbered replay filenames must stay stable for automation and operator use.
  - Save messages update the active scrubber track that triggered the artifact.

Related:
  - SkullbonezSource/Runtime/Replay/RunReplayTools.cpp
  - SkullbonezSource/Runtime/Replay/ReplayRuntime.h
*/
bool SaveReplayBufferFromScrubber( ReplayRuntime& replayRuntime, RunReplayTrack track, double now )
{
    // Invariant: The scrubber owns transient save status, but ReplayRuntime owns
    // the artifact serialization and track-specific buffer selection.
    static int sReplaySeq = 0;
    static int sSolverReplaySeq = 0;

    char path[256] = {};
    bool saved = false;
    int& sequence = track == RunReplayTrack::Solver ? sSolverReplaySeq : sReplaySeq;
    const char* prefix = track == RunReplayTrack::Solver ? "solver_replay_" : "replay_v2_";
    if ( RuntimeFileWriter::NextNumberedPath( path, sizeof( path ), "replays", prefix, ".skreplay", sequence ) )
    {
        saved = track == RunReplayTrack::Solver ? replayRuntime.SaveSolverReplay( path )
                                                : replayRuntime.SavePresentationWithSolverHashes( path );
    }

    replayRuntime.Scrubber().saveMessageTrack = track;
    if ( saved )
    {
        const char* fileName = strrchr( path, '\\' );
        if ( !fileName )
        {
            fileName = strrchr( path, '/' );
        }
        fileName = fileName ? fileName + 1 : path;
        sprintf_s( replayRuntime.Scrubber().saveMessage,
                   sizeof( replayRuntime.Scrubber().saveMessage ),
                   "SAVED %s",
                   fileName );
    }
    else
    {
        sprintf_s( replayRuntime.Scrubber().saveMessage,
                   sizeof( replayRuntime.Scrubber().saveMessage ),
                   "REPLAY SAVE FAILED" );
    }
    replayRuntime.Scrubber().saveMessageUntil = now + 2.5;
    replayRuntime.Scrubber().visibleUntil = now + REPLAY_SCRUBBER_VISIBLE_SECONDS;
    replayRuntime.Scrubber().visible = true;
    return saved;
}
