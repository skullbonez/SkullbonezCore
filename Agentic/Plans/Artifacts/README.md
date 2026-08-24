# Physics Golden Transition Artifacts

Physics plans may update any golden they govern without a per-transition owner
prompt. This standing authorization covers Physics CSV, known-issue, SkullScope,
replay, visual, causal, and performance goldens when the active Physics phase owns the
behavior change. It does not authorize blind refreshes, hide unexplained drift,
or change the approval rules for non-Physics work.

Every replacement is content-bound and archive-bound. Before writing a tracked
golden, preserve the exact old and new launch payloads under:

```text
Agentic/Plans/Artifacts/<physics-plan>/<phase>/golden-transitions/<transition-id>/
```

The directory is append-only. A later transition creates a different
`<transition-id>`; no file in a committed transition directory may be edited,
replaced, or deleted. Copy every executable needed to reproduce each behavior
and every non-system DLL reported by the recorded dependency scan. The new
producing executable is retained as the old-behavior comparison executable for
the next transition. Use `git add -f` for the explicitly retained `.exe` and
`.dll` files hidden by global artifact ignore rules.

## Manifest Schema

Each transition directory contains `manifest.json`. `physics_plan` names the
active tracked plan under `Agentic/Plans/TODO/`. Paths are repository relative,
hashes are lowercase SHA-256, sizes are exact bytes, and commit hashes are full
40-character source-parent revisions. `golden_sha256` repeats the
complete golden map for each behavior so no retained executable can be
mistakenly associated with a different transition.

```json
{
  "schema_version": 1,
  "physics_plan": "Agentic/Plans/TODO/ragdoll-physics-unification.md",
  "phase": "FP3",
  "transition_id": "<old-prefix>-to-<new-prefix>",
  "source_commit": "<source-parent-commit>",
  "goldens": [
    {
      "path": "TestOutput/baselines/physics_regression_varied.csv",
      "old_sha256": "<64-lowercase-hex>",
      "new_sha256": "<64-lowercase-hex>"
    }
  ],
  "old_behavior": {
    "source_commit": "<old-source-commit>",
    "golden_sha256": {
      "TestOutput/baselines/physics_regression_varied.csv": "<old-sha256>"
    },
    "executables": [
      {
        "configuration": "Debug|x64",
        "path": "Agentic/Plans/Artifacts/<physics-plan>/<phase>/golden-transitions/<transition-id>/old/SKULLBONEZ_CORE-Debug.exe",
        "size": 14052352,
        "sha256": "<executable-sha256>",
        "launch_command": "old/SKULLBONEZ_CORE-Debug.exe <exact arguments>",
        "dependency_scan_command": "dumpbin /DEPENDENTS old/SKULLBONEZ_CORE-Debug.exe",
        "required_dlls": [
          {
            "path": "Agentic/Plans/Artifacts/<physics-plan>/<phase>/golden-transitions/<transition-id>/old/WinPixEventRuntime.dll",
            "size": 58368,
            "sha256": "<dll-sha256>"
          }
        ]
      }
    ]
  },
  "new_behavior": {
    "source_commit": "<new-source-parent-commit>",
    "golden_sha256": {
      "TestOutput/baselines/physics_regression_varied.csv": "<new-sha256>"
    },
    "executables": [
      {
        "configuration": "Debug|x64",
        "path": "Agentic/Plans/Artifacts/<physics-plan>/<phase>/golden-transitions/<transition-id>/new/SKULLBONEZ_CORE-Debug.exe",
        "size": 14052352,
        "sha256": "<executable-sha256>",
        "launch_command": "new/SKULLBONEZ_CORE-Debug.exe <exact arguments>",
        "dependency_scan_command": "dumpbin /DEPENDENTS new/SKULLBONEZ_CORE-Debug.exe",
        "required_dlls": []
      }
    ]
  }
}
```

Use one `goldens[]` row per golden changed by the phase. Old and new behavior
maps must contain that exact path set. An empty `required_dlls` array asserts
that the recorded dependency scan found no non-system runtime DLL to retain.

## Core Physics Command

The core varied-scene writer has no interactive lane:

```bat
python tools\check_physics_baseline_guard.py --repo . ^
  --automated-override-output Debug\physics_regression_varied.csv ^
  --candidate-sha256 <exact-candidate-sha256> ^
  --artifact-manifest Agentic\Plans\Artifacts\<physics-plan>\<phase>\golden-transitions\<transition-id>\manifest.json
```

The old `--approve-output` and `--owner-approved-sha256` spellings remain aliases
for command compatibility; new documentation and automation use the names
above. The guard verifies the candidate, manifest, old and new golden hashes,
retained executables, required DLLs, and the byte-identical retained copy of the
new producing executable before it writes. The staged guard repeats the checks
from Git-index bytes and rejects any mutation of an older transition bundle.

For a Physics-plan golden without a domain-specific serialized writer, use the
same guard with a separate generated candidate:

```bat
python tools\check_physics_baseline_guard.py --repo . ^
  --automated-override-file Debug\candidate.csv ^
  --golden-path TestOutput/baselines/<golden> ^
  --candidate-sha256 <exact-candidate-sha256> ^
  --artifact-manifest <transition-manifest.json> ^
  --producing-executable Debug\SKULLBONEZ_CORE.exe ^
  --configuration "Debug|x64"
```

This generic lane covers deep CSV and performance goldens when an active
Physics phase owns them. It refuses the core CSV, which must also update its
tracked compatibility record through the specialized command above.

Physics-plan query, known-issue, replay, visual, and causal writers expose the
same exact-candidate and manifest requirements through their documented
automated-override flags. After any update, rerun the complete matching gate and
commit source, tests, goldens, and the transition bundle atomically.
