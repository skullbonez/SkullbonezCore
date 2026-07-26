#!/usr/bin/env python3
"""One-shot seed generator for tools/aggregate_ownership_rulings.json.

Run once during governance-shape-to-judgment-conversion G2 to build the initial
ruling file from live inventory output, so the 97 rows are not transcribed by
hand. Owners edit the JSON directly afterwards; this script is not a gate and is
deleted when G4 closes.
"""

from __future__ import annotations

import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import inventory_authority_free_aggregates as agg
import inventory_extraction_scars as scars

REPO = Path(".").resolve()

# Owner rulings decided during G0. Anything not named here defaults to `repair`
# owned by the plan that censuses it.
AGGREGATE_RULINGS = {
    "TornadoUICommandContext": (
        "remove",
        "ceremonial-aggregate-elimination CA1",
        "One `SceneWorld&` member. Identical in shape to PhysicsSleepPolicyUICommandContext; "
        "the two differ only by name and comment. Apply operations take SceneWorld& directly.",
    ),
    "PhysicsSleepPolicyUICommandContext": (
        "remove",
        "ceremonial-aggregate-elimination CA1",
        "One `SceneWorld&` member, duplicating TornadoUICommandContext exactly.",
    ),
    "SceneRuntimeCreateContext": (
        "remove",
        "ceremonial-aggregate-elimination CA2",
        "One `SceneController&` member, passed by value into CreateSceneFromUI. Strictly longer "
        "than taking SceneController& and shortens nothing.",
    ),
    "SceneAuthoredCameraContext": (
        "remove",
        "ceremonial-aggregate-elimination CA2",
        "Single-member authored-setup context; the setup operation takes its one owner directly.",
    ),
    "SceneGeneratedCameraContext": (
        "remove",
        "ceremonial-aggregate-elimination CA2",
        "Single-member generated-setup context; the setup operation takes its one owner directly.",
    ),
    "AssetContext": (
        "remove",
        "ceremonial-aggregate-elimination CA3",
        "One nullable `const AssetSystem*`. The nullable-registry fallback it documents is a "
        "parameter default, not an invariant, so the operations take the pointer directly.",
    ),
    "ShadowGraphInputs": (
        "retain-prior",
        "concrete-parameter-bag-elimination PB0",
        "PB0 Explicit Retain Ruling: the ten private *GraphInputs each feed one distinct concrete "
        "graph node and do not converge on an apply transaction. Single-member by design as a "
        "render-graph ABI thunk payload.",
    ),
    "ReflectionGraphInputs": (
        "retain-prior",
        "concrete-parameter-bag-elimination PB0",
        "PB0 Explicit Retain Ruling: see ShadowGraphInputs.",
    ),
}

SCAR_RULINGS = {
    "SkullbonezSource/Core/WorkerPool.h:indexFn": (
        "retain",
        "core threading",
        "`IndexFunctionT& indexFn = fn;` binds a forwarding reference to an lvalue reference so the "
        "chunk lambda can capture it by reference. Deleting the alias changes capture semantics; "
        "this is a required binding, not a preserved pre-extraction spelling.",
    ),
}

SCAR_DEFAULT_OWNER = "extraction-scar-remediation ES0"


def main() -> int:
    aggregates = agg.collect_aggregates(REPO, agg.DEFAULT_ROOTS)
    flagged = sorted(name for name, item in aggregates.items() if item.signals())

    aggregate_rows = []
    for name in flagged:
        item = aggregates[name]
        verdict, owner, reason = AGGREGATE_RULINGS.get(
            name,
            ("repair", "ceremonial-aggregate-elimination CA0", "Unruled at seed time; CA0 must rule this row."),
        )
        aggregate_rows.append(
            {
                "key": name,
                "site": f"{item.path}:{item.line}",
                "members": item.member_count,
                "verdict": verdict,
                "owner": owner,
                "reason": reason,
            }
        )

    scar_rows = []
    for scar in scars.scan_repo(REPO, scars.DEFAULT_ROOTS):
        verdict, owner, reason = SCAR_RULINGS.get(
            scar.key(),
            (
                "repair",
                SCAR_DEFAULT_OWNER,
                "Member-prefixed local or pure parameter alias preserving a pre-extraction "
                "spelling; rename or delete so the local is scope-honest."
                if scar.kind == "member-prefixed-local"
                else "Reference declaration that is only a second name for a parameter; use the "
                "parameter directly.",
            ),
        )
        scar_rows.append(
            {
                "key": scar.key(),
                "site": f"{scar.path}:{scar.line}",
                "kind": scar.kind,
                "verdict": verdict,
                "owner": owner,
                "reason": reason,
            }
        )

    payload = {
        "schema": 1,
        "purpose": (
            "Owner rulings for the two governance shape inventories. A row records a judgement, "
            "not a budget: no count in this file is a threshold, and adding a row is never a way "
            "to raise an allowance. An unruled finding fails the gate so the judgement cannot be "
            "skipped; a ruled finding passes because an owner has answered for it."
        ),
        "verdicts": {
            "remove": "Authority-free; delete it and let callers take the real owners.",
            "retain": "Owns a real invariant or binding requirement stated in the reason.",
            "retain-prior": "Already ruled by a named earlier plan; carried forward unchanged.",
            "repair": "Confirmed defect owned by the named plan; not yet implemented.",
        },
        "aggregates": aggregate_rows,
        "extraction_scars": sorted(scar_rows, key=lambda row: row["key"]),
    }

    out = REPO / "tools" / "aggregate_ownership_rulings.json"
    out.write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8")
    print(f"wrote {out} aggregates={len(aggregate_rows)} scars={len(scar_rows)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
