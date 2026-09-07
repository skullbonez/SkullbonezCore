"""Reject damaged comparison evidence without replacing a loaded inspection."""
from pathlib import Path
import argparse
import json
import os
import shutil
import hashlib
import struct

from skarness import SkarnessConnection, launch
from physics_ab_capture import load_actions


def main() -> None:
    root = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bundle", type=Path, required=True)
    parser.add_argument("--session", type=Path, required=True)
    args = parser.parse_args()
    session = args.session.resolve()
    session.mkdir(parents=True, exist_ok=False)
    bundle = args.bundle.resolve()
    metadata = json.loads(bundle.read_text())
    launch(session, root / "Automation/SKULLBONEZ_CORE.exe", None, hidden=True, fixed_step=True)
    connection = SkarnessConnection(session)
    results = []

    def command(name: str, arguments: dict | None = None, applied: bool = True) -> dict:
        result = connection.wait(connection.send(name, arguments or {}))
        results.append(result)
        assert (result.get("status") == "applied") == applied, result
        return result

    try:
        capabilities = connection.wait(connection.send("capabilities.get"))
        assert "comparison.load" in capabilities["commands"]
        command("state.subscribe", {"topics": [], "detail": "summary"})
        command("comparison.load", {"path": str(bundle)})
        command("comparison.seek", {"tick": 1})
        for case in ("hash", "capacity", "header", "count", "version"):
            target = session / case
            target.mkdir()
            # Fixtures share bytes read-only. Mutations affect only newly written
            # manifests or a new sparse file, never a link to recorded evidence.
            shutil.copytree(bundle.parent / "inputs", target / "inputs", copy_function=os.link)
            for side_name in ("A", "B"):
                diagnostic = bundle.parent / side_name / "physics.physicsdiag.ndjson"
                if diagnostic.exists():
                    (target / side_name).mkdir()
                    os.link(diagnostic, target / side_name / diagnostic.name)
            candidate = json.loads(json.dumps(metadata))
            for side, row in enumerate(candidate["sides"]):
                if case == "capacity" and side == 1:
                    with (target / row["path"]).open("wb") as stream:
                        stream.truncate(512 * 1024 * 1024)
                elif case == "count" and side == 1:
                    data = bytearray((bundle.parent / row["path"]).read_bytes())
                    count = struct.unpack_from("<I", data, 16)[0]
                    table = struct.unpack_from("<Q", data, 24)[0]
                    for index in range(count):
                        entry = table + 28 * index
                        if data[entry:entry + 4] == b"BODY":
                            payload = struct.unpack_from("<Q", data, entry + 4)[0]
                            struct.pack_into("<I", data, entry + 20, 0xffffffff)
                            struct.pack_into("<I", data, payload, 0xffffffff)
                            break
                    else:
                        raise AssertionError("Fixture has no body dictionary")
                    (target / row["path"]).write_bytes(data)
                    row["sha256"] = hashlib.sha256(data).hexdigest()
                else:
                    os.link(bundle.parent / row["path"], target / row["path"])
            if case == "hash":
                candidate["sides"][1]["sha256"] = "0" * 64
            elif case == "version":
                candidate["sides"][1]["version"] = 3
            elif case == "header":
                candidate["format"] = 17
            path = target / "comparison.json"
            path.write_text(json.dumps(candidate))
            rejected = command("comparison.load", {"path": str(path)}, applied=False)
            expected = {"hash": "Recording hash", "capacity": "available loading capacity", "header": "Unsupported", "count": "Recording contains", "version": "available loading capacity"}[case]
            assert expected in rejected.get("reason", ""), rejected
            state = command("comparison.state")["result"]["comparison"]
            assert state["tick"] == 1 and state["lastTick"] == metadata["ticks"], state

        finding = session / "finding.json"
        command("comparison.finding.save", {"path": str(finding), "note": "integrity fixture"})
        damaged = json.loads(finding.read_text())
        damaged["builds"][1] = "incorrect producer"
        finding.write_text(json.dumps(damaged))
        command("comparison.finding.load", {"path": str(finding)}, applied=False)
        damaged["settings"]["showA"] = []
        finding.write_text(json.dumps(damaged))
        command("comparison.finding.load", {"path": str(finding)}, applied=False)

        actions = session / "actions.json"
        for row in ({"tick": -1, "command": "scene.object.select"},
                    {"tick": 0, "command": "scene.reset"}):
            actions.write_text(json.dumps([row]))
            try:
                load_actions(actions, 24)
            except ValueError:
                pass
            else:
                raise AssertionError(f"Unsupported capture action accepted: {row}")
        print("COMPARISON INTEGRITY PASS: hashes, capacity, malformed input, findings, actions")
    finally:
        (session / "checks.json").write_text(json.dumps(results, indent=2))
        try:
            command("session.stop")
        finally:
            connection.close()


if __name__ == "__main__":
    main()
