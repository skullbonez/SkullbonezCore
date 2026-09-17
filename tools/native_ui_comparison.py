"""Capture current-build wall data for native UI tests without editing the shipped archive."""
from __future__ import annotations

import hashlib
import json
import tempfile
import time
from pathlib import Path

from physics_ab_capture import capture

REPO = Path(__file__).resolve().parents[1]


def current_wall_comparison(executable: Path | None = None) -> Path:
    return current_comparison("wall-only", executable)


def current_library_comparisons() -> tuple[Path, Path]:
    """Keep both real library routes and their distinct scene identities."""
    fixtures = current_comparison("ragdoll-wall"), current_comparison("wall-only")
    metadata = [json.loads(path.read_text(encoding="utf-8")) for path in fixtures]
    scene_hashes = [row["inputs"]["files"]["scene.scene.json"] for row in metadata]
    assert scene_hashes[0] != scene_hashes[1], "Library choices must exercise distinct scenes"
    assert metadata[0]["sides"][0]["bodyCount"] != metadata[1]["sides"][0]["bodyCount"]
    for choice, digest in zip(("ragdoll-wall", "wall-only"), scene_hashes):
        source = REPO / "SkullbonezData/solver-lab" / choice / "inputs/scene.scene.json"
        assert hashlib.sha256(source.read_bytes()).hexdigest() == digest
    return fixtures


def current_comparison(choice: str, executable: Path | None = None) -> Path:
    """Reuse only an immutable fixture produced by these exact inputs and binary."""
    executable = (executable or REPO / "Automation/SKULLBONEZ_CORE.exe").resolve()
    if choice not in ("ragdoll-wall", "wall-only"):
        raise ValueError("Unknown native comparison fixture")
    scene = REPO / "SkullbonezData/solver-lab" / choice / "inputs/scene.scene.json"
    signature = hashlib.sha256(("native-ui-fixture-v2:240-ticks:" + choice).encode("utf-8"))
    inputs = [executable, *sorted(executable.parent.glob("*.dll")),
              *sorted(p for p in (REPO / "SkullbonezData").rglob("*") if p.is_file())]
    for path in inputs:
        signature.update(str(path.relative_to(REPO)).encode("utf-8"))
        signature.update(b"\0")
        with path.open("rb") as stream:
            signature.update(hashlib.file_digest(stream, "sha256").digest())
    # Archived inputs already contain nested data paths. Keep the cache prefix
    # short so capture retries stay below Windows path limits.
    key = REPO / "TestOutput/ui" / signature.hexdigest()
    key.mkdir(parents=True, exist_ok=True)
    for manifest in sorted(key.glob("a-*/capture/comparison.json")):
        try:
            if json.loads(manifest.read_text(encoding="utf-8")).get("status") == "complete":
                return manifest
        except (OSError, ValueError):
            continue
    # A failed attempt remains evidence. The recorder requires a new directory,
    # so retries get a new child rather than overwriting an incomplete capture.
    output = Path(tempfile.mkdtemp(prefix="a-", dir=key)) / "capture"
    # These are two fresh captures of one current build. They exercise UI camera,
    # identity and timeline behavior; they are not a replacement Physics baseline.
    # The shipped A/B recordings and their original input hashes stay immutable.
    return capture(executable, executable, scene, output, seconds=2.0)


def wait_for_comparison(send, sample, fixture: Path, label: str):
    """Pixels of a selected workspace are evidence only after admission succeeds."""
    deadline = time.monotonic() + 90
    while True:
        ui = sample(label)
        state = send("comparison.state")["result"]["comparison"]
        assert not state["loadError"], state
        if state["active"] and not state["loading"]:
            assert Path(state["bundle"]).resolve() == fixture.resolve(), state
            return ui
        assert time.monotonic() < deadline, state
