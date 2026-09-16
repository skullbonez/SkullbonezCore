"""Capture current-build wall data for native UI tests without editing the shipped archive."""
from __future__ import annotations

import hashlib
import json
import tempfile
from pathlib import Path

from physics_ab_capture import capture

REPO = Path(__file__).resolve().parents[1]


def current_wall_comparison(executable: Path | None = None) -> Path:
    """Reuse only an immutable fixture produced by these exact inputs and binary."""
    executable = (executable or REPO / "Automation/SKULLBONEZ_CORE.exe").resolve()
    scene = REPO / "SkullbonezData/solver-lab/wall-only/inputs/scene.scene.json"
    signature = hashlib.sha256(b"native-wall-ui-fixture-v1:240-ticks")
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
