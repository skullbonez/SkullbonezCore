"""
loc_count.py - Count logical lines of code in SkullbonezSource/.

Excludes:
  - Blank lines (whitespace only)
  - Single-line comments (// ...)
  - Block comment lines (/* ... */ - any line that is purely inside or
    opening/closing a block comment)
  - Preprocessor lines are counted because they carry executable intent.

Prints per-file counts and a grand total.
"""

import sys
from pathlib import Path


def count_loc(path: Path) -> int:
    loc = 0
    in_block = False

    with path.open(encoding="utf-8-sig", errors="replace") as fh:
        for raw in fh:
            line = raw.strip()

            if not line:
                continue

            if in_block:
                if "*/" in line:
                    in_block = False
                    remainder = line[line.index("*/") + 2 :].strip()
                    if remainder and not remainder.startswith("//"):
                        loc += 1
                continue

            if "/*" in line:
                before = line[: line.index("/*")].strip()
                after_open = line[line.index("/*") + 2 :]
                if "*/" in after_open:
                    outer = before + " " + after_open[after_open.index("*/") + 2 :]
                    outer = outer.strip()
                    if outer and not outer.startswith("//"):
                        loc += 1
                else:
                    in_block = True
                    if before and not before.startswith("//"):
                        loc += 1
                continue

            if line.startswith("//"):
                continue

            loc += 1

    return loc


def source_files(src_dir: Path) -> list[Path]:
    extensions = {".cpp", ".h"}
    return sorted(
        (
            path
            for path in src_dir.rglob("*")
            if path.is_file() and path.suffix.lower() in extensions
        ),
        key=lambda path: str(path.relative_to(src_dir)).lower(),
    )


def main() -> int:
    repo = Path(__file__).resolve().parents[2]
    src_dir = repo / "SkullbonezSource"
    files = source_files(src_dir)

    if not files:
        print(f"No source files found in {src_dir}")
        return 1

    results = []
    for source_file in files:
        loc = count_loc(source_file)
        results.append((str(source_file.relative_to(src_dir)), loc))

    results.sort(key=lambda item: (-item[1], item[0].lower()))
    total = sum(loc for _, loc in results)

    name_w = max(len(name) for name, _ in results)
    print(f'\n{"File":<{name_w}}  LOC')
    print("-" * (name_w + 6))
    for name, loc in results:
        print(f"{name:<{name_w}}  {loc:>4}")
    print("-" * (name_w + 6))
    print(f'{"TOTAL":<{name_w}}  {total:>4}')
    print()
    return 0


if __name__ == "__main__":
    sys.exit(main())
