"""
File: Agentic/Skills/collapse_params.py
Purpose:
  Collapse balanced multiline C++ parameter lists before clang-format runs.

Summary:
  The formatter joins continuation lines while parentheses remain open, strips
  comments that would swallow joined code, and leaves lambda signatures alone
  so this pass and clang-format cannot oscillate over the same construct.

Invariants:
  - Only tracked .cpp and .h text under SkullbonezSource is considered.
  - Lambda captures are not collapsed.
"""
"""
File: Agentic/Skills/collapse_params.py
Purpose:
  Collapses multiline parameter lists in top-level SkullbonezSource headers.

Summary:
  This maintenance helper performs one deliberately narrow lexical rewrite over
  root headers. It joins a parenthesized declaration only until parentheses
  balance and leaves opening lines that contain lambda captures to clang-format.

Invariants:
  - The scan is bounded to `SkullbonezSource/*.h`; it never descends into
    subsystem directories.
  - A changed file is replaced only after its complete updated text is built.
  - An opening line containing a lambda capture remains untouched to avoid
    formatter oscillation.

Related:
  - Agentic/Reference/code-style-guide.md
  - tools/format_fix.bat
"""

import glob
import os
import re

SOURCE_DIR = os.path.join(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))), "SkullbonezSource")

def collapse_multiline_params(content):
    """
    Collapse multi-line function parameter lists to a single line.
    When a line has more open parens than close parens,
    join continuation lines until parens balance.
    """
    lines = content.split('\n')
    result = []
    i = 0
    while i < len(lines):
        line = lines[i]
        # Hazard: this is a lexical parenthesis counter, not a C++ parser. Strip
        # line comments before balancing so comment punctuation cannot consume a
        # later declaration; the deliberately narrow file scope limits exposure.
        code_part = re.sub(r'//.*$', '', line)
        depth = code_part.count('(') - code_part.count(')')
        # Skip collapsing lines that contain a lambda capture [...]() — clang-format
        # owns lambda body layout and will re-expand any single-line collapse,
        # creating an oscillation cycle between collapse_params and clang-format.
        has_lambda = bool(re.search(r'\[.*?\]\s*\(', code_part))
        if depth > 0 and not has_lambda:
            # Strip trailing comment from the opening line too —
            # if it has one, it would comment-out everything after the join.
            combined = re.sub(r'\s*//.*$', '', line).rstrip()
            while depth > 0 and i + 1 < len(lines):
                i += 1
                next_line = lines[i]
                next_code = re.sub(r'//.*$', '', next_line)
                next_depth = next_code.count('(') - next_code.count(')')
                stripped = next_line.strip()
                if next_depth + depth <= 0:
                    # Closing line — keep with its trailing comment (e.g. // description)
                    combined = combined + ' ' + stripped
                else:
                    # Intermediate line — strip trailing comment before merging
                    stripped_no_comment = re.sub(r'\s*//.*$', '', stripped)
                    combined = combined + ' ' + stripped_no_comment
                depth += next_depth
            result.append(combined)
        else:
            result.append(line)
        i += 1
    return '\n'.join(result)

all_files = (glob.glob(os.path.join(SOURCE_DIR, '*.h')))

count = 0
for filepath in all_files:
    with open(filepath, 'r', encoding='utf-8', errors='replace') as f:
        original = f.read()
    updated = collapse_multiline_params(original)
    if updated != original:
        with open(filepath, 'w', encoding='utf-8', newline='') as f:
            f.write(updated)
        count += 1
        print(f"  collapsed: {os.path.basename(filepath)}")

print(f"\nDone - {count} files updated")
