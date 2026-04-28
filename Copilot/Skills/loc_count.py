"""
loc_count.py — Count logical lines of code in SkullbonezSource/.
Excludes:
  - Blank lines (whitespace only)
  - Single-line comments  (// ...)
  - Block comment lines   (/* ... */ — any line that is purely inside or opening/closing a block comment)
  - Preprocessor lines are counted (they are executable intent, e.g. #include, #define)
Prints per-file counts and a grand total.
"""

import os
import re
import sys
import glob

def count_loc(path: str) -> int:
    loc = 0
    in_block = False

    with open(path, encoding='utf-8-sig', errors='replace') as fh:
        for raw in fh:
            line = raw.strip()

            if not line:
                continue

            if in_block:
                if '*/' in line:
                    in_block = False
                    remainder = line[line.index('*/') + 2:].strip()
                    if remainder and not remainder.startswith('//'):
                        loc += 1
                # still inside block comment — don't count
                continue

            # Does this line open a block comment?
            if '/*' in line:
                before = line[:line.index('/*')].strip()
                after_open = line[line.index('/*') + 2:]
                if '*/' in after_open:
                    # Inline block comment: /* ... */ on same line — count if there's real code outside
                    outer = before + ' ' + after_open[after_open.index('*/') + 2:]
                    outer = outer.strip()
                    if outer and not outer.startswith('//'):
                        loc += 1
                else:
                    in_block = True
                    if before and not before.startswith('//'):
                        loc += 1
                continue

            if line.startswith('//'):
                continue

            loc += 1

    return loc


def main():
    repo = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    src_dir = os.path.join(repo, 'SkullbonezSource')  # first-party only; ThirdPtySource is excluded

    files = sorted(
        glob.glob(os.path.join(src_dir, '*.h')) +
        glob.glob(os.path.join(src_dir, '*.cpp'))
    )

    if not files:
        print(f'No source files found in {src_dir}')
        sys.exit(1)

    results = []
    for f in files:
        n = count_loc(f)
        results.append((os.path.basename(f), n))

    results.sort(key=lambda x: -x[1])
    total = sum(n for _, n in results)

    name_w = max(len(r[0]) for r in results)
    print(f'\n{"File":<{name_w}}  LOC')
    print('-' * (name_w + 6))
    for name, n in results:
        print(f'{name:<{name_w}}  {n:>4}')
    print('-' * (name_w + 6))
    print(f'{"TOTAL":<{name_w}}  {total:>4}')
    print()


if __name__ == '__main__':
    main()
