"""
File: Agentic/Skills/loc_count.py
Purpose:
  Counts logical C++ source lines beneath SkullbonezSource.

Summary:
  The report is a deterministic, read-only size inventory: it walks `.cpp` and
  `.h` files, excludes blank and comment-only text, retains preprocessor lines,
  and prints one bounded row per discovered source file plus a total.

Glossary:
  Logical source line: A nonblank line with code or a preprocessor directive
    after whole-line C++ comments are excluded.

Invariants:
  - Files are sorted by repository-relative path before counting and by
    descending logical-line count before reporting.
  - Decode errors replace invalid bytes rather than skipping a source file.
  - The tool never writes repository content.

Related:
  - Agentic/Reference/code-style-guide.md
  - tools/check_source_design.py
"""

import sys
from pathlib import Path


def count_loc(path: Path) -> int:
    # Hazard: this is intentionally a line-oriented inventory, not a C++ lexer.
    # It tracks multiline comments but does not reinterpret comment tokens inside
    # string literals; use compiler-backed inventories for semantic conclusions.
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
