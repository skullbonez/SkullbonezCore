"""Generate or verify the deterministic 5,000-body sleeping scale fixture.

The fixture deliberately uses only the current version-2 authored body schema:
4,000 rows begin asleep and 1,000 high-altitude rows remain in flight for the
bounded perf run. Wide X/Z spacing avoids knife-edge contacts. One isolated
large-radius sleeper fixes broadphase cell size at the existing 24-unit cap so
the scene stays inside SpatialGrid's ratified 8,192-bucket envelope.
"""

from __future__ import annotations

import argparse
import json
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parent.parent
SCENE_PATH = REPO_ROOT / "SkullbonezData" / "scenes" / "physics_scale_sleepy_5000.scene.json"
SLEEPING_BODY_COUNT = 4_000
AWAKE_BODY_COUNT = 1_000


def _body(index: int) -> dict:
    sleeping = index < SLEEPING_BODY_COUNT
    if sleeping and index == 0:
        radius = 12.0
        position = [-5_000.0, 100.0, -5_000.0]
    elif sleeping:
        radius = 1.0
        position = [float((index % 80) * 20), 100.0, float((index // 80) * 20)]
    else:
        awake_index = index - SLEEPING_BODY_COUNT
        radius = 1.0
        position = [
            float(3_000 + (awake_index % 40) * 20),
            float(10_000 + (awake_index % 7) * 3),
            float((awake_index // 40) * 20),
        ]

    velocity = [0.0, 0.0, 0.0] if sleeping else [2.0, 0.0, -1.0]
    inertia = 0.4 * radius * radius
    return {
        "type": "ballState",
        "sceneObjectId": index + 1,
        "name": f"scale_{'sleep' if sleeping else 'awake'}_{index:04d}",
        "position": position,
        "velocity": velocity,
        "angularVelocity": [0.0, 0.0, 0.0],
        "orientation": [0.0, 0.0, 0.0, 1.0],
        "radius": radius,
        "mass": 1.0,
        "restitution": 0.0,
        "inertia": [inertia, inertia, inertia],
        "fixed": False,
        "sleeping": sleeping,
    }


def _scene() -> dict:
    return {
        "format": "skullbonez.scene.json",
        "version": 2,
        "cinematic": {},
        "simulation": {
            "seed": 3235774467,
            "physics": True,
            "text": False,
            "modelCapacity": 6_000,
        },
        "runtime": {"vsync": False, "pipelineSync": False},
        "playback": {
            "fixedStep": True,
            "frames": 600,
            "pauseSnapshotState": False,
            "exitOnComplete": True,
        },
        "debug": {"waterHidden": True, "terrainHidden": True},
        "cameras": [
            {
                "name": "scale_overview",
                "position": [321, 110, 557],
                "view": [581, 40, 633],
                "up": [0, 1, 0],
            }
        ],
        "logging": {
            "perfLog": "Profile/physics_scale_sleepy_5000_perf_log.csv",
            "perfLogFlush": False,
            "perfLogFlushInterval": 0,
        },
        "objects": [_body(index) for index in range(SLEEPING_BODY_COUNT + AWAKE_BODY_COUNT)],
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("--write", action="store_true", help="write the canonical fixture")
    mode.add_argument("--check", action="store_true", help="verify the committed fixture")
    args = parser.parse_args()

    expected = json.dumps(_scene(), indent=2) + "\n"
    if args.check:
        if not SCENE_PATH.exists() or SCENE_PATH.read_text(encoding="utf-8") != expected:
            print(f"FAIL: regenerate {SCENE_PATH.relative_to(REPO_ROOT)} with --write")
            return 1
        print(
            f"PASS: sleepy scale fixture has {SLEEPING_BODY_COUNT} sleepers, "
            f"{AWAKE_BODY_COUNT} awake bodies, and capacity 6000."
        )
        return 0

    SCENE_PATH.write_text(expected, encoding="utf-8")
    print(f"Wrote {SCENE_PATH.relative_to(REPO_ROOT)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
