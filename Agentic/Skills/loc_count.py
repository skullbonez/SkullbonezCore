"""
File: loc_count.py
Purpose:
  Counts logical engine lines of code and prints the largest files plus a total.

Mental model:
  The report answers two separate questions: which engine files are currently
  largest, and how much first-party C/C++/HLSL-style code exists in total. It
  counts implementation-bearing source, headers, inline fragments, and shaders.

Glossary:
  LOC (Lines Of Code): Non-blank, non-comment lines counted as implementation.
  HLSL (High-Level Shader Language): Shader source compiled for DirectX.
  Inline fragment: A .inl file included by another C++ source/header rather than
    compiled as an independent translation unit.

Invariants:
  - The main total covers SkullbonezSource .cpp/.h/.inl and shader .hlsl files.
  - Comment stripping follows the same simple C/C++ comment rules as the footer
    in tools/loc_count.bat, so the two totals stay comparable.

Related:
  - tools/loc_count.bat adds tracked-file category and subtotal reporting.
"""

import sys
from pathlib import Path

TOP_FILE_LIMIT = 10


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


def source_files(repo: Path) -> list[Path]:
    roots_and_extensions = (
        (repo / "SkullbonezSource", {".cpp", ".h", ".inl"}),
        (repo / "SkullbonezData" / "shaders", {".hlsl"}),
    )

    files = []
    for root, extensions in roots_and_extensions:
        if not root.exists():
            continue
        files.extend(
            path
            for path in root.rglob("*")
            if path.is_file() and path.suffix.lower() in extensions
        )

    return sorted(files, key=lambda path: str(path.relative_to(repo)).lower())


def main() -> int:
    repo = Path(__file__).resolve().parents[2]
    files = source_files(repo)

    if not files:
        print(f"No engine code files found in {repo}")
        return 1

    results = []
    for source_file in files:
        loc = count_loc(source_file)
        results.append((str(source_file.relative_to(repo)), loc))

    results.sort(key=lambda item: (-item[1], item[0].lower()))
    total = sum(loc for _, loc in results)
    top_results = results[:TOP_FILE_LIMIT]

    name_w = max(len(name) for name, _ in top_results)
    print(f"\nTop {TOP_FILE_LIMIT} engine code files by logical LOC")
    print(f'\n{"File":<{name_w}}  LOC')
    print("-" * (name_w + 6))
    for name, loc in top_results:
        print(f"{name:<{name_w}}  {loc:>4}")
    print("-" * (name_w + 6))
    print(f'{"TOTAL (all counted engine code)":<{name_w}}  {total:>4}')
    print()
    return 0


if __name__ == "__main__":
    sys.exit(main())
